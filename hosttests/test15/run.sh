#!/bin/zsh
# Test 15's host checks (test 14 with the picture turned half a turn), on the Mac.
#   zsh run.sh [--quick]     --quick: test 12's checks without test 11's whole run.sh (some six minutes less)
#
#   test 15 is test 14 and the turn, and nothing else: every file but lcd.c is test 14's byte for byte (titles, the
#     USB name and main.c's head say 15; CMakeLists.txt adds VIZ_FLIP), and lcd.c loses no line of test 14's and,
#     built without VIZ_FLIP, is test 14's to the compiler
#   flip_check.c    the turn itself, built with it and without: the byte the screen is sent at start-up and after
#                   lcd_resetup; the touch at Stephen's corners and centre, at every raw reading, and which way a
#                   finger moves. It checks that the touch turns with the picture; that 0xE8 turns this screen's
#                   picture half a turn and not some other way is the datasheet's word, and only the board can say
#   deliberate breaks of lcd.c, each to be caught
#   test 13's and test 12's run.sh against test 15's folder, as test 14's run.sh runs them (they build lcd.c
#     without VIZ_FLIP, so what they check is that the turn has changed nothing when it is off)
set -e
HERE=${0:A:h}
cd $HERE
V=../../viz-test15
V14=../../viz-test14
T12=${HERE:h}/test12
T13=${HERE:h}/test13
O=$(mktemp -d)
F=(-std=gnu11 -g -O1 -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined -fno-sanitize-recover=undefined)
quick=
[[ $1 == --quick ]] && quick=--quick

print -r -- "-- test 15 is test 14 and the turn"
for f in $V14/*(.); do
    n=${f:t}
    case $n in
        README.md|lcd.c) ;;
        cal.c|card.c) sed 's/AISLING VIZ TEST 14/AISLING VIZ TEST 15/' $f | cmp -s - $V/$n || { print -r -- "FAIL: $n is not test 14's but for its title"; exit 1 } ;;
        usb_descriptors.c) sed 's/"AISLING viz test 14"/"AISLING viz test 15"/' $f | cmp -s - $V/$n || { print -r -- "FAIL: $n is not test 14's but for the name"; exit 1 } ;;
        main.c)   # code lines; the comments at its head are its own
            diff <(grep -v '^ *//' $f | sed 's/---- AISLING viz test 14 ----/---- AISLING viz test 15 ----/') <(grep -v '^ *//' $V/$n) > /dev/null ||
                { print -r -- "FAIL: main.c is not test 14's but for its name in the log"; exit 1 } ;;
        CMakeLists.txt)
            diff <(sed 's/viz_test14/viz_test15/g' $f) $V/$n | grep '^[<>]' | grep -v '^[<>] #' > $O/cm || true
            print -r -- '> option(VIZ_FLIP "the picture and the touch turned half a turn" ON)
> if(VIZ_FLIP)
>     target_compile_definitions(viz_test15 PRIVATE VIZ_FLIP=1)
> endif()' | cmp -s - $O/cm || { cat $O/cm; print -r -- "FAIL: CMakeLists.txt is not test 14's with VIZ_FLIP"; exit 1 } ;;
        *) cmp -s $f $V/$n || { print -r -- "FAIL: $n is not test 14's"; exit 1 } ;;
    esac
