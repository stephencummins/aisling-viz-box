#!/bin/zsh
# Test 7's host checks, on the Mac with AddressSanitizer + UBSan:
#   land.c, tone.c, hot.h  test 6's, unchanged (compared byte for byte, so test 5's comparison with test 4 stands)
#   pumpsim.c        test 6's, on test 7's main.c (its pump code cut out of main.c, against a pretend DMA)
#   reader_check.c   test 5's: reader.c against AISLING's own announce.c (the piece in sync)
#   fm1scr_check.c   test 6's (everything AISLING 1.2 says, through announce.c and reader.c, the model and every pixel)
#                    plus test 7's: each knob said back counted, the value being sent and its bar, the dimmed labels,
#                    the box's own warning
#   gen_knobtab.py   knobtab.h made again from the corpus and fm1-aisling's ui.c, amb.c, amb.h: the same
#   knobctl_check.c  knobctl.c on its own (units), then against AISLING's real ui.c through fm1-aisling's own host
#                    build (amb_host, built into a temp dir, its sources untouched): every CC to every knob the
#                    buttons reach (the sweep), and drags on 8 pages (plan, then loop)
#   test 4's checks  (lcd, calibration, warmth, the landscape harness) against test 7's files
# then deliberate breaks of fm1scr.c and knobctl.c, each of which must be caught.
# run.sh DIR also writes pictures of the FM-1's screen into DIR (PNG). run.sh --no-sweep leaves out the sweep (22 s).
set -e
setopt pipefail   # a check that fails stops it, its FAIL lines shown
cd "${0:A:h}"
V6=../../viz-test6
V=../../viz-test7
FM=$HOME/Projects/fm1-aisling
A=$FM/firmware/src/announce.c
C=../test6/corpus.txt
O=$(mktemp -d)
F=(-std=gnu11 -g -O1 -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined -fno-sanitize-recover=undefined)
sweep=1
shots=
for a in "$@"; do
    if [[ $a == --no-sweep ]]; then sweep=; else shots=$a; fi
done

print -r -- "-- test 6's paint, unchanged"
for f in land.c land.h tone.c tone.h hot.h; do
    cmp $V6/$f $V/$f
    print -r -- "$f: the same as test 6's"
done

# main.c's pump: from its strip buffers to just before spi_i, word for word.
sed -n '/^static uint16_t strip\[2\]/,/^static int spi_i/p' $V/main.c | sed '$d' > $O/pump_block.inc
print -r -- "-- pump, $(wc -l < $O/pump_block.inc | tr -d ' ') lines of main.c"
clang $F -I$V -I$O -Wno-unused-variable -Wno-unused-function ../test6/pumpsim.c $V/land.c $V/tone.c -lm -o $O/pumpsim
$O/pumpsim ../test2/midilog.txt | grep -E "FAIL|passed|pushes|seed"

if [[ ! -f $A ]]; then
    print -r -- "FAIL: no $A (AISLING's announce.c, which the next checks build against)"
    exit 1
fi
print -r -- "-- the screen reader (test 5's check), against AISLING's announce.c"
clang $F -Wno-unused-function -I$V -DANNOUNCE_C="\"$A\"" -DANN_NAME='"AISLING 1.2"' ../test5/reader_check.c \
    $V/reader.c -o $O/reader
$O/reader | grep -E "FAIL|passed|checks"

