# AISLING visualiser, test 25

Test 24, with one fault put right: **the big picture stopped when the touch screen left the landscape.**

## The fault

Stephen found it on the day test 24 was merged: with the landscape on the monitor, tapping through to the FM-1's
page (or any other) froze the monitor's picture. So the box couldn't be worked during a performance.

The paint itself never stopped: off the landscape it steps ten times a second, so notes still land. But the
HDMI lines are made from the paint's *colours* (`land_cells`), and those are worked out by `land_prepare`, which
the main loop only called while the touch screen was showing the landscape.

## The change (main.c, HDMI build only)

- After every step of the paint off the landscape, `land_prepare` is called too, so the colours follow the paint
  on every page. Nothing of it is sent to the touch screen there; coming back to the landscape redraws it whole,
  as it always did.
- Off the landscape the paint steps thirty times a second in place of ten, so the monitor's picture moves
  smoothly. The page that installs firmware onto an FM-1 keeps ten.

The build without HDMI is test 24's (and so test 22's) but for its name. Everything else, and how to build,
load and go back, is in [test 24's README](../viz-test24/README.md), with `viz_test25` for `viz_test24`.

## Checked

- On the Mac: `zsh hosttests/test25/run.sh` (only the three files changed, by the lines counted).
- Built: `build252/viz_test25.uf2`, 418,816 bytes.
- On the board, 2026-11-22: it starts, meets the FM-1 and finds the card; HDMI 60 frames a second, lanes run
  dry 0, 1 late line at start-up; the landscape page as test 24's (35 frames a second). **Stephen changed pages
  and the monitor's picture kept moving: "it works".** Not measured: how much slower the other pages are for
  the extra work, and the HDMI figures while on them.
