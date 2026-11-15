#!/bin/zsh
# Test 12's host checks (the settings file on the microSD card, and the card as a USB drive), on the Mac, under
# AddressSanitizer + UBSan.
#   zsh run.sh [--quick]     --quick: without test 11's whole run.sh against this folder (some six minutes)
#
#   model.c      the card: SD's SPI mode byte by byte (start-up, CSD, single and multiple blocks, busy, a card pulled
#                out), the pins it shares spi1 on with the screen, and the SDK calls sd.c makes
#   check12.c    sd.c against every kind of card; SETTINGS.TXT's text; FatFs as the box builds it on FAT16, FAT32,
#                a card with no partition table, exFAT (refused), a full card, a card pulled at every stage of a
#                save; msc.c's SCSI callbacks and the card's page; and the block of main.c that decides when the
#                file is read and written, cut out of main.c here
#   fat.py       a FAT reader and writer that owes nothing to FatFs: what the box wrote, read back; a file a computer
#                put, read by the box. And macOS's fsck_msdos on every card the box has written.
#   the card images are made by hdiutil create and never attached
#   test 11's run.sh, against test 12's folder (everything test 12 didn't change must still pass)
#   deliberate breaks of sd.c, settings.c, card.c, msc.c and main.c, each to be caught
set -e
HERE=${0:A:h}
cd $HERE
V=../../viz-test12
V11=../../viz-test11
O=$(mktemp -d)
F=(-std=gnu11 -g -O1 -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined -fno-sanitize-recover=undefined)
quick=
[[ $1 == --quick ]] && quick=1
OWN=(sd.c settings.c card.c msc.c)   # what the breaks change (and main.c, cut)

print -r -- "-- the files shared with test 11"
for f in $V11/*.c $V11/*.h; do
    n=${f:t}
    [[ $n == (cal.c|cal.h|main.c|tusb_config.h|usb_descriptors.c) ]] && continue
    cmp -s $f $V/$n || { print -r -- "FAIL: $n is not test 11's"; exit 1 }
done
[[ $(diff <(sed 's/AISLING VIZ TEST 11/AISLING VIZ TEST 12/' $V11/cal.c) $V/cal.c | grep -c '^<') == 4 ]] ||
    { print -r -- "FAIL: cal.c is not test 11's with the CARD button"; exit 1 }
[[ $(diff $V11/cal.h $V/cal.h | grep -c '^<') == 0 ]] || { print -r -- "FAIL: cal.h is not test 11's with additions"; exit 1 }
print -r -- "the same (cal.c and cal.h but for the CARD button and the settings; main.c and the USB descriptors are test 12's)"

# The block of main.c under test: from the save's timings to show_card, and card_do
cut_main() {   # $1 main.c  $2 out
    { sed -n '/^#define SAVE_AFTER_MS/,/^static void show_card(void) {/p' $1 | sed '$d'
      sed -n "/^\/\/ A button on the card's page let go/,/^static void card_log/p" $1 | sed '$d' } > $2
    grep -q '^static void settings_tick' $2 && grep -q '^static void card_do' $2 && grep -q '^static void card_got' $2 &&
        ! grep -q 'show_card(void) {' $2 || { print -r -- "FAIL: main.c's block has moved"; exit 1 }
}
print -r -- "-- building"
mkdir -p $O/obj
for f in lcd.c diskio.c ff.c tone.c; do clang $F -Istub -I$V -c $V/$f -o $O/obj/${f:r}.o; done
clang $F -Istub -I$V -c model.c -o $O/obj/model.o
build() {   # $1 the folder with sd.c settings.c card.c msc.c and block.inc  $2 out
    clang $F -DMAIN_BLOCK="\"$1/block.inc\"" -Istub -I$1 -I$V check12.c $1/sd.c $1/settings.c $1/card.c $1/msc.c \
        $O/obj/*.o -o $2
}
mkdir -p $O/src
cp $V/${^OWN} $O/src/
cut_main $V/main.c $O/src/block.inc
build $O/src $O/c12

print -r -- "-- the card images (made, never attached)"
hdiutil create -size 16m -fs "MS-DOS FAT16" -layout MBRSPUD -volname AISLING $O/f16 > /dev/null
hdiutil create -size 64m -fs "MS-DOS FAT32" -layout MBRSPUD -volname AISLING $O/f32 > /dev/null
hdiutil create -size 16m -fs "MS-DOS FAT16" -layout NONE -volname AISLING $O/bare > /dev/null
hdiutil create -size 16m -fs ExFAT -layout MBRSPUD -volname AISLING $O/ex > /dev/null
for i in f16 f32 bare; do print -r -- "$i: $(python3 -I fat.py $O/$i.dmg info)"; done

# fsck_msdos on an image's FAT volume (cut out of its partition, if it has one)
fsck() {   # $1 image
    local at=$(python3 -I fat.py $1 info | sed 's/.*at sector \([0-9]*\):.*/\1/')
    dd if=$1 of=$O/vol.img bs=512 skip=$at 2> /dev/null
    fsck_msdos -n $O/vol.img > $O/fsck.out 2>&1 && ! grep -v '^\*\* ' $O/fsck.out | grep -qiE "orphan|lost|cross|invalid|error|mismatch|out of range"
}

