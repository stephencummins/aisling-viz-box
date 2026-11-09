# viz-test4

Test 4 for the AISLING visualiser box (Waveshare RP2350-PiZero, ELEGOO 3.5" SPI screen on header pins 1–26,
the FM-1 on the lower "PIO-USB" socket). Test 3 found two things: the panel's own gamma came up differently from
one start to the next, and gamma FALLING gave an even grey ramp and dark S1/S2. But the vellum ground (GND)
still looked white, not cream. So this one:

1. **Sends gamma FALLING at every start-up**, and after every screen reset. Nothing to press. E0h/E1h =
   `0F 3E 3D 0E 1C 0C 57 69 28 03 03 01 02 01 00`; VCOM and VRH stay the chip's own.
2. **Adds a warmth**, done on our side before anything is sent. The panel's gamma is one curve for red, green
   and blue, so it can make the picture lighter or darker but not warmer. Warmth leaves red alone and scales
   green and blue down, blue about three times as much. Five steps, starting in the middle (2):

   | step | green | blue | white becomes | the vellum ground F5 ED DA becomes |
   |------|-------|------|---------------|------------------------------------|
   | 0 | ×1.000 | ×1.000 | FF FF FF | F5 ED DA (as tests 2 and 3) |
   | 1 | ×0.977 | ×0.930 | FF F9 ED | F5 E7 CB |
   | **2** | **×0.949** | **×0.859** | **FF F2 DB** | **F5 E1 BB** |
   | 3 | ×0.926 | ×0.789 | FF EC C9 | F5 DB AC |
   | 4 | ×0.898 | ×0.719 | FF E5 B7 | F5 D5 9D |

   It applies to everything: the landscape (before its dither, so the fine steps survive), the strip, the
   status line, the calibration screen's ramps, swatches and text.
3. **Keeps test 3's calibration screen**, so the ramps, S1/S2 and GND can be checked at each warmth step. VCOM,
   which made no visible difference, is now keys only. It starts on the landscape: tap the picture for
   calibration, LAND to go back. (The first load started on calibration; changed 2026-11-09 once FALLING and
   warmth 2 were settled.)

No git. Our own code throughout, on pico-sdk 2.3.1, its TinyUSB 0.18.0 and Pico-PIO-USB 0.7.3. Panel register
values from the Ilitek datasheet only.

## The buttons

    WARM -   WARM +   VRH -   VRH +   START
    GAMMA    FIELD    VAL -   VAL +   LAND

- **WARM − / WARM +**: one warmth step. The whole screen redraws.
- **START**: everything back to how test 4 starts: gamma FALLING, VCOM and VRH the chip's own, warmth 2.
- **GAMMA, FIELD, VAL, VRH, LAND**: as test 3. GAMMA from FALLING goes on to the chip's own, then MID.

The settings line reads, for example, `WARM 2/4  VCOM NV  VRH CHIP  GAMMA FALLING VP20=87/127`. The landscape's
status line shows `WARM 2` (the tile count moved to the log to make room).

## What to look at

- **GND** (the vellum swatch) and the landscape's ground: cream, not white, without going yellow or pink.
- **The white** of the text and the top of the grey ramp: warm white, not blue-white, not yellow.
- **The grey ramp**: still even from black to white, with no colour cast in the mid-greys.
- **S1/S2**: still dark, still readable.
- **The heather, fields, sky and hills swatches**: still looking like their names.

Then LAND and look at the picture itself. If no step looks right, say which way it's wrong (too yellow, too
pink, still white, too dim): the gains are two numbers in `tone.c` and easy to move.

## Build

    export PATH="$HOME/.pico/arm-gnu-toolchain-14.3.rel1-darwin-arm64-arm-none-eabi/bin:$PATH"
    export PICO_SDK_PATH=$HOME/.pico/pico-sdk
    cmake -S . -B build -G "Unix Makefiles" -Dpicotool_DIR=/opt/homebrew/lib/cmake/picotool
    make -C build -j8

Output: `build/viz_test4.uf2` (176,640 bytes; SHA-256 `c3734fb4…5c6da5d0`, the landscape-first build). RAM: 415 KB of 512 KB static. No
warnings.

Host checks (Mac, AddressSanitizer + UBSan; `../hosttests/test4/run.sh`), all passed:
- `lcd.c`, `cal.c` and `tone.c` against a recording stand-in for the SDK, decoding every word sent to the
  screen (116 checks): start-up sends exactly reset, the set-up commands and gamma FALLING, and no VCOM or VRH;
  white goes out as FF9B at step 2 and no pure white pixel is sent; the GND swatch goes out warmed; WARM steps
  send no command and stop at each end; START from any state sends FALLING and resets only when VCOM or VRH has
  to come off; CHIP (key `k`) takes the warmth off too; every step keeps 0..255, never brightens and never
  reverses a ramp; test 3's VCOM, VRH, gamma and field checks all still hold; every window, pixel count and
  character right; only the expected commands, never C1h, C2h, E2h or E3h.
- `land.c` with the warmth: red unchanged at every step, green and blue scaled by their gains to within 0.1%,
  step 0 gives back the identical picture; test 3's landscape checks (big steps, edge notes, no NaN) still pass.

## Load (Stephen's call, each time)

Test 3 is on the board, so no BOOT button. With the screen on and "USB" in the Mac:

    stty -f /dev/cu.usbmodem* 1200      # the 1200-baud touch: reboots into the bootloader
    picotool load -x build/viz_test4.uf2

If that fails, the first-load way: screen off, hold BOOT, plug "USB" in, drag the .uf2 onto `RP2350`.
Never power both sockets from two sources at once: they share one VBUS.

## Reading the log

    python3 -I log.py /dev/cu.usbmodem* 10           # the snapshot, then 10 s of log
    python3 -I log.py /dev/cu.usbmodem* 20 WW        # keys first: here two warmth steps up

If it says nothing, unplug "USB" from the Mac and plug it back in. Every change logs a `panel:` line starting
with the warmth and its gains, then the registers: that line is what to keep once it looks right.

Keys, on either screen:
- `w`/`W` warmth down/up, `b` back to the start, `k` the chip's own and no warmth (the picture as test 2 sent it).
- The panel, in fine steps: `r`/`R` VRH down/up, `v`/`V` VCOM down/up, `y` the next gamma preset, `[` `]` the
  gamma field, `-`/`+` its value.
- `c` calibration screen, `l` landscape, `s` SPI speed (20 → 30 → 15), `f` full redraw.
- `p` palette, `g` ground, `d` demo (notes made on the board at 96 BPM), `t` touch log, `m` MIDI log.
- Anything else: the snapshot.

## If something's wrong

- **Too warm, or the wrong kind of warm:** WARM − or `w`; `k` shows the picture with no warmth and the chip's
  own gamma, for comparison, and START (`b`) comes back.
- **The screen goes white or blank:** START or `k` resets it. If even that fails, unplug. Nothing is stored: a
  power cycle starts again at FALLING and warmth 2.
- **Garbled at 30 MHz:** the screen's limit; `s` moves on, `f` redraws.
