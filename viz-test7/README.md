# viz-test7

Test 7 for the AISLING visualiser box (Waveshare RP2350-PiZero, ELEGOO 3.5" SPI screen on header pins 1–26,
the FM-1 on the lower "PIO-USB" socket). Stephen said (2026-11-10): "I can have just the visualiser, or use the
screen as an additional visual interface for the FM1." Test 6 put the FM-1's screen on the box as a second page;
test 7 lets you turn the FM-1's four knobs from it.

1. **Drag sideways on one of the four knobs on the FM-1's page, and that knob turns on the FM-1.** Right is more.
   The drag starts from the value the FM-1 last said and steps through that knob's own values in order, so
   nothing jumps. A knob's whole range is about 300 px (each step 3 to 24 px), and nothing moves until the finger
   has gone 10 px.
2. **What's being sent shows in orange, with a bar under it** for where it is in the knob's range, until the FM-1
   says the knob back. Then it's the FM-1's own value, as in test 6.
3. **If the FM-1 doesn't take it,** its own value comes back and THE FM-1 DIDN'T TAKE IT shows in red over the
   header for 4 seconds.
4. **Knobs the box can't turn have their labels dimmed:** Setup's four (AISLING takes no CC for Tune, MIDI out,
   Lights or Save), and Mode while it says "Custom".
5. **A touch that starts on a knob is the knob's:** it never flips the page or brings up calibration, so a finger
   resting on a knob is safe. One on a dimmed knob that doesn't move is a tap or a hold as anywhere else.
   Everywhere else on the page, a tap and a 2-second hold are test 6's.
6. **The landscape is test 6's, unchanged** (land.c, tone.c and hot.h byte for byte), and still the start. If
   you never tap over to the FM-1's page, this is test 6.

Everything else is test 6's: gamma FALLING at every start-up, warmth 2, the keys, the clock option. Nothing is
stored on the box or the FM-1.

**Tried on hardware (10 Nov 2026):** on Stephen's FM-1 on AISLING 1.2, a drag on Motion took it from 21 to 58,
in orange while it turned, and the FM-1 showed NOT SAVED. Everything here is also checked against AISLING 1.2's own
`ui.c`, built for the Mac.

