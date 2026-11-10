// Test 7's checks of knobctl.c: the box turning the FM-1's knobs (CC 16-19), with fm1scr.c's model of the FM-1's
// screen, and against AISLING 1.2's real ui.c through fm1-aisling's own host build (amb_host: run.sh builds it into a
// temp dir and runs these scripts through it).
//
//   knobctl_check units                  knobctl.c on its own: the threshold, the steps, the ends, the jitter, the
//                                        glitches, taps and releases, the page changing, the answers and the
//                                        time-outs (across the clock's wrap), the knobs it can't turn; then random
//                                        touch soup, and the table
//   knobctl_check sweep-script OUT       every CC 0..127 to each knob of the 26 pages the buttons reach: a script
//   knobctl_check sweep-check AMB_OUT    ... what AISLING said back, through fm1scr.c: knobtab.h's text each time
//   knobctl_check plan CORPUS OUT        drags on 8 pages through knobctl.c, with a stand-in FM-1: their CCs, a script
//   knobctl_check loop AMB_OUT           the same drags again, with what AISLING really said: the same CCs, the
//                                        FM-1 ends where the box last sent, and the box shows it
//
// Exits 1 on any failure.
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lcd.h"
#include "fm1scr.h"
#include "knobctl.h"
#include "knobtab.h"

static int checks, fails;
#define CHECK(c, ...)                                                                                                 \
    do {                                                                                                              \
        checks++;                                                                                                     \
        if (!(c)) {                                                                                                   \
            if (++fails <= 40) {                                                                                      \
                printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                           \
                printf(__VA_ARGS__);                                                                                  \
                printf("\n");                                                                                         \
            }                                                                                                         \
        }                                                                                                             \
    } while (0)

// fm1scr.c draws through these: counted here, not drawn (fm1scr_check.c checks its pixels)
static long n_fill, n_text;
void lcd_fill_rect(int x, int y, int w, int h, uint16_t colour) {
    (void)x, (void)y, (void)w, (void)h, (void)colour;
    n_fill++;
}
void lcd_text(int x, int y, const char *s, int scale, uint16_t fg, uint16_t bg) {
    (void)x, (void)y, (void)s, (void)scale, (void)fg, (void)bg;
    n_text++;
}

enum { V_AISLING = 0, V_SPACE = 5, V_SETUP = 6, V_CHORDS = 15, V_PATTERN = 16, V_PERFORM = 17, V_KEY = 18 };

static fm1scr_t M;
static knobctl_t K;
static uint32_t now;

static void say(const char *t, uint8_t flags) { fm1scr_say(&M, t, (uint32_t)strlen(t), flags, now); }
static void ticks(void) {   // main.c's order after the touch: fm1scr_tick, knobctl_tick, then the drawing
    fm1scr_tick(&M, now);
    knobctl_tick(&K, &M, now);
    fm1scr_draw(&M, 2, now);
}
static void fresh(uint32_t t0) {
    fm1scr_init(&M);
    knobctl_init(&K);
    now = t0;
}
static int nlist(int view, int i) { return knobctl_list_n(knobctl_list_of(view, i)); }

static uint32_t rng = 0x7e57u;
static uint32_t rnd(void) {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}
static int rnd_in(int lo, int hi) { return lo + (int)(rnd() % (uint32_t)(hi - lo + 1)); }

// ---------------------------------------------------------------------------------------------------- the table

static void table_checks(void) {
    for (int l = 0; l < KT_NLISTS; l++) {
        int n = KT_LISTS[l].n;
        CHECK(n >= 2 && n <= 128, "list %d has %d values", l, n);
        int prev = -1;
        for (int j = 0; j < n; j++) {
            const char *t = knobctl_text(l, j);
            CHECK(t && t[0] && strlen(t) < sizeof M.value[0], "list %d value %d won't fit", l, j);
            int cc = knobctl_cc_for(j, n);
            CHECK(cc >= 0 && cc <= 127, "list %d: CC for %d is %d", l, j, cc);
            CHECK(knobctl_idx_of(cc, n) == j, "list %d: CC %d for %d is %d to AISLING", l, cc, j, knobctl_idx_of(cc, n));
            CHECK(cc > prev, "list %d: the CCs aren't rising at %d", l, j);
            prev = cc;
        }
        CHECK(knobctl_idx_of(0, n) == 0 && knobctl_idx_of(127, n) == n - 1, "list %d: CC 0 and 127 aren't its ends", l);
        for (int v = 0; v < 128; v++) {
            int a = knobctl_idx_of(v, n);
            CHECK(a >= 0 && a < n, "list %d: CC %d is %d", l, v, a);
            if (v) CHECK(a >= knobctl_idx_of(v - 1, n), "list %d: CC %d goes back", l, v);
        }
        CHECK(knobctl_text(l, n) == NULL && knobctl_text(l, -1) == NULL, "list %d: text out of range", l);
        int s = knobctl_step_px(n);
        CHECK(s >= KC_STEP_MIN && s <= KC_STEP_MAX, "list %d: step %d", l, s);
    }
    CHECK(knobctl_text(-1, 0) == NULL && knobctl_text(KT_NLISTS, 0) == NULL, "a list out of range has text");
    for (int v = 0; v < FM1_VIEWS; v++)
        for (int i = 0; i < 4; i++) {
            int l = knobctl_list_of(v, i);
            CHECK(l >= -1 && l < KT_NLISTS, "%s knob %d: list %d", fm1scr_view_name(v), i, l);
            CHECK(fm1scr_label(v, i) && fm1scr_label(v, i)[0], "%s knob %d: no label", fm1scr_view_name(v), i);
            if (v == V_SETUP) CHECK(l < 0, "Setup's %s can be turned from the box", fm1scr_label(v, i));
            else CHECK(l >= 0, "%s's %s can't be turned from the box", fm1scr_view_name(v), fm1scr_label(v, i));
        }
    CHECK(knobctl_list_of(-1, 0) == -1 && knobctl_list_of(FM1_VIEWS, 0) == -1 && knobctl_list_of(0, 4) == -1,
          "list_of out of range");
    // the step: ~300 px for the whole range, 3..24 px
    CHECK(knobctl_step_px(101) == 3 && knobctl_step_px(7) == 24 && knobctl_step_px(2) == 24 &&
              knobctl_step_px(128) == 3 && knobctl_step_px(14) == 23 && knobctl_step_px(65) == 4,
          "the steps");
}

// ---------------------------------------------------------------------------------------------------- the units

static const char *AISLING_LINE = "Aisling page: Filter Off, Evolve 40, Motion 40, Drift 16 bars";
static const char *DRONE_LINE = "Drone page, voice Pipes: Pedal 90, Reso 0, Phaser 15, Attack 40";
static const char *SETUP_LINE = "Setup page, sleep off: Tune 0, MIDI out On, Lights On, Save Saved";
static const char *SPACE_LINE = "Space page: Echo 55, Echo time 1/4., Feedback 6.0 s, Reverb 55";

static unsigned touch(int x, int y, bool first) {
    unsigned m = knobctl_touch(&K, &M, x, y, first, now);
    ticks();
    return m;
}
static void later(uint32_t ms) {
    for (uint32_t e = 0; e < ms; e += 10) {
        now += ms - e < 10 ? ms - e : 10;
        ticks();
    }
}
static void page(const char *line) {
    say(line, FM1_SAY_FLAG_NOW);
    ticks();
}
// a drag on knob i from x0 by dx, one px a reading; the knobs sent each step OR'd
static unsigned slide(int x0, int dx, int y) {
    unsigned all = 0;
    int d = dx < 0 ? -1 : 1;
    for (int k = 1; k <= abs(dx); k++) {
        now += 4;
        all |= touch(x0 + d * k, y, false);
    }
    return all;
}

static void unit_threshold(void) {
    fresh(1000);
    page(AISLING_LINE);
    int idx;
    CHECK(knobctl_turnable(&K, &M, 0, &idx) >= 0 && idx == 50, "Filter Off isn't index 50 (%d)", idx);
    CHECK(touch(60, 110, true) == 0 && !K.dragging && K.in_row, "a touch in the row");
    CHECK(slide(60, 9, 110) == 0 && !K.dragging, "9 px is a drag");
    CHECK(touch(70, 110, false) == 0 && K.dragging && K.base == 50 && K.at == 50, "10 px isn't a drag");
    CHECK(touch(72, 110, false) == 0, "less than a step after the start sends");
    unsigned m = touch(73, 110, false);
    CHECK(m == 1u && K.from[0] == 50 && K.to[0] == 51, "a step after the start: %u, %d to %d", m, K.from[0], K.to[0]);
    CHECK(K.cc[0] == knobctl_cc_for(51, 101) && knobctl_idx_of(K.cc[0], 101) == 51, "the CC for 51 is %u", K.cc[0]);
    CHECK(M.pv[0] && !strcmp(M.pv[0], "Open 2") && M.pv_idx[0] == 51 && M.pv_n[0] == 101, "the preview mid-drag");
    // half a step back isn't a step back; a whole one is
    CHECK(touch(72, 110, false) == 0 && touch(71, 110, false) == 0, "less than a step back sends");
    m = touch(70, 110, false);
    CHECK(m == 1u && K.from[0] == 51 && K.to[0] == 50, "a step back: %u, %d to %d", m, K.from[0], K.to[0]);
    // left: one step each 3 px, never two at once at a px a reading
    int sends = 0, last = 50;
    for (int x = 69; x >= 40; x--) {
        now += 4;
        if (touch(x, 110, false)) {
            sends++;
            CHECK(K.from[0] == last && K.to[0] == last - 1, "left: %d to %d after %d", K.from[0], K.to[0], last);
            last = K.to[0];
        }
    }
    CHECK(sends == 10 && last == 40, "30 px left of the start is 10 steps (%d, at %d)", sends, last);
    CHECK(knobctl_release(&K, &M, now), "a drag's release isn't the knobs'");
    CHECK(K.n_sent == 12 && K.n_drags == 1, "counts %u %u", K.n_sent, K.n_drags);
    // the hold-still case: x0 is the reading the drag started at, not the touch
    fresh(1000);
    page(AISLING_LINE);
    touch(60, 110, true);
    touch(48, 110, false);   // 12 px left: the drag starts here, nothing sent
    CHECK(K.dragging && K.x0 == 48 && K.n_sent == 0, "the drag's start");
    CHECK(touch(46, 110, false) == 0 && touch(45, 110, false) == 1u && K.to[0] == 49, "a step left of the start");
    knobctl_release(&K, &M, now);
}

