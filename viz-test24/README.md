# viz-test24: the landscape out of the HDMI socket

**On the board since 22 Nov 2026 (Stephen: "load it").** The log: 252 MHz, 60 frames a second sent, no lane run
dry, 1 late line at start-up and none after; the picture's work 6293 microseconds a frame of 16667 (38% of core 0),
23 at the longest turn; the touch screen 35 frames a second where test 23 had 55 (step 22 ms, prepare 6); the FM-1
met and its screen read over the USB host. **The Arzopa shows the landscape** (Stephen: "I can see the visualiser!"). Its shape and speed: not yet judged. Test 23 with the landscape on the big screen in place of
the colour bars (HDMI build only, `-DVIZ_HDMI=ON -DVIZ_SYS_MHZ=252`). The touch screen, the card and the FM-1
through the box are as they were. Without `VIZ_HDMI` the build is test 22 with its name changed.

## How

Test 23 kept eight ready-made lines. A moving picture can't be kept: a frame of it as the socket wants it is over a
megabyte, and the board had about 7 KB spare. So the picture is worked out while it is being sent (`hdmipic.c`,
and the interrupt in `hdmi.c`):

- it is **320 x 120 points**, each two pixels wide and four lines tall, filling the 640 x 480;
- every point is blended from the four paint cells around it (the landscape is 120 x 68 cells, so there are more
  points than cells both ways), then taken down to 64 levels a colour with a 2 x 2 dither;
- the two pixels of a point are looked up as one 20-bit pair that sends as many ones as zeroes, so any point can
  follow any other;
- two rows' worth of lines are kept (9000 bytes in all with the blanking, 1000 more than test 23). While one row
  goes out four times, the line interrupt on core 0 works the next out in three turns.

Not on the big screen: the LCD's canvas grain and its warmth (the first is finer than these points, the second is
a correction for that panel), and the status lines under the picture.

## Keys

`H` changes the picture's shape: the whole 640 x 480 (for a screen that spreads it over its 16:9 face, as most
do), or a 16:9 band of 640 x 360 with black above and below (for one that keeps 4:3). `D` steps the pin drive, as
in test 23. Any other key: the snapshot, which now gives the picture's work in microseconds a frame.

## Untried, and what to look for

- **The work takes time from the landscape.** Counted from the compiled code, a row is about 60 microseconds and
  a frame about 7 ms of every 16.7: not far off half of core 0. The touch screen's 55 frames a second will fall,
  perhaps to 30. The snapshot gives the real figure. If it is too slow there are cheaper pictures (160 points
  across, or six lines to a row).
- **The longest turn must stay under a line (32 microseconds).** Counted: about 25. Over that, lines are late and
  the snapshot's "lines late" rises.
- **RAM:** 11.2 KB between the program and the stacks where test 23 had 15.0; the queues take about 6.8.
- Everything test 23 left untried still is, but for what it proved: the FM-1 has not been heard playing at 252 MHz.

## Checked on the Mac

