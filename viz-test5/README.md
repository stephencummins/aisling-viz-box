# viz-test5

Test 5 for the AISLING visualiser box (Waveshare RP2350-PiZero, ELEGOO 3.5" SPI screen on header pins 1–26,
the FM-1 on the lower "PIO-USB" socket). Test 4 settled the picture: gamma FALLING, warmth 2, starting on the
landscape. But it drew about 5 frames a second: the paint's step took 53 ms, then sending the changed tiles took
139 ms, one after the other. This one makes it smoother. It changes how fast, not what: given the same steps, every
pixel it sends is the one test 4 would have sent, and the host checks compare the two bit for bit.

1. **The work done every frame runs from RAM**, not flash (`hot.h`): the step, the colours, the render and the
   screen's DMA calls. Flash goes through a cache that core 1's USB host shares, so a miss costs both cores.
2. **The render does four pixels at a time through tables** (`land.c`, `render_fast`). On the Mac it's 2.7 times
   as fast as test 4's (139 µs a screen against 374). The board should gain more, because the floating-point maths
   the tables replace costs more there. Anything the tables don't cover (a strip not on a 4-pixel boundary) goes
   through test 4's render, unchanged (`render_ref`).
3. **The screen keeps going while the paint steps.** The step and the prepare stop after every row of paint and
   call `pump()`, which starts the next run of tiles on the DMA and renders the one after into the other half of
   a double buffer. Before, the screen sat idle through the step, and the step waited for the screen.
4. **The log breaks a frame down** (below), so the next step is a measurement, not a guess.

Everything else is test 4's: gamma FALLING at every start-up, warmth 2, the calibration screen and its buttons,
starting on the landscape, the keys. Nothing is stored; a power cycle starts the same way.

Our own code throughout, on pico-sdk 2.3.1, its TinyUSB 0.18.0 and Pico-PIO-USB 0.7.3.

## What to expect

At 20 MHz, about 380 changed tiles a frame are about 78 ms of SPI, so the screen alone allows about 12 frames a
second. The step and the render now happen while that SPI is going, so the hope is **about 10–12 frames a second,
twice test 4's**. The paint moves at the same speed in real time (each step is sized to the time since the last),
in smaller steps. The status line's frame count shows it straight away.

## Reading the log

    python3 -I log.py /dev/cu.usbmodem101 10

The snapshot now has two lines like these (averages over the last few seconds):

    frames: 11 a second; step 38 ms, prepare 4, the rest of the tiles 47 (waiting on the screen 30)
      rendering 21 ms of those; 380 tiles of 510 a frame (last 371), 77 ms of SPI at this speed

(The numbers are made up, to show the shape.)

- **step** and **prepare** include the tiles rendered and started while they ran.
- **the rest of the tiles**: after the prepare, finishing the runs still queued. **waiting on the screen** is the
  part of it spent waiting for the DMA.
- **rendering**: all the time spent rendering tiles, wherever it happened.
- **ms of SPI**: what that many tiles cost on the wire at the current speed.

How to read it. If **waiting on the screen** is a big part of the frame, the SPI is the limit, and `s` tries 30
MHz. In test 2, 30 MHz gave no speed, because drawing each tile was the limit then, and it looked a bit light to
Stephen. Now the drawing is out of the way, so it may help; `s` again moves on to 15 and `f` redraws. If waiting is near nothing and the
**step** is the big number, the processor is the limit. Then the next lever is the clock: it runs at 120 MHz now,
and 144 or 240 MHz would cut the step and the render in proportion. That's Stephen's call; this test doesn't
change it.

## Measured on the board (2026-11-09)

The FM-1 playing; the tile count moves with the music, so compare per tile as well as per frame.

| clock | screen SPI | frames a second | step | prepare | rest (waiting) | rendering | tiles a frame | SPI |
|---|---|---|---|---|---|---|---|---|
| 120 MHz | 20 MHz | 10 | 72 ms | 24 | 2 (1) | 38 | 374 | 76 ms |
| 144 MHz | 18 MHz | 10 | 60 ms | 18 | 30 (20) | 35 | 414 | 94 ms |
| 144 MHz | 24 MHz (`s`) | 10 | 67 ms | 22 | 2 (1) | 39 | 459 | 78 ms |

Test 4 managed 5.2 at 120 MHz. With a fresh picture (about 150 tiles) it's 13 at 120 MHz and 19 at 144.

