# viz-test2

Test 2 for the AISLING visualiser box (Waveshare RP2350-PiZero, ELEGOO 3.5" SPI screen on header pins 1–26,
the FM-1 on the lower "PIO-USB" socket). Test 1 proved the screen, touch and the FM-1's MIDI. This one tries:

1. **The landscape.** A cut-down port of the web visualiser's Land scene (`web/visualiser/index.html` in
   fm1-aisling), painted live from the FM-1: the Lead (ch3) in the sky, Sequence 2 (ch4) in the hills, the
   Sequence (ch2) in the fields, the Drone (ch1) in the bog. Pitch sets where across, velocity how much paint,
   the MIDI clock's tempo the wind. Kept from the web, line for line: the drops (how they open, spread in
   fingers, shed eddies and fade), the wind and its slow swirl, the paint's drift, feathering and fading, the
   palettes and grounds, the canvas grain. Left out to fit the board: the full fluid solve, the sharpened
   advection and the runs of paint down the canvas. The eddies are kicked in where the web kicks them but stand
   where they land and die away, so the paint moves more simply than in the browser.
2. **How fast it goes**, on this chip: frames a second, the time for the paint and for the picture, and how many
   of the 510 16×16 tiles change a frame (only those are sent). The screen's SPI starts at 20 MHz; `s` tries 30,
   60 and 15.
3. **Touch picks the piece.** The strip along the bottom names the eight pieces; a tap sends program change 0–7
   to the FM-1 over USB (channel 1; AISLING takes it on any channel). The piece sent shows orange.

Screen: the landscape in rows 0–271, a status row (FM-1, tempo, wind, blooms, fps, SPI MHz, tiles), then the strip.

No git. Our own code throughout, on pico-sdk 2.3.1, its TinyUSB 0.18.0 and Pico-PIO-USB 0.7.3.

## What changed since test 1

- D+ fixed on GP28 (D− GP29), as test 1 found.
- The touch calibration from Stephen's corners is in `lcd.c` (`touch_screen`).
- clk_peri runs from the 120 MHz system clock. SDK 2.x's `set_sys_clock_khz` leaves it on the 48 MHz USB
  clock, which held test 1's SPI to 24 MHz at best; now 60, 30, 20 and 15 MHz are on offer.
- Pixels go out by DMA (channel 11: PIO-USB takes channel 0 for itself) while the next run is drawn.

## Build

    export PATH="$HOME/.pico/arm-gnu-toolchain-14.3.rel1-darwin-arm64-arm-none-eabi/bin:$PATH"
    export PICO_SDK_PATH=$HOME/.pico/pico-sdk
    cmake -S . -B build -G "Unix Makefiles" -Dpicotool_DIR=/opt/homebrew/lib/cmake/picotool
    make -C build -j8

Output: `build/viz_test2.uf2` (162,816 bytes; SHA-256 `68a935b6…788a4ecd`). RAM: 415 KB of 512 KB static,
stacks in the two 4 KB scratch banks.

A host check of `land.c` (Mac, AddressSanitizer + UBSan) passed: 510 tiles on a forced redraw and none when
idle; paint lands in its bands; blooms clear 10 s after all notes off; a new ground redraws everything; no NaN
after the largest step or notes 0 and 127.

## Load (Stephen's call, each time)

Test 1 is on the board, so no BOOT button. With the screen on and "USB" in the Mac:

    stty -f /dev/cu.usbmodem* 1200      # the 1200-baud touch: reboots into the bootloader
    picotool load -x build/viz_test2.uf2

If that fails, the first-load way: screen off, hold BOOT, plug "USB" in, drag the .uf2 onto `RP2350`.
Never power both sockets from two sources at once: they share one VBUS.

## Reading the log

    python3 -I log.py /dev/cu.usbmodem* 10           # the snapshot, then 10 s of log
    python3 -I log.py /dev/cu.usbmodem* 20 sm        # keys first: here SPI up one step, MIDI log on

If it says nothing, unplug "USB" from the Mac and plug it back in.

Keys: `s` SPI speed (20 → 30 → 60 → 15), `p` palette, `g` ground, `f` full redraw, `d` demo (notes made on the
board at 96 BPM, to tell a screen problem from a USB one), `t` touch log, `m` MIDI log, anything else the snapshot.

## What to look for

- **The picture:** the four layers in their places, moving with the wind; a still screen while nothing plays
  apart from the wind. Judge colours and grain by eye.
- **Speed:** the status row's FPS, and the snapshot's `step` and `picture` ms. Expect the picture to cost most:
  at 20 MHz a tile takes 0.2 ms to send, so 240 tiles is about 50 ms. `s` shows how far the screen's shift
  registers can go: if 60 garbles, `f` redraws; `s` again moves on.
- **The wind:** heading for about 0.09 at Aran's 96 BPM (0.06 at Samhain's 64). It eases, as the web's does,
  over 5 seconds or so; when the clock stops (0.7 s without one) it eases back down to 0.01.
- **The strip:** a tap goes grey under the finger, then orange when the FM-1 has the program change, and the
  FM-1 changes piece. A tap on the piece already playing does nothing on the FM-1 (AISLING ignores it). While
  the FM-1 sleeps, it ignores MIDI.
- **Errors:** `FM-1 !` in red on the status row; the snapshot's `last error`, and `DROPS` if events were lost.

## If something's wrong

- **Garbled at 30 or 60 MHz:** the screen's limit; note it and step back (`s` cycles; `f` redraws).
- **Garbled at 20 MHz too:** lower `LCD_HZ` in `lcd.c` to 15 MHz and rebuild.
- **Touch lands in the wrong cell:** `t` logs where it thinks each touch is.
- **`NO FM-1 TO SEND TO`:** the FM-1 has no MIDI OUT endpoint open; the snapshot says which endpoints it found.
