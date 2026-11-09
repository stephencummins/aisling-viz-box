// Host test for test 4's lcd.c, cal.c and tone.c: decodes every word sent to the ILI9486 and checks it.
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include "sdk_stub.h"
#include "lcd.h"
#include "cal.h"
#include "font5x7.h"
#include "tone.h"

extern uint32_t rec[]; extern int nrec, bad_cs;
static int pass, fail;
#define CHECK(c, ...) do { if (c) pass++; else { fail++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static char said[1024];
void say(const char *fmt, ...) { va_list a; va_start(a, fmt); vsnprintf(said, sizeof said, fmt, a); va_end(a); }

// cal.c's text, caught on the way to the real lcd_text: every character must be in the font, every line on screen.
static int text_calls, text_bad;
static char last_info[2][256];
void test_lcd_text(int x, int y, const char *s, int scale, uint16_t fg, uint16_t bg) {
    text_calls++;
    int len = (int)strlen(s);
    if (x < 0 || x + len * 6 * scale > LCD_W || y + 8 * scale > LCD_H) { text_bad++; printf("  off screen: (%d,%d) x%d \"%s\"\n", x, y, scale, s); }
    for (const char *p = s; *p; p++) {
        char c = *p >= 'a' && *p <= 'z' ? (char)(*p - 32) : *p;
        if (c == ' ') continue;
        bool blank = true;
        if (c >= 32 && c <= 126) for (int i = 0; i < 7; i++) if (font5x7[c - 32][i]) blank = false;
        if (blank) { text_bad++; printf("  not in the font: '%c' in \"%s\"\n", *p, s); }
    }
    if (y == 1) snprintf(last_info[0], sizeof last_info[0], "%s", s);
    if (y == 11) snprintf(last_info[1], sizeof last_info[1], "%s", s);
    lcd_text(x, y, s, scale, fg, bg);
}

// ---------------------------------------------------------------- the decoder
typedef struct { int c; int n; uint8_t p[16]; long px; uint16_t first; int bad_param; } ev_t;
#define MAXEV 200000
static ev_t ev[MAXEV];
static int nev;
static bool seen[256];
static int win_bad, px_bad;
static int cx0, cx1, cy0, cy1;

static void decode(void) {
    nev = 0;
    for (int i = 0; i < nrec && nev < MAXEV; i++) {
        uint32_t w = rec[i];
        if (w == 0xFFFF0000u) { ev[nev++] = (ev_t){.c = -1}; continue; }
        if (!(w >> 16)) {
            ev_t *e = &ev[nev++]; memset(e, 0, sizeof *e);
            e->c = (int)(w & 0xFFFF);
            if (e->c > 0xFF) e->bad_param = 1;
            seen[e->c & 0xFF] = true;
            continue;
        }
        if (!nev || ev[nev - 1].c < 0) { px_bad++; continue; } // data with no command before it
        ev_t *e = &ev[nev - 1];
        if (e->c == 0x2C) { if (!e->px) e->first = (uint16_t)w; e->px++; continue; }
        if ((w & 0xFFFF) > 0xFF) e->bad_param = 1;
        if (e->n < 16) e->p[e->n] = (uint8_t)w;
        e->n++;
    }
    // windows and pixel counts
    for (int i = 0; i < nev; i++) {
        ev_t *e = &ev[i];
        if (e->c == 0x2A) { cx0 = e->p[0] << 8 | e->p[1]; cx1 = e->p[2] << 8 | e->p[3]; if (e->n != 4 || cx0 > cx1 || cx1 >= LCD_W) win_bad++; }
        if (e->c == 0x2B) { cy0 = e->p[0] << 8 | e->p[1]; cy1 = e->p[2] << 8 | e->p[3]; if (e->n != 4 || cy0 > cy1 || cy1 >= LCD_H) win_bad++; }
        if (e->c == 0x2C && e->px != (long)(cx1 - cx0 + 1) * (cy1 - cy0 + 1)) px_bad++;
    }
}
static void clear(void) { nrec = 0; }

