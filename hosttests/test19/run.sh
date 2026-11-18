#!/bin/zsh
# Test 19's host checks (test 18 and a firmware put on the FM-1 from the card), on the Mac.
#   zsh run.sh [--quick] [INSTALLED.fwsc NEW.fwsc]
#       --quick: test 12's checks without test 11's whole run.sh (some six minutes less)
#       the two packages: real ones, for the end-to-end check. Without them: $FM1_PKG_A and $FM1_PKG_B, else
#       ~/Downloads/aislingfx-1.1.fwsc and ~/Downloads/aisling-1.4.1.fwsc. None of that there: that check is skipped,
#       and said to be. (The packages aren't the box's and aren't in this repository.)
#
#   test 19 is test 18 and the firmware page, and nothing else: every file but three is test 18's byte for byte
#     (titles and the USB name say 19, CMakeLists.txt has the two new sources), swap.c, swap.h, fwpage.c and fwpage.h
#     are the new files, and the three (cal.c, card.c, main.c) differ from test 18's by the lines in changes.txt and
#     no others (main.c: its code lines)
#   swap_check.c   swap.c, the install's host side: its pieces, then whole installs against simdev.h's FM-1 (the
#                  cable pulled, messages lost, the card misreading, another instrument, a power cut)
#   vec.py         the same messages and package readings from fm1-aisling's tools/fm1_install.py, the file swap.c is
#                  a port of: swap_check's "vectors" must print the same lines
#   e2e_check.c    swap.c against AISLING's own updater and update loader (fm1-aisling's ota.c and ldr_core.c,
#                  compiled for the Mac over a 1 MiB flash in memory) with two real packages, each over the other
#   fw_check.c     fwpage.c, the page: over the box's FatFs, card.c and sd.c and test 12's model of a card, the card
#                  an image this script has macOS itself fill (so its names are the ones a Mac makes)
#   deliberate breaks of swap.c, swap.h and fwpage.c, each to be caught
#   earlier.sh     test 18's run.sh against test 19's folder, and inside it the earlier ones
# Not checked here: main.c's part (the USB's two cores, the touch on the page, the door on the card's page).
set -e
HERE=${0:A:h}
cd $HERE
V=../../viz-test19
V18=../../viz-test18
O=$(mktemp -d)
F=(-std=gnu11 -g -O1 -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined -fno-sanitize-recover=undefined)
FM=${FM:-$HOME/Projects/fm1-aisling}
quick=
pk=()
for a in "$@"; do
    if [[ $a == --quick ]]; then quick=--quick; else pk+=($a); fi
done
PKA=${pk[1]:-${FM1_PKG_A:-$HOME/Downloads/aislingfx-1.1.fwsc}}
PKB=${pk[2]:-${FM1_PKG_B:-$HOME/Downloads/aisling-1.4.1.fwsc}}
CHANGED=(cal.c card.c main.c)

print -r -- "-- test 19 is test 18 and the firmware page"
for f in $V18/*(.); do
    n=${f:t}
    case $n in
        README.md|cal.c|card.c|main.c) ;;
        usb_descriptors.c) sed 's/"AISLING viz test 18"/"AISLING viz test 19"/' $f | cmp -s - $V/$n || { print -r -- "FAIL: $n is not test 18's but for the name"; exit 1 } ;;
        CMakeLists.txt) sed 's/viz_test18/viz_test19/g' $f | cmp -s - <(grep -v -x -e '    swap.c' -e '    fwpage.c' $V/$n) &&
            [[ $(grep -c -x -e '    swap.c' -e '    fwpage.c' $V/$n) == 2 ]] || { print -r -- "FAIL: CMakeLists.txt is not test 18's but for the name and the two sources"; exit 1 } ;;
        *) cmp -s $f $V/$n || { print -r -- "FAIL: $n is not test 18's"; exit 1 } ;;
    esac
done
[[ -f $V/swap.c && -f $V/swap.h && -f $V/fwpage.c && -f $V/fwpage.h && $(ls $V/*(.) | wc -l) -eq $(($(ls $V18/*(.) | wc -l) + 4)) ]] ||
    { print -r -- "FAIL: test 19 is not test 18's files and the four"; exit 1 }
changes() {
    local n
    for n in $CHANGED; do
        print -r -- "== $n"
        case $n in
            main.c) diff <(grep -v '^ *//' $V18/$n | sed 's/---- AISLING viz test 18 ----/---- AISLING viz test 19 ----/') <(grep -v '^ *//' $V/$n) | grep '^[<>]' || true ;;
            *) diff <(sed 's/AISLING VIZ TEST 18/AISLING VIZ TEST 19/' $V18/$n) $V/$n | grep '^[<>]' || true ;;
        esac
    done
}
changes > $O/changes
cmp -s $O/changes changes.txt || { diff $O/changes changes.txt | head -20; print -r -- "FAIL: the three files' changes from test 18's are not changes.txt's (as they are now: $O/changes)"; exit 1 }
print -r -- "the same: test 18's files and the four; the three lose $(grep -c '^<' $O/changes) lines of test 18's and have $(grep -c '^>' $O/changes) new"

