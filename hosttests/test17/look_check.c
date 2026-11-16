// Test 17's host check: the light look (look.h), and where it is kept.
//
//   look_check map                 the rule itself, over every RGB565 colour, and the box's own pairs of colours
//   look_check settings OLD.TXT    SETTINGS.TXT: both looks there and back, the wrong ones refused, and OLD.TXT
//                                  (written by test 16's own settings.c: no look in it) read as it was meant
//   look_check pages DIR           the calibration page and the strip (draw_strip() cut from main.c by run.sh), drawn
//                                  in both looks through lcd.c and compared pixel by pixel; pictures (PPM) into DIR
//   look_check frames DARK LIGHT NAME...   pictures the other checks made of their pages in each look (NAME.ppm in
//                                  both folders): the light one must be the dark one with every colour turned over
//
// "Reads" here is the WCAG contrast ratio (1 .. 21) between two colours as an sRGB screen would show them. It is a
// sum about numbers: what the panel itself makes of them is for the board to say.
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lcd.h"
#include "look.h"

static int checks, failed;
#define CHECK(c, ...) do { checks++; if (!(c)) { if (++failed <= 40) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } } while (0)

static void rgb8(uint16_t c, int *r, int *g, int *b) {
    *r = c >> 11 & 31, *g = c >> 5 & 63, *b = c & 31;
    *r = *r << 3 | *r >> 2, *g = *g << 2 | *g >> 4, *b = *b << 3 | *b >> 2;
}
static double lin(int v) { double c = v / 255.0; return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4); }
static double lum(uint16_t c) {
    int r, g, b;
    rgb8(c, &r, &g, &b);
    return 0.2126 * lin(r) + 0.7152 * lin(g) + 0.0722 * lin(b);
}
static double reads(uint16_t a, uint16_t b) {
    double x = lum(a), y = lum(b);
    if (x < y) { double t = x; x = y; y = t; }
    return (x + 0.05) / (y + 0.05);
}
static int luma565(uint16_t c) {
    int r, g, b;
    rgb8(c, &r, &g, &b);
    return look_luma(r, g, b);
}

// ---------------------------------------------------------------- the rule

#ifdef MODE_MAP
// The box's pairs: what is drawn on what, as its sources have them (run.sh reads that the strip's and the status
// line's are still main.c's). text: letters, which must read; otherwise a ground against the ground behind it, which
// only has to be no fainter than it was.
#define K_CELL  RGB(24, 22, 28)
#define K_S1    RGB(26, 22, 30)
#define K_S2    RGB(40, 34, 46)
#define K_BPM   RGB(150, 140, 125)
static const struct { const char *what; uint16_t fg, bg; bool text; } PAIRS[] = {
    {"white on the ground (titles, the card's page, buttons)", C_WHITE, C_BLACK, true},
    {"black on white (a button held)", C_BLACK, C_WHITE, true},
    {"grey on the ground (the status line, small print)", C_GREY, C_BLACK, true},
    {"orange on the ground (warnings, the guest's strip)", C_ORANGE, C_BLACK, true},
    {"red on the ground (the status line: no FM-1)", C_RED, C_BLACK, true},
    {"green on the ground (the status line: FM-1 OK)", C_GREEN, C_BLACK, true},
    {"vellum on the ground", C_VELLUM, C_BLACK, true},
    {"the strip: a name on its cell", C_VELLUM, K_S1, true},
    {"the strip: a name on the other cell", C_VELLUM, K_S2, true},
    {"the strip: a tempo on its cell", K_BPM, K_S1, true},
    {"the strip: a tempo on the other cell", K_BPM, K_S2, true},
    {"the strip: the piece sent (black on orange)", C_BLACK, C_ORANGE, true},
    {"the strip: a name under a finger (vellum on grey)", C_VELLUM, C_GREY, true},
    {"the guest's strip: orange on its ground", C_ORANGE, K_S1, true},
    {"the FM-1's page: a label", RGB(150, 150, 150), C_BLACK, true},
    {"the FM-1's page: playing", RGB(90, 210, 110), C_BLACK, true},
    {"the effects: a name on its cell", C_VELLUM, K_CELL, true},
    {"the effects: the pad's knob on its cell", RGB(90, 205, 170), K_CELL, true},
    {"the effects: a dim name on its cell", K_BPM, K_CELL, true},
    {"the strip: a tempo under a finger (on grey; faint in the dark too)", K_BPM, C_GREY, false},
    {"a cell against the ground", K_CELL, C_BLACK, false},
    {"the strip against the ground", K_S1, C_BLACK, false},
    {"a rule (45) on the ground", RGB(45, 45, 45), C_BLACK, false},
    {"a dimmed label (70) on the ground", RGB(70, 70, 70), C_BLACK, false},
    {"a rule (70, 62, 78) on the ground", RGB(70, 62, 78), C_BLACK, false},
    {"the pad's guides on the pad", RGB(52, 48, 60), RGB(14, 13, 18), false},
};

