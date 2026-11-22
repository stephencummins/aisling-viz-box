#!/bin/zsh
# Test 24's host checks (test 23 with the landscape out of the HDMI socket in place of the test picture), on the Mac.
#   zsh run.sh
#
#   the same:      every file of test 23's is in test 24 byte for byte, but for README.md and the seven that changed
#                  (named below with how many lines), and the only new files are hdmipic.c and hdmipic.h
#   test 23's:     tmds.c is unchanged, and test 23's check of it is run again from here
#   pic_check.c:   hdmipic.c. The 65 levels: each pair of symbols is tmds.c's coding of two pixels within 1 of the
#                  level's value, and ends with as many ones as zeroes sent. Then whole pictures (noise, slopes,
#                  white, black, in both shapes), worked out in the three turns the board takes: every point decoded
#                  by a decoder written apart from the encoder and held to the level worked out in the check itself,
#                  the stream held to the coding with its balance carried along the line, the blanking untouched
#   two breaks:    a weight swapped, and a shift in the packing off by one, must each be caught
# Not checked here, because only the board and a screen can: hdmi.c (when each row is worked out, the DMA, the
# interrupt), how long the work takes at 252 MHz, and what the screen shows.
set -e
HERE=${0:A:h}
cd $HERE
V=../../viz-test24
V23=../../viz-test23
O=$(mktemp -d)
F=(-std=gnu11 -g -O1 -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined -fno-sanitize-recover=undefined)

print -r -- "-- test 24 is test 23 and the landscape's picture"
bad=0
for f in $V23/*(.); do
    n=${f:t}
    case $n in
        README.md|CMakeLists.txt|hdmi.c|hdmi.h|land.c|land.h|main.c|usb_descriptors.c) ;;
        *) cmp -s $f $V/$n || { print -r -- "  DIFFERS: $n"; bad=1 } ;;
    esac
done
for f in $V/*(.); do
    n=${f:t}
    [[ -e $V23/$n ]] && continue
    case $n in
        hdmipic.c|hdmipic.h) ;;
        *) print -r -- "  NEW AND NOT EXPECTED: $n"; bad=1 ;;
    esac
done
# What changed in the seven, counted: anything more than these lines is a surprise.
for pair in "CMakeLists.txt 38" "usb_descriptors.c 2" "hdmi.c 94" "hdmi.h 12" "land.c 3" "land.h 4" "main.c 19"; do
    n=${pair% *}; want=${pair#* }
    got=$(diff $V23/$n $V/$n | grep -c '^[<>]' || true)
    if [[ $got != $want ]]; then print -r -- "  $n: $got lines differ from test 23's, $want expected"; bad=1; fi
done
[[ $bad == 0 ]] && print -r -- "  as expected" || { print -r -- "FAILED"; exit 1 }

print -r -- "-- tmds.c (test 23's check, on test 24's copy)"
cc $F -I$V ../test23/tmds_check.c $V/tmds.c -o $O/tmds_check
$O/tmds_check

print -r -- "-- hdmipic.c"
cc $F -I$V pic_check.c $V/hdmipic.c $V/tmds.c -o $O/pic_check
$O/pic_check

print -r -- "-- deliberate breaks"
sed -e 's/PAIR(2, a1, a2, e1, e2, 9, 7, d0)/PAIR(2, a1, a2, e1, e2, 7, 9, d0)/' $V/hdmipic.c > $O/break1.c
sed -e 's/(o)\[3\] = (t4) >> 16 | (t5) << 4 | (t6) << 24;/(o)[3] = (t4) >> 16 | (t5) << 5 | (t6) << 24;/' $V/hdmipic.c > $O/break2.c
for b in 1 2; do
    cmp -s $V/hdmipic.c $O/break$b.c && { print -r -- "  break $b changed nothing: FAILED"; exit 1 }
    cc $F -I$V pic_check.c $O/break$b.c $V/tmds.c -o $O/broken$b
    if $O/broken$b > $O/broken$b.txt 2>&1; then print -r -- "  break $b NOT caught: FAILED"; exit 1; else print -r -- "  break $b caught"; fi
done

rm -rf $O
print -r -- "test 24: all passed"
