// perf.c (test 8: the effects page and the pad) on the Mac, under AddressSanitizer + UBSan.
//   perf_check units            the grid's cells, the CC numbers, a tap's one toggle, ALL OFF, no FM-1, leaving PERFORM;
//                               the pad's ends, middle, glitches, axes, reset; and the drawing: every word sent to the
//                               screen decoded, nothing at or below the status row, a full redraw covering the area,
//                               and the pages drawn bit by bit the same as a fresh full redraw
//   perf_check shots DIR        pictures (PPM)
//   perf_check script OUT       a script for fm1-aisling's amb_host: the CCs the box sends for a run of touches, and
//                               after each what AISLING's own ui.c must then hold (expect pcc) or say (said)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "lcd.h"
#include "perf.h"
#include "knobctl.h"
#include "fm1scr.h"
#include "pflash.h"

extern uint32_t rec[];
extern int nrec, bad_cs;

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { if (++fails <= 40) { printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } } while (0)

#define SENTINEL 0xFFFFFFFFu
static uint32_t fb_a[LCD_W * LCD_H], fb_b[LCD_W * LCD_H];
static int below, win_bad, px_bad;

static void decode(uint32_t *fb) {
    int c = -1, np = 0, cx0 = 0, cx1 = -1, cy0 = 0, cy1 = -1, x = 0, y = 0;
    long px = 0;
    uint8_t p[4] = {0};
    for (int i = 0; i <= nrec; i++) {
        uint32_t w = i < nrec ? rec[i] : 0;
        if (i == nrec || w == 0xFFFF0000u || !(w >> 16)) {
            if (c == 0x2C && px != (long)(cx1 - cx0 + 1) * (cy1 - cy0 + 1)) px_bad++;
            if ((c == 0x2A || c == 0x2B) && np != 4) win_bad++;
            c = i < nrec && w != 0xFFFF0000u ? (int)(w & 0xFFFF) : -1;
            np = 0, px = 0, x = cx0, y = cy0;
            continue;
        }
        uint16_t d = (uint16_t)w;
        if (c == 0x2A || c == 0x2B) {
            if (np < 4) p[np] = (uint8_t)d;
            if (++np == 4) {
                int a = p[0] << 8 | p[1], b = p[2] << 8 | p[3];
                if (c == 0x2A) cx0 = a, cx1 = b;
                else cy0 = a, cy1 = b;
                if (a > b || b >= (c == 0x2A ? LCD_W : LCD_H)) win_bad++;
            }
        } else if (c == 0x2C) {
            if (x <= cx1 && y <= cy1 && x < LCD_W && y < LCD_H) {
                fb[y * LCD_W + x] = d;
                if (y >= PERF_BOTTOM) below++;
            }
            px++;
            if (++x > cx1) x = cx0, y++;
        }
    }
    nrec = 0;
}
static void clear(uint32_t *fb) { for (int i = 0; i < LCD_W * LCD_H; i++) fb[i] = SENTINEL; }
static int unwritten(const uint32_t *fb) {
    int n = 0;
    for (int i = 0; i < LCD_W * PERF_BOTTOM; i++) n += fb[i] == SENTINEL;
    return n;
}
static int differ(const uint32_t *a, const uint32_t *b) {
    int n = 0;
    for (int i = 0; i < LCD_W * PERF_BOTTOM; i++) n += a[i] != b[i];
    return n;
}
static void draw(perf_t *p, bool xy) { if (xy) perf_xy_draw(p); else perf_fx_draw(p); }
static void fresh(const perf_t *p, bool xy, uint32_t *fb) {   // a copy, drawn whole
    perf_t c = *p;
    if (xy) perf_xy_redraw(&c); else perf_fx_redraw(&c);
    nrec = 0;
    draw(&c, xy);
    clear(fb);
    decode(fb);
}
static void ppm(const char *dir, const char *name, const uint32_t *fb) {
    char path[512];
    snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", LCD_W, LCD_H);
    for (int i = 0; i < LCD_W * LCD_H; i++) {
        uint32_t v = fb[i];
        uint8_t rgb[3];
        if (v == SENTINEL) {
            bool st = i / LCD_W >= PERF_BOTTOM;
            rgb[0] = st ? 28 : 255, rgb[1] = st ? 28 : 0, rgb[2] = st ? 28 : 255;
        } else {
            int r = v >> 11 & 31, g = v >> 5 & 63, b = v & 31;
            rgb[0] = (uint8_t)(r << 3 | r >> 2), rgb[1] = (uint8_t)(g << 2 | g >> 4), rgb[2] = (uint8_t)(b << 3 | b >> 2);
        }
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

// ---------------------------------------------------------------- touches

static int cell_of(const char *name) {   // an effect's cell, or a button's (FXB_*) by "!"
    for (int c = 0; c < FX_CELLS; c++) {
        int e = perf_fx_at(c);
        if (e >= 0 && !strcmp(PFLASH[e].name, name)) return c;
    }
    return -1;
}
static int cell_btn(int b) {
    for (int c = 0; c < FX_CELLS; c++) if (perf_fx_at(c) == b) return c;
    return -1;
}
static int fx_tap(perf_t *p, int c, int reads, bool live) {
    int x = c % FX_COLS * FX_CW + 40, y = c / FX_COLS * FX_CH + 20;
    for (int i = 0; i < reads; i++) perf_fx_touch(p, x + i % 3, y, i == 0, live);
    return perf_fx_release(p, reads >= 2);
}
static int xy_tap(perf_t *p, int x, int y, int reads, bool live) {
    for (int i = 0; i < reads; i++) perf_xy_touch(p, x, y, i == 0, live);
    return perf_xy_release(p, reads >= 2, live);
}
static int btn_y(int b) { return b * XY_BTN_H + 20; }

// ---------------------------------------------------------------- units

static void units(void) {
    perf_t p;
    perf_init(&p);

    // the grid: each effect once, three buttons, the white keys' groups a row each
    int seen[PFLASH_N] = {0}, btn = 0;
    for (int c = 0; c < FX_CELLS; c++) {
        int e = perf_fx_at(c);
        if (e >= 0 && e < PFLASH_N) seen[e]++;
        else if (e == FXB_ALLOFF || e == FXB_PAD || e == FXB_LAND) btn++;
        else CHECK(0, "cell %d holds %d", c, e);
    }
    for (int k = 0; k < PFLASH_N; k++) CHECK(seen[k] == 1, "%s is in %d cells", PFLASH[k].name, seen[k]);
    CHECK(btn == 3, "%d buttons", btn);
    static const char *const ROW0[4] = {"Stutter", "Half time", "Gate", "Roll"};
    for (int i = 0; i < 4; i++) CHECK(cell_of(ROW0[i]) == i, "%s in cell %d", ROW0[i], cell_of(ROW0[i]));
    CHECK(cell_of("Crush") == 3 * FX_COLS + 3 && cell_of("Reverb") == 2 * FX_COLS, "the groups' rows");
    CHECK(perf_fx_cell_at(479, 269) == FX_CELLS - 1 && perf_fx_cell_at(0, 270) == -1 && perf_fx_cell_at(-1, 5) == -1 &&
          perf_fx_cell_at(96, 45) == FX_COLS + 1, "cells by place");

    // the CCs: AISLING's (20-31, 102-116), all different, none a knob's 16-19; the pad's 74, 78, 79, 80
    bool used[128] = {false};
    for (int s = 0; s < PERF_NSLOT; s++) {
        int cc = perf_slot_cc(s);
        CHECK(cc > 19 && cc < 120 && !used[cc], "slot %d: CC %d", s, cc);
        if (cc >= 0 && cc < 128) used[cc] = true;
        if (s < PFLASH_N) CHECK(cc == (s < 12 ? 20 + s : 102 + s - 12), "effect %d: CC %d", s, cc);
    }
    CHECK(perf_slot_cc(27) == 74 && perf_slot_cc(28) == 78 && perf_slot_cc(29) == 79 && perf_slot_cc(30) == 80 &&
          perf_slot_cc(31) == -1 && perf_slot_cc(-1) == -1, "the pad's CCs");
    CHECK(PERF_NSLOT <= 32, "the slots fit a word");
    static const char *const LBL[4] = {"Filter", "Wet", "Energy", "Chorus"};
    for (int i = 0; i < 4; i++) {
        CHECK(!strcmp(fm1scr_label(PERF_VIEW, i), LBL[i]) && !strcmp(perf_pad_name(i), LBL[i]),
              "PERFORM's knob %d is %s", i, fm1scr_label(PERF_VIEW, i));
        CHECK(knobctl_list_n(knobctl_list_of(PERF_VIEW, i)) == 101, "its list");
    }
    CHECK(!strcmp(fm1scr_view_name(PERF_VIEW), "Perform"), "page %d is %s", PERF_VIEW, fm1scr_view_name(PERF_VIEW));

    // every name's lines fit their cell in the uncial hand
    for (int k = 0; k < PFLASH_N; k++) {
        char a[24];
        snprintf(a, sizeof a, "%s", PFLASH[k].name);
        for (char *w = strtok(a, " "); w; w = strtok(NULL, " "))
            CHECK(perf_utext_w(15, w) <= FX_CW - 2, "%s: %d px", w, perf_utext_w(15, w));
    }

    // a tap: nothing on the first reading, held on the second, nothing more however long or wherever it goes
    int st = cell_of("Stutter"), rv = cell_of("Reverb");
    perf_fx_touch(&p, 40, 20, true, true);
    CHECK(!p.due && !p.held[perf_fx_at(st)], "one reading held it");
    perf_fx_touch(&p, 41, 21, false, true);
    int k = perf_fx_at(st);
    CHECK(p.due == 1u << k && p.out[k] == 127 && p.held[k], "two readings: due %x", (unsigned)p.due);
    p.due = 0;
    for (int i = 0; i < 20; i++) perf_fx_touch(&p, 41, 21, false, true);
    for (int i = 0; i < 20; i++) perf_fx_touch(&p, 40 + FX_CW, 21, false, true);   // slid onto Half time
    CHECK(!p.due && perf_nheld(&p) == 1, "a long or sliding touch did more");
    CHECK(perf_fx_release(&p, true) == PERF_STAY, "release");
    CHECK(fx_tap(&p, rv, 5, true) == PERF_STAY && perf_nheld(&p) == 2, "a second effect");
    p.due = 0;
    fx_tap(&p, st, 3, true);   // again: let go
    CHECK(p.due == 1u << k && p.out[k] == 0 && !p.held[k] && perf_nheld(&p) == 1, "the second tap lets go");
    CHECK(p.n_holds == 2 && p.n_lets == 1, "counts");
    // a touch that starts between cells' readings (one here, one there) does nothing
    p.due = 0;
    perf_fx_touch(&p, 40, 20, true, true);
    perf_fx_touch(&p, 40 + FX_CW, 20, false, true);
    perf_fx_release(&p, true);
    CHECK(!p.due, "one reading each in two cells");

    // no FM-1: nothing changes, nothing to send
    p.due = 0;
    fx_tap(&p, st, 4, false);
    CHECK(!p.due && !p.held[k] && p.n_refused == 1, "a tap with no FM-1");

    // the page buttons: as the finger lifts, and only a real touch
    CHECK(fx_tap(&p, cell_btn(FXB_PAD), 4, true) == PERF_GO_PAD && fx_tap(&p, cell_btn(FXB_LAND), 4, true) == PERF_GO_LAND,
          "the page buttons");
    CHECK(fx_tap(&p, cell_btn(FXB_PAD), 1, true) == PERF_STAY, "one reading on a button");
    perf_fx_touch(&p, 40, 20 + 2 * FX_CH, true, true);   // Reverb, held: let go; then slid to PAD and lifted
    perf_fx_touch(&p, 40, 20 + 2 * FX_CH, false, true);
    perf_fx_touch(&p, 3 * FX_CW + 40, 5 * FX_CH + 20, false, true);
    perf_fx_touch(&p, 3 * FX_CW + 40, 5 * FX_CH + 20, false, true);
    CHECK(perf_fx_release(&p, true) == PERF_STAY, "a touch that did its thing then flipped the page");

    // ALL OFF: a 0 for every effect, held here or not
    fx_tap(&p, st, 3, true);
    fx_tap(&p, cell_of("Drop"), 3, true);
    p.due = 0;
    fx_tap(&p, cell_btn(FXB_ALLOFF), 3, true);
    uint32_t all = (1u << PFLASH_N) - 1;
    bool zero = true;
    for (int i = 0; i < PFLASH_N; i++) zero &= p.out[i] == 0;
    CHECK(p.due == all && zero && !perf_nheld(&p), "ALL OFF: due %x", (unsigned)p.due);

    // PERFORM left, or the FM-1 unplugged: nothing held, nothing of theirs to send
    fx_tap(&p, st, 3, true);
    fx_tap(&p, rv, 3, true);
    p.due |= 1u << PFLASH_N;
    perf_gone(&p);
    CHECK(!perf_nheld(&p) && p.due == 1u << PFLASH_N, "gone: due %x", (unsigned)p.due);
    p.due = 0;

    // the pad's ends and middle
    CHECK(perf_xy_cc(3, XY_MARGIN, false) == 0 && perf_xy_cc(3, 0, false) == 0 &&
          perf_xy_cc(3, XY_PAD_W - 1 - XY_MARGIN, false) == 127 && perf_xy_cc(3, XY_PAD_W - 1, false) == 127, "across: ends");
    CHECK(perf_xy_cc(3, PERF_BOTTOM - 1 - XY_MARGIN, true) == 0 && perf_xy_cc(3, XY_MARGIN, true) == 127, "up: ends");
    int last = -1;
    bool mono = true, mid = false, every = true;
    int rest = 0;
    bool got[128] = {false};
    for (int x = 0; x < XY_PAD_W; x++) {
        int cc = perf_xy_cc(3, x, false);
        mono &= cc >= last && cc - last <= 1 + (last < 0);
        last = cc;
        got[cc] = true;
        int f = perf_xy_cc(0, x, false);
        mid |= f == 64;
        rest += f == 64;
        CHECK(f == cc || (f == 64 && abs(cc - 64) <= XY_DETENT), "the detent at %d: %d, %d", x, f, cc);
    }
    for (int i = 0; i < 128; i++) every &= got[i];
    CHECK(mono && mid && every, "across: every CC in order (%d %d %d)", mono, mid, every);
    CHECK(rest >= 15 && rest <= 30, "the rest in the middle is %d px wide", rest);

    // a finger in the middle: Filter off, Wet 0; each CC sent is the one AISLING turns into that value
    perf_init(&p);
    CHECK(p.axis[0] == 0 && p.axis[1] == 1 && !*perf_pad_text(&p, 0), "the pad starts Filter across, Wet up, unsent");
    perf_xy_touch(&p, XY_PAD_W / 2, PERF_BOTTOM / 2, true, true);
    CHECK(!p.due, "the first reading of a touch sent");
    perf_xy_touch(&p, XY_PAD_W / 2 + 1, PERF_BOTTOM / 2, false, true);
    CHECK(p.due == (3u << PFLASH_N) && knobctl_idx_of(p.out[PFLASH_N], 101) == 50 && knobctl_idx_of(p.out[PFLASH_N + 1], 101) == 50, "the middle: due %x, %u %u",
          (unsigned)p.due, p.out[PFLASH_N], p.out[PFLASH_N + 1]);
    CHECK(!strcmp(perf_pad_text(&p, 0), "Off") && !strcmp(perf_pad_text(&p, 1), "0"), "the middle: %s, %s",
          perf_pad_text(&p, 0), perf_pad_text(&p, 1));
    p.due = 0;
    // the panel's jitter: a pixel or two sends nothing
    perf_xy_touch(&p, XY_PAD_W / 2 + 2, PERF_BOTTOM / 2 + 1, false, true);
    CHECK(!p.due, "jitter sent");
    // a glitch: one reading far off is held; the finger back, nothing sent for it
    perf_xy_touch(&p, 20, 20, false, true);
    CHECK(!p.due && p.n_glitches == 1, "a glitch sent");
    perf_xy_touch(&p, XY_PAD_W / 2 + 1, PERF_BOTTOM / 2, false, true);
    CHECK(!p.due, "back from a glitch sent");
    // a real jump (two readings that agree) is taken
    perf_xy_touch(&p, 10, 10, false, true);
    perf_xy_touch(&p, 11, 10, false, true);
    CHECK(p.due == (3u << PFLASH_N) && !strcmp(perf_pad_text(&p, 0), "Shut 100") && !strcmp(perf_pad_text(&p, 1), "+100"),
          "a jump: %s, %s", perf_pad_text(&p, 0), perf_pad_text(&p, 1));
    // dragged off the pad's edge: it stays the pad's, at the end
    p.due = 0;
    for (int x = 21; x < LCD_W; x += 25) perf_xy_touch(&p, x, 300, false, true);
    CHECK(!strcmp(perf_pad_text(&p, 0), "Open 100") && !strcmp(perf_pad_text(&p, 1), "-100"), "off the edge: %s, %s",
          perf_pad_text(&p, 0), perf_pad_text(&p, 1));
    CHECK(perf_xy_release(&p, true, true) == PERF_STAY, "the pad's release");
    for (int kn = 0; kn < PERF_PADS; kn++)
        for (int pos = 0; pos < XY_PAD_W; pos++) {
            int idx = knobctl_idx_of(perf_xy_cc(kn, pos, false), 101);
            CHECK(knobctl_idx_of(knobctl_cc_for(idx, 101), 101) == idx, "knob %d at %d", kn, pos);
        }

    // the axes: the next knob the other hasn't got; a touch that starts on a button never moves the pad
    p.due = 0;
    int ax = XY_PAD_W + 40;
    xy_tap(&p, ax, btn_y(XYB_X), 3, true);
    CHECK(p.axis[0] == 2 && p.axis[1] == 1, "across after one tap: %d", p.axis[0]);
    xy_tap(&p, ax, btn_y(XYB_X), 3, true);
    xy_tap(&p, ax, btn_y(XYB_X), 3, true);
    CHECK(p.axis[0] == 0, "across after three: %d", p.axis[0]);
    for (int i = 0; i < 9; i++) {
        xy_tap(&p, ax, btn_y(i % 3 ? XYB_X : XYB_Y), 3, true);
        CHECK(p.axis[0] != p.axis[1] && p.axis[0] < PERF_PADS && p.axis[1] < PERF_PADS, "the axes: %d %d", p.axis[0], p.axis[1]);
    }
    perf_xy_touch(&p, ax, btn_y(XYB_FX), true, true);
    perf_xy_touch(&p, ax, btn_y(XYB_FX), false, true);
    perf_xy_touch(&p, 100, 100, false, true);
    perf_xy_touch(&p, 101, 100, false, true);
    CHECK(!p.due && perf_xy_release(&p, true, true) == PERF_STAY, "a button's touch slid onto the pad");
    CHECK(xy_tap(&p, ax, btn_y(XYB_FX), 3, true) == PERF_GO_FX && xy_tap(&p, ax, btn_y(XYB_LAND), 3, true) == PERF_GO_LAND &&
          xy_tap(&p, ax, btn_y(XYB_FX), 1, true) == PERF_STAY, "the pad's page buttons");
    // reset: all four as a piece starts
    xy_tap(&p, ax, btn_y(XYB_RESET), 3, true);
    CHECK(p.due == (15u << PFLASH_N) && knobctl_idx_of(p.out[27], 101) == 50 && knobctl_idx_of(p.out[28], 101) == 50 && knobctl_idx_of(p.out[29], 101) == 50 && p.out[30] == 0 &&
          !strcmp(perf_pad_text(&p, 0), "Off") && !strcmp(perf_pad_text(&p, 2), "0") && !strcmp(perf_pad_text(&p, 3), "Off"),
          "reset: due %x", (unsigned)p.due);
    // no FM-1: the pad sends nothing
    perf_init(&p);
    xy_tap(&p, 100, 100, 4, false);
    xy_tap(&p, ax, btn_y(XYB_RESET), 3, false);
    CHECK(!p.due && p.idx[0] < 0 && p.n_refused == 2, "the pad with no FM-1");

    // the drawing: a run of touches on each page, drawn bit by bit, against a fresh full redraw after each
    for (int xy = 0; xy < 2; xy++) {
        perf_init(&p);
        nrec = 0;
        clear(fb_a);
        draw(&p, xy);
        decode(fb_a);
        CHECK(!unwritten(fb_a), "page %d: %d pixels never drawn", xy, unwritten(fb_a));
        unsigned r = 12345;
        for (int step = 0; step < 600; step++) {
            r = r * 1103515245u + 12345u;
            int x = (int)(r >> 8) % LCD_W, y = (int)(r >> 18) % PERF_BOTTOM, n = 1 + (int)(r >> 28) % 4;
            bool live = (r >> 5 & 7) != 0;
            for (int i = 0; i < n; i++) {
                if (xy) perf_xy_touch(&p, x + i, y, i == 0, live);
                else perf_fx_touch(&p, x + i, y, i == 0, live);
                draw(&p, xy);
            }
            if (r >> 3 & 1) {
                if (xy) perf_xy_release(&p, n >= 2, live);
                else perf_fx_release(&p, n >= 2);
                draw(&p, xy);
            }
            if (step % 97 == 96) perf_gone(&p), perf_forget_pad(&p), draw(&p, xy);
            decode(fb_a);
            fresh(&p, xy, fb_b);
            int d = differ(fb_a, fb_b);
            CHECK(!d, "page %d step %d: %d pixels differ from a fresh redraw", xy, step, d);
            if (d) break;
        }
    }
    CHECK(!below && !win_bad && !px_bad && !bad_cs, "drawing: %d below the area, %d bad windows, %d short writes, %d unselected",
          below, win_bad, px_bad, bad_cs);
    printf("units: %d checks, %d failed\n", checks, fails);
}

// ---------------------------------------------------------------- pictures

static void shots(const char *dir) {
    perf_t p;
    perf_init(&p);
    fresh(&p, false, fb_a);
    ppm(dir, "effects", fb_a);
    static const char *const H[] = {"Half time", "Macalla", "Shimmer", "Crush", "Ceo", "Vinyl"};
    for (unsigned i = 0; i < sizeof H / sizeof H[0]; i++) fx_tap(&p, cell_of(H[i]), 3, true);
    fresh(&p, false, fb_a);
    ppm(dir, "effects-held", fb_a);
    fresh(&p, true, fb_a);
    ppm(dir, "pad", fb_a);
    perf_xy_touch(&p, 250, 80, true, true);
    perf_xy_touch(&p, 251, 80, false, true);
    perf_xy_release(&p, true, true);
    fresh(&p, true, fb_a);
    ppm(dir, "pad-touched", fb_a);
    xy_tap(&p, XY_PAD_W + 40, btn_y(XYB_X), 3, true);
    xy_tap(&p, XY_PAD_W + 40, btn_y(XYB_X), 3, true);
    perf_xy_touch(&p, 330, 200, true, true);
    perf_xy_touch(&p, 331, 200, false, true);
    perf_xy_release(&p, true, true);
    fresh(&p, true, fb_a);
    ppm(dir, "pad-chorus", fb_a);
}

// ---------------------------------------------------------------- through AISLING's ui.c

static FILE *sf;
static int n_midi, n_expect;
static void flush(perf_t *p, bool say) {   // what the box would send, and what AISLING must say for the pad's
    for (int s = 0; s < PERF_NSLOT; s++) {
        if (!(p->due >> s & 1u)) continue;
        fprintf(sf, "midi B0 %02X %02X\n", perf_slot_cc(s), p->out[s]);
        n_midi++;
        if (s >= PFLASH_N && say) {
            fprintf(sf, "wait 250\nsaid %s %s\n", perf_pad_name(s - PFLASH_N), perf_pad_text(p, s - PFLASH_N));
            n_expect++;
        }
    }
    p->due = 0;
}
static void pcc(const perf_t *p) {
    uint32_t m = 0;
    for (int k = 0; k < PFLASH_N; k++) m |= (uint32_t)p->held[k] << k;
    fprintf(sf, "wait 100\nexpect pcc %u\n", (unsigned)m);
    n_expect++;
}

static void script(const char *out) {
    perf_t p;
    perf_init(&p);
    sf = fopen(out, "w");
    if (!sf) exit(2);
    fprintf(sf, "wait 500\nexpect view 0\nlisten\n");
    // every effect held one at a time (AISLING says its name), then let go; on HOME, as on any page
    for (int c = 0; c < FX_CELLS; c++) {
        int e = perf_fx_at(c);
        if (e < 0) continue;
        fx_tap(&p, c, 3, true);
        flush(&p, false);
        fprintf(sf, "wait 250\nsaid %s\n", PFLASH[e].name);
        pcc(&p);
        fx_tap(&p, c, 3, true);
        flush(&p, false);
        pcc(&p);
    }
    // a run of taps, several held together
    unsigned r = 777;
    for (int i = 0; i < 120; i++) {
        r = r * 1103515245u + 12345u;
        fx_tap(&p, (int)(r >> 10) % (FX_CELLS - 2), 2 + (int)(r >> 20) % 3, true);   // ALL OFF among them
        flush(&p, false);
        pcc(&p);
    }
    // something else held one (a DAW): ALL OFF lets that go too
    fprintf(sf, "midi B0 %02X 7F\nwait 100\n", perf_fx_cc(5));
    fx_tap(&p, cell_of("Reverb"), 3, true);
    flush(&p, false);
    fx_tap(&p, cell_btn(FXB_ALLOFF), 3, true);
    flush(&p, false);
    pcc(&p);
    // held from the box, then PERFORM entered and left on the FM-1: AISLING lets it go, and the box follows (main.c
    // calls perf_gone when the page said stops being PERFORM)
    fx_tap(&p, cell_of("Wash"), 3, true);
    flush(&p, false);
    pcc(&p);
    fprintf(sf, "tap SEL\nwait 300\nexpect view %d\n", PERF_VIEW);
    pcc(&p);
    fprintf(sf, "tap SEL\nwait 300\n");
    perf_gone(&p);
    pcc(&p);
    // the pad, on PERFORM (where AISLING says a fixed CC's knob): each pair of axes, a walk across it
    fprintf(sf, "tap SEL\nwait 300\nexpect view %d\nlisten\n", PERF_VIEW);
    for (int pair = 0; pair < 12; pair++) {
        for (int i = 0; i < 14; i++) {
            r = r * 1103515245u + 12345u;
            int x = (int)(r >> 8) % XY_PAD_W, y = (int)(r >> 18) % PERF_BOTTOM;
            perf_xy_touch(&p, x, y, true, true);
            perf_xy_touch(&p, x, y, false, true);
            perf_xy_release(&p, true, true);
            flush(&p, true);
        }
        if (p.idx[3] >= 0) fprintf(sf, "expect chorus %d\n", p.idx[3]), n_expect++;
        xy_tap(&p, XY_PAD_W + 40, btn_y(pair % 2 ? XYB_Y : XYB_X), 3, true);
    }
    xy_tap(&p, XY_PAD_W + 40, btn_y(XYB_RESET), 3, true);
    flush(&p, true);
    fprintf(sf, "expect chorus 0\nexpect store_writes 0\n");
    fclose(sf);
    printf("script: %d CCs, %d things AISLING must then hold or say\n", n_midi, n_expect);
}

int main(int argc, char **argv) {
    static const panel_t P0 = {0};
    lcd_init(&P0);
    nrec = 0;
    if (argc >= 2 && !strcmp(argv[1], "units")) units();
    else if (argc >= 3 && !strcmp(argv[1], "shots")) shots(argv[2]);
    else if (argc >= 3 && !strcmp(argv[1], "script")) script(argv[2]);
    else return 2;
    return fails ? 1 : 0;
}
