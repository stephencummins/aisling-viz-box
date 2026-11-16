// Test 18's host check: the box's own pages keep clear of the screen's last 16 pixels (ui.h: x >= UI_W, 464).
//
//   ui_check pages DIR     every page the box draws itself, drawn through lcd.c in both looks and read back pixel by
//                          pixel: calibration (and each button held), the strip in its states, the status line's tail
//                          (cut from main.c by run.sh, with the strip), the card's page in its four states (card.c
//                          with a pretend slot), the effects, the pad, the FM-1's page and a guest's as they start,
//                          and all 27 flashes. Right of UI_W there must be the ground and nothing else (a flash: its
//                          own colour, whole). Then where a finger finds a button: nowhere right of UI_W, and each
//                          button's size. Pictures (PPM) into DIR.
//   ui_check frames DIR... the pictures test 7's, test 11's and test 13's checks make of the FM-1's page, the effects,
//                          the pad, a guest's page and three flashes in many more states (run.sh makes them in both
//                          looks): the same rule over each.
//
// What a touch right of UI_W does on each page is touch_check.c's, with main.c's own poll_touch.
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lcd.h"
#include "look.h"
#include "ui.h"

static int checks, failed;
#define CHECK(c, ...) do { checks++; if (!(c)) { if (++failed <= 60) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } } while (0)

static void rgb8(uint16_t c, int *r, int *g, int *b) {
    *r = c >> 11 & 31, *g = c >> 5 & 63, *b = c & 31;
    *r = *r << 3 | *r >> 2, *g = *g << 2 | *g >> 4, *b = *b << 3 | *b >> 2;
}

static const char *LOOKS[2] = {"dark", "light"};

#ifdef MODE_PAGES
#include "ff.h"
#include "sd.h"
#include "msc.h"
#include "settings.h"
#include "cal.h"
#include "card.h"
#include "strip.h"
#include "perf.h"
#include "fm1scr.h"
#include "knobctl.h"
#include "guest.h"
extern uint32_t rec[];
extern int nrec, bad_cs;

#define SENTINEL 0xFFFFFFFFu
static uint32_t fb[LCD_W * LCD_H];
static int win_bad, px_bad;

// What the screen was sent since the last time, as pixels (SENTINEL: not drawn). As test 17's look_check.c.
static void decode(uint32_t *o) {
    int c = -1, np = 0, cx0 = 0, cx1 = -1, cy0 = 0, cy1 = -1, x = 0, y = 0;
    long px = 0;
    uint8_t p[4] = {0};
    for (int i = 0; i < LCD_W * LCD_H; i++) o[i] = SENTINEL;
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
            if (x <= cx1 && y <= cy1 && x < LCD_W && y < LCD_H) o[y * LCD_W + x] = d;
            px++;
            if (++x > cx1) x = cx0, y++;
        }
    }
    nrec = 0;
}