static void unit_jitter(void) {
    // a finger held still: a resistive panel's jitter within a step never sends (Drift: 7 values, 24 px each)
    fresh(1000);
    page(AISLING_LINE);
    CHECK(knobctl_step_px(nlist(V_AISLING, 3)) == 24, "Drift's step");
    touch(370, 110, true);
    touch(380, 110, false);
    unsigned m = slide(380, 24, 110);
    CHECK(m == 8u && K.to[3] == 4 && K.n_sent == 1, "Drift a step right: %u %d", m, K.to[3]);
    uint32_t sent = K.n_sent;
    for (int k = 0; k < 400; k++) {
        now += 5;
        touch(404 + rnd_in(-23, 23), 110 + rnd_in(-20, 20), false);
    }
    CHECK(K.n_sent == sent && K.dragging, "jitter within a step sent %u", K.n_sent - sent);
    knobctl_release(&K, &M, now);
    // and the finest: Filter, 3 px a step, jitter within 2 px
    page(AISLING_LINE);
    later(2000);
    touch(60, 110, true);
    slide(60, 16, 110);   // 6 px past the drag's start: 2 steps
    sent = K.n_sent;
    for (int k = 0; k < 400; k++) {
        now += 5;
        touch(76 + rnd_in(-2, 2), 110, false);
    }
    CHECK(K.n_sent == sent, "jitter within 2 px of a 3 px step sent %u", K.n_sent - sent);
    knobctl_release(&K, &M, now);
}

static void unit_ends(void) {
    fresh(1000);
    page(AISLING_LINE);   // Drift: 16 bars, index 3 of 7
    touch(370, 110, true);
    touch(380, 110, false);
    int sends = 0, top = -1;
    for (int x = 381; x <= 479; x++) {
        now += 4;
        if (touch(x, 110, false)) sends++, top = K.to[3];
    }
    CHECK(sends == 3 && top == 6 && K.at == 6, "right past the end: %d sends, at %d", sends, K.at);
    // coming back moves it straight away: the end is where the finger is now, not 30 px back
    int s = 0;
    for (int x = 478; x >= 456; x--) {
        now += 4;
        s += touch(x, 110, false) != 0;
    }
    CHECK(s == 0 && K.at == 6, "within a step of the end");
    CHECK(touch(455, 110, false) == 8u && K.from[3] == 6 && K.to[3] == 5, "a step back from the end");
    // and the bottom
    sends = 0;
    int bottom = -1;
    for (int x = 454; x >= 200; x--) {
        now += 4;
        if (touch(x, 110, false)) sends++, bottom = K.to[3];
    }
    CHECK(sends == 5 && bottom == 0 && K.at == 0, "left past the start: %d sends, at %d", sends, K.at);
    for (int x = 201; x <= 223; x++) {
        now += 4;
        CHECK(touch(x, 110, false) == 0, "within a step of the bottom sends");
    }
    CHECK(touch(224, 110, false) == 8u && K.to[3] == 1, "a step up from the bottom");
    knobctl_release(&K, &M, now);
}

static void unit_glitch(void) {
    fresh(1000);
    page(AISLING_LINE);
    touch(60, 110, true);
    slide(60, 13, 110);   // the drag started at 70, sent 51 at 73
    CHECK(K.at == 51, "the glitch drag's start");
    uint32_t g = K.n_glitches, sent = K.n_sent;
    CHECK(touch(180, 110, false) == 0 && K.n_glitches == g + 1, "a reading 107 px away is taken");
    CHECK(touch(74, 110, false) == 0 && K.at == 51, "back from a glitch sends");
    CHECK(touch(300, 110, false) == 0 && touch(75, 110, false) == 0 && K.n_sent == sent, "two glitches apart");
    // two far readings that agree: the finger moved
    CHECK(touch(160, 110, false) == 0, "a far reading");
    unsigned m = touch(170, 110, false);
    CHECK(m == 1u && K.to[0] == 50 + (170 - 70 + 1) / 3 && K.from[0] == 51, "two far readings that agree: %u to %d", m,
          K.to[0]);
    knobctl_release(&K, &M, now);
}

static void unit_taps(void) {
    // a tap on a knob the box can turn is the knobs': no page flip, no hold
    fresh(1000);
    page(AISLING_LINE);
    touch(60, 110, true);
    for (int k = 0; k < 150; k++) now += 20, touch(60 + rnd_in(-3, 3), 110, false);   // 3 s still
    CHECK(knobctl_release(&K, &M, now) && K.n_sent == 0, "a held tap on a knob isn't the knobs'");
    // the row's edges
    touch(60, 79, true);
    CHECK(!knobctl_release(&K, &M, now), "y 79 is in the row");
    touch(60, 80, true);
    CHECK(knobctl_release(&K, &M, now), "y 80 isn't in the row");
    touch(60, 141, true);
    CHECK(knobctl_release(&K, &M, now), "y 141 isn't in the row");
    touch(60, 142, true);
    CHECK(!knobctl_release(&K, &M, now), "y 142 is in the row");
    // outside the row, a drag is nobody's here (main.c: a tap or a hold)
    touch(60, 200, true);
    slide(60, 100, 200);
    CHECK(!knobctl_release(&K, &M, now) && K.n_sent == 0, "a drag below the row");
    // started in the row and left it: still the knobs'
    touch(60, 110, true);
    slide(60, 30, 110);
    uint32_t sent = K.n_sent;
    CHECK(sent > 0, "the drag sent nothing");
    for (int y = 110; y < 260; y += 5) now += 4, touch(90, y, false);
    CHECK(K.n_sent == sent && knobctl_release(&K, &M, now), "a drag that left the row");
    // a knob it can't turn: a tap is a tap (page flip, or a hold to calibrate), a drag is the knobs'
    page(SETUP_LINE);
    uint32_t locked = K.n_locked;
    touch(60, 110, true);
    for (int k = 0; k < 120; k++) now += 20, touch(60 + rnd_in(-4, 4), 110, false);
    CHECK(!knobctl_release(&K, &M, now) && K.n_locked == locked + 1, "a still tap on Setup's Tune");
    touch(300, 110, true);
    touch(309, 110, false);
    CHECK(!knobctl_release(&K, &M, now), "9 px on a dimmed knob is a drag");
    touch(300, 110, true);
    touch(310, 110, false);
    CHECK(knobctl_release(&K, &M, now) && K.n_sent == sent, "a drag on a dimmed knob");
    // nothing heard yet: every knob is a tap
    fresh(1000);
    ticks();
    CHECK(M.locked == 0, "nothing heard, nothing dimmed");
    touch(60, 110, true);
    CHECK(!knobctl_release(&K, &M, now), "a tap before the FM-1 said anything");
    // a knob it can turn: a still tap is the knobs'; an empty one: a tap
    page("Mix page: Drone 38, Seq 44, Lead 76, Seq 2 0");
    CHECK(knobctl_turnable(&K, &M, 3, NULL) >= 0, "Mix's Seq 2");
    touch(420, 110, true);
    CHECK(knobctl_release(&K, &M, now), "a tap on Mix's Seq 2");
    page("Mix page: Drone 38, Seq 44, Lead 76");
    if (!M.value[3][0]) {
        touch(420, 110, true);
        CHECK(!knobctl_release(&K, &M, now), "a tap on Mix's empty Seq 2");
    }
}

static void unit_page_change(void) {
    fresh(1000);
    page(AISLING_LINE);
    touch(60, 110, true);
    slide(60, 16, 110);
    CHECK(K.dragging && K.n_sent == 2, "the drag before the page changed");
    page(DRONE_LINE);   // the FM-1's page changed under the finger (ticks cancels it)
    CHECK(!K.dragging && K.dead && K.n_cancelled == 1, "a page change doesn't cancel the drag");
    CHECK(!K.w[0].waiting && M.pv[0] == NULL, "the wait outlives the page");
    CHECK(slide(76, 100, 110) == 0, "a cancelled drag sends");
    CHECK(knobctl_release(&K, &M, now), "a cancelled drag's release");
    // the page changing between two readings, with no tick between
    page(AISLING_LINE);
    touch(60, 110, true);
    slide(60, 16, 110);
    say(DRONE_LINE, FM1_SAY_FLAG_NOW);
    CHECK(knobctl_touch(&K, &M, 80, 110, false, now) == 0 && K.dead && K.n_cancelled == 2, "between readings");
    knobctl_release(&K, &M, now);
    // the box's reset (the landscape came up, the FM-1 unplugged) mid-drag
    page(AISLING_LINE);
    touch(60, 110, true);
    slide(60, 16, 110);
    knobctl_reset(&K, &M);
    CHECK(M.pv[0] == NULL && !K.dragging && K.dead, "the reset");
    CHECK(slide(76, 60, 110) == 0, "a drag after the reset sends");
    knobctl_release(&K, &M, now);
    // Mode going Custom under the finger
    page("Key page: Tempo 76, Root D, Mode Minor, Transpose Parallel");
    uint32_t sent = K.n_sent, cancelled = K.n_cancelled;
    touch(300, 110, true);
    slide(300, 40, 110);   // the drag starts at 310: Major at 334
    CHECK(K.dragging && K.n_sent == sent + 1 && K.to[2] == 1, "the Mode drag");
    sent = K.n_sent;
    say("MODE Custom", FM1_SAY_FLAG_NOW);
    ticks();
    CHECK(!K.dragging && K.n_cancelled == cancelled + 1 && M.pv[2] && !strcmp(M.pv[2], "Major"),
          "Mode Custom mid-drag: the drag goes, the send waits");
    CHECK(slide(340, 50, 110) == 0 && K.n_sent == sent, "Mode Custom mid-drag sent");
    knobctl_release(&K, &M, now);
}

