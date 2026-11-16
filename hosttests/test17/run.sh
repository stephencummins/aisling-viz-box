#!/bin/zsh
# Test 17's host checks (test 16 with a light look for the box's own pages), on the Mac.
#   zsh run.sh [--quick] [DIR]   --quick: test 12's checks without test 11's whole run.sh (some six minutes less)
#                                DIR: the pictures of the pages in both looks, as PNG, into it
#
#   test 17 is test 16 and the look, and nothing else: every file but seven is test 16's byte for byte (titles and
#     the USB name say 17), look.h is the one new file, and the seven (lcd.c, perf.c, settings.c, settings.h, cal.c,
#     cal.h, main.c) differ from test 16's by the lines in changes.txt and no others (main.c: its code lines)
#   look_check.c   map: look.h's rule over every colour, and the box's own pairs of colours in both looks
#                  settings: "look" in SETTINGS.TXT there and back, and a file test 16's own settings.c wrote
#                  pages: calibration and the strip drawn in both looks through lcd.c, pixel by pixel
#                  frames: the pictures test 11's, test 13's and test 7's checks make of the effects, the pad, the
#                  guest's page and the FM-1's page, made in both looks: the light is the dark turned over
#   old16.c        test 16's settings.c writes a file; test 17's reads it, and test 16's reads test 17's
#   main.c read: the button, the key, the saving and the file's word all reach lcd_look_set
#   deliberate breaks of look.h, lcd.c, perf.c, settings.c, cal.c and main.c, each to be caught
#   test 16's run.sh against test 17's folder, and inside it test 15's, 13's and 12's (and 11's and 7's), each
#     without its comparison of the files test 17 changed (they are compared here, with test 16's). They run in the
#     dark look, which is what the box starts in: what they check is that the look has changed nothing when it is off
set -e
HERE=${0:A:h}
cd $HERE
V=../../viz-test17
V16=../../viz-test16
O=$(mktemp -d)
F=(-std=gnu11 -g -O1 -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined -fno-sanitize-recover=undefined)
FM=${FM:-$HOME/Projects/fm1-aisling}   # announce.c, for the FM-1's page (as test 7's run.sh)
quick=
shots=
for a in "$@"; do
    if [[ $a == --quick ]]; then quick=--quick; else shots=$a; fi
done
CHANGED=(lcd.c perf.c settings.c settings.h cal.c cal.h main.c)

print -r -- "-- test 17 is test 16 and the look"
for f in $V16/*(.); do
    n=${f:t}
    case $n in
        README.md|lcd.c|perf.c|settings.c|settings.h|cal.c|cal.h|main.c) ;;
        card.c) sed 's/AISLING VIZ TEST 16/AISLING VIZ TEST 17/' $f | cmp -s - $V/$n || { print -r -- "FAIL: $n is not test 16's but for its title"; exit 1 } ;;
        usb_descriptors.c) sed 's/"AISLING viz test 16"/"AISLING viz test 17"/' $f | cmp -s - $V/$n || { print -r -- "FAIL: $n is not test 16's but for the name"; exit 1 } ;;
        CMakeLists.txt) sed 's/viz_test16/viz_test17/g' $f | cmp -s - $V/$n || { print -r -- "FAIL: CMakeLists.txt is not test 16's but for the name"; exit 1 } ;;
        *) cmp -s $f $V/$n || { print -r -- "FAIL: $n is not test 16's"; exit 1 } ;;
    esac
done
[[ -f $V/look.h && $(ls $V/*(.) | wc -l) -eq $(($(ls $V16/*(.) | wc -l) + 1)) ]] || { print -r -- "FAIL: test 17 is not test 16's files and look.h"; exit 1 }
# The seven: whole lines, but main.c's code lines only (the comments at its head are its own)
changes() {
    local n
    for n in $CHANGED; do
        print -r -- "== $n"
        case $n in
            main.c) diff <(grep -v '^ *//' $V16/$n | sed 's/---- AISLING viz test 16 ----/---- AISLING viz test 17 ----/') <(grep -v '^ *//' $V/$n) | grep '^[<>]' || true ;;
            cal.c) diff <(sed 's/AISLING VIZ TEST 16/AISLING VIZ TEST 17/' $V16/$n) $V/$n | grep '^[<>]' || true ;;
            *) diff $V16/$n $V/$n | grep '^[<>]' || true ;;
        esac
    done
}
changes > $O/changes
cmp -s $O/changes changes.txt || { diff $O/changes changes.txt | head -20; print -r -- "FAIL: the seven files' changes from test 16's are not changes.txt's"; exit 1 }
for n in lcd.c perf.c settings.c settings.h cal.h; do
    [[ $(diff $V16/$n $V/$n | grep -c '^<') == 0 ]] || { print -r -- "FAIL: $n has lost a line of test 16's"; exit 1 }
