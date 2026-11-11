// perf.c: (test 8) the effects page and the pad. See perf.h.
#include <stdlib.h>
#include <string.h>
#include "perf.h"
#include "knobctl.h"
#include "lcd.h"
#include "tone.h"
#include "ufont.h"

static const uint8_t PAD_CC[PERF_PADS] = {74, 78, 79, 80};   // ui.c's CC_FIXED + {0, 4, 5, 6}: Filter, Wet, Energy, Chorus
static const char *const PAD_NAME[PERF_PADS] = {"Filter", "Wet", "Energy", "Chorus"};

#define C_CELL   RGB(24, 22, 28)
#define C_PADBG  RGB(14, 13, 18)
#define C_GUIDE  RGB(52, 48, 60)
#define C_X      C_ORANGE
#define C_Y      RGB(90, 205, 170)
#define C_DIM    RGB(150, 140, 125)

// ---------------------------------------------------------------- what's where

// The white keys' four groups as PERFORM has them, a row each; the black keys' effects take the fifth column and
// the last two rows, in key order. By name, so pflash.h's order can change under it.
static const char *const WHITE[4][4] = {
    {"Stutter", "Half time", "Gate", "Roll"},
    {"Repeat", "Macalla", "Tape stop", "Reverse"},
    {"Reverb", "Shimmer", "Freeze", "Wash"},
    {"Flange", "Phaser", "Drive", "Crush"},
};
static int8_t CELL[FX_CELLS];
static bool cells_made;

static int by_name(const char *n) {
    for (int k = 0; k < PFLASH_N; k++)
        if (!strcmp(PFLASH[k].name, n)) return k;
    return -1;
}

static void make_cells(void) {
    bool used[PFLASH_N] = {false};
    int8_t black[FX_CELLS];
    int nb = 0;
    for (int c = 0; c < FX_CELLS; c++) CELL[c] = -1;
    for (int r = 0; r < 4; r++)
        for (int i = 0; i < 4; i++) {
            int k = by_name(WHITE[r][i]);
            CELL[r * FX_COLS + i] = (int8_t)k;
            if (k >= 0) used[k] = true;
        }
    for (int k = 0; k < PFLASH_N; k++)
        if (!used[k]) black[nb++] = (int8_t)k;
    int b = 0;
    for (int r = 0; r < 4 && b < nb; r++) CELL[r * FX_COLS + 4] = black[b++];
    for (int c = 4 * FX_COLS; c < FX_CELLS - 3 && b < nb; c++) CELL[c] = black[b++];
    CELL[FX_CELLS - 3] = FXB_ALLOFF;
    CELL[FX_CELLS - 2] = FXB_PAD;
    CELL[FX_CELLS - 1] = FXB_LAND;
    cells_made = true;
}

int perf_fx_at(int cell) {
    if (!cells_made) make_cells();
    return cell >= 0 && cell < FX_CELLS ? CELL[cell] : -1;
}
int perf_fx_cell_at(int x, int y) {
    if (x < 0 || y < 0 || x >= FX_COLS * FX_CW || y >= FX_ROWS * FX_CH) return -1;
    return y / FX_CH * FX_COLS + x / FX_CW;
}
int perf_fx_cc(int k) { return k < 0 || k >= PFLASH_N ? -1 : k < 12 ? 20 + k : 102 + k - 12; }
int perf_slot_cc(int s) { return s < 0 || s >= PERF_NSLOT ? -1 : s < PFLASH_N ? perf_fx_cc(s) : PAD_CC[s - PFLASH_N]; }
int perf_nheld(const perf_t *p) {
    int n = 0;
    for (int k = 0; k < PFLASH_N; k++) n += p->held[k];
    return n;
}
const char *perf_pad_name(int knob) { return knob >= 0 && knob < PERF_PADS ? PAD_NAME[knob] : "?"; }
static int pad_list(int knob) { return knobctl_list_of(PERF_VIEW, knob); }
const char *perf_pad_text(const perf_t *p, int knob) {
    if (knob < 0 || knob >= PERF_PADS || p->idx[knob] < 0) return "";
    const char *t = knobctl_text(pad_list(knob), p->idx[knob]);
    return t ? t : "";
}

static void out(perf_t *p, int slot, int v) {
    p->out[slot] = (uint8_t)v;
    p->due |= 1u << slot;
}

