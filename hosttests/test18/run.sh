#!/bin/zsh
# Test 18's host checks (test 17 with the box's own pages kept clear of the screen's last 16 pixels), on the Mac.
#   zsh run.sh [--quick] [DIR]   --quick: test 12's checks without test 11's whole run.sh (some six minutes less)
#                                DIR: the pictures of the pages in both looks, as PNG, into it
#
#   test 18 is test 17, the usable width and the hold, and nothing else: every file but nine is test 17's byte for
#     byte (titles and the USB name say 18), ui.h is the one new file, and the nine (cal.c, card.c, fm1scr.c, guest.c,
#     knobctl.h, main.c, perf.c, perf.h, strip.h) differ from test 17's by the lines in changes.txt and no others
#     (main.c: its code lines)
#   ui_check.c     pages: every page the box draws itself, through lcd.c, in both looks: right of x 464 the look's
#                  ground and nothing else; and where a finger finds a button, a cell, a knob or a piece
#                  frames: the pictures test 11's, 13's and 7's checks make of the effects, the pad, a guest's page,
#                  the FM-1's page and three flashes (31 states), made in both looks: the same there
#   touch_check.c  main.c's own poll_touch: a touch that begins right of x 464 on one of the box's pages does
#                  nothing, however it is dragged or held, and its lift does nothing; the landscape's picture still
#                  takes one there
#   hold.sh        the two-second hold (hold_check.c) and its own breaks
#   deliberate breaks of ui.h's use in cal.c, card.c, perf.c, perf.h, fm1scr.c, guest.c and main.c, each to be caught
#   earlier.sh     test 17's run.sh against test 18's folder, and inside it test 16's, 15's, 13's, 12's (and 11's
#                  and 7's): what is changed in them to run so is listed at its head
set -e
HERE=${0:A:h}
cd $HERE
V=../../viz-test18
V17=../../viz-test17
O=$(mktemp -d)
F=(-std=gnu11 -g -O1 -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined -fno-sanitize-recover=undefined)
FM=${FM:-$HOME/Projects/fm1-aisling}   # announce.c, for the FM-1's page (as test 7's run.sh)
quick=
shots=
for a in "$@"; do
    if [[ $a == --quick ]]; then quick=--quick; else shots=$a; fi
done
CHANGED=(cal.c card.c fm1scr.c guest.c knobctl.h main.c perf.c perf.h strip.h)

print -r -- "-- test 18 is test 17, the usable width and the hold"
for f in $V17/*(.); do
    n=${f:t}
    case $n in
        README.md|cal.c|card.c|fm1scr.c|guest.c|knobctl.h|main.c|perf.c|perf.h|strip.h) ;;
        usb_descriptors.c) sed 's/"AISLING viz test 17"/"AISLING viz test 18"/' $f | cmp -s - $V/$n || { print -r -- "FAIL: $n is not test 17's but for the name"; exit 1 } ;;
        CMakeLists.txt) sed 's/viz_test17/viz_test18/g' $f | cmp -s - $V/$n || { print -r -- "FAIL: CMakeLists.txt is not test 17's but for the name"; exit 1 } ;;
        *) cmp -s $f $V/$n || { print -r -- "FAIL: $n is not test 17's"; exit 1 } ;;
    esac
done
[[ -f $V/ui.h && $(ls $V/*(.) | wc -l) -eq $(($(ls $V17/*(.) | wc -l) + 1)) ]] || { print -r -- "FAIL: test 18 is not test 17's files and ui.h"; exit 1 }
# The nine: whole lines, but main.c's code lines only (the comments at its head are its own)
changes() {
    local n
    for n in $CHANGED; do
        print -r -- "== $n"
        case $n in
            main.c) diff <(grep -v '^ *//' $V17/$n | sed 's/---- AISLING viz test 17 ----/---- AISLING viz test 18 ----/') <(grep -v '^ *//' $V/$n) | grep '^[<>]' || true ;;
            cal.c|card.c) diff <(sed 's/AISLING VIZ TEST 17/AISLING VIZ TEST 18/' $V17/$n) $V/$n | grep '^[<>]' || true ;;
            *) diff $V17/$n $V/$n | grep '^[<>]' || true ;;
        esac
    done
}
changes > $O/changes
cmp -s $O/changes changes.txt || { diff $O/changes changes.txt | head -20; print -r -- "FAIL: the nine files' changes from test 17's are not changes.txt's (as they are now: $O/changes)"; exit 1 }
print -r -- "the same: test 17's files and ui.h; the nine lose $(grep -c '^<' $O/changes) lines of test 17's and have $(grep -c '^>' $O/changes) new"