done
print -r -- "the same: test 16's files and look.h; the seven lose $(grep -c '^<' $O/changes) lines of test 16's (cal.c's and main.c's) and have $(grep -c '^>' $O/changes) new"

# ---- the programs. $1 is always the folder the box's files are taken from ($V, or a broken copy)
map() { clang $F -DMODE_MAP -I../test4/stub -I$1 -I$V look_check.c -lm -o $2 }
sett() { clang $F -DMODE_SETTINGS -I../test12/stub -I$1 -I$V look_check.c $1/settings.c $1/lcd.c $V/tone.c ../test4/stub.c ffstub.c -lm -o $2 }
cut_strip() {   # $1 main.c  $2 out: the pieces and draw_strip()
    { sed -n '/^static const struct { const char \*name; uint8_t bpm; } PIECES\[8\] = {$/,/^};$/p' $1
      sed -n '/^static void draw_strip(void) {$/,/^}$/p' $1 } > $2
    grep -q '^static void draw_strip' $2 && grep -q 'PIECES\[8\]' $2 || { print -r -- "FAIL: main.c's draw_strip has moved"; exit 1 }
}
pages() {
    cut_strip $1/main.c $1/strip.inc
    clang $F -DMODE_PAGES -I../test4/stub -I$1 -I$V look_check.c $1/cal.c $1/lcd.c $V/tone.c ../test4/stub.c -lm -o $2
}
# The other tests' picture programs, in a look ($3: dark or light)
LOOKSET=()
perfp() {
    [[ $3 == light ]] && LOOKSET=(lookset.c) || LOOKSET=()
    clang $F -I../test4/stub -I$1 -I$V ../test11/perf_check.c $1/perf.c $V/knobctl.c $V/fm1scr.c $1/lcd.c $V/tone.c \
        $V/reader.c ../test4/stub.c $LOOKSET -lm -o $2
}
guestp() {
    [[ $3 == light ]] && LOOKSET=(lookset.c) || LOOKSET=()
    sed -n '/^static void send_piece(int i) {/,/^static bool land_touching;/p' $1/main.c | sed '$d' > $1/block.inc
    grep -q '^static void send_piece' $1/block.inc || { print -r -- "FAIL: main.c's sending block has moved"; exit 1 }
    clang $F -DMAIN_BLOCK="\"$1/block.inc\"" -I../test4/stub -I$1 -I$V ../test13/guest_check.c $V/guest.c $1/perf.c \
        $V/knobctl.c $V/fm1scr.c $1/lcd.c $V/tone.c $V/reader.c ../test4/stub.c $LOOKSET -lm -o $2
}
fm1p() {
    [[ $3 == light ]] && LOOKSET=(lookset.c) || LOOKSET=()
    clang $F -I../test4/stub -I$1 -I$V -DFM1SCR_C="\"$V/fm1scr.c\"" -DANNOUNCE_C="\"$FM/firmware/src/announce.c\"" \
        -DANN_NAME='"AISLING 1.3"' -DCORPUS_LINES=8997 ../test7/fm1scr_check.c $1/lcd.c $V/tone.c $V/reader.c \
        ../test4/stub.c $LOOKSET -lm -o $2
}
PERF_PICS=(effects effects-held pad pad-touched pad-chorus pad-amounts-full pad-amounts effects-amounts)
# main.c read: every way to the look ends in lcd_look_set, and what the pages are drawn by goes through look.h
read_main() {   # $1 folder
    local m=$1/main.c
    sed -n '/^static void look_next(void) {$/,/^}$/p' $m > $O/look_next
    grep -q '^    lcd_look_set(lcd_look() == LOOK_LIGHT ? LOOK_DARK : LOOK_LIGHT);$' $O/look_next &&
    grep -q '^    redraw_all();$' $O/look_next &&
    grep -q '^    else if (b == CAL_LOOK) look_next();' $m &&
    grep -q "^        case 'h': look_next(); break;" $m &&
    grep -q '^    s->look = (uint8_t)lcd_look();' $m &&
    [[ $(grep -A2 '^    if (s->look != was.look) {' $m | grep -c -e '^        lcd_look_set(s->look);$' -e '^        changed = true;$') == 2 ]] &&
    [[ $(grep -c 'look_565(' $1/lcd.c) == 2 && $(grep -c 'look_565(' $1/perf.c) == 1 ]] &&
    [[ $(sed -n '/^void lcd_fill_rect/,/^}/p' $1/lcd.c | grep -c 'colour = look_565(colour, look_now);') == 1 ]] &&
    [[ $(sed -n '/^void lcd_text/,/^}/p' $1/lcd.c | grep -c 'fg = look_565(fg, look_now), bg = look_565(bg, look_now);') == 1 ]] &&
    [[ $(sed -n '/^void lcd_blit/,/^}/p' $1/lcd.c | grep -c 'look') == 0 ]]
}

