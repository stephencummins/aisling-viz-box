#!/bin/zsh
# Test 14's host checks (tests 12 and 13 in one build), on the Mac.
#   zsh run.sh [--quick]     --quick: test 12's checks without test 11's whole run.sh (some six minutes less)
#
# Test 14 has no code of its own, so it has no checks of its own either: what is checked is that it is tests 12 and
# 13 and nothing else, and that each one's checks, deliberate breaks included, still pass against it.
#   every file but main.c is test 12's byte for byte (titles and the USB name say 14; CMakeLists.txt adds guest.c),
#     and guest.c and guest.h are test 13's
#   main.c: what it changes of test 12's is what test 13 changed of test 11's, and what it changes of test 13's is
#     what test 12 changed of test 11's (code lines; the comments at its head are its own)
#   test 13's run.sh against test 14's folder (without its file comparison with test 11, which test 12 made untrue)
#   test 12's run.sh against test 14's folder, test 11's inside it
set -e
HERE=${0:A:h}
cd $HERE
V=../../viz-test14
V11=../../viz-test11
V12=../../viz-test12
V13=../../viz-test13
T12=${HERE:h}/test12
T13=${HERE:h}/test13
O=$(mktemp -d)
quick=
[[ $1 == --quick ]] && quick=--quick

print -r -- "-- test 14 is tests 12 and 13"
for f in $V12/*(.); do
    n=${f:t}
    case $n in
        main.c|README.md) ;;
        cal.c|card.c) sed 's/AISLING VIZ TEST 12/AISLING VIZ TEST 14/' $f | cmp -s - $V/$n || { print -r -- "FAIL: $n is not test 12's but for its title"; exit 1 } ;;
        usb_descriptors.c) sed 's/"AISLING viz test 12"/"AISLING viz test 14"/' $f | cmp -s - $V/$n || { print -r -- "FAIL: $n is not test 12's but for the name"; exit 1 } ;;
        CMakeLists.txt)
            [[ "$(diff <(sed 's/viz_test12/viz_test14/g' $f) $V/$n | grep '^[<>]' | grep -v '^[<>] #')" == ">     guest.c" ]] ||
                { print -r -- "FAIL: CMakeLists.txt is not test 12's with guest.c"; exit 1 } ;;
        *) cmp -s $f $V/$n || { print -r -- "FAIL: $n is not test 12's"; exit 1 } ;;
    esac
done
for n in guest.c guest.h; do
    cmp -s $V13/$n $V/$n || { print -r -- "FAIL: $n is not test 13's"; exit 1 }
done
[[ $(ls $V/*(.) | wc -l) -eq $(($(ls $V12/*(.) | wc -l) + 2)) ]] || { print -r -- "FAIL: test 14 has a file that is neither's"; exit 1 }
# main.c: the two sets of changes, each found whole in the merge. Lines that are only a comment are left out (the
# three heads differ), and a test's number in a line doesn't count.
# Sorted: where diff hangs a closing brace or a blank line between two hunks is its own choice.
changes() { diff $1 $2 | grep '^[<>]' | grep -v '^[<>] *//' | sed -E 's/(test|TEST) 1[1-4]/\1 N/g' | sort || true }
changes $V11/main.c $V13/main.c > $O/c13
changes $V12/main.c $V/main.c > $O/c13in14
changes $V11/main.c $V12/main.c > $O/c12
changes $V13/main.c $V/main.c > $O/c12in14
[[ -s $O/c13 && -s $O/c12 ]] || { print -r -- "FAIL: nothing to compare"; exit 1 }
# The one place both changed (redraw_all's FM-1 line): test 12 put the card's page before it, test 13 added the
# guest's redraw to it, so each side sees the other's line as it left it.
cmp -s $O/c13 $O/c13in14 || { diff $O/c13 $O/c13in14 | head -20; print -r -- "FAIL: main.c's changes to test 12's are not test 13's changes"; exit 1 }
diff $O/c12 $O/c12in14 > $O/d12 || true
if [[ -s $O/d12 ]]; then cat $O/d12 | head -20; print -r -- "FAIL: main.c's changes to test 13's are not test 12's changes"; exit 1; fi
print -r -- "the same: test 12's files, test 13's guest.c and guest.h; main.c has each one's changes whole" \
    "($(wc -l < $O/c12 | tr -d ' ') lines of test 12's, $(wc -l < $O/c13 | tr -d ' ') of test 13's) and no others"

print -r -- "-- test 13's run.sh, against test 14"
sed -e '/^print -r -- "-- the files shared with test 11"$/,/^print -r -- "the same (cal.c but for its title/d' \
    -e 's|viz-test13|viz-test14|g; s/AISLING VIZ TEST 13/AISLING VIZ TEST 14/g' \
    -e "s|^HERE=\${0:A:h}\$|HERE=$T13|" $T13/run.sh > $O/run13.sh
grep -q "^HERE=$T13\$" $O/run13.sh && grep -q '^V=../../viz-test14$' $O/run13.sh && ! grep -q 'is not test 11' $O/run13.sh ||
    { print -r -- "FAIL: test 13's run.sh has moved"; exit 1 }
zsh $O/run13.sh --quick > $O/run13.out 2>&1 || { grep FAIL $O/run13.out | head; tail -5 $O/run13.out; print -r -- "FAIL: test 13's checks"; exit 1 }
grep -E "units|main.c read|breaks caught|all passed" $O/run13.out | sed 's/^ */   /'
grep -q "^test 13: all passed$" $O/run13.out && [[ $(grep -c "^ *FAIL" $O/run13.out) == 0 ]] || { print -r -- "FAIL: in test 13's checks"; exit 1 }

print -r -- "-- test 12's run.sh, against test 14${quick:+ (quick)}"
sed -e 's|viz-test12|viz-test14|g; s/AISLING VIZ TEST 12/AISLING VIZ TEST 14/g; s/title says test 12/title says test 14/' \
    -e "s|^HERE=\${0:A:h}\$|HERE=$T12|" $T12/run.sh > $O/run12.sh
grep -q "^HERE=$T12\$" $O/run12.sh && grep -q '^V=../../viz-test14$' $O/run12.sh || { print -r -- "FAIL: test 12's run.sh has moved"; exit 1 }
zsh $O/run12.sh $quick > $O/run12.out 2>&1 || { grep FAIL $O/run12.out | head; tail -5 $O/run12.out; print -r -- "FAIL: test 12's checks"; exit 1 }
grep -E "^(sd|units|fs|notfat|full|pull|msc|main|names):|breaks caught" $O/run12.out | sed 's/^ */   /'
[[ $(grep -c "^ *FAIL" $O/run12.out) == 0 ]] || { print -r -- "FAIL: in test 12's checks"; exit 1 }
grep -q "^breaks caught: 47 of 47$" $O/run12.out || { print -r -- "FAIL: not all of test 12's breaks caught"; exit 1 }

print -r -- "test 14: all passed"