# The earlier tests' checks as changed to run against this folder (earlier.sh): test 7's check of the FM-1's page,
# with the knobs 116 wide, is what makes that page's pictures here
M=$(zsh earlier.sh --tree)
[[ -f $M/hosttests/test7/fm1scr_check.c ]] || { print -r -- "$M"; print -r -- "FAIL: earlier.sh's copy of the earlier checks"; exit 1 }

# ---- the programs. $1 is always the folder the box's files are taken from (a copy of $V, or a broken one)
uip() {   # ui_check's pages. strip.inc: the pieces and draw_strip(); status.inc: the status line's lettering
    mkdir -p $2.d
    { sed -n '/^static const struct { const char \*name; uint8_t bpm; } PIECES\[8\] = {$/,/^};$/p' $1/main.c
      sed -n '/^static void draw_strip(void) {$/,/^}$/p' $1/main.c } > $2.d/strip.inc
    sed -n '/^    int room = /,/^    lcd_text(48, STATUS_Y + 2, b, 1, C_GREY, C_BLACK);$/p' $1/main.c > $2.d/status.inc
    grep -q '^static void draw_strip' $2.d/strip.inc && grep -q 'PIECES\[8\]' $2.d/strip.inc &&
        [[ $(wc -l < $2.d/status.inc) -ge 5 && $(wc -l < $2.d/status.inc) -le 40 ]] || { print -r -- "FAIL: main.c's draw_strip or status line has moved"; exit 1 }
    clang $F -DMODE_PAGES -I$2.d -I../test12/stub -I../test4/stub -I$1 ui_check.c $1/cal.c $1/card.c $1/perf.c $1/knobctl.c \
        $1/fm1scr.c $1/guest.c $1/lcd.c $1/tone.c $1/reader.c ../test4/stub.c -lm -o $2
}
tch() {   # touch_check: main.c's own touch (cut as hold.sh cuts it)
    mkdir -p $2.d
    { grep -E '^#define (STATUS_Y|STRIP_Y) ' $1/main.c
      sed -n '/^enum { SCR_LAND, /,/^static uint32_t touch_last_ms, touch_first_ms;$/p' $1/main.c } > $2.d/defs.inc
    sed -n '/^static void show_cal(void) {$/,/^}$/p' $1/main.c > $2.d/cal.inc
    sed -n '/^static bool land_touching; /,/^static void draw_strip(void) {$/p' $1/main.c | sed '$d' > $2.d/touch.inc
    [[ $(wc -l < $2.d/defs.inc) -ge 8 && $(wc -l < $2.d/cal.inc) -ge 8 && $(wc -l < $2.d/touch.inc) -ge 100 ]] &&
        grep -q '^static void poll_touch(void) {$' $2.d/touch.inc || { print -r -- "FAIL: main.c's touch has moved"; exit 1 }
    clang $F -Wno-unused-function -Wno-unused-variable -I$2.d -I../test4/stub -I$1 touch_check.c $1/perf.c $1/knobctl.c \
        $1/fm1scr.c $1/guest.c $1/cal.c $1/lcd.c $1/tone.c ../test4/stub.c -lm -o $2
}
# The other tests' picture programs, in both looks (test 17's lookset.c makes them draw light): $2/dark and $2/light
pics() {
    local l LOOKSET
    sed -n '/^static void send_piece(int i) {/,/^static bool land_touching;/p' $1/main.c | sed '$d' > $1/block.inc
    grep -q '^static void send_piece' $1/block.inc || { print -r -- "FAIL: main.c's sending block has moved"; exit 1 }
    for l in dark light; do
        mkdir -p $2/$l
        [[ $l == light ]] && LOOKSET=(../test17/lookset.c) || LOOKSET=()
        clang $F -I../test4/stub -I$1 ../test11/perf_check.c $1/perf.c $1/knobctl.c $1/fm1scr.c $1/lcd.c $1/tone.c \
            $1/reader.c ../test4/stub.c $LOOKSET -lm -o $2/perf.$l
        clang $F -DMAIN_BLOCK="\"$1/block.inc\"" -I../test4/stub -I$1 ../test13/guest_check.c $1/guest.c $1/perf.c \
            $1/knobctl.c $1/fm1scr.c $1/lcd.c $1/tone.c $1/reader.c ../test4/stub.c $LOOKSET -lm -o $2/guest.$l
        clang $F -I../test4/stub -I$1 -DFM1SCR_C="\"$1/fm1scr.c\"" -DANNOUNCE_C="\"$FM/firmware/src/announce.c\"" \
            -DANN_NAME='"AISLING 1.3"' -DCORPUS_LINES=8997 $M/hosttests/test7/fm1scr_check.c $1/lcd.c $1/tone.c $1/reader.c \
            ../test4/stub.c $LOOKSET -lm -o $2/fm1.$l
        # (what they check themselves is earlier.sh's; here only their pictures are wanted)
        $2/perf.$l shots $2/$l > $2/perf.$l.out 2>&1 || true
        $2/guest.$l shots $2/$l > $2/guest.$l.out 2>&1 || true
        $2/fm1.$l ../test11/corpus.txt --quick --shots $2/$l > $2/fm1.$l.out 2>&1 || true
    done
}
frames() {   # $1 the folder pics() filled: its pictures through ui_check, in both looks
    local l n
    for l in dark light; do
        n=($1/$l/*.ppm(N:t:r))
        [[ $#n == 31 ]] || { print -r -- "FAIL: $#n pictures in the $l look, not 31"; return 1 }
        $O/frames $l $1/$l $n > $1/frames.$l.out 2>&1 || return 1
        grep -q "^ui: [1-9][0-9]* checks, 0 failed$" $1/frames.$l.out || return 1
    done
}
ran() { grep -q "^$2: [1-9][0-9]* checks, 0 failed$" $1 }

mkdir -p $O/src
cp $V/*.c $V/*.h $O/src/
print -r -- "-- the pages, in both looks"
uip $O/src $O/ui
mkdir -p $O/pages
$O/ui $O/pages > $O/ui.out || { cat $O/ui.out; print -r -- "FAIL: the pages"; exit 1 }
grep -E "^pages:|^touch:|^ui:" $O/ui.out
ran $O/ui.out ui || { print -r -- "FAIL: the pages' checks didn't run"; exit 1 }
clang $F -DMODE_FRAMES -I../test4/stub -I$V ui_check.c -lm -o $O/frames
pics $O/src $O/p
[[ $(cat $O/p/perf.dark.out $O/p/guest.dark.out $O/p/fm1.dark.out | grep -c FAIL) == 0 ]] ||
    { print -r -- "FAIL: the other tests' picture programs fail in the dark look"; exit 1 }
frames $O/p || { cat $O/p/frames.*.out | tail -20; print -r -- "FAIL: a page of the other tests' has something right of x 464"; exit 1 }
grep -h "^frames:" $O/p/frames.dark.out $O/p/frames.light.out
if [[ -n $shots ]]; then
    mkdir -p $shots
    for p in $O/pages/*.ppm; do sips -s format png $p --out $shots/${p:t:r}.png > /dev/null; done
    for l in dark light; do
        for p in $O/p/$l/*.ppm; do sips -s format png $p --out $shots/${p:t:r}-$l.png > /dev/null; done
    done
    print -r -- "pictures in $shots"
fi

print -r -- "-- a touch right of x 464"
tch $O/src $O/touch
$O/touch > $O/touch.out || { tail -20 $O/touch.out; print -r -- "FAIL: the touch"; exit 1 }
grep "^touch:" $O/touch.out
ran $O/touch.out touch || { print -r -- "FAIL: the touch's checks didn't run"; exit 1 }

print -r -- "-- the two-second hold"
zsh hold.sh > $O/hold.out 2>&1 || { tail -20 $O/hold.out; print -r -- "FAIL: the hold"; exit 1 }
grep -E "^hold: [0-9]+ checks|breaks caught|all passed" $O/hold.out
grep -q "^hold: all passed$" $O/hold.out || { print -r -- "FAIL: the hold's checks didn't run"; exit 1 }

print -r -- "-- deliberate breaks, each to be caught"
# each: its name, its file, sed's change, and what must catch it (P the pages, T the touch, R the other tests' pictures)
BN=() BF=() BS=() BC=()
brk() { BN+=($1) BF+=($2) BC+=($3) BS+=($4) }
brk "cal.c: the buttons' five columns across the screen, as test 17" cal.c P 's/^#define BTN_X(c) ui_col(c, 5) /#define BTN_X(c) ((c) * 96) /'
brk "cal.c: CARD to the screen's edge" cal.c P 's/^#define CARD_W  (UI_W - CARD_X)$/#define CARD_W  (LCD_W - CARD_X)/'
brk "cal.c: the ramps across the screen" cal.c P 's/^static int x64(int i) { return ui_col(i, 64); }$/static int x64(int i) { return i * LCD_W \/ 64; }/'
brk "cal.c: the header's rate at the screen's edge" cal.c P 's/PANEL CALIBRATION%\*s", UI_W \/ 6 - 38, s);$/PANEL CALIBRATION%*s", LCD_W \/ 6 - 38, s);/'
brk "cal.c: a button found under the brick" cal.c P 's/^    if (ui_off(x)) return -1;   /    if (x >= LCD_W) return -1;   /'
brk "card.c: the three buttons across the screen, as test 17" card.c P 's/^#define BTN_X(b) ui_col(b, CARD_BUTTONS) /#define BTN_X(b) ((b) * 160) /'
brk "card.c: its lines as long as the screen" card.c P 's/^    int room = UI_W \/ (6 \* scale), k = 0;$/    int room = LCD_W \/ (6 * scale), k = 0;/'
brk "card.c: a button found under the brick" card.c P 's/^    if (y < BTN_Y || x < 0 || ui_off(x)) return -1;   /    if (y < BTN_Y || x < 0 || x >= LCD_W) return -1;   /'
brk "perf.h: the pad's buttons at the screen's edge, as test 17" perf.h PR 's/^#define XY_PAD_W (UI_W - XY_BTN_W) /#define XY_PAD_W (LCD_W - XY_BTN_W) /'
brk "perf.c: a pad's button found under the brick" perf.c T 's/^    if (x < XY_PAD_W || ui_off(x) || y < 0/    if (x < XY_PAD_W || x >= LCD_W || y < 0/'
brk "perf.c: an effect's cell two pixels wider" perf.c PR 's/w = FX_X(c % FX_COLS + 1) - x - 1, h = FX_CH - 2;$/w = FX_X(c % FX_COLS + 1) - x + 1, h = FX_CH - 2;/'
brk "fm1scr.c: the page across the screen, as test 17" fm1scr.c PR 's/^#define PAGE_W UI_W$/#define PAGE_W LCD_W/'
brk "fm1scr.c: the page's margin not cleared" fm1scr.c P '/^        fill(PAGE_W, 0, LCD_W - PAGE_W, FM1SCR_BOTTOM, BG);   \/\/ (test 18) under the brick: nothing$/d'
brk "fm1scr.c: a flash's name centred in the screen" fm1scr.c P 's/^    box_text(0, 0, PAGE_W, mid, top, 16, t, sc, AL_C, C_BLACK, c);$/    box_text(0, 0, LCD_W, mid, top, 16, t, sc, AL_C, C_BLACK, c);/'
brk "fm1scr.c: a flash's colour stopping at x 464" fm1scr.c P '/^    fill(PAGE_W, 0, LCD_W - PAGE_W, FM1SCR_BOTTOM, c);   /d'
brk "guest.c: the head's right at the screen's edge" guest.c PR 's/^    lcd_text(UI_W - MARGIN - (int)strlen(r) \* 6, HEAD_Y + 6, r, 1, DIM, BG);/    lcd_text(LCD_W - MARGIN - (int)strlen(r) * 6, HEAD_Y + 6, r, 1, DIM, BG);/'
brk "guest.c: the rules across the screen" guest.c PR 's/^    lcd_fill_rect(0, RULE1_Y, UI_W, 1, RULE);$/    lcd_fill_rect(0, RULE1_Y, LCD_W, 1, RULE);/; /^    lcd_fill_rect(UI_W, RULE1_Y, LCD_W - UI_W, 1, BG);/d'
brk "guest.c: the text's rows as long as the screen's, as test 17" guest.c P 's/^#define BIG_W    ((UI_W - 2 \* MARGIN) \/ 18) /#define BIG_W    26 /'
brk "main.c: the status line as long as the screen" main.c P 's/^    int room = UI_W \/ 6 - 8; /    int room = LCD_W \/ 6 - 8; /'
brk "main.c: a guest's strip line not cut" main.c P '/^        ga\[UI_W \/ 6\] = gb\[UI_W \/ 6\] = 0;   /d'
brk "main.c: a guest's strip lines centred in the screen" main.c P 's/^        lcd_text((UI_W - (int)strlen(ga) \* 6) \/ 2, STRIP_Y + 8, ga,/        lcd_text((LCD_W - (int)strlen(ga) * 6) \/ 2, STRIP_Y + 8, ga,/'
brk "main.c: a guest's strip ground to the screen's edge" main.c P 's/^        lcd_fill_rect(0, STRIP_Y, UI_W, STRIP_H, bg);$/        lcd_fill_rect(0, STRIP_Y, LCD_W, STRIP_H, bg);/; /^        lcd_fill_rect(UI_W, STRIP_Y, LCD_W - UI_W, STRIP_H, C_BLACK);   /d'
brk "main.c: a touch under the brick is anybody's" main.c T 's/^    return ui_off(x) \&\& !(screen == SCR_LAND \&\& y < STATUS_Y);$/    return false;/'
brk "main.c: the landscape's picture not touched there either" main.c T 's/^    return ui_off(x) \&\& !(screen == SCR_LAND \&\& y < STATUS_Y);$/    return ui_off(x);/'
brk "main.c: the landscape's two rows touched there" main.c T 's/^    return ui_off(x) \&\& !(screen == SCR_LAND \&\& y < STATUS_Y);$/    return ui_off(x) \&\& screen != SCR_LAND;/'
brk "main.c: nobody's touch read once it is dragged on to the page" main.c T 's/^            if (touch_off) {$/            if (touch_off \&\& ui_off(x)) {/'
brk "main.c: nobody's touch acted on when it lifts" main.c T 's/^        if (touch_off) { touch_off = false; return; }$/        touch_off = false;/'
brk "main.c: a touch that slides under the brick becomes nobody's" main.c T 's/touch_first_ms = now; touch_off = touch_begins_off(x, y); }$/touch_first_ms = now; } touch_off = touch_begins_off(x, y);/'
caught=0
missed=0
for i in {1..$#BN}; do
    B=$O/b$i
    mkdir -p $B
    cp $V/*.c $V/*.h $B/
    sed -i '' -e $BS[i] $B/$BF[i]
    cmp -s $B/$BF[i] $V/$BF[i] && { print -r -- "FAIL: break $i didn't apply: $BN[i]"; exit 1 }
    by=
    if [[ $BC[i] == *P* ]]; then
        uip $B $B/ui 2> $B/ui.err || { head -5 $B/ui.err; print -r -- "FAIL: break $i doesn't build: $BN[i]"; exit 1 }
        mkdir -p $B/pages
        if $B/ui $B/pages > $B/ui.out 2>&1 && ran $B/ui.out ui; then
            print -r -- "FAIL: the pages don't catch break $i: $BN[i]"; missed=1; continue
        fi
        by="the pages: $(grep -c '^FAIL' $B/ui.out), first: $(grep -m1 '^FAIL' $B/ui.out | cut -c1-110)"
    fi
    if [[ $BC[i] == *T* ]]; then
        tch $B $B/touch 2> $B/touch.err || { head -5 $B/touch.err; print -r -- "FAIL: break $i doesn't build: $BN[i]"; exit 1 }
        if $B/touch > $B/touch.out 2>&1 && ran $B/touch.out touch; then
            print -r -- "FAIL: the touch doesn't catch break $i: $BN[i]"; missed=1; continue
        fi
        by="the touch: $(grep -c '^FAIL' $B/touch.out), first: $(grep -m1 '^FAIL' $B/touch.out | cut -c1-110)"
    fi
    if [[ $BC[i] == *R* ]]; then
        pics $B $B/p 2> $B/p.err || { head -5 $B/p.err; print -r -- "FAIL: break $i doesn't build: $BN[i]"; exit 1 }
        if frames $B/p; then print -r -- "FAIL: the other tests' pictures don't catch break $i: $BN[i]"; missed=1; continue; fi
        by="$by; the other tests' pictures: $(cat $B/p/frames.*.out | grep -c '^FAIL')"
    fi
    caught=$((caught + 1))
    print -r -- "caught: $BN[i] ($by)"
done
print -r -- "breaks caught: $caught of $#BN"
[[ $missed == 0 && $caught == $#BN ]] || exit 1

zsh earlier.sh $quick || { print -r -- "FAIL: the earlier tests' checks"; exit 1 }
print -r -- "test 18: all passed"
