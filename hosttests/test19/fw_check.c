// Test 19's check of the firmware page: fwpage.c as it is, over the box's own FatFs, card.c and sd.c and test 12's
// model of a microSD card, holding an image a Mac filled (run.sh attaches it and copies the files in, so the names
// on it are the ones macOS makes: AISLIN~1.FWS); the FM-1 at the other end is simdev.h's.
//
//   fw_check make DIR     the files for the card's folder FM1, into DIR
//   fw_check run IMG      the checks, with IMG as the card
//   fw_check bare IMG     a card with no folder FM1
//
// fwpage.c is built with its lcd_fill_rect and lcd_text sent here (rec_fill, rec_text): what is on the page is
// known as the texts drawn and not since painted over, and every rectangle's place is looked at.
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sdk_stub.h"
#include "tusb.h"
#include "ff.h"
#include "lcd.h"
#include "sd.h"
#include "card.h"
#include "swap.h"
#include "fwpage.h"
#include "ui.h"
#include "model.h"
#include "simdev.h"

static int checks, failed;
#define CHECK(c, ...) do { checks++; if (!(c)) { if (++failed <= 60) { printf("FAIL %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } } while (0)

bool tud_msc_set_sense(uint8_t lun, uint8_t key, uint8_t asc, uint8_t ascq) { return true; }

// ---- the files
static const struct { const char *name, *product; uint32_t size, mark_at, seed; } PK[] = {
    {"aisling-1.4.1.fwsc", "FM-1_353010401", 150000, 70000, 1},
    {"aislingfx-1.1.fwsc", "FM-1_391010100", 120000, 1912, 2},   // the mark across two of the page's reads
    {"stock.fwsc", "FM-1_900", 100000, 0, 3},                    // no update loader of Felucca's in it
    {"other.fwsc", "OP-9_100", 100000, 5000, 4},                 // another instrument's
};
enum { NPK = 4, PKMAX = 150000 };
static uint8_t raw[NPK][PKMAX], lgc[NPK][PKMAX];
static uint32_t lgn[NPK];
static void packages(void) {
    for (int i = 0; i < NPK; i++) {
        make_pkg(raw[i], PK[i].size, PK[i].product, PK[i].mark_at, PK[i].seed);
        lgn[i] = make_logical(raw[i], PK[i].size, lgc[i]);
    }
}
static void put_file(const char *dir, const char *name, const void *p, uint32_t n) {
    char path[600];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f || fwrite(p, 1, n, f) != n || fclose(f)) { perror(path); exit(2); }
}

// ---- the screen, as fwpage.c draws on it
typedef struct { int x, y, w, h; char s[100]; uint16_t fg, bg; bool live; } text_t;
static text_t texts[400];
static int ntexts, draws, bad_place;
static char bad_what[160];
static bool hit(const text_t *t, int x, int y, int w, int h) { return t->x < x + w && x < t->x + t->w && t->y < y + h && y < t->y + t->h; }
static void paint_over(int x, int y, int w, int h) {
    int k = 0;
    for (int i = 0; i < ntexts; i++) if (!hit(&texts[i], x, y, w, h)) texts[k++] = texts[i];
    ntexts = k;
}
static void place(int x, int y, int w, int h, bool ground, const char *what) {
    // on the screen; and anything that isn't the page's ground left of the brick (ui.h)
    if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > LCD_W || y + h > LCD_H || (!ground && x + w > UI_W)) {
        if (!bad_place++) snprintf(bad_what, sizeof bad_what, "%s at %d,%d %dx%d", what, x, y, w, h);
    }
}
void rec_fill(int x, int y, int w, int h, uint16_t colour) {
    draws++;
    place(x, y, w, h, colour == C_BLACK, "a rectangle");
    paint_over(x, y, w, h);
}
void rec_text(int x, int y, const char *s, int scale, uint16_t fg, uint16_t bg) {
    int w = (int)strlen(s) * 6 * scale, h = 8 * scale;
    draws++;
    place(x, y, w ? w : 1, h, false, s);   // its blanks too: a line is cut to the usable width, not to the screen's
    paint_over(x, y, w, h);
    if (ntexts == 400) { printf("FAIL: too many texts\n"); exit(2); }
    text_t *t = &texts[ntexts++];
    *t = (text_t){.x = x, .y = y, .w = w, .h = h, .fg = fg, .bg = bg, .live = true};
    snprintf(t->s, sizeof t->s, "%s", s);
}
static const text_t *seen(const char *part) {
    for (int i = 0; i < ntexts; i++) if (strstr(texts[i].s, part)) return &texts[i];
    return NULL;
}
static bool any_row(void) {   // anything written where the list is
    for (int i = 0; i < ntexts; i++) if (texts[i].y >= 46 && texts[i].y < 196 && strspn(texts[i].s, " ") != strlen(texts[i].s)) return true;
    return false;
}
static const text_t *button(const char *name) {   // a button's lettering is the name and nothing else
    for (int i = 0; i < ntexts; i++) if (!strcmp(texts[i].s, name) && texts[i].y >= 264) return &texts[i];
    return NULL;
}