# Every check, with the program given. Output to $2; fails (returns 1) if any check does.
all() {   # $1 program  $2 output
    local p=$1 bad=0 i
    : > $2
    $p sd >> $2 2>&1 || bad=1
    $p units >> $2 2>&1 || bad=1
    # settings.c's names are land.c's, perf.c's and main.c's
    $p names > $O/names.txt 2>> $2 || bad=1
    {
        sed -n '/^static const palette_t PALETTES/,/^};/p' $V/land.c | sed -n 's/^    {"\([a-z]*\)", .*/palette \1/p'
        sed -n '/^static const ground_t GROUNDS/,/^};/p' $V/land.c | sed -n 's/^    {"\([a-z]*\)", .*/ground \1/p'
        sed -n 's/^static const char \*const PAD_NAME\[PERF_PADS\] = {\(.*\)};/\1/p' $V/perf.c | tr -d ' "' | tr ',' '\n' | sed 's/^/pad /'
        sed -n 's/^static const uint32_t SPEEDS_MHZ\[N_SPEEDS\] = {\(.*\)};/\1/p' $V/main.c | tr -d ' ' | tr ',' '\n' | sed 's/^/speed /'
        sed -n 's/^#define TONE_STEPS \([0-9]*\).*/steps \1/p' $V/tone.h
    } > $O/names.want
    if ! cmp -s $O/names.txt $O/names.want || (( $(wc -l < $O/names.want) != 15 )); then
        print -r -- "FAIL: settings.c's names aren't land.c's, perf.c's and main.c's" >> $2
        bad=1
    fi
    for i in f16 f32 bare; do
        $p fs $O/$i.dmg $O/$i.out $O/$i.txt >> $2 2>&1 || { bad=1; continue }
        # what the box wrote, as a reader that isn't FatFs finds it; and the card sound
        python3 -I fat.py $O/$i.out cat SETTINGS.TXT 2>> $2 | cmp -s - $O/$i.txt || { print -r -- "FAIL: $i: SETTINGS.TXT isn't the text the box wrote" >> $2; bad=1 }
        fsck $O/$i.out || { print -r -- "FAIL: $i: fsck_msdos: $(tail -2 $O/fsck.out | tr '\n' ' ')" >> $2; bad=1 }
        # a file a computer put there (CRLF, its own case, a line the box doesn't know), read by the box
        cp $O/$i.out $O/put.img
        printf 'warmth = 0\r\nPalette = Kells\r\nground = mist\r\nvolume = 11\r\npad_up = chorus\r\nvcom = 0x40\r\n' > $O/mine.txt
        python3 -I fat.py $O/put.img put SETTINGS.TXT $O/mine.txt 2>> $2
        $p read $O/put.img > $O/read.txt 2>> $2 || bad=1
        print -r -- 'CARD OK load=1 got=11d bad=0 unknown=1 cut=0 line=4 first="volume isn'"'"'t a setting"
warmth=0 spi=20 palette=kells ground=mist vcom=1:40 vrh=0:00 gamma=0 pad=Filter,Chorus' > $O/read.want
        cmp -s $O/read.txt $O/read.want || { print -r -- "FAIL: $i: a file put by a computer read as: $(head -1 $O/read.txt)" >> $2; bad=1 }
        fsck $O/put.img || { print -r -- "FAIL: fat.py left $i unsound" >> $2; bad=1 }
    done
    $p notfat $O/ex.dmg >> $2 2>&1 || bad=1
    for i in f16 f32; do
        $p full $O/$i.dmg $O/full.out >> $2 2>&1 || bad=1
        fsck $O/full.out || { print -r -- "FAIL: $i filled: fsck_msdos: $(tail -2 $O/fsck.out | tr '\n' ' ')" >> $2; bad=1 }
        $p pull $O/$i.dmg >> $2 2>&1 || bad=1
    done
    grep -q "empty or gone 0, " $2 || { print -r -- "FAIL: a pulled card left SETTINGS.TXT empty or gone" >> $2; bad=1 }
    $p msc $O/f16.dmg $O/ex.dmg $O/bare.dmg >> $2 2>&1 || bad=1
    $p msc $O/f32.dmg $O/ex.dmg $O/f16.dmg >> $2 2>&1 || bad=1
    $p main $O/f16.dmg >> $2 2>&1 || bad=1
    $p main $O/f32.dmg >> $2 2>&1 || bad=1
    return $bad
}

