#!/bin/zsh
# Test 8's host checks (the effects page and the pad), on the Mac, under AddressSanitizer + UBSan.
#   zsh run.sh [--no-sweep] [DIR]     DIR: pictures of the two pages go there as PNG
#
#   the files test 8 shares with test 7 must be test 7's byte for byte (cal.c but for its title; lcd.c and main.c are
#     test 8's own), and then test 7's whole run.sh is run against test 8's folder: the paint, the pump cut out of
#     test 8's main.c, the FM-1 page, the knob drags, test 4's lcd and calibration checks, its 31 deliberate breaks
#   perf_check.c     units: the grid, the CCs, a tap's one toggle, ALL OFF, no FM-1, PERFORM left; the pad's ends,
#                    detent, glitches, axes, reset; the drawing decoded word by word and compared with a fresh redraw
#                    script: every CC the box sends for a run of touches played into AISLING's own ui.c in its host
#                    build (amb_host, built into a temp dir), which must then hold (expect pcc) or say (said) the same
#   deliberate breaks of perf.c, each to be caught
set -e
HERE=${0:A:h}
cd $HERE
V=../../viz-test8
V7=../../viz-test7
FM=${FM:-$HOME/Projects/fm1-aisling}
# amb_host needs AISLING's generated headers (build/gen); a tree whose font header predates 1.3 won't build it
if ! grep -qs FONT_US $FM/build/gen/felucca_font.h; then
    for alt in $HOME/Projects/aisling-drafts/rel131 $HOME/Projects/aisling-drafts/fw-1.3; do
        if grep -qs FONT_US $alt/build/gen/felucca_font.h; then FM=$alt; break; fi
    done
fi
export FM
O=$(mktemp -d)
F=(-std=gnu11 -g -O1 -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined -fno-sanitize-recover=undefined)
args=()
shots=
for a in "$@"; do
    if [[ $a == --no-sweep ]]; then args+=($a); else shots=$a; fi
done

print -r -- "-- the files shared with test 7"
for f in fm1scr.c fm1scr.h knobctl.c knobctl.h knobtab.h pflash.h land.c land.h tone.c tone.h reader.c reader.h \
         cal.h font5x7.h; do
    [[ -f $V7/$f ]] || continue
    cmp -s $V/$f $V7/$f || { print -r -- "FAIL: $f is not test 7's"; exit 1 }
done
sed 's/AISLING VIZ TEST 7: PANEL/AISLING VIZ TEST 8: PANEL/' $V7/cal.c | cmp -s - $V/cal.c || { print -r -- "FAIL: cal.c"; exit 1 }
print -r -- "the same"
TTF=$FM/assets/fonts/UncialAntiqua-Regular.ttf
if [[ -f $TTF ]] && python3 -c 'import PIL' 2> /dev/null; then
    python3 gen_ufont.py $TTF > $O/ufont.h
    cmp -s $O/ufont.h $V/ufont.h || { print -r -- "FAIL: ufont.h is not what gen_ufont.py makes"; exit 1 }
    print -r -- "ufont.h: as gen_ufont.py makes it"
else
    print -r -- "(no font file or no PIL here: ufont.h not made again)"
fi

print -r -- "-- test 7's run.sh, against test 8"
T7=${HERE:h}/test7
sed -e 's|^V=../../viz-test7$|V=../../viz-test8|' -e "s|^cd \"\${0:A:h}\"\$|cd $T7|" \
    -e 's/AISLING VIZ TEST 7/AISLING VIZ TEST 8/; s/title says test 7/title says test 8/' $T7/run.sh > $O/run7.sh
grep -q '^V=../../viz-test8$' $O/run7.sh && grep -q "^cd $T7\$" $O/run7.sh || { print -r -- "FAIL: test 7's run.sh has moved"; exit 1 }
zsh $O/run7.sh $args | sed 's/^/   /'
[[ ${pipestatus[1]} == 0 ]] || { print -r -- "FAIL: test 7's checks"; exit 1 }

pc() {   # $1 perf.c  $2 out
    clang $F -I../test4/stub -I$V perf_check.c $1 $V/knobctl.c $V/fm1scr.c $V/lcd.c $V/tone.c $V/reader.c \
        ../test4/stub.c -lm -o $2
}
print -r -- "-- the effects page and the pad"
pc $V/perf.c $O/pc
$O/pc units | grep -E "FAIL|units"
amb=
if [[ -f $FM/host/amb_host.c ]] && grep -qs FONT_US $FM/build/gen/felucca_font.h; then
    cc -D_GNU_SOURCE -O2 -ffp-contract=off -std=c99 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter \
        -DEN_HOST -I$FM/firmware/src -I$FM/firmware/src/dsp -I$FM/build/gen -o $O/amb_host $FM/host/amb_host.c \
        $FM/firmware/src/dsp/amb.c -lm 2> $O/amb_host.err || { cat $O/amb_host.err; exit 1 }
    amb=$O/amb_host
    mkdir -p $O/run
    $O/pc script $O/perf.amb
    $amb $O/perf.amb $O/run > $O/perf.txt 2> $O/perf.err || true
    print -r -- "through AISLING's ui.c ($FM): $(grep -c '^  ok' $O/perf.txt) held or said as sent, $(grep -c FAIL $O/perf.txt) not"
    if grep -q FAIL $O/perf.txt || ! grep -q 'ok store_writes == 0' $O/perf.txt; then grep FAIL $O/perf.txt | head; exit 1; fi