// ---- the FM-1 and the USB between
static fm1_t dev;
static uint32_t now_ms = 5000;
static struct { uint8_t b[80]; uint32_t n; } fifo[64];
static int fh, ft;
static bool cable_out, link_told;
static int offers, sends, busy_every, upgrades_sent;
static void to_host(const uint8_t *p, uint32_t n) {
    if (cable_out) return;
    memcpy(fifo[ft].b, p, n);
    fifo[ft].n = n;
    ft = (ft + 1) % 64;
}
bool fw_send(const uint8_t *w, uint32_t n) {
    static const uint8_t UP[] = {0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7};
    offers++;
    if (busy_every && offers % busy_every) return false;
    sends++;
    if (n == sizeof UP && !memcmp(w, UP, n)) upgrades_sent++;
    if (!cable_out) dev_rx(&dev, w, n, now_ms);
    return true;
}
static char lines[40][120];
static int nlines;
static void pass(void) {
    bool on = dev_on_bus(&dev) && !cable_out;
    if (on != link_told) { link_told = on; if (!on) fh = ft; fw_link(on, now_ms); }
    while (fh != ft) {
        int i = fh;
        fh = (fh + 1) % 64;
        if (link_told) fw_rx(fifo[i].b, fifo[i].n, now_ms);
    }
    dev_tick(&dev, now_ms);
    fw_tick(now_ms);
    const char *l = fw_news();
    if (l && nlines < 40) snprintf(lines[nlines++], sizeof lines[0], "%s", l);
    now_ms++;
}
static void wait_ms(uint32_t ms) { while (ms--) pass(); }
static bool finish(uint32_t most_ms) {   // until the install is over
    while (fw_busy() && most_ms--) pass();
    wait_ms(50);
    return !fw_busy();
}
static bool logged(const char *part) {
    for (int i = 0; i < nlines; i++) if (strstr(lines[i], part)) return true;
    return false;
}

// ---- fingers
static int tap(int x, int y) {
    fw_touch(x, y);
    return fw_lift(now_ms);
}
static bool tap_text(const text_t *t) {
    if (!t) return false;
    tap(t->x + 3, t->y + t->h / 2);
    return true;
}
// The packages' names as the page showed them, without ".FWS" (the list's check fills them in).
static char nm[6][13];
#define N_A nm[0]
#define N_FX nm[1]
#define N_STOCK nm[2]
#define N_OTHER nm[3]
#define N_JUNK nm[4]
static const char *fmt(const char *f, const char *a) {
    static char t[2][120];
    static int k;
    k ^= 1;
    snprintf(t[k], sizeof t[k], f, a);
    return t[k];
}
static bool tap_row(const char *part) { return tap_text(seen(part)); }
static bool tap_button(const char *name) { return tap_text(button(name)); }

static card_cfg_t hc(uint32_t sectors) {
    return (card_cfg_t){.present = true, .ver = 2, .hc = true, .csd = 1, .bl = 9, .sectors = sectors,
                        .init_ms = 40, .busy_us = 1500, .name = "AISLG"};
}
static uint32_t img_sectors;
static void board(const char *img) {
    panel_t p;
    memset(&p, 0, sizeof p);
    lcd_init(&p);
    m_no_pullup_is_error(false);
    if (img) {
        img_sectors = m_load(img);
        m_insert(hc(img_sectors));
    } else {
        m_insert((card_cfg_t){0});
    }
}
static void plug(int installed, int after) {   // an FM-1 with this package's firmware on it, to be given that one
    dev_init(&dev, lgc[after], lgn[after], PK[installed].product, PK[after].product, to_host);
    fh = ft = 0;
}
static void open_page(void) {
    ntexts = 0;
    fw_open(link_told, now_ms);
    fw_draw();
}

// The way to an install: the row, INSTALL, SURE?
static bool pick_and_go(const char *row) {
    if (!seen(row) && !tap_button("MORE")) return false;
    return tap_row(row) && tap_button("INSTALL") && tap_button("SURE?");
}

