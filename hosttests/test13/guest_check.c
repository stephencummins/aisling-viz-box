// guest.c (test 13: any firmware on the FM-1) on the Mac, under AddressSanitizer + UBSan.
//   guest_check units        who is plugged in and when that changes; the name out of a HELLO answer; what may be
//                            sent to whom; the layer for a note; held notes let go; what a guest says, its history,
//                            the wrapping; the strip; and the page: every word sent to the screen decoded, nothing at
//                            or below the status row, a full redraw covering the page, and the page drawn bit by bit
//                            the same as a fresh full redraw
//                            and the block of main.c that sends (send_piece, knob_send, perf_send), cut out of
//                            main.c by run.sh: for every one of the five and with and without a MIDI OUT, how many
//                            program changes and CCs leave
//   guest_check shots DIR    pictures (PPM)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include "lcd.h"
#include "guest.h"
#include "perf.h"
#include "knobctl.h"
#include "fm1scr.h"
#include "pflash.h"

extern uint32_t rec[];
extern int nrec, bad_cs;

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { if (++fails <= 40) { printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } } while (0)

// ------------------------------------------------ main.c's sending block, with what it needs stood in
typedef struct { const char *name; uint8_t bpm; } piece_t;
static const piece_t PIECES[8] = {{"BURREN", 84}, {"ARAN", 72}, {"SKELLIG", 60}, {"DINGLE", 100},
                                  {"BLASKET", 70}, {"SAMHAIN", 64}, {"CONNEMARA", 92}, {"TIR NA NOG", 80}};
static int n_pc, n_say;
static int to_fm1;
static bool queue_try_add(int *q, const uint8_t *p) { (void)q; (void)p; n_pc++; return true; }
static void say(const char *fmt, ...) { (void)fmt; n_say++; }
#define __dmb() ((void)0)
#define CC_N (4 + PERF_NSLOT)
static volatile uint8_t cc_val[CC_N];
static volatile uint32_t cc_gen[CC_N];
static char fm1_error[24];
static bool fm1_out, status_due, log_knob;
static fm1scr_t fm1;
static knobctl_t kc;
static perf_t pf;
static guest_t gu;
static bool may_send(void) { return guest_may_send(&gu, fm1_out); }
#include MAIN_BLOCK

static uint32_t cc_total(void) {
    uint32_t n = 0;
    for (int i = 0; i < CC_N; i++) n += cc_gen[i];
    return n;
}

// ------------------------------------------------ the screen, decoded (as perf_check.c)
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
                if (y >= GUEST_BOTTOM) below++;
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
    for (int i = 0; i < LCD_W * GUEST_BOTTOM; i++) n += fb[i] == SENTINEL;
    return n;
}
static int differ(const uint32_t *a, const uint32_t *b) {
    int n = 0;
    for (int i = 0; i < LCD_W * GUEST_BOTTOM; i++) n += a[i] != b[i];
    return n;
}
static int lit(const uint32_t *fb, int y0, int y1) {   // pixels that aren't the background
    int n = 0;
    for (int i = y0 * LCD_W; i < y1 * LCD_W; i++) n += fb[i] != SENTINEL && fb[i] != fb[0];
    return n;
}
static int margin_lit(const uint32_t *fb) {   // anything but background in the right margin's 6 columns
    int n = 0;
    for (int y = 0; y < GUEST_BOTTOM; y++)
        for (int x = LCD_W - 6; x < LCD_W; x++) n += fb[y * LCD_W + x] != fb[0] && y != 24 && y != 196;
    return n;
}
static void fresh(const guest_t *g, uint32_t *fb) {   // a copy, drawn whole
    static guest_t c;
    c = *g;
    guest_redraw(&c);
    nrec = 0;
    guest_draw(&c);
    clear(fb);
    decode(fb);
}
// The page as it stands on the screen after another draw, against a fresh whole one.
static void same_as_fresh(guest_t *g, const char *what) {
    guest_draw(g);
    decode(fb_a);
    fresh(g, fb_b);
    CHECK(unwritten(fb_b) == 0, "%s: a full redraw leaves %d pixels of the page untouched", what, unwritten(fb_b));
    CHECK(differ(fb_a, fb_b) == 0, "%s: drawn bit by bit differs from a full redraw in %d pixels", what, differ(fb_a, fb_b));
    CHECK(margin_lit(fb_a) == 0, "%s: %d pixels drawn in the right margin", what, margin_lit(fb_a));
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
            rgb[0] = rgb[1] = rgb[2] = 28;
        } else {
            int r = v >> 11 & 31, g = v >> 5 & 63, b = v & 31;
            rgb[0] = (uint8_t)(r << 3 | r >> 2), rgb[1] = (uint8_t)(g << 2 | g >> 4), rgb[2] = (uint8_t)(b << 3 | b >> 2);
        }
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