print -r -- "-- the FM-1's screen"
fm1scr() {   # $1: the fm1scr.c to build against; $2: the program
    clang $F -I../test4/stub -I$V -DFM1SCR_C="\"$1\"" -DANNOUNCE_C="\"$A\"" -DANN_NAME='"AISLING 1.2"' \
        fm1scr_check.c $V/lcd.c $V/tone.c $V/reader.c ../test4/stub.c -o $2
}
fm1scr ${V:A}/fm1scr.c $O/fm1scr
if [[ -n $shots ]]; then
    mkdir -p $shots
    $O/fm1scr $C --shots $shots
    for p in $shots/*.ppm; do sips -s format png $p --out ${p:r}.png > /dev/null && rm $p; done
    print -r -- "pictures: $shots"
else
    $O/fm1scr $C
fi

print -r -- "-- the knobs' table, made again"
python3 -I gen_knobtab.py $C $V/fm1scr.c $FM/firmware/src/app/ui.c $FM/firmware/src/dsp/amb.c \
    $FM/firmware/src/dsp/amb.h $O/knobtab.h
cmp $O/knobtab.h $V/knobtab.h
print -r -- "knobtab.h: the same"

print -r -- "-- the knobs from the box"
kc() {   # $1: the directory with fm1scr.c and knobctl.c; $2: the program
    clang $F -I$V knobctl_check.c $1/fm1scr.c $1/knobctl.c -o $2
}
kc $V $O/kc
$O/kc units | grep -E "FAIL|units:"
amb=
if [[ -f $FM/host/amb_host.c && -f $FM/build/gen/ann_words.h ]]; then
    # fm1-aisling's own host build of AISLING 1.2, into the temp dir (nothing written to fm1-aisling)
    cc -D_GNU_SOURCE -O2 -ffp-contract=off -std=c99 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter \
        -DEN_HOST -I$FM/firmware/src -I$FM/firmware/src/dsp -I$FM/build/gen -o $O/amb_host $FM/host/amb_host.c \
        $FM/firmware/src/dsp/amb.c -lm 2> $O/amb_host.err || { cat $O/amb_host.err; exit 1 }
    amb=$O/amb_host
    mkdir -p $O/run
    if [[ -n $sweep ]]; then
        $O/kc sweep-script $O/sweep.amb
        $amb $O/sweep.amb $O/run > $O/sweep.txt 2> $O/sweep.err
        $O/kc sweep-check $O/sweep.txt | grep -E "FAIL|sweep|said|not"
    fi
    $O/kc plan $C $O/drag.amb | grep -E "FAIL|plan|drags"
    $amb $O/drag.amb $O/run > $O/drag.txt 2> $O/drag.err
    $O/kc loop $O/drag.txt | grep -E "FAIL|loop|drags"
else
    print -r -- "(no $FM/host/amb_host.c or build/gen/ann_words.h: the sweep and the loop through AISLING's ui.c left out)"
    $O/kc plan $C $O/drag.amb | grep -E "FAIL|plan|drags"
fi

print -r -- "-- test 4's checks against test 7"
T4=../test4
clang $F -I$T4/stub -I$V -c $V/lcd.c -o $O/lcd.o
clang $F -I$T4/stub -I$V -Dlcd_text=test_lcd_text -c $V/cal.c -o $O/cal.o
clang $F -I$T4/stub -I$V -c $V/tone.c -o $O/tone.o
clang $F -I$T4/stub -I$V -c $T4/stub.c -o $O/stub.o
sed 's/AISLING VIZ TEST 4/AISLING VIZ TEST 7/; s/title says test 4/title says test 7/' $T4/test.c > $O/test.c
clang $F -I$T4/stub -I$V -c $O/test.c -o $O/test.o
clang $F $O/lcd.o $O/cal.o $O/tone.o $O/stub.o $O/test.o -o $O/t4
$O/t4 | grep -E "FAIL|passed"
clang $F -I$V $T4/land4.c $V/land.c $V/tone.c -lm -o $O/land4
$O/land4 | grep -E "FAIL|passed"
clang $F -I$V ../test3/harness3.c $V/land.c $V/tone.c -lm -o $O/harness
$O/harness | tail -3

# Each break must make something fail: fm1scr_check --quick (no random texts), knobctl_check units, the drags planned
# and, with amb_host, looped through AISLING's ui.c.
print -r -- "-- deliberate breaks, each to be caught"
MF=(
    # test 6's nine, on test 7's fm1scr.c
    fm1scr.c fm1scr.c fm1scr.c fm1scr.c fm1scr.c fm1scr.c fm1scr.c fm1scr.c fm1scr.c
    # test 7's
    fm1scr.c fm1scr.c fm1scr.c fm1scr.c fm1scr.c fm1scr.c
    knobctl.c knobctl.c knobctl.c knobctl.c knobctl.c knobctl.c knobctl.c knobctl.c knobctl.c knobctl.c knobctl.c
    knobctl.c knobctl.c
)
MN=(
    "a turned knob's label: the shortest that fits (Echo time 1/4. as Echo)"
    "a turned knob not drawn again (its brightness)"
    "the rule under the page left undrawn"
    "a chord change: any capital after Chord (the Chords page's Chord Minor)"
    "the whole header's missing not saved yet not taken as saved"
    "the waiting screen one row into the status row"
    "a knob only if its label is capital then lower case (MIDI out On, A time)"
    "a cut text taken whole (its last item half said)"
    "a new key not asking for the scale"
    "the value being sent drawn white, as any other"
    "the bar's length out (n, not n - 1)"
    "the bar on the rule under the knobs"
    "a knob the box can't turn: its label not dimmed"
    "the Key page's MODE not counted as Mode said back"
    "the Chords page's turns not asking for the whole page"
    "absolute: the drag starts where the finger is, not from the knob's value (a jump)"
    "the CC for a value one more than the middle of its run"
    "no 10 px before a drag starts"
    "a tap on a knob flipping the page"
    "no warning when the FM-1 doesn't take it"
    "Mode draggable on Custom (a value not in its list taken as its first)"
    "Setup's knobs sent (AISLING takes no CC for them)"
    "no WHERE asked for when a send isn't said back"
    "the MODE say's dropped full stop not looked past (Mixolyd. never found)"
    "the nearest step, not the last whole step passed (jitter turns it back)"
    "Motion's knobs: a page a step off what was sent taken as not taken"
    "a page change under the finger not cancelling the drag"
    "a send not waited for: the next drag starts from the FM-1's old value"
)
ME=(
    's/if (n > bl \&\& /if ((best < 0 || n < bl) \&\& /'
    '/^static void turned(/,/^}/s/^    m->dirty |= FM1_D_KNOB << i;$//'
    's/fill(0, 140, LCD_W, 2, RULE);//'
    "s/return \\*p == ' ' || !\\*p || \\*p == ',';/return true;/"
    's/if (whole \&\& said_play \&\& !said_unsaved)/if (0)/'
    's/box_text(0, 0, LCD_W, FM1SCR_BOTTOM, 124/box_text(0, 0, LCD_W, FM1SCR_BOTTOM + 1, 124/'
    's|^    if (m->view >= 0) {   // "Ornament|    if (m->view >= 0 \&\& t[1] >= '"'a'"' \&\& t[1] <= '"'z'"') {   // "Ornament|'
    's/bool cut = n >= CUT_AT;/bool cut = false;/'
    's|m->want_where = true;   // a new key: a new scale|;|'
    's/uint16_t fg = pv || m->turned == i ? C_ORANGE : FG;/uint16_t fg = m->turned == i ? C_ORANGE : FG;/'
    's|(KBAR_W \* m->pv_idx\[i\] + (n - 1) / 2) / (n - 1)|KBAR_W * m->pv_idx[i] / n|'
    's/^#define KBAR_Y 133 /#define KBAR_Y 139 /'
    's/on \&\& (m->locked >> i \& 1u) ? DIM : LABEL/LABEL/'
    '/the Key page.s Mode knob turned by a CC/,/^    }$/s/m->heard\[2\]++;//'
    's/if (m->view == V_CHORDS || m->view == V_PATTERN) m->want_where = true;//'
    's/k->w\[i\].idx : idx);/k->w[i].idx : (x % KC_CELL_W) * (n - 1) \/ (KC_CELL_W - 1));/'
    's|return lo < 0 ? -1 : (lo + hi) / 2;|return lo < 0 ? -1 : (lo + hi) / 2 + 1;|'
    's/if (abs(x - k->x0) < KC_START_PX) return 0;//'
    's/bool r = k->in_row \&\& !(k->locked_press \&\& !k->moved);/bool r = k->dragging;/'
    's/fm1scr_note(m, "THE FM-1 DIDN.T TAKE IT", now);//'
    's/j = knobctl_find(list, m->value\[i\], near);/j = knobctl_find(list, m->value[i], near); if (j < 0) j = 0;/'
    's/? KT_LIST\[view\]\[i\] : -1;/? (KT_LIST[view][i] < 0 ? 0 : KT_LIST[view][i]) : -1;/'
    's/                k->want_where = true;//'
    's/for (int dots = 0; dots < 2 \&\& best < 0; dots++)/for (int dots = 0; dots < 1 \&\& best < 0; dots++)/'
    's|int t = k->at + (off > 0 ? off / s : -(-off / s));|int t = k->at + (off > 0 ? (off + s / 2) / s : -((-off + s / 2) / s));|'
    's/abs(idx - k->w\[i\].idx) < abs(idx - k->w\[i\].before)/0/'
    's/m->view != k->view/0/g'
    's/k->base = k->at = (int16_t)(k->w\[i\].waiting \&\& k->w\[i\].list == list ? k->w\[i\].idx : idx);/k->base = k->at = (int16_t)idx;/'
)
caught=0
for i in {1..${#MN}}; do
    rm -rf $O/b
    mkdir -p $O/b
    cp $V/fm1scr.c $V/fm1scr.h $V/knobctl.c $V/knobctl.h $V/knobtab.h $O/b/
    sed "${ME[$i]}" $V/${MF[$i]} > $O/b/${MF[$i]}
    if cmp -s $O/b/${MF[$i]} $V/${MF[$i]}; then
        print -r -- "FAIL: break $i didn't apply: ${MN[$i]}"
        exit 1
    fi
    { fm1scr $O/b/fm1scr.c $O/mut && kc $O/b $O/mutk } 2> $O/mut.err || { cat $O/mut.err; exit 1 }
    : > $O/mut.out
    bad=
    $O/mut $C --quick >> $O/mut.out 2>&1 || bad=1
    $O/mutk units >> $O/mut.out 2>&1 || bad=1
    $O/mutk plan $C $O/mut.amb >> $O/mut.out 2>&1 || bad=1
    if [[ -z $bad && -n $amb ]]; then
        $amb $O/mut.amb $O/run > $O/mut.txt 2> /dev/null
        $O/mutk loop $O/mut.txt >> $O/mut.out 2>&1 || bad=1
    fi
    if [[ -z $bad ]]; then
        print -r -- "FAIL: not caught: ${MN[$i]}"
    else
        caught=$((caught + 1))
        print -r -- "caught: ${MN[$i]} ($(grep -c '^FAIL' $O/mut.out) failures shown; first: $(grep -m1 '^FAIL' $O/mut.out | cut -c1-110))"
    fi
done
print -r -- "breaks caught: $caught of ${#MN}"
rm -rf $O
[[ $caught == ${#MN} ]]