// a second drag before the FM-1 has said the first back: from what was sent, not from the FM-1's value
static void unit_second_drag(void) {
    fresh(1000);
    page(AISLING_LINE);
    touch(60, 110, true);
    slide(60, 13, 110);   // Off to Open 2
    knobctl_release(&K, &M, now);
    CHECK(K.w[0].waiting && K.to[0] == 51, "the first drag: %d", K.to[0]);
    later(50);
    touch(60, 110, true);
    slide(60, 13, 110);
    CHECK(K.base == 51 && K.from[0] == 51 && K.to[0] == 52 && M.pv[0] && !strcmp(M.pv[0], "Open 4"),
          "the second drag before the answer: from %d, %d to %d", K.base, K.from[0], K.to[0]);
    knobctl_release(&K, &M, now);
    later(100);
    say("Filter Open 2", FM1_SAY_FLAG_NOW);   // the first said back: the second still waited for
    ticks();
    CHECK(K.w[0].waiting && K.w[0].idx == 52 && !strcmp(M.pv[0], "Open 4"), "the first's answer ended the second's wait");
    say("Filter Open 4", FM1_SAY_FLAG_NOW);
    ticks();
    CHECK(!K.w[0].waiting && !M.pv[0] && K.n_not_taken == 0, "the second's answer");
}

static void unit_answers(void) {
    // said back: answered, the preview goes
    fresh(1000);
    page(AISLING_LINE);
    touch(60, 110, true);
    slide(60, 13, 110);
    knobctl_release(&K, &M, now);
    CHECK(K.w[0].waiting && M.pv[0] && !strcmp(M.pv[0], "Open 2"), "the wait after the release");
    later(120);
    say("Filter Open 2", FM1_SAY_FLAG_NOW);
    ticks();
    CHECK(!K.w[0].waiting && K.n_answered == 1 && M.pv[0] == NULL && !strcmp(M.value[0], "Open 2"), "the answer");
    later(3000);
    CHECK(K.n_not_taken == 0 && M.msg[0] == 0 && K.n_asked == 0 && !K.want_where, "a time-out after the answer");
    // said back, but not what was sent: still waiting, then the FM-1's own back
    touch(60, 110, true);
    slide(60, 13, 110);   // 52, Open 4
    knobctl_release(&K, &M, now);
    later(100);
    say("Filter Open 2", FM1_SAY_FLAG_NOW);
    ticks();
    CHECK(K.w[0].waiting && M.pv[0] && !strcmp(M.pv[0], "Open 4"), "answered by another value");
    later(1500);
    CHECK(!K.w[0].waiting && K.n_not_taken == 1 && M.pv[0] == NULL && !strcmp(M.value[0], "Open 2"),
          "no answer: the FM-1's own");
    CHECK(M.warn && !strcmp(M.msg, "THE FM-1 DIDN'T TAKE IT"), "the warning: %s", M.msg);
    CHECK(K.n_asked == 1 && K.want_where && K.n_paged == 0, "the WHERE asked before the time-out: %u", K.n_asked);
    K.want_where = false;   // (main.c sends it)
    // not said back in 300 ms: the box asks for the page, and the page settles it
    later(4100);   // (the warning gone)
    uint32_t asked = K.n_asked, answered = K.n_answered, nt = K.n_not_taken;
    touch(60, 110, true);
    slide(60, -13, 110);   // Open 2 to Off
    knobctl_release(&K, &M, now);
    uint32_t at = K.w[0].ms;
    CHECK(K.w[0].waiting && K.to[0] == 50, "the drag to Off: %d", K.to[0]);
    while (now - at < KC_ASK_MS - 1) now++, ticks();
    CHECK(K.w[0].waiting && !K.w[0].asked && !K.want_where && K.n_asked == asked, "asked before 300 ms");
    page("Aisling page: Filter Open 2, Evolve 40, Motion 40, Drift 16 bars");   // a page before the ask
    CHECK(K.w[0].waiting && K.n_not_taken == nt && M.pv[0], "a page before the ask settled it");
    now++;
    ticks();
    CHECK(K.w[0].asked && K.want_where && K.n_asked == asked + 1, "no ask at %u ms", now - at);
    K.want_where = false;
    say("Evolve 41", FM1_SAY_FLAG_NOW);   // another knob said: not it
    later(100);
    CHECK(K.w[0].waiting && !K.want_where && K.n_asked == asked + 1, "asked twice, or settled by another knob");
    page(AISLING_LINE);   // Filter Off
    CHECK(!K.w[0].waiting && K.n_answered == answered + 1 && K.n_paged == 1 && M.pv[0] == NULL && M.msg[0] == 0 &&
              !strcmp(M.value[0], "Off"),
          "the page with the value sent didn't settle it");
    int idx0;
    knobctl_turnable(&K, &M, 0, &idx0);
    CHECK(idx0 == 50, "Off at %d", idx0);
    // the page with another value: not taken, then and there
    touch(60, 110, true);
    slide(60, 13, 110);   // Off to Open 2
    knobctl_release(&K, &M, now);
    at = K.w[0].ms;
    later(KC_ASK_MS + 10);
    CHECK(K.want_where && K.w[0].asked, "no ask for Open 2");
    K.want_where = false;
    later(60);
    page(AISLING_LINE);   // still Off
    CHECK(!K.w[0].waiting && K.n_not_taken == nt + 1 && K.n_paged == 2 && M.pv[0] == NULL &&
              !strcmp(M.value[0], "Off") && now - at < KC_ANSWER_MS,
          "the page with another value didn't settle it");
    CHECK(M.warn && !strcmp(M.msg, "THE FM-1 DIDN'T TAKE IT"), "no warning from the page: %s", M.msg);
    // asked, then the FM-1 on another page: forgotten, neither
    later(4100);
    touch(60, 110, true);
    slide(60, 13, 110);
    knobctl_release(&K, &M, now);
    later(KC_ASK_MS + 10);
    K.want_where = false;
    page(DRONE_LINE);
    CHECK(!K.w[0].waiting && K.n_not_taken == nt + 1 && K.n_answered == answered + 1 && M.msg[0] == 0,
          "asked, then another page");
    later(3000);
    CHECK(!K.want_where && K.n_paged == 2, "an ask after the page went");
    // Motion's knob (Filter): five steps, Off to Open 10, and the page a step back (the hand's turn let go at a
    // section change, gliding back): taken, then moved; no warning
    later(4100);
    page(AISLING_LINE);
    answered = K.n_answered, nt = K.n_not_taken;
    touch(60, 110, true);
    slide(60, 10 + 5 * knobctl_step_px(101), 110);
    knobctl_release(&K, &M, now);
    CHECK(K.w[0].waiting && K.w[0].idx == 55 && K.w[0].before == 50, "Filter to Open 10: %d from %d", K.w[0].idx,
          K.w[0].before);
    later(KC_ASK_MS + 10);
    CHECK(K.want_where, "no ask for Open 10");
    K.want_where = false;
    page("Aisling page: Filter Open 8, Evolve 40, Motion 40, Drift 16 bars");
    CHECK(!K.w[0].waiting && K.n_answered == answered + 1 && K.n_moved == 1 && K.n_not_taken == nt && M.msg[0] == 0 &&
              M.pv[0] == NULL && !strcmp(M.value[0], "Open 8"),
          "Motion's step after the turn: answered %u, moved %u, not taken %u, %s", K.n_answered - answered, K.n_moved,
          K.n_not_taken - nt, M.msg);
    knobctl_turnable(&K, &M, 0, &idx0);
    CHECK(idx0 == 54, "Open 8 at %d", idx0);
    // ... back where it was before the drag: not taken
    later(100);
    page(AISLING_LINE);
    touch(60, 110, true);
    slide(60, 10 + 5 * knobctl_step_px(101), 110);
    knobctl_release(&K, &M, now);
    later(KC_ASK_MS + 10);
    K.want_where = false;
    page("Aisling page: Filter Open 2, Evolve 40, Motion 40, Drift 16 bars");   // 51: nearer Off than Open 10
    CHECK(!K.w[0].waiting && K.n_not_taken == nt + 1 && K.n_moved == 1 && M.warn, "Filter back near where it was");
    // a knob Motion doesn't move (Evolve), a step off: not taken
    later(4100);
    page(AISLING_LINE);
    int sev = knobctl_step_px(nlist(V_AISLING, 1));
    touch(180, 110, true);
    slide(180, 10 + 5 * sev, 110);
    knobctl_release(&K, &M, now);
    CHECK(K.w[1].waiting && !strcmp(knobctl_text(K.w[1].list, K.w[1].idx), "45"), "Evolve to 45: %s",
          knobctl_text(K.w[1].list, K.w[1].idx));
    later(KC_ASK_MS + 10);
    K.want_where = false;
    page("Aisling page: Filter Off, Evolve 44, Motion 40, Drift 16 bars");
    CHECK(!K.w[1].waiting && K.n_not_taken == nt + 2 && K.n_moved == 1 && M.warn, "Evolve a step off: not taken");
    // Mode said back without its full stop ("Mode Mixolyd." on the page, "MODE Mixolyd" said): answered by the say
    later(4100);
    page("Key page: Tempo 160, Root B, Mode Minor, Transpose Parallel. Piece Skellig, B Minor, playing");
    int lm = knobctl_list_of(V_KEY, 2), im, jx = knobctl_find(lm, "Mixolyd.", -1);
    CHECK(knobctl_turnable(&K, &M, 2, &im) == lm && im >= 0 && jx >= 0 && knobctl_find(lm, "Mixolyd", -1) == jx,
          "Mode's list: Minor %d, Mixolyd. %d", im, jx);
    int sm = knobctl_step_px(knobctl_list_n(lm));
    answered = K.n_answered;
    uint32_t asked2 = K.n_asked;
    touch(300, 110, true);
    slide(300, (jx > im ? 1 : -1) * (10 + abs(jx - im) * sm), 110);
    knobctl_release(&K, &M, now);
    CHECK(K.w[2].waiting && K.w[2].idx == jx, "Mode to Mixolyd.: %d", K.w[2].idx);
    later(120);
    say("MODE Mixolyd", FM1_SAY_FLAG_NOW);
    ticks();
    CHECK(!K.w[2].waiting && K.n_answered == answered + 1 && K.n_asked == asked2 && !strcmp(M.value[2], "Mixolyd") &&
              !(M.locked & 4),
          "MODE Mixolyd didn't answer Mixolyd.: waiting %d, value %s", K.w[2].waiting, M.value[2]);
    knobctl_turnable(&K, &M, 2, &im);
    CHECK(im == jx, "Mixolyd at %d", im);
    // ... but Echo time's 1/4 and 1/4. are two values, each said as it is
    int le = knobctl_list_of(V_SPACE, 1), q = knobctl_find(le, "1/4", -1), qd = knobctl_find(le, "1/4.", -1);
    CHECK(q >= 0 && qd == q + 1 && knobctl_find(le, "1/4", qd) == q && knobctl_find(le, "1/4.", q) == qd &&
              knobctl_find(le, "1/4..", -1) >= 0 && knobctl_find(le, "1/3", -1) < 0 && knobctl_find(le, NULL, 0) < 0 &&
              knobctl_find(lm, "Mixoly", -1) < 0,
          "knobctl_find's full stops: 1/4 %d, 1/4. %d", q, qd);
    later(4100);
    page(SPACE_LINE);   // Echo time 1/4.
    int se = knobctl_step_px(knobctl_list_n(le));
    answered = K.n_answered;
    touch(130, 110, true);
    slide(130, -(10 + se), 110);   // to 1/4
    knobctl_release(&K, &M, now);
    CHECK(K.w[1].waiting && K.w[1].idx == q, "Echo time to 1/4: %d", K.w[1].idx);
    later(100);
    say("Echo time 1/4", FM1_SAY_FLAG_NOW);
    ticks();
    CHECK(!K.w[1].waiting && K.n_answered == answered + 1, "Echo time 1/4 not answered");
    knobctl_turnable(&K, &M, 1, &im);
    CHECK(im == q, "1/4 at %d", im);
    touch(130, 110, true);
    slide(130, 10 + se, 110);   // back to 1/4.
    knobctl_release(&K, &M, now);
    CHECK(K.w[1].waiting && K.w[1].idx == qd, "Echo time to 1/4.: %d", K.w[1].idx);
    later(100);
    say("Echo time 1/4", FM1_SAY_FLAG_NOW);   // (not what was sent)
    ticks();
    CHECK(K.w[1].waiting && K.n_answered == answered + 1, "1/4 answered 1/4.");
    say("Echo time 1/4.", FM1_SAY_FLAG_NOW);
    ticks();
    CHECK(!K.w[1].waiting && K.n_answered == answered + 2, "1/4. not answered");
    // two values that say the same (Space's Feedback): the one sent, not its twin
    page(SPACE_LINE);
    int l = knobctl_list_of(V_SPACE, 2), n = knobctl_list_n(l), twin = -1;
#define T(j) knobctl_text(l, j)
    for (int j = 2; j + 2 < n && twin < 0; j++)   // a clean pair: one value, two the same, another
        if (strcmp(T(j - 2), T(j - 1)) && strcmp(T(j - 1), T(j)) && !strcmp(T(j), T(j + 1)) && strcmp(T(j + 1), T(j + 2)))
            twin = j;
#undef T
    CHECK(twin > 0, "Feedback has no twins");
    if (twin <= 0) return;
    char line[96];
    snprintf(line, sizeof line, "Feedback %s", knobctl_text(l, twin - 1));
    say(line, FM1_SAY_FLAG_NOW);
    later(2000);
    int idx;
    knobctl_turnable(&K, &M, 2, &idx);
    CHECK(idx == twin - 1, "Feedback at %d, not %d", idx, twin - 1);
    int s = knobctl_step_px(n);
    touch(300, 110, true);
    touch(310, 110, false);
    slide(310, s, 110);
    CHECK(K.to[2] == twin, "the first twin: %d", K.to[2]);
    slide(310 + s, s, 110);
    CHECK(K.to[2] == twin + 1 && K.from[2] == twin, "the second twin: %d", K.to[2]);
    knobctl_release(&K, &M, now);
    later(50);
    snprintf(line, sizeof line, "Feedback %s", knobctl_text(l, twin + 1));
    say(line, FM1_SAY_FLAG_NOW);
    ticks();
    knobctl_turnable(&K, &M, 2, &idx);
    CHECK(!K.w[2].waiting && idx == twin + 1, "the twin said back: %d, not %d", idx, twin + 1);
    // and a drag from there starts there
    touch(300, 110, true);
    touch(290, 110, false);
    slide(290, -s, 110);
    CHECK(K.from[2] == twin + 1 && K.to[2] == twin, "the drag from the second twin: %d to %d", K.from[2], K.to[2]);
    knobctl_release(&K, &M, now);
}

