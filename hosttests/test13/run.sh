#!/bin/zsh
# Test 13's host checks (any firmware on the FM-1), on the Mac, under AddressSanitizer + UBSan.
#   zsh run.sh [--quick] [DIR]    --quick: without test 11's whole run.sh against this folder (some minutes)
#                                 DIR: pictures of the guest's page go there as PPM
#
#   every file test 13 shares with test 11 must be test 11's byte for byte (cal.c but for its title; main.c and
#     CMakeLists.txt are test 13's own; guest.c and guest.h are new)
#   guest_check.c   guest.c's units and its page (see its head), and the block of main.c that sends (send_piece,
#                   knob_send, perf_send), cut out of main.c here: for nothing plugged in, asking, silent, AISLING and
#                   a guest, with and without a MIDI OUT, the program changes and CCs that leave
#   main.c read: nothing reaches core 1's queues but through that block; no touch is told it is live but by may_send()
#   deliberate breaks of guest.c and main.c, each to be caught
#   test 11's run.sh, against test 13's folder (everything test 13 didn't change must still pass)
set -e
HERE=${0:A:h}
cd $HERE
V=../../viz-test13
V11=../../viz-test11
O=$(mktemp -d)
F=(-std=gnu11 -g -O1 -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -Wno-unused-variable
   -fsanitize=address,undefined -fno-sanitize-recover=undefined)
quick=
shots=
for a in "$@"; do
    if [[ $a == --quick ]]; then quick=1; else shots=$a; fi
done