static int map(void) {
    // dark is the colour itself, every one; and anything that is not LOOK_LIGHT is dark
    for (int c = 0; c < 65536; c++) {
        CHECK(look_565((uint16_t)c, LOOK_DARK) == c, "dark changes %04x", c);
        CHECK(look_565((uint16_t)c, 7) == c, "a look that isn't one changes %04x", c);
    }
    CHECK(look_565(C_BLACK, LOOK_LIGHT) == C_WHITE, "black is %04x in the light", look_565(C_BLACK, LOOK_LIGHT));
    CHECK(look_565(C_WHITE, LOOK_LIGHT) == C_BLACK, "white is %04x in the light", look_565(C_WHITE, LOOK_LIGHT));
    // the greys stay greys, and keep their order turned over: a lighter one in the dark is a darker one in the light
    int last = 256;
    for (int v = 0; v < 32; v++) {
        uint16_t g = (uint16_t)(v << 11 | (v << 1 | v >> 4) << 5 | v), l = look_565(g, LOOK_LIGHT);
        int r, gg, b;
        rgb8(l, &r, &gg, &b);
        CHECK(abs(r - gg) <= 4 && abs(gg - b) <= 4 && abs(r - b) <= 4, "grey %d has a hue in the light: %d,%d,%d", v, r, gg, b);
        CHECK(luma565(l) <= last, "grey %d is lighter than grey %d in the light", v, v - 1);
        last = luma565(l);
    }
    // every colour: its hue keeps its order (what was redder than green stays so); one that read on black reads on
    // white; and a pure one is deep
    int flips = 0, weak45 = 0, weak3 = 0;
    double low45 = 99, low3 = 99;
    for (int c = 0; c < 65536; c++) {
        uint16_t l = look_565((uint16_t)c, LOOK_LIGHT);
        int r = c >> 11 & 31, g = (c >> 5 & 63) >> 1, b = c & 31, R = l >> 11 & 31, G = (l >> 5 & 63) >> 1, B = l & 31;
        if ((r > g + 1 && R < G - 1) || (g > r + 1 && G < R - 1) || (r > b + 1 && R < B - 1) || (b > r + 1 && B < R - 1) ||
            (g > b + 1 && G < B - 1) || (b > g + 1 && B < G - 1)) flips++;
        double d = reads((uint16_t)c, C_BLACK), e = reads(l, C_WHITE);
        if (d >= 4.5) { if (e < low45) low45 = e; if (e < 3.0) weak45++; }
        if (d >= 3.0) { if (e < low3) low3 = e; if (e < 2.6) weak3++; }
    }
    CHECK(flips == 0, "%d colours change the order of their red, green and blue", flips);
    CHECK(weak45 == 0, "%d colours that read 4.5 or better on black read under 3 on white", weak45);
    CHECK(weak3 == 0, "%d colours that read 3 or better on black read under 2.6 on white", weak3);
    printf("every colour: 4.5 or better on black is %.2f or better on white; 3 or better is %.2f or better\n", low45, low3);
    uint16_t pure[3] = {C_RED, C_GREEN, C_BLUE};
    for (int i = 0; i < 3; i++)
        CHECK(reads(look_565(pure[i], LOOK_LIGHT), C_WHITE) >= 4.4, "pure colour %d reads %.2f on white", i,
              reads(look_565(pure[i], LOOK_LIGHT), C_WHITE));
    // the box's own pairs
    double low = 99;
    for (unsigned i = 0; i < sizeof PAIRS / sizeof PAIRS[0]; i++) {
        uint16_t f = look_565(PAIRS[i].fg, LOOK_LIGHT), b = look_565(PAIRS[i].bg, LOOK_LIGHT);
        double d = reads(PAIRS[i].fg, PAIRS[i].bg), e = reads(f, b);
        if (PAIRS[i].text) {
            CHECK(e >= 4.0, "%s: reads %.2f in the light (%.2f in the dark)", PAIRS[i].what, e, d);
            if (e < low) low = e;
        } else {
            CHECK(e >= d, "%s: fainter in the light (%.2f) than the dark (%.2f)", PAIRS[i].what, e, d);
        }
        CHECK(f != b, "%s: one colour in the light", PAIRS[i].what);
        if (getenv("LOOK_PAIRS")) printf("  %-66s dark %5.2f  light %5.2f\n", PAIRS[i].what, d, e);
    }
    printf("the box's pairs: %u; the faintest lettering in the light reads %.2f\n", (unsigned)(sizeof PAIRS / sizeof PAIRS[0]), low);
    return 0;
}
#endif

