// (test 13) Any firmware on the FM-1: who it is, what the box may send it, its notes on the landscape, and the page
// of what it says. guest.h has the why.
#include <stdio.h>
#include <string.h>
#include "guest.h"
#include "lcd.h"
#include "ui.h"

#define D_HEAD 1u
#define D_NOW  2u
#define D_HIST 4u
#define D_ALL  7u

// The page: the name across the top, the newest thing said large, the ones before it small underneath.
#define BG       C_BLACK
#define DIM      RGB(150, 140, 125)
#define RULE     RGB(70, 62, 78)
#define HEAD_Y   4
#define RULE1_Y  24
#define BODY_Y   30
#define BODY_H   164          // to y 193
#define RULE2_Y  196
#define HIST_Y   202          // GUEST_HIST lines of 10, and one of heading: to y 271
#define MARGIN   6
#define BIG_W    ((UI_W - 2 * MARGIN) / 18)   // (test 18) characters across at scale 3: 25 in 464 (26 in 480)
#define BIG_ROWS 6            // 6 rows of 26 px
#define MID_W    ((UI_W - 2 * MARGIN) / 12)   // at scale 2: 37 (39 in 480)
#define MID_ROWS 9            // 9 x 18 px

void guest_init(guest_t *g) {
    memset(g, 0, sizeof *g);
    g->drawn_scale = -1;
    g->dirty = D_ALL;
}

static void forget(guest_t *g) {
    g->forced = false;
    g->name[0] = 0;
    g->now[0] = 0;
    g->now_warn = false;
    g->n_hist = 0;
    g->n_said = 0;
    memset(g->held_ms, 0, sizeof g->held_ms);
    g->dirty = D_ALL;
}

void guest_plugged(guest_t *g, uint32_t now) {
    forget(g);
    g->who = WHO_ASKING;
    g->since_ms = now;
}

void guest_gone(guest_t *g) {
    forget(g);
    g->who = WHO_NONE;
}

bool guest_named(guest_t *g, const char *name, bool aisling) {
    if (g->who == WHO_NONE) return false;   // an answer still on its way from one already unplugged
    uint8_t was = g->who;
    bool renamed = strncmp(g->name, name, sizeof g->name - 1) != 0;
    if (renamed) {
        snprintf(g->name, sizeof g->name, "%s", name);
        g->dirty |= D_HEAD;
    }
    g->who = aisling ? WHO_AISLING : WHO_GUEST;
    g->forced = false;
    if (g->who != was) g->dirty = D_ALL;
    return g->who != was;
}

bool guest_tick(guest_t *g, uint32_t now) {
    if (g->who != WHO_ASKING || now - g->since_ms < GUEST_ASK_MS) return false;
    g->who = WHO_SILENT;
    g->dirty = D_ALL;
    return true;
}

bool guest_force(guest_t *g) {
    if (g->who == WHO_SILENT) {
        g->who = WHO_AISLING;
        g->forced = true;
    } else if (g->who == WHO_AISLING && g->forced) {
        g->who = WHO_SILENT;
        g->forced = false;
    } else {
        return false;
    }
    g->dirty = D_ALL;
    return true;
}

bool guest_hello_name(const uint8_t *buf, int len, char *out, int n) {
    if (n > 0) out[0] = 0;
    if (n < 1 || len < 5 || buf[0] != 0x7D || buf[1] != 0x53 || buf[2] != 0x52 || buf[3] != 1) return false;
    int o = 0;
    for (int i = 5; i < len && buf[i] && o < n - 1; i++) out[o++] = buf[i] >= 32 && buf[i] < 127 ? (char)buf[i] : ' ';
    out[o] = 0;
    return true;
}

bool guest_may_send(const guest_t *g, bool midi_out) { return midi_out && g->who == WHO_AISLING; }

bool guest_page(const guest_t *g) { return g->who == WHO_GUEST || g->who == WHO_SILENT; }

int guest_layer(const guest_t *g, uint8_t ch, uint8_t note) {
    if (g->who == WHO_AISLING) return ch & 3;
    note &= 127;
    return note < 48 ? 0 : note < 60 ? 1 : note < 72 ? 3 : 2;
}

void guest_note(guest_t *g, uint8_t note, uint8_t vel, uint32_t now) {
    g->held_ms[note & 127] = vel ? (now ? now : 1) : 0;
}