static void unit_wrap(void) {
    // the time-out across the clock's wrap
    fresh(0xFFFFF000u);
    page(AISLING_LINE);
    later(0xF00);   // now = 0xFFFFFF00
    touch(60, 110, true);
    slide(60, 13, 110);   // sent at about 0xFFFFFF34
    uint32_t sent_at = K.w[0].ms;
    knobctl_release(&K, &M, now);
    while (now - sent_at < KC_ANSWER_MS - 1) now++, ticks();
    CHECK(K.w[0].waiting && M.pv[0], "timed out early, across the wrap");
    now++;
    ticks();
    CHECK(!K.w[0].waiting && K.n_not_taken == 1 && M.pv[0] == NULL && !strcmp(M.value[0], "Off"),
          "no time-out across the wrap");
    CHECK(K.n_asked == 1 && K.want_where && sent_at > 0xFFFFFE00u, "the ask across the wrap: %u", K.n_asked);
    CHECK(now < 0x1000u, "the wrap didn't happen (%08x)", now);
    CHECK(!strcmp(M.msg, "THE FM-1 DIDN'T TAKE IT"), "the warning across the wrap");
    later(3990);
    CHECK(M.msg[0], "the warning went early");
    later(20);
    CHECK(M.msg[0] == 0, "the warning stayed: %s", M.msg);
}

static void unit_locked(void) {
    // Setup: cc_in takes none of its knobs
    fresh(1000);
    page(SETUP_LINE);
    CHECK(M.locked == 15u, "Setup's knobs aren't dimmed: %x", M.locked);
    for (int i = 0; i < 4; i++) {
        touch(120 * i + 60, 110, true);
        slide(120 * i + 60, i & 1 ? 59 : -59, 110);
        CHECK(knobctl_release(&K, &M, now), "a drag on Setup's %s", fm1scr_label(V_SETUP, i));
    }
    CHECK(K.n_sent == 0 && K.n_drags == 0, "Setup's knobs sent %u", K.n_sent);
    // Mode on Custom: dimmed; on Dorian: not
    page("Key page: Tempo 76, Root D, Mode Custom, Transpose Parallel");
    CHECK(M.locked == 4u, "Mode Custom isn't dimmed: %x", M.locked);
    touch(300, 110, true);
    slide(300, 50, 110);
    CHECK(knobctl_release(&K, &M, now) && K.n_sent == 0, "Mode Custom sent");
    say("MODE Dorian", FM1_SAY_FLAG_NOW);
    ticks();
    CHECK(M.locked == 0, "Mode Dorian is dimmed: %x", M.locked);
    touch(300, 110, true);
    touch(310, 110, false);
    slide(310, 24, 110);
    int l = knobctl_list_of(V_KEY, 2);
    int dor = knobctl_find(l, "Dorian", -1);
    CHECK(dor >= 0 && K.n_sent == 1 && K.from[2] == dor && K.to[2] == dor + 1, "Mode from Dorian: %d to %d", K.from[2],
          K.to[2]);
    knobctl_release(&K, &M, now);
    // a value it doesn't know, an empty one, a page it doesn't know
    page("Drone page, voice Pipes: Pedal 900, Reso 0, Phaser 15, Attack 40");
    CHECK(M.locked == 1u, "Pedal 900 isn't dimmed: %x", M.locked);
    page("Mix page: Drone 38, Seq 44, Lead 76");
    CHECK((M.locked & 7u) == 0, "Mix is dimmed: %x", M.locked);
    CHECK(knobctl_turnable(&K, &M, 3, NULL) < 0 || M.value[3][0], "an empty knob turns");
}