// The commands other than pixels, as a string: "R 01 11 3A:55 36:28 C0:0E,0E ..."
static char seq[4096];
static const char *cmds(void) {
    decode();
    int k = 0; seq[0] = 0;
    for (int i = 0; i < nev && k < (int)sizeof seq - 64; i++) {
        ev_t *e = &ev[i];
        if (e->c == 0x2A || e->c == 0x2B || e->c == 0x2C) continue;
        if (e->c < 0) { k += snprintf(seq + k, sizeof seq - k, "%sR", k ? " " : ""); continue; }
        k += snprintf(seq + k, sizeof seq - k, "%s%02X", k ? " " : "", e->c);
        for (int j = 0; j < e->n && j < 16; j++) k += snprintf(seq + k, sizeof seq - k, "%c%02X", j ? ',' : ':', e->p[j]);
    }
    return seq;
}
// How many pixels sent since the last clear() were this colour.
static long px_count(uint16_t v) {
    long n = 0; int c = -1;
    for (int i = 0; i < nrec; i++) {
        uint32_t w = rec[i];
        if (w == 0xFFFF0000u) { c = -1; continue; }
        if (!(w >> 16)) { c = (int)(w & 0xFFFF); continue; }
        if (c == 0x2C && (uint16_t)w == v) n++;
    }
    return n;
}
static const ev_t *find(int c) { for (int i = nev - 1; i >= 0; i--) if (ev[i].c == c) return &ev[i]; return NULL; }
static bool bytes_are(const ev_t *e, const char *hex) {
    char got[64] = ""; int k = 0;
    for (int j = 0; e && j < e->n && j < 16; j++) k += snprintf(got + k, sizeof got - k, "%s%02X", j ? " " : "", e->p[j]);
    if (!e || strcmp(got, hex)) { printf("  got %02X: \"%s\", want \"%s\"\n", e ? e->c : 0, got, hex); return false; }
    return true;
}