else
    print -r -- "(no AISLING 1.3 tree with build/gen: the loop through AISLING's ui.c left out)"
fi
if [[ -n $shots ]]; then
    mkdir -p $shots $O/shots
    $O/pc shots $O/shots
    for p in $O/shots/*.ppm; do sips -s format png $p --out $shots/${p:t:r}.png > /dev/null; done
    print -r -- "pictures in $shots"
fi

print -r -- "-- deliberate breaks, each to be caught"
MN=(
    "an effect held on the first reading of a touch (a glitch holds it)"
    "a held finger toggling again and again"
    "a tap with no FM-1 lighting its cell"
    "ALL OFF only for what the box holds (a DAW's left on)"
    "an effect's CC one out"
    "PERFORM left: the cells stay lit"
    "a second tap that doesn't let go"
    "a touch that did its thing, then lifted on a page button, flipping the page"
    "the pad: up is less"
    "the pad: no rest in the middle"
    "the pad: one far reading taken (a glitch)"
    "the pad: a value's place in its list sent, not its CC"
    "the pad: both axes on one knob"
    "the pad: reset leaves Chorus"
    "the pad: a touch begun on a button, slid on, moves the knobs"
    "the pad: the old cross's square left on the screen"
    "a cell lit but not drawn again"
    "the grid one row into the status row"
    "the pad's Effects button not drawn again when what's held changes"
)
ME=(
    's/c < 0 || p->fx_n < 2) return;/c < 0 || p->fx_n < 1) return;/'
    '/if (e >= 0) {/,/fx_set/s/p->fx_done = true;//'
    '/if (e >= 0) {/,/fx_set/s/if (!live) { p->n_refused++; return; }//'
    's/            else out(p, k, 0);//'
    's/k < 12 ? 20 + k : 102 + k - 12; }/k < 12 ? 20 + k : 103 + k - 12; }/'
    '/^void perf_gone(/,/^}/s/p->held\[k\] = 0, //'
    's/fx_set(p, e, !p->held\[e\]);/fx_set(p, e, true);/'
    's/if (!ok || done || c < 0) return PERF_STAY;/if (!ok || c < 0) return PERF_STAY;/'
    's/int d = y_axis ? PERF_BOTTOM - 1 - XY_MARGIN - px : px - XY_MARGIN;/int d = px - XY_MARGIN;/'
    's/abs(cc - 64) <= XY_DETENT/0/'
    's/<= XY_JUMP_PX/<= 9999/g'
    's/out(p, PFLASH_N + knob, knobctl_cc_for(idx, n));/out(p, PFLASH_N + knob, idx);/'
    's/if (k == p->axis\[1 - a\]) k = (k + 1) % PERF_PADS;//'
    's/for (int k = 0; k < PERF_PADS; k++) pad_send(p, k, k < 3/for (int k = 0; k < 3; k++) pad_send(p, k, k < 3/'
    's/if (!p->in_pad) {   /if (!p->in_pad \&\& x >= XY_PAD_W) {   /'
    's/if (p->cross\[0\] >= 0 \&\& p->cross\[1\] >= 0) lcd_fill_rect(p->cross\[0\] - 4, p->cross\[1\] - 4, 9, 9, C_PADBG);//'
    's/if (perf_fx_at(c) == k) p->fx_dirty |= 1u << c;/;/'
    's/^#define FX_CH   45/#define FX_CH   46/'
    '/^static void fx_set(/,/^}/s/p->xy_dirty |= XYD_BTN0 << XYB_FX;//'
)
MF=(perf.c perf.c perf.c perf.c perf.c perf.c perf.c perf.c perf.c perf.c perf.c perf.c perf.c perf.c perf.c perf.c perf.c perf.h perf.c)
caught=0
for i in {1..${#MN}}; do
    rm -rf $O/b
    mkdir -p $O/b
    cp $V/perf.c $V/perf.h $O/b/
    sed "${ME[$i]}" $V/${MF[$i]} > $O/b/${MF[$i]}
    if cmp -s $O/b/${MF[$i]} $V/${MF[$i]}; then
        print -r -- "FAIL: break $i didn't apply: ${MN[$i]}"
        exit 1
    fi
    clang $F -I../test4/stub -I$O/b -I$V perf_check.c $O/b/perf.c $V/knobctl.c $V/fm1scr.c $V/lcd.c $V/tone.c \
        $V/reader.c ../test4/stub.c -lm -o $O/mut 2> $O/mut.err || { cat $O/mut.err; exit 1 }
    : > $O/mut.out
    bad=
    $O/mut units >> $O/mut.out 2>&1 || bad=1
    if [[ -z $bad && -n $amb ]]; then
        $O/mut script $O/mut.amb >> $O/mut.out 2>&1 || bad=1
        $amb $O/mut.amb $O/run > $O/mut.txt 2> /dev/null || true
        grep FAIL $O/mut.txt >> $O/mut.out && bad=1
    fi
    if [[ -z $bad ]]; then
        print -r -- "FAIL: not caught: ${MN[$i]}"
    else
        caught=$((caught + 1))
        print -r -- "caught: ${MN[$i]} ($(grep -c 'FAIL' $O/mut.out) failures shown; first: $(grep -m1 'FAIL' $O/mut.out | cut -c1-110))"
    fi
done
print -r -- "breaks caught: $caught of ${#MN}"
rm -rf $O
[[ $caught == ${#MN} ]]