// random touch soup with the FM-1 saying all sorts: nothing out of range, nothing on a knob it can't turn, every
// step a step from where the finger was
static void unit_soup(void) {
    static const char *const FIXED[] = {"MODE Custom",  "MODE Dorian", "Tempo EXT",      "Tempo 80",  "PLAYING",
                                        "Filter Bogus", "Slot 9",      "Chord A minor 7", "SEQ MUTED", "Evolve 1000"};
    const char *pages[FM1_VIEWS + 1] = {0};
    // the corpus's page lines are in fm1scr_check; these will do: each page's line from the model's own names
    char lines[FM1_VIEWS][200];
    for (int v = 0; v < FM1_VIEWS; v++) {
        int o = snprintf(lines[v], sizeof lines[v], "%s page: ", fm1scr_view_name(v));
        for (int i = 0; i < 4; i++) {
            int l = knobctl_list_of(v, i);
            const char *t = l >= 0 ? knobctl_text(l, (int)(rnd() % (uint32_t)knobctl_list_n(l))) : "On";
            o += snprintf(lines[v] + o, sizeof lines[v] - (size_t)o, "%s%s %s", i ? ", " : "", fm1scr_label(v, i), t);
        }
        pages[v] = lines[v];
    }
    fresh(0xFFFF0000u);
    bool down = false;
    int x = 0, y = 0, last_to[4] = {-1, -1, -1, -1};
    bool sent_this_drag = false, pend = false;
    uint32_t n_mask_bits = 0, pend_at = 0;
    for (int it = 0; it < 400000; it++) {
        int r = rnd_in(0, 99);
        now += r < 2 ? (uint32_t)rnd_in(1000, 3000) : (uint32_t)rnd_in(1, 30);
        if (r < 6) {
            say(pages[rnd_in(0, FM1_VIEWS - 1)], FM1_SAY_FLAG_NOW);
        } else if (r < 14 && M.view >= 0) {   // a knob said: a value of its list, often the one sent
            int i = rnd_in(0, 3), l = knobctl_list_of(M.view, i);
            char t[96];
            if (l >= 0) {
                int j = K.w[i].waiting && rnd_in(0, 2) ? K.w[i].idx : rnd_in(0, knobctl_list_n(l) - 1);
                snprintf(t, sizeof t, "%s %s", fm1scr_label(M.view, i), knobctl_text(l, j));
                say(t, FM1_SAY_FLAG_NOW);
            }
        } else if (r < 16) {
            say(FIXED[rnd_in(0, 9)], FM1_SAY_FLAG_NOW);
        } else if (r < 18) {
            knobctl_reset(&K, rnd_in(0, 1) ? &M : NULL);
        } else {   // the touch
            bool first = !down;
            if (!down) {
                down = true;
                x = rnd_in(0, 479);
                y = rnd_in(0, 99) < 70 ? rnd_in(80, 141) : rnd_in(0, 319);
                sent_this_drag = false;
                for (int i = 0; i < 4; i++) last_to[i] = -1;
            } else if (rnd_in(0, 99) < 6) {   // up
                knobctl_release(&K, &M, now);
                down = false;
                ticks();
                continue;
            } else if (rnd_in(0, 99) < 3) {
                x = rnd_in(0, 479);   // a glitch, or a jump
            } else {
                x += rnd_in(-12, 12);
                y += rnd_in(-2, 2);
                x = x < 0 ? 0 : x > 479 ? 479 : x;
                y = y < 0 ? 0 : y > 319 ? 319 : y;
            }
            int view = M.view, base = K.base;
            bool was_dragging = K.dragging;
            unsigned m = knobctl_touch(&K, &M, x, y, first, now);
            CHECK(m == 0 || (m & (m - 1)) == 0, "two knobs at once: %x", m);
            for (int i = 0; i < 4; i++) {
                if (!(m & (1u << i))) continue;
                n_mask_bits++;
                int l = knobctl_list_of(view, i), n = knobctl_list_n(l), idx;
                CHECK(i == K.knob && K.in_row && !K.dead && K.dragging, "a send that isn't the drag's");
                CHECK(M.view == K.view && view == K.view, "a send on another page");
                CHECK(l >= 0 && l == K.list && knobctl_turnable(&K, &M, i, &idx) == l, "a send on a dimmed knob");
                CHECK(K.cc[i] <= 127 && K.cc[i] == knobctl_cc_for(K.to[i], n) && knobctl_idx_of(K.cc[i], n) == K.to[i],
                      "the CC %u for %d of %d", K.cc[i], K.to[i], n);
                CHECK(K.to[i] >= 0 && K.to[i] < n && K.to[i] != K.from[i], "a send to %d from %d of %d", K.to[i],
                      K.from[i], n);
                if (sent_this_drag && last_to[i] >= 0)
                    CHECK(K.from[i] == last_to[i], "a send from %d after %d", K.from[i], last_to[i]);
                else
                    CHECK(K.from[i] == base || !was_dragging, "the drag's first send from %d, not %d", K.from[i],
                          base);
                last_to[i] = K.to[i];
                sent_this_drag = true;
            }
            if (K.dragging && !K.dead && !K.cand) {
                int s = knobctl_step_px(knobctl_list_n(K.list)), off = (K.x - K.x0) - (K.at - K.base) * s;
                CHECK(abs(off) < s, "%d px from where it is (step %d)", off, s);
                CHECK(K.at >= 0 && K.at < knobctl_list_n(K.list), "at %d", K.at);
            }
        }
        ticks();
        if (K.want_where) {   // main.c's WHERE: answered, or lost
            K.want_where = false;
            if (M.view >= 0 && rnd_in(0, 2)) pend = true, pend_at = now + (uint32_t)rnd_in(5, 300);
        }
        if (pend && (int32_t)(now - pend_at) >= 0 && M.view >= 0) {   // the page, the sent value on it or not
            pend = false;
            char line[240];
            int o = snprintf(line, sizeof line, "%s page: ", fm1scr_view_name(M.view));
            for (int i = 0; i < 4; i++) {
                const char *t = K.w[i].waiting && K.w[i].view == M.view && rnd_in(0, 1)
                                    ? knobctl_text(K.w[i].list, K.w[i].idx)
                                    : M.value[i][0] ? M.value[i] : "On";
                o += snprintf(line + o, sizeof line - (size_t)o, "%s%s %s", i ? ", " : "", fm1scr_label(M.view, i), t);
            }
            uint32_t settled = K.n_answered + K.n_not_taken;
            bool any = false;
            for (int i = 0; i < 4; i++) any = any || (K.w[i].waiting && K.w[i].asked && K.w[i].view == M.view);
            say(line, FM1_SAY_FLAG_NOW);
            ticks();
            for (int i = 0; i < 4; i++)
                CHECK(!(K.w[i].waiting && K.w[i].asked && K.w[i].view == M.view), "asked, and the page didn't settle it");
            CHECK(!any || K.n_answered + K.n_not_taken > settled, "the page settled nothing");
        }
        for (int i = 0; i < 4; i++) {
            int l = knobctl_list_of(M.view, i), j = l >= 0 && M.value[i][0] ? knobctl_find(l, M.value[i], -1) : -1;
            bool dim = M.view >= 0 && M.value[i][0] && j < 0;
            CHECK(((M.locked >> i) & 1u) == dim, "%s's %s (%s) dimmed %d", fm1scr_view_name(M.view),
                  fm1scr_label(M.view, i), M.value[i], (M.locked >> i) & 1u);
            if (M.pv[i]) CHECK((K.dragging && K.knob == i) || K.w[i].waiting, "a preview of nothing");
        }
        if (fails > 40) break;
    }
    CHECK(K.n_sent == n_mask_bits, "sent %u, %u bits", K.n_sent, n_mask_bits);
    CHECK(K.n_sent > 2000 && K.n_answered > 200 && K.n_not_taken > 20 && K.n_cancelled > 50 && K.n_glitches > 100 &&
              K.n_locked > 100 && K.n_asked > 100 && K.n_paged > 50,
          "the soup didn't reach everything: sent %u answered %u not taken %u cancelled %u glitches %u locked %u "
          "asked %u paged %u",
          K.n_sent, K.n_answered, K.n_not_taken, K.n_cancelled, K.n_glitches, K.n_locked, K.n_asked, K.n_paged);
    printf("soup: %u sent, %u answered, %u not taken (%u WHEREs asked, %u settled by the page), %u cancelled, "
           "%u glitches, %u locked presses\n",
           K.n_sent, K.n_answered, K.n_not_taken, K.n_asked, K.n_paged, K.n_cancelled, K.n_glitches, K.n_locked);
}

// ---------------------------------------------------------------------------------------------------- the sweep

typedef struct {
    const char *tap;   // the button (NULL: none, the first page)
    int view;          // the page it reaches
    bool use;          // swept / dragged on
} nav_t;

