#!/bin/zsh
# Test 6's host checks, on the Mac with AddressSanitizer + UBSan:
#   land.c, tone.c, hot.h  test 5's, unchanged (compared byte for byte, so test 5's comparison with test 4 stands)
#   pumpsim.c        main.c's own pump code (cut out of main.c) against a pretend DMA, now with trips to the FM-1's
#                    screen as well as the calibration screen
#   reader_check.c   test 5's: reader.c against AISLING's own announce.c (the piece in sync)
#   fm1scr_check.c   the FM-1's screen: everything AISLING 1.2 says (corpus.txt), through announce.c and reader.c,
#                    the model it builds and every pixel it draws
#   test 4's checks  (lcd, calibration, warmth, the landscape harness) against test 6's files
# then each of nine deliberate breaks of fm1scr.c, each of which fm1scr_check must catch.
# run.sh DIR also writes pictures of the FM-1's screen into DIR (PNG).
set -e
cd "${0:A:h}"
V5=../../viz-test5
V=../../viz-test6
A=$HOME/Projects/fm1-aisling/firmware/src/announce.c
O=$(mktemp -d)
F=(-std=gnu11 -g -O1 -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined -fno-sanitize-recover=undefined)

print -r -- "-- test 5's paint, unchanged"
for f in land.c land.h tone.c tone.h hot.h; do
    cmp $V5/$f $V/$f && print -r -- "$f: the same as test 5's"
done

# main.c's pump: from its strip buffers to just before spi_i, word for word.
sed -n '/^static uint16_t strip\[2\]/,/^static int spi_i/p' $V/main.c | sed '$d' > $O/pump_block.inc
print -r -- "-- pump, $(wc -l < $O/pump_block.inc | tr -d ' ') lines of main.c"
clang $F -I$V -I$O -Wno-unused-variable -Wno-unused-function pumpsim.c $V/land.c $V/tone.c -lm -o $O/pumpsim
$O/pumpsim ../test2/midilog.txt | grep -E "FAIL|passed|pushes|seed"

if [[ ! -f $A ]]; then
    print -r -- "FAIL: no $A (AISLING's announce.c, which the next two checks build against)"
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
if [[ -n $1 ]]; then
    mkdir -p $1
    $O/fm1scr corpus.txt --shots $1
    for p in $1/*.ppm; do sips -s format png $p --out ${p:r}.png > /dev/null && rm $p; done
    print -r -- "pictures: $1"
else
    $O/fm1scr corpus.txt
fi

print -r -- "-- test 4's checks against test 6"
T4=../test4
clang $F -I$T4/stub -I$V -c $V/lcd.c -o $O/lcd.o
clang $F -I$T4/stub -I$V -Dlcd_text=test_lcd_text -c $V/cal.c -o $O/cal.o
clang $F -I$T4/stub -I$V -c $V/tone.c -o $O/tone.o
clang $F -I$T4/stub -I$V -c $T4/stub.c -o $O/stub.o
sed 's/AISLING VIZ TEST 4/AISLING VIZ TEST 6/; s/title says test 4/title says test 6/' $T4/test.c > $O/test.c
clang $F -I$T4/stub -I$V -c $O/test.c -o $O/test.o
clang $F $O/lcd.o $O/cal.o $O/tone.o $O/stub.o $O/test.o -o $O/t4
$O/t4 | grep -E "FAIL|passed"
clang $F -I$V $T4/land4.c $V/land.c $V/tone.c -lm -o $O/land4
$O/land4 | grep -E "FAIL|passed"
clang $F -I$V ../test3/harness3.c $V/land.c $V/tone.c -lm -o $O/harness
$O/harness | tail -3

# Each break must make fm1scr_check fail (the corpus and what's beyond it; --quick leaves out the random texts).
print -r -- "-- nine deliberate breaks of fm1scr.c, each to be caught"
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
)
ME=(
    's/if (n > bl \&\& /if ((best < 0 || n < bl) \&\& /'
    's/^    m->dirty |= FM1_D_KNOB << i;$//'
    's/fill(0, 140, LCD_W, 2, RULE);//'
    "s/return \\*p == ' ' || !\\*p || \\*p == ',';/return true;/"
    's/if (whole \&\& said_play \&\& !said_unsaved)/if (0)/'
    's/box_text(0, 0, LCD_W, FM1SCR_BOTTOM, 124/box_text(0, 0, LCD_W, FM1SCR_BOTTOM + 1, 124/'
    's|^    if (m->view >= 0) {   // "Ornament|    if (m->view >= 0 \&\& t[1] >= '"'a'"' \&\& t[1] <= '"'z'"') {   // "Ornament|'
    's/bool cut = n >= CUT_AT;/bool cut = false;/'
    's|m->want_where = true;   // a new key: a new scale|;|'
)
caught=0
for i in {1..${#MN}}; do
    sed "${ME[$i]}" $V/fm1scr.c > $O/fm1scr.c
    if cmp -s $O/fm1scr.c $V/fm1scr.c; then
        print -r -- "FAIL: break $i didn't apply: ${MN[$i]}"
        exit 1
    fi
    cp $V/fm1scr.h $O/
    fm1scr $O/fm1scr.c $O/mut 2> $O/mut.err || { cat $O/mut.err; exit 1 }
    if $O/mut corpus.txt --quick > $O/mut.out 2>&1; then
        print -r -- "FAIL: not caught: ${MN[$i]}"
    else
        caught=$((caught + 1))
        print -r -- "caught: ${MN[$i]} ($(grep -c '^FAIL' $O/mut.out) failures shown; first: $(grep -m1 '^FAIL' $O/mut.out | cut -c1-110))"
    fi
done
print -r -- "breaks caught: $caught of ${#MN}"
rm -rf $O
[[ $caught == ${#MN} ]]
