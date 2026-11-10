// fm1scr.c (test 6: the FM-1's screen on the box) on the Mac, under AddressSanitizer + UBSan.
//
//  a. The corpus: everything AISLING 1.2 says to a screen reader (corpus.txt, made by fm1-aisling's host build of its
//     own ui.c: every page, every knob through its range, the whole screen as WHERE answers it, every chord, every
//     message), said in order with the flags ui.c gives each, and checked against the text itself, found the plain
//     way: the page, its detail, each knob's label and value, the header, the chord, the section, the scale, the
//     messages and their timing. Then what the corpus doesn't have: a cut text, the filter on the move, evolve and
//     sleep, the Key page, PLAYING from MIDI, the longest messages.
//  b. Anything at all: each corpus line cut at every length, random bytes, and random soup of the grammar's own
//     words, up to 300 bytes. The model stays in range, its strings end, and after the whole screen is said again
//     it's the same as a model told only that.
//  c. End to end: AISLING's own announce.c (included as its firmware includes it) says each line; its frames go
//     through the box's reader.c packed as the FM-1's USB code packs them, and what comes out the other end must
//     build the same model as the text fed straight in. The box's HELLO and WHERE as announce.c takes them.
//  d. The drawing: every word sent to the screen decoded into a frame buffer. Nothing at or below y 272 (the
//     status row), no window out of bounds, no glyph the font lacks, a full redraw covering every pixel of the area,
//     and the screen as drawn bit by bit (only what changed) the same as a fresh full redraw.
// argv: the corpus; then optionally --quick (no b) and --shots DIR (PPM pictures of the screen).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "lcd.h"
#include "tone.h"
#include "font5x7.h"
#include "reader.h"
#include "fm1scr.h"

extern uint32_t rec[];
extern int nrec, bad_cs;

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { if (++fails <= 40) { printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } } while (0)

// ---------------------------------------------------------------- AISLING's announce.c, sending into sent[]

static uint8_t sent[8192];
static uint32_t sent_n, sent_frames;
static int ann_wire_send(const uint8_t *p, uint32_t n);
#include ANNOUNCE_C
static int ann_wire_send(const uint8_t *p, uint32_t n) {
    if (sent_n + n > sizeof sent) return 1;
    memcpy(sent + sent_n, p, n);
    sent_n += n;
    sent_frames++;
    return 0;
}
_Static_assert(FM1_SAY_FLAG_NOW == ANN_NOW && FM1_SAY_FLAG_KEEP == ANN_KEEP, "the flags are announce.c's");

// ---------------------------------------------------------------- the drawing calls, checked, then fm1scr.c

static int draw_bad, glyph_q, glyph_lc, n_texts;
static struct { int x, y, s; char t[96]; } texts[256];   // the texts drawn since n_texts = 0

static bool own_glyph(int c) {
    if (c == ' ') return true;
    if (c < 32 || c > 126) return false;
    for (int i = 0; i < 7; i++)
        if (font5x7[c - 32][i]) return true;
    return false;
}

static void chk_fill_rect(int x, int y, int w, int h, uint16_t c) {
    checks++;
    if (w <= 0 || h <= 0 || x < 0 || y < 0 || x + w > LCD_W || y + h > FM1SCR_BOTTOM) {
        if (++draw_bad <= 5) printf("FAIL a fill out of the area: %d,%d %dx%d\n", x, y, w, h);
    }
    lcd_fill_rect(x, y, w, h, c);
}

static void chk_text(int x, int y, const char *s, int scale, uint16_t fg, uint16_t bg) {
    int n = (int)strlen(s);
    checks++;
    if (scale < 1 || scale > 4 || !n || x < 0 || y < 0 || x + n * 6 * scale > LCD_W || y + 8 * scale > FM1SCR_BOTTOM) {
        if (++draw_bad <= 5) printf("FAIL text out of the area: %d,%d scale %d \"%s\"\n", x, y, scale, s);
    }
    for (const char *p = s; *p; p++)
        if (!own_glyph((unsigned char)*p)) {
            if (*p >= 'a' && *p <= 'z' && own_glyph(*p - 32)) glyph_lc++;   // drawn as its capital
            else glyph_q++;                                                 // drawn as '?'
        }
    if (n_texts < (int)(sizeof texts / sizeof texts[0])) {
        texts[n_texts].x = x, texts[n_texts].y = y, texts[n_texts].s = scale;
        snprintf(texts[n_texts].t, sizeof texts[n_texts].t, "%s", s);
        n_texts++;
    }
    lcd_text(x, y, s, scale, fg, bg);
}

#define lcd_fill_rect chk_fill_rect
#define lcd_text chk_text
#include FM1SCR_C
#undef lcd_fill_rect
#undef lcd_text

static bool drawn_has(const char *s) {
    for (int i = 0; i < n_texts; i++)
        if (!strcmp(texts[i].t, s)) return true;
    return false;
}

// ---------------------------------------------------------------- the frame buffer

#define SENTINEL 0xFFFFFFFFu   // a pixel not written
static uint32_t fb_inc[LCD_W * LCD_H], fb_new[LCD_W * LCD_H];
static int win_bad, px_bad, below;

static void fb_clear(uint32_t *fb) {
    for (int i = 0; i < LCD_W * LCD_H; i++) fb[i] = SENTINEL;
}

// Every word sent since the last decode, into fb: 2Ah and 2Bh set the window, 2Ch's pixels fill it row by row.
static void decode(uint32_t *fb) {
    int c = -1, np = 0, cx0 = 0, cx1 = -1, cy0 = 0, cy1 = -1, x = 0, y = 0;
    long px = 0;
    uint8_t p[4] = {0};
    for (int i = 0; i <= nrec; i++) {
        uint32_t w = i < nrec ? rec[i] : 0;
        if (i == nrec || w == 0xFFFF0000u || !(w >> 16)) {   // the end, a reset, or a command
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
                if (y >= FM1SCR_BOTTOM) below++;
            }
            px++;
            if (++x > cx1) x = cx0, y++;
        }
    }
    nrec = 0;
}

static int unwritten(const uint32_t *fb) {   // pixels of the area (y 0-271) never written
    int n = 0;
    for (int i = 0; i < LCD_W * FM1SCR_BOTTOM; i++) n += fb[i] == SENTINEL;
    return n;
}

static int differ(const uint32_t *a, const uint32_t *b, int *first) {
    int n = 0;
    *first = -1;
    for (int i = 0; i < LCD_W * FM1SCR_BOTTOM; i++)
        if (a[i] != b[i]) {
            if (*first < 0) *first = i;
            n++;
        }
    return n;
}

// The model as a fresh full redraw would draw it, into fb_new.
static void fresh(const fm1scr_t *m, int conn, uint32_t now) {
    fm1scr_t c = *m;
    fm1scr_redraw(&c);
    fb_clear(fb_new);
    nrec = 0;
    fm1scr_draw(&c, conn, now);
    decode(fb_new);
}