// the order the buttons reach the pages in (hosttests' nav probe; Mix isn't reached)
static const nav_t SWEEP_NAV[] = {
    {NULL, 0, 1},     {"ENV", 1, 1},   {"ENV", 8, 1},   {"ENV", 12, 1},  {"FX", 5, 1},     {"FX", 20, 1},
    {"LFO", 3, 1},    {"LFO", 10, 1},  {"LFO", 26, 1},  {"LFO", 14, 1},  {"ARP", 2, 1},    {"ARP", 9, 1},
    {"ARP", 13, 1},   {"ARP", 23, 1},  {"SEQ", 4, 1},   {"SEQ", 11, 1},  {"SEQ", 16, 1},   {"SEQ", 24, 1},
    {"SAVE", 19, 1},  {"SAVE", 21, 1}, {"SAVE", 25, 1}, {"GLO", 6, 1},   {"GLO", 22, 1},   {"EDIT", 15, 1},
    {"HOME", 0, 0},   {"HOME", 18, 1}, {"SEL", 17, 1},
};
#define NSWEEP (int)(sizeof SWEEP_NAV / sizeof SWEEP_NAV[0])
// Aisling's Evolve last: above 0 it moves the Filter on its own
static const int AISLING_ORDER[4] = {0, 2, 3, 1}, ORDER[4] = {0, 1, 2, 3};

static void sweep_script(const char *out) {
    FILE *f = fopen(out, "w");
    if (!f) exit(2);
    fprintf(f, "wait 3000\nlisten\nwait 300\nmidi B0 11 00\nwait 300\n");
    int n = 0;
    for (int p = 0; p < NSWEEP; p++) {
        const nav_t *nv = &SWEEP_NAV[p];
        if (nv->tap) fprintf(f, "tap %s\nwait 400\n", nv->tap);
        fprintf(f, "where\nwait 200\necho @P %d\n", nv->view);
        if (!nv->use) continue;
        const int *ord = nv->view == V_AISLING ? AISLING_ORDER : ORDER;
        for (int q = 0; q < 4; q++) {
            int i = ord[q];
            fprintf(f, "where\nwait 200\necho @K %d\n", i);
            for (int v = 0; v < 128; v++, n++)   // the CC, what's said; then the page asked for, as the box would
                fprintf(f, "midi B0 %02X %02X\nwait 150\necho @C %d %d\nwhere\nwait 200\necho @W %d %d\n", 0x10 + i, v,
                        i, v, i, v);
        }
        if (nv->view == V_AISLING) fprintf(f, "midi B0 11 00\nwait 300\n");
    }
    fclose(f);
    printf("sweep script: %d pages, %d CCs\n", NSWEEP, n);
}

// an amb_host output: "said: TEXT", "said!: TEXT" (cut off what was being said), or an echo's "@..."
typedef struct {
    char *text;
    uint8_t flags;
    bool mark;
} oline_t;
static oline_t *OL;
static int NOL;
static void read_output(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        printf("FAIL: can't read %s\n", path);
        exit(1);
    }
    static char buf[4096];
    int cap = 0;
    while (fgets(buf, sizeof buf, f)) {
        buf[strcspn(buf, "\r\n")] = 0;
        oline_t o = {0};
        if (!strncmp(buf, "said: ", 6)) o.text = strdup(buf + 6);
        else if (!strncmp(buf, "said!: ", 7)) o.text = strdup(buf + 7), o.flags = FM1_SAY_FLAG_NOW;
        else if (buf[0] == '@') o.text = strdup(buf), o.mark = true;
        else continue;
        if (NOL == cap) OL = realloc(OL, sizeof *OL * (size_t)(cap = cap ? cap * 2 : 4096));
        OL[NOL++] = o;
    }
    fclose(f);
}

// What AISLING does with a CC it takes but doesn't say back, by knob: the reason the box asks for a WHERE.
typedef struct {
    int view, knob, n, first_cc;
    char said[48];   // what it said instead (the first time), or ""
} unsaid_t;
static unsaid_t UNSAID[64];
static int NUNSAID;

static void sweep_check(const char *path) {
    read_output(path);
    fresh(0);
    char kval[24] = "", prev[24] = "", other[48] = "";
    int np = 0, nc = 0, nw = 0, nk = 0, view_seen[FM1_VIEWS] = {0}, list_seen[KT_NLISTS] = {0}, same = 0;
    int cur_view = -1, n_said_back = 0, n_unsaid = 0, n_motion = 0;
    uint16_t heard0 = 0;
    for (int j = 0; j < NOL; j++) {
        if (!OL[j].mark) {
            now += 10;
            uint32_t knob_says = M.n_knob;
            say(OL[j].text, OL[j].flags);
            if (M.n_knob != knob_says && !other[0]) snprintf(other, sizeof other, "%s", OL[j].text);
            continue;
        }
        int a, b;
        if (sscanf(OL[j].text, "@P %d", &a) == 1) {
            np++;
            cur_view = a;
            CHECK(M.view == a, "@P %d: the box has %s", a, fm1scr_view_name(M.view));
            if (M.view >= 0 && M.view < FM1_VIEWS) view_seen[M.view] = 1;
        } else if (sscanf(OL[j].text, "@K %d", &a) == 1) {
            nk++;
            memcpy(kval, M.value[a & 3], sizeof kval);
            memcpy(prev, kval, sizeof prev);
            heard0 = M.heard[a & 3];
            other[0] = 0;
        } else if (sscanf(OL[j].text, "@C %d %d", &a, &b) == 2 || sscanf(OL[j].text, "@W %d %d", &a, &b) == 2) {
            bool w = OL[j].text[1] == 'W';
            nc += !w, nw += w;
            int l = knobctl_list_of(cur_view, a);
            if (l < 0) {
                CHECK(!strcmp(M.value[a], kval), "%s's %s moved by CC %d: %s, was %s", fm1scr_view_name(cur_view),
                      fm1scr_label(cur_view, a), b, M.value[a], kval);
                same += w;
                continue;
            }
            int wi = knobctl_idx_of(b, knobctl_list_n(l)), got = knobctl_find(l, M.value[a], wi);
            const char *want = knobctl_text(l, wi);
            if (w) {   // the page: the truth (Motion's knobs: a step or two after a section change, the turn let go)
                bool mo = knobctl_moves(cur_view, a) && got >= 0 && got != wi && abs(got - wi) <= 2;
                n_motion += mo;
                CHECK(got == wi || mo, "%s's %s at CC %d: the FM-1's page has %s, the box's table %s",
                      fm1scr_view_name(cur_view), fm1scr_label(cur_view, a), b, M.value[a], want ? want : "?");
                list_seen[l] = 1;
                memcpy(prev, M.value[a], sizeof prev);
                heard0 = M.heard[a];
                other[0] = 0;
                continue;
            }
            bool said = M.heard[a] != heard0, moved = knobctl_find(l, prev, wi) != wi;
            if (said) {   // said back: it must be the table's (the MODE say without its full stop: knobctl_find)
                n_said_back++;
                CHECK(got == wi, "%s's %s at CC %d: the FM-1 said %s, the box's table %s",
                      fm1scr_view_name(cur_view), fm1scr_label(cur_view, a), b, M.value[a], want ? want : "?");
            } else if (moved) {   // taken (the @W will tell) but not said: what the box's WHERE is for
                n_unsaid++;
                int u = 0;
                while (u < NUNSAID && (UNSAID[u].view != cur_view || UNSAID[u].knob != a)) u++;
                if (u == NUNSAID && NUNSAID < 64) UNSAID[NUNSAID++] = (unsaid_t){cur_view, a, 0, b, ""}, snprintf(UNSAID[u].said, sizeof UNSAID[u].said, "%s", other);
                if (u < NUNSAID) UNSAID[u].n++;
            }
        }
    }
    int nv = 0, nl = 0;
    for (int v = 0; v < FM1_VIEWS; v++) nv += view_seen[v];
    for (int l = 0; l < KT_NLISTS; l++) nl += list_seen[l];
    CHECK(np == NSWEEP && nk == 26 * 4 && nc == 26 * 4 * 128 && nw == nc, "the sweep's markers: %d pages, %d knobs, "
          "%d CCs, %d WHEREs", np, nk, nc, nw);
    CHECK(nv == 26 && !view_seen[7], "the sweep reached %d pages", nv);
    CHECK(nl == KT_NLISTS, "the sweep covered %d of the %d lists", nl, KT_NLISTS);
    printf("sweep: %d pages (all but Mix), %d CCs through AISLING's ui.c, each then a WHERE: every page as the box's "
           "table has it (%d of Motion's a step or two off), all %d lists; Setup's %d CCs moved nothing; %d turns said "
           "back, %d not (%d knobs); %u said\n",
           nv, nc, n_motion, nl, same, n_said_back, n_unsaid, NUNSAID, M.n_said);
    CHECK(n_motion < 40, "%d pages off the table on Motion's knobs: more than its glide", n_motion);
    for (int u = 0; u < NUNSAID; u++)
        printf("  not said back: %s's %s, %d time%s (first at CC %d)%s%s\n", fm1scr_view_name(UNSAID[u].view),
               fm1scr_label(UNSAID[u].view, UNSAID[u].knob), UNSAID[u].n, UNSAID[u].n == 1 ? "" : "s",
               UNSAID[u].first_cc, UNSAID[u].said[0] ? "; said instead: " : "", UNSAID[u].said);
}

// ---------------------------------------------------------------------------------------------------- the drags

static const nav_t DRAG_NAV[] = {
    {NULL, 0, 1},   {"FX", 5, 1},     {"GLO", 6, 1},    {"SEQ", 4, 1},   {"SEQ", 11, 0},
    {"SEQ", 16, 1}, {"EDIT", 15, 1},  {"HOME", 0, 0},   {"HOME", 18, 1}, {"SEL", 17, 1},
};
#define NDRAG (int)(sizeof DRAG_NAV / sizeof DRAG_NAV[0])
#define FEED_MS 8u      // what the FM-1 says after a command, this long after it (the loop: after the marker)
#define READ_MS 16u     // the touch is read this often

