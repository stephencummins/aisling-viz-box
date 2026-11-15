# viz-test14: the card and any firmware, in one build

**Merged, host-checked and built for the board 15 Nov 2026; not on it, untried.** Test 12 (the settings on a
microSD card, and the card as a USB drive) and test 13 (an FM-1 that isn't running AISLING) were each written on
test 11, apart. This is the two together, and nothing else: there is no new code in it.

- Every file but `main.c` is test 12's byte for byte (the titles and the USB name say 14; `CMakeLists.txt` adds
  `guest.c`). `guest.c` and `guest.h` are test 13's.
- `main.c` is test 11's with both sets of changes. They met in one place, `redraw_all()`, where test 12 put the
  card's page first and test 13 redraws the guest's page with the FM-1's; both are kept.
- The two don't touch each other. The card holds the box's own settings (warmth, the screen, palette, ground, the
  pad's two knobs), whoever is plugged in; applying a file sends nothing to the FM-1. The pad's knobs from a file
  are kept while a guest is plugged in, and used when AISLING is back: only `may_send()` lets anything out.
- Serial keys: test 12's `u` (the card's page) and test 13's `i` (a silent FM-1 taken for AISLING).

What each does is in its own README: [`../viz-test12/README.md`](../viz-test12/README.md) and
[`../viz-test13/README.md`](../viz-test13/README.md).

## Host checks

    zsh ../hosttests/test14/run.sh [--quick]

Test 14 has no checks of its own. The script checks that it is the two tests and nothing more (the files compared;
`main.c`'s changes from test 12's must be test 13's changes from test 11's, 122 lines, and its changes from test
13's must be test 12's, 269 lines), then runs each test's own `run.sh` against this folder: test 13's 11,000 unit
checks and 32 deliberate breaks, test 12's checks and 47 deliberate breaks, and inside that test 11's whole suite.
All pass; every break is caught.

## Build

As test 8's (see test 12's README). 144 MHz build of 15 Nov 2026: `build144/viz_test14.uf2` (364,032 bytes; SHA-256
`16715355ab53918ae4e7ec274ee8da77a57e1b0ab60b1631d37b00362d2cc7b3`). No warnings.

## Not tried

All of it, on the board. Test 12 has been on the board with no card in the slot (15 Nov 2026: the Mac takes the new
USB setup, the screen and touch are undisturbed, the card's page says there is no card); nothing that needs a card
has been tried. Test 13 has not been on the board. Each README's own "not tried" list still stands.
