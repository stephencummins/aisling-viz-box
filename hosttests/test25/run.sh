#!/bin/zsh
# Test 25's host check (test 24 with the HDMI picture kept moving on every page), on the Mac.
#   zsh run.sh
#
#   the same:   every file of test 24's is in test 25 byte for byte, but for README.md and the three that changed
#               (named below with how many lines), and there are no new files. So test 24's checks of tmds.c and
#               hdmipic.c stand for test 25's copies too.
# Not checked here, because only the board and a screen can: that the big picture keeps moving on the other pages,
# and what the extra work costs them.
set -e
HERE=${0:A:h}
cd $HERE
V=../../viz-test25
V24=../../viz-test24
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
for pair in "CMakeLists.txt 32" "usb_descriptors.c 2" "main.c 23"; do
    n=${pair% *}; want=${pair#* }
    got=$(diff $V24/$n $V/$n | grep -c '^[<>]' || true)
    if [[ $got != $want ]]; then print -r -- "  $n: $got lines differ from test 24's, $want expected"; bad=1; fi
done
[[ $bad == 0 ]] && print -r -- "test 25: as expected" || { print -r -- "FAILED"; exit 1 }