static void hello(uint8_t *b, int *n, const char *name) {   // a HELLO answer: 7D 53 52 01 ver name 00 link
    int o = 0;
    b[o++] = 0x7D, b[o++] = 0x53, b[o++] = 0x52, b[o++] = 1, b[o++] = 1;
    for (const char *p = name; *p; p++) b[o++] = (uint8_t)*p;
    b[o++] = 0;
    for (const char *p = "https://x.example/w.json"; *p; p++) b[o++] = (uint8_t)*p;
    *n = o;
}
static void sayg(guest_t *g, const char *t, uint8_t flags) { guest_say(g, t, (int)strlen(t), flags); }

// ------------------------------------------------ who it is
static void who_checks(void) {
    static guest_t g;
    guest_init(&g);
    CHECK(g.who == WHO_NONE, "starts with nothing plugged in");
    CHECK(!guest_tick(&g, 99999), "nothing plugged in never turns silent");
    CHECK(!guest_named(&g, "X0X 1.1", false) && g.who == WHO_NONE, "an answer from one already unplugged is dropped");
    guest_plugged(&g, 1000);
    CHECK(g.who == WHO_ASKING, "plugged in: asking");
    CHECK(!guest_tick(&g, 1000 + GUEST_ASK_MS - 1) && g.who == WHO_ASKING, "still asking just short of the limit");
    CHECK(guest_tick(&g, 1000 + GUEST_ASK_MS) && g.who == WHO_SILENT, "no answer in time: silent");
    CHECK(!guest_tick(&g, 1000 + 2 * GUEST_ASK_MS), "silent stays silent, and says so once");
    CHECK(guest_named(&g, "X0X 1.1", false) && g.who == WHO_GUEST && !strcmp(g.name, "X0X 1.1"), "a late answer: a guest");
    CHECK(!guest_named(&g, "X0X 1.1", false), "the same answer again changes nothing");
    guest_gone(&g);
    CHECK(g.who == WHO_NONE && !g.name[0], "unplugged: no one, no name");
    guest_plugged(&g, 5000);
    CHECK(guest_named(&g, "AISLING 1.4", true) && g.who == WHO_AISLING, "AISLING named");
    CHECK(!guest_tick(&g, 5000 + 10 * GUEST_ASK_MS) && g.who == WHO_AISLING, "one that answered never turns silent");
    CHECK(!guest_force(&g) && g.who == WHO_AISLING, "key i does nothing to one that named itself AISLING");
    guest_gone(&g);
    guest_plugged(&g, 0);   // time 0, as at boot
    CHECK(guest_named(&g, "MOSS-1", false), "a guest");
    CHECK(!guest_force(&g) && g.who == WHO_GUEST, "key i does nothing to a guest");
    guest_gone(&g);
    CHECK(!guest_force(&g) && g.who == WHO_NONE, "key i does nothing with nothing plugged in");
    guest_plugged(&g, 0);
    CHECK(!guest_force(&g) && g.who == WHO_ASKING, "key i does nothing while still asking");
    guest_tick(&g, GUEST_ASK_MS);
    CHECK(guest_force(&g) && g.who == WHO_AISLING && g.forced, "key i: a silent one taken for AISLING");
    CHECK(guest_force(&g) && g.who == WHO_SILENT && !g.forced, "key i again: put back");
    guest_force(&g);
    guest_gone(&g);
    guest_plugged(&g, 0);
    CHECK(g.who == WHO_ASKING && !g.forced, "the next one plugged in isn't taken for AISLING");

    // the name out of a HELLO answer
    uint8_t b[80];
    int n;
    char name[24];
    hello(b, &n, "X0X 1.1");
    CHECK(guest_hello_name(b, n, name, sizeof name) && !strcmp(name, "X0X 1.1"), "name read: '%s'", name);
    hello(b, &n, "AMB-1 EVOLVED WITH A VERY LONG NAME INDEED");
    CHECK(guest_hello_name(b, n, name, sizeof name) && strlen(name) == 23, "a long name cut to fit: %zu", strlen(name));
    hello(b, &n, "A\x01" "B\x7F" "C");
    CHECK(guest_hello_name(b, n, name, sizeof name) && !strcmp(name, "A B C"), "unprintable bytes as spaces: '%s'", name);
    hello(b, &n, "NOEND");
    CHECK(guest_hello_name(b, 5 + 3, name, sizeof name) && !strcmp(name, "NOE"), "a name with no end stops at the length: '%s'", name);
    CHECK(guest_hello_name(b, 5, name, sizeof name) && !name[0], "an answer with no name: empty");
    b[3] = 16;
    CHECK(!guest_hello_name(b, n, name, sizeof name) && !name[0], "a SAY isn't a HELLO answer");
    b[3] = 1, b[0] = 0x7E;
    CHECK(!guest_hello_name(b, n, name, sizeof name), "nor is someone else's SysEx");
    b[0] = 0x7D;
    CHECK(!guest_hello_name(b, 4, name, sizeof name), "nor one too short");
    // and it agrees with perf_fw on what AISLING is
    hello(b, &n, "AISLING 1.4");
    CHECK(perf_fw(b, n) == 1, "perf_fw: AISLING 1.4");
    hello(b, &n, "AISLINGER 2.0");
    CHECK(perf_fw(b, n) < 0, "perf_fw: a name that only starts like AISLING is a guest");
}