// ---------------------------------------------------------------- SETTINGS.TXT

#ifdef MODE_SETTINGS
#include "settings.h"

static const unsigned OLD = SET_WARMTH | SET_SPI | SET_PALETTE | SET_GROUND | SET_VCOM | SET_VRH | SET_GAMMA |
                            SET_PAD_ACROSS | SET_PAD_UP;

static void some(settings_t *s) {
    memset(s, 0, sizeof *s);
    s->warmth = 2, s->spi = 1, s->palette = 1, s->ground = 2, s->pad[0] = 0, s->pad[1] = 1;
}

static int settings(const char *old) {
    static char text[SETTINGS_MAX + 1], t2[SETTINGS_MAX + 1];
    for (int look = 0; look < LOOK_N; look++) {
        settings_t a, b;
        settings_report_t r;
        some(&a);
        a.look = (uint8_t)look;
        size_t n = settings_format(&a, "FALLING", text, sizeof text);
        CHECK(n > 0 && n < SETTINGS_MAX, "the file is %zu bytes", n);
        char want[24];
        snprintf(want, sizeof want, "\nlook = %s\n", SET_LOOKS[look]);
        CHECK(strstr(text, want) != NULL, "no line saying look = %s", SET_LOOKS[look]);
        some(&b);
        b.look = (uint8_t)!look;   // the other one, so that reading it is what sets it
        settings_parse(text, n, &b, &r);
        CHECK(r.got == (OLD | SET_LOOK), "look %s: got %x of %x", SET_LOOKS[look], r.got, OLD | SET_LOOK);
        CHECK(r.bad == 0 && r.unknown == 0 && !r.cut, "look %s: %d bad, %d unknown", SET_LOOKS[look], r.bad, r.unknown);
        CHECK(b.look == look, "look %s read back as %d", SET_LOOKS[look], b.look);
        CHECK(settings_same(&a, &b), "look %s: not the same after the file", SET_LOOKS[look]);
        size_t n2 = settings_format(&b, "FALLING", t2, sizeof t2);
        CHECK(n2 == n && !memcmp(text, t2, n), "look %s: the file written again is another", SET_LOOKS[look]);
        b.look = (uint8_t)!look;
        CHECK(!settings_same(&a, &b), "two settings with different looks are the same: it would never be saved");
    }
    // by hand: any case, spaces; and the ones that aren't
    static const struct { const char *line; int look, bad; } HAND[] = {
        {"look = light\n", LOOK_LIGHT, 0}, {"LOOK=LIGHT\n", LOOK_LIGHT, 0}, {"  Look   =   Dark  \r\n", LOOK_DARK, 0},
        {"look = light # as the FM-1\n", LOOK_LIGHT, 0},
        {"look = sepia\n", -1, 1}, {"look = \n", -1, 1}, {"look = 1\n", -1, 1}, {"look = lightish\n", -1, 1},
    };
    for (unsigned i = 0; i < sizeof HAND / sizeof HAND[0]; i++)
        for (int was = 0; was < LOOK_N; was++) {
            settings_t s;
            settings_report_t r;
            some(&s);
            s.look = (uint8_t)was;
            settings_parse(HAND[i].line, strlen(HAND[i].line), &s, &r);
            if (HAND[i].look >= 0) {
                CHECK(r.got == SET_LOOK && r.bad == 0 && r.unknown == 0, "[%s]: got %x, %d bad", HAND[i].line, r.got, r.bad);
                CHECK(s.look == HAND[i].look, "[%s]: look %d", HAND[i].line, s.look);
            } else {
                CHECK(r.got == 0 && r.bad == HAND[i].bad && r.first_line == 1, "[%s]: got %x, %d bad", HAND[i].line, r.got, r.bad);
                CHECK(s.look == was, "[%s]: a look that isn't one changed it to %d", HAND[i].line, s.look);
                CHECK(strstr(r.first, "dark or light") != NULL, "[%s]: says \"%s\"", HAND[i].line, r.first);
            }
        }
    // the file test 16 wrote
    FILE *f = fopen(old, "rb");
    CHECK(f != NULL, "no %s", old);
    if (f) {
        size_t n = fread(text, 1, SETTINGS_MAX, f);
        fclose(f);
        text[n] = 0;
        CHECK(n > 500 && !strstr(text, "look"), "test 16's file is %zu bytes, or speaks of a look", n);
        for (int was = 0; was < LOOK_N; was++) {
            settings_t s, w;
            settings_report_t r;
            memset(&s, 0xEE, sizeof s);   // nothing it reads may be left as it was
            s.look = (uint8_t)was;
            settings_parse(text, n, &s, &r);
            CHECK(r.got == OLD, "test 16's file: got %x, should be %x (all but the look)", r.got, OLD);
            CHECK(r.bad == 0 && r.unknown == 0 && !r.cut && r.first_line == 0, "test 16's file: %d bad, %d unknown", r.bad, r.unknown);
            CHECK(s.look == was, "test 16's file changed the look from %d to %d", was, s.look);
            some(&w);
            w.look = (uint8_t)was;
            w.panel = s.panel;
            CHECK(settings_same(&s, &w), "test 16's file: its settings read as others");
        }
    }
    printf("a file of test 16's: every line used, the look left as it is (dark, on a box just started)\n");
    return 0;
}
#endif