done
[[ $(ls $V/*(.) | wc -l) -eq $(ls $V14/*(.) | wc -l) ]] || { print -r -- "FAIL: test 15 has a file test 14 hasn't"; exit 1 }
# lcd.c: nothing of test 14's taken out, and with the turn off the compiler sees test 14's
[[ $(diff $V14/lcd.c $V/lcd.c | grep -c '^<') == 0 ]] || { print -r -- "FAIL: lcd.c has lost a line of test 14's"; exit 1 }
clang -E -P -I../test4/stub -I$V14 $V14/lcd.c > $O/lcd14.i
clang -E -P -I../test4/stub -I$V $V/lcd.c > $O/lcd15.i
clang -E -P -DVIZ_FLIP=1 -I../test4/stub -I$V $V/lcd.c > $O/lcd15f.i
[[ -s $O/lcd14.i ]] && cmp -s $O/lcd14.i $O/lcd15.i || { print -r -- "FAIL: lcd.c without VIZ_FLIP is not test 14's"; exit 1 }
! cmp -s $O/lcd15.i $O/lcd15f.i || { print -r -- "FAIL: VIZ_FLIP changes nothing in lcd.c"; exit 1 }
print -r -- "the same: test 14's files; lcd.c adds $(diff $V14/lcd.c $V/lcd.c | grep -c '^>') lines and, with the turn off, is test 14's" \
    "(with it on, $(diff $O/lcd15.i $O/lcd15f.i | grep -c '^>') lines differ)"

print -r -- "-- the turn"
# lcd.c with its pen IRQ and its touch read renamed, so flip_check.c can play a touch in
flip() {   # $1 lcd.c  $2 out (out.0 without the turn, out.1 with it)
    local d
    for d in 0 1; do
        clang $F -DVIZ_FLIP=$d -Dgpio_get=t15_gpio_get -Dspi_write_read_blocking=t15_spi_write_read_blocking \
            -I../test4/stub -I$V -c $1 -o $O/lcd$d.o
        clang $F -DVIZ_FLIP=$d -I../test4/stub -I$V flip_check.c $O/lcd$d.o $V/tone.c ../test4/stub.c -lm -o $2.$d
    done
}
flip $V/lcd.c $O/flip
$O/flip.0 > $O/flip0.out || { cat $O/flip0.out; print -r -- "FAIL: the turn off"; exit 1 }
$O/flip.1 > $O/flip1.out || { cat $O/flip1.out; print -r -- "FAIL: the turn on"; exit 1 }
tail -1 $O/flip0.out; tail -1 $O/flip1.out
grep -q "^flip 0: [1-9][0-9]* checks, 0 failed$" $O/flip0.out && grep -q "^flip 1: [1-9][0-9]* checks, 0 failed$" $O/flip1.out ||
    { print -r -- "FAIL: the turn's checks didn't run"; exit 1 }
# The build file must turn it on: what is flashed is the turned picture
grep -q '^option(VIZ_FLIP "the picture and the touch turned half a turn" ON)$' $V/CMakeLists.txt || { print -r -- "FAIL: VIZ_FLIP is not on in the build"; exit 1 }

print -r -- "-- deliberate breaks, each to be caught"
BN=("the touch's y not turned" "the touch's x not turned" "the touch not turned at all" "the touch one pixel out"
    "the picture mirrored, not turned (MX alone)" "the picture mirrored, not turned (MY alone)"
    "the picture not turned" "the turn on whether asked for or not")
BS=('/^bool touch_screen/,/^}/s/^    \*y = LCD_H - 1 - \*y;$//'
    '/^bool touch_screen/,/^}/s/^    \*x = LCD_W - 1 - \*x;$//'
    '/^bool touch_screen/,/^}/s/^#if VIZ_FLIP$/#if 0/'
    '/^bool touch_screen/,/^}/s/^    \*x = LCD_W - 1 - \*x;$/    *x = LCD_W - *x;/'
    's/^#define VIZ_MADCTL 0xE8$/#define VIZ_MADCTL 0x68/'
    's/^#define VIZ_MADCTL 0xE8$/#define VIZ_MADCTL 0xA8/'
    's/^#define VIZ_MADCTL 0xE8$/#define VIZ_MADCTL 0x28/'
    's/^#define VIZ_FLIP 0$/#define VIZ_FLIP 1/')
caught=0
mkdir -p $O/b
for i in {1..$#BS}; do
    sed -e $BS[$i] $V/lcd.c > $O/b/lcd.c
    if cmp -s $O/b/lcd.c $V/lcd.c; then print -r -- "FAIL: break $i ($BN[$i]) changed nothing"; exit 1; fi
    # the last one fights the -D the check builds with; it is built as the board builds, without one
    if [[ $i == $#BS ]]; then
        clang -E -P -I../test4/stub -I$V $O/b/lcd.c | cmp -s - $O/lcd14.i && { print -r -- "FAIL: break $i ($BN[$i]) not caught"; exit 1 }
        caught=$((caught + 1)); print -r -- "   caught: $BN[$i]"; continue
    fi
    flip $O/b/lcd.c $O/b/flip
    if $O/b/flip.0 > /dev/null && $O/b/flip.1 > /dev/null; then print -r -- "FAIL: break $i ($BN[$i]) not caught"; exit 1; fi
    caught=$((caught + 1)); print -r -- "   caught: $BN[$i]"
done
print -r -- "breaks caught: $caught of $#BS"

print -r -- "-- test 13's run.sh, against test 15"
sed -e '/^print -r -- "-- the files shared with test 11"$/,/^print -r -- "the same (cal.c but for its title/d' \
    -e 's|viz-test13|viz-test15|g; s/AISLING VIZ TEST 13/AISLING VIZ TEST 15/g' \
    -e "s|^HERE=\${0:A:h}\$|HERE=$T13|" $T13/run.sh > $O/run13.sh
grep -q "^HERE=$T13\$" $O/run13.sh && grep -q '^V=../../viz-test15$' $O/run13.sh && ! grep -q 'is not test 11' $O/run13.sh ||
    { print -r -- "FAIL: test 13's run.sh has moved"; exit 1 }
zsh $O/run13.sh --quick > $O/run13.out 2>&1 || { grep FAIL $O/run13.out | head; tail -5 $O/run13.out; print -r -- "FAIL: test 13's checks"; exit 1 }
grep -E "units|main.c read|breaks caught|all passed" $O/run13.out | sed 's/^ */   /'
grep -q "^test 13: all passed$" $O/run13.out && [[ $(grep -c "^ *FAIL" $O/run13.out) == 0 ]] || { print -r -- "FAIL: in test 13's checks"; exit 1 }

print -r -- "-- test 12's run.sh, against test 15${quick:+ (quick)}"
# lcd.c is no longer test 11's byte for byte: it is left out of test 12's comparison (it is compared above, with test 14's)
sed -e 's|viz-test12|viz-test15|g; s/AISLING VIZ TEST 12/AISLING VIZ TEST 15/g; s/title says test 12/title says test 15/' \
    -e 's/\[\[ \$n == (cal.c|cal.h|main.c|tusb_config.h|usb_descriptors.c) \]\] && continue/[[ $n == (cal.c|cal.h|lcd.c|main.c|tusb_config.h|usb_descriptors.c) ]] \&\& continue/' \
    -e "s|^HERE=\${0:A:h}\$|HERE=$T12|" $T12/run.sh > $O/run12.sh
grep -q "^HERE=$T12\$" $O/run12.sh && grep -q '^V=../../viz-test15$' $O/run12.sh && grep -q '(cal.c|cal.h|lcd.c|main.c|' $O/run12.sh ||
    { print -r -- "FAIL: test 12's run.sh has moved"; exit 1 }
zsh $O/run12.sh $quick > $O/run12.out 2>&1 || { grep FAIL $O/run12.out | head; tail -5 $O/run12.out; print -r -- "FAIL: test 12's checks"; exit 1 }
grep -E "^(sd|units|fs|notfat|full|pull|msc|main|names):|breaks caught" $O/run12.out | sed 's/^ */   /'
[[ $(grep -c "^ *FAIL" $O/run12.out) == 0 ]] || { print -r -- "FAIL: in test 12's checks"; exit 1 }
grep -q "^breaks caught: 47 of 47$" $O/run12.out || { print -r -- "FAIL: not all of test 12's breaks caught"; exit 1 }

print -r -- "test 15: all passed"