// ------------------------------------------------ what may be sent, the layers, the held notes
static void send_checks(void) {
    static guest_t g;
    for (int who = WHO_NONE; who <= WHO_GUEST; who++) {
        for (int out = 0; out < 2; out++) {
            guest_init(&g);
            g.who = (uint8_t)who;
            bool want = who == WHO_AISLING && out;
            CHECK(guest_may_send(&g, out) == want, "may_send who %d out %d", who, out);
            // main.c's own senders
            gu = g, fm1_out = out;
            n_pc = 0;
            memset((void *)cc_gen, 0, sizeof cc_gen);
            for (int i = 0; i < 8; i++) send_piece(i);
            knob_send(0xF);
            pf.due = (1u << PERF_NSLOT) - 1;
            perf_send();
            CHECK(pf.due == 0, "perf_send leaves nothing due (who %d out %d)", who, out);
            CHECK(n_pc == (want ? 8 : 0), "who %d out %d: %d program changes left", who, out, n_pc);
            CHECK(cc_total() == (want ? (uint32_t)CC_N : 0u), "who %d out %d: %u CCs left", who, out, (unsigned)cc_total());
        }
        CHECK(guest_page(&g) == (who == WHO_GUEST || who == WHO_SILENT), "guest_page who %d", who);
    }
    // layers: AISLING's by channel
    guest_init(&g);
    g.who = WHO_AISLING;
    for (int ch = 0; ch < 16; ch++) CHECK(guest_layer(&g, (uint8_t)ch, 60) == (ch & 3), "AISLING channel %d", ch);
    // anything else's by pitch, whatever the channel, and the same for a note's on and its off
    for (int who = WHO_NONE; who <= WHO_GUEST; who++) {
        if (who == WHO_AISLING) continue;
        g.who = (uint8_t)who;
        int seen[4] = {0};
        for (int note = 0; note < 128; note++) {
            int l = guest_layer(&g, 0, (uint8_t)note);
            CHECK(l >= 0 && l < 4, "layer in range");
            for (int ch = 1; ch < 16; ch++) CHECK(guest_layer(&g, (uint8_t)ch, (uint8_t)note) == l, "channel doesn't matter");
            seen[l]++;
        }
        CHECK(seen[0] == 48 && seen[1] == 12 && seen[3] == 12 && seen[2] == 56, "pitch bands %d %d %d %d", seen[0], seen[1], seen[3], seen[2]);
        CHECK(guest_layer(&g, 9, 36) == 0 && guest_layer(&g, 0, 59) == 1 && guest_layer(&g, 0, 60) == 3 &&
              guest_layer(&g, 0, 71) == 3 && guest_layer(&g, 0, 72) == 2, "the bands' edges");
    }
    // held notes
    guest_init(&g);
    g.who = WHO_GUEST;
    CHECK(guest_expire(&g, 1u << 30) < 0, "nothing held, nothing let go");
    guest_note(&g, 40, 100, 0);          // at time 0 exactly: still remembered
    guest_note(&g, 80, 100, 500);
    guest_note(&g, 81, 100, 600);
    guest_note(&g, 81, 0, 700);          // let go by the firmware itself
    CHECK(guest_expire(&g, GUEST_HOLD_MS - 5) < 0, "not yet");
    CHECK(guest_expire(&g, GUEST_HOLD_MS + 2) == 40, "the first, after the hold");
    CHECK(guest_expire(&g, GUEST_HOLD_MS + 2) < 0, "once only, and the others not yet");
    guest_note(&g, 80, 90, 4000);        // struck again: the hold starts again
    CHECK(guest_expire(&g, 500 + GUEST_HOLD_MS) < 0, "a note struck again holds from then");
    CHECK(guest_expire(&g, 4000 + GUEST_HOLD_MS) == 80, "and is let go after that");
    CHECK(guest_expire(&g, 1u << 30) < 0, "the one the firmware let go is never let go again");
    guest_note(&g, 5, 1, 0xFFFFFF00u);   // the clock wrapping
    CHECK(guest_expire(&g, 0xFFFFFF00u + GUEST_HOLD_MS - 1) < 0 && guest_expire(&g, 0xFFFFFF00u + GUEST_HOLD_MS) == 5, "across the clock's wrap");
    guest_note(&g, 6, 1, 10), guest_note(&g, 7, 1, 10);
    guest_notes_clear(&g);
    CHECK(guest_expire(&g, 1u << 30) < 0, "cleared");
    guest_note(&g, 200, 1, 10);          // a byte with its top bit set is masked, not written past the table
    CHECK(guest_expire(&g, 10 + GUEST_HOLD_MS) == (200 & 127), "note masked to 7 bits");
}

