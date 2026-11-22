# AISLING visualiser, test 26

Test 25, with one fault put right: **a card lent to the Mac would not mount.**

## The fault

Found on 22 November 2026, with test 25 on the board. MOUNT ON MAC lent the card and the Mac saw it (31.9 GB,
FAT32), but never mounted it: "failed to mount". The Mac's log gave the reason, a read error from its FAT module
(`com.apple.fskit.msdos`, error 5) after an attempt of 21 seconds in which the box counted about 2 MB read. The
card was sound: the Mac's read-only check of it (`diskutil verifyVolume`) passed, 124 files, no errors.

Test 25 made the paint step thirty times a second off the landscape, each step and its colours about 28 ms. The
box answers the Mac from the same loop, so on the card page it had about 5 ms in every 33 for the Mac's reads:
about 100 KB a second. Up to test 24 the paint stepped ten times a second there, and the card mounted.

With the change below the card mounted on the board (see Checked).

## The change (main.c, HDMI build only)

- While the card is lent to the Mac, the paint steps ten times a second again (as on the page that installs
  firmware onto an FM-1). The monitor's picture moves less smoothly for as long as the Mac has the card.

Everything else, and how to build, load and go back, is in [test 24's README](../viz-test24/README.md), with
`viz_test26` for `viz_test24`.

## Checked

- On the Mac: `zsh hosttests/test26/run.sh` (only the three files changed, by the lines counted).
- Built: `build252/viz_test26.uf2`, 418816 bytes.
- On the board, 2026-11-22 (the monitor's lead unplugged): it starts at 252 MHz, meets the FM-1 and finds the
  card. **The lent card mounted on the Mac within seconds**; a file was copied to its `FM1` folder, the card was
  ejected, and the box went back to CARD OK. Stephen then installed that file onto an FM-1 from the card.
  Not tried: lending the card with the monitor connected.