// ---------------------------------------------------------------- the pages, through lcd.c

#ifdef MODE_PAGES
static void ppm_rows(const char *dir, const char *name, uint32_t *const *fb, int nfb, int y0, int h) {
    char path[600];
    snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) { printf("FAIL: can't write %s\n", path); failed++; return; }
    fprintf(f, "P6\n%d %d\n255\n", LCD_W, h * nfb);
    for (int k = 0; k < nfb; k++)
        for (int i = y0 * LCD_W; i < (y0 + h) * LCD_W; i++) {
            uint8_t rgb[3] = {255, 0, 255};
            if (fb[k][i] != 0xFFFFFFFFu) {
                int r, g, b;
                rgb8((uint16_t)fb[k][i], &r, &g, &b);
                rgb[0] = (uint8_t)r, rgb[1] = (uint8_t)g, rgb[2] = (uint8_t)b;
            }
            fwrite(rgb, 1, 3, f);
        }
    fclose(f);
}
#endif

#ifdef MODE_PAGES
#include "cal.h"
#include "strip.h"
extern uint32_t rec[];
extern int nrec, bad_cs;

#define SENTINEL 0xFFFFFFFFu
static uint32_t fb[2][5][LCD_W * LCD_H];   // [look][page]
static int win_bad, px_bad;

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