static void shot(const char *dir, const char *name, const fm1scr_t *m, int conn) {
    if (!dir) return;
    n_texts = 0;
    fm1scr_t c = *m;   // a message left over from the steps before, timed out (as on the FM-1), but in its own shots
    if (strcmp(name, "warn") && strcmp(name, "message")) c.msg[0] = 0, c.warn = false;
    fresh(&c, conn, 0);
    char path[512];
    snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", LCD_W, LCD_H);
    for (int i = 0; i < LCD_W * LCD_H; i++) {
        uint32_t v = fb_new[i];
        uint8_t rgb[3];
        if (v == SENTINEL) {   // the status row's place below; magenta if the area missed a pixel
            bool st = i / LCD_W >= FM1SCR_BOTTOM;
            rgb[0] = st ? 28 : 255, rgb[1] = st ? 28 : 0, rgb[2] = st ? 28 : 255;
        } else {
            int r = v >> 11 & 31, g = v >> 5 & 63, b = v & 31;
            rgb[0] = (uint8_t)(r << 3 | r >> 2), rgb[1] = (uint8_t)(g << 2 | g >> 4), rgb[2] = (uint8_t)(b << 3 | b >> 2);
        }
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

// ---------------------------------------------------------------- the models

// The first field that differs; NULL if none. loose: what a WHERE doesn't say again (the message, the turned knob,
// Setup's line when not on Setup) is left out.
static const char *model_diff(const fm1scr_t *a, const fm1scr_t *b, bool loose) {
#define S(f) if (strcmp(a->f, b->f)) return #f;
#define I(f) if (a->f != b->f) return #f;
    I(seen) S(piece) S(root) S(mode) S(tempo) I(playing) I(unsaved) I(view) S(detail)
    for (int i = 0; i < 4; i++) S(value[i])
    S(chord) I(next_pct) I(hold) S(section) S(line) S(scale)
    if (!loose || a->view == V_SETUP) S(extra)
    if (!loose) { S(msg) I(warn) I(msg_until) I(turned) if (a->turned >= 0) I(turned_ms) }
#undef S
#undef I
    return NULL;
}

static bool ends(const char *s, size_t cap) { return memchr(s, 0, cap) != NULL; }

static void check_inv(const fm1scr_t *m, const char *what) {
#define E(f) CHECK(ends(m->f, sizeof m->f), "%s: %s not ended", what, #f)
    E(piece); E(root); E(mode); E(tempo); E(detail); E(chord); E(section); E(line); E(extra); E(scale); E(msg);
    for (int i = 0; i < 4; i++) E(value[i]);
#undef E
    CHECK(m->view >= -1 && m->view < FM1_VIEWS, "%s: view %d", what, m->view);
    CHECK(m->turned >= -1 && m->turned < 4, "%s: turned %d", what, m->turned);
    CHECK(m->next_pct >= -1 && m->next_pct <= 100, "%s: next %d", what, m->next_pct);
    CHECK(m->hold >= -1 && m->hold <= 2, "%s: hold %d", what, m->hold);
    CHECK(m->playing >= -1 && m->playing <= 1 && m->unsaved >= -1 && m->unsaved <= 1, "%s: playing %d unsaved %d",
          what, m->playing, m->unsaved);
    CHECK(!(m->dirty & ~FM1_D_ALL), "%s: dirty %x", what, m->dirty);
}

// ---------------------------------------------------------------- what the text says, found the plain way

static void cpy(char *d, size_t cap, const char *s, size_t n) {
    if (n >= cap) n = cap - 1;
    memcpy(d, s, n);
    d[n] = 0;
}

static int split_list(const char *s, size_t len, char out[][48], int max) {
    int n = 0;
    size_t i = 0;
    while (n < max) {
        size_t j = i;
        while (j + 1 < len && !(s[j] == ',' && s[j + 1] == ' ')) j++;
        bool more = j + 1 < len;
        cpy(out[n++], 48, s + i, more ? j - i : len - i);
        if (!more) break;
        i = j + 2;
    }
    return n;
}

typedef struct {
    int view;
    char detail[64];
    int n;
    char item[8][48];      // "Echo time 1/4."
    bool has_head;
    int nh;
    char head[8][48];      // "Skellig", "D Minor", "tempo 76", "playing", "not saved yet"
    bool has_extra;
    char extra[96];        // "CPU 0%, peak 0%"
} pexp_t;

static bool expect_page(const char *t, pexp_t *e) {
    memset(e, 0, sizeof *e);
    e->view = -1;
    const char *pg = strstr(t, " page");
    if (!pg) return false;
    for (int v = 0; v < FM1_VIEWS; v++)
        if (strlen(VIEW_NAME[v]) == (size_t)(pg - t) && !strncmp(t, VIEW_NAME[v], (size_t)(pg - t))) e->view = v;
    if (e->view < 0) return false;
    const char *s = pg + 5, *end = t + strlen(t);
    const char *piece = strstr(s, ". Piece "), *cpu = piece ? strstr(piece, ". CPU ") : NULL;
    const char *kend = piece ? piece : end;
    const char *colon = strstr(s, ": ");
    if (colon && colon > kend) colon = NULL;
    if (s[0] == ',' && s[1] == ' ') {
        const char *d = s + 2, *de = colon ? colon : kend;
        if (!strncmp(d, "voice ", 6)) d += 6;
        else if (!strncmp(d, "character ", 10)) d += 10;
        cpy(e->detail, sizeof e->detail, d, (size_t)(de - d));
    }
    if (colon) e->n = split_list(colon + 2, (size_t)(kend - colon - 2), e->item, 8);
    if (piece) {
        const char *he = cpu ? cpu : end;
        e->has_head = true;
        e->nh = split_list(piece + 8, (size_t)(he - piece - 8), e->head, 8);
    }
    if (cpu) e->has_extra = true, cpy(e->extra, sizeof e->extra, cpu + 2, (size_t)(end - cpu - 2));
    return true;
}

static void check_page(const fm1scr_t *m, const pexp_t *e, const char *t) {
    CHECK(m->view == e->view, "%s: view %d, not %d", t, m->view, e->view);
    CHECK(!strcmp(m->detail, e->detail), "%s: detail \"%s\", not \"%s\"", t, m->detail, e->detail);
    int nv = 0;
    for (int i = 0; i < 4 && m->view >= 0; i++) {
        if (!m->value[i][0]) continue;
        nv++;
        char w[64];
        snprintf(w, sizeof w, "%s %s", KNOB_LABEL[m->view][i], m->value[i]);
        bool found = false;
        for (int j = 0; j < e->n; j++) found |= !strcmp(w, e->item[j]);
        CHECK(found, "%s: knob %d is \"%s\", not in the page", t, i, w);
    }
    CHECK(nv == e->n, "%s: %d knob values, the page says %d", t, nv, e->n);
}

static void check_head(const fm1scr_t *m, const pexp_t *e, const char *t) {
    if (!e->has_head) return;
    CHECK(!strcmp(m->piece, e->head[0]), "%s: piece \"%s\"", t, m->piece);
    char k[48];
    snprintf(k, sizeof k, "%s %s", m->root, m->mode);
    CHECK(e->nh > 1 && !strcmp(k, e->head[1]), "%s: key \"%s\"", t, k);
    bool pl = false, rs = false, ns = false;
    for (int j = 2; j < e->nh; j++) {
        if (!strncmp(e->head[j], "tempo ", 6))
            CHECK(!strcmp(m->tempo, e->head[j] + 6), "%s: tempo \"%s\"", t, m->tempo);
        pl |= !strcmp(e->head[j], "playing"), rs |= !strcmp(e->head[j], "resting");
        ns |= !strcmp(e->head[j], "not saved yet");
    }
    CHECK(pl != rs && m->playing == pl, "%s: playing %d", t, m->playing);
    CHECK(m->unsaved == ns, "%s: unsaved %d", t, m->unsaved);
}

// "D, E, F, G, A, Bb and B, C" as the screen shows it: "D E F G A Bb/B C"
static void scale_shown(const char *s, char *out, size_t cap) {
    size_t n = 0;
    while (*s && n + 1 < cap) {
        if (!strncmp(s, ", ", 2)) out[n++] = ' ', s += 2;
        else if (!strncmp(s, " and ", 5)) out[n++] = '/', s += 5;
        else out[n++] = *s++;
    }
    out[n] = 0;
}

static void check_garden(const fm1scr_t *m, const char *t) {
    const char *c = t + 6, *ce = strstr(c, ". ");
    const char *how = strstr(c, ", ");
    if (how && ce && how > ce) how = NULL;
    char chord[48];
    cpy(chord, sizeof chord, c, (size_t)((how ? how : ce) - c));
    CHECK(!strcmp(m->chord, chord), "%s: chord \"%s\"", t, m->chord);
    if (how) {
        how += 2;
        if (!strncmp(how, "hold", 4)) CHECK(m->hold == 1 && m->next_pct == -1, "%s: hold %d", t, m->hold);
        else if (!strncmp(how, "held", 4)) CHECK(m->hold == 2 && m->next_pct == -1, "%s: hold %d", t, m->hold);
        else CHECK(m->hold == 0 && m->next_pct == atoi(how + 11), "%s: next %d", t, m->next_pct);
    }
    const char *sec = strstr(t, ". Section ");
    if (sec) {
        sec += 10;
        const char *se = strstr(sec, ". ");
        char w[48];
        cpy(w, sizeof w, sec, se ? (size_t)(se - sec) : strlen(sec));
        CHECK(!strcmp(m->section, w), "%s: section \"%s\"", t, m->section);
    }
    const char *sc = strstr(t, ". Scale ");
    if (sc) {
        char w[96];
        scale_shown(sc + 8, w, sizeof w);
        CHECK(!strcmp(m->scale, w), "%s: scale \"%s\", not \"%s\"", t, m->scale, w);
    }
    if (!strstr(t, " moving ")) CHECK(!m->line[0], "%s: line \"%s\"", t, m->line);
}

// The knob a turn names: the longest label of the page with a space and a value after it.
static int expect_knob(int view, const char *t) {
    int best = -1;
    size_t bl = 0;
    for (int i = 0; i < 4; i++) {
        size_t n = strlen(KNOB_LABEL[view][i]);
        if (n > bl && !strncmp(t, KNOB_LABEL[view][i], n) && t[n] == ' ' && t[n + 1]) best = i, bl = n;
    }
    return best;
}

static uint8_t flags_of(const char *kind, const char *t) {
    if (!strcmp(kind, "CHORD")) return 0;                                       // ui.c: a chord change, 0
    if (!strcmp(kind, "WARN")) return ANN_NOW | ANN_KEEP;                       // warn()
    if (!strcmp(kind, "WHERE")) return strncmp(t, "Chord ", 6) ? ANN_NOW : 0;   // speak(): the page NOW, garden 0
    if (!strcmp(kind, "MSG") && !strncmp(t, "Tempo ", 6)) return 0;            // the clock came or went
    return ANN_NOW;                                                             // pages, knobs, say_as, PIECE
}

// ---------------------------------------------------------------- the corpus

#define MAXL 10000
static char *kind[MAXL], *text[MAXL];
static int nl;

static void load(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) { printf("FAIL no corpus %s\n", path); exit(1); }
    char line[512];
    while (fgets(line, sizeof line, f) && nl < MAXL) {
        line[strcspn(line, "\r\n")] = 0;
        char *tab = strchr(line, '\t');
        if (!tab) continue;
        *tab = 0;
        kind[nl] = strdup(line);
        text[nl] = strdup(tab + 1);
        nl++;
    }
    fclose(f);
}

static uint32_t now = 1000;
static fm1scr_t m;

// One say, as main.c does it: tick, say, draw what changed into fb_inc.
static void say(fm1scr_t *mm, const char *t, uint8_t flags, uint32_t step) {
    now += step;
    fm1scr_tick(mm, now);
    mm->want_where = false;
    fm1scr_say(mm, t, (uint32_t)strlen(t), flags, now);
    n_texts = 0;
    fm1scr_draw(mm, 2, now);
    decode(fb_inc);
}

static void compare_fresh(const fm1scr_t *mm, const char *t) {
    int first;
    fresh(mm, 2, now);
    int u = unwritten(fb_new), d = differ(fb_inc, fb_new, &first);
    CHECK(!u, "%s: a full redraw left %d pixels of the area unwritten", t, u);
    CHECK(!d, "%s: drawn bit by bit, %d pixels differ from a full redraw (first at %d,%d)", t, d,
          first % LCD_W, first / LCD_W);
}

static void caps(char *out, const char *s) { fm1scr_caps(out, s, 160); }

// The header's texts as drawn, joined (a message on one line, or two).
static void head_drawn(char *out, size_t cap) {
    out[0] = 0;
    for (int i = 0; i < n_texts; i++)
        if (texts[i].y < 44) {
            size_t n = strlen(out);
            snprintf(out + n, cap - n, "%s%s", n ? " " : "", texts[i].t);
        }
}

static int count_kind(const char *k) {
    int n = 0;
    for (int i = 0; i < nl; i++) n += !strcmp(kind[i], k);
    return n;
}

// ---------------------------------------------------------------- a. the corpus, in order

static void corpus_walk(const char *shots) {
    int cur = -1, shots_taken = 0;
    for (int i = 0; i < nl; i++) {
        const char *k = kind[i], *t = text[i];
        uint8_t fl = flags_of(k, t);
        fm1scr_t b = m;
        say(&m, t, fl, 100);
        check_inv(&m, t);
        CHECK(m.n_unmatched == 0, "%s: unmatched", t);
        CHECK(m.n_cut == 0, "%s: taken as cut", t);
        pexp_t e;

        if (!strcmp(k, "SCREEN") || (!strcmp(k, "WHERE") && strncmp(t, "Chord ", 6))) {
            CHECK(expect_page(t, &e), "%s: not a page", t);
            CHECK(m.n_page == b.n_page + 1, "%s: not taken as a page", t);
            check_page(&m, &e, t);
            if (e.has_head) {
                CHECK(m.n_header == b.n_header + 1, "%s: header not taken", t);
                check_head(&m, &e, t);
            }
            char a[48];
            caps(a, VIEW_NAME[e.view]);
            if (e.view != b.view) CHECK(drawn_has(a), "%s: the page's name not drawn", t);
            if (e.view == V_KEY && e.has_head == false) {   // the Key page's knobs are the header's key and tempo
                CHECK(!strcmp(m.tempo, m.value[0]) && !strcmp(m.root, m.value[1]) && !strcmp(m.mode, m.value[2]),
                      "%s: header %s %s %s", t, m.tempo, m.root, m.mode);
            }
            cur = e.view;
        } else if (!strcmp(k, "WHERE")) {
            CHECK(m.n_garden == b.n_garden + 1, "%s: not taken as the garden", t);
            check_garden(&m, t);
        } else if (!strcmp(k, "KNOB")) {
            int ki = cur >= 0 ? expect_knob(cur, t) : -1;
            CHECK(ki >= 0, "%s: no knob of page %d", t, cur);
            CHECK(m.n_knob == b.n_knob + 1 && m.n_msg == b.n_msg, "%s: not taken as a knob", t);
            if (ki >= 0) {
                CHECK(m.turned == ki, "%s: turned %d, not %d", t, m.turned, ki);
                CHECK(!strcmp(m.value[ki], t + strlen(KNOB_LABEL[cur][ki]) + 1), "%s: value \"%s\"", t, m.value[ki]);
                char a[32];
                fm1scr_caps(a, m.value[ki], 30);
                CHECK(drawn_has(a), "%s: its value not drawn", t);
                if (cur == V_KEY && (ki == 1 || ki == 2)) CHECK(m.want_where, "%s: a new key wants the scale", t);
                if (cur == V_KEY && ki == 0) CHECK(!strcmp(m.tempo, m.value[0]), "%s: tempo %s", t, m.tempo);
                if (cur == V_KEY && ki == 1) CHECK(!strcmp(m.root, m.value[1]), "%s: root %s", t, m.root);
                if (cur == V_KEY && ki == 2) CHECK(!strcmp(m.mode, m.value[2]), "%s: mode %s", t, m.mode);
            }
        } else if (!strcmp(k, "CHORD")) {
            CHECK(m.n_chord == b.n_chord + 1, "%s: not a chord", t);
            CHECK(!strcmp(m.chord, t + 6), "%s: chord \"%s\"", t, m.chord);
            char sym[48];
            chord_symbol(m.chord, sym, 40);
            CHECK(!strchr(sym, ' ') && strlen(sym) <= 7, "%s: shown as \"%s\"", t, sym);
            CHECK(drawn_has(sym), "%s: \"%s\" not drawn", t, sym);
        } else {   // MSG, WARN
            if (!strncmp(t, "Tempo ", 6)) {
                CHECK(m.n_msg == b.n_msg && !strcmp(m.msg, b.msg), "%s: a message", t);
                CHECK(!strcmp(m.tempo, t + 6), "%s: tempo %s", t, m.tempo);
            } else {
                char want[160];
                if (!strncmp(t, "PIECE ", 6)) {
                    const char *c = strchr(t, ',');
                    snprintf(want, sizeof want, "%.*s", (int)(c - t), t);
                    char k2[48];
                    snprintf(k2, sizeof k2, "%s %s, tempo %s", m.root, m.mode, m.tempo);
                    CHECK(!strcmp(k2, c + 2), "%s: header \"%s\"", t, k2);
                    CHECK(!strncmp(m.piece, t + 6, strlen(m.piece)) && t[6 + strlen(m.piece)] == ',', "%s: piece %s",
                          t, m.piece);
                    CHECK(m.unsaved == b.unsaved, "%s: unsaved changed", t);
                } else {
                    snprintf(want, sizeof want, "%s", t);
                }
                CHECK(m.n_msg == b.n_msg + 1 && !strcmp(m.msg, want), "%s: message \"%s\"", t, m.msg);
                bool w = !strcmp(k, "WARN");
                CHECK(m.warn == w && m.msg_until == now + (w ? 4000u : 1500u), "%s: warn %d until %u", t, m.warn,
                      m.msg_until - now);
                char got[200], c2[200];
                head_drawn(got, sizeof got);
                caps(c2, want);
                CHECK(!strcmp(got, c2), "%s: drawn as \"%s\"", t, got);
                if (!strncmp(t, "MODE ", 5)) CHECK(!strcmp(m.mode, t + 5), "%s: mode %s", t, m.mode);
                if (!strncmp(t, "TEMPO ", 6)) CHECK(!strcmp(m.tempo, t + 6), "%s: tempo %s", t, m.tempo);
                if (!strcmp(t, "SAVED")) CHECK(m.unsaved == 0, "%s: unsaved %d", t, m.unsaved);
                if (!strcmp(t, "PLAYING") || !strcmp(t, "RESTING")) CHECK(m.playing == (t[1] == 'L'), "%s", t);
            }
        }
        // the screen drawn bit by bit against a full redraw (every line but most knob turns)
        if (strcmp(k, "KNOB") || i % 37 == 0) compare_fresh(&m, t);

        // and a few named by what they say: the checks above found these the plain way; these say what they are
        if (!strcmp(t, "Aisling page: Filter Off, Evolve 40, Motion 40, Drift 16 bars. Piece Skellig, C Minor, tempo 76, resting"))
            CHECK(!strcmp(m.piece, "Skellig") && !strcmp(m.root, "C") && !strcmp(m.mode, "Minor") &&
                  !strcmp(m.tempo, "76") && m.playing == 0 && m.unsaved == 0 && !strcmp(m.value[3], "16 bars"), "8628");
        if (!strcmp(t, "Chord F minor 7, next chord 0% of the way. Section Emerge. Scale C, D, Eb, F, G, Ab, Bb"))
            CHECK(!strcmp(m.chord, "F minor 7") && m.next_pct == 0 && m.hold == 0 && !strcmp(m.section, "Emerge") &&
                  !strcmp(m.scale, "C D Eb F G Ab Bb") && !m.line[0], "8629");
        if (!strncmp(t, "Setup page, sleep off: Tune 0, MIDI out On, Lights On, Save Save. Piece", 72))
            CHECK(!strcmp(m.value[3], "Save") && !strcmp(m.value[1], "On") && !strcmp(m.detail, "sleep off") &&
                  m.unsaved == 1 && !strcmp(m.extra, "CPU 0%  peak 0%"), "8632: extra \"%s\"", m.extra);
        if (!strncmp(t, "Sequence page, voice Harp: Density 55, Octave 0, Length 12, Grid 16ths. Piece", 78))
            CHECK(!strcmp(m.tempo, "EXT") && m.playing == 1 && m.unsaved == 1 && !strcmp(m.detail, "Harp"), "8634");
        if (!strncmp(t, "Space page: Echo 55, Echo time 1/4., Feedback 6.0 s, Reverb 55. Piece", 70))
            CHECK(m.unsaved == 0 && m.playing == 0, "8636: unsaved %d", m.unsaved);
        if (!strcmp(t, "Chord F minor 7, hold. Section Flow. Scale C#, Eb and E, F#, Ab and A, B"))
            CHECK(m.hold == 1 && m.next_pct == -1 && !strcmp(m.scale, "C# Eb/E F# Ab/A B"), "8639");
        if (!strncmp(t, "Sequence 2 page, voice Moog: Chance 100, Octave -1, Length 7, Level 0. Piece", 77))
            CHECK(!strcmp(m.root, "F#") && !strcmp(m.mode, "Custom") && !strcmp(m.value[1], "-1"), "8640");
        if (!strcmp(t, "Space page: Echo 55, Echo time 1/4., Feedback 6.0 s, Reverb 55"))
            CHECK(!strcmp(m.value[0], "55") && !strcmp(m.value[1], "1/4.") && !strcmp(m.value[2], "6.0 s"), "Space");
        if (!strcmp(t, "Mix page: Drone 38, Seq 44, Lead 76, Seq 2 0"))
            CHECK(!strcmp(m.value[1], "44") && !strcmp(m.value[3], "0"), "Mix");
        if (!strcmp(t, "Pattern page, voice Moog: Lane Steps, Accent 50, Chance 100%, Play Auto"))
            CHECK(!strcmp(m.value[2], "100%") && !strcmp(m.detail, "Moog"), "Pattern");
        if (!strcmp(t, "Chords page, walk Home: Slot 1, Chord Minor, Type 7, Play Loop"))
            CHECK(!strcmp(m.detail, "walk Home") && !strcmp(m.value[1], "Minor") && m.n_chord == b.n_chord, "Chords");
        if (!strcmp(t, "Key page: Tempo 76, Root D, Mode Minor, Transpose Parallel"))
            CHECK(!strcmp(m.root, "D") && !strcmp(m.mode, "Minor") && !strcmp(m.tempo, "76"), "Key");
        if (!strcmp(t, "Setup page, sleep off: Tune 0, MIDI out On, Lights On, Save Saved"))
            CHECK(!strcmp(m.value[1], "On") && !strcmp(m.value[3], "Saved") && m.unsaved == 0, "Setup");
        if (!strcmp(t, "Repeats page, voice Harp: Repeat A 100, A time 1/8., Repeat B 0, B time 1/4"))
            CHECK(!strcmp(m.value[1], "1/8.") && !strcmp(m.value[3], "1/4"), "Repeats");
        if (!strcmp(t, "MIDI out Off") || !strcmp(t, "A time 1/4") || !strcmp(t, "B time 1/2"))
            CHECK(m.n_knob == b.n_knob + 1 && m.n_msg == b.n_msg, "%s: a knob", t);

        if (shots && !strcmp(k, "WHERE") && strncmp(t, "Chord ", 6) && shots_taken < 3) {
            // the whole screen, after its garden comes too
            fm1scr_t c = m;
            fm1scr_say(&c, text[i + 1], (uint32_t)strlen(text[i + 1]), 0, now);
            char name[32];
            snprintf(name, sizeof name, "where%d", ++shots_taken);
            shot(shots, name, &c, 2);
        }
    }
}

// ---------------------------------------------------------------- a2. what the corpus doesn't have

static void pad_to(char *out, size_t cap, const char *s, size_t n) {   // s, then x's to n characters
    snprintf(out, cap, "%s", s);
    for (size_t k = strlen(out); k < n && k + 1 < cap; k++) out[k] = 'x', out[k + 1] = 0;
}

static void beyond(const char *shots) {
    char t[400], got[200];
    fm1scr_t b;

    // the filter on the move, said in the garden of a sound page
    say(&m, "Seq sound page, voice Harp: Cutoff 55, Decay 80, Reverb 50, Echo 50", ANN_NOW, 100);
    say(&m, "Chord A minor, next chord 40% of the way. Section Flow. Cutoff moving darker, about -1 octave. "
            "Scale A, B, C, D, E, F, G", 0, 100);
    CHECK(!strcmp(m.line, "Cutoff darker -1 octave") && m.next_pct == 40 && !strcmp(m.chord, "A minor") &&
          !strcmp(m.scale, "A B C D E F G"), "moving: line \"%s\"", m.line);
    CHECK(drawn_has("CUTOFF DARKER -1 OCTAVE") && drawn_has("Am"), "moving: not drawn");
    compare_fresh(&m, "moving");
    shot(shots, "moving", &m, 2);
    say(&m, "Chord A minor, next chord 60% of the way. Section Flow. Cutoff moving brighter, about half an octave. "
            "Scale A, B, C, D, E, F, G", 0, 100);
    CHECK(!strcmp(m.line, "Cutoff brighter 1/2 octave") && m.next_pct == 60, "moving: line \"%s\"", m.line);
    say(&m, "Chord C major, next chord 10% of the way. Section Flow. Scale A, B, C, D, E, F, G", 0, 100);
    CHECK(!m.line[0], "still: line \"%s\"", m.line);
    compare_fresh(&m, "still");

    // a text announce.c may have cut (230 or more): its last item is left out, the rest taken
    say(&m, "Space page: Echo 55, Echo time 1/4., Feedback 6.0 s, Reverb 55", ANN_NOW, 100);
    pad_to(t, sizeof t, "Space page: Echo 60, Echo time 1/8, Feedback 3.0 s, Reverb 5", 235);
    b = m;
    say(&m, t, ANN_NOW, 100);
    CHECK(m.n_cut == b.n_cut + 1 && !strcmp(m.value[0], "60") && !strcmp(m.value[1], "1/8") &&
          !strcmp(m.value[2], "3.0 s") && !strcmp(m.value[3], "55"), "cut knobs: %s %s %s %s", m.value[0],
          m.value[1], m.value[2], m.value[3]);
    pad_to(t, sizeof t, "Chord B minor, next chord 50% of the way. Section Solo. Scale B, C#, D", 235);
    say(&m, t, 0, 100);
    CHECK(!strcmp(m.chord, "B minor") && !strcmp(m.section, "Solo") && !strcmp(m.scale, "A B C D E F G") &&
          m.next_pct == 50, "cut garden: scale \"%s\"", m.scale);
    say(&m, "Setup page, sleep off: Tune 0, MIDI out On, Lights On, Save Saved", ANN_NOW, 100);
    pad_to(t, sizeof t, "Setup page, sleep off: Tune 0, MIDI out On, Lights On, Save Saved. Piece Skellig, D Minor, "
                        "tempo 76, playing, not saved yet. CPU 3%, peak 12%, on USB po", 235);
    say(&m, t, ANN_NOW, 100);
    CHECK(!strcmp(m.extra, "CPU 3%  peak 12%") && m.unsaved == 1 && m.playing == 1, "cut extra: \"%s\"", m.extra);
    say(&m, "Setup page, sleep in 15 minutes: Tune 0, MIDI out On, Lights On, Save Save. Piece Tir na nOg, D Hex maj, "
            "tempo 120, playing, not saved yet. CPU 100%, peak 100%, battery about 60%, 2 dropouts", ANN_NOW, 100);
    CHECK(!strcmp(m.extra, "CPU 100%  peak 100%  battery 60%  2 dropouts") && !strcmp(m.detail, "sleep in 15 minutes")
          && !strcmp(m.piece, "Tir na nOg") && !strcmp(m.mode, "Hex maj"), "battery: \"%s\"", m.extra);
    CHECK(drawn_has("CPU 100%  PEAK 100%  BATTERY 60%  2 DROPOUTS"), "battery not drawn");
    compare_fresh(&m, "battery");
    shot(shots, "setup", &m, 2);
    pad_to(t, sizeof t, "Drone page, voice Pipes: Pedal 90, Reso 0, Phaser 15, Attack 40. Piece Skellig, D Minor, "
                        "tempo 76, resting, not saved ye", 235);
    say(&m, t, ANN_NOW, 100);
    CHECK(m.unsaved == 1 && m.playing == 0 && m.view == 1, "cut header: unsaved %d", m.unsaved);
    say(&m, "Drone page, voice Pipes: Pedal 90, Reso 0, Phaser 15, Attack 40. Piece Skellig, D Minor, tempo 76, "
            "resting", ANN_NOW, 100);
    CHECK(m.unsaved == 0, "whole header: unsaved %d", m.unsaved);

    // sleep (on Setup only) and evolve (on the Aisling page only)
    say(&m, "Setup page, sleep off: Tune 0, MIDI out On, Lights On, Save Saved", ANN_NOW, 100);
    say(&m, "sleep in 30 minutes", ANN_NOW, 100);
    CHECK(!strcmp(m.detail, "sleep in 30 minutes") && !strcmp(m.msg, "sleep in 30 minutes"), "sleep: %s", m.detail);
    say(&m, "Aisling page, evolve every 8 bars: Filter Off, Evolve 40, Motion 40, Drift 16 bars", ANN_NOW, 100);
    CHECK(!strcmp(m.detail, "evolve every 8 bars"), "evolve page: \"%s\"", m.detail);
    say(&m, "sleep in 1 hour", ANN_NOW, 100);
    CHECK(!strcmp(m.detail, "evolve every 8 bars"), "sleep off Setup: \"%s\"", m.detail);
    say(&m, "evolve now and then", ANN_NOW, 100);
    CHECK(!m.detail[0], "evolve now and then: \"%s\"", m.detail);
    say(&m, "evolve every 32 bars", ANN_NOW, 100);
    CHECK(!strcmp(m.detail, "evolve every 32 bars") && m.n_unmatched == 0, "evolve: \"%s\"", m.detail);
    compare_fresh(&m, "evolve");

    // the knobs whose labels aren't one capital and lower case, and the Key page's Tempo
    say(&m, "Setup page, sleep off: Tune 0, MIDI out On, Lights On, Save Saved", ANN_NOW, 100);
    b = m;
    say(&m, "MIDI out Off", ANN_NOW, 100);
    CHECK(m.n_knob == b.n_knob + 1 && m.turned == 1 && !strcmp(m.value[1], "Off") && m.n_msg == b.n_msg, "MIDI out");
    say(&m, "Repeats page, voice Harp: Repeat A 100, A time 1/8., Repeat B 0, B time 1/4", ANN_NOW, 100);
    say(&m, "A time 1/16", ANN_NOW, 100);
    CHECK(m.turned == 1 && !strcmp(m.value[1], "1/16"), "A time: %s", m.value[1]);
    say(&m, "B time 1/2.", ANN_NOW, 100);
    CHECK(m.turned == 3 && !strcmp(m.value[3], "1/2."), "B time: %s", m.value[3]);
    say(&m, "Key page: Tempo 76, Root D, Mode Minor, Transpose Parallel", ANN_NOW, 100);
    b = m;
    say(&m, "Tempo 77", ANN_NOW, 100);
    CHECK(m.n_knob == b.n_knob + 1 && m.turned == 0 && !strcmp(m.tempo, "77") && !strcmp(m.value[0], "77") &&
          m.n_msg == b.n_msg, "Key Tempo");
    b = m;
    say(&m, "Tempo EXT", 0, 100);   // the clock came, on the Key page: the value, not a turn
    CHECK(m.n_knob == b.n_knob && !strcmp(m.value[0], "EXT") && !strcmp(m.tempo, "EXT") && m.n_msg == b.n_msg,
          "Key Tempo EXT");
    say(&m, "Root E", ANN_NOW, 100);
    CHECK(m.want_where && !strcmp(m.root, "E") && m.turned == 1, "Root: want_where %d", m.want_where);
    compare_fresh(&m, "Key");
    shot(shots, "key", &m, 2);
    say(&m, "Aisling page: Filter Off, Evolve 40, Motion 40, Drift 16 bars", ANN_NOW, 100);
    b = m;
    say(&m, "Tempo 80", 0, 100);
    CHECK(!strcmp(m.tempo, "80") && m.n_knob == b.n_knob && m.n_msg == b.n_msg && !strcmp(m.value[0], "Off"),
          "Tempo off Key");

    // PLAYING and RESTING from MIDI start and stop (0) aren't messages; from the button (NOW) they are
    b = m;
    say(&m, "RESTING", 0, 100);
    CHECK(m.playing == 0 && m.n_msg == b.n_msg, "RESTING 0");
    say(&m, "PLAYING", 0, 100);
    CHECK(m.playing == 1 && m.n_msg == b.n_msg, "PLAYING 0");
    say(&m, "RESTING", ANN_NOW, 100);
    CHECK(m.playing == 0 && m.n_msg == b.n_msg + 1 && !strcmp(m.msg, "RESTING"), "RESTING NOW");

    // a message's time, and a warning's; the turned knob's
    say(&m, "Filter 40", ANN_NOW, 100);
    uint32_t t0 = now;
    CHECK(m.turned == 0, "turned");
    fm1scr_tick(&m, t0 + 1999);
    CHECK(m.turned == 0, "turned too short");
    m.dirty = 0;
    fm1scr_tick(&m, t0 + 2000);
    CHECK(m.turned == -1 && (m.dirty & FM1_D_KNOB), "turned too long");
    say(&m, "NOT SAVED FLASH ERROR", ANN_NOW | ANN_KEEP, 100);
    t0 = now;
    CHECK(m.warn && drawn_has("NOT SAVED FLASH ERROR"), "warn");
    shot(shots, "warn", &m, 2);
    fm1scr_tick(&m, t0 + 3999);
    CHECK(m.msg[0], "warn too short");
    m.dirty = 0;
    fm1scr_tick(&m, t0 + 4000);
    CHECK(!m.msg[0] && !m.warn && (m.dirty & FM1_D_HEAD), "warn too long");
    {   // across the clock's wrap
        fm1scr_t w = m;
        fm1scr_say(&w, "SEQ MUTED", 9, ANN_NOW, 0xFFFFFF00u);
        fm1scr_tick(&w, 0xFFFFFF00u + 1499u);
        CHECK(w.msg[0], "wrap: too short");
        fm1scr_tick(&w, 0xFFFFFF00u + 1500u);
        CHECK(!w.msg[0], "wrap: too long");
    }

    // the longest messages: 25 at scale 3, 26 and on two lines at scale 2, broken at a space
    say(&m, "ABCDEFGHIJKLMNOPQRSTUVWXY", ANN_NOW, 100);
    CHECK(drawn_has("ABCDEFGHIJKLMNOPQRSTUVWXY") && n_texts && texts[0].s == 3, "25 not whole at 3");
    say(&m, "ABCDEFGHIJKLMNOPQRSTUVWXYZ", ANN_NOW, 100);
    CHECK(drawn_has("ABCDEFGHIJKLMNOPQRSTUVWXYZ"), "26 not whole");
    const char *long_msg = "UPDATE MODE CANCELLED BECAUSE THE BUTTON WAS LET GO TOO SOON";
    say(&m, long_msg, ANN_NOW, 100);
    head_drawn(got, sizeof got);
    CHECK(!strcmp(got, long_msg), "two lines: \"%s\"", got);
    shot(shots, "message", &m, 2);
    say(&m, "Section names, scales and keys: all of them, in the order the pieces use them, with more after that", ANN_NOW,
        100);
    head_drawn(got, sizeof got);
    char c2[200];
    caps(c2, m.msg);
    CHECK(strlen(got) >= 70 && !strncmp(got, c2, strlen(got)), "too long: \"%s\"", got);
    compare_fresh(&m, "messages");

    // a PIECE over the header: the header changes under it, without its "not saved yet"
    say(&m, "PIECE Burren, G Mixolyd., tempo 84", ANN_NOW, 100);
    CHECK(!strcmp(m.root, "G") && !strcmp(m.mode, "Mixolyd.") && !strcmp(m.tempo, "84") && !strcmp(m.piece, "Burren") &&
          drawn_has("PIECE BURREN"), "PIECE Burren");
    fm1scr_tick(&m, now + 1500);
    n_texts = 0;
    fm1scr_draw(&m, 2, now + 1500);
    decode(fb_inc);
    CHECK(drawn_has("BURREN") && drawn_has("G MIXOLYD.   TEMPO 84"), "the header after PIECE");
    compare_fresh(&m, "PIECE");

    CHECK(glyph_q == 0 && glyph_lc == 0, "%d glyphs drawn as '?', %d as their capitals", glyph_q, glyph_lc);
}

// ---------------------------------------------------------------- c. end to end, through announce.c and reader.c

static uint32_t unpack(const uint8_t *pk, uint32_t n, uint8_t *out) {   // USB-MIDI packets to bytes
    uint32_t k = 0;
    for (uint32_t i = 0; i + 4 <= n; i += 4) {
        uint32_t cin = pk[i] & 15u, nb = cin == 4 || cin == 7 ? 3 : cin == 6 ? 2 : cin == 5 ? 1 : 0;
        for (uint32_t j = 0; j < nb; j++) out[k++] = pk[i + 1 + j];
    }
    return k;
}

static int box_piece;
static const char *const NAMES[8] = {"Skellig", "Burren", "Aran", "Carolan", "Blasket", "Samhain", "Connemara",
                                     "Tir na nOg"};   // reader.c's, in program-change order

// sent[], packed as the FM-1's usb.c packs SysEx (three bytes a packet, CIN 4; an end in CIN 5/6/7), into the reader;
// each SAY that comes out goes to fm1scr_say as main.c's drain_said gives it.
static void wire(reader_t *r, fm1scr_t *mm, uint32_t t) {
    for (uint32_t i = 0; i < sent_n;) {
        uint32_t k = sent_n - i < 3 ? sent_n - i : 3;
        for (uint32_t j = 0; j < k; j++)
            if (sent[i + j] == 0xF7) { k = j + 1; break; }
        uint8_t pk[4] = {0};
        pk[0] = (uint8_t)(sent[i + k - 1] == 0xF7 ? 4 + k : 4);
        memcpy(pk + 1, sent + i, k);
        int g = reader_packet(r, pk);
        if (g >= 0) box_piece = g;
        if (r->said_new) {
            char txt[READER_MAX - 5];
            uint16_t n = r->said_n;
            CHECK(n <= sizeof txt, "a SAY of %u", n);
            memcpy(txt, r->buf + 5, n);
            r->said_new = false;
            fm1scr_say(mm, txt, n, r->said_flags, t);
        }
        i += k;
    }
    sent_n = 0;
}

static void end_to_end(void) {
    reader_t r = {0};
    uint8_t pk[12], by[16];
    uint32_t t = 5000;

    // the box's HELLO: the reader goes on there, the FM-1 answers, and wants to say the whole screen
    uint32_t n = unpack(pk, reader_hello(pk), by);
    CHECK(n == 7 && by[0] == 0xF0 && by[n - 1] == 0xF7, "hello unpacked");
    CHECK(ann_frame(by + 1, n - 2, t) == 1 && ann_on(), "hello not taken");
    CHECK(ann_where() == 1 && ann_where() == 0, "a new reader gets the whole screen, once");
    fm1scr_t m1, m2;
    fm1scr_init(&m1);
    fm1scr_init(&m2);
    wire(&r, &m2, t);
    CHECK(r.answered == 1 && r.said == 0 && !m2.seen, "the answer: %u answered, %u said", r.answered, r.said);

    // the box's WHERE, as announce.c takes it
    n = unpack(pk, reader_where(pk), by);
    CHECK(n == 6 && ann_frame(by + 1, n - 2, t) == 1 && ann_where() == 1, "where not taken");

    // every line, both ways
    int pieces = 0;
    for (int i = 0; i < nl; i++) {
        const char *k = kind[i], *s = text[i];
        uint8_t fl = flags_of(k, s);
        t += 100;
        n = sent_frames;
        ann_say(s, 0, fl);
        CHECK(sent_frames == n + 1, "%s: not sent", s);
        box_piece = -1;
        wire(&r, &m2, t);
        fm1scr_say(&m1, s, (uint32_t)strlen(s), fl, t);
        const char *d = model_diff(&m1, &m2, false);
        CHECK(!d, "%s: through the wire, %s differs", s, d);
        CHECK(m1.n_said == m2.n_said, "%s: said %u, wired %u", s, m1.n_said, m2.n_said);
        const char *p = !strncmp(s, "PIECE ", 6) ? s + 6 : strstr(s, ". Piece ") ? strstr(s, ". Piece ") + 8 : NULL;
        if (p && !strncmp(k, "WHERE", 5)) {
            CHECK(box_piece >= 0 && !strncmp(p, NAMES[box_piece], strlen(NAMES[box_piece])), "%s: piece %d", s, box_piece);
            pieces++;
        } else if (p) {
            CHECK(box_piece >= 0 && !strncmp(p, NAMES[box_piece], strlen(NAMES[box_piece])), "%s: piece %d", s, box_piece);
            pieces++;
        }
        if (ann.heard + 4000u < t) {   // the box says HELLO every 2 s; here, often enough to keep it on
            n = unpack(pk, reader_hello(pk), by);
            ann_frame(by + 1, n - 2, t);
            ann_where();
            wire(&r, &m2, t);
        }
    }
    CHECK(pieces == 16, "%d texts named their piece", pieces);

    // a text over 240: announce.c cuts it at a word, and the box takes it as cut
    char lng[400];
    pad_to(lng, sizeof lng, "", 0);
    while (strlen(lng) < 300) strcat(lng, "Chord A minor, next chord 40% of the way. ");
    ann_say(lng, 0, 0);
    uint32_t len = sent_n - 7;
    CHECK(len <= 240 && len >= 230 && sent[6 + len - 1] != ' ' && lng[len] == ' ', "cut to %u", len);
    fm1scr_t b = m2;
    wire(&r, &m2, t);
    CHECK(m2.n_cut == b.n_cut + 1, "the box didn't take it as cut");

    // no HELLO for 5 s: the FM-1 stops; nothing sent, and a WHERE isn't answered
    ann_tick(ann.heard + 5001u);
    CHECK(!ann_on(), "still on after 5 s");
    n = sent_frames;
    ann_say("Ornament 35", 0, ANN_NOW);
    CHECK(sent_frames == n, "said with the reader gone");
    n = unpack(pk, reader_where(pk), by);
    ann_frame(by + 1, n - 2, t);
    CHECK(ann_where() == 0, "a WHERE answered with the reader gone");
}

// ---------------------------------------------------------------- b. anything at all

static uint32_t rng = 12345;
static uint32_t rnd(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }

static const char *const TOK[] = {
    "Aisling page", "Setup page", "Key page", "Space page", "Mix page", "Chords page", "Repeats page",
    "Sequence 2 page", "Seq2 sound page", "Lead style page", ", ", ": ", ". ", ".", ",", " ", ":", "Piece ", "Chord ",
    "Section ", "Scale ", "Scale", " moving ", ", about ", "half an octave", "hold", "held", "next chord ", "40%",
    "100", "250", "99999", "-1", "voice ", "character ", "walk ", "CPU ", "peak ", "battery about ", "on USB power",
    "not saved yet", "playing", "resting", "tempo ", "PIECE ", "MODE ", "TEMPO ", "Tempo ", "PLAYING", "RESTING",
    "SAVED", "sleep ", "evolve ", "evolve now and then", " and ", "Bb", "C#", "F", "b", "minor 7", "major",
    "Skellig", "Tir na nOg", "Echo", "Echo time", "Seq", "Seq 2", "MIDI out", "Tempo", "Root", "Save", "Saved",
    "A time", "Filter", "Off", "1/4.", "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX", "\x01", "\x7f", "\xc3\xa9",
};
#define NTOK ((int)(sizeof TOK / sizeof TOK[0]))

static void anything(void) {
    // the whole screen, said to a fresh model: what any model must come back to
    const char *wp = NULL, *wg = NULL;
    for (int i = 0; i + 1 < nl; i++)
        if (!strcmp(kind[i], "WHERE") && strncmp(text[i], "Chord ", 6)) { wp = text[i], wg = text[i + 1]; break; }
    fm1scr_t ref;
    fm1scr_init(&ref);
    fm1scr_say(&ref, wp, (uint32_t)strlen(wp), ANN_NOW, 0);
    fm1scr_say(&ref, wg, (uint32_t)strlen(wg), 0, 0);

    // each line cut at every length, from the whole screen
    long cuts = 0;
    for (int i = 0; i < nl; i++) {
        size_t len = strlen(text[i]);
        for (size_t k = 0; k <= len; k++) {
            fm1scr_t c = ref;
            fm1scr_say(&c, text[i], (uint32_t)k, (uint8_t)(rnd() & 3), 100);
            check_inv(&c, text[i]);
            cuts++;
        }
    }

    // random bytes, and soup; drawn as they go, against a full redraw now and then
    fm1scr_t s = ref;
    fm1scr_redraw(&s);
    fb_clear(fb_inc);
    nrec = 0;
    fm1scr_draw(&s, 2, 0);
    decode(fb_inc);
    char t[320];
    long soups = 0, converged = 0;
    uint32_t tm = 0;
    for (int it = 0; it < 120000; it++) {
        uint32_t n = 0;
        int kind_of = (int)(rnd() % 4);
        if (kind_of == 0) {   // bytes
            n = rnd() % 301;
            for (uint32_t j = 0; j < n; j++) t[j] = (char)(rnd() & 255);
        } else if (kind_of == 1) {   // a corpus line, spliced into another
            const char *a = text[rnd() % (uint32_t)nl], *b = text[rnd() % (uint32_t)nl];
            size_t ka = strlen(a) ? rnd() % (strlen(a) + 1) : 0;
            n = (uint32_t)snprintf(t, sizeof t, "%.*s%s", (int)ka, a, b + (strlen(b) ? rnd() % strlen(b) : 0));
        } else {   // soup
            int k = 1 + (int)(rnd() % 40);
            t[0] = 0;
            for (int j = 0; j < k; j++) {
                const char *w = TOK[rnd() % NTOK];
                if (strlen(t) + strlen(w) >= 300) break;
                strcat(t, w);
            }
            n = (uint32_t)strlen(t);
        }
        if (n >= sizeof t) n = sizeof t - 1;
        tm += rnd() % 700;
        fm1scr_tick(&s, tm);
        fm1scr_say(&s, t, n, (uint8_t)(rnd() & 3), tm);
        check_inv(&s, "soup");
        n_texts = 0;
        fm1scr_draw(&s, 2, tm);
        decode(fb_inc);
        soups++;
        if (it % 64 == 0) {
            int first;
            fresh(&s, 2, tm);
            int d = differ(fb_inc, fb_new, &first), u = unwritten(fb_new);
            CHECK(!d && !u, "soup %d: drawn bit by bit, %d pixels differ from a full redraw (first at %d,%d), %d unwritten:"
                  " \"%.*s\"", it, d, first % LCD_W, first / LCD_W, u, (int)n, t);
            if (d) memcpy(fb_inc, fb_new, sizeof fb_inc);   // (one report each)
        }
        if (it % 16 == 0) {   // the whole screen again: the same as a model told only that
            fm1scr_t c = s;
            fm1scr_say(&c, wp, (uint32_t)strlen(wp), ANN_NOW, tm);
            fm1scr_say(&c, wg, (uint32_t)strlen(wg), 0, tm);
            const char *d = model_diff(&c, &ref, true);
            CHECK(!d, "soup %d: after the whole screen, %s differs (\"%s\" vs \"%s\"): after \"%.*s\"", it, d,
                  !d ? "" : !strcmp(d, "detail") ? c.detail : !strcmp(d, "line") ? c.line : "",
                  !d ? "" : !strcmp(d, "detail") ? ref.detail : !strcmp(d, "line") ? ref.line : "", (int)n, t);
            converged++;
        }
    }
    printf("anything: %ld cut lines, %ld random texts (%ld times back to the whole screen)\n", cuts, soups, converged);
}

// ---------------------------------------------------------------- main

int main(int argc, char **argv) {
    const char *shots = NULL;
    bool quick = false;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--quick")) quick = true;
        else if (!strcmp(argv[i], "--shots") && i + 1 < argc) shots = argv[++i];
    }
    (void)ann_later;   // announce.c's, not used here
    load(argc > 1 ? argv[1] : "corpus.txt");
    printf("corpus: %d lines (%d SCREEN, %d KNOB, %d WHERE, %d CHORD, %d MSG, %d WARN)\n", nl, count_kind("SCREEN"),
           count_kind("KNOB"), count_kind("WHERE"), count_kind("CHORD"), count_kind("MSG"), count_kind("WARN"));
    CHECK(nl == 8952, "%d lines", nl);

    static const panel_t P0 = {0};
    lcd_init(&P0);
    nrec = 0;

    // d. the waiting screens: the whole area, nothing below it
    fm1scr_init(&m);
    for (int conn = 0; conn < 3; conn++) {
        fm1scr_t c;
        fm1scr_init(&c);
        fb_clear(fb_new);
        n_texts = 0;
        fm1scr_draw(&c, conn, 0);
        decode(fb_new);
        CHECK(!unwritten(fb_new), "waiting %d: %d pixels unwritten", conn, unwritten(fb_new));
        CHECK(drawn_has(conn ? "WAITING FOR THE FM-1'S SCREEN READER" : "NO FM-1"), "waiting %d: its text", conn);
        n_texts = 0;
        fm1scr_draw(&c, conn, 0);
        CHECK(nrec == 0 && !n_texts, "waiting %d: drawn again with nothing changed", conn);
        fm1scr_draw(&c, conn ? 0 : 1, 0);
        CHECK(nrec > 0, "waiting %d: the plug changed and nothing drawn", conn);
        nrec = 0;
    }
    shot(shots, "waiting", &m, 1);
    shot(shots, "nofm1", &m, 0);
    fb_clear(fb_inc);
    fm1scr_draw(&m, 1, 0);
    decode(fb_inc);

    corpus_walk(shots);
    printf("a. the corpus: %u said, %u pages, %u knobs, %u gardens, %u chords, %u messages, %u headers\n", m.n_said,
           m.n_page, m.n_knob, m.n_garden, m.n_chord, m.n_msg, m.n_header);
    beyond(shots);
    printf("a2. beyond the corpus: done\n");
    end_to_end();
    printf("c. end to end: %u frames through the reader\n", sent_frames);
    int real_q = glyph_q;   // the random bytes below draw '?' on purpose
    if (!quick) anything();

    CHECK(draw_bad == 0 && win_bad == 0 && px_bad == 0 && below == 0 && bad_cs == 0,
          "drawing: %d out of the area, %d bad windows, %d bad pixel counts, %d pixels below 272, %d CS errors",
          draw_bad, win_bad, px_bad, below, bad_cs);
    CHECK(real_q == 0, "%d glyphs of AISLING's texts drawn as '?'", real_q);
    printf("drawing: %d out of the area, %d bad windows, %d bad pixel counts, %d below 272, %d CS errors; glyphs: %d "
           "'?', %d as capitals%s\n", draw_bad, win_bad, px_bad, below, bad_cs, glyph_q, glyph_lc,
           quick ? "" : " (the random bytes draw '?' on purpose)");
    printf("%s: %d checks, %d failed\n", fails ? "FAILED" : "passed", checks, fails);
    return fails ? 1 : 0;
}