**For AISLING 1.3 (11 Nov 2026: built and on the board, loaded through BOOT; Stephen turned Chorus from the box on the FM-1 that day and it worked; the box did not show as a serial port on the Mac that day, cause not found):** PERFORM's fourth knob is Chorus
(Off, then 1 to 100) where 1.2 had Sweep, so `fm1scr.c`'s label and `knobtab.h` are 1.3's now (24 lists, 967
values), made from `hosttests/test7/corpus.txt`, everything 1.3 says (8,982 texts; test 6 keeps 1.2's). A held
key's effect name on PERFORM ("Stutter", "Tape stop") is taken as a message. `run.sh` passes against 1.3's `ui.c`
(`FM=~/Projects/aisling-drafts/fw-1.3` until 1.3 is merged): the sweep's 13,312 CCs, all 24 lists, 28 breaks of
28 caught. The numbers under Host checks below are 1.2's run. The `.uf2` on the site is still
1.2's table: against a 1.3 FM-1 it shows Chorus as a message, not on the fourth knob, and can't turn it.

**PERFORM's flash (11 Nov 2026, for AISLING 1.3; on the board since that day, loaded through BOOT and verified, untried with the FM-1):** when a key is held on PERFORM
the FM-1 fills its screen with the effect's colour and its name. The box does the same over y 0-271, on the
landscape and on the FM-1's page: the name large (the 5x7 font at up to 8 times, so `lcd_text` takes scale 8 now),
the effect's line under it, black on the effect's own colour. `pflash.h` holds the 27 names, lines and colours,
made by `hosttests/test7/gen_pflash.py` from AISLING's `ui.c`. AISLING says an effect's name when its key goes down
and nothing when it's let go, so the flash is timed: one second from the last name heard (`FM1_FLASH_MS`), then the
page is drawn again. A longer hold outlasts it. Calibration is never covered. No change to AISLING.

## How it turns the knobs

AISLING 1.2 already takes them: CC 16 to 19, on any channel, are the four knobs of whatever page is on its screen
(`ui.c`'s `cc_in`), 0 to 127 across each knob's range. No firmware change. `cc_in` never saves, so NOT SAVED shows
as it does for a knob turned on the FM-1.

The box knows each knob's values in order (`knobtab.h`, in flash, made from the corpus and `ui.c` by
`hosttests/test7/gen_knobtab.py`). For each step it sends the middle of the CCs AISLING turns into that value, so
rounding can't land it on a neighbour.

- **Whole steps only.** The knob moves when the finger is a whole step past it, to the last whole step passed
  (not the nearest). A resistive panel's jitter is under a step, so it can't send a value back and forth.
- **A fast drag never queues up.** Core 0 sets each knob's latest CC; core 1 sends whatever is newest, so the
  last value always goes. Core 1's order: a piece change, then the knobs, then a WHERE, then HELLO.
- **A second drag before the FM-1 has said the first back** starts from what was sent, not the FM-1's old value.
- **A page change on the FM-1 mid-drag** ends the drag.

### Knowing it was taken

AISLING says a turned knob back about 120 ms later ("Evolve 41"). Not always, though; the host sweep found three
ways it doesn't:

- a turn that moves another knob too is said as that one (the Chords page's Play turned to Endless turns the slots,
  and is said "Chord Minor");
- Motion moving a knob at the same moment is said instead;
- now and then a turn isn't said at all.

So if a knob hasn't been said back in 300 ms, the box asks for the whole page (a WHERE), and the page that comes
back settles it. If that shows something else, or nothing has come in 1.5 seconds, it wasn't taken.

**Motion moves four knobs by itself:** Filter, Wet, Energy and Sequence 2's Octave. A turn holds the macros only
until the next section change, then they glide back. For those, a page that's nearer what was sent than what the
knob was before the drag counts as taken, then moved. One step back from a one-step drag can't be told apart from
not taken, so that one warns.

**The MODE say drops the mode's full stop:** the page shows "Mixolyd." and the FM-1 says "MODE Mixolyd". The box
looks past trailing full stops, but only when there's no exact match, because Echo time has both "1/4" and "1/4.".

## What to expect

On the FM-1's page:

- Drag a knob: its value goes orange with a bar under it, and steps as you go. Let go and, about a tenth of a
  second later, the FM-1 says it back: the bar goes, and the value stays orange for 2 seconds as any turned knob's
  does, then goes white.
- Turn a knob on the FM-1 while the box's page is up: as test 6, it shows straight away.
- The grid up on the FM-1: AISLING ignores the CCs, so every drag ends in THE FM-1 DIDN'T TAKE IT.
- On Setup, all four labels are grey and a drag does nothing.

## Reading the log

    python3 -I log.py /dev/cu.usbmodem101 10

New in the snapshot, once a knob has been dragged:

    knobs from the box: 12 drags, 140 steps (CCs sent 140, failed 0), 138 taken, 2 not taken (5 WHEREs asked,
      5 settled by the page, 1 taken then moved by Motion); 0 touches on a dimmed knob, 0 drags cut short, 0 glitches

(The numbers are made up, to show the shape.)

- **not taken** should be 0 with the grid down. If every drag is not taken, the CCs aren't reaching AISLING.
- **CCs failed:** the FM-1's USB didn't take the packet. It should be 0.
- **glitches:** touch readings far from the last, held until the next agreed. A few are normal.

New key: `n` logs each step the box sends, as `knob Evolve 40 -> 41, CC 17 = 52`. With `a` as well (what the FM-1
says), a step and its answer sit one under the other. The rest are test 6's.

## Build

    export PATH="$HOME/.pico/arm-gnu-toolchain-14.3.rel1-darwin-arm64-arm-none-eabi/bin:$PATH"
    export PICO_SDK_PATH=$HOME/.pico/pico-sdk
    cmake -S . -B build -G "Unix Makefiles" -Dpicotool_DIR=/opt/homebrew/lib/cmake/picotool
    make -C build -j8
    cmake -S . -B build144 -G "Unix Makefiles" -Dpicotool_DIR=/opt/homebrew/lib/cmake/picotool -DVIZ_SYS_MHZ=144
    make -C build144 -j8

Output: `build/viz_test7.uf2` (233,984 bytes; SHA-256 `f400472c…9354a73f48`) and, at 144 MHz as test 5's on the
board, `build144/viz_test7.uf2` (233,984 bytes; `60510ee6…ad48945adc`). No warnings. The knob table is in flash
(code 120,812 bytes, test 6 109,060); RAM grows by 372 bytes.

## Host checks

Mac, AddressSanitizer + UBSan; `../hosttests/test7/run.sh` (`run.sh DIR` also writes pictures of the page into
DIR; `--no-sweep` leaves out the 22-second sweep). All passed:

- **The knobs against AISLING's own `ui.c`.** `run.sh` builds fm1-aisling's own host build of AISLING 1.2
  (`amb_host`) into a temp directory; nothing in fm1-aisling is written or changed. If it isn't there, these are
  left out with a note.
  - **The sweep:** every CC 0 to 127 to every knob on 26 pages (all but Mix, which the host's buttons don't
    reach), 13,312 CCs, each followed by a WHERE. Every page came back as the box's table has it (5 of Motion's
    knobs a step or two off, as expected), all 23 lists, and Setup's 512 CCs moved nothing. 8,071 turns were said
    back; 2 weren't: Chords' Play (said as "Chord Minor") and once Perform's Wet. 21,926 checks.
  - **The drags:** 32 drags on 8 pages through `knobctl.c`, turned into a script for `amb_host`; what the real
    `ui.c` said went back into the box's model. Every knob ended where the finger left it, 1,136 CCs, none not
    taken. Each drag's first send was from the knob's value as said, and every send one step on from the last
    (two, once, where the finger outran a 3 px step). 1,534 + 1,506 checks.