// what cal.c asks of main.c
void say(const char *fmt, ...) { (void)fmt; }

// draw_strip(), as main.c has it (strip.inc: its PIECES and the function, cut out by run.sh)
#define STATUS_Y 272
#define STRIP_Y  284
#define STRIP_H  (LCD_H - STRIP_Y)
static int last_sent = -1, pressed_cell = -1;
static struct { bool on; } gu;
static bool guest_strip(void *g, char *a, char *b) {
    (void)g;
    if (!gu.on) return false;
    strcpy(a, "A GUEST: KEYSTEP 37");
    strcpy(b, "ITS NOTES PLAY THE LAND. THE BOX SENDS IT NOTHING.");
    return true;
}
#include "strip.inc"

enum { P_CAL, P_STRIP, P_SENT, P_HELD, P_GUEST, P_N };

static void draw_all(int look) {
    lcd_look_set(look);
    CHECK(lcd_look() == look, "the look set is %d, asked %d", lcd_look(), look);
    nrec = 0, cal_draw(), decode(fb[look][P_CAL]);
    gu.on = false, last_sent = -1, pressed_cell = -1;
    draw_strip(), decode(fb[look][P_STRIP]);
    last_sent = 2;
    draw_strip(), decode(fb[look][P_SENT]);
    pressed_cell = 5;
    draw_strip(), decode(fb[look][P_HELD]);
    gu.on = true;
    draw_strip(), decode(fb[look][P_GUEST]);
}

