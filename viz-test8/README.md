# viz-test8

Test 8 for the AISLING visualiser box (Waveshare RP2350-PiZero, ELEGOO 3.5" SPI screen on header pins 1–26, the
FM-1 on the lower "PIO-USB" socket). Stephen said (2026-11-11): "build the latching grid and the XY pad". Test 7
turned the FM-1's knobs from the box; test 8 adds two pages for playing AISLING 1.3's PERFORM effects from it.

**On the board since 11 Nov 2026 (the 144 MHz build of that day's first commit, `44b5f743…`, loaded with picotool from the bootloader and verified). Stephen tried the grid and the pad with the FM-1 that day: "It's brilliant." The PAD button letting go of what's held came after and is not on the board yet.** It is checked on the Mac, against AISLING
1.3.1's own `ui.c` (see Host checks).

1. **The effects page: all 27 PERFORM effects, each a cell. Tap one and it's held; tap it again and it's let go.**
   Several can be held together. A held cell is filled with the effect's own colour (the colours of the FM-1's
   flash); the others show their names in that colour. The four white-key groups are the top four rows, left to
   right as on the FM-1 (Rhythm, Echo, Space, Colour); the eleven black-key effects fill the right-hand column
   and the rows under.
2. **ALL OFF** lets go every effect, including one something else is holding (a DAW, say).
3. **The pad: one finger moves two of PERFORM's knobs at once.** Across is Filter and up is Wet to start with.
   The ACROSS and UP buttons each step to the next of the four (Filter, Wet, Energy, Chorus) the other hasn't got.
   An orange line and a green line cross where the two values are; the buttons show each value as AISLING says it
   ("OPEN 34", "+46"). Filter, Wet and Energy rest in the middle (about 19 px wide, so "Off" and "0" are easy to
   find). RESET puts all four back as a piece starts.
4. **Getting there:** a tap on the landscape goes to the FM-1's page as before, a tap there now goes to the
   effects, and the effects and the pad have buttons for each other and for the landscape (LAND). On these two
   pages a tap is never a page flip, and the 2-second hold for calibration works only from the landscape and the
   FM-1's page.