void perf_init(perf_t *p) {
    memset(p, 0, sizeof *p);
    p->fx_cell = p->fx_press = p->xy_btn = p->xy_press = -1;
    p->axis[0] = 0, p->axis[1] = 1;   // Filter across, Wet up
    for (int i = 0; i < PERF_PADS; i++) p->idx[i] = -1;
    p->cross[0] = p->cross[1] = -1;
    p->fx_dirty = 1u << 31;
    p->xy_dirty = XYD_ALL;
}
void perf_forget_pad(perf_t *p) {
    for (int i = 0; i < PERF_PADS; i++) p->idx[i] = -1;
    p->xy_dirty |= XYD_CROSS | XYD_BTN0 << XYB_X | XYD_BTN0 << XYB_Y;
}
void perf_gone(perf_t *p) {
    for (int c = 0; c < FX_CELLS; c++) {
        int k = perf_fx_at(c);
        if (k >= 0 && p->held[k]) p->held[k] = 0, p->fx_dirty |= 1u << c;
    }
    p->due &= ~((1u << PFLASH_N) - 1);   // and nothing of theirs still to send
}

// ---------------------------------------------------------------- the uncial hand

static const ufont_t *uf(int size) { return size >= 24 ? &UFONT_U24 : &UFONT_U15; }
static int ug(char c) {
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    if (c < UFONT_FIRST || c > UFONT_LAST) c = '?';
    return c - UFONT_FIRST;
}
int perf_utext_w(int size, const char *s) {
    const ufont_t *f = uf(size);
    int w = 0;
    for (; *s; s++) w += f->w[ug(*s)];
    return w;
}
static uint16_t ubuf[40 * 36];
int perf_utext(int x, int y, const char *s, int size, uint16_t fg, uint16_t bg) {
    const ufont_t *f = uf(size);
    uint16_t mix[16];
    for (int a = 0; a < 16; a++) {   // fg over bg at each of the 16 alphas, then the picture's warmth
        int r = ((fg >> 11 & 31) * a + (bg >> 11 & 31) * (15 - a) + 7) / 15;
        int g = ((fg >> 5 & 63) * a + (bg >> 5 & 63) * (15 - a) + 7) / 15;
        int b = ((fg & 31) * a + (bg & 31) * (15 - a) + 7) / 15;
        mix[a] = tone_565((uint16_t)(r << 11 | g << 5 | b));
    }
    int x0 = x;
    for (; *s; s++) {
        int gi = ug(*s), w = f->w[gi], h = f->h, rb = (w + 1) / 2;
        if (w * h > (int)(sizeof ubuf / sizeof ubuf[0])) continue;
        const uint8_t *px = f->px + f->at[gi];
        for (int r = 0; r < h; r++)
            for (int c = 0; c < w; c++) {
                uint8_t v = px[r * rb + c / 2];
                ubuf[r * w + c] = mix[c & 1 ? v & 15 : v >> 4];
            }
        lcd_blit(x, y, w, h, ubuf);
        x += w;
    }
    return x - x0;
}

static void caps(char *o, const char *s, int max) {
    int i = 0;
    for (; s[i] && i < max; i++) o[i] = s[i] >= 'a' && s[i] <= 'z' ? (char)(s[i] - 32) : s[i];
    o[i] = 0;
}

// One line centred in a box w wide: the uncial hand if it fits, else the plain font.
static void line_c(int x, int y, int w, const char *s, uint16_t fg, uint16_t bg) {
    char t[24];
    caps(t, s, 23);
    int tw = perf_utext_w(15, t);
    if (tw <= w) perf_utext(x + (w - tw) / 2, y, t, 15, fg, bg);
    else {
        int n = (int)strlen(t);
        if (n * 6 > w) t[n = w / 6] = 0;
        lcd_text(x + (w - n * 6) / 2, y + 7, t, 1, fg, bg);
    }
}

// A name in a box: two words on two lines.
static void name_box(int x, int y, int w, int h, const char *name, uint16_t fg, uint16_t bg) {
    char a[24];
    int lh = uf(15)->h;
    lcd_fill_rect(x, y, w, h, bg);
    strncpy(a, name, 23);
    a[23] = 0;
    char *sp = strchr(a, ' ');
    if (sp && 2 * lh <= h) {
        *sp = 0;
        int top = y + (h - 2 * lh) / 2;
        line_c(x, top, w, a, fg, bg);
        line_c(x, top + lh, w, sp + 1, fg, bg);
    } else {
        line_c(x, y + (h - lh) / 2, w, a, fg, bg);
    }
}

// ---------------------------------------------------------------- the effects page