static void mode_run(const char *img) {
    packages();
    // -- no card
    board(NULL);
    CHECK(card_start() == CARD_NONE, "no card: %s", card_state_name(card_state()));
    plug(1, 0);
    wait_ms(10);
    open_page();
    CHECK(seen("NO CARD") && seen("AN FM-1 IS PLUGGED IN") && button("INSTALL") && button("BACK") && !any_row(), "no card: the page");
    tap_button("INSTALL");
    CHECK(!button("SURE?") && !fw_busy(), "no card: INSTALL does something");
    CHECK(tap_button("BACK") && 1, "BACK");
    fw_touch(button("BACK")->x + 3, 290);
    CHECK(fw_lift(now_ms) == FW_DO_BACK, "BACK doesn't say so");
    fw_close();

    // -- the card
    board(img);
    CHECK(card_start() == CARD_OK, "the card: %s (%s)", card_state_name(card_state()), sd_error());
    open_page();
    CHECK(seen("PICK ONE: 1 TO 5 OF 6"), "the head: %s", ntexts > 1 ? texts[1].s : "");
    // all six, over the two pages, each once. The names are the short ones macOS gave, and which it gives is its
    // own affair (stock.fwsc is STOC~1 on one macOS and STOC~5 on another): so a row is known by what follows its name,
    // its name has to be a short name of that file's, and the rest of the check uses the name the page showed.
    {
        static const char *const want[] = {" FM-1_353010401     146K", " FM-1_391010100     117K", " FM-1_900            98K",
                                           " OP-9_100            98K", " NOT A PACKAGE        5K", " NOT A PACKAGE        0K"};
        static const char *const stem[] = {"AISLIN~", "AISLIN~", "STOC~", "OTHE~", "JUN~", "TIN~"};
        int found[6] = {0}, rows = 0;
        for (int page = 0; page < 2; page++) {
            for (int i = 0; i < ntexts; i++) {
                if (!strstr(texts[i].s, ".FWS")) continue;
                rows++;
                int k = 0;
                while (k < 6 && !(strlen(texts[i].s) == 36 && !strcmp(texts[i].s + 12, want[k]) && !strncmp(texts[i].s, stem[k], strlen(stem[k])))) k++;
                const char *dot = strstr(texts[i].s, ".FWS");
                if (k < 6 && dot && dot - texts[i].s <= 8 && (dot[4] == ' ' || dot - texts[i].s == 8)) {
                    found[k]++;
                    snprintf(nm[k], sizeof nm[k], "%.*s", (int)(dot - texts[i].s), texts[i].s);
                } else CHECK(0, "a row [%s]", texts[i].s);
            }
            if (!page) CHECK(tap_button("MORE") && seen("PICK ONE: 6 TO 6 OF 6"), "MORE");
        }
        CHECK(rows == 6 && found[0] == 1 && found[1] == 1 && found[2] == 1 && found[3] == 1 && found[4] == 1 && found[5] == 1,
              "the six rows: %d (%d %d %d %d %d %d)", rows, found[0], found[1], found[2], found[3], found[4], found[5]);
        CHECK(strcmp(nm[0], nm[1]) && nm[0][0] && nm[1][0] && nm[2][0] && nm[3][0] && nm[4][0], "the names: %s %s %s %s %s", nm[0], nm[1], nm[2], nm[3], nm[4]);
        CHECK(!seen("NOTES") && !seen("OLD~") && !seen("OLD.") && !seen("_AISLI") && !seen("._"), "something listed that isn't a package's name");
        CHECK(tap_button("MORE") && seen("PICK ONE: 1 TO 5 OF 6"), "MORE, back to the first");
    }
    // a file that isn't a package: picked, said, not installable
    if (!seen(N_JUNK)) tap_button("MORE");
    CHECK(tap_row(N_JUNK) && seen("THAT FILE DOESN'T SAY WHAT IT IS FOR"), "junk picked");
    tap_button("INSTALL");
    CHECK(!button("SURE?") && !fw_busy() && upgrades_sent == 0, "junk: INSTALL does something");
    if (!seen(N_A)) tap_button("MORE");
    // SURE? is taken back by another row, and by MORE
    CHECK(tap_row(N_A) && seen(N_A)->bg == C_WHITE && tap_button("INSTALL") && button("SURE?") &&
          seen("FM-1_353010401 REPLACES WHAT IS ON THE"), "INSTALL asks");
    CHECK(tap_row(N_FX) && !button("SURE?") && button("INSTALL") && seen(N_FX)->bg == C_WHITE &&
          seen(N_A)->bg != C_WHITE, "another row takes SURE? back");
    CHECK(tap_button("INSTALL") && button("SURE?") && tap_button("MORE") && !button("SURE?") && !fw_busy(), "MORE takes SURE? back");
    tap_button("MORE");
    // a finger that slides off INSTALL does nothing
    tap_row(N_A);
    fw_touch(button("INSTALL")->x, 290), fw_touch(200, 120);
    fw_lift(now_ms);
    CHECK(!button("SURE?"), "a finger slid off INSTALL");
    CHECK(sends == 0, "something was sent before any install (%d)", sends);

    // -- the install: aisling over aislingfx
    CHECK(pick_and_go(N_A) && fw_busy(), "the install didn't begin");
    CHECK(seen("ASKING WHO IT IS") && seen(fmt("FM-1_353010401 FROM %s.FWS", N_A)) && seen("LEAVE THE FM-1 ON") && !seen(N_FX),
          "the page of an install");
    {   // nothing is touchable while it runs, and the heads come in turn
        bool check = false, loader = false, writing = false, restart = false, pct50 = false;
        int d0;
        tap(button("BACK")->x + 3, 290), tap(100, 60);
        CHECK(fw_busy() && tap(button("BACK")->x + 3, 290) == FW_DO_NOTHING, "BACK during an install");
        for (uint32_t i = 0; i < 200000 && fw_busy(); i++) {
            pass();
            check |= !!seen("THE FM-1 CHECKS IT"), loader |= !!seen("TO ITS LOADER"), restart |= !!seen("RESTARTING");
            writing |= !!seen("WRITING "), pct50 |= !!seen("WRITING 50%");
            if (i == 3000) {   // the page left for another: it goes on, and draws nothing
                fw_close();
                d0 = draws;
            }
            if (i == 4000) {
                CHECK(draws == d0 && fw_busy(), "drawn while another page is up");
                open_page();
            }
        }
        wait_ms(50);
        CHECK(check && loader && writing && pct50 && restart, "the heads: %d %d %d %d %d", check, loader, writing, pct50, restart);
    }
    CHECK(!fw_busy() && seen("FM-1_353010401 IS ON THE FM-1") && dev.wrote_all && dev.bad_answers == 0 && dev.upgrades == 2 &&
          dev.retries == 0 && upgrades_sent == 2, "the install: [%s] wrote all %d, bad answers %d (%s), retries %d, upgrades %d %d",
          nlines ? lines[nlines - 1] : "", dev.wrote_all, dev.bad_answers, dev.bad, dev.retries, dev.upgrades, upgrades_sent);
    CHECK(logged(fmt("firmware: putting FM-1_353010401 (%s.FWS, 150000 bytes) on the FM-1", N_A)) &&
          logged("firmware: FM-1_353010401 IS ON THE FM-1 (") && logged(", it was FM-1_391010100)"), "the log's lines");
    CHECK(seen("PICK ONE: 1 TO 5 OF 6") && seen(N_A) && button("INSTALL"), "the list is back");

    // -- and back, the USB busy two times in three
    plug(0, 1);
    wait_ms(10);
    busy_every = 3;
    CHECK(pick_and_go(N_FX) && finish(200000) && seen("FM-1_391010100 IS ON THE FM-1") && dev.wrote_all &&
          dev.bad_answers == 0 && dev.retries == 0, "the way back: wrote all %d, bad %d (%s), retries %d", dev.wrote_all,
          dev.bad_answers, dev.bad, dev.retries);
    busy_every = 0;

    // -- a package with no loader of Felucca's: never begun
    plug(1, 2);
    wait_ms(10);
    {
        int s0 = sends;
        CHECK(pick_and_go(N_STOCK) && !fw_busy() && seen("IT HAS NO FELUCCA UPDATE LOADER IN IT") && sends == s0 &&
              dev.upgrades == 0 && button("INSTALL"), "the stock package");
    }
    // -- another instrument's: asked who it is, and no further
    plug(1, 3);
    wait_ms(10);
    CHECK(pick_and_go(N_OTHER) && finish(20000) && seen("THIS PACKAGE IS FOR ANOTHER") && seen("INSTRUMENT. NOTHING WAS WRITTEN") && dev.upgrades == 0 &&
          dev.answers == 0, "another instrument's package: upgrades %d", dev.upgrades);
    // -- no FM-1
    cable_out = true;
    wait_ms(10);
    CHECK(seen("NO FM-1 TO SEND TO"), "the link's line");
    CHECK(pick_and_go(N_A) && finish(60000) && seen("NOTHING ANSWERED. NOTHING WAS WRITTEN"), "no FM-1");
    cable_out = false;
    wait_ms(300);
    CHECK(seen("AN FM-1 IS PLUGGED IN"), "the link's line, back");

    // -- the card pulled out in the middle of the write: said; the card back, INSTALL again finishes from the loader
    plug(1, 0);
    wait_ms(10);
    CHECK(pick_and_go(N_A), "the install didn't begin");
    for (uint32_t i = 0; i < 100000 && !seen("WRITING 30%"); i++) pass();
    CHECK(seen("WRITING 30%") != NULL, "it never wrote");
    m_insert((card_cfg_t){0});
    CHECK(finish(400000) && seen("THE CARD COULDN'T BE READ.") && !dev.wrote_all, "the card pulled: [%s]",
          nlines ? lines[nlines - 1] : "");
    fw_close();
    m_insert(hc(img_sectors));
    CHECK(card_start() == CARD_OK, "the card back: %s", card_state_name(card_state()));
    open_page();
    CHECK(dev.mode == D_LOADER || dev.mode == D_WRITE, "the FM-1 isn't in its loader (%d)", dev.mode);
    dev_restart(&dev);
    wait_ms(2000);
    CHECK(pick_and_go(N_A) && finish(300000) && seen("FM-1_353010401 IS ON THE FM-1 (IT WAS") && seen("IN ITS LOADER)") && dev.wrote_all &&
          dev.bad_answers == 0, "INSTALL again: [%s] bad %d (%s)", nlines ? lines[nlines - 1] : "", dev.bad_answers, dev.bad);

    // -- the card at the Mac, the folder gone
    fw_close();
    CHECK(card_lend(), "the card wasn't lent");
    open_page();
    CHECK(seen("THE CARD IS AT THE MAC") && !any_row(), "lent: the page");
    fw_close();
    CHECK(card_take_back() == CARD_OK, "the card back from the Mac");

    CHECK(bad_place == 0, "%d things drawn where they shouldn't be, the first %s", bad_place, bad_what);
    CHECK(m_errors == 0, "the card's model: %s", m_err);
    // the door on the card's page
    ntexts = 0;
    fw_door_draw(false), fw_door_draw(true);
    CHECK(seen("FIRMWARE FOR THE FM-1") && bad_place == 0 && fw_door_at(10, 164) && fw_door_at(463, 193) && !fw_door_at(464, 170) &&
          !fw_door_at(10, 163) && !fw_door_at(10, 194), "the door");
    // (the card's page has a line down to y 161 and its news from 196: card.c's L5_Y and NEWS_Y)
    for (int i = 0; i < ntexts; i++) CHECK(texts[i].y >= 162 && texts[i].y + texts[i].h <= 196, "the door's name at y %d", texts[i].y);
    printf("fwpage: %d checks, %d failed\n", checks, failed);
}

