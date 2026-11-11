#!/usr/bin/env python3
# Test 7: makes viz-test7/knobtab.h, each FM-1 knob's values in order (its lo to its hi) as AISLING 1.3 says them, so
# the box can step a knob through them and send the CC for each (AISLING's ui.c, cc_in: CC 16-19 set the page's
# four knobs, 0..127 across the knob's range).
#
#   gen_knobtab.py CORPUS FM1SCR_C UI_C AMB_C AMB_H OUT
#
# Where the lists come from:
#   - most knobs: test 6's corpus (fm1-aisling's speak_dump walks each knob lo..hi and says it, as a reader hears it);
#   - the macros (Filter, Wet, Energy): speak_dump doesn't walk them (its last "else" sets proj.leds), so the corpus
#     says "Filter Off" 101 times. Their values are ui.c's knob_text formula over knob_range's -50..50;
#   - Mode: its text is mode_name(), which reads proj.pool, and speak_dump leaves the pool alone ("Minor" 10 times).
#     Its list is MODE_ORDER's modes by EN_MODE_NAME (a CC sets MODE_ORDER[i]);
#   - Play (the Chords page): knob_get reads P_PMODE, not proj.leds, so the corpus says "Loop" twice. 0 Endless, 1 Loop;
#   - the knobs cc_in won't take (Setup's: Tune, MIDI out, Lights, Save; K_NONE, K_CPU): -1, not turned from the box.
# Anything else that says the same thing twice running in the corpus stops this, but Space's Feedback (whole tenths
# of a second: some values say the same) and what's named below.
#
# KT_MOVES marks the knobs the engine moves by itself after a hand's turn (amb.c's am_step: Filter, Wet and Energy are
# held only till the next section change, then glide back to Motion's; ui.c's s2_shift: Motion's shift of Sequence 2's
# Octave, shown again once the engine has the hand's), so the page the box asks for can show a step or two off what it
# sent although the FM-1 took it.
import re
import sys

corpus_p, fm1scr_p, ui_p, amb_c_p, amb_h_p, out_p = sys.argv[1:7]


def die(msg):
    sys.exit("gen_knobtab: " + msg)


def c_table(src, name):
    m = re.search(name + r'\[[^\]]*\](?:\[4\])?\s*=\s*\{(.*?)\n\};', src, re.S)
    if not m:
        die("no table " + name)
    return m.group(1)


fm1scr = open(fm1scr_p).read()
labels = [re.findall(r'"([^"]*)"', row) for row in re.findall(r'\{([^{}]*)\}', c_table(fm1scr, 'KNOB_LABEL'))]
names = re.findall(r'"([^"]*)"', c_table(fm1scr, 'VIEW_NAME'))
NV = len(names)
if NV != 27 or len(labels) != NV or any(len(l) != 4 for l in labels):
    die("fm1scr.c's tables aren't 27 pages of 4 knobs")

ui = open(ui_p).read()
# VIEW_KNOB: what each knob is (a parameter P_*, or one of ui.c's K_*)
m = re.search(r'VIEW_KNOB\[NVIEWS\]\[4\]\s*=\s*\{(.*?)\n\};', ui, re.S)
if not m:
    die("no VIEW_KNOB in ui.c")
body = re.sub(r'/\*.*?\*/', '', m.group(1), flags=re.S)
kinds = [[t.strip() for t in row.split(',')] for row in re.findall(r'\{([^{}]*)\}', body)]
if len(kinds) != NV or any(len(k) != 4 for k in kinds):
    die("VIEW_KNOB isn't 27 x 4")
# ui.c's labels must be fm1scr.c's (they were copied from it)
m = re.search(r'KNOB_LABEL\[NVIEWS\]\[4\]\s*=\s*\{(.*?)\n\};', ui, re.S)
ui_labels = [re.findall(r'"([^"]*)"', row) for row in re.findall(r'\{([^{}]*)\}', m.group(1))]
if ui_labels != labels:
    die("fm1scr.c's KNOB_LABEL isn't ui.c's")

# what cc_in leaves alone
m = re.search(r'static void cc_in\(.*?\n\}', ui, re.S)
skip = re.search(r'if \((k == K_NONE[^)]*)\)\s*\n\s*return;', m.group(0))
if not skip:
    die("cc_in's skip line has moved")
