# viz-test6

Test 6 for the AISLING visualiser box (Waveshare RP2350-PiZero, ELEGOO 3.5" SPI screen on header pins 1–26,
the FM-1 on the lower "PIO-USB" socket). Stephen asked (2026-11-09): "Could the display from the FM1 be put on the
little screen". This draws it: the FM-1's screen, rebuilt from what AISLING says to a screen reader, as a second
page on the box.

1. **A tap on the landscape shows the FM-1's screen; a tap on it goes back.** Anywhere above the status row.
2. **Calibration is now a hold of 2 seconds or more**, on either page; it comes up when you let go (a tap did it
   in tests 4 and 5). Its LAND button comes back as before.
3. **The strip and the status row stay** on both pages: a piece touched on the strip is sent, and the highlight
   follows the FM-1 as in test 5.
4. **The landscape is test 5's, unchanged** (land.c, tone.c and hot.h are byte for byte the same). It keeps
   painting while the FM-1's page is shown, ten steps a second and not drawn, so the notes played meanwhile are
   there when you tap back.

Everything else is test 5's: gamma FALLING at every start-up, warmth 2, starting on the landscape, the keys, the
clock option. Nothing is stored; a power cycle starts the same way.

Our own code throughout, on pico-sdk 2.3.1, its TinyUSB 0.18.0 and Pico-PIO-USB 0.7.3.

## Where the picture comes from

Test 5 already says HELLO to AISLING as a screen reader would, to keep the strip's piece in sync. AISLING answers by
saying everything on its screen, as text, and then every change to it. The texts look like this:

    Drone page, voice Pipes: Pedal 90, Reso 0, Phaser 15, Attack 40. Piece Skellig, D Minor, tempo 76, playing
    Chord F minor 7, next chord 40% of the way. Section Flow. Scale D, E, F, G, A, Bb, C
    Ornament 35
    Chord A minor
    PIECE Aran, A Dorian, tempo 96
    SEQ MUTED

`fm1scr.c` reads those back into the screen they describe (the grammar is AISLING's own `ui.c`), and draws it.
Some things change without being said: how far to the next chord, the scale after a new mode, a value a button
moved, Setup's CPU. For those the box asks for the whole screen again (WHERE, a reader's "say it all again"):

- when the page comes up;
- after something changed that wasn't all said, once the FM-1 has been quiet for 400 ms, at most once a second;
- every 3 seconds once it has been quiet for one, so the next-chord bar moves.

Only while the FM-1's page is shown. Each WHERE is about 100 USB packets from the FM-1, a few milliseconds.

Nothing on the FM-1 changes: no firmware, no settings, nothing stored. If the box goes, the FM-1 stops talking 5
seconds later.

## What to expect

The page, top to bottom (y 0–271; the status row and the strip below it as before):

- **The header:** the piece and PLAYING (green) or RESTING; the key and tempo, and NOT SAVED (orange) when it isn't.
  A button's message ("SEQ MUTED") shows over the header for a second and a half; a warning, in red, for four.
- **The page's name** (in orange) and what's beside it: its voice, character, walk, sleep, ...
- **The four knobs**, label over value. The one just turned is bright for 2 seconds.
- **The chord**, as the FM-1 writes it ("Fm7", "Bm"), and the section.
- **NEXT CHORD** and a bar for how far, or HOLD (orange) or HELD.
- **The filter on the move** on a sound page ("Cutoff darker -1 octave"), or on Setup its CPU and power.
- **SCALE** and its notes.

Before the FM-1 has said anything, the page says NO FM-1 (orange) or, plugged in, WAITING FOR THE FM-1'S SCREEN
READER (grey). A firmware without the reader never answers; the strip works as before.

It redraws only what changed, as soon as it's said, so a knob turned on the FM-1 should show on the box straight
away. AISLING stops a text at 240 characters, even mid-word, so in one of 230 or more the last item is left as it was
rather than taken half-said.

## Reading the log

    python3 -I log.py /dev/cu.usbmodem101 10

New in the snapshot, once the FM-1 has said something:

    FM-1's screen: Drone page; SKELLIG, D Minor, tempo 76, playing; chord F MINOR 7, Flow
      heard 412: 6 pages, 380 knobs, 9 garden, 14 chords, 3 messages; 0 not understood, 0 cut off
      WHEREs asked 9 (0 failed), texts dropped 0

(The numbers are made up, to show the shape.)

- **not understood** should stay at 0. Anything else means AISLING said something fm1scr.c doesn't know: `a` turns
  on a log of every text the FM-1 says (`said (now): Ornament 35`), which shows what it was.
- **texts dropped**: core 1 had more than the paint's queue could hold. It should be 0; a WHERE puts it right.
- **WHEREs failed**: the FM-1 didn't take one (its USB busy). The next one goes 3 seconds later.

The keys: `o` the FM-1's page, `l` the landscape, `c` calibration, `a` the log of what the FM-1 says. The rest are
test 5's.