// ------------------------------------------------ what it says
static void say_checks(void) {
    static guest_t g;
    static char rows[12][GUEST_HIST_W + 4];
    guest_init(&g);
    guest_plugged(&g, 0);
    guest_named(&g, "X0X 1.1", false);
    sayg(&g, "", 0);
    sayg(&g, "   \x01 ", 0);
    CHECK(!g.now[0] && g.n_said == 0, "nothing said by an empty SAY");
    sayg(&g, "  BD   TUNE\t37 ", 0);
    CHECK(!strcmp(g.now, "BD TUNE 37") && g.n_hist == 0, "cleaned: '%s'", g.now);
    sayg(&g, "BD TUNE 37", 0);
    CHECK(g.n_hist == 0 && g.n_said == 2, "the same again doesn't push itself into the history");
    sayg(&g, "BD TUNE 38", 2);
    CHECK(!strcmp(g.now, "BD TUNE 38") && g.now_warn && g.n_hist == 1 && !strcmp(g.hist[0], "BD TUNE 37"), "the one before, kept");
    sayg(&g, "BD TUNE 39", 0);
    CHECK(!g.now_warn, "a warning ends with the next thing said");
    char t[40];
    for (int i = 0; i < 20; i++) snprintf(t, sizeof t, "STEP %d", i), sayg(&g, t, 0);
    CHECK(g.n_hist == GUEST_HIST, "history full at %d", g.n_hist);
    for (int i = 0; i < GUEST_HIST; i++) {
        snprintf(t, sizeof t, "STEP %d", 18 - i);
        CHECK(!strcmp(g.hist[i], t), "history %d is '%s', wanted '%s'", i, g.hist[i], t);
    }
    // the longest SAY, and longer (cut at 240), then into the history cut and marked
    static char big[400];
    for (int i = 0; i < 399; i++) big[i] = i % 7 == 6 ? ' ' : (char)('A' + i % 26);
    guest_say(&g, big, 399, 0);
    CHECK(strlen(g.now) <= GUEST_TEXT, "the newest cut to %zu", strlen(g.now));
    guest_say(&g, big, 5, 0);   // n counts, not the text's end
    CHECK(!strcmp(g.now, "ABCDE"), "a SAY's length is n: '%s'", g.now);
    CHECK(strlen(g.hist[0]) == GUEST_HIST_W && !strcmp(g.hist[0] + GUEST_HIST_W - 2, ".."), "a long one in the history: cut and marked");
    guest_gone(&g);
    CHECK(!g.now[0] && g.n_hist == 0, "unplugged: what it said is forgotten");

    // wrapping
    int n = guest_wrap("ONE TWO THREE", 7, 12, rows);
    CHECK(n == 2 && !strcmp(rows[0], "ONE TWO") && !strcmp(rows[1], "THREE"), "wrap at a space: %d '%s' '%s'", n, rows[0], rows[1]);
    n = guest_wrap("ONE TWO THREE", 6, 12, rows);
    CHECK(n == 3 && !strcmp(rows[0], "ONE") && !strcmp(rows[1], "TWO") && !strcmp(rows[2], "THREE"), "no row over the width");
    n = guest_wrap("ABCDEFGHIJ KL", 4, 12, rows);
    CHECK(n == 4 && !strcmp(rows[0], "ABCD") && !strcmp(rows[1], "EFGH") && !strcmp(rows[2], "IJ") && !strcmp(rows[3], "KL"), "a long word cut: %d", n);
    n = guest_wrap("A B C D E F", 1, 3, rows);
    CHECK(n == 3, "rows limited: %d", n);
    CHECK(guest_wrap("", 10, 12, rows) == 0 && guest_wrap("    ", 10, 12, rows) == 0, "nothing wraps to nothing");
    // every length of the worst text at both sizes: no row too long, nothing lost while there's room
    for (int len = 1; len <= GUEST_TEXT; len++) {
        char s[GUEST_TEXT + 1];
        for (int i = 0; i < len; i++) s[i] = i % 9 == 8 ? ' ' : (char)('a' + i % 26);
        s[len] = 0;
        if (s[len - 1] == ' ') s[len - 1] = 'z';
        for (int k = 0; k < 2; k++) {
            int w = k ? 39 : 26, max = k ? 9 : 7;
            n = guest_wrap(s, w, max, rows);
            int chars = 0;
            for (int i = 0; i < n; i++) {
                CHECK((int)strlen(rows[i]) <= w && rows[i][0] && rows[i][0] != ' ', "row %d of length %d at width %d", i, len, w);
                for (const char *p = rows[i]; *p; p++) chars += *p != ' ';
            }
            int want = 0;
            for (const char *p = s; *p; p++) want += *p != ' ';
            if (k) CHECK(chars == want, "at the small size the longest SAY fits whole: %d of %d (length %d)", chars, want, len);
        }
    }
    // the strip
    char a[80], b2[80];
    guest_init(&g);
    CHECK(!guest_strip(&g, a, b2), "no FM-1: the pieces");
    guest_plugged(&g, 0);
    CHECK(!guest_strip(&g, a, b2), "asking: the pieces");
    guest_named(&g, "AISLING 1.4", true);
    CHECK(!guest_strip(&g, a, b2), "AISLING: the pieces");
    guest_gone(&g), guest_plugged(&g, 0), guest_tick(&g, GUEST_ASK_MS);
    CHECK(guest_strip(&g, a, b2) && strstr(a, "NOT AISLING") && strlen(a) <= 78 && strlen(b2) <= 78, "silent: '%s'", a);
    guest_named(&g, "AMB-1 EVOLVED WITH A VERY LONG NAME INDEED", false);
    CHECK(guest_strip(&g, a, b2) && !strncmp(a, "AMB-1 EVOLVED", 13) && strlen(a) <= 78 && strlen(b2) <= 78, "guest: '%s' (%zu)", a, strlen(a));
}

