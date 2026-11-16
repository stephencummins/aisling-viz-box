#!/bin/zsh
# Test 16's host checks (test 15 with the strip of pieces moved in from the screen's right edge), on the Mac.
#   zsh run.sh [--quick]     --quick: test 12's checks without test 11's whole run.sh (some six minutes less)
#
#   test 16 is test 15 and the strip, and nothing else: every file but main.c is test 15's byte for byte (titles and
#     the USB name say 16), strip.h is the one new file, and main.c's code differs from test 15's by the lines in
#     main_changes.txt and no others
#   strip_check.c   strip.h with the pieces as main.c names them: the cells fill the strip, the strip stops short of
#                   the edge, every pixel of the row is in the right cell, and each name and tempo is drawn inside
#                   its cell with room about it. It checks where things are put; how much the brick covers is the
#                   tray's, and only the board in it can say
#   main.c read: the touch and the drawing both go by strip.h
#   deliberate breaks of strip.h and main.c, each to be caught
#   test 15's run.sh against test 16's folder (without its comparison of main.c with test 14's, which test 16 made
#     untrue), and inside it test 13's and test 12's
set -e
HERE=${0:A:h}
cd $HERE
V=../../viz-test16
V15=../../viz-test15
T15=${HERE:h}/test15
O=$(mktemp -d)
F=(-std=gnu11 -g -O1 -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=undefined)
quick=
[[ $1 == --quick ]] && quick=--quick

print -r -- "-- test 16 is test 15 and the strip"
for f in $V15/*(.); do
    n=${f:t}
    case $n in
        README.md|main.c) ;;
        cal.c|card.c) sed 's/AISLING VIZ TEST 15/AISLING VIZ TEST 16/' $f | cmp -s - $V/$n || { print -r -- "FAIL: $n is not test 15's but for its title"; exit 1 } ;;
        usb_descriptors.c) sed 's/"AISLING viz test 15"/"AISLING viz test 16"/' $f | cmp -s - $V/$n || { print -r -- "FAIL: $n is not test 15's but for the name"; exit 1 } ;;
        CMakeLists.txt) sed 's/viz_test15/viz_test16/g' $f | cmp -s - $V/$n || { print -r -- "FAIL: CMakeLists.txt is not test 15's but for the name"; exit 1 } ;;
        *) cmp -s $f $V/$n || { print -r -- "FAIL: $n is not test 15's"; exit 1 } ;;
    esac
done
[[ -f $V/strip.h && $(ls $V/*(.) | wc -l) -eq $(($(ls $V15/*(.) | wc -l) + 1)) ]] || { print -r -- "FAIL: test 16 is not test 15's files and strip.h"; exit 1 }
# main.c: code lines (the comments at its head are its own)
changes() { diff <(grep -v '^ *//' $V15/main.c | sed 's/---- AISLING viz test 15 ----/---- AISLING viz test 16 ----/') <(grep -v '^ *//' $1) | grep '^[<>]' || true }
changes $V/main.c > $O/changes
[[ -s $O/changes ]] && cmp -s $O/changes main_changes.txt || { diff $O/changes main_changes.txt | head -20; print -r -- "FAIL: main.c's changes from test 15's are not main_changes.txt's"; exit 1 }
print -r -- "the same: test 15's files and strip.h; main.c loses $(grep -c '^<' $O/changes) lines of test 15's and has $(grep -c '^>' $O/changes) new"

# The pieces, as main.c has them, for the check
pieces() { sed -n '/^static const struct { const char \*name; uint8_t bpm; } PIECES\[8\] = {$/,/^};$/p' $1 | sed 's/uint8_t/unsigned char/' }
strip() {   # $1 the folder strip.h is in  $2 main.c  $3 out
    pieces $2 > $O/pieces.h
    [[ $(wc -l < $O/pieces.h) -eq 4 ]] || { print -r -- "FAIL: main.c's PIECES not found"; exit 1 }
    clang $F -I$1 -I$O strip_check.c -o $3
}
# main.c read: what it does with strip.h
read_main() {   # $1 main.c
    grep -q '^#include "strip.h"$' $1 &&
    ! grep -q 'CELL_W' <(grep -v KC_CELL_W $1) &&
    grep -q '!guest_page(&gu) ? strip_cell(x) : -1;' $1 &&
    grep -q '^        int x = strip_x(i), cw = STRIP_CW\[i\];$' $1 &&
    grep -q '^        lcd_fill_rect(x, STRIP_Y, cw, STRIP_H, bg);$' $1 &&
    [[ $(grep -c '^        lcd_text(x + (cw - w + 1) / 2, STRIP_Y + [0-9]*, [a-z]*, 1, fg2\{0,1\}, bg);$' $1) == 2 ]] &&
    grep -q '^    lcd_fill_rect(STRIP_W, STRIP_Y, LCD_W - STRIP_W, STRIP_H, C_BLACK);' $1
}

print -r -- "-- the strip"
strip $V $V/main.c $O/strip
$O/strip > $O/strip.out || { cat $O/strip.out; print -r -- "FAIL: the strip"; exit 1 }
tail -1 $O/strip.out
grep -q "^strip: [1-9][0-9]* checks, 0 failed$" $O/strip.out || { print -r -- "FAIL: the strip's checks didn't run"; exit 1 }
read_main $V/main.c || { print -r -- "FAIL: main.c doesn't go by strip.h"; exit 1 }
print -r -- "main.c read: the touch and the drawing go by strip.h"