SKIP = set(re.findall(r'k == (\w+)', skip.group(1)))
if SKIP != {'K_NONE', 'K_CPU', 'K_SAVE', 'K_LEDS', 'P_TUNE', 'P_MIDI'}:
    die("cc_in skips something new: %s" % sorted(SKIP))
# the CC to index rule, as test 7's knobctl.c inverts it
if 'knob_set(k, lo + (v * (hi - lo) + 63) / 127);' not in m.group(0):
    die("cc_in's CC-to-value rule has changed")

# the modes in the knob's order
m = re.search(r'MODE_ORDER\[EN_NMODES\]\s*=\s*\{(.*?)\};', ui, re.S)
order = [t.strip() for t in m.group(1).split(',')]
amb_h = open(amb_h_p).read()
m = re.search(r'enum\s*\{\s*(MODE_IONIAN[^}]*)\}', amb_h, re.S)
enum = [t.strip() for t in re.sub(r'/\*.*?\*/', '', m.group(1), flags=re.S).split(',') if t.strip()]
enum = [e for e in enum if e != 'EN_NMODES']
amb_c = open(amb_c_p).read()
m = re.search(r'EN_MODE_NAME\[EN_NMODES\]\s*=\s*\{(.*?)\};', amb_c, re.S)
mode_names = re.findall(r'"([^"]*)"', m.group(1))
if len(enum) != len(mode_names) or len(order) != len(enum):
    die("the modes don't line up: %d %d %d" % (len(enum), len(mode_names), len(order)))
MODE_LIST = [mode_names[enum.index(o)] for o in order]


def macro_text(k, v):   # ui.c knob_text: K_FILTER, then K_DUB..K_SPACE
    if k == 'K_FILTER':
        return 'Off' if not v else ('Shut ' if v < 0 else 'Open ') + str(abs(v) * 2)
    return ('' if not v else '-' if v < 0 else '+') + str(abs(v) * 2)


# the corpus: each page's SCREEN, then its knobs' KNOB lines, knob 0's values lo..hi, then knob 1's, ...
walked = {}
view = -1
for line in open(corpus_p):
    kind, _, text = line.rstrip('\n').partition('\t')
    if kind == 'SCREEN':
        view += 1
        if not text.startswith(names[view] + ' page'):
            die("corpus page %d is %r, not %s" % (view, text[:30], names[view]))
        continue
    if kind != 'KNOB' or view >= NV:
        continue
    best, bl = -1, 0
    for i, l in enumerate(labels[view]):
        if text.startswith(l + ' ') and len(l) > bl:
            best, bl = i, len(l)
    if best < 0:
        die("corpus KNOB %r isn't one of %s's" % (text, names[view]))
    walked.setdefault((view, best), []).append(text[bl + 1:])
if view != NV - 1:
    die("the corpus has %d pages" % (view + 1))

MOVED = ('K_FILTER', 'K_DUB', 'K_ENERGY', 'P_SEQ2OCT')
m = re.search(r'#define AM_N (\d+)', amb_c)
if not m or m.group(1) != '3' or 'static int s2_shift(void) { return ui.s2_hand ? 0 : en_s2oct_shift; }' not in ui:
    die("what Motion moves by itself has changed (amb.c's AM_N, ui.c's s2_shift)")
