# AISLING visualiser box

A small box that shows [AISLING](https://github.com/stephencummins/fm1-aisling) as a picture. A Waveshare
RP2350-PiZero acts as USB host to an FM-1 running AISLING, reads its USB MIDI out (ch1 Drone, ch2 Sequence, ch3
Lead, ch4 Sequence 2, plus clock) and paints a cut-down version of the web visualiser's landscape (Lead sky,
Sequence 2 hills, Sequence fields, Drone bog) on an ELEGOO 3.5" SPI touch screen (ILI9486 + XPT2046) that plugs
straight onto the board's header. A strip of the eight pieces along the bottom sends a program change back to the
FM-1, so the screen also picks the piece.

Our own code throughout, on pico-sdk 2.3.1, its TinyUSB 0.18.0 and Pico-PIO-USB 0.7.3. No Waveshare or ELEGOO
code. Panel register values are written from the Ilitek ILI9486 datasheet, never copied from vendor or fbtft inits.

## Where it's up to

**Test 7 (its 144 MHz build) is on the board.** It starts on the landscape with gamma FALLING and warmth 2, every
power-up, at about 10 frames a second, and the strip's highlight follows the piece however it's chosen on the FM-1.
A tap brings up the FM-1's own screen as a second page, and a finger dragged across one of its four knobs turns that
knob on the FM-1 (AISLING 1.2 or later). The vellum ground still shows white on this panel: the panel squashes its
brightest levels together, and neither warmth nor the top gamma fields fixed it.

How it was made, the parts and a ready-built `.uf2`: https://aisling.stephen8n.com/screen/

| Folder | What it is |
|---|---|
| `viz-test1/` | Screen, FM-1 enumeration and MIDI messages on screen, raw touch. Passed. |
| `viz-test2/` | The landscape, plus touch to pick the piece. Found the paint stall below 10 fps and the collapsed tone curve. |
| `viz-test3/` | A calibration screen for the panel's VCOM, VRH and gamma, judged by eye; dt cap 15. Picked gamma FALLING. |
| `viz-test4/` | FALLING sent at every start-up, and a warmth on our side. |
| `viz-test5/` | Twice as smooth (same pixels), a 144 MHz build, and the strip's piece in sync with the FM-1. |
| `viz-test6/` | The FM-1's screen on the box, from what AISLING says to a screen reader. |
| `viz-test7/` | Test 6, and the FM-1's four knobs turned by dragging on the box. **On the board.** |
| `hosttests/` | Host checks on the Mac (AddressSanitizer + UBSan): `hosttests/testN/run.sh`. |

Each test's own README has what it does, what to look at, build, load and what to do if it goes wrong.

## Build

Toolchain under `~/.pico/`: ARM GNU 14.3.rel1, pico-sdk 2.3.1 (`~/.pico/pico-sdk`), Pico-PIO-USB 0.7.3
(`~/.pico/Pico-PIO-USB`); picotool 2.3.1 from Homebrew.

    cd viz-test7
    export PATH="$HOME/.pico/arm-gnu-toolchain-14.3.rel1-darwin-arm64-arm-none-eabi/bin:$PATH"
    export PICO_SDK_PATH=$HOME/.pico/pico-sdk
    cmake -S . -B build -G "Unix Makefiles" -Dpicotool_DIR=/opt/homebrew/lib/cmake/picotool
    make -C build -j8
    # at 144 MHz (tests 5 to 7): the same with -DVIZ_SYS_MHZ=144 and -B build144

## Load

With a test already on the board (the screen covers BOOT), and its "USB" socket in the Mac:

    stty -f /dev/cu.usbmodem* 1200      # the 1200-baud touch: reboots into the bootloader
    picotool load -x build144/viz_test7.uf2

Read the log with `python3 -I log.py /dev/cu.usbmodem* 10` (keys can follow the seconds).

## Never

- Power both USB-C sockets from two sources at once: they share one VBUS, which also powers the FM-1.
- Send the ILI9486 C1h, C2h (step-up and power), E2h or E3h.

## Licence

GPL-3.0, as AISLING. See `LICENSE`. The knob and page names in tests 6 and 7 come from AISLING's `ui.c`, which is
mostly NACHTFAHRT's, by FartyPants (GPL-3.0-only). A built `.uf2` also contains the Raspberry Pi Pico SDK, TinyUSB and
Pico-PIO-USB: their licences are in `NOTICES.md`.