static const char *shots;
static void ppm(const char *name, int look, int y0, int h) {
    if (!shots) return;
    char path[600];
    snprintf(path, sizeof path, "%s/%s-%s.ppm", shots, name, LOOKS[look]);
    FILE *f = fopen(path, "wb");
    if (!f) { CHECK(0, "can't write %s", path); return; }
    fprintf(f, "P6\n%d %d\n255\n", LCD_W, h);
    for (int i = y0 * LCD_W; i < (y0 + h) * LCD_W; i++) {
        uint8_t rgb[3] = {255, 0, 255};
        if (fb[i] != SENTINEL) {
            int r, g, b;
            rgb8((uint16_t)fb[i], &r, &g, &b);
            rgb[0] = (uint8_t)r, rgb[1] = (uint8_t)g, rgb[2] = (uint8_t)b;
        }
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

static int n_pages;
static long n_margin;
// The page just drawn, rows y0 to y1: right of UI_W the ground (want: -1 the look's own ground) and nothing else.
// whole: every pixel there was drawn (the page clears what the last one left). edge: the page reaches the usable
// width's last four pixels (it was laid out again to 464, not slid off the left). Returns the pixels drawn in all.
static long page(const char *name, int look, int y0, int y1, bool whole, bool edge, long want) {
    decode(fb);
    uint32_t ground = want >= 0 ? (uint32_t)want : look_565(C_BLACK, look);
    long other = 0, undrawn = 0, drawn = 0, at_edge = 0, below = 0;
    int ox = -1, oy = -1;
    for (int y = 0; y < LCD_H; y++)
        for (int x = 0; x < LCD_W; x++) {
            uint32_t v = fb[y * LCD_W + x];
            if (v != SENTINEL) drawn++;
            if (v != SENTINEL && (y < y0 || y >= y1)) below++;
            if (y < y0 || y >= y1) continue;
            if (x >= UI_W) {
                n_margin++;
                if (v == SENTINEL) undrawn++;
                else if (v != ground) { if (!other++) ox = x, oy = y; }
            } else if (x >= UI_W - 4 && v != SENTINEL && v != ground) at_edge++;
        }
    CHECK(other == 0, "%s (%s): %ld pixels right of x %d are not the ground, the first at (%d,%d): %04x", name, LOOKS[look],
          other, UI_W, ox, oy, (unsigned)fb[oy * LCD_W + (ox < 0 ? 0 : ox)]);
    if (whole) CHECK(undrawn == 0, "%s (%s): %ld pixels right of x %d not drawn", name, LOOKS[look], undrawn, UI_W);
    if (edge) CHECK(at_edge > 0, "%s (%s): nothing drawn in x %d to %d: the page stops short", name, LOOKS[look], UI_W - 4, UI_W - 1);
    CHECK(below == 0, "%s (%s): %ld pixels drawn outside rows %d to %d", name, LOOKS[look], below, y0, y1 - 1);
    CHECK(drawn > 0, "%s (%s): nothing drawn", name, LOOKS[look]);
    n_pages++;
    return drawn;
}
// the furthest right a pixel that isn't the ground was drawn, in rows y0 to y1
static int right_end(int look, int y0, int y1) {
    uint32_t ground = look_565(C_BLACK, look);
    int r = -1;
    for (int y = y0; y < y1; y++)
        for (int x = LCD_W - 1; x > r; x--)
            if (fb[y * LCD_W + x] != SENTINEL && fb[y * LCD_W + x] != ground) r = x;
    return r;
}

// ---- what cal.c asks of main.c
void say(const char *fmt, ...) { (void)fmt; }

// ---- the card's slot, pretended: a card or none, FAT or not, lent or not
static bool slot_card, slot_fat, slot_save_ok;
static uint32_t slot_read, slot_written;
static bool lent;
FRESULT f_mount(FATFS *fs, const TCHAR *path, BYTE opt) { (void)fs, (void)path; return !opt || slot_fat ? FR_OK : FR_NO_FILESYSTEM; }
bool sd_init(void) { return slot_card; }
bool sd_ok(void) { return slot_card; }
uint32_t sd_sectors(void) { return slot_card ? 62333952u : 0; }
const char *sd_name(void) { return "SD32G"; }
const char *sd_error(void) { return "NO ANSWER TO CMD0 (NOTHING IN THE SLOT, OR NOT SEATED)"; }
void msc_lend(void) { lent = true; }
void msc_take_back(void) { lent = false; }
bool msc_written(void) { return slot_written != 0; }
uint32_t msc_blocks_read(void) { return slot_read; }
uint32_t msc_blocks_written(void) { return slot_written; }
int settings_load(settings_t *s, settings_report_t *r) {
    (void)s;
    memset(r, 0, sizeof *r);
    r->got = 0x3ff, r->bad = 2, r->unknown = 1, r->first_line = 14;
    snprintf(r->first, sizeof r->first, "%s", "WARMTH = PURPLE: NOT A NUMBER FROM -12 TO 12, AND A LONG ONE AT THAT");
    return 1;
}
bool settings_save(const settings_t *s, const char *g) { (void)s, (void)g; return slot_save_ok; }
const char *settings_error(void) { return "THE CARD IS FULL (FR_DENIED), AND MORE"; }
const char *settings_fr_name(int fr) { (void)fr; return "NO FAT VOLUME ON IT (FR_NO_FILESYSTEM)"; }

// ---- main.c's own: the strip and the status line's tail (strip.inc, status.inc: cut out by run.sh)
#define STATUS_Y 272
#define STRIP_Y  284
#define STRIP_H  (LCD_H - STRIP_Y)
static int last_sent = -1, pressed_cell = -1;
static struct { int on; } gu;
static char long_a[80], long_b[80];
static bool pretend_strip(void *g, char *a, char *b) {
    (void)g;
    if (!gu.on) return false;
    strcpy(a, gu.on == 2 ? long_a : "KEYSTEP 37: THE LANDSCAPE, AND WHAT ITS SCREEN SAYS");
    strcpy(b, gu.on == 2 ? long_b : "THE PIECES, KNOBS AND EFFECTS ARE AISLING'S: NOTHING IS SENT TO THIS ONE");
    return true;
}
#define guest_strip(g, a, b) pretend_strip(g, a, b)
#include "strip.inc"
static void status_tail(const char *a, char *b, int n, uint16_t afg) {
#include "status.inc"
}
#undef guest_strip

// ---- where a finger finds something
static void rect_of(int (*at)(int, int), int b, int *x0, int *y0, int *x1, int *y1) {
    *x0 = *y0 = 9999, *x1 = *y1 = -1;
    for (int y = 0; y < LCD_H; y++)
        for (int x = 0; x < LCD_W; x++)
            if (at(x, y) == b) {
                if (x < *x0) *x0 = x;
                if (x > *x1) *x1 = x;
                if (y < *y0) *y0 = y;
                if (y > *y1) *y1 = y;
            }
}
static bool is_rect(int (*at)(int, int), int b, int x0, int y0, int x1, int y1) {
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            if (at(x, y) != b) return false;
    return true;
}
static int strip_at(int x, int y) { return y >= STRIP_Y && y < LCD_H ? strip_cell(x) : -1; }
static int fx_at(int x, int y) { return perf_fx_cell_at(x, y); }

static void touches(void) {
    // right of UI_W (and off the screen's end) nothing is found, whatever the row
    int n = 0;
    for (int y = -2; y < LCD_H + 2; y++)
        for (int x = UI_W; x < LCD_W + 20; x++) {
            n += cal_button_at(x, y) != -1;
            n += card_button_at(x, y) != -1;
            n += perf_fx_cell_at(x, y) != -1;
            n += strip_cell(x) != -1;
        }
    CHECK(n == 0, "%d places right of x %d where a finger finds a button, a cell or a piece", n, UI_W);
    // and the last usable pixel is somebody's on each
    CHECK(cal_button_at(UI_W - 1, 300) == CAL_LAND && cal_button_at(UI_W - 1, 264) == CAL_START && cal_button_at(UI_W - 1, 230) == CAL_CARD,
          "calibration: x %d isn't LAND's, START's and CARD's", UI_W - 1);
    CHECK(card_button_at(UI_W - 1, 300) == CARD_BTN_BACK, "the card's page: x %d isn't BACK's", UI_W - 1);
    CHECK(perf_fx_cell_at(UI_W - 1, 100) >= 0 && strip_cell(UI_W - 1) == STRIP_N - 1, "the effects or the strip: x %d is nobody's", UI_W - 1);

    // sizes: each is a whole rectangle, they tile the usable width, and none is small
    int x0, y0, x1, y1, wmin = 999, wmax = 0, hmin = 999, sum = 0;
    for (int b = 0; b < CAL_BUTTONS; b++) {
        rect_of(cal_button_at, b, &x0, &y0, &x1, &y1);
        CHECK(x1 >= 0 && is_rect(cal_button_at, b, x0, y0, x1, y1), "calibration's button %d isn't one rectangle", b);
        int w = x1 - x0 + 1, h = y1 - y0 + 1;
        CHECK(x0 == ui_col(b % 5, 5) && x1 == ui_col(b % 5 + 1, 5) - 1, "calibration's button %d is x %d to %d", b, x0, x1);
        if (w < wmin) wmin = w;
        if (w > wmax) wmax = w;
        if (h < hmin) hmin = h;
        if (b < 5) sum += w;
    }
    CHECK(sum == UI_W && wmin >= 92 && hmin >= 32, "calibration's ten buttons: %d to %d wide, %d high, a row %d across", wmin, wmax, hmin, sum);
    printf("touch: calibration's ten buttons %d or %d wide by %d (they were 96 by 32), a row %d across\n", wmin, wmax, hmin, sum);
    int lx0, lx1, cx0, cx1;
    rect_of(cal_button_at, CAL_LOOK, &lx0, &y0, &lx1, &y1);
    CHECK(is_rect(cal_button_at, CAL_LOOK, lx0, y0, lx1, y1) && lx1 - lx0 + 1 >= 92 && y1 - y0 + 1 >= 36, "LOOK is %d by %d", lx1 - lx0 + 1, y1 - y0 + 1);
    int lh = y1 - y0 + 1;
    rect_of(cal_button_at, CAL_CARD, &cx0, &y0, &cx1, &y1);
    CHECK(is_rect(cal_button_at, CAL_CARD, cx0, y0, cx1, y1) && cx1 - cx0 + 1 >= 92 && y1 - y0 + 1 >= 36, "CARD is %d by %d", cx1 - cx0 + 1, y1 - y0 + 1);
    CHECK(cx0 == lx1 + 1 && cx1 == UI_W - 1, "LOOK ends at x %d, CARD is x %d to %d", lx1, cx0, cx1);
    printf("touch: LOOK x %d to %d (%d by %d), CARD x %d to %d (%d by %d) (they were 96 by 36 each)\n", lx0, lx1, lx1 - lx0 + 1, lh,
           cx0, cx1, cx1 - cx0 + 1, y1 - y0 + 1);

    sum = 0, wmin = 999, wmax = 0;
    for (int b = 0; b < CARD_BUTTONS; b++) {
        rect_of(card_button_at, b, &x0, &y0, &x1, &y1);
        int w = x1 - x0 + 1;
        CHECK(is_rect(card_button_at, b, x0, y0, x1, y1) && x0 == ui_col(b, CARD_BUTTONS) && y0 == 264 && y1 == LCD_H - 1,
              "the card's button %d is x %d to %d, y %d to %d", b, x0, x1, y0, y1);
        sum += w;
        if (w < wmin) wmin = w;
        if (w > wmax) wmax = w;
    }
    CHECK(sum == UI_W && wmin >= 154, "the card's three buttons: %d to %d wide, %d across", wmin, wmax, sum);
    printf("touch: the card's three buttons %d or %d wide by 56 (they were 160 by 56), %d across\n", wmin, wmax, sum);

    sum = 0, wmin = 999, wmax = 0, hmin = 999;
    for (int c = 0; c < FX_COLS * FX_ROWS; c++) {
        rect_of(fx_at, c, &x0, &y0, &x1, &y1);
        int w = x1 - x0 + 1, h = y1 - y0 + 1;
        CHECK(x1 >= 0 && is_rect(fx_at, c, x0, y0, x1, y1), "the effects' cell %d isn't one rectangle", c);
        if (c < FX_COLS) sum += w;
        if (w < wmin) wmin = w;
        if (w > wmax) wmax = w;
        if (h < hmin) hmin = h;
    }
    CHECK(sum == UI_W && wmin >= 92 && hmin >= 45, "the effects' cells: %d to %d wide, %d high, %d across", wmin, wmax, hmin, sum);
    printf("touch: the effects' 30 cells %d or %d wide by %d (they were 96 by 45), a row %d across\n", wmin, wmax, hmin, sum);

    sum = 0, wmin = 999;
    for (int i = 0; i < STRIP_N; i++) {
        rect_of(strip_at, i, &x0, &y0, &x1, &y1);
        CHECK(x0 == strip_x(i) && x1 - x0 + 1 == STRIP_CW[i], "the strip's piece %d is x %d to %d", i, x0, x1);
        sum += x1 - x0 + 1;
        if (x1 - x0 + 1 < wmin) wmin = x1 - x0 + 1;
    }
    CHECK(sum == UI_W && STRIP_W == UI_W && wmin >= 50, "the strip: %d across, the narrowest piece %d", sum, wmin);
    // ui.h's columns: they end at UI_W whatever their number, and none is empty
    for (int k = 1; k <= 16; k++) {
        CHECK(ui_col(0, k) == 0 && ui_col(k, k) == UI_W, "ui_col: %d columns end at %d", k, ui_col(k, k));
        for (int i = 0; i < k; i++) CHECK(ui_col(i + 1, k) - ui_col(i, k) >= UI_W / k && ui_col(i + 1, k) - ui_col(i, k) <= UI_W / k + 1, "ui_col: %d of %d", i, k);
    }
    CHECK(UI_W == 464 && LCD_W == 480 && ui_off(UI_W) && !ui_off(UI_W - 1) && ui_off(LCD_W - 1), "ui.h: UI_W %d", UI_W);
    CHECK(KC_CELL_W * 4 == UI_W, "the FM-1's four knobs: %d each", KC_CELL_W);
    printf("touch: the FM-1's four knobs %d wide each (they were 120)\n", KC_CELL_W);
}

static void pages(void) {
    static fm1scr_t m;
    static perf_t pf;
    static guest_t g;
    static settings_t st;
    settings_report_t rep;
    char name[60];
    int cal_end = 0, status_end = 0, strip_end = 0, off_min = 999, off_max = -999;
    for (int look = 0; look < 2; look++) {
        lcd_look_set(look);
        nrec = 0;

        // calibration: the whole page, then each button held, then the two lines of settings drawn again
        cal_draw();
        page("calibration", look, 0, LCD_H, true, true, -1);
        ppm("calibration", look, 0, LCD_H);
        if (look == 0) cal_end = right_end(look, 0, LCD_H);
        for (int b = 0; b < CAL_BUTTONS; b++) {
            cal_press_show(b);
            snprintf(name, sizeof name, "calibration, button %d held", b);
            page(name, look, 0, LCD_H, false, false, -1);
        }
        cal_press_show(CAL_LOOK), page("calibration, LOOK held", look, 0, LCD_H, false, false, -1);
        cal_press_show(CAL_CARD), page("calibration, CARD held", look, 0, LCD_H, false, true, -1);
        cal_press_show(-1), page("calibration, CARD let go", look, 0, LCD_H, false, true, -1);
        cal_draw_info(), page("calibration, its settings", look, 0, LCD_H, false, false, -1);

        // the strip: as it is, a piece sent, a piece held, a guest's two lines, and two lines too long for it
        for (int s = 0; s < 6; s++) {
            static const char *const SN[] = {"the strip", "the strip, a piece sent", "the strip, the last piece sent",
                                             "the strip, the last piece held", "the strip, a guest's", "the strip, lines of 79"};
            gu.on = s == 4 ? 1 : s == 5 ? 2 : 0;
            last_sent = s == 1 ? 2 : s == 2 ? STRIP_N - 1 : -1;
            pressed_cell = s == 3 ? STRIP_N - 1 : -1;
            draw_strip();
            long d = page(SN[s], look, STRIP_Y, LCD_H, true, true, -1);
            CHECK(d == (long)LCD_W * STRIP_H, "%s: %ld pixels drawn, not the strip's %d", SN[s], d, LCD_W * STRIP_H);
            if (s == 5) {   // the long lines are cut, not run on: lettering to x 461 and no further
                int r = -1;
                uint32_t bg = fb[(STRIP_Y + 1) * LCD_W];
                for (int y = STRIP_Y; y < LCD_H; y++)
                    for (int x = 0; x < UI_W; x++)
                        if (fb[y * LCD_W + x] != bg && x > r) r = x;
                CHECK(r >= UI_W - 8 && r < UI_W, "the strip's long lines end at x %d", r);
                strip_end = r;
            }
            if (s == 4) {   // a guest's two lines sit in the middle of the page, not of the screen
                uint32_t bg = fb[(STRIP_Y + 1) * LCD_W];
                for (int ln = 0; ln < 2; ln++) {   // each line by itself: rows 8 to 15 and 21 to 28 of the strip
                    int l = LCD_W, r = -1;
                    for (int y = STRIP_Y + (ln ? 21 : 8); y < STRIP_Y + (ln ? 29 : 16); y++)
                        for (int x = 0; x < UI_W; x++)
                            if (fb[y * LCD_W + x] != bg) { if (x < l) l = x; if (x > r) r = x; }
                    int off = l - (UI_W - 1 - r);
                    CHECK(r > l && off >= -6 && off <= 6, "the strip, a guest's (%s): line %d is x %d to %d, %d off the page's middle", LOOKS[look], ln + 1, l, r, off);
                }
            }
            static const char *const PN[] = {"strip", "strip-sent", "strip-sent-last", "strip-held-last", "strip-guest", "strip-long"};
            ppm(PN[s], look, STRIP_Y, STRIP_H);
        }

        // the status line: as long as it can be made
        char b[80];
        memset(b, 'W', 79), b[79] = 0;
        status_tail("FM-1 OK!", b, 79, C_GREEN);
        page("the status line", look, STATUS_Y, STRIP_Y, false, true, -1);
        if (look == 0) status_end = right_end(look, STATUS_Y, STRIP_Y);
        ppm("status-full", look, STATUS_Y, STRIP_Y - STATUS_Y);

        // the card's page: no card, a card that isn't FAT, a card, a card not saved to, a card lent; news; buttons held
        card_open();
        slot_card = false, card_start(), card_draw();
        page("the card's page, no card", look, 0, LCD_H, true, true, -1);
        ppm("card-none", look, 0, LCD_H);
        slot_card = true, slot_fat = false, card_start(), card_draw();
        page("the card's page, not FAT", look, 0, LCD_H, true, true, -1);
        ppm("card-notfat", look, 0, LCD_H);
        slot_fat = true, slot_save_ok = true;
        CHECK(card_start() == CARD_OK && card_load(&st, &rep) == 1 && card_save(&st, ""), "the pretend card isn't taken");
        card_draw();
        page("the card's page, a card", look, 0, LCD_H, true, true, -1);
        ppm("card-ok", look, 0, LCD_H);
        slot_save_ok = false;
        CHECK(!card_save(&st, ""), "the pretend card saves when it shouldn't");
        card_draw_info();
        page("the card's page, not saved", look, 0, LCD_H, false, false, -1);
        card_say("A LINE OF NEWS LONGER THAN THE SCREEN IS WIDE, SO IT BREAKS AT A SPACE AND RUNS ON TO A SECOND LINE BELOW IT");
        page("the card's page, long news", look, 0, LCD_H, false, false, -1);
        card_say("ANEWSLINEWITHNOSPACESATALLANDLONGERTHANTHESCREENISWIDESOITHASTOBECUTSOMEWHEREABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");
        page("the card's page, news with no spaces", look, 0, LCD_H, false, false, -1);
        card_draw();
        ppm("card-news", look, 0, LCD_H);
        page("the card's page, news and not saved", look, 0, LCD_H, true, true, -1);
        slot_read = 4000000000u, slot_written = 4000000000u;
        CHECK(card_lend() && card_state() == CARD_LENT, "the pretend card isn't lent");
        card_draw();
        page("the card's page, at the Mac", look, 0, LCD_H, true, true, -1);
        ppm("card-lent", look, 0, LCD_H);
        slot_read++, card_draw_counts();
        page("the card's page, its counts", look, 0, LCD_H, false, false, -1);
        CHECK(card_press(CARD_BTN_BACK) == CARD_DO_NOTHING, "BACK with the Mac writing");   // its warning, two lines
        page("the card's page, the warning", look, 0, LCD_H, false, false, -1);
        for (int k = 0; k < CARD_BUTTONS; k++) {
            card_press_show(k);
            snprintf(name, sizeof name, "the card's page, button %d held", k);
            page(name, look, 0, LCD_H, false, false, -1);
        }
        card_press_show(-1), page("the card's page, BACK let go", look, 0, LCD_H, false, true, -1);
        slot_written = 0, card_take_back(), slot_read = 0;

        // the effects and the pad, as they start
        perf_init(&pf);
        perf_fx_redraw(&pf), perf_fx_draw(&pf);
        page("the effects", look, 0, STATUS_Y, true, true, -1);
        ppm("effects-start", look, 0, STATUS_Y);
        perf_xy_redraw(&pf), perf_xy_draw(&pf);
        page("the pad", look, 0, STATUS_Y, true, true, -1);
        ppm("pad-start", look, 0, STATUS_Y);

        // the FM-1's page with nothing heard from it, and plugged in; a guest's page
        fm1scr_init(&m);
        for (int conn = 0; conn < 2; conn++) {
            fm1scr_redraw(&m), fm1scr_draw(&m, conn, 1000);
            snprintf(name, sizeof name, "the FM-1's page (%s)", conn ? "plugged in" : "nothing plugged in");
            page(name, look, 0, STATUS_Y, true, false, -1);
            ppm(conn ? "fm1-start" : "fm1-none", look, 0, STATUS_Y);
        }
        // and with everything on it said, as long as each can be: the right-hand boxes end at the page's edge, not the screen's
        m.seen = true, m.playing = 1, m.unsaved = 1, m.view = 0, m.next_pct = 50;
        memset(m.detail, 'W', sizeof m.detail - 1), memset(m.section, 'W', sizeof m.section - 1);
        memset(m.line, 'W', sizeof m.line - 1), memset(m.scale, 'W', sizeof m.scale - 1);
        memset(m.piece, 'W', sizeof m.piece - 1), memset(m.chord, 'W', sizeof m.chord - 1);
        strcpy(m.root, "Eb"), strcpy(m.mode, "Mixolyd."), strcpy(m.tempo, "76");
        for (int i = 0; i < 4; i++) memset(m.value[i], 'W', sizeof m.value[i] - 1);
        fm1scr_redraw(&m), fm1scr_draw(&m, 1, 2000);
        page("the FM-1's page, full", look, 0, STATUS_Y, true, false, -1);
        int fr = right_end(look, 0, STATUS_Y);
        CHECK(fr >= UI_W - 16 && fr < UI_W, "the FM-1's page, full (%s): it ends at x %d", LOOKS[look], fr);
        ppm("fm1-full", look, 0, STATUS_Y);
        guest_init(&g);
        guest_plugged(&g, 1000);
        guest_named(&g, "A NAME OF 23 LETTERS: W", false);
        static const char SAID[] = "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW";
        guest_say(&g, SAID, (int)strlen(SAID), 0);
        guest_say(&g, "AND A SECOND THING SAID, OF WORDS, LONG ENOUGH TO RUN TO THE END OF ITS LINE AND ON PAST IT", 91, 0);
        guest_redraw(&g), guest_draw(&g);
        page("a guest's page", look, 0, STATUS_Y, true, true, -1);
        ppm("guest-full", look, 0, STATUS_Y);

        // the flashes: the colour to the screen's edge (it is a colour, not something to read), the lettering not
        for (int k = 0; k < PFLASH_N; k++) {
            fm1scr_draw_flash(k);
            decode(fb);
            uint32_t c = fb[0];
            int r = -1, l = LCD_W;
            long other = 0, undrawn = 0;
            for (int y = 0; y < STATUS_Y; y++)
                for (int x = 0; x < LCD_W; x++) {
                    uint32_t v = fb[y * LCD_W + x];
                    if (v == SENTINEL) undrawn++;
                    else if (v != c) { if (x >= UI_W) other++; else { if (x > r) r = x; if (x < l) l = x; } }
                }
            int off = l - (UI_W - 1 - r);
            if (off < off_min) off_min = off;
            if (off > off_max) off_max = off;
            CHECK(c != SENTINEL && undrawn == 0, "flash %d (%s): %ld pixels of y 0 to 271 not drawn", k, LOOKS[look], undrawn);
            CHECK(other == 0, "flash %d (%s): %ld pixels right of x %d are not its colour", k, LOOKS[look], other, UI_W);
            CHECK(r >= 0 && r < UI_W, "flash %d (%s): its lettering ends at x %d", k, LOOKS[look], r);
            for (int i = STATUS_Y * LCD_W; i < LCD_H * LCD_W; i++) if (fb[i] != SENTINEL) { CHECK(0, "flash %d draws below y 271", k); break; }
            n_pages++;
            if (k == 5 || k == 20) { snprintf(name, sizeof name, "flash-%d", k); ppm(name, look, 0, STATUS_Y); }
        }
    }
    printf("pages: the flashes' lettering is %d to %d off the page's middle\n", off_min, off_max);
    CHECK(off_min >= -12 && off_max <= 2, "the flashes' lettering is %d to %d off the page's middle", off_min, off_max);
    CHECK(!win_bad && !px_bad && !bad_cs, "the screen was sent a bad window (%d), a short fill (%d) or bytes unselected (%d)",
          win_bad, px_bad, bad_cs);
    CHECK(cal_end == UI_W - 1, "calibration's page ends at x %d", cal_end);
    CHECK(status_end >= UI_W - 8 && status_end < UI_W, "the status line, full, ends at x %d", status_end);
    // a guest's two lines, as guest.c writes them, with the longest name there can be: they fit whole
    guest_t gg;
    char a[80], b[80];
    guest_init(&gg), guest_plugged(&gg, 0);
    guest_named(&gg, "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW", false);
    int la = 0, lb = 0;
    CHECK(guest_strip(&gg, a, b), "no guest's lines");
    la = (int)strlen(a), lb = (int)strlen(b);
    CHECK(strlen(gg.name) == sizeof gg.name - 1 && la <= UI_W / 6 && lb <= UI_W / 6, "a guest's lines are %d and %d letters: more than %d", la, lb, UI_W / 6);
    gg.who = WHO_SILENT;
    CHECK(guest_strip(&gg, a, b) && (int)strlen(a) <= UI_W / 6 && (int)strlen(b) <= UI_W / 6, "a silent one's lines don't fit");
    printf("pages: %d drawn in both looks, %ld pixels right of x %d looked at; calibration ends at x %d, the status line "
           "(69 letters after its 8) at x %d, a 79-letter strip line cut at x %d\n", n_pages, n_margin, UI_W, cal_end, status_end, strip_end);
    printf("pages: a guest's two strip lines are at most %d and %d letters (a name is at most %d); %d fit\n", la, lb,
           (int)sizeof gg.name - 1, UI_W / 6);
}
#endif

// ---------------------------------------------------------------- pictures the other checks made

#ifdef MODE_FRAMES
// A picture as test 7's, 11's and 13's checks write them: 480 across, 320 rows or 272 (a flash). What they didn't
// draw is (28,28,28) or magenta.
static uint8_t *load(const char *path, int *h) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    int w = 0, m = 0;
    uint8_t *p = NULL;
    if (fscanf(f, "P6 %d %d %d", &w, h, &m) == 3 && w == LCD_W && (*h == LCD_H || *h == 272) && m == 255 && fgetc(f) == '\n') {
        p = malloc((size_t)(w * *h * 3));
        if (fread(p, 3, (size_t)(w * *h), f) != (size_t)(w * *h)) { free(p); p = NULL; }
    }
    fclose(f);
    return p;
}
static bool unwritten(const uint8_t *p) { return (p[0] == 255 && p[1] == 0 && p[2] == 255) || (p[0] == 28 && p[1] == 28 && p[2] == 28); }

// frames LOOK DIR NAME...: NAME.ppm in DIR, drawn in that look
static void frames(int look, const char *dir, int n, char **names) {
    int gr, gg, gb, nf = 0;
    long seen = 0;
    rgb8(look_565(C_BLACK, look), &gr, &gg, &gb);
    for (int k = 0; k < n; k++) {
        char path[700];
        int h = 0;
        snprintf(path, sizeof path, "%s/%s.ppm", dir, names[k]);
        uint8_t *p = load(path, &h);
        CHECK(p, "no picture %s", path);
        if (!p) continue;
        bool flash = !strncmp(names[k], "flash-", 6);
        const uint8_t *c0 = p;   // a flash: its colour is its first pixel's
        long other = 0, undrawn = 0, inside = 0;
        int ox = -1, oy = -1, r = -1;
        for (int y = 0; y < (h < 272 ? h : 272); y++)
            for (int x = 0; x < LCD_W; x++) {
                const uint8_t *q = p + (y * LCD_W + x) * 3;
                if (x < UI_W) {
                    if (!unwritten(q)) inside++;
                    if (flash && memcmp(q, c0, 3) && x > r) r = x;
                    continue;
                }
                seen++;
                if (unwritten(q)) undrawn++;
                else if (flash ? memcmp(q, c0, 3) != 0 : q[0] != gr || q[1] != gg || q[2] != gb) { if (!other++) ox = x, oy = y; }
            }
        CHECK(other == 0, "%s (%s): %ld pixels right of x %d are not the ground, the first at (%d,%d)", names[k], LOOKS[look], other, UI_W, ox, oy);
        CHECK(undrawn == 0, "%s (%s): %ld pixels right of x %d not drawn", names[k], LOOKS[look], undrawn, UI_W);
        CHECK(inside > 100000, "%s (%s): only %ld pixels of the page drawn", names[k], LOOKS[look], inside);
        if (flash) CHECK(r > 100 && r < UI_W, "%s (%s): its lettering ends at x %d", names[k], LOOKS[look], r);
        nf++;
        free(p);
    }
    printf("frames: %d pictures (%s), %ld pixels right of x %d looked at\n", nf, LOOKS[look], seen, UI_W);
}
#endif

int main(int argc, char **argv) {
    (void)argc, (void)argv, (void)rgb8;
#ifdef MODE_PAGES
    static const panel_t P0 = {0};
    lcd_init(&P0);
    nrec = 0;
    shots = argc >= 2 ? argv[1] : NULL;
    memset(long_a, 'W', 79), memset(long_b, 'M', 79);
    pages();
    touches();
#elif defined MODE_FRAMES
    if (argc < 4) return 2;
    frames(!strcmp(argv[1], "light") ? LOOK_LIGHT : LOOK_DARK, argv[2], argc - 3, argv + 3);
#endif
    printf("ui: %d checks, %d failed\n", checks, failed);
    return failed != 0;
}
