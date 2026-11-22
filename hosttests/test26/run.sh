#!/bin/zsh
# Test 26's host check (test 25 with the paint slowed while the card is lent to the Mac), on the Mac.
#   zsh run.sh
#
#   the same:   every file of test 25's is in test 26 byte for byte, but for README.md and the three that changed
#               (named below with how many lines), and there are no new files. So the checks of tmds.c and
#               hdmipic.c stand for test 26's copies too.
# Not checked here, because only the board and a Mac can: that the lent card mounts.
set -e
HERE=${0:A:h}
cd $HERE
V=../../viz-test26
V24=../../viz-test25
bad=0
for f in $V24/*(.); do
    n=${f:t}
    case $n in
        README.md|CMakeLists.txt|main.c|usb_descriptors.c) ;;
        *) cmp -s $f $V/$n || { print -r -- "  DIFFERS: $n"; bad=1 } ;;
    esac
done
for f in $V/*(.); do
    [[ -e $V24/${f:t} ]] || { print -r -- "  NEW AND NOT EXPECTED: ${f:t}"; bad=1 }
done
for pair in "CMakeLists.txt 32" "usb_descriptors.c 2" "main.c 12"; do
    n=${pair% *}; want=${pair#* }
    got=$(diff $V24/$n $V/$n | grep -c '^[<>]' || true)
    if [[ $got != $want ]]; then print -r -- "  $n: $got lines differ from test 25's, $want expected"; bad=1; fi
done
[[ $bad == 0 ]] && print -r -- "test 26: as expected" || { print -r -- "FAILED"; exit 1 }
