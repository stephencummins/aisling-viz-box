#!/bin/zsh
# Test 4's host checks, on the Mac with AddressSanitizer + UBSan:
#   test.c   lcd.c, cal.c and tone.c against a recording stand-in for the SDK (stub/), decoding every word sent
#   land4.c  the warmth in the landscape: red untouched, green and blue scaled by their gains
#   test 3's landscape harness, against test 4's land.c
set -e
cd "${0:A:h}"
V=../../viz-test4
O=$(mktemp -d)
F=(-std=gnu11 -g -O1 -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined -fno-sanitize-recover=undefined -I$V)
clang $F -Istub -c $V/lcd.c -o $O/lcd.o
clang $F -Istub -Dlcd_text=test_lcd_text -c $V/cal.c -o $O/cal.o
clang $F -Istub -c $V/tone.c -o $O/tone.o
clang $F -Istub -c stub.c -o $O/stub.o
clang $F -Istub -c test.c -o $O/test.o
clang $F $O/*.o -o $O/t4
$O/t4 | grep -E "FAIL|passed"
clang $F land4.c $V/land.c $V/tone.c -lm -o $O/land4
$O/land4 | grep -E "FAIL|passed"
clang $F ../test3/harness3.c $V/land.c $V/tone.c -lm -o $O/harness
$O/harness | tail -3
rm -rf $O