print -r -- "-- the rule"
map $V $O/map
$O/map > $O/map.out || { cat $O/map.out; print -r -- "FAIL: the rule"; exit 1 }
grep -E "faintest|^look:" $O/map.out
grep -q "^look: [1-9][0-9]* checks, 0 failed$" $O/map.out || { print -r -- "FAIL: the rule's checks didn't run"; exit 1 }

print -r -- "-- the settings file"
# test 16's own settings.c writes a file (no look in it); test 17's writes one too (the look: light)
clang $F -I../test12/stub -I$V16 old16.c $V16/settings.c $V16/lcd.c $V16/tone.c ../test4/stub.c ffstub.c -lm -o $O/old16
clang $F -DWITH_LOOK -I../test12/stub -I$V old16.c $V/settings.c $V/lcd.c $V/tone.c ../test4/stub.c ffstub.c -lm -o $O/new17
$O/old16 write > $O/old16.txt
$O/new17 write > $O/new17.txt
[[ -s $O/old16.txt ]] && ! grep -qi look $O/old16.txt || { print -r -- "FAIL: test 16's file wasn't written, or has a look"; exit 1 }
diff $O/old16.txt $O/new17.txt | grep '^[<>]' > $O/txt.diff || true
print -r -- "> # The box's own pages (the strip, the FM-1's page, the effects, the pad, the card, calibration):
> # dark, or light (the same colours, deeper, on white). The landscape keeps its ground.
> look = light
> " | cmp -s - $O/txt.diff || { cat $O/txt.diff; print -r -- "FAIL: test 17's file is not test 16's with the look's paragraph"; exit 1 }
sett $V $O/sett
$O/sett $O/old16.txt > $O/sett.out || { cat $O/sett.out; print -r -- "FAIL: the settings"; exit 1 }
grep -q "^look: [1-9][0-9]* checks, 0 failed$" $O/sett.out || { print -r -- "FAIL: the settings' checks didn't run"; exit 1 }
# and the other way: test 16's settings.c, given test 17's file, uses all it knows and names the one line it doesn't
$O/old16 read $O/new17.txt > $O/back.out
grep -q "got=1ff bad=0 unknown=1 " $O/back.out && grep -q "look isn't a setting" $O/back.out || { cat $O/back.out; print -r -- "FAIL: test 16 doesn't read test 17's file"; exit 1 }
print -r -- "$(tail -1 $O/sett.out); test 16's file ($(wc -c < $O/old16.txt | tr -d ' ') bytes) read by test 17 as dark, test 17's ($(wc -c < $O/new17.txt | tr -d ' ') bytes) by test 16 but for its look"

