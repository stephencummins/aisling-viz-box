#!/bin/zsh
# Test 5's host checks, on the Mac with AddressSanitizer + UBSan:
#   compare.c  test 5's land.c beside test 4's: the same state after every step and prepare, the same dirty lists,
#              the same pixels (whole screens at every warmth, runs aligned and not), with the step's floating point
#              built three ways (contract off, on, fast: the board's GCC fuses multiply-adds)
#   pumpsim.c  main.c's own pump code (cut out of main.c) against a pretend DMA: no buffer changed while it's being
#              sent, and the screen ends up exactly as the colours say
#   reader_check.c  reader.c (the strip's piece, from the FM-1's screen reader) against AISLING's own announce.c
#   test 4's checks (lcd, calibration, warmth, the landscape harness), against test 5's files
# then, without sanitizers at -O2, the speed of the two renders and the runs a frame.
set -e
cd "${0:A:h}"
V4=../../viz-test4
V5=../../viz-test5
O=$(mktemp -d)
F=(-std=gnu11 -g -O1 -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined -fno-sanitize-recover=undefined)

# land.c's outside names, from nm, each to be prefixed: -Dland_init=t4_land_init and so on.
renames() {
    clang -std=gnu11 -O0 -I$1 -c $1/land.c -o $O/names.o
    nm -gU $O/names.o | awk '{print $3}' | sed 's/^_//' | while read s; do print -- "-D$s=$2$s"; done
}
R4=(${(f)"$(renames $V4 t4_)"})
R5=(${(f)"$(renames $V5 t5_)"})
print "renamed: ${#R4} names in test 4's land.c, ${#R5} in test 5's"

# -fno-builtin-sinf/cosf: left alone, clang on the Mac sometimes merges a sinf and cosf of one angle into Apple's
# __sincosf_stret, whose answers can differ in the last bit, and it merges differently in the two land.c files (test 5's
# loops have the yield in them, so its inlining differs). Under ASan that made the step differ from frame 86 of the
# log by a bit or two. The board never merges them: both builds call sinf and cosf separately, 4 of each in land_step.
X=(-fno-builtin-sinf -fno-builtin-cosf)
for C in off on fast; do
    clang $F $X -ffp-contract=$C -I$V4 $R4 -DLAND_C="\"$V4/land.c\"" -DSTATE_FN=t4_state -c wrap.c -o $O/w4.o
    clang $F $X -ffp-contract=$C -I$V5 $R5 -DLAND_C="\"$V5/land.c\"" -DSTATE_FN=t5_state -c wrap.c -o $O/w5.o
    clang $F -ffp-contract=$C -I$V5 -c compare.c -o $O/compare.o
    clang $F -ffp-contract=$C -I$V5 -c $V5/tone.c -o $O/tone.o
    clang $F $O/w4.o $O/w5.o $O/compare.o $O/tone.o -lm -o $O/compare
    print -r -- "-- compare, fp-contract=$C"
    $O/compare ../test2/midilog.txt | grep -E "FAIL|differs|run x|passed|comparisons|per frame"
done

# main.c's pump: from its strip buffers to just before spi_i, word for word.
sed -n '/^static uint16_t strip\[2\]/,/^static int spi_i/p' $V5/main.c | sed '$d' > $O/pump_block.inc
print -r -- "-- pump, $(wc -l < $O/pump_block.inc | tr -d ' ') lines of main.c"
clang $F -I$V5 -I$O -Wno-unused-variable -Wno-unused-function pumpsim.c $V5/land.c $V5/tone.c -lm -o $O/pumpsim
$O/pumpsim ../test2/midilog.txt | grep -E "FAIL|passed|pushes|seed"

# reader.c (the strip's piece from the FM-1's screen reader) against AISLING's own announce.c, as its firmware builds it.
print -r -- "-- the screen reader, against AISLING's announce.c"
A=$HOME/Projects/fm1-aisling/firmware/src/announce.c
if [[ -f $A ]]; then
    clang $F -Wno-unused-function -I$V5 -DANNOUNCE_C="\"$A\"" -DANN_NAME='"AISLING 1.2"' reader_check.c $V5/reader.c \
        -o $O/reader
    $O/reader | grep -E "FAIL|passed|checks"
else
    print -r -- "SKIPPED: no $A"
fi

print -r -- "-- test 4's checks against test 5"
T4=../test4
clang $F -I$T4/stub -I$V5 -c $V5/lcd.c -o $O/lcd.o
clang $F -I$T4/stub -I$V5 -Dlcd_text=test_lcd_text -c $V5/cal.c -o $O/cal.o
clang $F -I$T4/stub -I$V5 -c $V5/tone.c -o $O/tone.o
clang $F -I$T4/stub -I$V5 -c $T4/stub.c -o $O/stub.o
sed 's/AISLING VIZ TEST 4/AISLING VIZ TEST 5/; s/title says test 4/title says test 5/' $T4/test.c > $O/test.c
clang $F -I$T4/stub -I$V5 -c $O/test.c -o $O/test.o
clang $F $O/lcd.o $O/cal.o $O/tone.o $O/stub.o $O/test.o -o $O/t4
$O/t4 | grep -E "FAIL|passed"
clang $F -I$V5 $T4/land4.c $V5/land.c $V5/tone.c -lm -o $O/land4
$O/land4 | grep -E "FAIL|passed"
clang $F -I$V5 ../test3/harness3.c $V5/land.c $V5/tone.c -lm -o $O/harness
$O/harness | tail -3

print -r -- "-- speed (the host, -O2, no sanitizers)"
P=(-std=gnu11 -O2 -Wno-unused-parameter)
clang $P -I$V4 $R4 -DLAND_C="\"$V4/land.c\"" -DSTATE_FN=t4_state -c wrap.c -o $O/w4.o
clang $P -I$V5 $R5 -DLAND_C="\"$V5/land.c\"" -DSTATE_FN=t5_state -c wrap.c -o $O/w5.o
clang $P -I$V5 compare.c $V5/tone.c $O/w4.o $O/w5.o -lm -o $O/speed
$O/speed ../test2/midilog.txt time
rm -rf $O