## Build

    export PATH="$HOME/.pico/arm-gnu-toolchain-14.3.rel1-darwin-arm64-arm-none-eabi/bin:$PATH"
    export PICO_SDK_PATH=$HOME/.pico/pico-sdk
    cmake -S . -B build -G "Unix Makefiles" -Dpicotool_DIR=/opt/homebrew/lib/cmake/picotool
    make -C build -j8
    cmake -S . -B build144 -G "Unix Makefiles" -Dpicotool_DIR=/opt/homebrew/lib/cmake/picotool -DVIZ_SYS_MHZ=144
    make -C build144 -j8

Output: `build/viz_test6.uf2` (210,944 bytes; SHA-256 `ddf22a39…f82f73be6a`) and, at 144 MHz as test 5's on the
board, `build144/viz_test6.uf2` (210,944 bytes; `466e0d5b…c1c20611cb`). No warnings. At 144 MHz the screen SPI
starts at 24 MHz, as test 5's. RAM: the static data ends at 475 KB of 512 KB (test 5: 474), 37 KB left for the heap.

## Host checks

Mac, AddressSanitizer + UBSan; `../hosttests/test6/run.sh` (`run.sh DIR` also writes pictures of the page into
DIR). All passed:

- **The FM-1's screen** (`fm1scr_check.c`, 5,960,418 checks):
  - **Everything AISLING 1.2 says** (`corpus.txt`, 8,952 texts, made by fm1-aisling's host build of its own
    `ui.c`): every page, every knob through its range, the whole screen as a WHERE answers it, every chord, every
    message, in order with the flags AISLING gives each. Each text is checked against the model, worked out a
    second way from the text itself: the page, its detail, each knob's label and value, the header, the chord, the
    section, the scale, the messages and their timing.
  - **What the corpus doesn't have:** a cut text, the filter on the move, evolve and sleep, the Key page's tempo
    and key, PLAYING and RESTING, the longest messages, the timers at their edges and across the clock's wrap.
  - **End to end:** AISLING's own `announce.c`, included as its firmware includes it, says every line. Its frames go
    through the box's `reader.c`, packed as the FM-1's USB code packs SysEx (9,172 frames), and must build the same
    model as the text fed straight in. The box's HELLO and WHERE are taken by `announce.c` as AISLING takes them.
  - **Anything at all:** every text cut at every length (88,457) and 120,000 random texts (bytes, spliced texts,
    soup of the grammar's own words). The model stays in range, its strings end, nothing crashes, and saying the
    whole screen again puts it right.
  - **Every pixel:** each word sent to the screen is decoded into a picture. Nothing at or below y 272 (the status
    row), no window out of bounds, every pixel of the area covered by a full redraw, no letter the font lacks, and
    the page drawn change by change is the same, pixel for pixel, as one drawn fresh.
- **Nine deliberate breaks of `fm1scr.c`**, each caught: a knob's label matched short ("Echo time 1/4." as Echo's),
  a turned knob not redrawn, a rule left undrawn, the Chords page's Chord knob taken as a chord change, the
  header's "not saved yet" rule dropped, the waiting page one row into the status row, the knobs whose labels
  aren't capital-then-lower-case (MIDI out, A time) missed, a cut text taken whole, a new key not asking for the
  scale.
- **main.c's own pump code** against a stand-in DMA, now with trips to the FM-1's page as well as calibration: no
  push off the landscape, and the landscape exactly right when it comes back. 401,968 checks.
- **Test 5's reader check** against test 6's `reader.c`: 989 checks.
- **Test 4's own checks** against test 6's lcd.c, cal.c, tone.c and land.c, with the title changed to test 6:
  116 + 6 passed, test 3's harness OK.

The font gained the letters it was missing for this page (`% ;` and `a b d g i m s u`, for "Bb", "Fm7", "add9",
"dim", "sus4"); a lower-case letter without its own drawing uses its capital's.

## Load (Stephen's call, each time)

Test 5's 144 MHz build is on the board, so no BOOT button. With the screen on and "USB" in the Mac:

    stty -f /dev/cu.usbmodem101 1200    # the 1200-baud touch: reboots into the bootloader
    picotool load -x build144/viz_test6.uf2

If that fails, the first-load way: screen off, hold BOOT, plug "USB" in, drag the .uf2 onto `RP2350`. Never power
both sockets from two sources at once: they share one VBUS.

To go back to test 5, the same two lines with `../viz-test5/build144/viz_test5.uf2` (SHA-256 `56a0b3d2…`).

## If something's wrong

- **A tap goes to calibration, or a hold doesn't:** `t` logs the touches; say which.
- **Something on the FM-1 isn't on the box, or is wrong:** `a`, then do it again on the FM-1; the log has the
  text it said. "Not understood" in the snapshot counts them.
- **The box's page lags the FM-1:** the snapshot's WHERE and dropped counts, and whether the landscape's frame rate
  (on tapping back) has changed from test 5's.
- **It hangs or the USB drops:** unplug and plug back in, then load test 5. The log's last lines say how far it got.
