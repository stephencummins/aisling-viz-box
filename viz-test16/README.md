# viz-test16: the strip moved in from the right edge

**On the board since 16 Nov 2026 (the 144 MHz build below, loaded with picotool from the bootloader, over test 15, and verified). Stephen, with the box in the Lego tray, the same day: "the screen is perfect now".** Test 15 with the strip of
pieces stopping short of the screen's right edge, and nothing else.

Why: in the Lego tray a brick on the right covers the screen's last few pixels. Up to test 15 the strip is eight
cells of 60 across all 480, and TIR NA NOG, ten letters of 6, fills the last one to its edge, so the name's end is
under the brick. Stephen, 16 Nov 2026: "shift it to the left a bit".

- The strip is 464 wide, 16 short of the edge. What is right of it is black, and a touch there is nobody's.
- The cells are no longer one width, so the long names still fit: 56, 54, 50, 56, 56, 58, 64, 70 (SKELLIG to
  TIR NA NOG). Each name keeps 10 pixels or more about it; TIR NA NOG now ends 21 pixels from the screen's edge.
- All of it is in `strip.h` (the widths, where a cell starts, which cell a touch is in). `main.c` draws and reads
  touches by it: its changes from test 15's are in `../hosttests/test16/main_changes.txt`, 7 lines out, 8 in.
- Every other file is test 15's byte for byte (the titles and the USB name say 16).
- Not changed: the two lines the strip shows for an FM-1 that isn't running AISLING. They are centred on the whole
  screen and can be up to 78 letters, so a long one still runs under the brick.
- Nothing is sent to the FM-1 differently: a cell sends the same program change as before.

What the rest does is in [`../viz-test15/README.md`](../viz-test15/README.md).

## Host checks

    zsh ../hosttests/test16/run.sh [--quick]

- Test 16 is test 15 and the strip: the files compared; `main.c`'s changed lines are `main_changes.txt`'s and no
  others.
- `strip_check.c`, with the pieces as `main.c` names them: the cells fill the strip exactly; the strip stops short
  of the edge; every pixel of the row is in the right cell and those right of the strip in none; each name and tempo
  is drawn inside its cell with room about it; no cell is under 48 wide.
- `main.c` read: the touch and the drawing both go by `strip.h`, and the margin is cleared.
- 11 deliberate breaks of `strip.h` and `main.c`, each caught.
- Test 15's `run.sh` against this folder, and inside it test 13's and test 12's.

The checks show where things are put. How much the brick covers is the tray's: only the board in it can say whether
16 pixels is enough.

## Build

As test 8's (see test 12's README), with the ARM compiler on the path
(`~/.pico/arm-gnu-toolchain-14.3.rel1-darwin-arm64-arm-none-eabi/bin`). 144 MHz build of 16 Nov 2026:
`build144/viz_test16.uf2` (364,544 bytes; SHA-256
`2efd68b47918afc786b1a623d51aa1fb61da9ad76bc79eb950850ec602141579`). No warnings.

## Not tried

Each piece by finger in its new cell (not reported). The strip by eye in the tray is seen: TIR NA NOG clears the
brick. After the load the box came back as "AISLING viz test 16", found
the FM-1 and the card, and its settings file matched. A fault seen with this build that test 15 doesn't show is the
strip's. Each earlier README's own "not tried" list still stands.