`zsh hosttests/test24/run.sh`: the 65 levels (each pair is the coding's, ends level, within 1 of its value); whole
pictures in both shapes, 691,200 points, each decoded by a decoder written apart from the encoder and held to a
level worked out in the check; two deliberate breaks caught; test 23's own check again; and every other file the
same as test 23's but for the counted lines.

## The way back

`viz-test23/build252/viz_test23.uf2` (the bars), or test 22's `build144/viz_test22.uf2` (no HDMI, 144 MHz).

---

# viz-test23: a test picture out of the HDMI socket

**Ran on the board on 22 Nov 2026, and the Arzopa showed the picture.** The log: 252 MHz, 60 frames a second sent,
no lane run dry, 3 late lines at start-up and none after; the flash held (timing 60007203 to 60007406); the FM-1
(as FX-1) met and answering over the USB host; the touch screen at 55 frames a second, SPI 21 MHz. Stephen saw the
bars white to black, the white frame top, bottom and right (the left edge can't be told from the white bar beside
it: the picture's fault), at the first pin setting (2 mA, slow edges). Not confirmed: the FM-1 heard playing.

Test 22, and in one build of it (`-DVIZ_HDMI=ON`) a still
picture out of the board's mini-HDMI socket: 640 x 480 at 60 Hz, eight colour bars inside a white frame one pixel
wide. Everything else runs as in test 22: the touch screen, the card, the FM-1 through the box.

Stephen plugged an Arzopa portable screen into the socket and asked how to make it show something. Nothing in
tests 1 to 22 drives that socket, so this is the first step: does a screen lock to what the board sends, and does
the FM-1 keep playing through the box while it does. The landscape on the big screen comes after, if this works.

## What it has to do

The socket is wired straight to eight pins (GPIO32 to 39) through resistors; there is no video chip. The bits are
made here (`hdmi.c`, `tmds.c`, both written from the DVI specification and the RP2350's datasheet):

- three PIO state machines send one bit each tick of the system clock, a fourth makes the pixel clock;
- every line is worked out once at start-up and kept in RAM (8000 bytes), and the DMA feeds the lines out;
- once a line, a short interrupt on core 0 names the line after next.

## What is different from every test before, and untried

- **The chip runs at 252 MHz. It is rated for 150.** One bit a tick at 640 x 480 needs 252 Mbit/s. The core's
  voltage is raised from 1.10 V to 1.20 V for it (inside what the regulator allows without unlocking). People run
  RP2350s like this, but this board never has.
- **The flash's clock is divided further** before the change, so the flash sees a clock no faster than it had.
  The log shows the timing word before and after.
- **The landscape's code runs from flash, not RAM**, in this build only: the lines needed the room (`hot.h`).
  The frame rate on the touch screen may be different.
- **The screen's and the card's SPI run from 126 MHz** (half the clock): the touch screen gets 21 MHz where it had 24.
- **The USB host's timing** is worked out from the clock by Pico-PIO-USB with dividers it has not had here before
  (5.25 and 2.625). Whether the FM-1 is still met at 252 MHz is one of the things to find out.
- If the interrupt is ever kept waiting a whole line (32 microseconds), the picture slips for a frame; the log
  counts these ("lines late").

If the build does not start at all, the box shows nothing and its serial port does not appear. The way back is the
BOOT button: hold it while plugging in the "USB" socket, and load test 22.

## Building

    cmake -G "Unix Makefiles" -DVIZ_SYS_MHZ=252 -DVIZ_HDMI=ON -B build252 && make -C build252 -j8

Without `-DVIZ_HDMI=ON` (`-DVIZ_SYS_MHZ=144 -B build144`, as before) the build is test 22 with its name changed:
the same size to the byte.

## Trying it

A mini-HDMI lead from the board's socket to the Arzopa, and the Arzopa powered from its own supply, not from the
box. Then the log (`log.py`):

- `HDMI: 640 x 480 test picture, N frames sent; lines late 0, lanes run dry 0` is what a healthy stream says;
  frames go up by 60 a second.
- **D** steps how hard the pins drive the cable: 2 mA slow edges (the start), 2 fast, 4 slow, 4 fast, 8 fast,
  12 fast. If the screen says "no signal" or sparkles, try the others.
- The picture: all four white edges showing means the screen has the whole 640 x 480. Bars white, yellow, cyan,
  green, magenta, red, blue, black, left to right, means the three colour lanes are the right way round.

## Checked on the Mac

`hosttests/test23/run.sh`: `tmds.c` against a decoder written apart from it (every value at every balance, the
control symbols, whole lines bit for bit as they are sent), a deliberate break that must be caught, and that
nothing else in the folder differs from test 22 but the lines meant to. Not checked, because only the board and
a screen can: `hdmi.c`, the clock, the picture.

What follows is test 22's page, as it was.

---

# viz-test22: the card page says MOUNT ON MAC and UNMOUNT

**On the board since 21 Nov 2026.** Test 21 with the card page's words changed. Nothing else is different.

Stephen: "I'm a bit confused by 'TO THE MAC' as a phrase." The button lends the card to the computer as a USB
drive, and takes it back, so it now says what a Mac calls that:

- the button: **MOUNT ON MAC**, and **UNMOUNT** while the card is lent (was TO THE MAC, TAKE IT BACK)
- the status: **ON THE MAC** (was AT THE MAC), and "MOUNTED ON THE MAC: THE BOX KEEPS OFF"
- the help lines, the firmware page ("THE CARD IS MOUNTED ON THE MAC") and the log say the same

Seen on the board by Stephen: "Yes, that's good." Ejecting the card on the Mac still gives it back by itself.

What follows is test 21's page, as it was.

---

# viz-test21: a true percentage, and no false "ANSWERS AS"

**On the board 21 Nov 2026.** Test 20 with two faults in the install page put right (`swap.c`, `swap.h`).

- **The percentage is the write's.** The box cannot see the FM-1 write; it sees which parts of the package the
  FM-1's loader asks for. Test 20 counted every read, so the number ran ahead while the loader was still looking
  the package over. The loader reads the app area (`0x4000` to `0x93000`, 143 sectors) in one unbroken run, so
  `served()` now follows that run (`run_lo`, `run_end`) and the percentage is how far along it is, of
  `SWAP_APP_SPAN`.
- **No "ANSWERS AS" when the answer is right.** After the restart the FM-1 can answer with its product number cut
  short. `swap_same_product()` takes an answer that is the start of the product, with the model and at least four
  characters more, as the same; a different model or a different number is still a mismatch.

Tried on the board with an FM-1 (Stephen pressing INSTALL): Choralroot 0.41, then Handy-1 0.3.0, then AISLING 1.5,
one after another from the card with nothing restarted. All three went on. That is also the first time firmware by
other people was put on from the card, and the way back from each.

Not done: there is no `hosttests/test21`. `swap_check` was run by hand from `hosttests/test19` against this
folder; `fw_check` there has been out of date since test 20's long names and fails the same on test 20 and 21.
What follows is test 20's page, as it was.

---

# viz-test20: the install made to work, and a quicker, livelier touch

**On the board since 21 Nov 2026.** Test 19 and what trying it on the box and an FM-1 showed was needed.

- **The install works end to end.** Test 19 lost the FM-1 at the hand-over to its update loader. Three faults in the
  box's USB host, found on the board: a failed read was never started again; the port never saw the FM-1 leave (the
  remove has to be queued before the port is emptied, or the attach is seen first and nothing is met); and after
  the last restart the port can say something is there that was never met. `main.c` now starts reads again, starts
  the port again after five failures, and starts it again every 3 seconds while something plugged in is unmet.
  FX-1 1.1 and AISLING 1.5 were put on an FM-1 from the card this way (Stephen pressing INSTALL).
- **The shelf shows the files' own names** (`FF_USE_LFN 1`, `ffunicode.c`), and the page says the name, not the
  product number: "AISLINGFX-1.1 IS ON THE FM-1".
- **MENU** at the right of the status line opens calibration (Stephen: "add an easier way to get to the
  calibration page").
- **Quicker taps** (Stephen: "would prefer if the UI was quicker in the response to taps"). Timed on the board, a
  finger is down 200 to 500 ms and everything waited for the lift and 60 ms more. MENU and calibration's buttons
  now act on the press; the wait after a lift is 45 ms.
- **The landscape answers a finger** (Stephen: "shouldn't the touch on the land page react? swirls and blooms"):
  a touch drops paint where it lands, a drag pulls the paint after it and leaves a swirl (`land_touch`). A tap
  still changes page and a drag still works the pad. On the board Stephen found the touch works but "swirls are
  dotty, not swirls": the trail is now laid along the whole stretch between readings, with a few wide eddies on
  alternate sides of the path. Of that second try, on the board: "the swirls look better now".

Not done: test 19's host checks (`hosttests/test19`) are not carried over to this folder and were not run on it.
What follows is test 19's page, as it was.

---

# viz-test19: a firmware put on the FM-1 from the card

**Not on the board. Nothing here has been tried on the box or on an FM-1.** Built and checked on the Mac on
18 Nov 2026. Test 18 and one new page: the card's folder `FM1` as a shelf of firmware packages, and a touch that
puts one of them on the FM-1 over the box's USB host socket. Nothing else.

Why: Stephen, of switching the FM-1 between firmwares without a computer: "let's build the first one".

- **What it installs.** Felucca `.fwsc` packages, the kind AISLING's own updater takes. The box does the computer's
  part of that update: `swap.c` is a port to C of fm1-aisling's `tools/fm1_install.py` (Leo Kuroshita's,
  GPL-3.0-only; see `NOTICES.md`). The messages, the two steps and the waits are that file's.
- **The shelf.** Put packages in a folder `FM1` at the top of the card. The box has no long names, so a file shows
  under the short name macOS gives it: `aisling-1.4.1.fwsc` is `AISLIN~1.FWS` or the like (the number is macOS's choice). Beside the name is what the package
  says it is (`FM-1_353010401`) and its size. Up to 20 packages, five to a page. Hidden files and macOS's `._`
  files are left out. A file that doesn't say what it is for is listed as NOT A PACKAGE and can't be installed.
- **Getting there.** The card's page has a new bar, FIRMWARE FOR THE FM-1, between its lines and its news. Or
  key `z` on the log. BACK returns to the card's page.
- **Installing.** Touch a row, then INSTALL, then SURE?. Picking another row or MORE takes SURE? back. Before
  anything is sent the whole file is read once: it must still be what it was when listed, and it must carry
  Felucca's update loader. A package without one (a stock firmware, say) is refused, because the box would have no
  way to put anything else on afterwards.
- **While it runs** the page shows the step (ASKING WHO IT IS, THE FM-1 CHECKS IT, TO ITS LOADER, WRITING with a
  bar, RESTARTING) and no button or key does anything. The box sends the FM-1 nothing else for as long.
- **The two steps.** In the first, the firmware running on the FM-1 reads parts of the package and starts its
  update loader; nothing has been written. In the second the loader reads the whole package and writes it. Then the
  FM-1 restarts and the page says who answers: `FM-1_353010401 IS ON THE FM-1`.
- **The way back** is the same thing with the other package. An install needs the firmware on the FM-1 to be one
  with Felucca's updater in it (AISLING, AISLING FX and the like).
- **When it goes wrong.** The page says which of nine things happened and whether anything was written. A package
  for another instrument, no FM-1, no answer: nothing was written. Stopped part way through the write: the FM-1
  waits in its loader, through power cuts, and INSTALL again finishes it (the box sees it is already in its loader
  and does only the write). A cable pulled and put back within 30 seconds is carried on from.
- **If the box can't finish it**, the FM-1 is still recoverable from a computer: OCT- held at power-on is its USB
  rescue, and fm1-aisling's installer works from there.
- `cal.c` and `card.c` differ from test 18's by their titles only; `main.c` by the lines in
  `../hosttests/test19/changes.txt` (4 out, 165 in, the three files together). `swap.c`, `swap.h`, `fwpage.c` and
  `fwpage.h` are new. Every other file is test 18's byte for byte (the USB name says 19, `CMakeLists.txt` has the
  two new sources).

What the rest does is in [`../viz-test18/README.md`](../viz-test18/README.md).

## Host checks

    zsh ../hosttests/test19/run.sh [--quick] [INSTALLED.fwsc NEW.fwsc]

The two packages are real ones, for the end-to-end check. They aren't the box's and aren't in this repository;
without them (or without a clone of fm1-aisling at `~/Projects/fm1-aisling`, or `$FM`) those parts are skipped and
the script says so.

- Test 19 is test 18 and the page: the files compared; the three's changed lines are `changes.txt`'s and no
  others.
- `swap_check.c` (61 and 45 checks): `swap.c`'s pieces, then whole installs against `simdev.h`, a model of the
  FM-1's side: the cable pulled in each step, messages lost, the USB busy, the card misreading, another
  instrument's package, a power cut in the write and the install taken up again from the loader.
- `vec.py` (155 lines): the same messages built, and the same packages read, by `fm1_install.py` itself. `swap.c`
  must print the same, byte for byte. Two real packages are among them when given.
- `e2e_check.c` (38 checks): `swap.c` against AISLING's own updater and update loader, fm1-aisling's `ota.c` and
  `ldr_core.c` compiled for the Mac over a 1 MiB flash in memory. With AISLING 1.4.1 and AISLING FX 1.1, each put
  over the other, the flash afterwards is the package's image; also with the power cut in the write and the
  install begun again.
- `fw_check.c` (46 and 3 checks): `fwpage.c` over the box's own FatFs, `card.c` and `sd.c` and test 12's model of a
  card. The card is an image macOS itself fills, so the names are the ones a Mac makes, `._` file and all. The
  list and its pages, SURE? and what takes it back, a finger slid off, an install to its end with every word the
  page shows, the way back with the USB refusing two sends in three, a stock package refused, another instrument,
  no FM-1, the card pulled at 30% and the install finished after, every line within 464, the door's place.
- 24 deliberate breaks of `swap.c`, `swap.h` and `fwpage.c`, each caught; and a shifted package shown to be caught
  end to end as well.
- `earlier.sh`: test 18's `run.sh` against this folder, and inside it the earlier tests'. Three of test 18's
  scripts are changed to look here, and one line of test 15's (it didn't expect two more sources in `CMakeLists.txt`); what is changed is at `earlier.sh`'s head, and each change is checked to have
  been made.

The whole run, `zsh hosttests/test19/run.sh` with the two real packages, on the MacBook (macOS 27.2.1) on
18 Nov 2026: 26 minutes, ending `earlier: all passed` and `test 19: all passed`, with tests 13, 15, 16, 17 and 18
each saying `all passed` inside it. On the iMac (macOS 13) test 19's own part passes the same, breaks and all,
but the whole run does not finish there, for two reasons that are older than test 19 and are the iMac's:

- tests 9 to 11 take AISLING's source from `~/Projects/aisling-drafts/fw-1.4`, which only the MacBook has.
  `earlier.sh` now gives them the fm1-aisling clone where that folder is missing;
- test 11 makes `ufont.h` again with Pillow and compares. The iMac's Pillow (11.1, FreeType 2.13.2) draws 18 lines
  of it differently, for test 11's own folder as much as this one. The MacBook has no Pillow, so there the
  comparison has always been skipped. `ufont.h` itself is the same file from test 11 to test 19.

The two Macs give the same files different short names (`stock.fwsc` is `STOC~1.FWS` from one and `STOC~5.FWS` from
the other), so the page's check takes each name from the list and doesn't expect a number.

## What the checks can't say

- **`main.c`'s part isn't checked on the Mac**: the USB's two cores handing messages across, the page's touch, the
  door on the card's page (it is redrawn four times a second while that page is up), the keys held off during an
  install. Test 18's cuts of `main.c` still pass against it; nothing drives the new lines.
- **The FM-1 in the checks is code, not the instrument.** The end-to-end check runs AISLING's real updater and
  loader, but on a Mac, with the flash and the restart pretended. How the chip's first-stage boot hands over to
  the loader is modelled from fm1-aisling's sources.
- **The loader has no check of the whole image.** A wrong byte in an answer during the write would be written and
  the install called done. Each message is as `fm1_install.py` sends it, which is all the protocol has; the box
  adds nothing. A firmware that then doesn't start is what OCT- at power-on is for.
- **Reading the package before an install stops the picture and the touch** for as long as the card takes, since
  it is done in one go.
- **A cable out during the write** is waited on for 30 seconds and then, if the FM-1 is back but silent, for the
  write's own three minutes before the page gives up. `fm1_install.py` waits the same three minutes.

## Build

As test 18's. 144 MHz build of 18 Nov 2026: `build144/viz_test19.uf2` (394,240 bytes; SHA-256
`1f98557700f360a6c7982ac2e3b1229de4746592d8954dc1d1514540104de6e6`). No warnings.

## Not tried

Everything on hardware:

- the page on the screen, the door, a touch on either;
- one install on a real FM-1, either direction;
- whether the box answers fast enough (the FM-1 asks again after 5 seconds; the box's answers were only timed in
  a model);
- the FM-1 leaving the USB and coming back as its loader, twice an install, through Pico-PIO-USB;
- a card a Mac really wrote, in the box's slot, with a package of 600 KB;
- any firmware but AISLING and AISLING FX, including whether the FM-1's stock firmware exists as a `.fwsc` at all.

Not built: keeping each firmware's own saved sounds across a swap. The update protocol only writes; it has no way
to read the FM-1's flash back.

First try, when there is one: with a computer and fm1-aisling's installer at hand, and OCT- known to work.