print -r -- "-- the files shared with test 11"
for f in $V11/*.c $V11/*.h; do
    n=${f:t}
    [[ $n == (cal.c|main.c) ]] && continue
    cmp -s $f $V/$n || { print -r -- "FAIL: $n is not test 11's"; exit 1 }
done
sed 's/AISLING VIZ TEST 11/AISLING VIZ TEST 13/' $V11/cal.c | cmp -s - $V/cal.c || { print -r -- "FAIL: cal.c is not test 11's but for its title"; exit 1 }
print -r -- "the same (cal.c but for its title; main.c is test 13's; guest.c and guest.h are new)"

# The block of main.c under test: send_piece, knob_send and perf_send
cut_main() {   # $1 main.c  $2 out
    sed -n '/^static void send_piece(int i) {/,/^static bool land_touching;/p' $1 | sed '$d' > $2
    grep -q '^static void send_piece' $2 && grep -q '^static void knob_send' $2 && grep -q '^static void perf_send' $2 ||
        { print -r -- "FAIL: main.c's block has moved"; exit 1 }
}
# main.c read. $1 main.c; prints FAIL lines.
read_main() {
    local m=$1 n
    # the two ways out to the FM-1 from core 0 (the program queue, a CC's generation) are written only in the block
    n=$(sed '/^static void send_piece(int i) {/,/^static bool land_touching;/d' $m | grep -cE 'queue_try_add\(&to_fm1|cc_gen\[[^]]*\] = ' || true)
    [[ $n == 0 ]] || print -r -- "FAIL: main.c sends from outside its sending block ($n places)"
    # a touch is live only by may_send()
    n=$(grep -cE 'perf_(fx|xy|land)_(touch|release)\(.*fm1_out' $m || true)
    [[ $n == 0 ]] || print -r -- "FAIL: main.c: a touch told it is live by fm1_out alone ($n places)"
    n=$(grep -cE 'perf_(fx_touch|xy_touch|land_touch|xy_release|land_release)\(.*may_send\(\)\)' $m || true)
    [[ $n == 5 ]] || print -r -- "FAIL: main.c: $n of 5 touches ask may_send()"
    grep -q '^static bool may_send(void) { return guest_may_send(&gu, fm1_out); }$' $m || print -r -- "FAIL: main.c: may_send isn't guest_may_send"
    # a guest's words don't reach AISLING's page, nor AISLING's the guest's; a piece heard is AISLING's only
    grep -q 'if (sd.who == 1) fm1scr_say(&fm1, sd.text, sd.n, sd.flags, now);' $m || print -r -- "FAIL: main.c: what is said isn't sorted by who said it"
    grep -q 'else if (sd.who == 2) guest_say(&gu, sd.text, sd.n, sd.flags);' $m || print -r -- "FAIL: main.c: a guest's words don't reach its page"
    grep -q 'if (piece >= 0 && who_c1 == 1) {' $m || print -r -- "FAIL: main.c: a piece taken from a guest's words"
    grep -q 'if (gu.who == WHO_AISLING || gu.who == WHO_GUEST) want_where_now();' $m || print -r -- "FAIL: main.c: a WHERE to one that hasn't answered"
    grep -q 'if (!fm1_out || gu.who != WHO_AISLING) {' $m || print -r -- "FAIL: main.c: timed WHEREs to a guest"
    grep -q 'uint8_t li = (uint8_t)guest_layer(&gu, ch, d1);' $m || print -r -- "FAIL: main.c: notes not placed by guest_layer"
    return 0
}
build() {   # $1 folder with guest.c, guest.h and block.inc  $2 out
    clang $F -DMAIN_BLOCK="\"$1/block.inc\"" -I../test4/stub -I$1 -I$V guest_check.c $1/guest.c $V/perf.c $V/knobctl.c \
        $V/fm1scr.c $V/lcd.c $V/tone.c $V/reader.c ../test4/stub.c -lm -o $2
}

print -r -- "-- who is plugged in, what is sent, the guest's page"
mkdir -p $O/src
cp $V/guest.c $V/guest.h $O/src/
cut_main $V/main.c $O/src/block.inc
print -r -- "main.c's sending block: $(wc -l < $O/src/block.inc | tr -d ' ') lines"
build $O/src $O/gc
if [[ -n $shots ]]; then mkdir -p $shots; $O/gc shots $shots | grep -E "FAIL|units"; else $O/gc units | grep -E "FAIL|units"; fi
[[ ${pipestatus[1]} == 0 ]] || { print -r -- "FAIL: guest_check"; exit 1 }
read_main $V/main.c > $O/read.out
if [[ -s $O/read.out ]]; then cat $O/read.out; exit 1; fi
print -r -- "main.c read: one way out to the FM-1, behind may_send(); words and pieces sorted by who said them"

print -r -- "-- deliberate breaks"
MN=(
    "a guest sent to as AISLING is"
    "a silent FM-1 sent to"
    "sent to with no MIDI OUT"
    "still asking after the limit"
    "silent a moment early"
    "key i on a guest"
    "the name kept after unplugging"
    "what it said kept after unplugging"
    "a guest's notes by channel"
    "the bog's band a note short"
    "a held note never let go"
    "a note the firmware let go, let go again later"
    "the same thing said twice fills the history"
    "the history's oldest lost one early"
    "a row wider than the page"
    "a long word run off the row"
    "the newest never drawn small (a whole screen cut)"
    "the rows under a shorter text left on the screen"
    "the size changed without clearing"
    "the history's gaps never cleared"
    "the heading not redrawn for a new name"
    "an unprintable byte kept in a name"
    "a SAY read past its length"
    "a SAY taken for a HELLO answer"
    "the strip's pieces drawn for a guest"
    "main.c: the strip's piece sent to anything with a MIDI OUT"
    "main.c: the knobs' CCs not gated"
    "main.c: the effects' CCs not gated"
    "main.c: the landscape's drag live for a guest"
    "main.c: a guest's words read as AISLING's screen"
    "main.c: a piece taken from a guest's words"
    "main.c: a WHERE to one that hasn't answered"
)
ME=(
    's/return midi_out \&\& g->who == WHO_AISLING;/return midi_out \&\& (g->who == WHO_AISLING || g->who == WHO_GUEST);/'
    's/return midi_out \&\& g->who == WHO_AISLING;/return midi_out \&\& g->who != WHO_GUEST;/'
    's/return midi_out \&\& g->who == WHO_AISLING;/return g->who == WHO_AISLING;/'
    's/now - g->since_ms < GUEST_ASK_MS) return false;/now - g->since_ms <= GUEST_ASK_MS) return false;/'
    's/now - g->since_ms < GUEST_ASK_MS) return false;/now - g->since_ms < GUEST_ASK_MS - 1) return false;/'
    's/^    if (g->who == WHO_SILENT) {$/    if (g->who == WHO_SILENT || g->who == WHO_GUEST) {/'
    '/^static void forget/,/^}/s/    g->name\[0\] = 0;//'
    '/^static void forget/,/^}/s/    g->n_hist = 0;//'
    's/    if (g->who == WHO_AISLING) return ch \& 3;/    if (g->who != WHO_NONE) return ch \& 3;/'
    's/return note < 48 ? 0 : note < 60/return note < 47 ? 0 : note < 60/'
    's/now - g->held_ms\[i\] >= GUEST_HOLD_MS) {/0) {/'
    's/g->held_ms\[note \& 127\] = vel ? (now ? now : 1) : 0;/g->held_ms[note \& 127] = now ? now : 1;/'
    's/    if (!strcmp(t, g->now)) {   /    if (0) {   /'
    's/int keep = g->n_hist < GUEST_HIST ? g->n_hist : GUEST_HIST - 1;/int keep = g->n_hist < GUEST_HIST - 1 ? g->n_hist : GUEST_HIST - 2;/'
    's/^#define BIG_W    26 /#define BIG_W    27 /'
    's/            take = w;$/            take = len;/'
    's/    if (n > BIG_ROWS) scale = 2,/    if (0) scale = 2,/'
    's/    if (n < g->drawn_rows) lcd_fill_rect/    if (0) lcd_fill_rect/'
    's/    if (scale != g->drawn_scale) {   /    if (g->drawn_scale < 0) {   /'
    's/    for (int i = 0; i <= GUEST_HIST; i++) lcd_fill_rect(0, HIST_Y + 8 + i \* 10, LCD_W, 2, BG);//'
    's/        g->dirty |= D_HEAD;$/        ;/'
    's/buf\[i\] >= 32 \&\& buf\[i\] < 127 ? (char)buf\[i\] : . .;/(char)buf[i];/'
    's/    for (int i = 0; i < n \&\& t\[i\]; i++) {$/    for (int i = 0; t[i]; i++) {/'
    's/ || buf\[2\] != 0x52 || buf\[3\] != 1) return false;/ || buf[2] != 0x52) return false;/'
    's/^    if (g->who == WHO_GUEST) {$/    if (0) {/'
    '/^static void send_piece/,/^}/s/if (fm1_out \&\& !may_send()) {/if (0) {/'
    's/^    if (!may_send()) return;   \/\/ (test 13) the knobs/    if (0) return;   \/\/ the knobs/'
    's/    if (!due || !may_send()) return;/    if (!due) return;/'
    's/perf_land_touch(\&pf, x, y, touch_reads == 1, may_send())/perf_land_touch(\&pf, x, y, touch_reads == 1, fm1_out)/'
    's/        if (sd.who == 1) fm1scr_say/        if (sd.who) fm1scr_say/'
    's/if (piece >= 0 \&\& who_c1 == 1) {/if (piece >= 0) {/'
    's/    if (gu.who == WHO_AISLING || gu.who == WHO_GUEST) want_where_now();/    want_where_now();/'
)
MF=(guest.c guest.c guest.c guest.c guest.c guest.c guest.c guest.c guest.c guest.c guest.c guest.c guest.c guest.c guest.c
    guest.c guest.c guest.c guest.c guest.c guest.c guest.c guest.c guest.c guest.c
    main.c main.c main.c main.c main.c main.c main.c)
[[ ${#MN} == ${#ME} && ${#MN} == ${#MF} ]] || { print -r -- "FAIL: the breaks' lists differ in length"; exit 1 }
caught=0
for i in {1..${#MN}}; do
    rm -rf $O/b
    mkdir -p $O/b
    cp $V/guest.c $V/guest.h $O/b/
    cp $V/main.c $O/b/main.c
    sed "${ME[$i]}" $V/${MF[$i]} > $O/b/${MF[$i]}
    if cmp -s $O/b/${MF[$i]} $V/${MF[$i]}; then
        print -r -- "FAIL: break $i didn't apply: ${MN[$i]}"
        exit 1
    fi
    cut_main $O/b/main.c $O/b/block.inc
    build $O/b $O/mut 2> $O/mut.err || { cat $O/mut.err; exit 1 }
    : > $O/mut.out
    $O/mut units >> $O/mut.out 2>&1 || true
    read_main $O/b/main.c >> $O/mut.out
    if ! grep -qE "FAIL|ERROR|runtime error" $O/mut.out; then
        print -r -- "FAIL: not caught: ${MN[$i]}"
    else
        caught=$((caught + 1))
        print -r -- "caught: ${MN[$i]} ($(grep -cE 'FAIL|ERROR|runtime error' $O/mut.out) shown; first: $(grep -m1 -E 'FAIL|ERROR|runtime error' $O/mut.out | cut -c1-110))"
    fi
done
print -r -- "$caught of ${#MN} breaks caught"
[[ $caught == ${#MN} ]] || exit 1

if [[ -z $quick ]]; then
    print -r -- "-- test 11's run.sh, against test 13"
    T11=${HERE:h}/test11
    sed -e 's|viz-test11|viz-test13|g' -e 's/AISLING VIZ TEST 11/AISLING VIZ TEST 13/g; s/title says test 11/title says test 13/' \
        -e "s|^HERE=\${0:A:h}\$|HERE=$T11|" $T11/run.sh > $O/run11.sh
    grep -q "^HERE=$T11\$" $O/run11.sh && grep -q '^V=../../viz-test13$' $O/run11.sh || { print -r -- "FAIL: test 11's run.sh has moved"; exit 1 }
    zsh $O/run11.sh --no-sweep > $O/run11.out 2>&1 || { grep FAIL $O/run11.out | head; tail -5 $O/run11.out; print -r -- "FAIL: test 11's checks"; exit 1 }
    grep -E "breaks caught" $O/run11.out | sed 's/^ */   /'
    [[ $(grep -c "^ *FAIL" $O/run11.out) == 0 ]] || { grep "^ *FAIL" $O/run11.out | head; print -r -- "FAIL: in test 11's checks"; exit 1 }
fi
print -r -- "test 13: all passed"