- **`knobctl.c` on its own** (`knobctl_check.c`, 2,243,056 checks): the 10 px start, whole steps both ways, the
  ends, a panel's jitter and glitches, taps and holds in the knob row, a page change mid-drag, said back and not,
  the WHERE and the page settling it, Motion's knobs, the clock's wrap, a second drag before the answer, dimmed
  knobs never sending, and a soup of random touches and FM-1 texts.
- **The knobs' table** made again from the corpus and fm1-aisling's `ui.c`, `amb.c` and `amb.h`: byte for byte
  `knobtab.h` (104 knobs in 23 lists, 866 values).
- **The FM-1's screen** (`fm1scr_check.c`, 7,529,207 checks): test 6's (everything AISLING 1.2 says, end to end
  through `announce.c` and `reader.c`, every text cut at every length, 120,000 random texts, every pixel), plus the
  knobs from the box: what's being sent and its bar to the pixel, the dimmed labels, the warning and its 4 seconds,
  a knob said back counted (the Key page's MODE too), and 3,000 random steps of all of it, each drawn change by
  change and checked against a fresh redraw.
- **28 deliberate breaks of `fm1scr.c` and `knobctl.c`, each caught:** test 6's nine, and a drag that jumps
  (absolute, not relative), the CC one past the middle of its run, no 10 px start, a tap on a knob flipping the
  page, no warning, Mode draggable on Custom, Setup's knobs sent, no WHERE asked, the MODE full stop not looked
  past, the nearest step instead of the last whole one (jitter), Motion's knobs warned as not taken, a page change
  not ending the drag, a second drag starting from the FM-1's old value, the value being sent drawn white, the
  bar's length out, the bar on the rule, a locked label not dimmed, the MODE say not counted, the Chords page's
  turns not asking for the page.
- **Test 6's paint unchanged:** land.c, land.h, tone.c, tone.h and hot.h byte for byte. **main.c's pump** against
  a stand-in DMA: 401,968. **The reader:** 989. **Test 4's own checks** against test 7's files: 116 + 6 passed,
  test 3's harness OK.

## Load (Stephen's call, each time)

Test 5's 144 MHz build is on the board (test 6 has never been loaded), so no BOOT button. With the screen on and
"USB" in the Mac:

    stty -f /dev/cu.usbmodem101 1200    # the 1200-baud touch: reboots into the bootloader
    picotool load -x build144/viz_test7.uf2

If that fails, the first-load way: screen off, hold BOOT, plug "USB" in, drag the .uf2 onto `RP2350`. Never power
both sockets from two sources at once: they share one VBUS.

Test 7 is test 6 plus the knobs, so loading it also tries test 6's page for the first time. To go back: test 6
(`../viz-test6/build144/viz_test6.uf2`, `466e0d5b…`) or test 5 (`../viz-test5/build144/viz_test5.uf2`,
`56a0b3d2…`), the same two lines.

## If something's wrong

- **Every drag says THE FM-1 DIDN'T TAKE IT:** first check the grid isn't up. Then `n` and `a` together: if the
  box's steps go out and the FM-1 says nothing back, the CCs aren't reaching `ui.c`; the snapshot's "CCs failed"
  says whether USB took them.
- **It jumps, or turns back on its own:** `t` logs the touches; say which knob and which way.
- **A knob turns the wrong amount, or lands a step off:** `n` and `a`; the log has the CC sent and what the FM-1
  said.
- **A tap on a knob flips the page, or a drag brings up calibration:** it shouldn't; `t`, and say where.
- **It hangs or the USB drops:** unplug and plug back in, then load test 6 or test 5.