static int pages(const char *dir) {
    CHECK(lcd_look() == LOOK_DARK, "the box starts in look %d, not the dark", lcd_look());
    lcd_look_set(9);
    CHECK(lcd_look() == LOOK_DARK, "a look that isn't one is taken as %d", lcd_look());
    draw_all(LOOK_DARK);
    draw_all(LOOK_LIGHT);
    CHECK(!win_bad && !px_bad && !bad_cs, "the screen was sent a bad window (%d), a short fill (%d) or bytes unselected (%d)",
          win_bad, px_bad, bad_cs);
    // the strip, in each of its states: every pixel drawn, and the light one the dark one turned over
    for (int p = P_STRIP; p < P_N; p++) {
        int miss = 0, wrong = 0, same = 0;
        for (int i = STRIP_Y * LCD_W; i < LCD_H * LCD_W; i++) {
            uint32_t d = fb[0][p][i], l = fb[1][p][i];
            if (d == SENTINEL || l == SENTINEL) { miss++; continue; }
            if (l != look_565((uint16_t)d, LOOK_LIGHT)) wrong++;
            if (l == d) same++;
        }
        CHECK(miss == 0, "strip %d: %d pixels not drawn", p, miss);
        CHECK(wrong == 0, "strip %d: %d pixels aren't the dark ones turned over", p, wrong);
        CHECK(same == 0, "strip %d: %d pixels the same in both looks", p, same);
        int n = 0;   // and lettering in it: more than one colour to a cell
        for (int i = STRIP_Y * LCD_W + 1; i < LCD_H * LCD_W; i++) n += fb[1][p][i] != fb[1][p][i - 1];
        CHECK(n > 400, "strip %d: nothing written in it (%d edges)", p, n);
    }
    // calibration: the ramps and the swatches as they are (they are what the panel is set by), the page about them
    // turned over. The one place the two differ in what they say (VELLUM ON BLACK / INK ON WHITE) is left out.
    long kept = 0, turned = 0, wrong = 0, miss = 0;
    for (int y = 0; y < LCD_H; y++)
        for (int x = 0; x < LCD_W; x++) {
            if (x >= 190 && x < 286 && y >= 232 && y < 244) continue;
            uint32_t d = fb[0][P_CAL][y * LCD_W + x], l = fb[1][P_CAL][y * LCD_W + x];
            if (d == SENTINEL || l == SENTINEL) { miss++; continue; }
            uint16_t t = look_565((uint16_t)d, LOOK_LIGHT);
            if (l == t && l != d) turned++;
            else if (l == d && l != t) {
                kept++;
                bool ramp = (y >= 22 && y < 70) || (y >= 92 && y < 162) || (y >= 166 && y < 196);
                if (!ramp) wrong++;
            } else if (l != t) wrong++;
        }
    CHECK(miss == 0, "calibration: %ld pixels not drawn", miss);
    CHECK(wrong == 0, "calibration: %ld pixels neither kept (a ramp, a swatch) nor turned over (the page)", wrong);
    CHECK(kept > 60000, "calibration: only %ld pixels of ramps and swatches kept as they are", kept);
    CHECK(turned > 60000, "calibration: only %ld pixels of the page turned over", turned);
    CHECK(fb[1][P_CAL][2 * LCD_W + 470] == C_WHITE || fb[1][P_CAL][319 * LCD_W + 479] != fb[0][P_CAL][319 * LCD_W + 479],
          "calibration: the light page's ground isn't white");
    // the LOOK button: where it is touched, and that the others are where they were
    CHECK(cal_button_at(300, 220) == CAL_LOOK && cal_button_at(383, 247) == CAL_LOOK && cal_button_at(288, 212) == CAL_LOOK,
          "LOOK isn't under a finger at its place");
    CHECK(cal_button_at(287, 220) != CAL_LOOK && cal_button_at(300, 211) != CAL_LOOK && cal_button_at(300, 248) != CAL_LOOK,
          "LOOK is touched outside its place");
    CHECK(cal_button_at(384, 220) == CAL_CARD && cal_button_at(479, 230) == CAL_CARD, "CARD has moved");
    CHECK(cal_button_at(10, 260) == CAL_WARM_DN && cal_button_at(470, 310) == CAL_LAND, "the two rows of buttons have moved");
    // pixels sent as they are (the landscape's way, and the uncial lettering's once mixed) are not touched
    lcd_look_set(LOOK_LIGHT);
    static uint16_t px[64];
    for (int i = 0; i < 64; i++) px[i] = (uint16_t)(i * 1039 + 7);
    nrec = 0;
    lcd_blit(8, 8, 8, 8, px);
    static uint32_t o[LCD_W * LCD_H];
    decode(o);
    int moved = 0;
    for (int i = 0; i < 64; i++) moved += o[(8 + i / 8) * LCD_W + 8 + i % 8] != px[i];
    CHECK(moved == 0, "lcd_blit changed %d of 64 pixels in the light look", moved);
    if (dir) {
        uint32_t *one[1];
        for (int look = 0; look < 2; look++) {
            char name[40];
            one[0] = fb[look][P_CAL];
            snprintf(name, sizeof name, "calibration-%s", look ? "light" : "dark");
            ppm_rows(dir, name, one, 1, 0, LCD_H);
            uint32_t *four[4] = {fb[look][P_STRIP], fb[look][P_SENT], fb[look][P_HELD], fb[look][P_GUEST]};
            snprintf(name, sizeof name, "strip-%s", look ? "light" : "dark");
            ppm_rows(dir, name, four, 4, STRIP_Y, STRIP_H);
        }
    }
    printf("calibration: %ld pixels kept (ramps, swatches), %ld turned over; the strip in 4 states\n", kept, turned);
    return 0;
}
#endif

// ---------------------------------------------------------------- pictures the other checks made

#ifdef MODE_FRAMES
static uint8_t *load(const char *dir, const char *name) {
    char path[600];
    snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    int w = 0, h = 0, m = 0;
    uint8_t *p = NULL;
    if (fscanf(f, "P6 %d %d %d", &w, &h, &m) == 3 && w == LCD_W && h == LCD_H && m == 255 && fgetc(f) == '\n') {
        p = malloc((size_t)(w * h * 3));
        if (fread(p, 3, (size_t)(w * h), f) != (size_t)(w * h)) { free(p); p = NULL; }
    }
    fclose(f);
    return p;
}
static uint16_t to565(const uint8_t *p) { return (uint16_t)((p[0] >> 3) << 11 | (p[1] >> 2) << 5 | p[2] >> 3); }
static bool unwritten(const uint8_t *p) { return (p[0] == 255 && p[1] == 0 && p[2] == 255) || (p[0] == 28 && p[1] == 28 && p[2] == 28); }