enum { PLAN, LOOP };
static int mode;
static FILE *script;
static uint32_t t_script;
static int at_mark;     // LOOP: the next marker in OL
static bool lost;

// what's due to be said: FIFO, in time order
typedef struct {
    uint32_t t;
    char *text;
    uint8_t flags;
    bool own;
} due_t;
static due_t DUE[4096];
static int due_head, due_tail;
static void due_add(uint32_t t, char *text, uint8_t flags, bool own) {
    if (due_tail - due_head >= 4096) {
        printf("FAIL: the due queue is full\n");
        exit(1);
    }
    DUE[due_tail++ % 4096] = (due_t){t, text, flags, own};
}
static void deliver(void) {
    while (due_head < due_tail && (int32_t)(DUE[due_head % 4096].t - now) <= 0) {
        due_t *d = &DUE[due_head++ % 4096];
        say(d->text, d->flags);
        if (d->own) free(d->text);
    }
}
static uint32_t n_asks_sim;   // the box's WHEREs (main.c sends them; here the drag's own after the release answers)
static void asks(void) {
    if (K.want_where) K.want_where = false, n_asks_sim++;
}
static void advance_to(uint32_t T) {
    deliver();
    while ((int32_t)(T - now) > 0) {
        uint32_t nx = now + 10;
        if (due_head < due_tail && (int32_t)(DUE[due_head % 4096].t - nx) < 0) nx = DUE[due_head % 4096].t;
        if ((int32_t)(nx - T) > 0) nx = T;
        if ((int32_t)(nx - now) <= 0) nx = now + 1;
        now = nx;
        deliver();
        ticks();
        asks();
    }
}

// the stand-in FM-1 (PLAN): each page's values from the corpus, a knob said when a CC changes it, a page when asked
static int fk_view, fk_next;
static int fk_idx[FM1_VIEWS][4];
static char fk_head[FM1_VIEWS][64], fk_val[FM1_VIEWS][4][24];
static void fake_seed(const char *corpus) {
    FILE *f = fopen(corpus, "r");
    if (!f) {
        printf("FAIL: can't read %s\n", corpus);
        exit(1);
    }
    static char buf[1024];
    int v = 0;
    while (fgets(buf, sizeof buf, f) && v < FM1_VIEWS) {
        buf[strcspn(buf, "\r\n")] = 0;
        if (strncmp(buf, "SCREEN\t", 7)) continue;
        const char *t = buf + 7;
        fm1scr_t tmp;
        fm1scr_init(&tmp);
        fm1scr_say(&tmp, t, (uint32_t)strlen(t), FM1_SAY_FLAG_NOW, 0);
        CHECK(tmp.view == v, "corpus page %d is %d", v, tmp.view);
        const char *c = strstr(t, ": ");
        snprintf(fk_head[v], sizeof fk_head[v], "%.*s", c ? (int)(c - t) : 0, t);
        for (int i = 0; i < 4; i++) {
            memcpy(fk_val[v][i], tmp.value[i], sizeof fk_val[v][i]);
            int l = knobctl_list_of(v, i);
            fk_idx[v][i] = l >= 0 ? knobctl_find(l, tmp.value[i], -1) : -1;
        }
        v++;
    }
    fclose(f);
    CHECK(v == FM1_VIEWS, "the corpus has %d pages", v);
}
static char *fake_page(bool where) {
    char *o = malloc(400);
    int v = fk_view, n = snprintf(o, 400, "%s: ", fk_head[v]);
    for (int i = 0; i < 4; i++) {
        int l = knobctl_list_of(v, i);
        const char *t = l >= 0 && fk_idx[v][i] >= 0 ? knobctl_text(l, fk_idx[v][i]) : fk_val[v][i];
        n += snprintf(o + n, (size_t)(400 - n), "%s%s %s", i ? ", " : "", fm1scr_label(v, i), t);
    }
    if (where) snprintf(o + n, (size_t)(400 - n), ". Piece Skellig, D Minor, tempo 76, playing");
    return o;
}
static void fake_cmd(const char *c) {
    unsigned st, d1, d2;
    if (!strncmp(c, "tap ", 4)) {
        fk_view = fk_next;
        due_add(now + FEED_MS, fake_page(false), FM1_SAY_FLAG_NOW, true);
    } else if (!strcmp(c, "where") || !strcmp(c, "listen")) {
        due_add(now + FEED_MS, fake_page(true), FM1_SAY_FLAG_NOW, true);
    } else if (sscanf(c, "midi %x %x %x", &st, &d1, &d2) == 3 && st == 0xB0 && d1 >= 0x10 && d1 < 0x14) {
        int i = (int)d1 - 0x10, l = knobctl_list_of(fk_view, i);
        if (l < 0) return;
        int idx = knobctl_idx_of((int)d2, knobctl_list_n(l));
        if (idx == fk_idx[fk_view][i]) return;
        fk_idx[fk_view][i] = idx;
        char *o = malloc(64);
        if (fk_view == V_KEY && i == 2) {   // ui.c's set_mode: MODE, said without the name's full stop (msg_text)
            int e = snprintf(o, 64, "MODE %s", knobctl_text(l, idx));
            while (e > 0 && o[e - 1] == '.') o[--e] = 0;
        } else snprintf(o, 64, "%s %s", fm1scr_label(fk_view, i), knobctl_text(l, idx));
        due_add(now + FEED_MS, o, FM1_SAY_FLAG_NOW, true);
    }
}

static void flush_wait(void) {
    if (mode == PLAN && now != t_script) fprintf(script, "wait %u\n", now - t_script);
    t_script = now;
}
static void cmd(const char *fmt, ...) {
    char b[96];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    flush_wait();
    if (mode == PLAN) {
        fprintf(script, "%s\n", b);
        fake_cmd(b);
    }
}
static int n_marks;
static void mark(const char *fmt, ...) {
    char b[96];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    flush_wait();
    n_marks++;
    if (mode == PLAN) {
        fprintf(script, "echo %s\n", b);
        return;
    }
    // LOOP: the same marker next in what AISLING said, and what it said after it, said here FEED_MS later
    int j = at_mark;
    while (j < NOL && !OL[j].mark) j++;
    if (j >= NOL || strcmp(OL[j].text, b)) {
        printf("FAIL: the loop went another way at marker %d: the box %s, AISLING's run %s\n", n_marks, b,
               j < NOL ? OL[j].text : "(the end)");
        fails++;
        lost = true;
        return;
    }
    for (j++; j < NOL && !OL[j].mark; j++) due_add(now + FEED_MS, OL[j].text, OL[j].flags, false);
    at_mark = j;
}

// a drag's checks, the same in both modes
typedef struct {
    int view, knob, start, n, s, dir;
    int sends, first_from, last_to, turns, lo, hi, prev_dir;
    bool chain_ok, steps_ok;
} drag_t;
static drag_t D;
static int n_drags_done, n_sends_done, n_two_steps;

static void reading(int x, int y, bool first) {
    unsigned m = knobctl_touch(&K, &M, x, y, first, now);
    for (int i = 0; i < 4; i++) {
        if (!(m & (1u << i))) continue;
        n_sends_done++;
        CHECK(i == D.knob && M.view == D.view, "a send on %s knob %d, the drag's %d", fm1scr_view_name(M.view), i,
              D.knob);
        if (D.sends == 0) D.first_from = K.from[i];
        else if (K.from[i] != D.last_to) D.chain_ok = false;
        // a third of a step a reading, and the jitter's whole step end to end: never more than two at once
        if (abs(K.to[i] - K.from[i]) > 2) D.steps_ok = false;
        n_two_steps += abs(K.to[i] - K.from[i]) == 2;
        int dir = K.to[i] > K.from[i] ? 1 : -1;
        if (D.sends && dir != D.prev_dir) D.turns++;
        D.prev_dir = dir;
        D.last_to = K.to[i];
        D.lo = K.to[i] < D.lo ? K.to[i] : D.lo;
        D.hi = K.to[i] > D.hi ? K.to[i] : D.hi;
        D.sends++;
        cmd("midi B0 %02X %02X", 0x10 + i, K.cc[i]);
        mark("@S %d %d %d", D.view, i, K.cc[i]);
    }
    ticks();
    asks();
}

// a value of the knob's to start from, near frac of its range, that says something no other value does
static int pick_start(int l, int i) {
    static const int FRAC[4] = {30, 55, 70, 40};
    int n = knobctl_list_n(l), j0 = ((n - 1) * FRAC[i] + 50) / 100;
    for (int k = 0; k < n; k++) {
        int j = (j0 + k) % n;
        if (knobctl_find(l, knobctl_text(l, j), -1) == j &&
            (j + 1 >= n || strcmp(knobctl_text(l, j), knobctl_text(l, j + 1))))
            return j;
    }
    return j0;
}