static void fx_set(perf_t *p, int k, bool on) {
    if (p->held[k] == on) return;
    p->held[k] = on;
    out(p, k, on ? 127 : 0);
    if (on) p->n_holds++;
    else p->n_lets++;
    for (int c = 0; c < FX_CELLS; c++)
        if (perf_fx_at(c) == k) p->fx_dirty |= 1u << c;
}

void perf_fx_touch(perf_t *p, int x, int y, bool first, bool live) {
    int c = perf_fx_cell_at(x, y);
    if (first) p->fx_cell = -1, p->fx_n = 0, p->fx_done = false;
    if (c != p->fx_cell) p->fx_cell = (int8_t)c, p->fx_n = 1;
    else if (p->fx_n < 100) p->fx_n++;
    int e = perf_fx_at(c);
    int press = !p->fx_done && e < -1 ? c : -1;   // a button under the finger shows pressed
    if (press != p->fx_press) {
        if (p->fx_press >= 0) p->fx_dirty |= 1u << p->fx_press;
        if (press >= 0) p->fx_dirty |= 1u << press;
        p->fx_press = (int8_t)press;
    }
    // Two readings in a row in one cell, and the touch does its one thing there: no waiting for the finger to lift,
    // and a finger sliding on changes nothing more.
    if (p->fx_done || c < 0 || p->fx_n < 2) return;
    if (e >= 0) {
        p->fx_done = true;
        if (!live) { p->n_refused++; return; }
        fx_set(p, e, !p->held[e]);
    } else if (e == FXB_ALLOFF) {
        p->fx_done = true;
        if (!live) { p->n_refused++; return; }
        for (int k = 0; k < PFLASH_N; k++) {   // all 27, held here or not: whatever else held one, it's let go
            if (p->held[k]) fx_set(p, k, false);
            else out(p, k, 0);
        }
        p->fx_dirty |= 1u << p->fx_press;
        p->fx_press = -1;
    }
}

int perf_fx_release(perf_t *p, bool ok) {
    int c = p->fx_cell;
    bool done = p->fx_done;
    if (p->fx_press >= 0) p->fx_dirty |= 1u << p->fx_press;
    p->fx_press = p->fx_cell = -1;
    p->fx_n = 0;
    p->fx_done = false;
    if (!ok || done || c < 0) return PERF_STAY;
    int e = perf_fx_at(c);   // the page buttons act as the finger lifts, so it can't land on the next page
    if (e == FXB_PAD)   // to the pad: what's held here is let go, so nothing is left on with no cell in sight to show it
        for (int k = 0; k < PFLASH_N; k++)
            if (p->held[k]) fx_set(p, k, false);
    return e == FXB_PAD ? PERF_GO_PAD : e == FXB_LAND ? PERF_GO_LAND : PERF_STAY;
}

void perf_fx_redraw(perf_t *p) { p->fx_dirty = 1u << 31; }

void perf_fx_draw(perf_t *p) {
    uint32_t d = p->fx_dirty;
    if (!d) return;
    p->fx_dirty = 0;
    if (d >> 31) {
        lcd_fill_rect(0, 0, LCD_W, PERF_BOTTOM, C_BLACK);
        d = (1u << FX_CELLS) - 1;
    }
    for (int c = 0; c < FX_CELLS; c++) {
        if (!(d >> c & 1u)) continue;
        int x = c % FX_COLS * FX_CW + 1, y = c / FX_COLS * FX_CH + 1, w = FX_CW - 2, h = FX_CH - 2;
        int e = perf_fx_at(c);
        if (e >= 0) {
            uint16_t col = PFLASH[e].col;
            name_box(x, y, w, h, PFLASH[e].name, p->held[e] ? C_BLACK : col, p->held[e] ? col : C_CELL);
        } else if (e < -1) {
            bool pr = c == p->fx_press;
            name_box(x, y, w, h, e == FXB_ALLOFF ? "All off" : e == FXB_PAD ? "Pad" : "Land",
                     pr ? C_BLACK : C_VELLUM, pr ? C_GREY : RGB(52, 44, 60));
        } else {
            lcd_fill_rect(x, y, w, h, C_BLACK);
        }
    }
}

// ---------------------------------------------------------------- the pad

