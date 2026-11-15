# viz-test15: the picture turned half a turn

**On the board since 15 Nov 2026 (the 144 MHz build below, loaded with picotool from the bootloader, over test 14).
Stephen: "it worked, picture is the right way up". The touch has not been reported on.** Test 14 with the picture
and the touch turned half a turn, and nothing else.

Why: the small board's sockets open under one long edge of the screen, and up to test 14 that edge is the picture's
bottom. A box standing with its cables behind it (in the Lego tray, say) then shows the picture upside down. In
this build the picture's top is along the edge the cables leave from.

- The turn is in `lcd.c`, under `VIZ_FLIP`: the screen's memory access control byte is `0xE8` where it was `0x28`
  (MX and MY both, on top of MV and BGR), and `touch_screen()` gives the opposite pixel for the same spot on the
  glass. Nothing that draws or reads a touch knows about it.
- `CMakeLists.txt` turns `VIZ_FLIP` on. `cmake ... -DVIZ_FLIP=OFF` builds test 14's picture.
- Every other file is test 14's byte for byte (the titles, the USB name and `main.c`'s head say 15).
- The touch's calibration is test 1's still: the same glass, the same readings, turned.

What the rest does is in [`../viz-test14/README.md`](../viz-test14/README.md).

## Host checks

    zsh ../hosttests/test15/run.sh [--quick]

- Test 15 is test 14 and the turn: the files compared; `lcd.c` loses no line of test 14's, and with `VIZ_FLIP`
  off the compiler sees test 14's `lcd.c` exactly.
- `flip_check.c`, built with the turn and without: the byte sent at start-up and after `lcd_resetup()`; a touch at
  the four corners and the centre, at every raw reading (on the screen, and the opposite pixel of the unturned
  one), and which way a finger moves. 12 checks each way.
- 8 deliberate breaks of `lcd.c` (the touch turned on one axis only, not at all, a pixel out; the picture mirrored
  on one axis; the picture not turned; the turn on unasked), each caught.
- Test 13's and test 12's `run.sh` against this folder, as test 14's runs them, and inside test 12's, test 11's
  whole suite. They build `lcd.c` without `VIZ_FLIP`, so they show the turn changes nothing when it is off.

All pass; every break is caught.

The checks show that the touch turns with the picture. That `0xE8` turns this screen's picture half a turn, and
not some other way, is the ILI9486 datasheet's word: only the board can say.

## Build

As test 8's (see test 12's README). 144 MHz build of 15 Nov 2026: `build144/viz_test15.uf2` (364,032 bytes; SHA-256
`03926d6c96d5f2c088b141c12dbcb3482bd13ec46e998a0128ce29fdc5d4899b`). No warnings.

## Not tried

The picture is the right way up (Stephen, 15 Nov 2026). Not yet reported:

- that a touch lands where the finger is, at the corners above all;
- how the landscape looks: the screen is now filled from its other end, so any tearing runs the other way.

Test 14 was on the board before this build. A fault seen here that test 14 doesn't show is the turn's:
`-DVIZ_FLIP=OFF` builds the same code without it. Each earlier README's own "not tried" list still stands.