# ---- the programs. $1 is always the folder the box's files are taken from (a copy of $V, or a broken one)
swp() { clang $F -I$1 -I. swap_check.c $1/swap.c -o $2 }
e2e() {
    local r
    for r in old new loader; do
        case $r in
            old) D=(-DROLE=old "-DFELUCCA_ID=\"$IDA\"") ;;
            new) D=(-DROLE=new "-DFELUCCA_ID=\"$IDB\"") ;;
            loader) D=(-DROLE=loader -DROLE_LOADER) ;;
        esac
        clang ${F:#-g} -Wno-unused-function $D -I. -I$FM/firmware/src -I$FM/firmware/loader -c e2e_dev.c -o $2.$r.o || return 1
    done
    clang ${F:#-g} -I$1 -I. e2e_check.c $1/swap.c $2.old.o $2.new.o $2.loader.o -o $2
}
fwp() {
    local f
    mkdir -p $2.d
    for f in lcd.c diskio.c ff.c tone.c sd.c settings.c card.c msc.c swap.c; do
        clang $F -I../test12/stub -I$1 -c $1/$f -o $2.d/${f:r}.o || return 1
    done
    clang $F -I../test12/stub -I$1 -I../test12 -c ../test12/model.c -o $2.d/model.o &&
        clang $F -Dlcd_fill_rect=rec_fill -Dlcd_text=rec_text -I../test12/stub -I$1 -c $1/fwpage.c -o $2.d/fwpage.o &&
        clang $F -Wno-unused-function -I../test12/stub -I$1 -I../test12 -I. fw_check.c $2.d/*.o -o $2
}
ran() { grep -q "^$2: [1-9][0-9]* checks, 0 failed$" $1 }

mkdir -p $O/src
cp $V/*.c $V/*.h $O/src/

print -r -- "-- the install's host side"
swp $O/src $O/swap
$O/swap > $O/swap.out || { tail -20 $O/swap.out; print -r -- "FAIL: swap.c"; exit 1 }
grep "^swap " $O/swap.out
ran $O/swap.out "swap units" && ran $O/swap.out "swap installs" || { print -r -- "FAIL: swap.c's checks didn't run"; exit 1 }

real=()
[[ -f $PKA && -f $PKB ]] && real=($PKA $PKB)
have_e2e=
if [[ -f $FM/tools/fm1_install.py ]]; then
    print -r -- "-- the same as fm1-aisling's installer"
    $O/swap vectors $real > $O/vec.c.out
    python3 -I vec.py $FM $real > $O/vec.py.out
    cmp -s $O/vec.c.out $O/vec.py.out && [[ $(wc -l < $O/vec.c.out) -ge 150 ]] ||
        { diff $O/vec.c.out $O/vec.py.out | head -10; print -r -- "FAIL: swap.c and fm1_install.py differ"; exit 1 }
    print -r -- "vectors: $(wc -l < $O/vec.c.out | tr -d ' ') lines the same, $#real real packages among them"
    if [[ $#real == 2 && -f $FM/firmware/src/ota.c && -f $FM/firmware/loader/ldr_core.c ]]; then
        IDA=$(sed -n 's/^pkg [0-9]* \[\(.*\)\] 1 1 .*/\1/p' $O/vec.c.out | sed -n 1p)
        IDB=$(sed -n 's/^pkg [0-9]* \[\(.*\)\] 1 1 .*/\1/p' $O/vec.c.out | sed -n 2p)
        [[ -n $IDA && -n $IDB && $IDA != $IDB ]] || { print -r -- "FAIL: the two packages must be two installable firmwares ($IDA, $IDB)"; exit 1 }
        print -r -- "-- AISLING's own updater and loader: $IDB over $IDA, and back"
        e2e $O/src $O/e2e
        $O/e2e $PKA $PKB > $O/e2e.out 2>&1 || { tail -20 $O/e2e.out; print -r -- "FAIL: the end to end"; exit 1 }
        grep "^e2e:" $O/e2e.out
        ran $O/e2e.out e2e || { print -r -- "FAIL: the end-to-end checks didn't run"; exit 1 }
        have_e2e=1
    else
        print -r -- "SKIPPED: the end-to-end check (two real packages wanted: $PKA and $PKB)"
    fi
else
    print -r -- "SKIPPED: the comparison with fm1_install.py and the end-to-end check (no $FM)"
fi

print -r -- "-- the page, on a card a Mac filled"
fwp $O/src $O/fw
mkdir $O/files $O/mnt
$O/fw make $O/files
xattr -w com.apple.quarantine "0081;00000000;Safari;" $O/files/aislingfx-1.1.fwsc   # as a download has: macOS writes ._ beside it
hdiutil create -size 16m -fs "MS-DOS FAT16" -layout MBRSPUD -volname AISLING $O/card > /dev/null
hdiutil create -size 16m -fs "MS-DOS FAT16" -layout MBRSPUD -volname AISLING $O/bare > /dev/null
hdiutil attach $O/card.dmg -mountpoint $O/mnt -nobrowse > /dev/null
mkdir $O/mnt/FM1 $O/mnt/FM1/old.fwsc
for f in aisling-1.4.1.fwsc aislingfx-1.1.fwsc stock.fwsc other.fwsc junk.fwsc tiny.fwsc notes.txt; do cp $O/files/$f $O/mnt/FM1/; done
[[ -f $O/mnt/FM1/._aislingfx-1.1.fwsc ]] || print -r -- "(macOS wrote no ._ file this time: that much of the listing isn't checked)"
hdiutil detach $O/mnt > /dev/null
$O/fw bare $O/bare.dmg > $O/fwb.out 2>&1 || { tail $O/fwb.out; print -r -- "FAIL: the page, no folder"; exit 1 }
$O/fw run $O/card.dmg > $O/fw.out 2>&1 || { tail -20 $O/fw.out; print -r -- "FAIL: the page"; exit 1 }
grep -h "^fwpage" $O/fwb.out $O/fw.out
ran $O/fwb.out "fwpage bare" && ran $O/fw.out fwpage || { print -r -- "FAIL: the page's checks didn't run"; exit 1 }

print -r -- "-- deliberate breaks, each to be caught"
# each: its name, its file, sed's change, and what must catch it (S swap_check, W the page's check)
BN=() BF=() BS=() BC=()
brk() { BN+=($1) BF+=($2) BC+=($3) BS+=($4) }
brk "swap.c: the logical image's place in the file, without the head's markers" swap.c SW 's/ : logical + BLOCKS;$/ : logical;/'
brk "swap.c: the answer to a step's finish misspelt" swap.c S '/^static void served/,/^}/s/static const uint8_t yes\[8\] = "success";/static const uint8_t yes[8] = "succesS";/'
brk "swap.c: another instrument's package put on" swap.c SW 's/^        if (!swap_same_model(id.text, s->product)) {$/        if (0) {/'
brk "swap.c: an FM-1 already in its loader sent UPGRADE as if it weren't" swap.c S 's/^        } else if (id.loader) {   /        } else if (0) {   /'
brk "swap.c: a request past the package's end answered" swap.c S 's/^    if (n > SWAP_MAXDATA || (uint64_t)addr + n > s->image_len) {$/    if (n > SWAP_MAXDATA) {/'
brk "swap.c: the card's first misread ends it" swap.c S 's/^        if (++s->misreads >= 3) fail(s, SWAP_E_CARD);$/        if (++s->misreads >= 1) fail(s, SWAP_E_CARD);/'
brk "swap.c: the cable put back, and nobody asked who it is" swap.c S 's/^    if (!was \&\& (s->state == SWAP_CHECK || s->state == SWAP_WRITE))$/    if (0)/'
brk "swap.c: a finish asked for again, not answered again" swap.c S '/^        else again_finish(s, sx, n, SWAP_FINISH_CHECK, now);$/d'
brk "swap.c: whoever answers afterwards is taken for the new firmware" swap.c S 's/^        if (strcmp(id.text, s->product)) fail(s, SWAP_E_MISMATCH);$/        if (0) fail(s, SWAP_E_MISMATCH);/'
brk "swap.c: the loader's mark not found after a false start" swap.c S "s/ ? k + 1 : p\[i\] == 'F';\$/ ? k + 1 : 0;/"
brk "swap.c: a message's last packet always marked as one byte" swap.c S 's/^        out\[o\] = (uint8_t)(left > 3 ? 0x04 : 0x04 + left);$/        out[o] = (uint8_t)(left > 3 ? 0x04 : 0x05);/'
brk "swap.h: the write given up after 1.8 seconds of quiet" swap.h S 's/^#define SWAP_IDLE_WRITE_MS 180000u /#define SWAP_IDLE_WRITE_MS 1800u /'
brk "fwpage.c: hidden files listed (macOS's ._ ones)" fwpage.c W "s/^        if ((fno.fattrib \& (AM_DIR | AM_HID | AM_SYS)) || fno.fname\[0\] == '.' || !is_package(fno.fname)) continue;\$/        if ((fno.fattrib \& AM_DIR) || !is_package(fno.fname)) continue;/"
brk "fwpage.c: every file listed" fwpage.c W "s/ || fno.fname\[0\] == '.' || !is_package(fno.fname)) continue;\$/ || fno.fname[0] == '.') continue;/"
brk "fwpage.c: a package with no loader of Felucca's put on" fwpage.c W 's/^    if (!has) return "IT HAS NO FELUCCA/    if (0) return "IT HAS NO FELUCCA/'
brk "fwpage.c: the mark looked for in each piece by itself" fwpage.c W 's/^        has = swap_mark(&mark, chunk, got);$/        mark = 0; has = swap_mark(\&mark, chunk, got);/'
brk "fwpage.c: INSTALL without SURE?" fwpage.c W 's/^        if (stage == ST_PICK) {$/        if (0) {/'
brk "fwpage.c: SURE? kept when another row is picked" fwpage.c W '/^        sel = top + (t - T_ROW0);$/{n;d;}'
brk "fwpage.c: INSTALL alive on a file that isn't a package" fwpage.c W 's/^    if (b == BTN_GO) return sel >= 0 \&\& files\[sel\].product\[0\];$/    if (b == BTN_GO) return sel >= 0;/'
brk "fwpage.c: the image's head read from the file as it lies" fwpage.c W 's/^    if (at < SWAP_HEAD_LOGICAL) {$/    if (0) {/'
brk "fwpage.c: its lines as long as the screen" fwpage.c W 's/^    int room = UI_W \/ (6 \* scale), k = 0;$/    int room = LCD_W \/ (6 * scale), k = 0;/'
brk "fwpage.c: the door found under the brick" fwpage.c W 's/^bool fw_door_at(int x, int y) { return x >= 0 \&\& !ui_off(x) \&\& /bool fw_door_at(int x, int y) { return x >= 0 \&\& x < LCD_W \&\& /'
brk "fwpage.c: the door two rows into the card's line" fwpage.c W 's/^#define DOOR_Y  164$/#define DOOR_Y  160/'
brk "fwpage.c: drawn while another page is up" fwpage.c W '/^void fw_tick/,/^}/s/^    if (!shown) return;$//'
caught=0
missed=0
for i in {1..$#BN}; do
    B=$O/b$i
    mkdir -p $B
    cp $V/*.c $V/*.h $B/
    sed -i '' -e $BS[i] $B/$BF[i]
    cmp -s $B/$BF[i] $V/$BF[i] && { print -r -- "FAIL: break $i didn't apply: $BN[i]"; exit 1 }
    by=
    if [[ $BC[i] == *S* ]]; then
        swp $B $B/swap 2> $B/swap.err || { head -5 $B/swap.err; print -r -- "FAIL: break $i doesn't build: $BN[i]"; exit 1 }
        if $B/swap > $B/swap.out 2>&1 && ran $B/swap.out "swap units" && ran $B/swap.out "swap installs"; then
            print -r -- "FAIL: swap_check doesn't catch break $i: $BN[i]"; missed=1; continue
        fi
        by="swap_check: $(grep -c '^FAIL' $B/swap.out)"
    fi
    if [[ $BC[i] == *W* ]]; then
        fwp $B $B/fw 2> $B/fw.err || { head -5 $B/fw.err; print -r -- "FAIL: break $i doesn't build: $BN[i]"; exit 1 }
        if $B/fw run $O/card.dmg > $B/fw.out 2>&1 && ran $B/fw.out fwpage; then
            print -r -- "FAIL: the page's check doesn't catch break $i: $BN[i]"; missed=1; continue
        fi
        by="${by:+$by; }the page: $(grep -c '^FAIL' $B/fw.out), first: $(grep -m1 '^FAIL' $B/fw.out | cut -c1-90)"
    fi
    caught=$((caught + 1))
    print -r -- "caught: $BN[i] ($by)"
done
print -r -- "breaks caught: $caught of $#BN"
[[ $missed == 0 && $caught == $#BN ]] || exit 1
if [[ -n $have_e2e ]]; then   # and the end-to-end check, shown to notice: the package's bytes shifted by the head's markers
    B=$O/be
    mkdir -p $B
    cp $V/*.c $V/*.h $B/
    sed -i '' -e 's/ : logical + BLOCKS;$/ : logical;/' $B/swap.c
    e2e $B $B/e2e
    if $B/e2e $PKA $PKB > $B/e2e.out 2>&1 && ran $B/e2e.out e2e; then print -r -- "FAIL: the end-to-end check doesn't notice a shifted package"; exit 1; fi
    print -r -- "caught end to end: a shifted package ($(grep -c '^FAIL' $B/e2e.out) failures)"
fi

zsh earlier.sh $quick || { print -r -- "FAIL: the earlier tests' checks"; exit 1 }
print -r -- "test 19: all passed"