REPEATS_OK = {(5, 2)}   # Space's Feedback: "1.5 s" twice, ...
lists, list_of = [], {}
KT = [[-1] * 4 for _ in range(NV)]
note = []
for v in range(NV):
    for i in range(4):
        k = kinds[v][i]
        got = walked.get((v, i))
        if got is None:
            die("the corpus doesn't walk %s's %s" % (names[v], labels[v][i]))
        if k in SKIP:
            note.append("%s's %s: %s, not from the box" % (names[v], labels[v][i], k))
            continue
        if k in ('K_FILTER', 'K_DUB', 'K_ENERGY', 'K_SPACE'):
            L = [macro_text(k, x) for x in range(-50, 51)]
            if len(got) != 101 or len(set(got)) != 1 or got[0] not in L:
                die("%s's %s: the corpus isn't 101 of one of its values" % (names[v], labels[v][i]))
        elif k == 'K_MODE':
            L = MODE_LIST
            if len(got) != len(L) or len(set(got)) != 1 or got[0] not in L:
                die("Mode: the corpus isn't %d of one mode" % len(L))
        elif k == 'K_CHORUS':   # (1.3) PERFORM's fourth knob: speak_dump doesn't walk it either. ui.c knob_text
            L = ['Off'] + [str(x) for x in range(1, 101)]
            if len(got) != 101 or len(set(got)) != 1 or got[0] not in L:
                die("Chorus: the corpus isn't 101 of one of its values")
        elif k == 'K_PLAY':
            L = ['Endless', 'Loop']
            if len(got) != 2 or len(set(got)) != 1 or got[0] not in L:
                die("Play: the corpus isn't 2 of Endless or Loop")
        else:
            L = got
            reps = [(a, j) for j, (a, b) in enumerate(zip(L, L[1:])) if a == b]
            if reps and (v, i) not in REPEATS_OK:
                die("%s's %s says %r twice running (index %d): not walked?" % (names[v], labels[v][i], reps[0][0],
                                                                                reps[0][1]))
        if not 2 <= len(L) <= 128:
            die("%s's %s has %d values: the box steps 2..128" % (names[v], labels[v][i], len(L)))
        for t in L:
            if not t or len(t) > 23 or not all(32 <= ord(c) < 127 and c not in '"\\' for c in t):
                die("%s's %s: %r won't do (fm1scr's value[24])" % (names[v], labels[v][i], t))
        key = tuple(L)
        if key not in list_of:
            list_of[key] = len(lists)
            lists.append(L)
        KT[v][i] = list_of[key]

out = ["// knobtab.h: each FM-1 knob's values in order, its lo to its hi, as AISLING 1.3 says them; the box steps a",
       "// knob through these and sends the CC for each (knobctl.c). Made by hosttests/test7/gen_knobtab.py from test 6's",
       "// corpus and fm1-aisling's ui.c, amb.c and amb.h (our own code): don't edit, run it again.",
       "#ifndef VIZ_KNOBTAB_H_INCLUDED",
       "#define VIZ_KNOBTAB_H_INCLUDED",
       "#include <stdint.h>",
       "",
       "typedef struct {",
       "    uint8_t n;                  // how many values (2..128)",
       "    const char *const *text;    // each as said, lo first",
       "} kt_list_t;",
       ""]
for j, L in enumerate(lists):
    body = ', '.join('"%s"' % t for t in L)
    out.append("static const char *const KT_L%d[%d] = {%s};" % (j, len(L), body))
out.append("")
out.append("#define KT_NLISTS %d" % len(lists))
out.append("static const kt_list_t KT_LISTS[KT_NLISTS] = {")
for j, L in enumerate(lists):
    out.append("    {%d, KT_L%d}," % (len(L), j))
out.append("};")
out.append("")
out.append("// KT_LIST[page][knob]: its list in KT_LISTS; -1: AISLING takes no CC for it (%s)" %
           ", ".join(sorted(SKIP)))
out.append("static const int8_t KT_LIST[%d][4] = {" % NV)
for v in range(NV):
    out.append("    {%s},   // %s" % (", ".join("%d" % x for x in KT[v]), names[v]))
out.append("};")
out.append("")
out.append("// KT_MOVES[page]: bit i, the engine moves knob i by itself after a turn (%s): Motion's" % ", ".join(MOVED))
out.append("static const uint8_t KT_MOVES[%d] = {" % NV)
for v in range(NV):
    bits = sum(1 << i for i in range(4) if kinds[v][i] in MOVED and KT[v][i] >= 0)
    out.append("    0x%X,   // %s%s" % (bits, names[v], (": " + ", ".join(labels[v][i] for i in range(4) if bits >> i & 1))
                                         if bits else ""))
out.append("};")
out.append("#endif")
open(out_p, 'w').write('\n'.join(out) + '\n')
n_texts = sum(len(L) for L in lists)
print("knobtab: %d knobs from the box in %d lists (%d values); %s" %
      (sum(x >= 0 for r in KT for x in r), len(lists), n_texts, "; ".join(note)))
