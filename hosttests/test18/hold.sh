#!/bin/zsh
# (test 18) The two-second hold: hold_check.c built against main.c's own poll_touch, then each deliberate break.
#   zsh hold.sh [folder of the build to check, default ../../viz-test18]
cd ${0:A:h}
V=${1:-../../viz-test18}; V=${V:A}
O=$(mktemp -d /tmp/t18hold.XXXXXX)
F=(-std=gnu11 -g -O1 -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -Wno-unused-variable -fsanitize=address,undefined -fno-sanitize-recover=undefined)
fail=0
# main.c's pieces: the two rows' places and the touch's state; show_cal; the touch itself
cut() {   # $1 the folder holding main.c, $2 where to
    { grep -E '^#define (STATUS_Y|STRIP_Y) ' $1/main.c
      sed -n '/^enum { SCR_LAND, /,/^static uint32_t touch_last_ms, touch_first_ms;$/p' $1/main.c } > $2/defs.inc
    sed -n '/^static void show_cal(void) {$/,/^}$/p' $1/main.c > $2/cal.inc
    sed -n '/^static bool land_touching; /,/^static void draw_strip(void) {$/p' $1/main.c | sed '$d' > $2/touch.inc
    [[ $(wc -l < $2/defs.inc) -ge 8 && $(wc -l < $2/cal.inc) -ge 8 && $(wc -l < $2/touch.inc) -ge 100 ]] &&
        grep -q '^static void poll_touch(void) {$' $2/touch.inc && grep -q '^static bool hold_due' $2/touch.inc
}
build() {   # $1 the folder of main.c and perf.h, $2 the program
    cut $1 $1/inc 2> /dev/null || { print "the cut of $1/main.c went wrong"; return 1 }
    clang $F -I$1/inc -I../test4/stub -I$1 -I$V hold_check.c $V/perf.c $V/knobctl.c $V/fm1scr.c $V/guest.c $V/cal.c $V/lcd.c \
        $V/tone.c ../test4/stub.c -lm -o $2
}
mkdir -p $O/v/inc
cp $V/main.c $V/perf.h $O/v/
build $O/v $O/hold 2> $O/err || { cat $O/err; print "FAILED: hold_check doesn't build"; exit 1 }
[[ -s $O/err ]] && { cat $O/err; print "FAILED: hold_check builds with warnings"; fail=1 }
$O/hold > $O/out 2>&1; rc=$?
tail -3 $O/out
(( rc == 0 )) && grep -q '^hold: [0-9]* checks, 0 failed$' $O/out || { cat $O/out; print "FAILED: the hold's checks"; fail=1 }

# deliberate breaks: each must change its file, build, and be caught
BN=() BF=() BS=()
brk() { BN+=($1) BF+=($2) BS+=($3) }
brk "a drag after 10 px, as test 17"            perf.h 's|^#define LAND_TRAVEL_PX 20$|#define LAND_TRAVEL_PX 10|'
brk "a drag after 21 px"                        perf.h 's|^#define LAND_TRAVEL_PX 20$|#define LAND_TRAVEL_PX 21|'
brk "the hold not acted on with the finger down" main.c 's|^            if (hold_due(now)) {|            if (0 \&\& hold_due(now)) {|'
brk "the hold at one second"                    main.c 's|now - touch_first_ms < HOLD_MS \|\| touch_y|now - touch_first_ms < HOLD_MS / 2 \|\| touch_y|'
brk "the hold at three seconds"                 main.c 's|now - touch_first_ms < HOLD_MS \|\| touch_y|now - touch_first_ms < HOLD_MS * 3 / 2 \|\| touch_y|'
brk "the touch not spent"                       main.c 's|^                touch_spent = true;$|                touch_spent = false;|'
brk "the spent finger read again"               main.c 's|^            if (touch_spent) {   // (test 18) the finger that held|            if (0) {   // (test 18) the finger that held|'
brk "the spent finger free at once on a gap"    main.c 's|^        if (now - touch_spent_ms >= 60) touch_spent = false;$|        touch_spent = false;|'
brk "the spent finger never free"               main.c 's|^        if (now - touch_spent_ms >= 60) touch_spent = false;$|        ;|'
brk "a drag can be a hold"                      main.c 's|^    if (screen == SCR_LAND) return land_touching \&\& !pf.l_drag;$|    if (screen == SCR_LAND) return land_touching;|'
brk "a knob can be a hold"                      main.c 's|^    return guest_page(&gu) \|\| !(kc.in_row.*$|    return true;|'
brk "a locked knob can not"                     main.c 's|^    return guest_page(&gu) \|\| !(kc.in_row.*$|    return guest_page(\&gu) \|\| !kc.in_row;|'
brk "the knob row of a guest can not"          main.c 's|^    return guest_page(&gu) \|\| !(kc.in_row|    return !(kc.in_row|'
brk "the status row can be held"                main.c 's/ || touch_y >= STATUS_Y || pressed_cell >= 0) return false;$/ || pressed_cell >= 0) return false;/'
brk "the effects page can be held"              main.c 's|^    if (screen != SCR_FM1 \|\| land_touching \|\| perf_touching) return false;$|    if (screen == SCR_CAL \|\| screen == SCR_CARD) return false;|'
caught=0
for i in {1..$#BN}; do
    B=$O/b$i; mkdir -p $B/inc
    cp $V/main.c $V/perf.h $B/
    sed -i '' -e $BS[i] $B/$BF[i]
    if cmp -s $B/$BF[i] $V/$BF[i]; then print "FAILED: break '$BN[i]' changed nothing"; fail=1; continue; fi
    if ! build $B $B/hold 2> $B/err; then print "FAILED: break '$BN[i]' doesn't build"; head -5 $B/err; fail=1; continue; fi
    if $B/hold > $B/out 2>&1; then print "FAILED: break '$BN[i]' not caught"; fail=1
    else caught=$((caught + 1)); print "  caught: $BN[i] ($(grep -c '^FAIL' $B/out) checks fail)"; fi
done
print "hold: breaks caught: $caught of $#BN"
(( fail == 0 && caught == $#BN )) && print "hold: all passed" || { print "hold: FAILED"; exit 1 }