int main(void) {
    // ---------------------------------------------------------------- warmth on its own
    CHECK(tone_step() == 0 && tone_g == 256 && tone_b == 256, "before tone_set, no warmth");
    CHECK(tone_565(0xFFFF) == 0xFFFF && tone_565(0x1234) == 0x1234, "step 0 leaves colours alone");
    tone_set(TONE_START);
    CHECK(tone_step() == 2 && tone_g == 243 && tone_b == 220, "start is step 2: G 243, B 220 (%d, %d)", tone_g, tone_b);
    CHECK(tone_565(0xFFFF) == 0xFF9B, "white at step 2 is R31 G60 B27 = FF9B (got %04X)", tone_565(0xFFFF));
    CHECK(tone_565(0x0000) == 0 && tone_565(0xF800) == 0xF800, "black stays black, pure red stays red");
    CHECK(tone8(255, 243) == 0xF2 && tone8(255, 220) == 0xDB, "white in 8 bits at step 2: F2, DB (%02X, %02X)", tone8(255, 243), tone8(255, 220));
    CHECK(tone8(0xED, 243) == 0xE1 && tone8(0xDA, 220) == 0xBB, "the vellum ground F5 ED DA at step 2: F5 E1 BB (%02X %02X)", tone8(0xED, 243), tone8(0xDA, 220));
    { int worst = 0;
      for (int st = 0; st < TONE_STEPS; st++) for (int v = 0; v < 256; v++) {
          int g = tone8(v, tone_gain_g(st)), b = tone8(v, tone_gain_b(st));
          if (g < 0 || g > 255 || b < 0 || b > 255 || g > v || b > v) worst++;
          if (v && tone8(v, tone_gain_g(st)) < tone8(v - 1, tone_gain_g(st))) worst++;
      }
      CHECK(worst == 0, "every step keeps 0..255, never brightens, never reverses a ramp (%d bad)", worst); }
    { int bad = 0;
      for (int st = 1; st < TONE_STEPS; st++)
          if (tone_gain_g(st) >= tone_gain_g(st - 1) || tone_gain_b(st) >= tone_gain_b(st - 1) ||
              256 - tone_gain_b(st) < 2 * (256 - tone_gain_g(st))) bad++;
      CHECK(bad == 0 && tone_gain_g(0) == 256 && tone_gain_b(0) == 256, "each step warmer, blue cut at least twice green's"); }
    CHECK(tone_gain_g(-3) == 256 && tone_gain_b(99) == 184, "the log's gain lookups clamp");
    tone_set(-5); CHECK(tone_step() == 0, "tone_set clamps low");
    tone_set(99); CHECK(tone_step() == 4 && tone_g == 230 && tone_b == 184, "tone_set clamps high");
    tone_set(TONE_START);

    // ---------------------------------------------------------------- start-up (as main.c: tone_set, then lcd_init)
    uint32_t hz = lcd_init(&CAL_START_PANEL);
    const char *s = cmds();
    printf("init at %lu Hz: %s\n", (unsigned long)hz, s);
    char want_init[256];
    snprintf(want_init, sizeof want_init, "R 01 11 3A:55 36:28 E0:%s E1:%s 20 13 29", "0F,3E,3D,0E,1C,0C,57,69,28,03,03,01,02,01,00", "0F,3E,3D,0E,1C,0C,57,69,28,03,03,01,02,01,00");
    CHECK(!strcmp(s, want_init), "init sends reset, 01 11 3A 36, gamma FALLING (E0, E1), 20 13 fill 29 (got %s)", s);
    CHECK(!find(0xC0) && !find(0xC5), "init sends no VCOM or VRH");
    CHECK(px_count(0) == 153600, "init's fill is black, whole screen, warmth or not (%ld)", px_count(0));
    const panel_t *p = lcd_panel();
    CHECK(!p->vcom_set && !p->vrh_set && p->gamma_set, "panel starts with gamma set, VCOM and VRH the chip's own");

    // ---------------------------------------------------------------- the screen
    clear(); text_calls = 0; cal_draw(); decode();
    CHECK(text_bad == 0, "every character in the font and every line on screen (%d bad)", text_bad);
    CHECK((int)strlen(last_info[0]) == 80 && (int)strlen(last_info[1]) == 80, "both info lines are 80 characters (%zu, %zu)", strlen(last_info[0]), strlen(last_info[1]));
    printf("info: [%s]\n      [%s]\n", last_info[0], last_info[1]);
    CHECK(strstr(last_info[0], "AISLING VIZ TEST 4") == last_info[0] && strstr(last_info[0], "SPI 20 MHZ") != NULL, "title says test 4 and SPI 20 MHZ");
    CHECK(strstr(last_info[1], "WARM 2/4  VCOM NV  VRH CHIP  GAMMA FALLING VP20=87/127") == last_info[1], "settings line at start-up");
    CHECK(px_count(0xFFFF) == 0, "at step 2 no pure white pixel goes out (%ld)", px_count(0xFFFF));
    CHECK(px_count(0xFF9B) > 1000, "white goes out warmed, FF9B (%ld pixels)", px_count(0xFF9B));
    { // the vellum ground swatch F5 ED DA, as RGB() packs it, warmed on the way out
      uint16_t vel = RGB(0xF5, 0xED, 0xDA), velw = tone_565(vel);
      printf("vellum swatch: %04X sent as %04X\n", vel, velw);
      CHECK(px_count(vel) == 0 && px_count(velw) > 100, "the GND swatch goes out warmed (%ld unwarmed, %ld warmed)", px_count(vel), px_count(velw)); }
    long px = 0; for (int i = 0; i < nev; i++) px += ev[i].px;
    printf("cal_draw: %d commands, %ld pixels (%.2f screens), %d text calls\n", nev, px, px / 153600.0, text_calls);
    CHECK(!strcmp(cmds(), ""), "cal_draw sends only windows and pixels (got %s)", seq);

    // ---------------------------------------------------------------- buttons
    struct { int x, y, b; } hit[] = {{0, 255, -1}, {-1, 300, -1}, {0, 256, 0}, {95, 256, 0}, {96, 256, 1}, {479, 287, 4},
                                     {0, 288, 5}, {200, 300, 7}, {479, 319, 9}, {479, 400, 9}, {600, 260, 4}, {240, 0, -1}};
    for (size_t i = 0; i < sizeof hit / sizeof hit[0]; i++)
        CHECK(cal_button_at(hit[i].x, hit[i].y) == hit[i].b, "button at (%d,%d) is %d, got %d", hit[i].x, hit[i].y, hit[i].b, cal_button_at(hit[i].x, hit[i].y));
    clear(); cal_press_show(3); cal_press_show(3); cal_press_show(-1);
    CHECK(!strcmp(cmds(), ""), "pressing shows only pixels");

    // ---------------------------------------------------------------- warmth buttons
    clear(); bool r = cal_do(CAL_WARM_UP, false); s = cmds();
    CHECK(r && !strcmp(s, ""), "WARM + asks for a redraw and sends no command (got %s)", s);
    CHECK(tone_step() == 3 && strstr(said, "warmth 3 of 4 (G x237/256, B x202/256)") == said + 7, "logged: %s", said);
    cal_do(CAL_WARM_UP, true);
    clear(); r = cal_do(CAL_WARM_UP, false);
    CHECK(!r && tone_step() == 4 && strstr(said, "at the end of the warmth steps"), "WARM + at the top: no redraw, says so: %s", said);
    for (int i = 0; i < 4; i++) cal_do(CAL_WARM_DN, false);
    CHECK(tone_step() == 0 && strstr(said, "warmth 0 (none)"), "WARM - down to 0: %s", said);
    clear(); r = cal_do(CAL_WARM_DN, false);
    CHECK(!r && tone_step() == 0 && !strcmp(cmds(), ""), "WARM - at 0: nothing");
    clear(); cal_draw_info(); CHECK(strstr(last_info[1], "WARM 0/4  ") == last_info[1], "info shows WARM 0/4: [%s]", last_info[1]);
    clear(); cal_draw(); CHECK(px_count(0xFFFF) > 1000 && px_count(0xFF9B) == 0, "at step 0 white is white again");
    cal_do(CAL_WARM_UP, false); cal_do(CAL_WARM_UP, false);

    // ---------------------------------------------------------------- CHIP from the start: gamma off needs a reset
    clear(); r = cal_do(CAL_CHIP, true); s = cmds();
    CHECK(r && !strcmp(s, "R 01 11 3A:55 36:28 20 13 29"), "k from the start resets and sends nothing (got %s)", s);
    CHECK(tone_step() == 0 && strstr(said, "warmth 0 (none) | VCOM from NV memory (chip) | VRH chip (0x0E) | gamma chip") == said + 7,
          "and takes the warmth off: %s", said);
    CHECK(px_count(0) == 153600, "the reset's fill is black");
    clear(); cal_draw_info();
    CHECK(strstr(last_info[1], "WARM 0/4  VCOM NV  VRH CHIP  GAMMA CHIP (FIELD VP20)") == last_info[1], "settings line after k: [%s]", last_info[1]);

    // ---------------------------------------------------------------- VCOM
    clear(); r = cal_do(CAL_VCOM_UP, false); s = cmds();
    CHECK(!r && !strcmp(s, "C5:00,60,80"), "first VCOM press sends C5 00 60 80 only (got %s)", s);
    CHECK(strstr(said, "VCOM reg 0x60 = -0.500 V") != NULL, "described as -0.500 V: %s", said);
    clear(); cal_do(CAL_VCOM_UP, false); CHECK(!strcmp(cmds(), "C5:00,64,80"), "VCOM + by touch steps 4 (got %s)", seq);
    clear(); cal_do(CAL_VCOM_UP, true);  CHECK(!strcmp(cmds(), "C5:00,65,80"), "VCOM + by key steps 1 (got %s)", seq);
    clear(); cal_do(CAL_VCOM_DN, true);  CHECK(!strcmp(cmds(), "C5:00,64,80"), "VCOM - by key steps 1 (got %s)", seq);
    for (int i = 0; i < 40; i++) cal_do(CAL_VCOM_UP, false);
    clear(); cal_do(CAL_VCOM_UP, true); CHECK(!strcmp(cmds(), "C5:00,80,80"), "VCOM stops at 0x80 (got %s)", seq);
    CHECK(strstr(said, "= 0.000 V") != NULL, "0x80 is 0.000 V: %s", said);
    cal_do(CAL_VCOM_DN, true);
    CHECK(strstr(said, "0x7F = -0.016 V") != NULL, "0x7F is -0.016 V: %s", said);
    for (int i = 0; i < 40; i++) cal_do(CAL_VCOM_DN, false);
    clear(); cal_do(CAL_VCOM_DN, true); CHECK(!strcmp(cmds(), "C5:00,00,80"), "VCOM stops at 0x00 (got %s)", seq);
    CHECK(strstr(said, "0x00 = -2.000 V") != NULL, "0x00 is -2.000 V: %s", said);
    cal_do(CAL_VCOM_UP, true);
    CHECK(strstr(said, "0x01 = -1.984 V") != NULL, "0x01 is -1.984 V: %s", said);
    clear(); cal_draw_info();
    CHECK(strstr(last_info[1], "WARM 0/4  VCOM 01 -1.984V  VRH CHIP") == last_info[1], "info line shows VCOM: [%s]", last_info[1]);

    // ---------------------------------------------------------------- VRH (with VCOM set, both are sent)
    clear(); r = cal_do(CAL_VRH_UP, false); s = cmds();
    CHECK(!r && !strcmp(s, "C0:0E,0E C5:00,01,80"), "first VRH press sends C0 0E 0E (got %s)", s);
    CHECK(strstr(said, "VRH 0x0E = 4.438 V") != NULL, "0x0E is 4.438 V: %s", said);
    for (int i = 0; i < 40; i++) cal_do(CAL_VRH_UP, false);
    CHECK(lcd_panel()->vrh == 0x1F && strstr(said, "VRH 0x1F = 5.500 V"), "VRH stops at 0x1F, 5.500 V: %s", said);
    for (int i = 0; i < 40; i++) cal_do(CAL_VRH_DN, false);
    clear(); cal_do(CAL_VRH_DN, true); s = cmds();
    CHECK(lcd_panel()->vrh == 0x01 && strstr(s, "C0:01,01"), "VRH stops at 0x01, never 0 (halt) (got %s)", s);
    CHECK(strstr(said, "VRH 0x01 = 3.625 V") != NULL, "0x01 is 3.625 V: %s", said);

    // ---------------------------------------------------------------- gamma presets
    const char *want[4] = {NULL,
        "08 20 20 08 10 08 40 88 40 08 10 08 20 20 08",   // MID
        "00 01 02 01 03 03 28 96 57 0C 1C 0E 3D 3E 0F",   // RISING
        "0F 3E 3D 0E 1C 0C 57 69 28 03 03 01 02 01 00"};  // FALLING
    const char *name[4] = {"CHIP", "MID", "RISING", "FALLING"};
    for (int g = 1; g < 4; g++) {
        clear(); r = cal_do(CAL_GAMMA, false); decode();
        CHECK(!r, "%s: no reset", name[g]);
        CHECK(bytes_are(find(0xE0), want[g]) && bytes_are(find(0xE1), want[g]), "%s: E0 and E1 bytes", name[g]);
        CHECK(strstr(said, name[g]) != NULL, "%s named in the log: %s", name[g], said);
    }
    printf("after FALLING: %s\n", said);
    // back round to CHIP: gamma off needs a reset, and VCOM and VRH must come back after it
    clear(); r = cal_do(CAL_GAMMA, false); s = cmds();
    CHECK(r, "gamma back to the chip's own resets the screen");
    CHECK(!strcmp(s, "R 01 11 3A:55 36:28 C0:01,01 C5:00,01,80 20 13 29"), "reset then VCOM and VRH again, no gamma (got %s)", s);
    CHECK(strstr(said, "gamma chip") && strstr(said, "screen reset"), "log says so: %s", said);

    // ---------------------------------------------------------------- VAL and FIELD
    clear(); cal_do(CAL_VAL_UP, false); decode();
    CHECK(lcd_panel()->gamma_set && bytes_are(find(0xE0), want[1]), "VAL with gamma off starts from MID");
    clear(); cal_do(CAL_VAL_UP, false); decode();
    CHECK(lcd_panel()->gamma[6] == 72, "VAL + by touch on VP20 steps 8 (got %d)", lcd_panel()->gamma[6]);
    CHECK(bytes_are(find(0xE0), "08 20 20 08 10 08 48 88 40 08 10 08 20 20 08"), "VP20 is the 7th byte");
    cal_do(CAL_VAL_UP, true); CHECK(lcd_panel()->gamma[6] == 73, "VAL + by key steps 1");
    for (int i = 0; i < 40; i++) cal_do(CAL_VAL_UP, false);
    CHECK(lcd_panel()->gamma[6] == 127, "VP20 stops at 127 (got %d)", lcd_panel()->gamma[6]);
    clear(); cal_draw_info();
    CHECK(strstr(last_info[1], "GAMMA MID* VP20=127/127") != NULL, "info shows the edit: [%s]", last_info[1]);
    // VP27 (low nibble) and VP36 (high nibble) of byte 8
    cal_field_step(+1); CHECK(strstr(said, "VP27") != NULL, "field after VP20 is VP27: %s", said);
    for (int i = 0; i < 20; i++) cal_do(CAL_VAL_UP, false);
    clear(); cal_do(CAL_VAL_UP, false); decode();
    CHECK(lcd_panel()->gamma[7] == 15 && find(0xE0) && find(0xE0)->p[7] == 0x8F, "VP27 fills the low nibble only (byte 8 = %02X)", find(0xE0) ? find(0xE0)->p[7] : 0);
    cal_do(CAL_FIELD, false); CHECK(strstr(said, "VP36") != NULL, "FIELD steps to VP36: %s", said);
    for (int i = 0; i < 20; i++) cal_do(CAL_VAL_DN, false);
    clear(); cal_do(CAL_VAL_DN, false); decode();
    CHECK(find(0xE0) && find(0xE0)->p[7] == 0x0F, "VP36 at 0 empties the high nibble (byte 8 = %02X)", find(0xE0) ? find(0xE0)->p[7] : 0);
    // the 4-bit ends: VP0 and VP63 never set bit 4
    cal_field_step(-8); CHECK(strstr(said, "VP0 ") != NULL, "back to VP0: %s", said);
    for (int i = 0; i < 40; i++) cal_do(CAL_VAL_UP, false);
    cal_field_step(-1); CHECK(strstr(said, "VP63") != NULL, "wraps to VP63: %s", said);
    for (int i = 0; i < 40; i++) cal_do(CAL_VAL_UP, false);
    clear(); cal_do(CAL_VAL_UP, true); decode();
    CHECK(find(0xE0) && find(0xE0)->p[0] == 0x0F && find(0xE0)->p[14] == 0x0F, "VP0 and VP63 top out at 0x0F (%02X, %02X)", find(0xE0)->p[0], find(0xE0)->p[14]);
    // every field at its top: each byte inside its width
    for (int f = 0; f < 16; f++) { cal_field_step(+1); for (int i = 0; i < 130; i++) cal_do(CAL_VAL_UP, true); }
    clear(); cal_do(CAL_VAL_UP, true); decode();
    CHECK(bytes_are(find(0xE0), "0F 3F 3F 0F 1F 0F 7F FF 7F 0F 1F 0F 3F 3F 0F"), "every field at its top fits its bits");

    // ---------------------------------------------------------------- resetup and speed
    clear(); lcd_resetup(); s = cmds();
    CHECK(!strncmp(s, "11 3A:55 36:28 C0:01,01 C5:00,01,80 E0:", 39) && strstr(s, " E1:") && strstr(s, " 20 13 29") && !strchr(s, 'R'),
          "resetup sends the panel again, no reset (got %s)", s);
    CHECK(lcd_set_hz(30000000) == 30000000 && lcd_hz() == 30000000, "speed to 30 MHz");
    clear(); cal_draw_info(); CHECK(strstr(last_info[0], "SPI 30 MHZ") != NULL, "title follows the speed: [%s]", last_info[0]);

    // ---------------------------------------------------------------- START from everything changed
    cal_do(CAL_WARM_UP, false); cal_do(CAL_WARM_UP, false); cal_do(CAL_WARM_UP, false); // to 3
    clear(); r = cal_do(CAL_START, false); s = cmds();
    snprintf(want_init, sizeof want_init, "R 01 11 3A:55 36:28 E0:%s E1:%s 20 13 29", "0F,3E,3D,0E,1C,0C,57,69,28,03,03,01,02,01,00", "0F,3E,3D,0E,1C,0C,57,69,28,03,03,01,02,01,00");
    CHECK(r && !strcmp(s, want_init), "START resets (VCOM and VRH come off) and sends FALLING (got %s)", s);
    p = lcd_panel();
    CHECK(!p->vcom_set && !p->vrh_set && p->gamma_set && !memcmp(p, &CAL_START_PANEL, sizeof *p), "START is exactly the start-up panel");
    CHECK(tone_step() == 2, "START puts the warmth back to 2 (%d)", tone_step());
    CHECK(strstr(said, "back to the start") && strstr(said, "warmth 2 of 4") && strstr(said, "gamma FALLING") && strstr(said, "screen reset"), "logged: %s", said);
    clear(); cal_draw_info();
    CHECK(strstr(last_info[1], "WARM 2/4  VCOM NV  VRH CHIP  GAMMA FALLING VP") == last_info[1], "info: FALLING, no * (edits forgotten): [%s]", last_info[1]);
    // START again: nothing to undo, so no reset, FALLING sent again
    clear(); r = cal_do(CAL_START, true); s = cmds();
    snprintf(want_init, sizeof want_init, "E0:%s E1:%s", "0F,3E,3D,0E,1C,0C,57,69,28,03,03,01,02,01,00", "0F,3E,3D,0E,1C,0C,57,69,28,03,03,01,02,01,00");
    CHECK(r && !strcmp(s, want_init), "START from the start: no reset, FALLING again, redraw (got %s)", s);
    // GAMMA from the start goes on to CHIP (FALLING is the last preset), then MID
    clear(); r = cal_do(CAL_GAMMA, false); s = cmds();
    CHECK(r && s[0] == 'R' && !lcd_panel()->gamma_set, "GAMMA after FALLING: the chip's own, with a reset (got %s)", s);
    clear(); cal_do(CAL_GAMMA, false); decode();
    CHECK(bytes_are(find(0xE0), want[1]), "then MID");
    clear(); r = cal_do(CAL_START, false); s = cmds();
    CHECK(r && !strncmp(s, "E0:0F,3E", 8) && !strchr(s, 'R'), "START from MID: no reset needed (got %s)", s);
    // VRH set, START: VRH comes off, so a reset
    cal_do(CAL_VRH_UP, false);
    clear(); r = cal_do(CAL_START, false); s = cmds();
    CHECK(r && s[0] == 'R' && !strstr(s, "C0"), "START with VRH set: reset, no C0 (got %s)", s);

    // ---------------------------------------------------------------- CHIP
    cal_do(CAL_VCOM_UP, false);
    clear(); r = cal_do(CAL_CHIP, false); s = cmds();
    CHECK(r && !strcmp(s, "R 01 11 3A:55 36:28 20 13 29"), "CHIP resets and sends no panel settings (got %s)", s);
    p = lcd_panel();
    CHECK(!p->vcom_set && !p->vrh_set && !p->gamma_set && tone_step() == 0, "CHIP clears everything, warmth too");
    CHECK(strstr(said, "warmth 0 (none) | VCOM from NV memory (chip) | VRH chip (0x0E) | gamma chip") != NULL, "described as the chip's own: %s", said);
    clear(); r = cal_do(CAL_CHIP, false); CHECK(r && !strcmp(cmds(), ""), "CHIP with nothing set: no reset, only a redraw (got %s)", seq);
    clear(); r = cal_do(CAL_LAND, false); CHECK(!r && !strcmp(cmds(), ""), "LAND is main.c's job: cal_do does nothing");
    // the longest settings line still fits
    for (int i = 0; i < 9; i++) cal_do(CAL_WARM_UP, false);
    cal_do(CAL_VCOM_UP, false); cal_do(CAL_VCOM_DN, true); cal_do(CAL_VRH_UP, false);
    cal_do(CAL_GAMMA, false); cal_do(CAL_GAMMA, false); cal_do(CAL_GAMMA, false); cal_do(CAL_VAL_UP, false);
    clear(); text_bad = 0; cal_draw(); decode();
    printf("longest: [%s]\n", last_info[1]);
    CHECK(text_bad == 0 && strstr(last_info[1], "GAMMA FALLING* VP") && last_info[1][79] == ' ', "a long settings line fits in 80: [%s]", last_info[1]);
    char d[256]; cal_describe(d, sizeof d);
    printf("describe (%zu chars): %s\n", strlen(d), d);
    CHECK(strlen(d) < 255 && strstr(d, "gamma FALLING edited"), "the log line is whole (%zu)", strlen(d));

    // ---------------------------------------------------------------- whole run
    CHECK(!seen[0xC1] && !seen[0xC2] && !seen[0xC3] && !seen[0xC4] && !seen[0xE2] && !seen[0xE3], "never C1 C2 C3 C4 E2 E3");
    static const int ok[] = {0x01, 0x11, 0x13, 0x20, 0x29, 0x2A, 0x2B, 0x2C, 0x36, 0x3A, 0xC0, 0xC5, 0xE0, 0xE1};
    int other = 0;
    for (int c = 0; c < 256; c++) if (seen[c]) { bool k = false; for (size_t i = 0; i < sizeof ok / sizeof ok[0]; i++) if (ok[i] == c) k = true; if (!k) { other++; printf("  unexpected command %02X\n", c); } }
    CHECK(other == 0, "only the expected commands");
    CHECK(bad_cs == 0, "every word sent with the screen selected at 16 bits (%d not)", bad_cs);
    CHECK(win_bad == 0 && px_bad == 0, "every window on screen and every pixel count right (%d, %d)", win_bad, px_bad);
    CHECK(text_bad == 0, "all text in the font and on screen (%d)", text_bad);

    printf("%d passed, %d failed\n", pass, fail);
    return fail != 0;
}
