# viz-test18: every page clear of the brick, and a forgiving hold

**Not on the board. Built and checked on the Mac only (16 Nov 2026).** Test 17 with the box's own pages laid out
in 464 pixels, not 480, and the two-second hold for calibration made easier to do. Nothing else.

Why: the Lego tray's brick covers the screen's last 16 pixels on the right. Test 16 moved the strip of pieces out
from under it. Stephen, with test 17 in the tray: "Move the rest of UI slightly to the left like you did with the
bottom row". And, the same evening, of the hold that opens calibration: "can't get it to open".

- **One number.** `UI_W` in the new `ui.h` is 464: the width every page of the box's own is laid out in. `strip.h`'s
  `STRIP_W` is now that number. `ui_off(x)` says a place is under the brick; `ui_col(i, n)` is where the i-th of n
  columns begins.
- **The landscape's picture is still the whole screen**, 480 across, and a touch on it at x 464 to 479 is still
  the picture's. Everything below is the box's own pages.
- **Status line:** 69 letters after its 8 of state, not 72. Its lettering ends at x 460.
- **Calibration:** the ramps, ticks, swatches and their labels are across 464. The ten buttons are 92 or 93 wide by
  32 (were 96 by 32). LOOK is x 280 to 371 and CARD x 372 to 463, each 92 by 36 (were 96 by 36, at 288 and 384).
  The notes beside the sample row begin 4 pixels further left (x 186).
- **Effects:** the five columns are 92, 96, 92, 92 and 92 wide, by 45 (were five of 96). The second keeps 96 for
  SHIMMER, which is 91 across in the uncial hand.
- **Pad:** the pad is 368 wide (was 384). Its five buttons keep 96 by 54 and are 16 to the left, at x 368 to 463.
- **The FM-1's page:** every box ends at 464. A knob's cell is 116 wide (was 120); its lettering and bar are still
  108, with 4 either side, not 6. `knobctl.h`'s cell follows, so a knob is touched where it is drawn.
- **An effect's flash:** the colour still fills the screen to its edge (it is a colour, not something to read);
  the lettering is centred in 464, so 8 to the left.
- **A guest's page:** 25 and 37 letters a row (were 26 and 39), so its text turns a letter or two sooner. The rules
  and the right-hand heading end at 464.
- **A guest's two strip lines:** centred in 464, and cut at 77 letters. The lines `guest.c` writes are at most 64
  and 72 letters, so nothing it says is cut.
- **The card's page:** 77, 38 and 25 letters a line (were 80, 40 and 26). The three buttons are 154 or 155 wide by
  56 (were 160 by 56).
- **A touch that begins at x 464 or more is nobody's** on every page but the landscape's picture: it presses
  nothing, turns no page, holds no effect and opens no calibration, wherever it is dragged, until the finger is
  lifted. A finger that slides there from a button lets the button go.
- **The hold.** Calibration opens at the two-second mark with the finger still down (it was judged when the finger
  came up). That touch is then spent: nothing more is done with it or its lift. On the landscape a touch becomes a
  drag 20 pixels from where it began, not 10; from there on it is a drag and never a hold. The 20 is a guess.
- The nine files changed (`cal.c`, `card.c`, `fm1scr.c`, `guest.c`, `knobctl.h`, `main.c`, `perf.c`, `perf.h`,
  `strip.h`) differ from test 17's by the lines in `../hosttests/test18/changes.txt`: 91 out, 163 in. `ui.h` is new.
  Every other file is test 17's byte for byte (the titles and the USB name say 18).
- The screen is sent nothing new: no command, no register. The same fills and text at other places.

What the rest does is in [`../viz-test17/README.md`](../viz-test17/README.md).

## Host checks

    zsh ../hosttests/test18/run.sh [--quick] [DIR]

`DIR` gets the pictures of the pages in both looks, as PNG.

- Test 18 is test 17, the width and the hold: the files compared; the nine's changed lines are `changes.txt`'s and
  no others.