// A page drawn by lcd_fill_rect and lcd_text is, in the light, the dark page with each pixel turned over, exactly.
// The uncial lettering (perf.c) is mixed from its two colours after they are turned, so the pixels of a letter's
// soft edge are near, not equal: each must lie between the turned ink and the turned ground, which is checked as its
// luma being within the span of the turned pixels about it (the 5 x 5 round it, and 24 either way: mixing after
// the turning comes out a little lighter than turning after the mixing), and there must not be many of them.
static int frames(const char *dd, const char *ld, int n, char **names) {
    long all = 0, exact = 0, soft = 0, far = 0;
    for (int k = 0; k < n; k++) {
        uint8_t *d = load(dd, names[k]), *l = load(ld, names[k]);
        CHECK(d && l, "%s: no picture in %s", names[k], !d ? dd : ld);
        if (!d || !l) continue;
        long ex = 0, so = 0, fa = 0, un = 0, differ = 0;
        for (int y = 0; y < LCD_H; y++)
            for (int x = 0; x < LCD_W; x++) {
                const uint8_t *a = d + (y * LCD_W + x) * 3, *b = l + (y * LCD_W + x) * 3;
                if (unwritten(a) || unwritten(b)) {
                    if (memcmp(a, b, 3)) fa++;
                    un++;
                    continue;
                }
                uint16_t t = look_565(to565(a), LOOK_LIGHT), got = to565(b);
                if (got != to565(a)) differ++;
                if (got == t) { ex++; continue; }
                int lo = 255, hi = 0;   // the turned pixels about it, 5 x 5
                for (int j = -2; j <= 2; j++)
                    for (int i = -2; i <= 2; i++) {
                        int xx = x + i, yy = y + j;
                        if (xx < 0 || yy < 0 || xx >= LCD_W || yy >= LCD_H) continue;
                        const uint8_t *q = d + (yy * LCD_W + xx) * 3;
                        if (unwritten(q)) continue;
                        int v = luma565(look_565(to565(q), LOOK_LIGHT));
                        if (v < lo) lo = v;
                        if (v > hi) hi = v;
                    }
                int v = luma565(got);
                if (v >= lo - 24 && v <= hi + 24) so++; else { fa++; if (getenv("LOOK_FAR")) printf("  %s (%d,%d): luma %d, about it %d..%d\n", names[k], x, y, v, lo, hi); }
            }
        CHECK(fa == 0, "%s: %ld pixels in the light aren't the dark ones turned over, nor a letter's edge", names[k], fa);
        CHECK(so * 100 <= (ex + so) * 8, "%s: %ld pixels only near (of %ld): more than letters' edges", names[k], so, ex + so);
        CHECK(differ * 2 > ex + so, "%s: the light picture is mostly the dark one (%ld pixels differ)", names[k], differ);
        CHECK(ex + so + fa + un == (long)LCD_W * LCD_H && ex > 50000, "%s: %ld pixels drawn", names[k], ex + so);
        all += ex + so, exact += ex, soft += so, far += fa;
        free(d), free(l);
    }
    printf("%d pictures in both looks: %ld pixels, %ld the dark one turned over exactly, %ld a letter's edge, %ld neither\n",
           n, all, exact, soft, far);
    return 0;
}
#endif

int main(int argc, char **argv) {
    (void)argc, (void)argv;
    (void)reads, (void)luma565;
#ifdef MODE_MAP
    map();
#elif defined MODE_SETTINGS
    if (argc < 2) return 2;
    settings(argv[1]);
#elif defined MODE_PAGES
    pages(argc >= 2 ? argv[1] : NULL);
#elif defined MODE_FRAMES
    if (argc < 4) return 2;
    frames(argv[1], argv[2], argc - 3, argv + 3);
#endif
    printf("look: %d checks, %d failed\n", checks, failed);
    return failed != 0;
}
