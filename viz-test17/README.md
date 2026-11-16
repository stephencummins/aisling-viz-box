# viz-test17: a light look for the box's own pages

**Not on the board. Built and checked on the Mac only (16 Nov 2026).** Test 16 with a second look for what the box
itself draws, and nothing else.

Why: AISLING 1.4.1 gave the FM-1 its Screen: Dark or Light. Stephen, the same day: "Can I also get the VOXBOX UI
with a light option".

- **Dark** is every test before this one, to the pixel, and is what the box starts in. **Light** is the same pages
  with every colour turned over and made deeper, on white: black ground is white, vellum lettering is ink, the grey
  labels are a darker grey, and the oranges, greens and reds keep their hue and are brought down until they read on
  white.
- It is one rule, in `look.h`, on every flat colour as it goes out: `lcd.c`'s fills and text, and `perf.c`'s uncial
  lettering before it is mixed. No page is drawn differently, so the strip, the status line, the FM-1's page, the
  effects, the pad, a guest's page, the card's page and calibration all have it.
- **The landscape keeps its ground.** Vellum, night and mist are the visualiser's, and the picture is sent as
  pixels, which the look does not touch. In Light the strip and the status line about the picture are light; the
  picture is as it was.
- Where it is: **LOOK** on the calibration page, a button of its own on the samples' row, left of CARD. Key **h**
  on the serial line. `look = dark` or `look = light` in `SETTINGS.TXT`, saved two seconds after a change like the
  rest.
- An older `SETTINGS.TXT` has no `look` line and is read as before: the look stays as it is, which on a box just
  started is Dark. Test 16, given test 17's file, uses the nine settings it knows and says `look isn't a setting`.
- On the calibration page the grey ramp, the colour ramps and the swatches are drawn as they are whatever the look:
  they are what the panel is set by. The page about them has the look. To make room for LOOK the samples' text is
  shorter (`96 BPM`, without the wind), and the line under it says which look it is: `VELLUM ON BLACK` or
  `INK ON WHITE`.
- An effect's flash (the full screen in the effect's colour, its name on it) is turned over too: a pale flash with
  black letters in Dark is a deep one with white letters in Light.
- **An FM-1 on 1.4.1's Light does not show through.** The box does not mirror the FM-1's pixels: it draws its own
  page from what the FM-1's screen reader says, in the box's colours. The two looks are set apart, each on its own
  device, and no link between them is built.
- The seven files changed (`lcd.c`, `perf.c`, `settings.c`, `settings.h`, `cal.c`, `cal.h`, `main.c`) differ from
  test 16's by the lines in `../hosttests/test17/changes.txt`: 4 out, 56 in. `look.h` is new. Every other file is
  test 16's byte for byte (the titles and the USB name say 17).
- The screen is sent nothing new: no command, no register. The same fills with other colours.

What the rest does is in [`../viz-test16/README.md`](../viz-test16/README.md).

## Host checks

    zsh ../hosttests/test17/run.sh [--quick] [DIR]

`DIR` gets the pictures of the pages in both looks, as PNG.

- Test 17 is test 16 and the look: the files compared; the seven's changed lines are `changes.txt`'s and no others.
- `look_check.c map`: Dark is the colour itself, for all 65,536; black and white change places; greys stay grey and
  keep their order; a colour that read on black (4.5 or more) reads on white (3 or more); pure colours are held to
  4.4 or more; and the box's own 26 pairs of colours, by name. The faintest lettering in Light reads 4.31.
  "Reads" is the WCAG contrast ratio, a sum about numbers.
- `look_check.c settings` and `old16.c`: both looks into `SETTINGS.TXT` and back; `Look = LIGHT` and a note after
  it taken; other words refused with "should be dark or light"; a file written by test 16's own `settings.c`
  (1,310 bytes) read with all nine settings and the look left alone; test 17's file (1,508 bytes) is test 16's with
  one paragraph more, and test 16 reads it but for that line.
- `look_check.c pages`: calibration and the strip (cut from `main.c`) drawn in both looks through `lcd.c`. Every
  pixel in Light is the Dark one turned over, but the ramps and swatches, which are the same in both. LOOK's and
  CARD's touch areas. `lcd_blit` sends pixels as given.
- `look_check.c frames`: the pictures that test 11's, test 13's and test 7's checks make of the effects, the pad, a
  guest's page and the FM-1's page, made again in Light: 28 pictures, 3,655,680 pixels, 3,622,666 the Dark one
  turned over exactly and 33,014 on a letter's soft edge (the uncial lettering mixes after the turning, so its
  edges come out a little lighter than turning the mixed pixel would).
- `main.c` read: the button, the key, the saving and the file's word all reach `lcd_look_set`; `lcd.c` turns fills
  and text and no pixels.
- 23 deliberate breaks of `look.h`, `lcd.c`, `perf.c`, `settings.c`, `cal.c` and `main.c`, each caught.
- Test 16's `run.sh` against this folder, and inside it test 15's, 13's, 12's, 11's and 7's, each without its
  comparison of a file test 17 changed. They run in Dark: what they show is that the look changes nothing when it
  is off.

Run whole on 16 Nov 2026 (26 minutes): `test 17: all passed`, with the earlier tests' breaks all caught inside it
(11, 8, 32, 31, 36 and 47).

The pictures were looked at, both looks: calibration, the strip, the effects with some held, the pad, the Journey
and SETUP pages, a warning, a guest's page, a flash, no FM-1. Nothing was unreadable on white. The card's page has
no host picture; its colours are among the 26 pairs.

The checks are sums and pictures on a Mac. What the panel makes of a white screen (its glare, its gamma at the
light end, whether the deep colours still tell apart, how it sits by a vellum landscape) is for the board to say.

## Build

As test 8's (see test 12's README), with the ARM compiler on the path
(`~/.pico/arm-gnu-toolchain-14.3.rel1-darwin-arm64-arm-none-eabi/bin`). 144 MHz build of 16 Nov 2026:
`build144/viz_test17.uf2` (368,640 bytes; SHA-256
`0ef9198357b7d2779e1c4a519dd9b704fbd92a39c0976ffcddaa4b20770f03ed`). No warnings.

## Not tried

All of it, on the board. The build has not been loaded. Light has never been on the panel: not the white, not the
colours, not LOOK under a finger, not key h, not the look coming back from the card after a restart, not a card
with test 16's file in the slot. Each earlier README's own "not tried" list still stands.