print -r -- "-- the pages in both looks"
mkdir -p $O/src $O/pics $O/dark $O/light
cp $V/look.h $V/lcd.c $V/perf.c $V/settings.c $V/cal.c $V/main.c $O/src/
pages $O/src $O/pages
$O/pages $O/pics > $O/pages.out || { cat $O/pages.out; print -r -- "FAIL: calibration and the strip"; exit 1 }
grep -E "^calibration|^look:" $O/pages.out
grep -q "^look: [1-9][0-9]* checks, 0 failed$" $O/pages.out || { print -r -- "FAIL: the pages' checks didn't run"; exit 1 }
for l in dark light; do
    perfp $O/src $O/perf.$l $l
    guestp $O/src $O/guest.$l $l
    fm1p $O/src $O/fm1.$l $l
    $O/perf.$l shots $O/$l > $O/perf.$l.out 2>&1 || true
    $O/guest.$l shots $O/$l > $O/guest.$l.out 2>&1 || true
    # (in the light its own checks of the colours it expects fail, as they should: only its pictures are wanted)
    $O/fm1.$l ../test11/corpus.txt --quick --shots $O/$l > $O/fm1.$l.out 2>&1 || true
done
[[ $(grep -c FAIL $O/perf.dark.out $O/guest.dark.out $O/fm1.dark.out | awk -F: '{n += $2} END {print n}') == 0 ]] ||
    { print -r -- "FAIL: the other tests' picture programs fail in the dark look"; exit 1 }