// ------------------------------------------------ the page
static void page_checks(const char *dir) {
    static guest_t g;
    guest_init(&g);
    nrec = 0;
    clear(fb_a);
    below = win_bad = px_bad = 0;
    guest_plugged(&g, 0);
    guest_tick(&g, GUEST_ASK_MS);
    same_as_fresh(&g, "silent");
    CHECK(lit(fb_a, 0, 24) > 200 && lit(fb_a, 30, 194) > 500, "silent: a heading and a line of what it is");
    if (dir) ppm(dir, "guest-silent", fb_a);
    guest_named(&g, "X0X 1.1", false);
    same_as_fresh(&g, "named, nothing said");
    if (dir) ppm(dir, "guest-listening", fb_a);
    int head = lit(fb_a, 0, 24);
    sayg(&g, "BD TUNE 37", 0);
    same_as_fresh(&g, "one thing said");
    CHECK(lit(fb_a, 0, 24) == head, "the heading untouched by a SAY");
    CHECK(lit(fb_a, 202, 272) == 0, "no history yet");
    if (dir) ppm(dir, "guest-one", fb_a);
    nrec = 0;
    guest_draw(&g);
    CHECK(nrec == 0, "nothing new, nothing drawn (%d words)", nrec);
    sayg(&g, "PATTERN 3, KICK ON STEPS 1 5 9 13, SNARE ON 5 13, SWING 54 PERCENT", 0);
    same_as_fresh(&g, "a second, longer");
    CHECK(lit(fb_a, 202, 272) > 100, "the first is in the history");
    sayg(&g, "WARNING: MEMORY FULL", 2);
    same_as_fresh(&g, "a warning");
    if (dir) ppm(dir, "guest-warning", fb_a);
    // a whole screen (a WHERE's answer): the small size; then a short one after it: the large again, nothing left over
    static char big[GUEST_TEXT + 1];
    snprintf(big, sizeof big, "X0X 1.1. PATTERN 3 OF 16. TEMPO 124. SWING 54. KICK: TUNE 37, DECAY 60, LEVEL 100. SNARE: TUNE 52, "
             "SNAP 70, LEVEL 90. HAT: DECAY 20, LEVEL 80. CLAP: LEVEL 64. ACCENT 30. FILL OFF. CHAIN OFF. STEP 9 OF 16. "
             "PLAYING. MIDI CLOCK INTERNAL.");
    sayg(&g, big, 0);
    same_as_fresh(&g, "a whole screen");
    CHECK(g.drawn_scale == 2, "a whole screen at the small size");
    if (dir) ppm(dir, "guest-whole", fb_a);
    sayg(&g, "PLAY", 0);
    same_as_fresh(&g, "a short one after a whole screen");
    CHECK(g.drawn_scale == 3, "back to the large size");
    char t[60];
    for (int i = 0; i < 40; i++) {
        snprintf(t, sizeof t, i % 5 ? "STEP %d" : "STEP %d OF A PATTERN THAT RUNS ON AND ON FOR SOME TIME YET", i);
        sayg(&g, t, i % 7 == 3 ? 2 : 0);
        same_as_fresh(&g, "a run of things said");
    }
    if (dir) ppm(dir, "guest-run", fb_a);
    // every length of SAY, of the widest letter
    for (int len = 1; len <= GUEST_TEXT; len += 3) {
        for (int i = 0; i < len; i++) big[i] = i % 11 == 10 ? ' ' : 'W';
        big[len] = 0;
        sayg(&g, big, 0);
        same_as_fresh(&g, "every length");
    }
    // one word with no space in it, at each size: cut at the row's end, nothing in the margin
    // (the screen's text drops a letter that would cross its edge, so a row too wide loses letters: count them)
    sayg(&g, "W", 0);
    same_as_fresh(&g, "one letter");
    int one = lit(fb_a, 30, 194);
    memset(big, 'W', GUEST_TEXT), big[100] = 0;
    sayg(&g, big, 0);
    same_as_fresh(&g, "a word of 100 letters");
    CHECK(one > 0 && lit(fb_a, 30, 194) == 100 * one, "a word of 100 letters: %d lit pixels, wanted 100 x %d", lit(fb_a, 30, 194), one);
    CHECK(g.drawn_scale == 3, "a word of 100 letters at the large size");
    memset(big, 'W', GUEST_TEXT), big[GUEST_TEXT] = 0;
    sayg(&g, big, 2);
    same_as_fresh(&g, "a word of 240 letters");
    guest_named(&g, "MOSS-1", false);   // a new name on the same socket (it can't happen, but the heading must follow)
    same_as_fresh(&g, "renamed");
    guest_gone(&g);
    guest_plugged(&g, 0);
    guest_tick(&g, GUEST_ASK_MS);
    same_as_fresh(&g, "silent again, after a guest");
    CHECK(lit(fb_a, 202, 272) == 0, "the last one's history gone");
    CHECK(below == 0, "%d pixels drawn at or below the status row", below);
    CHECK(win_bad == 0 && px_bad == 0 && bad_cs == 0, "the screen's windows: %d bad, %d miscounted, %d unselected", win_bad, px_bad, bad_cs);
}

int main(int argc, char **argv) {
    static const panel_t P0 = {0};
    lcd_init(&P0);
    nrec = 0;
    const char *dir = argc >= 3 && !strcmp(argv[1], "shots") ? argv[2] : NULL;
    who_checks();
    send_checks();
    say_checks();
    page_checks(dir);
    printf("units: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