print -r -- "-- the checks"
if ! all $O/c12 $O/all.out; then
    grep -vE "^(sd|units|fs|notfat|full|pull|msc|main): " $O/all.out | head -40
    print -r -- "FAIL: test 12's checks"
    exit 1
fi
grep -E "^(sd|units|fs|notfat|full|pull|msc|main): " $O/all.out
print -r -- "names: settings.c's are land.c's, perf.c's and main.c's; fat.py and fsck_msdos agree with the box on every card"

if [[ -z $quick ]]; then
    print -r -- "-- test 11's run.sh, against test 12"
    T11=${HERE:h}/test11
    sed -e 's|viz-test11|viz-test12|g' -e 's/AISLING VIZ TEST 11/AISLING VIZ TEST 12/g; s/title says test 11/title says test 12/' \
        -e "s|^HERE=\${0:A:h}\$|HERE=$T11|" -e '/cmp -s - \$V\/cal.c/d' -e 's/         cal.h font5x7.h; do/         font5x7.h; do/' \
        $T11/run.sh > $O/run11.sh
    grep -q "^HERE=$T11\$" $O/run11.sh && grep -q '^V=../../viz-test12$' $O/run11.sh && ! grep -q 'cal.h font5x7.h' $O/run11.sh &&
        ! grep -q 'cmp -s - \$V/cal.c' $O/run11.sh || { print -r -- "FAIL: test 11's run.sh has moved"; exit 1 }
    zsh $O/run11.sh --no-sweep > $O/run11.out 2>&1 || { grep FAIL $O/run11.out | head; print -r -- "FAIL: test 11's checks"; exit 1 }
    grep -E "breaks caught" $O/run11.out | sed 's/^ */   /'
    [[ $(grep -c "FAIL: not caught" $O/run11.out) == 0 ]] || { print -r -- "FAIL: a break of test 11's not caught"; exit 1 }
fi