static void drag(int view, int i) {
    int l = knobctl_list_of(view, i), n = knobctl_list_n(l), s = l >= 0 ? knobctl_step_px(n) : 24;
    bool locked = l < 0;
    int start = locked ? 0 : pick_start(l, i);
    char before[4][24];
    // the start: a CC from outside (a controller, say), then the page asked for
    if (!locked) {
        cmd("midi B0 %02X %02X", 0x10 + i, knobctl_cc_for(start, n));
        advance_to(now + 300);
    }
    cmd("where");
    advance_to(now + 200);
    mark("@O %d %d", view, i);
    if (lost) return;
    memcpy(before, M.value, sizeof before);
    int idx = -1;
    if (!locked) {
        CHECK(!strcmp(M.value[i], knobctl_text(l, start)), "%s's %s didn't start at %s: %s", fm1scr_view_name(view),
              fm1scr_label(view, i), knobctl_text(l, start), M.value[i]);
        CHECK(knobctl_turnable(&K, &M, i, &idx) == l && idx == start, "%s's %s: the box has it at %d, not %d",
              fm1scr_view_name(view), fm1scr_label(view, i), idx, start);
    } else {
        CHECK(knobctl_turnable(&K, &M, i, &idx) < 0 && (M.locked >> i & 1u), "%s's %s isn't dimmed",
              fm1scr_view_name(view), fm1scr_label(view, i));
    }
    // the path: right 3.4 steps, left 5, on past an end, back 1.2 steps; the finger at a third of a step a reading,
    // with a resistive panel's jitter (up to half a step)
    int xs = i == 3 ? 370 : 120 * i + 60, y = 110, j = (s - 1) / 2, v = s / 3 > 1 ? s / 3 : 1;
    int dir = i < 2 ? 1 : -1, w[4];
    for (int tries = 0; tries < 2; tries++, dir = -dir) {
        w[0] = xs + KC_START_PX + 34 * s / 10;
        w[1] = w[0] - 5 * s;
        w[2] = dir > 0 ? xs + KC_START_PX + (n - 1 - start) * s + 2 * s + 15 : xs - KC_START_PX - start * s - 2 * s - 15;
        w[3] = w[2] - dir * 12 * s / 10;
        if (locked) w[2] = xs + dir * 60, w[3] = w[2] - dir * 20;
        bool ok = true;
        for (int k = 0; k < 4; k++) ok = ok && w[k] - j >= 0 && w[k] + j <= 479;
        if (ok) break;
        CHECK(tries == 0, "%s's %s from %d: no room on the screen", fm1scr_view_name(view), fm1scr_label(view, i), start);
    }
    D = (drag_t){view, i, start, n, s, dir, 0, -1, start, 0, 1 << 30, -1, 0, true, true};
    uint32_t locked0 = K.n_locked, cancelled0 = K.n_cancelled, glitches0 = K.n_glitches, nt0 = K.n_not_taken;
    uint32_t sent0 = K.n_sent;
    double x = xs;
    advance_to(now + READ_MS);
    reading(xs + rnd_in(-j, j), y, true);
    for (int k = 0; k < 4 && !lost; k++) {
        while (!lost && (int)x != w[k]) {
            x += x < w[k] ? (w[k] - x < v ? w[k] - x : v) : -(x - w[k] < v ? x - w[k] : v);
            advance_to(now + READ_MS);
            reading((int)x + rnd_in(-j, j), y + rnd_in(-3, 3), false);
        }
    }
    for (int k = 0; k < 6 && !lost; k++) {   // held a moment
        advance_to(now + READ_MS);
        reading((int)x + rnd_in(-j, j), y + rnd_in(-3, 3), false);
    }
    if (lost) return;
    advance_to(now + 60);   // main.c: up after 60 ms of no touch
    bool knobs = knobctl_release(&K, &M, now);
    CHECK(knobs, "the release of a drag on %s's %s would flip the page", fm1scr_view_name(view), fm1scr_label(view, i));
    CHECK(K.n_cancelled == cancelled0 && K.n_glitches == glitches0, "%s's %s: %u cancelled, %u glitches",
          fm1scr_view_name(view), fm1scr_label(view, i), K.n_cancelled - cancelled0, K.n_glitches - glitches0);
    // the page asked for after the release, as the box does when a turn isn't said back (AISLING's run: it settles
    // the box's wait if it asked)
    advance_to(now + 400);
    mark("@R %d %d", view, i);
    cmd("where");
    advance_to(now + 1200);
    mark("@E %d %d", view, i);
    if (lost) return;
    n_drags_done++;
    if (locked) {
        CHECK(D.sends == 0 && K.n_sent == sent0 && K.n_locked == locked0 + 1, "%s's %s sent %d", fm1scr_view_name(view),
              fm1scr_label(view, i), D.sends);
        CHECK(!memcmp(before, M.value, sizeof before), "Setup moved");
        return;
    }
    const char *lab = fm1scr_label(view, i);
    const char *vn = fm1scr_view_name(view);
    CHECK(D.sends > 0 && D.first_from == start, "%s's %s: the first send was from %d, not %d", vn, lab, D.first_from,
          start);
    CHECK(D.chain_ok && D.steps_ok, "%s's %s: a send that wasn't one or two steps on from the last", vn, lab);
    CHECK(D.turns <= 3, "%s's %s: the sends turned %d times (the finger 3)", vn, lab, D.turns);
    CHECK(dir > 0 ? D.hi == n - 1 : D.lo == 0, "%s's %s: never reached the end (%d..%d of %d)", vn, lab, D.lo, D.hi, n);
    CHECK(D.last_to == K.at, "%s's %s: the last send %d, at %d", vn, lab, D.last_to, K.at);
    CHECK(knobctl_find(l, M.value[i], D.last_to) == D.last_to, "%s's %s: the box shows %s, the last send was %s", vn,
          lab, M.value[i], knobctl_text(l, D.last_to));
    CHECK(!K.w[i].waiting && M.pv[i] == NULL && K.n_not_taken == nt0, "%s's %s: not answered", vn, lab);
    knobctl_turnable(&K, &M, i, &idx);
    CHECK(idx == D.last_to, "%s's %s: the box has it at %d, the last send %d", vn, lab, idx, D.last_to);
}

static void drags(void) {
    fresh(0xFFFFF000u);   // the clock wraps in the first few seconds
    t_script = now;
    due_head = due_tail = 0;
    at_mark = 0;
    rng = 0x5eed7u;
    if (mode == LOOP)   // said before the first marker
        for (; at_mark < NOL && !OL[at_mark].mark; at_mark++) due_add(now + FEED_MS, OL[at_mark].text, OL[at_mark].flags, false);
    fk_view = 0;
    advance_to(now + 3000);
    cmd("listen");
    advance_to(now + 300);
    cmd("midi B0 11 00");   // Evolve 0: above it, the Filter moves on its own
    advance_to(now + 300);
    for (int p = 0; p < NDRAG && !lost; p++) {
        const nav_t *nv = &DRAG_NAV[p];
        if (nv->tap) {
            fk_next = nv->view;
            cmd("tap %s", nv->tap);
            advance_to(now + 400);
        }
        cmd("where");
        advance_to(now + 200);
        mark("@P %d", nv->view);
        if (lost) break;
        CHECK(M.view == nv->view, "@P %d: the box has %s", nv->view, fm1scr_view_name(M.view));
        if (!nv->use) continue;
        const int *ord = nv->view == V_AISLING ? AISLING_ORDER : ORDER;
        for (int q = 0; q < 4 && !lost; q++) drag(nv->view, ord[q]);
        if (nv->view == V_AISLING) {
            cmd("midi B0 11 00");
            advance_to(now + 300);
        }
    }
    advance_to(now + 500);
    flush_wait();
    if (mode == LOOP && !lost) {
        int j = at_mark;
        while (j < NOL && !OL[j].mark) j++;
        CHECK(j == NOL, "AISLING's run has markers the box didn't: %s", j < NOL ? OL[j].text : "");
    }
    CHECK(K.n_not_taken == 0, "%u not taken", K.n_not_taken);
    CHECK(n_asks_sim == K.n_asked, "the box asked %u times, the drags saw %u", K.n_asked, n_asks_sim);
    if (mode == PLAN) CHECK(K.n_asked == 0, "the stand-in says every turn back, but the box asked %u times", K.n_asked);
    CHECK(n_two_steps * 10 < n_sends_done, "%d of %d sends were two steps", n_two_steps, n_sends_done);
    printf("%s: %d drags on %d pages, %d CCs sent (%d of them two steps), %u answered, %u not taken (%u WHEREs asked, %u settled by the page, "
           "%u Motion's); %d markers\n",
           mode == PLAN ? "plan" : "loop", n_drags_done, 8, n_sends_done, n_two_steps, K.n_answered, K.n_not_taken, K.n_asked,
           K.n_paged, K.n_moved, n_marks);
}

int main(int argc, char **argv) {
    if (argc >= 2 && !strcmp(argv[1], "units")) {
        table_checks();
        unit_threshold();
        unit_jitter();
        unit_ends();
        unit_glitch();
        unit_taps();
        unit_page_change();
        unit_answers();
        unit_second_drag();
        unit_wrap();
        unit_locked();
        unit_soup();
    } else if (argc == 3 && !strcmp(argv[1], "sweep-script")) {
        sweep_script(argv[2]);
        return 0;
    } else if (argc == 3 && !strcmp(argv[1], "sweep-check")) {
        sweep_check(argv[2]);
    } else if (argc == 4 && !strcmp(argv[1], "plan")) {
        mode = PLAN;
        fake_seed(argv[2]);
        script = fopen(argv[3], "w");
        if (!script) return 2;
        drags();
        fclose(script);
    } else if (argc == 3 && !strcmp(argv[1], "loop")) {
        mode = LOOP;
        read_output(argv[2]);
        drags();
    } else {
        fprintf(stderr, "knobctl_check units | sweep-script OUT | sweep-check AMB_OUT | plan CORPUS OUT | loop AMB_OUT\n");
        return 2;
    }
    printf("%s: %d checks, %d failed (drew %ld rects, %ld texts)\n", argv[1], checks, fails, n_fill, n_text);
    return fails ? 1 : 0;
}