- `ui_check.c` (749 checks): each page drawn through `lcd.c` in Dark and in Light, and every pixel of x 464 to 479
  looked at. It must be the ground and nothing else (a flash: its colour and nothing else). The pages: calibration
  and its settings, the status line at its longest, the strip as it is, with a piece sent, with the last piece
  held, with a guest's lines and with lines of 79 letters, the card's page in each of its states, the effects, the
  pad, the FM-1's page with nothing plugged in, waiting, and with every box as full as it can be, a guest's page
  full, and all 27 flashes. Each page is also seen to reach its last 4 pixels (it did not stop short) where it
  should, and the flashes' and the guest strip's lettering to sit in the middle of 464.
  Then every place on the screen is asked of each page's own lookup (`cal_button_at`, `card_button_at`, the
  effects' cell, the strip's piece): none answers at x 464 or more, each button is one rectangle, and each row is
  464 across.
- The same question of the pictures test 11's, test 13's and test 7's checks make (31 in each look: the effects
  with some held, the pad, a guest's pages, the FM-1's pages, a warning, flashes).
- `touch_check.c` (21,557 checks): `main.c`'s own `poll_touch`, cut out and driven with a pretend finger. On the
  FM-1's page, a guest's, the effects, the pad, calibration and the landscape's two rows, a finger put down at
  x 464, 471 and 479 on every third row, dragged 160 pixels on to the page and lifted (1,653 touches): no button,
  piece, knob, effect, page or drawing. Held there three seconds: no calibration. The landscape's picture at
  x 472 to 479 still taps, holds and drags. At x 460 each page still answers. A finger sliding from LAND, from a
  piece or from a pad's button to x 470 lets it go.
- `hold.sh` (202 checks, and 15 deliberate breaks of its own, each caught): the hold, as above.
- 28 deliberate breaks of `cal.c`, `card.c`, `perf.c`, `perf.h`, `fm1scr.c`, `guest.c` and `main.c`, each putting
  something back under the brick, each caught.
- `earlier.sh`: test 17's `run.sh` against this folder, and inside it test 16's, 15's, 13's, 12's, 11's, 7's and 4's.
  They looked for things where they were, so eleven of their files are changed to run here; what is changed in each
  is listed at `earlier.sh`'s head, and each change is checked to have been made.

The whole run, `zsh hosttests/test18/run.sh`, on 16 Nov 2026: 26 and a half minutes, ending `earlier: all passed`
and `test 18: all passed`, with tests 13, 15, 16 and 17 each saying `all passed` inside it. On the computer only:
nothing of test 18 has been on the board.

The pictures were looked at, both looks: calibration and its settings, the status line, the strips, the card's
pages, the effects, the pad, the FM-1's pages (Journey, SETUP, KEY, LEAD, a warning, full), a guest's pages, two
flashes, no FM-1, waiting. Every page ends at 464 with the ground beyond it, and nothing was unreadable in either
look. On the card's page a long reason (`NO FAT VOLUME ON IT (FR_NO_FILESY`) is cut two letters sooner than it
was.

The checks are sums and pictures on a Mac. Whether 16 pixels is what the brick really covers, and whether buttons
4 pixels narrower are as easy under a finger, is for the board to say.

Not separately proven here: the card's page under `main.c`'s touch (its lookup is checked, its path through
`poll_touch` is pretended), and the status row's last two pixels before 464, which are cleared and not lettered.

## Build

As test 8's (see test 12's README), with the ARM compiler on the path
(`~/.pico/arm-gnu-toolchain-14.3.rel1-darwin-arm64-arm-none-eabi/bin`). 144 MHz build of 16 Nov 2026:
`build144/viz_test18.uf2` (370,176 bytes; SHA-256
`d10a77c97fb34f2cd2d7630b7ec9385655e9c3c017e9b9f85592596858f25668`). No warnings.

## Not tried

All of it, on the board. The build has not been loaded. Not the pages beside the brick, not a finger on the
narrower buttons or at the brick's edge, not the hold, not either look. Each earlier README's own "not tried" list
still stands.