print -r -- "-- deliberate breaks, each to be caught"
MN=(
    "sd: a byte-addressed card given sector numbers"
    "sd: a high-capacity card not told the box can address one (it never starts)"
    "sd: the card's size one unit short"
    "sd: a read past the card's end sent to it"
    "sd: a failed write leaving the card thought good"
    "sd: the bus not given back to the screen after a read"
    "sd: no CMD12 after a read of several blocks"
    "sd: the card started at the screen's speed"
    "sd: no pull-up on D0 (an empty slot looks busy)"
    "settings: warmth 5 taken"
    "settings: a VCOM of 0x81 taken"
    "settings: a gamma number out of its range taken"
    "settings: the pad's two knobs allowed to be one"
    "settings: names matched only in the file's own case"
    "settings: a note after a value read as part of it"
    "settings: the file emptied before it's written (a pulled card loses it)"
    "settings: a short write (a full card) called a save"
    "settings: a longer old file's end left after the new text"
    "settings: a text that didn't fit given as if it had"
    "card: an exFAT card called no card"
    "card: saved while the Mac has it (neither its state nor FatFs in the way)"
    "card: read while the Mac has it"
    "card: lent with FatFs still holding it"
    "card: taken from the Mac's writing at the first tap"
    "card: a save's failure not looked into (a card gone still CARD OK)"
    "msc: the Mac reading a card it hasn't been lent"
    "msc: the Mac writing a card it hasn't been lent"
    "msc: a size given for a card it hasn't been lent"
    "msc: no unit attention when the card is lent"
    "msc: any START STOP taken for an eject"
    "msc: an eject not giving the card back"
    "msc: the cable's going noticed at once (a port reset would take the card back)"
    "msc: the cable's going never noticed"
    "msc: a write not marked as one"
    "msc: half a sector passed on"
    "main: a change written at once (no two seconds)"
    "main: a change written again and again"
    "main: the file not winning when the card comes back"
    "main: a card with no file not given one"
    "main: saved while the Mac has it"
    "main: lent with a change unsaved"
    "main: lent with no computer there"
    "main: BACK leaving the card with the Mac"
    "main: an axis with an effect given the file's knob"
    "main: the screen's speed not set from the file"
    "main: an eject not looked for"
    "main: a failed save tried again every half second"
)
ME=(
    's/uint32_t a = block_addr ? lba : lba \* 512u;/uint32_t a = lba;/'
    's/v2 ? 1u << 30 : 0/0/'
    's/size = (size + 1) << 10;/size = size << 10;/'
    's/return ok \&\& n > 0 \&\& lba < sectors \&\& n <= sectors - lba;/return ok \&\& n > 0;/'
    '/^bool sd_write/,/^}/s/        ok = false;//'
    '/^bool sd_read/,/^}/s/    bus_give();//'
    's/            if (cmd(12, 0) != 0x00) good = false;//'
    's/    bus_take(SD_SLOW_HZ);/    bus_take(SD_FAST_HZ);/'
    's/    gpio_pull_up(SD_PIN_D0);//'
    's/one_number(v, TONE_STEPS - 1, \&n)/one_number(v, TONE_STEPS, \&n)/'
    's/^#define VCM_MAX  0x80/#define VCM_MAX  0x81/'
    's/number(\&v, (1u << PANEL_GAMMA_BITS\[i\]) - 1, \&n)/number(\&v, 255, \&n)/'
    's/ \&\& s->pad\[0\] == s->pad\[1\]) {/ \&\& 0) {/'
    's/if (lower((unsigned char)\*a) != lower((unsigned char)\*b)) return false;/if (*a != *b) return false;/'
    "s/text\[h\] == '#'/text[h] == '~'/"
    's/FA_WRITE | FA_OPEN_ALWAYS/FA_WRITE | FA_CREATE_ALWAYS/'
    's/if (fr == FR_OK \&\& put_n != len) fr = FR_DENIED;.*//'
    's/if (fr == FR_OK \&\& put_n == len \&\& f_size(\&file) > len) fr = f_truncate(\&file);//'
    's/^    if (!t.fit) {/    if (0) {/'
    's/sd_ok() ? CARD_NOT_FAT : CARD_NONE/CARD_NONE/'
    '/^bool card_save/,/^}/s/    if (state != CARD_OK) return false;//;/^bool card_lend/,/^}/s/    unmount();.*//'
    '/^int card_load/,/^}/s/    if (state != CARD_OK) {/    if (state == CARD_NONE) {/'
    '/^bool card_lend/,/^}/s/    unmount();.*//'
    's/if (armed == b || !msc_written()) return false;/return false;/'
    '/^bool card_save/,/^}/s/    check_gone();//'
    '/^int32_t tud_msc_read10_cb/,/^}/s/if (!lent || offset/if (offset/'
    '/^int32_t tud_msc_write10_cb/,/^}/s/if (!lent || offset/if (offset/'
    's/\*block_count = lent ? sd_sectors() : 0;/*block_count = sd_sectors();/'
    's/    attention = true;.*/    attention = false;/'
    's/if (load_eject \&\& !start) back(MSC_BACK_EJECTED);/if (!start) back(MSC_BACK_EJECTED);/'
    's/if (load_eject \&\& !start) back(MSC_BACK_EJECTED);/;/'
    's/} else if (now_ms - gone_ms >= GONE_MS) {/} else {/'
    's/} else if (now_ms - gone_ms >= GONE_MS) {/} else if (0) {/'
    's/    written = true;   \/\/ even if it fails.*//'
    's/|| !bufsize || bufsize % 512) return -1;/|| !bufsize) return -1;/'
    's/} else if (now - pending_ms >= SAVE_AFTER_MS) {/} else {/'
    's/    saved = \*s;   \/\/ written or not.*/    if (!ok) saved = *s;/'
    '/^static void card_got/,/^}/s/        settings_apply(\&s);//'
    's/        save_settings(\&s, "there wasn.t one");//'
    '/^static void settings_tick/,/^}/s/    if (card_state() != CARD_OK) {/    if (card_state() == CARD_NONE) {/'
    's/if (card_state() == CARD_OK \&\& !settings_same(\&s, \&saved)) save_settings(\&s, "before the Mac has it");//'
    's/        if (!tud_mounted()) {/        if (0) {/'
    '/case CARD_DO_BACK:/,/break;/s/            card_take_back();//'
    's/pf.pad_fx\[a\] < 0 \&\& s->pad\[a\] < SET_N_PADS/s->pad[a] < SET_N_PADS/'
    's/lcd_set_hz(SPEEDS_MHZ\[spi_i\] \* 1000000);/;/'
    's/    int why = msc_came_back();/    int why = MSC_BACK_NONE; (void)msc_came_back;/'
    's/    saved = \*s;   \/\/ written or not.*/    if (ok) saved = *s;/'
)
MF=(sd.c sd.c sd.c sd.c sd.c sd.c sd.c sd.c sd.c
    settings.c settings.c settings.c settings.c settings.c settings.c settings.c settings.c settings.c settings.c
    card.c card.c card.c card.c card.c card.c
    msc.c msc.c msc.c msc.c msc.c msc.c msc.c msc.c msc.c msc.c
    main.c main.c main.c main.c main.c main.c main.c main.c main.c main.c main.c main.c)