5. **Going to the pad lets go of everything held** (Stephen, 11 Nov 2026: "when it switches to pad, just turn off
   any locked effect"): the PAD button sends each held effect's CC at 0 before the page changes, so nothing is left
   on where no cell shows it. Going to the landscape or the FM-1's page keeps what's held; the status row shows
   `HOLD 3` while any are.
6. **The uncial hand.** The names on both pages are in Uncial Antiqua, the font of AISLING's own page names. The
   landscape, the FM-1's page, the flash and calibration are as they were, in the plain font.

Everything else is test 7's, and `fm1scr.c`, `knobctl.c`, `land.c`, `tone.c` and the tables are test 7's byte for
byte. Nothing is stored on the box or the FM-1, and AISLING is unchanged.

## How it works

AISLING 1.3 takes a CC for each effect (`ui.c`'s `cc_in`): 20 to 31 for the first twelve and 102 to 116 for the
rest, on any channel and any page; 64 or more holds it, less lets it go. The box sends 127 and 0. Filter, Wet,
Energy and Chorus have fixed CCs of their own (74, 78, 79, 80), which work on any page too, so neither page needs
the FM-1 to be on PERFORM. On PERFORM the FM-1's own screen shows them; elsewhere you only hear them.

- **A tap acts on the second touch reading in one cell,** not when the finger lifts, and does its one
  thing: a finger held down or slid across the grid changes nothing more. One stray reading can't hold an effect.
- **The page buttons act as the finger lifts,** so the same touch can't land on the next page.
- **The FM-1 lets go of everything a CC holds when it leaves PERFORM** (`perf_set(0)`). The box hears the page
  change and clears its cells to match. The same when the FM-1 is unplugged.
- **With no FM-1, a tap does nothing** and lights nothing.
- **The pad sends, for each value, the CC AISLING turns into exactly that value** (the same table as test 7's
  knobs), and only when the value changes. A reading more than 40 px from the last is held until the next agrees
  (a resistive panel's glitches), and 3 px of jitter moves nothing. A touch that starts on the pad stays the
  pad's; one that starts on a button never moves a knob.
- **The pad doesn't know a knob's value until it has sent one,** so the lines appear with the first touch. The
  four are not saved by AISLING and nor are they here.
- **Sending:** the 31 CCs join test 7's four in the same latest-value slots, so a fast drag never queues up.

The font is in flash (`ufont.h`: 15 px and 24 px, 64 characters each, 4-bit coverage, 22,600 bytes), blended over
the cell's colour and sent with `lcd_blit`, new in `lcd.c`. `hosttests/test8/gen_ufont.py` makes it from
fm1-aisling's `assets/fonts/UncialAntiqua-Regular.ttf`.

**Uncial Antiqua** is by Brian J. Bonislawsky (Astigmatic), © 2011, under the SIL Open Font License 1.1, with the
Reserved Font Name "Uncial Antiqua". The licence is beside this file, `UncialAntiqua-OFL.txt`. The font is not
changed, only drawn at two sizes.

## Serial keys

`e` the effects page, `x` the pad; with `n` on, each CC is logged (`effect Stutter held, CC 20 = 127`,
`pad Filter Open 34, CC 74 = 85`). The snapshot (any key that isn't a command, `?` say; `s` is the SPI speed) has a line for these pages: held, holds, lets, moves, taps
refused for want of an FM-1, glitches. The rest are test 7's.

## Build

    export PATH="$HOME/.pico/arm-gnu-toolchain-14.3.rel1-darwin-arm64-arm-none-eabi/bin:$PATH"
    export PICO_SDK_PATH=$HOME/.pico/pico-sdk
    cmake -S . -B build -G "Unix Makefiles" -Dpicotool_DIR=/opt/homebrew/lib/cmake/picotool
    make -C build -j8
    cmake -S . -B build144 -G "Unix Makefiles" -Dpicotool_DIR=/opt/homebrew/lib/cmake/picotool -DVIZ_SYS_MHZ=144
    make -C build144 -j8

Output (11 Nov 2026): `build/viz_test8.uf2` (298,496 bytes; SHA-256
`99a29e327d119f9f297218b0f3deaf122169357471102999b87f0a8cc66d6a40`) and, at 144 MHz as on the board,
`build144/viz_test8.uf2` (298,496 bytes; `526dc6d2d00717378e417caf278290708affdee188d224ba956e03f90edeab42`).
No warnings. Code 152,860 bytes (test 7 123,076), most of it the font; RAM grows by 3,324 bytes.

## Host checks

Mac, AddressSanitizer + UBSan; `../hosttests/test8/run.sh` (`run.sh DIR` also writes pictures of the two pages
into DIR; `--no-sweep` leaves out test 7's 22-second sweep). All passed on 11 Nov 2026:

- **Test 7's whole `run.sh`, against test 8's folder:** the shared files are test 7's byte for byte, the pump
  code is cut from test 8's `main.c`, the FM-1 page, the knob drags, test 4's screen and calibration checks, and
  its 31 deliberate breaks, 31 caught.
- **`perf_check.c` units (3,302 checks):** the grid is each effect once and three buttons; the CC numbers; a tap's
  one toggle, a long and a sliding touch, no FM-1, ALL OFF, PERFORM left; the pad's ends, every CC in order, the
  rest in the middle, jitter, a glitch, a real jump, the edges, the axes never on one knob, reset. **The
  drawing:** every word sent to the screen is decoded; a full redraw covers every pixel above the status row and
  nothing in it; and over 600 random touches on each page, what was drawn bit by bit is pixel for pixel a fresh
  full redraw. (That found one fault: the old cross's square was left behind.)
- **Through AISLING's own `ui.c`:** the CCs the box sends for a run of touches (592 of them: each effect held and
  let go, 120 random taps with ALL OFF among them, a DAW's hold cleared, PERFORM entered and left, then the pad
  over all twelve pairings of knobs) are played into fm1-aisling's host build, built into a temp directory. After
  each, AISLING must hold exactly the effects the box shows (`expect pcc`) or say the value the pad shows
  (`said Filter Open 34`): 557 held or said as sent, none not, nothing saved.
- **19 deliberate breaks of `perf.c`, 19 caught.**

`run.sh` uses `~/Projects/fm1-aisling`, or a worktree beside it, whichever has 1.3's generated headers; this run
was against 1.3.1 (`aisling-drafts/rel131`, ae2e804).

## Not tried

All of it, on the board: how a 96 x 45 px cell is to hit with a finger, the uncial at this size on this panel, the
pad's feel on a resistive screen, whether redrawing a cell is quick enough to feel instant, and the frame rate of
the landscape, which should be untouched. AISLING's effect CCs have never been sent to a real FM-1 from anything.
