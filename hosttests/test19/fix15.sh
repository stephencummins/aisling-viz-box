#!/bin/zsh
# (test 19) For earlier.sh: in the copy of hosttests/ at $1, test 15's run.sh compares CMakeLists.txt with test 14's
# without the two sources test 19 adds (run.sh has checked they are there). Nothing else of it is changed.
set -e
f=$1/test15/run.sh
cp $f $f.was
sed -i '' -e "s#^            diff <(sed 's/viz_test14/viz_test15/g' \$f) \$V/\$n | grep #            diff <(sed 's/viz_test14/viz_test15/g' \$f) <(grep -v -x -e '    swap.c' -e '    fwpage.c' \$V/\$n) | grep #" $f
[[ $(diff $f.was $f | grep -c '^>') == 1 && $(diff $f.was $f | grep -c '^<') == 1 ]] || { print -r -- "FAIL: test 15's run.sh has moved"; exit 1 }
rm $f.was