[[ ${#MN} == ${#ME} && ${#MN} == ${#MF} ]] || { print -r -- "FAIL: the breaks' lists differ in length"; exit 1 }
caught=0
for i in {1..${#MN}}; do
    rm -rf $O/b
    mkdir -p $O/b
    cp $V/${^OWN} $O/b/
    f=${MF[$i]}
    sed "${ME[$i]}" $V/$f > $O/b/$f
    if cmp -s $O/b/$f $V/$f; then
        print -r -- "FAIL: break $i didn't apply: ${MN[$i]}"
        exit 1
    fi
    cut_main $([[ $f == main.c ]] && print $O/b/main.c || print $V/main.c) $O/b/block.inc
    build $O/b $O/mut 2> $O/mut.err || { cat $O/mut.err; print -r -- "FAIL: break $i doesn't build: ${MN[$i]}"; exit 1 }
    if all $O/mut $O/mut.out; then
        print -r -- "FAIL: not caught: ${MN[$i]}"
    else
        caught=$((caught + 1))
        print -r -- "caught: ${MN[$i]} ($(grep -cE 'FAIL|ERROR' $O/mut.out) failures shown; first: $(grep -m1 -E 'FAIL|ERROR' $O/mut.out | cut -c1-110))"
    fi
done
print -r -- "breaks caught: $caught of ${#MN}"
[[ $caught == ${#MN} ]] || exit 1
rm -rf $O
