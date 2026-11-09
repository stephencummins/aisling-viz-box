#!/bin/zsh
# Test 2's host checks for land.c, on the Mac with AddressSanitizer + UBSan:
#   harness.c   every tile forced, idle, blooms clearing after all off, no NaN
#   replay.c    the FM-1's logged MIDI (midilog.txt, from the board) at the device's pace, dt capped at 6:
#               fault 1, the fields and bog stall
#   replay_chk.c + finite.c   the same with the cap lifted to 15 (test 3's fix): moving again, all finite
# harness_big.c is the big-step variant used while choosing the cap.
set -e
cd "${0:A:h}"
V=../../viz-test2
O=$(mktemp -d)
F=(-std=gnu11 -g -O1 -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined -fno-sanitize-recover=undefined -I$V)
clang $F harness.c $V/land.c -lm -o $O/harness
$O/harness | tail -3
clang $F replay.c $V/land.c -lm -o $O/replay
echo "replay at 5 fps, cap 6 (test 2 as loaded):"; $O/replay midilog.txt 5 6 | tail -3
clang $F replay_chk.c finite.c -lm -o $O/replay_chk
echo "replay at 5 fps, cap 15:"; $O/replay_chk midilog.txt 5 15 | tail -2
rm -rf $O
