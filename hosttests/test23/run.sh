#!/bin/zsh
# Test 23's host checks (test 22 with a test picture out of the HDMI socket in one build of it), on the Mac.
#   zsh run.sh
#
#   the same:      every file of test 22's is in test 23 byte for byte, but for README.md and the six that changed
#                  (CMakeLists.txt, hot.h, lcd.h, main.c, usb_descriptors.c: named below with what changed), and the
#                  only new files are hdmi.c, hdmi.h, tmds.c, tmds.h
#   tmds_check.c   tmds.c against a decoder written apart from it: every value at every balance, the four control
#                  symbols, and whole lines (blanking, syncs, the bars, noise), bit for bit as the serialiser sends
#   a break:       tmds.c with its two choices of "turn it over" swapped must be caught
# Not checked here, because only the board and a screen can: hdmi.c (the PIO program, the DMA, the interrupt), the
# 252 MHz clock, and whether a screen shows the picture.
set -e
HERE=${0:A:h}
cd $HERE
V=../../viz-test23
V22=../../viz-test22
O=$(mktemp -d)
F=(-std=gnu11 -g -O1 -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined -fno-sanitize-recover=undefined)

print -r -- "-- test 23 is test 22 and the HDMI picture"
bad=0
for f in $V22/*(.); do
    n=${f:t}
    case $n in
        README.md|CMakeLists.txt|hot.h|lcd.h|main.c|usb_descriptors.c) ;;
        *) cmp -s $f $V/$n || { print -r -- "  DIFFERS: $n"; bad=1 } ;;
    esac
done
for f in $V/*(.); do
    n=${f:t}
    [[ -e $V22/$n ]] && continue
    case $n in
        hdmi.c|hdmi.h|tmds.c|tmds.h) ;;
        *) print -r -- "  NEW AND NOT EXPECTED: $n"; bad=1 ;;
    esac
done
# What changed in the five, counted: anything more than these lines is a surprise.
for pair in "hot.h 4" "lcd.h 3" "usb_descriptors.c 2" "main.c 40" "CMakeLists.txt 40"; do
    n=${pair% *}; want=${pair#* }
    got=$(diff $V22/$n $V/$n | grep -c '^[<>]' || true)
    if [[ $got != $want ]]; then print -r -- "  $n: $got lines differ from test 22's, $want expected"; bad=1; fi
done
[[ $bad == 0 ]] && print -r -- "  as expected" || { print -r -- "FAILED"; exit 1 }

print -r -- "-- tmds.c"
cc $F -I$V tmds_check.c $V/tmds.c -o $O/tmds_check
$O/tmds_check

print -r -- "-- a deliberate break: the two ways of turning a value over swapped"
sed -e 's/cnt > 0 \&\& q1 > q0/cnt < 0 \&\& q1 > q0/' -e 's/cnt < 0 \&\& q0 > q1/cnt > 0 \&\& q0 > q1/' $V/tmds.c > $O/tmds_broken.c
cmp -s $V/tmds.c $O/tmds_broken.c && { print -r -- "  the break changed nothing: FAILED"; exit 1 }
cc $F -I$V tmds_check.c $O/tmds_broken.c -o $O/tmds_broken
if $O/tmds_broken > $O/broken.txt 2>&1; then print -r -- "  NOT caught: FAILED"; exit 1; else print -r -- "  caught ($(grep -c FAIL: $O/broken.txt) failures)"; fi

rm -rf $O
print -r -- "test 23: all passed"
