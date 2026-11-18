#!/bin/zsh
# (test 19) The earlier tests' checks, against test 19's folder: test 18's run.sh, and inside it test 17's, 16's,
# 15's, 13's, 12's, 11's and 7's, as test 18's run.sh ran them against its own.
#   zsh earlier.sh [--quick]     --quick: test 12's checks without test 11's whole run.sh
#
# The scripts find each other by folder, so they are run from a copy of hosttests/ that is links to this one, with
# the files below written in. What is changed in them, and nothing else:
#   test18/run.sh       the names say 19; without its comparison of the files with test 17's (run.sh compares them
#                       with test 18's)
#   test18/hold.sh      the folder it looks at when given none is test 19's
#   test18/earlier.sh   the names it writes into test 17's run.sh say 19, it takes the folders of the builds
#                       through this copy's links, and it runs fix15.sh on its own copy: test 15's run.sh
#                       compares CMakeLists.txt without the two sources test 19 adds
# Each change is checked to have been made. Everything else in those scripts runs as it was written: what
# test 18's earlier.sh changes in the tests before it, it changes as before.
set -e
HERE=${0:A:h}
cd $HERE
O=$(mktemp -d)
quick=
[[ $1 == --quick ]] && quick=--quick
H=${HERE:h}
M=$O/m
mkdir -p $M/hosttests
for d in ${HERE:h:h}/viz-test*(/); do ln -s $d $M/${d:t}; done
for d in $H/test*(/); do
    [[ ${d:t} == test19 ]] && continue
    mkdir $M/hosttests/${d:t}
    for f in $d/*; do ln -s $f $M/hosttests/${d:t}/${f:t}; done
done
T=$M/hosttests
moved() { print -r -- "FAIL: $1 has moved"; exit 1 }
put() {   # $1 the file (test18/run.sh), the rest: sed's arguments. Written in place of the link.
    local f=$1
    shift
    rm $T/$f
    sed "$@" $H/$f > $T/$f
}
lines() { diff $H/$1 $T/$1 | grep -c "^$2" || true }

put test18/run.sh \
    -e '/^print -r -- "-- test 18 is test 17, the usable width and the hold"$/,/^print -r -- "the same: test 17.s files and ui.h; the nine lose /d' \
    -e 's|^V=../../viz-test18$|V=../../viz-test19|'
grep -q '^V=../../viz-test19$' $T/test18/run.sh && ! grep -q 'cmp -s $O/changes changes.txt' $T/test18/run.sh &&
    ! grep -q 'is not test 17' $T/test18/run.sh && grep -q '^M=$(zsh earlier.sh --tree)$' $T/test18/run.sh &&
    [[ $(lines test18/run.sh '>') == 1 && $(lines test18/run.sh '<') -ge 25 ]] || moved "test 18's run.sh"

put test18/hold.sh -e 's|^V=${1:-../../viz-test18}; V=${V:A}$|V=${1:-../../viz-test19}; V=${V:A}|'
[[ $(lines test18/hold.sh '>') == 1 && $(lines test18/hold.sh '<') == 1 ]] || moved "test 18's hold.sh"

put test18/earlier.sh \
    -e 's#s|viz-test17|viz-test18|g; s/viz_test17/viz_test18/g; s/AISLING VIZ TEST 17/AISLING VIZ TEST 18/g; s/"AISLING viz test 17"/"AISLING viz test 18"/g; s/title says test 17/title says test 18/#s|viz-test17|viz-test19|g; s/viz_test17/viz_test19/g; s/AISLING VIZ TEST 17/AISLING VIZ TEST 19/g; s/"AISLING viz test 17"/"AISLING viz test 19"/g; s/title says test 17/title says test 19/#' \
    -e "s|^grep -q '^V=../../viz-test18\$' \$T/test17/run.sh |grep -q '^V=../../viz-test19\$' \$T/test17/run.sh |" \
    -e 's|^for d in ${HERE:h:h}/viz-test\*(/); do ln -s $d $M/${d:t}; done$|for d in ${HERE:h:h}/viz-test*(-/); do ln -s $d $M/${d:t}; done|' \
    -e 's|^for d in $H/test\*(/); do$|for d in $H/test*(-/); do|' \
    -e 's|^\[\[ $1 == --tree \]\] \&\& { print -r -- $M; exit 0 } |zsh '$HERE'/fix15.sh $T \|\| exit 1\
&|' \
    -e 's/^print -r -- "-- test 17.s run.sh, against test 18\${quick/print -r -- "-- test 17'"'"'s run.sh, against test 19${quick/'
[[ $(lines test18/earlier.sh '>') == 6 && $(lines test18/earlier.sh '<') == 5 ]] && grep -q '/fix15.sh $T || exit 1$' $T/test18/earlier.sh && grep -q 'viz_test17/viz_test19/g' $T/test18/earlier.sh &&
    grep -q "^grep -q '^V=../../viz-test19\$' " $T/test18/earlier.sh || moved "test 18's earlier.sh"

[[ -n $TREE_ONLY ]] && { print -r -- $T; exit 0 }
# Tests 9 to 11 take AISLING's source from a drafts folder of the machine they were written on. Where that isn't,
# and no $FM is given, they get the clone of fm1-aisling (it must be 1.4 or later: they say so if it isn't).
if [[ -z $FM && ! -d $HOME/Projects/aisling-drafts/fw-1.4 && -d $HOME/Projects/fm1-aisling ]]; then
    export FM=$HOME/Projects/fm1-aisling
    print -r -- "(AISLING's source for the earlier tests: $FM)"
fi
print -r -- "-- test 18's run.sh, against test 19${quick:+ (quick)}"
zsh $T/test18/run.sh $quick > $O/run18.out 2>&1 || { grep FAIL $O/run18.out | head; tail -5 $O/run18.out; print -r -- "FAIL: test 18's checks (the whole of what they said: $O/run18.out)"; exit 1 }
grep -E "^pages:|^touch:|^ui:|^frames:|^hold:|^look:|^calibration|main.c read|breaks caught|^ *strip:|all passed" $O/run18.out | sed 's/^ */   /'
[[ $(grep -c "^ *FAIL" $O/run18.out) == 0 ]] && grep -q "^test 18: all passed$" $O/run18.out && grep -q "^ *test 17: all passed$" $O/run18.out &&
    grep -q "^ *test 16: all passed$" $O/run18.out && grep -q "^ *test 15: all passed$" $O/run18.out && grep -q "^earlier: all passed$" $O/run18.out ||
    { print -r -- "FAIL: in test 18's checks"; exit 1 }
[[ $(grep -c "^breaks caught: 28 of 28$" $O/run18.out) == 1 && $(grep -c "^ *breaks caught: 23 of 23$" $O/run18.out) == 1 ]] &&
    grep -q "breaks caught: 11 of 11" $O/run18.out && grep -q "breaks caught: 8 of 8" $O/run18.out && grep -q "breaks caught: 47 of 47" $O/run18.out ||
    { print -r -- "FAIL: not all of the earlier breaks caught"; exit 1 }
print -r -- "earlier: all passed"