print -r -- "-- deliberate breaks, each to be caught"
# f: the file broken (strip.h: strip_check must fail; main.c: the reading, or the check with its pieces, must)
BN=("the strip run to the screen's edge again" "a cell too many pixels wide" "TIR NA NOG's cell given to ARAN"
    "a touch on a cell's last pixel taken for the next" "a touch right of the strip taken for the last piece"
    "the cells drawn a pixel along" "a short cell too small for a finger"
    "main.c: the touch still by eight even cells" "main.c: the cells drawn at the old width"
    "main.c: the brick's margin not cleared" "main.c: a name longer than its cell")
BF=(strip.h strip.h strip.h strip.h strip.h strip.h strip.h main.c main.c main.c main.c)
BS=('s/^#define STRIP_W 464 /#define STRIP_W 480 /'
    's/{56, 54, 50, 56, 56, 58, 64, 70}/{56, 54, 50, 56, 56, 58, 64, 72}/'
    's/{56, 54, 50, 56, 56, 58, 64, 70}/{56, 54, 70, 56, 56, 58, 64, 50}/'
    's/^        if (x < STRIP_CW\[i\]) return i;$/        if (x < STRIP_CW[i] - 1) return i;/'
    '/^static inline int strip_cell/,/^}/s/^    return -1;$/    return STRIP_N - 1;/'
    '/^static inline int strip_x/,/^}/s/^    int x = 0;$/    int x = 1;/'
    's/{56, 54, 50, 56, 56, 58, 64, 70}/{56, 60, 44, 56, 56, 58, 64, 70}/'
    's|!guest_page(&gu) ? strip_cell(x) : -1;|!guest_page(\&gu) ? x / (LCD_W / 8) : -1;|'
    's/^        int x = strip_x(i), cw = STRIP_CW\[i\];$/        int x = strip_x(i), cw = LCD_W \/ 8;/'
    '/^    lcd_fill_rect(STRIP_W, STRIP_Y, LCD_W - STRIP_W, STRIP_H, C_BLACK);/d'
    's/{"CONNEMARA", 92}, {"TIR NA NOG", 80},/{"CONNEMARA", 92}, {"TIR NA NOG AGUS", 80},/')
caught=0
mkdir -p $O/b
for i in {1..$#BS}; do
    cp $V/strip.h $V/main.c $O/b/
    sed -e $BS[$i] $V/$BF[$i] > $O/b/$BF[$i]
    if cmp -s $O/b/$BF[$i] $V/$BF[$i]; then print -r -- "FAIL: break $i ($BN[$i]) changed nothing"; exit 1; fi
    strip $O/b $O/b/main.c $O/b/strip
    if $O/b/strip > /dev/null && read_main $O/b/main.c; then print -r -- "FAIL: break $i ($BN[$i]) not caught"; exit 1; fi
    caught=$((caught + 1)); print -r -- "   caught: $BN[$i]"
done
print -r -- "breaks caught: $caught of $#BS"

print -r -- "-- test 15's run.sh, against test 16${quick:+ (quick)}"
# main.c is no longer test 14's but for its name, and strip.h is a file test 14 hasn't: both are compared above, with
# test 15's, so those two lines of test 15's script are taken out. The rest runs as it is, on test 16's folder.
sed -e '/^        main.c)   # code lines; the comments at its head are its own$/,/^                { print -r -- "FAIL: main.c is not test 14.s but for its name in the log"; exit 1 } ;;$/c\
        main.c) ;;' \
    -e '/^\[\[ \$(ls \$V\/\*(.) | wc -l) -eq \$(ls \$V14\/\*(.) | wc -l) \]\] || /d' \
    -e 's|viz-test15|viz-test16|g; s/viz_test15/viz_test16/g; s/AISLING VIZ TEST 15/AISLING VIZ TEST 16/g; s/"AISLING viz test 15"/"AISLING viz test 16"/g; s/title says test 15/title says test 16/' \
    -e "s|^HERE=\${0:A:h}\$|HERE=$T15|" $T15/run.sh > $O/run15.sh
grep -q "^HERE=$T15\$" $O/run15.sh && grep -q '^V=../../viz-test16$' $O/run15.sh && grep -q '^        main.c) ;;$' $O/run15.sh &&
    ! grep -q 'has a file test 14' $O/run15.sh && ! grep -q 'viz.test15' $O/run15.sh || { print -r -- "FAIL: test 15's run.sh has moved"; exit 1 }
zsh $O/run15.sh $quick > $O/run15.out 2>&1 || { grep FAIL $O/run15.out | head; tail -5 $O/run15.out; print -r -- "FAIL: test 15's checks"; exit 1 }
grep -E "^flip|units|main.c read|breaks caught|^   (sd|fs|notfat|full|pull|msc|main|names):|all passed" $O/run15.out | sed 's/^ */   /'
[[ $(grep -c "^ *FAIL" $O/run15.out) == 0 ]] && grep -q "^test 15: all passed$" $O/run15.out || { print -r -- "FAIL: in test 15's checks"; exit 1 }
[[ $(grep -c "^breaks caught: 8 of 8$" $O/run15.out) == 1 ]] && grep -q "breaks caught: 47 of 47" $O/run15.out || { print -r -- "FAIL: not all of the earlier breaks caught"; exit 1 }

print -r -- "test 16: all passed"
