# viz-test1

Test 1 for the AISLING visualiser box: a Waveshare RP2350-PiZero with the ELEGOO 3.5" SPI screen on its header,
reading the FM-1's MIDI over USB host. It answers four questions and draws nothing pretty:

1. Does the screen work, the right way round and in the right colours? Four bars, each with its colour's name
   written on it, plus a white dot in the top-left corner.
2. Which of GPIO28/29 is D+ on the PIO-USB socket? It reads both lines before the host starts.
3. Does the FM-1 enumerate, and what does it say it is? IDs, name and interface classes.
4. Do its MIDI messages arrive? The last three, decoded, plus counters (MIDI clock and active sensing are
   counted, not listed).

Touch is shown raw (`TOUCH X … Y …`) as a check on the XPT2046. The same log goes to the Mac as a serial port
on the "USB" socket. Any key re-sends the whole status.

No git (aisling-drafts has none). Our own code throughout; built on pico-sdk 2.3.1, its TinyUSB 0.18.0 and
Pico-PIO-USB 0.7.3. Nothing from Waveshare's or ELEGOO's files.

## Build

    export PATH="$HOME/.pico/arm-gnu-toolchain-14.3.rel1-darwin-arm64-arm-none-eabi/bin:$PATH"
    export PICO_SDK_PATH=$HOME/.pico/pico-sdk
    cmake -S . -B build -G "Unix Makefiles" -Dpicotool_DIR=/opt/homebrew/lib/cmake/picotool
    make -C build -j8

Output: `build/viz_test1.uf2`. The cmake here is the python.org one in `/Library/Frameworks/Python.framework/Versions/3.13/bin`.

## Load (Stephen's call, each time)

**First load.** The screen covers BOOT, so:

1. Unplug everything. Take the screen off.
2. Hold BOOT and plug the upper socket ("USB") into the Mac. An `RP2350` drive appears.
3. Drag `build/viz_test1.uf2` onto it. The board reboots.
4. Unplug, refit the screen (pins 1–26), plug the FM-1 into the lower socket ("PIO-USB"), then plug "USB" back into the Mac.

**Later loads**, with the screen on:

    stty -f /dev/cu.usbmodem* 1200      # the 1200-baud touch: reboots into the bootloader
    picotool load -x build/viz_test1.uf2

Never power both sockets from two sources at once: they share one VBUS.

## Reading the log

    screen /dev/cu.usbmodem* 115200     # quit with Ctrl-A then K

## If something's wrong

- **Mirrored or upside down:** change `VIZ_MADCTL` in `lcd.c` (0x28 now; MX is 0x40, MY 0x80).
- **RED bar shows blue:** drop the 0x08 (BGR) bit from `VIZ_MADCTL`.
- **Colours inverted** (white bar black): send 0x21 instead of 0x20 in `lcd_init`.
- **Screen white or blank:** the SPI may be too fast for the shift registers; lower `LCD_HZ`.
- **Stuck on "WAITING FOR THE FM-1":** `USB LINES` shows what the two pins read. Both 0 means nothing is pulling
  D+ up: check the cable and that the FM-1 is on the lower socket.
