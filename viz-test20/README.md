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
  still changes page and a drag still works the pad. Loaded, not yet reported on.

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
