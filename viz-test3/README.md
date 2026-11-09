# viz-test3

Test 3 for the AISLING visualiser box (Waveshare RP2350-PiZero, ELEGOO 3.5" SPI screen on header pins 1–26,
the FM-1 on the lower "PIO-USB" socket). Test 2 showed the landscape, but the panel's tone curve had collapsed:
only grey level 0 was dark and every other level near full brightness, so the sky looked fine and the darker
layers vanished. Test 2 sent the ILI9486 none of its analogue settings. This one lets Stephen set them by eye:

1. **A calibration screen** (the one it boots into): a 64-step grey ramp, red, green and blue ramps, swatches of
   the colours test 2 lost (the heather palette, the vellum ground, the orange, the strip's greys), and a copy of
   the strip and status line. Above them, the settings now; along the bottom, ten buttons.
2. **The panel's settings**, written from the Ilitek datasheet (v0.01) and nowhere else:
   - **VCOM** (C5h): `VCOM = -2 + reg/64` V, reg 0x00–0x80. Left alone, the chip takes VCOM from an NV memory
     the module's maker may never have programmed. The first press sets 0x60 (−0.5 V, the register's power-on
     value); after that a touch steps 4 and a key 1.
   - **VRH** (C0h, both polarities the same): `3.625 + (reg − 1)/16` V, reg 0x01–0x1F. Power-on 0x0E (4.438 V).
     0x00 ("halt") is never sent.
   - **Gamma** (E0h and E1h, the same curve for both): 16 fields. The datasheet leaves their power-on values as
     "TBD", so the presets are starting points built from the fields alone: MID (every field halfway), RISING
     (each at its grey level's share of its range) and FALLING (the mirror). Then FIELD picks one and VAL steps
     it. VP0 and VP63 are taken as 4 bits: the register page says 5, the command list 4 with bit 4 at 0.
   - **CHIP**: everything back to the chip's own. The screen can't be read back and a sent setting can only be
     undone by a hardware reset, so this (and any setting going back to "chip") resets the screen and redraws.
   - Never sent: C1h, C2h, E2h, E3h.
3. **The landscape** with whatever was chosen (LAND). Tap the landscape to come back to calibration.
4. Two fixes from test 2: the paint's step cap raised from 6 frames to 15, so below 10 fps the paint keeps
   real time; and the SPI choices are now 15, 20 and 30 MHz, starting at 20 (30 looked lighter in test 2).

No git. Our own code throughout, on pico-sdk 2.3.1, its TinyUSB 0.18.0 and Pico-PIO-USB 0.7.3.

## The buttons

    VCOM -   VCOM +   VRH -   VRH +   CHIP
    GAMMA    FIELD    VAL -   VAL +   LAND

The settings line reads, for example, `VCOM 60 -0.500V  VRH 0E 4.438V  GAMMA RISING* VP20=40/127`
(`*`: a field edited since the preset). The bar under the grey ramp marks the grey level the chosen field sets.

## What to aim for

- The grey ramp running from black to white in 64 even steps, no band of near-identical light greys at the
  right and no crushed blacks at the left; the colour ramps the same.
- The swatches looking like their names: the heather bog dark purple-brown, the fields olive, the sky orange
  to sand, the hills slate blue; the vellum ground cream, not white.
- SKELLIG's cell dark with readable vellum text, the BPM grey readable.

A suggested order: VCOM first (press once, then step either way and watch the mid-greys), then GAMMA through
its presets, then FIELD and VAL on whichever preset looks closest. Then LAND to see it on the picture.

## Build

    export PATH="$HOME/.pico/arm-gnu-toolchain-14.3.rel1-darwin-arm64-arm-none-eabi/bin:$PATH"
    export PICO_SDK_PATH=$HOME/.pico/pico-sdk
    cmake -S . -B build -G "Unix Makefiles" -Dpicotool_DIR=/opt/homebrew/lib/cmake/picotool
    make -C build -j8

Output: `build/viz_test3.uf2` (174,592 bytes; SHA-256 `6b311043…2e0ea8d3`). RAM: 415 KB of 512 KB static.

Host checks (Mac, AddressSanitizer + UBSan), all passed:
- `lcd.c` and `cal.c` against a recording stand-in for the SDK, decoding every word sent to the screen (77
  checks): start-up sends no panel settings; each button sends exactly its command and bytes (C5 00 60 80,
  C0 0E 0E, the three presets' E0/E1 bytes worked out by hand, VP27/VP36 sharing a byte); the clamps hold;
  undoing a setting resets the screen and sends the others again; `s` re-sends the panel; only the expected
  commands ever go out; every window and pixel count is right; every character is in the font and on screen.
- `land.c` with steps up to 15 frames: no NaN, the paint stays bounded, the logged FM-1 MIDI replayed for
  300 s at 4, 5 and 10 frames a second.

## Load (Stephen's call, each time)

Test 2 is on the board, so no BOOT button. With the screen on and "USB" in the Mac:

    stty -f /dev/cu.usbmodem* 1200      # the 1200-baud touch: reboots into the bootloader
    picotool load -x build/viz_test3.uf2

If that fails, the first-load way: screen off, hold BOOT, plug "USB" in, drag the .uf2 onto `RP2350`.
Never power both sockets from two sources at once: they share one VBUS.

## Reading the log

    python3 -I log.py /dev/cu.usbmodem* 10           # the snapshot, then 10 s of log
    python3 -I log.py /dev/cu.usbmodem* 20 VVy       # keys first: here VCOM on, up one, a gamma preset

If it says nothing, unplug "USB" from the Mac and plug it back in. Every panel change logs a `panel:` line with
the registers and volts, and the snapshot has the current one: that line is what to keep once it looks right.

Keys, on either screen:
- The panel, in fine steps: `v`/`V` VCOM down/up, `r`/`R` VRH down/up, `y` the next gamma preset, `[` `]` the
  gamma field, `-`/`+` its value, `k` all back to the chip's own.
- `c` calibration screen, `l` landscape, `s` SPI speed (20 → 30 → 15), `f` full redraw.
- `p` palette, `g` ground, `d` demo (notes made on the board at 96 BPM), `t` touch log, `m` MIDI log.
- Anything else: the snapshot.

## If something's wrong

- **A setting makes it worse:** step back, or CHIP to start again. Nothing here is stored: a power cycle
  forgets it all and the board starts at the chip's own settings.
- **The screen goes white or blank after a setting:** CHIP (or `k`) resets it. If even that fails, unplug.
- **Garbled at 30 MHz:** the screen's limit; `s` moves on, `f` redraws.
- **Touch lands on the wrong button:** `t` logs where it thinks each touch is.