grep -q FAIL $O/fm1.light.out || { print -r -- "FAIL: test 7's check sees nothing different in the light look"; exit 1 }
NAMES=($O/dark/*.ppm(:t:r))
NAMES=(${NAMES:#flash-*})   # the flashes are 272 rows, not 320: looked at, not compared
[[ $#NAMES == 28 ]] || { print -r -- "FAIL: $#NAMES pictures, not 28"; exit 1 }
clang $F -DMODE_FRAMES -I../test4/stub -I$V look_check.c -lm -o $O/frames
$O/frames $O/dark $O/light $NAMES > $O/frames.out || { cat $O/frames.out; print -r -- "FAIL: a page in the light is not the dark one turned over"; exit 1 }
grep -E "pictures in both|^look:" $O/frames.out
grep -q "^look: [1-9][0-9]* checks, 0 failed$" $O/frames.out || { print -r -- "FAIL: the frames' checks didn't run"; exit 1 }
read_main $V || { print -r -- "FAIL: main.c, lcd.c or perf.c doesn't go by the look as read"; exit 1 }
print -r -- "main.c read: the button, the key, the saving and the file all reach lcd_look_set; lcd.c turns fills and text, and no pixels"
if [[ -n $shots ]]; then
    mkdir -p $shots
    for l in dark light; do
        for p in $O/$l/*.ppm; do sips -s format png $p --out $shots/${p:t:r}-$l.png > /dev/null; done
    done
    for p in $O/pics/*.ppm; do sips -s format png $p --out $shots/${p:t:r}.png > /dev/null; done
    print -r -- "pictures in $shots"
fi

print -r -- "-- deliberate breaks, each to be caught"
BN=("look.h: light is dark" "look.h: turned over but not deepened" "look.h: pure colours not held down (yellow on white)"
    "look.h: dark is not the colour itself" "look.h: black stays black"
    "lcd.c: fills not turned" "lcd.c: text not turned" "lcd.c: the look never set" "lcd.c: the box starting light"
    "perf.c: the uncial lettering not turned"
    "settings.c: the look not written" "settings.c: light written as dark" "settings.c: any word taken for a look"
    "settings.c: a file with no look setting it dark" "settings.c: a changed look not a change to save"
    "cal.c: the ramps turned with the page" "cal.c: LOOK's touch a row low" "cal.c: LOOK drawn over CARD"
    "main.c: LOOK doing nothing" "main.c: the look not saved" "main.c: the file's look not used"
    "main.c: the page not drawn again in the new look" "main.c: the strip drawn dark whatever the look")
BF=(look.h look.h look.h look.h look.h lcd.c lcd.c lcd.c lcd.c perf.c
    settings.c settings.c settings.c settings.c settings.c cal.c cal.c cal.c main.c main.c main.c main.c main.c)
BS=('s/^    if (look != LOOK_LIGHT) return c;$/    return c;/'
    '/^    t = t \* (t + 255) \/ 510;$/d'
    '/^    if (t > cap) t = cap;$/d'
    's/^    if (look != LOOK_LIGHT) return c;$/    if (look != LOOK_LIGHT) return c | 1;/'
    's/^    int t = 255 - y;$/    int t = y ? 255 - y : 0;/'
    '/^    colour = look_565(colour, look_now);   \/\/ (test 17)$/d'
    '/^    fg = look_565(fg, look_now), bg = look_565(bg, look_now);   \/\/ (test 17)$/d'
    's/look_now = look == LOOK_LIGHT ? LOOK_LIGHT : LOOK_DARK;/look_now = LOOK_DARK; (void)look;/'
    's/^static uint8_t look_now = LOOK_DARK;$/static uint8_t look_now = LOOK_LIGHT;/'
    '/^    fg = look_565(fg, lcd_look()), bg = look_565(bg, lcd_look());/d'
    's/\\nlook = %s\\n\\n",$/\\n\\n",/; s/^        SET_LOOKS\[s->look < LOOK_N ? s->look : LOOK_DARK\]);$/        0);/'
    's/^        SET_LOOKS\[s->look < LOOK_N ? s->look : LOOK_DARK\]);$/        SET_LOOKS[LOOK_DARK]);/'
    's/^        if ((i = index_of(v, SET_LOOKS, LOOK_N)) < 0) return -1;$/        if ((i = index_of(v, SET_LOOKS, LOOK_N)) < 0) i = LOOK_LIGHT;/'
    's/^    if (same_word(name, "look")) {.*$/    s->look = LOOK_DARK; if (same_word(name, "look")) {/'
    '/^    if (a->look != b->look) return false;   \/\/ (test 17)$/d'
    '1,/^    lcd_look_set(LOOK_DARK);$/{/^    lcd_look_set(LOOK_DARK);$/d;}'
    's/^    if (x >= LOOK_X \&\& y >= SAMPLE_Y \&\& y < SAMPLE_Y + SAMPLE_H) return CAL_LOOK;/    if (x >= LOOK_X \&\& y >= SAMPLE_Y + SAMPLE_H \&\& y < SAMPLE_Y + 2 * SAMPLE_H) return CAL_LOOK;/'
    's/^#define LOOK_X  288 /#define LOOK_X  336 /'
    's/^    else if (b == CAL_LOOK) look_next();/    else if (b == CAL_LOOK) say("look");/'
    '/^    s->look = (uint8_t)lcd_look();/d'
    's/^        lcd_look_set(s->look);$/        ;/'
    '/^static void look_next(void) {$/,/^}$/s/^    redraw_all();$//'
    '/^static void draw_strip(void) {$/a\
    lcd_look_set(LOOK_DARK);')
[[ $#BN == $#BS && $#BN == $#BF ]] || { print -r -- "FAIL: the breaks' lists differ in length"; exit 1 }
caught=0
mkdir -p $O/b
for i in {1..$#BS}; do
    f=$BF[$i]
    cp $V/look.h $V/lcd.c $V/perf.c $V/settings.c $V/cal.c $V/main.c $O/b/
    sed -e $BS[$i] $V/$f > $O/b/$f
    if cmp -s $O/b/$f $V/$f; then print -r -- "FAIL: break $i ($BN[$i]) changed nothing"; exit 1; fi
    ok=1   # 1: nothing noticed
    case $f in
        look.h) map $O/b $O/b/p 2> $O/b/err && $O/b/p > /dev/null || ok=0 ;;
        lcd.c|cal.c) pages $O/b $O/b/p 2> $O/b/err && $O/b/p > /dev/null && read_main $O/b || ok=0 ;;
        perf.c)
            mkdir -p $O/b/light
            perfp $O/b $O/b/p light 2> $O/b/err && { $O/b/p shots $O/b/light > /dev/null 2>&1 || true } &&
                $O/frames $O/dark $O/b/light $PERF_PICS > /dev/null && read_main $O/b || ok=0 ;;
        settings.c)
            sett $O/b $O/b/p 2> $O/b/err && $O/b/p $O/old16.txt > /dev/null &&
                clang $F -DWITH_LOOK -I../test12/stub -I$O/b -I$V old16.c $O/b/settings.c $V/lcd.c $V/tone.c ../test4/stub.c ffstub.c -lm -o $O/b/n17 2>> $O/b/err &&
                $O/b/n17 write | cmp -s - $O/new17.txt || ok=0 ;;
        main.c) read_main $O/b && pages $O/b $O/b/p 2> $O/b/err && $O/b/p > /dev/null || ok=0 ;;
    esac
    if (( ok )); then print -r -- "FAIL: break $i ($BN[$i]) not caught"; exit 1; fi
    caught=$((caught + 1)); print -r -- "   caught: $BN[$i]"
done
print -r -- "breaks caught: $caught of $#BS"

print -r -- "-- test 16's run.sh, against test 17${quick:+ (quick)}"
# The earlier scripts each compare a file or two that test 17 has changed with an older test's (cal.c, cal.h, perf.c,
# settings.c, settings.h, lcd.c, main.c). Those comparisons are made above, with test 16's, so they are taken out
# here; everything else runs as it is, on test 17's folder. The scripts find each other by folder, so they are run
# from a copy of hosttests/ that is links to this one, with the three changed scripts (and test 12's check12.c, which
# must know the file now has a tenth setting: its list of them, and the count the box says it read) written in.
M=$O/m
mkdir -p $M/hosttests
for d in ${HERE:h:h}/viz-test*(/); do ln -s $d $M/${d:t}; done
for d in ${HERE:h}/test*(/); do
    case ${d:t} in
        test12|test15|test16)
            mkdir $M/hosttests/${d:t}
            for f in $d/*; do
                [[ ${f:t} == run.sh || ${f:t} == check12.c ]] || ln -s $f $M/hosttests/${d:t}/${f:t}
            done ;;
        *) ln -s $d $M/hosttests/${d:t} ;;
    esac
done
# test 16's: without its comparison with test 15's files, and saying 17
sed -e '/^print -r -- "-- test 16 is test 15 and the strip"$/,/^print -r -- "the same: test 15.s files and strip.h; main.c loses /d' \
    -e 's|viz-test16|viz-test17|g; s/viz_test16/viz_test17/g; s/AISLING VIZ TEST 16/AISLING VIZ TEST 17/g; s/"AISLING viz test 16"/"AISLING viz test 17"/g; s/title says test 16/title says test 17/' \
    ../test16/run.sh > $M/hosttests/test16/run.sh
grep -q '^V=../../viz-test17$' $M/hosttests/test16/run.sh && ! grep -q 'main_changes.txt' <(grep -v '^#' $M/hosttests/test16/run.sh) &&
    ! grep -q 'viz.test16' $M/hosttests/test16/run.sh && grep -q '^print -r -- "-- the strip"$' $M/hosttests/test16/run.sh ||
    { print -r -- "FAIL: test 16's run.sh has moved"; exit 1 }
# test 15's: the changed files left out of its comparison with test 14's, and lcd.c with the turn off compared with
# itself as it now is (that it is test 16's but for the look's lines is changes.txt's)
sed -e 's/^        README.md|lcd.c) ;;$/        README.md|lcd.c|cal.c|cal.h|perf.c|settings.c|settings.h) ;;/' \
    -e 's/^        cal.c|card.c) sed /        card.c) sed /' \
    -e 's|^clang -E -P -I../test4/stub -I\$V14 \$V14/lcd.c > \$O/lcd14.i$|clang -E -P -I../test4/stub -I$V $V/lcd.c > $O/lcd14.i|' \
    ../test15/run.sh > $M/hosttests/test15/run.sh
grep -q '^        README.md|lcd.c|cal.c|cal.h|perf.c|settings.c|settings.h) ;;$' $M/hosttests/test15/run.sh &&
    grep -q '^        card.c) sed ' $M/hosttests/test15/run.sh && grep -q '^clang -E -P -I../test4/stub -I$V $V/lcd.c > $O/lcd14.i$' $M/hosttests/test15/run.sh ||
    { print -r -- "FAIL: test 15's run.sh has moved"; exit 1 }
# test 12's: perf.c left out of its comparison with test 11's; cal.c has lost six of test 11's lines, not four (the
# two sample lines the LOOK button shortened); and its check knows the tenth setting
sed -e '/^    \[\[ \$n == (cal.c|cal.h|main.c|tusb_config.h|usb_descriptors.c) \]\] && continue$/a\
    [[ $n == perf.c ]] \&\& continue' \
    -e "s/\$V\/cal.c | grep -c '^<') == 4 \]\] ||\$/\$V\/cal.c | grep -c '^<') == 6 ]] ||/" \
    ../test12/run.sh > $M/hosttests/test12/run.sh
sed -e 's/ | SET_PAD_UP;$/ | SET_PAD_UP | SET_LOOK;/' -e 's/count("SETTINGS.TXT read, 9 settings from it")/count("SETTINGS.TXT read, 10 settings from it")/' \
    ../test12/check12.c > $M/hosttests/test12/check12.c
grep -q '^    \[\[ $n == perf.c \]\] && continue$' $M/hosttests/test12/run.sh && grep -q "grep -c '^<') == 6 ]] ||$" $M/hosttests/test12/run.sh &&
    [[ $(diff ../test12/run.sh $M/hosttests/test12/run.sh | grep -c '^[<>]') == 3 ]] &&
    [[ $(diff ../test12/check12.c $M/hosttests/test12/check12.c | grep -c '^>') == 2 ]] ||
    { print -r -- "FAIL: test 12's run.sh or check12.c has moved"; exit 1 }
zsh $M/hosttests/test16/run.sh $quick > $O/run16.out 2>&1 || { grep FAIL $O/run16.out | head; tail -5 $O/run16.out; print -r -- "FAIL: test 16's checks"; exit 1 }
grep -E "^strip|flip|units|main.c read|breaks caught|^ *(sd|fs|notfat|full|pull|msc|main|names):|all passed" $O/run16.out | sed 's/^ */   /'
[[ $(grep -c "^ *FAIL" $O/run16.out) == 0 ]] && grep -q "^test 16: all passed$" $O/run16.out && grep -q "^ *test 15: all passed$" $O/run16.out ||
    { print -r -- "FAIL: in test 16's checks"; exit 1 }
[[ $(grep -c "^breaks caught: 11 of 11$" $O/run16.out) == 1 ]] && grep -q "breaks caught: 8 of 8" $O/run16.out && grep -q "breaks caught: 47 of 47" $O/run16.out ||
    { print -r -- "FAIL: not all of the earlier breaks caught"; exit 1 }

print -r -- "test 17: all passed"