int perf_xy_cc(int knob, int px, bool y_axis) {
    int span = (y_axis ? PERF_BOTTOM : XY_PAD_W) - 1 - 2 * XY_MARGIN;
    int d = y_axis ? PERF_BOTTOM - 1 - XY_MARGIN - px : px - XY_MARGIN;
    int cc = d <= 0 ? 0 : d >= span ? 127 : (d * 127 + span / 2) / span;
    if (knob < 3 && abs(cc - 64) <= XY_DETENT) cc = 64;   // Filter, Wet and Energy rest in the middle: easy to find
    return cc;
}
static int xy_px(int knob, int idx, bool y_axis) {   // where a value sits on the pad
    int n = knobctl_list_n(pad_list(knob)), cc = knobctl_cc_for(idx, n);
    int span = (y_axis ? PERF_BOTTOM : XY_PAD_W) - 1 - 2 * XY_MARGIN, d = (cc * span + 63) / 127;
    return y_axis ? PERF_BOTTOM - 1 - XY_MARGIN - d : XY_MARGIN + d;
}
static void pad_send(perf_t *p, int knob, int idx) {
    int n = knobctl_list_n(pad_list(knob));
    p->idx[knob] = (int16_t)idx;
    out(p, PFLASH_N + knob, knobctl_cc_for(idx, n));
    p->n_moves++;
    p->xy_dirty |= XYD_CROSS;
    for (int a = 0; a < 2; a++)
        if (p->axis[a] == knob) p->xy_dirty |= XYD_BTN0 << a;
}
static int xy_btn_at(int x, int y) {
    if (x < XY_PAD_W || x >= LCD_W || y < 0 || y >= PERF_BOTTOM) return -1;
    int b = y / XY_BTN_H;
    return b < XY_BTNS ? b : XY_BTNS - 1;
}
static void xy_press(perf_t *p, int b) {
    if (b == p->xy_press) return;
    if (p->xy_press >= 0) p->xy_dirty |= XYD_BTN0 << p->xy_press;
    if (b >= 0) p->xy_dirty |= XYD_BTN0 << b;
    p->xy_press = (int8_t)b;
}
static bool near(int ax, int ay, int bx, int by) { return abs(ax - bx) <= XY_JUMP_PX && abs(ay - by) <= XY_JUMP_PX; }

void perf_xy_touch(perf_t *p, int x, int y, bool first, bool live) {
    if (first) {
        p->in_pad = x < XY_PAD_W && y < PERF_BOTTOM;
        p->have = p->pend = false;
        p->at[0] = p->at[1] = -1000;   // the first place of a touch is taken as it is
        p->xy_btn = (int8_t)(p->in_pad ? -1 : xy_btn_at(x, y));
    }
    if (!p->in_pad) {   // a button's touch: pressed while the finger is on the one it started on
        int b = xy_btn_at(x, y);
        xy_press(p, b >= 0 && b == p->xy_btn ? b : -1);
        return;
    }
    x = x < 0 ? 0 : x >= XY_PAD_W ? XY_PAD_W - 1 : x;   // the pad's touch stays the pad's, wherever it goes
    y = y < 0 ? 0 : y >= PERF_BOTTOM ? PERF_BOTTOM - 1 : y;
    if (!(p->have && near(x, y, p->hx, p->hy)) && !(p->pend && near(x, y, p->px, p->py))) {
        if (p->have) p->n_glitches++;   // far from the last: held until the next reading agrees with it
        p->px = (int16_t)x, p->py = (int16_t)y;
        p->pend = true;
        return;
    }
    bool was = p->have;
    p->hx = (int16_t)x, p->hy = (int16_t)y;
    p->have = true;
    p->pend = false;
    if (!live) {
        if (!was) p->n_refused++;
        return;
    }
    for (int a = 0; a < 2; a++) {
        int knob = p->axis[a], pos = a ? y : x;
        int idx = knobctl_idx_of(perf_xy_cc(knob, pos, a), knobctl_list_n(pad_list(knob)));
        if (idx == p->idx[knob] || abs(pos - p->at[a]) < XY_SLACK_PX) continue;   // (the panel's jitter)
        p->at[a] = (int16_t)pos;
        pad_send(p, knob, idx);
    }
}

int perf_xy_release(perf_t *p, bool ok, bool live) {
    bool pad = p->in_pad;
    int b = p->xy_press;
    xy_press(p, -1);
    p->in_pad = p->have = p->pend = false;
    p->xy_btn = -1;
    if (pad || !ok || b < 0) return PERF_STAY;
    if (b == XYB_X || b == XYB_Y) {   // the next of the four knobs the other axis hasn't got
        int a = b == XYB_Y, k = (p->axis[a] + 1) % PERF_PADS;
        if (k == p->axis[1 - a]) k = (k + 1) % PERF_PADS;
        p->axis[a] = (uint8_t)k;
        p->xy_dirty |= XYD_CROSS | XYD_BTN0 << b;
    } else if (b == XYB_RESET) {   // all four as a piece starts: Filter off, Wet and Energy 0, Chorus off
        if (!live) p->n_refused++;
        else
            for (int k = 0; k < PERF_PADS; k++) pad_send(p, k, k < 3 ? (knobctl_list_n(pad_list(k)) - 1) / 2 : 0);
        p->xy_dirty |= XYD_BTN0 << XYB_X | XYD_BTN0 << XYB_Y;
    }
    return b == XYB_FX ? PERF_GO_FX : b == XYB_LAND ? PERF_GO_LAND : PERF_STAY;
}