static void mode_bare(const char *img) {
    board(img);
    CHECK(card_start() == CARD_OK, "the card: %s", card_state_name(card_state()));
    open_page();
    CHECK(seen("NO FOLDER FM1") && seen("PUT .FWSC PACKAGES IN A FOLDER FM1 ON") && seen("THE CARD") && !any_row(), "no folder: the page");
    tap_button("INSTALL"), tap_button("MORE");
    CHECK(!button("SURE?") && !fw_busy() && bad_place == 0, "no folder: a button does something");
    printf("fwpage bare: %d checks, %d failed\n", checks, failed);
}

int main(int argc, char **argv) {
    if (argc == 3 && !strcmp(argv[1], "make")) {
        static uint8_t junk[5000];
        packages();
        for (int i = 0; i < NPK; i++) put_file(argv[2], PK[i].name, raw[i], PK[i].size);
        lcg_x = 99;
        for (int i = 0; i < 5000; i++) junk[i] = lcg();
        put_file(argv[2], "junk.fwsc", junk, 5000);
        put_file(argv[2], "tiny.fwsc", junk, 100);
        put_file(argv[2], "notes.txt", "not a package\n", 14);
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "run")) mode_run(argv[2]);
    else if (argc == 3 && !strcmp(argv[1], "bare")) mode_bare(argv[2]);
    else { printf("usage: fw_check make DIR | run IMG | bare IMG\n"); return 2; }
    return failed != 0;
}