int guest_expire(guest_t *g, uint32_t now) {
    for (int i = 0; i < 128; i++) {
        if (g->held_ms[i] && now - g->held_ms[i] >= GUEST_HOLD_MS) {
            g->held_ms[i] = 0;
            return i;
        }
    }
    return -1;
}

void guest_notes_clear(guest_t *g) { memset(g->held_ms, 0, sizeof g->held_ms); }

// Printable ASCII, runs of spaces as one, none at either end.
static int clean(const char *t, int n, char *out, int max) {
    int o = 0;
    bool gap = false;
    for (int i = 0; i < n && t[i]; i++) {
        char c = t[i] >= 33 && t[i] < 127 ? t[i] : ' ';
        if (c == ' ') { gap = o > 0; continue; }
        if (gap && o < max) out[o++] = ' ';
        gap = false;
        if (o < max) out[o++] = c;
    }
    out[o] = 0;
    return o;
}

void guest_say(guest_t *g, const char *text, int n, uint8_t flags) {
    char t[GUEST_TEXT + 1];
    if (!clean(text, n, t, GUEST_TEXT)) return;
    g->n_said++;
    if (!strcmp(t, g->now)) {   // the same again (a WHERE's whole screen, unchanged): nothing moves
        g->now_warn = (flags & 2) != 0;
        g->dirty |= D_NOW;
        return;
    }
    if (g->now[0]) {
        int keep = g->n_hist < GUEST_HIST ? g->n_hist : GUEST_HIST - 1;
        memmove(g->hist[1], g->hist[0], (size_t)keep * sizeof g->hist[0]);
        size_t l = strlen(g->now);
        if (l > GUEST_HIST_W) {   // cut, and marked as cut
            memcpy(g->hist[0], g->now, GUEST_HIST_W - 2);
            memcpy(g->hist[0] + GUEST_HIST_W - 2, "..", 3);
        } else {
            memcpy(g->hist[0], g->now, l + 1);
        }
        if (g->n_hist < GUEST_HIST) g->n_hist++;
        g->dirty |= D_HIST;
    }
    memcpy(g->now, t, strlen(t) + 1);
    g->now_warn = (flags & 2) != 0;
    g->dirty |= D_NOW;
}

int guest_wrap(const char *t, int w, int max_rows, char rows[][GUEST_HIST_W + 4]) {
    int r = 0;
    if (w > GUEST_HIST_W + 3) w = GUEST_HIST_W + 3;
    while (*t && r < max_rows) {
        while (*t == ' ') t++;
        if (!*t) break;
        int len = (int)strlen(t), take = len;
        if (len > w) {
            take = w;
            // back to the last space that leaves a whole word; none: the word is cut at w
            for (int i = w; i > 0; i--) if (t[i] == ' ') { take = i; break; }
        }
        memcpy(rows[r], t, (size_t)take);
        rows[r][take] = 0;
        while (take > 0 && rows[r][take - 1] == ' ') rows[r][--take] = 0;
        t += strlen(rows[r]);
        r++;
    }
    return r;
}

void guest_redraw(guest_t *g) {
    g->dirty = D_ALL;
    g->drawn_scale = -1;
}

// A line of text and the rest of its row to the right margin in the background, so nothing of the last one shows.
static void line(int y, const char *s, int scale, int w, uint16_t fg) {
    char pad[GUEST_HIST_W + 4];
    snprintf(pad, sizeof pad, "%-*.*s", w, w, s);
    lcd_text(MARGIN, y, pad, scale, fg, BG);
}

static void draw_head(const guest_t *g) {
    char l[40];
    lcd_fill_rect(0, 0, LCD_W, RULE1_Y, BG);
    snprintf(l, sizeof l, "%s", g->who == WHO_SILENT ? "NOT AISLING" : g->name[0] ? g->name : "(NO NAME)");
    lcd_text(MARGIN, HEAD_Y, l, 2, C_ORANGE, BG);
    const char *r = g->who == WHO_SILENT ? "NO SCREEN READER IN IT" : "WHAT ITS SCREEN SAYS";
    lcd_text(UI_W - MARGIN - (int)strlen(r) * 6, HEAD_Y + 6, r, 1, DIM, BG);   // (test 18) to the usable edge
    lcd_fill_rect(0, RULE1_Y, UI_W, 1, RULE);
    lcd_fill_rect(UI_W, RULE1_Y, LCD_W - UI_W, 1, BG);   // (test 18) the rule ends at the usable edge
    lcd_fill_rect(0, RULE1_Y + 1, LCD_W, BODY_Y - RULE1_Y - 1, BG);
}