At 120 MHz the processor is the limit. 144 MHz takes the work down by its sixth (rendering 85 µs a tile against
102), but the SPI can't make 20 MHz from 144: it gets 18 (144 / 8), and then the screen is the limit and nothing is
gained. `s` gives 24 (144 / 6) and the screen stops being the limit again: the last row is busier music (459 tiles
against 374) at the same 10 a second.

## Clock

The system clock is a build option, `VIZ_SYS_MHZ`: 120 (the default, as tests 1–4) or 144. It has to be a
multiple of 12 for PIO-USB, which works its own timing out from it, and the RP2350 is rated to 150. Built at 120,
the uf2 is byte-identical to the one without the option.

    cmake -S . -B build144 -G "Unix Makefiles" -Dpicotool_DIR=/opt/homebrew/lib/cmake/picotool -DVIZ_SYS_MHZ=144
    make -C build144 -j8      # build144/viz_test5.uf2

At 144 MHz the screen SPI starts at 18 MHz; `s` steps it 18 → 24 → 14.4. Nothing is stored, so a power cycle
starts at 18 again.

## Build

    export PATH="$HOME/.pico/arm-gnu-toolchain-14.3.rel1-darwin-arm64-arm-none-eabi/bin:$PATH"
    export PICO_SDK_PATH=$HOME/.pico/pico-sdk
    cmake -S . -B build -G "Unix Makefiles" -Dpicotool_DIR=/opt/homebrew/lib/cmake/picotool
    make -C build -j8

Output: `build/viz_test5.uf2` (181,248 bytes; SHA-256 `d706cc4c…53d70cb707`). No warnings. RAM: the static data
ends at 474 KB of 512 KB, against test 4's 431 KB by the same measure (the tables and the code moved into RAM);
38 KB left for the heap.

## Host checks

Mac, AddressSanitizer + UBSan; `../hosttests/test5/run.sh`. All passed:

- **Test 5's land.c against test 4's**, both built into one program with their names prefixed. The FM-1's logged
  MIDI is replayed for 1,200 frames, three seeds, with uneven steps (big stalls, tiny steps), tempo, palette and
  ground changes and all-notes-off, and every 60 frames a full screen at every warmth step. After every step and
  every prepare, all the paint's state must match byte for byte, the same tiles must be listed dirty, and random
  strips (half on a 4-pixel boundary, half not) must render identically. Meanwhile, from inside test 5's step and
  prepare, random strips are rendered, as `pump()` does on the board, to show that rendering mid-step changes
  nothing. 55,512 checks, at each of fp-contract off, on and fast.
- **main.c's own pump code** (lifted out of main.c by the script, not copied) against a stand-in DMA that takes
  a random time and checks the buffer being sent never changes while it's sent. No window goes out of bounds, no
  push starts while one is going or on the calibration screen, and after each warmth change, palette, ground,
  calibration and back, the screen ends up exactly the picture `land_render` gives. 401,968 checks, at four DMA
  speeds. At realistic speeds 88–98% of the pushes overlap the step or prepare.
- **Test 4's own checks** run against test 5's lcd.c, cal.c, tone.c and land.c, with the title changed to test
  5: 116 + 6 passed, test 3's harness OK.
- **Speed** (the Mac, -O2): the render 2.7 times faster, the step about the same (the Mac has no flash cache to
  miss). Over the log, 368–386 dirty tiles a frame in about 37–40 runs.

One trap found on the way: under ASan, clang on the Mac sometimes merges a `sinf` and `cosf` of one angle into
Apple's `__sincosf_stret`, whose last bit can differ, and it merged differently in the two land.c files. That made
the paint differ from frame 86 by a bit or two. The board's build never merges them (both tests call `sinf` and
`cosf` separately), so the comparison builds with `-fno-builtin-sinf -fno-builtin-cosf`.

## Load (Stephen's call, each time)

Test 4 is on the board, so no BOOT button. With the screen on and "USB" in the Mac:

    stty -f /dev/cu.usbmodem101 1200    # the 1200-baud touch: reboots into the bootloader
    picotool load -x build/viz_test5.uf2

If that fails, the first-load way: screen off, hold BOOT, plug "USB" in, drag the .uf2 onto `RP2350`. Never power
both sockets from two sources at once: they share one VBUS.

To go back to test 4, the same two lines with `../viz-test4/build/viz_test4.uf2`.

## If something's wrong

- **Torn or wrong tiles, a strip of old picture that never clears:** `f` redraws everything. Say where on the
  screen it was; test 4 is the fallback.
- **It hangs or the USB drops:** unplug and plug back in, then load test 4. The log's last lines say how far it got.
- **No smoother:** the log's `frames:` line says where the time goes, which is what the next round of speed work needs.
