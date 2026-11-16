#!/bin/zsh
# (test 18) The earlier tests' checks, against test 18's folder: test 17's run.sh, and inside it test 16's, 15's, 13's,
# 12's, 11's and 7's, as test 17's run.sh ran test 16's against its own.
#   zsh earlier.sh [--quick]     --quick: test 12's checks without test 11's whole run.sh
#   zsh earlier.sh --tree        make the copy described below, say where it is, and run nothing
#
# The scripts find each other by folder, so they are run from a copy of hosttests/ that is links to this one, with
# the files below written in. What is changed in them, and nothing else:
#   test17/run.sh       the names say 18; without its comparison of the files with test 16's (run.sh compares them
#                       with test 17's); its break of LOOK's place starts from 280, where LOOK now is; and cal.c has
#                       now lost 20 of test 11's lines, not 6 (the buttons' places and widths)
#   test17/look_check.c LOOK and CARD looked for where they now are (280 to 371, 372 to 463), LAND at x 460, and the
#                       look's name (which the two looks letter differently) at x 186
#   test16/run.sh       strip.h takes its width from ui.h, so its break of the width starts from UI_W, and the
#                       broken copies are built with ui.h beside them
#   test15/run.sh       card.c, fm1scr.c, guest.c, knobctl.h and perf.h left out of its comparison with test 14's
#   test13/run.sh       main.c asks may_send() at six touches, not five (the sixth: the hold letting the
#                       landscape's touch go, hold_check.c's)
#                       and its break of the guest's row width starts from the width as now written (25, from UI_W)
#   test12/run.sh       fm1scr.c, knobctl.h and perf.h left out of its comparison with test 11's
#   test12/check12.c    the card's three buttons looked for at their new places (154 or 155 wide, ending at x 463)
#   test11/run.sh       knobctl.h left out of its comparison with test 7's, and fm1scr.c's count of changed lines
#   test7/fm1scr_check.c  a knob's cell 116 wide, not 120 (KC_X, the bars' places, the cells' ends)
#   test7/run.sh        three of its breaks of fm1scr.c start from lines that now say PAGE_W where they said LCD_W
#   test4/test.c        (run by test 7's run.sh) calibration's two lines 77 letters, not 80; its buttons looked for
#                       at x 91 and 463, not 95 and 479; and x 600 is no button's, where it was the last column's
# Each change is checked to have been made. Everything else in those scripts runs as it was written.
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
    [[ ${d:t} == test18 ]] && continue
    mkdir $M/hosttests/${d:t}
    for f in $d/*; do ln -s $f $M/hosttests/${d:t}/${f:t}; done
done
T=$M/hosttests
moved() { print -r -- "FAIL: $1 has moved"; exit 1 }
put() {   # $1 the file (test17/run.sh), the rest: sed's arguments. Written in place of the link.
    local f=$1
    shift
    rm $T/$f
    sed "$@" $H/$f > $T/$f
}
lines() { diff $H/$1 $T/$1 | grep -c "^$2" || true }

put test17/run.sh \
    -e '/^print -r -- "-- test 17 is test 16 and the look"$/,/^print -r -- "the same: test 16.s files and look.h; the seven lose /d' \
    -e 's|viz-test17|viz-test18|g; s/viz_test17/viz_test18/g; s/AISLING VIZ TEST 17/AISLING VIZ TEST 18/g; s/"AISLING viz test 17"/"AISLING viz test 18"/g; s/title says test 17/title says test 18/' \
    -e 's|^for d in ${HERE:h:h}/viz-test\*(/); do ln -s|for d in ${HERE:h:h}/viz-test*(-/); do ln -s|' \
    -e "s|'s/^#define LOOK_X  288 /#define LOOK_X  336 /'|'s/^#define LOOK_X  280 /#define LOOK_X  328 /'|" \
    -e "s/grep -c '^<') == 6 \]\] ||/grep -c '^<') == 20 ]] ||/g"
grep -q '^V=../../viz-test18$' $T/test17/run.sh && ! grep -q 'cmp -s $O/changes changes.txt' $T/test17/run.sh &&
    ! grep -q 'viz.test17' $T/test17/run.sh && grep -q '^print -r -- "-- the rule"$' $T/test17/run.sh &&
    grep -q 'viz-test\*(-/); do ln -s' $T/test17/run.sh && grep -q "LOOK_X  280 /#define LOOK_X  328 /" $T/test17/run.sh &&
    [[ $(grep -c "grep -c '^<') == 20 ]] ||" $T/test17/run.sh) == 2 ]] && ! grep -q "== 6 " $T/test17/run.sh || moved "test 17's run.sh"

put test17/look_check.c \
    -e 's/if (x >= 190 \&\& x < 286 \&\& y >= 232 \&\& y < 244) continue;/if (x >= 186 \&\& x < 282 \&\& y >= 232 \&\& y < 244) continue;/' \
    -e 's/cal_button_at(383, 247) == CAL_LOOK \&\& cal_button_at(288, 212) == CAL_LOOK,/cal_button_at(371, 247) == CAL_LOOK \&\& cal_button_at(280, 212) == CAL_LOOK,/' \
    -e 's/CHECK(cal_button_at(287, 220) != CAL_LOOK /CHECK(cal_button_at(279, 220) != CAL_LOOK /' \
    -e 's/CHECK(cal_button_at(384, 220) == CAL_CARD \&\& cal_button_at(479, 230) == CAL_CARD,/CHECK(cal_button_at(372, 220) == CAL_CARD \&\& cal_button_at(463, 230) == CAL_CARD,/' \
    -e 's/cal_button_at(470, 310) == CAL_LAND,/cal_button_at(460, 310) == CAL_LAND,/'
[[ $(lines test17/look_check.c '>') == 5 && $(lines test17/look_check.c '<') == 5 ]] || moved "test 17's look_check.c"

put test16/run.sh \
    -e "s|'s/^#define STRIP_W 464 /#define STRIP_W 480 /'|'s/^#define STRIP_W UI_W /#define STRIP_W 480 /'|" \
    -e 's|^    cp $V/strip.h $V/main.c $O/b/$|    cp $V/ui.h $V/strip.h $V/main.c $O/b/|'
[[ $(lines test16/run.sh '>') == 2 && $(lines test16/run.sh '<') == 2 ]] || moved "test 16's run.sh"

put test15/run.sh -e '/^        README.md|lcd.c) ;;$/i\
        card.c|fm1scr.c|guest.c|knobctl.h|perf.h) ;;   # (test 18) laid out again: compared with test 17'"'"'s
'
[[ $(lines test15/run.sh '>') == 1 && $(lines test15/run.sh '<') == 0 ]] || moved "test 15's run.sh"

put test13/run.sh -e 's/^    \[\[ \$n == 5 \]\] || print -r -- "FAIL: main.c: \$n of 5 touches ask may_send()"$/    [[ $n == 6 ]] || print -r -- "FAIL: main.c: $n of 6 touches ask may_send()"/' \
    -e "s|'s/^#define BIG_W    26 /#define BIG_W    27 /'|'s/^#define BIG_W    ((UI_W - 2 . MARGIN) . 18) /#define BIG_W    27 /'|"
[[ $(lines test13/run.sh '>') == 2 && $(lines test13/run.sh '<') == 2 ]] || moved "test 13's run.sh"

put test12/run.sh -e '/^    \[\[ \$n == (cal.c|cal.h|main.c|tusb_config.h|usb_descriptors.c) \]\] && continue$/i\
    [[ $n == (fm1scr.c|knobctl.h|perf.h) ]] \&\& continue   # (test 18) laid out again: compared with test 17'"'"'s
'
[[ $(lines test12/run.sh '>') == 1 && $(lines test12/run.sh '<') == 0 ]] || moved "test 12's run.sh"
put test12/check12.c \
    -e 's/card_button_at(0, 264) == CARD_BTN_MAIN \&\& card_button_at(159, 319) == CARD_BTN_MAIN \&\&$/card_button_at(0, 264) == CARD_BTN_MAIN \&\& card_button_at(153, 319) == CARD_BTN_MAIN \&\&/' \
    -e 's/^       card_button_at(160, 300) == CARD_BTN_SAVE \&\& card_button_at(319, 300) == CARD_BTN_SAVE \&\&$/       card_button_at(154, 300) == CARD_BTN_SAVE \&\& card_button_at(308, 300) == CARD_BTN_SAVE \&\&/' \
    -e 's/^       card_button_at(320, 300) == CARD_BTN_BACK \&\& card_button_at(479, 319) == CARD_BTN_BACK \&\&$/       card_button_at(309, 300) == CARD_BTN_BACK \&\& card_button_at(463, 319) == CARD_BTN_BACK \&\&/'
[[ $(lines test12/check12.c '>') == 3 && $(lines test12/check12.c '<') == 3 ]] || moved "test 12's check12.c"

put test11/run.sh \
    -e 's/^for f in knobctl.c knobctl.h pflash.h land.c land.h tone.c tone.h reader.c reader.h \\$/for f in knobctl.c pflash.h land.c land.h tone.c tone.h reader.c reader.h \\/' \
    -e "s/grep -c '^<') == 2 \&\& \$(diff \$V7\/fm1scr.c \$V\/fm1scr.c | grep -c '^>') == 5 \]\]/grep -c '^<') == 40 \&\& \$(diff \$V7\/fm1scr.c \$V\/fm1scr.c | grep -c '^>') == 55 ]]/"
[[ $(lines test11/run.sh '>') == 2 && $(lines test11/run.sh '<') == 2 ]] && grep -q '^         cal.h font5x7.h; do$' $T/test11/run.sh || moved "test 11's run.sh"

put test7/fm1scr_check.c \
    -e '/^#define KC_X(i) (120 \* (i))$/,/isn.t %d px of orange/{s/120/116/g; s/x + 6/x + 4/g; s/, 6, KBAR_H, BG) == 6 \*/, 4, KBAR_H, BG) == 4 */; s/116 - 6 - KBAR_W/116 - 4 - KBAR_W/g;}' \
    -e '/"a bar under knob %d", i);$/s/KC_X(i), KBAR_Y, 120, KBAR_H/KC_X(i), KBAR_Y, 116, KBAR_H/' \
    -e 's/CHECK(px_count(fb_inc, KC_X(1) + 6, KBAR_Y, KBAR_W, KBAR_H, C_ORANGE)/CHECK(px_count(fb_inc, KC_X(1) + 4, KBAR_Y, KBAR_W, KBAR_H, C_ORANGE)/' \
    -e 's/px_count(fb_inc, KC_X(i), 80, 120, 22, /px_count(fb_inc, KC_X(i), 80, 116, 22, /g'
[[ $(lines test7/fm1scr_check.c '>') == 10 && $(lines test7/fm1scr_check.c '<') == 10 ]] && ! grep -q 'KC_X(i), [0-9A-Z_]*, 120' $T/test7/fm1scr_check.c ||
    moved "test 7's fm1scr_check.c"

put test7/run.sh \
    -e "s|^    's/fill(0, 140, LCD_W, 2, RULE);//'\$|    's/fill(0, 140, PAGE_W, 2, RULE);//'|" \
    -e "s|^    's/box_text(0, 0, LCD_W, FM1SCR_BOTTOM, 124/box_text(0, 0, LCD_W, FM1SCR_BOTTOM + 1, 124/'\$|    's/box_text(0, 0, PAGE_W, FM1SCR_BOTTOM, 124/box_text(0, 0, PAGE_W, FM1SCR_BOTTOM + 1, 124/'|" \
    -e "s|^    's/^    box_text(0, mid, LCD_W, FM1SCR_BOTTOM - mid, 0, 16, t, PF_LINE, AL_C, C_BLACK, c);\$//'\$|    's/^    box_text(0, mid, PAGE_W, FM1SCR_BOTTOM - mid, 0, 16, t, PF_LINE, AL_C, C_BLACK, c);\$//'|"
[[ $(lines test7/run.sh '>') == 3 && $(lines test7/run.sh '<') == 3 ]] || moved "test 7's run.sh"

put test4/test.c \
    -e 's/CHECK((int)strlen(last_info\[0\]) == 80 \&\& (int)strlen(last_info\[1\]) == 80, "both info lines are 80 characters/CHECK((int)strlen(last_info[0]) == 77 \&\& (int)strlen(last_info[1]) == 77, "both info lines are 77 characters/' \
    -e 's/{0, 256, 0}, {95, 256, 0}, {96, 256, 1}, {479, 287, 4},$/{0, 256, 0}, {91, 256, 0}, {96, 256, 1}, {463, 287, 4},/' \
    -e 's/{479, 319, 9}, {479, 400, 9}, {600, 260, 4}, {240, 0, -1}};$/{463, 319, 9}, {463, 400, 9}, {600, 260, -1}, {240, 0, -1}};/' \
    -e 's/last_info\[1\]\[79\] == . ., "a long settings line fits in 80:/last_info[1][76] == '"' '"', "a long settings line fits in 77:/'
[[ $(lines test4/test.c '>') == 4 && $(lines test4/test.c '<') == 4 ]] || moved "test 4's test.c"

[[ $1 == --tree ]] && { print -r -- $M; exit 0 }   # run.sh: only the copy, to build test 7's changed check from

print -r -- "-- test 17's run.sh, against test 18${quick:+ (quick)}"
zsh $T/test17/run.sh $quick > $O/run17.out 2>&1 || { grep FAIL $O/run17.out | head; tail -5 $O/run17.out; print -r -- "FAIL: test 17's checks (the whole of what they said: $O/run17.out)"; exit 1 }
grep -E "^look:|^calibration|pictures in both|main.c read|breaks caught|^ *strip:|all passed" $O/run17.out | sed 's/^ */   /'
[[ $(grep -c "^ *FAIL" $O/run17.out) == 0 ]] && grep -q "^test 17: all passed$" $O/run17.out && grep -q "^ *test 16: all passed$" $O/run17.out &&
    grep -q "^ *test 15: all passed$" $O/run17.out || { print -r -- "FAIL: in test 17's checks"; exit 1 }
[[ $(grep -c "^breaks caught: 23 of 23$" $O/run17.out) == 1 ]] && grep -q "breaks caught: 11 of 11" $O/run17.out &&
    grep -q "breaks caught: 8 of 8" $O/run17.out && grep -q "breaks caught: 47 of 47" $O/run17.out ||
    { print -r -- "FAIL: not all of the earlier breaks caught"; exit 1 }
print -r -- "earlier: all passed"