static void draw_now(guest_t *g) {
    static char rows[MID_ROWS][GUEST_HIST_W + 4];
    const char *t = g->now;
    uint16_t fg = g->now_warn ? C_ORANGE : C_VELLUM;
    if (g->who == WHO_SILENT) {
        t = "This firmware doesn't say what is on its screen. The landscape paints from its notes.";
        fg = DIM;
    } else if (!t[0]) {
        t = "Listening. Nothing said yet.";
        fg = DIM;
    }
    int scale = 3, w = BIG_W, pitch = 26, n = guest_wrap(t, BIG_W, BIG_ROWS + 1, rows);
    if (n > BIG_ROWS) scale = 2, w = MID_W, pitch = 18, n = guest_wrap(t, MID_W, MID_ROWS, rows);
    if (scale != g->drawn_scale) {   // the rows sit elsewhere at the other size
        lcd_fill_rect(0, BODY_Y, LCD_W, BODY_H, BG);
        g->drawn_rows = 0;
    }
    for (int i = 0; i < n; i++) line(BODY_Y + i * pitch, rows[i], scale, w, fg);
    if (n < g->drawn_rows) lcd_fill_rect(0, BODY_Y + n * pitch, LCD_W, (g->drawn_rows - n) * pitch, BG);
    g->drawn_scale = (int8_t)scale;
    g->drawn_rows = (uint8_t)n;
}

static void draw_hist(const guest_t *g) {
    lcd_fill_rect(0, BODY_Y + BODY_H, LCD_W, RULE2_Y - BODY_Y - BODY_H, BG);
    lcd_fill_rect(0, RULE2_Y, UI_W, 1, RULE);
    lcd_fill_rect(UI_W, RULE2_Y, LCD_W - UI_W, 1, BG);   // (test 18) the rule ends at the usable edge
    lcd_fill_rect(0, RULE2_Y + 1, LCD_W, HIST_Y - RULE2_Y - 1, BG);
    line(HIST_Y, g->n_hist ? "BEFORE THAT" : "", 1, GUEST_HIST_W, RULE);
    for (int i = 0; i < GUEST_HIST; i++) line(HIST_Y + 10 + i * 10, i < g->n_hist ? g->hist[i] : "", 1, GUEST_HIST_W, DIM);
    // what no line covers: the two rows under each, and the margins
    for (int i = 0; i <= GUEST_HIST; i++) lcd_fill_rect(0, HIST_Y + 8 + i * 10, LCD_W, 2, BG);
    lcd_fill_rect(0, HIST_Y, MARGIN, GUEST_BOTTOM - HIST_Y, BG);
    lcd_fill_rect(MARGIN + GUEST_HIST_W * 6, HIST_Y, LCD_W - MARGIN - GUEST_HIST_W * 6, GUEST_BOTTOM - HIST_Y, BG);
}

void guest_draw(guest_t *g) {
    uint8_t d = g->dirty;
    g->dirty = 0;
    if (!d) return;
    if (d & D_HEAD) draw_head(g);
    if (d & D_NOW) draw_now(g);
    if (d & D_HIST) draw_hist(g);
}

bool guest_strip(const guest_t *g, char a[80], char b[80]) {
    if (g->who == WHO_GUEST) {
        snprintf(a, 80, "%s: THE LANDSCAPE, AND WHAT ITS SCREEN SAYS", g->name[0] ? g->name : "THIS FIRMWARE");
        snprintf(b, 80, "THE PIECES, KNOBS AND EFFECTS ARE AISLING'S: NOTHING IS SENT TO THIS ONE");
        return true;
    }
    if (g->who == WHO_SILENT) {
        snprintf(a, 80, "NOT AISLING: THE LANDSCAPE ONLY");
        snprintf(b, 80, "THE PIECES, KNOBS AND EFFECTS ARE AISLING'S: NOTHING IS SENT TO THIS ONE");
        return true;
    }
    a[0] = b[0] = 0;
    return false;
}
