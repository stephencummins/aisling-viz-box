#!/bin/zsh
# Test 3's host checks, on the Mac with AddressSanitizer + UBSan:
#   test.c      lcd.c and cal.c against a recording stand-in for the SDK (stub/), decoding every word sent
#   harness3.c  the landscape: every tile forced, idle, blooms clearing, no NaN, big steps (dt cap 15)
#   ../test2/replay_chk.c  the logged FM-1 MIDI replayed at 4, 5 and 10 frames a second with the cap at 15
set -e
cd "${0:A:h}"
V=../../viz-test3
O=$(mktemp -d)
F=(-std=gnu11 -g -O1 -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined -fno-sanitize-recover=undefined -I$V)
clang $F -Istub -c $V/lcd.c -o $O/lcd.o
clang $F -Istub -Dlcd_text=test_lcd_text -c $V/cal.c -o $O/cal.o
clang $F -Istub -c stub.c -o $O/stub.o
clang $F -Istub -c test.c -o $O/test.o
clang $F $O/*.o -o $O/t3
$O/t3 | grep -E "FAIL|passed"
clang $F harness3.c $V/land.c -lm -o $O/harness
$O/harness | tail -3
clang $F ../test2/replay_chk.c ../test2/finite.c -lm -o $O/replay   # finite.c includes land.c
for fps in 4 5 10; do echo "replay at $fps fps, cap 15:"; $O/replay ../test2/midilog.txt $fps 15 | tail -2; done
rm -rf $O