void perf_xy_redraw(perf_t *p) { p->xy_dirty = XYD_ALL; }

static void xy_button(const perf_t *p, int b) {
    int x = XY_PAD_W + 1, y = b * XY_BTN_H + 1, w = LCD_W - XY_PAD_W - 2, h = XY_BTN_H - 2;
    bool pr = b == p->xy_press;
    uint16_t bg = pr ? C_GREY : b <= XYB_Y ? C_CELL : RGB(52, 44, 60);
    if (b > XYB_Y) {
        name_box(x, y, w, h, b == XYB_RESET ? "Reset" : b == XYB_FX ? "Effects" : "Land", pr ? C_BLACK : C_VELLUM, bg);
        return;
    }
    int knob = p->axis[b];
    uint16_t fg = pr ? C_BLACK : b ? C_Y : C_X;
    char t[24];
    lcd_fill_rect(x, y, w, h, bg);
    lcd_text(x + 3, y + 3, b ? "UP" : "ACROSS", 1, pr ? C_BLACK : C_DIM, bg);
    line_c(x, y + 10, w, PAD_NAME[knob], fg, bg);
    caps(t, perf_pad_text(p, knob), 23);
    int n = (int)strlen(t), sc = n * 12 <= w ? 2 : 1;
    if (n * 6 > w) t[n = w / 6] = 0;
    lcd_text(x + (w - n * 6 * sc) / 2, y + (sc == 2 ? 34 : 38), t, sc, pr ? C_BLACK : C_VELLUM, bg);
}

void perf_xy_draw(perf_t *p) {
    uint32_t d = p->xy_dirty;
    if (!d) return;
    p->xy_dirty = 0;
    if (d & XYD_ALL) {
        lcd_fill_rect(0, 0, XY_PAD_W, PERF_BOTTOM, C_PADBG);
        lcd_fill_rect(XY_PAD_W, 0, LCD_W - XY_PAD_W, PERF_BOTTOM, C_BLACK);
        p->cross[0] = p->cross[1] = -1;
        d |= XYD_CROSS | ((1u << XY_BTNS) - 1) * XYD_BTN0;
    }
    if (d & XYD_CROSS) {
        int nx = p->idx[p->axis[0]] < 0 ? -1 : xy_px(p->axis[0], p->idx[p->axis[0]], false);
        int ny = p->idx[p->axis[1]] < 0 ? -1 : xy_px(p->axis[1], p->idx[p->axis[1]], true);
        if ((d & XYD_ALL) || nx != p->cross[0] || ny != p->cross[1]) {
            if (p->cross[0] >= 0) lcd_fill_rect(p->cross[0] - 1, 0, 3, PERF_BOTTOM, C_PADBG);
            if (p->cross[1] >= 0) lcd_fill_rect(0, p->cross[1] - 1, XY_PAD_W, 3, C_PADBG);
            if (p->cross[0] >= 0 && p->cross[1] >= 0) lcd_fill_rect(p->cross[0] - 4, p->cross[1] - 4, 9, 9, C_PADBG);
            lcd_fill_rect(XY_PAD_W / 2 - 1, 0, 1, PERF_BOTTOM, C_GUIDE);   // the middle, where three of the four rest
            lcd_fill_rect(0, PERF_BOTTOM / 2 - 1, XY_PAD_W, 1, C_GUIDE);
            if (nx >= 0) lcd_fill_rect(nx - 1, 0, 3, PERF_BOTTOM, C_X);
            if (ny >= 0) lcd_fill_rect(0, ny - 1, XY_PAD_W, 3, C_Y);
            if (nx >= 0 && ny >= 0) lcd_fill_rect(nx - 4, ny - 4, 9, 9, C_VELLUM);
            p->cross[0] = (int16_t)nx, p->cross[1] = (int16_t)ny;
        }
    }
    for (int b = 0; b < XY_BTNS; b++)
        if (d & XYD_BTN0 << b) xy_button(p, b);
}
