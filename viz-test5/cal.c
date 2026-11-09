// Test 5's calibration screen (test 4's, unchanged). See cal.h.
//
// From the top: two lines of settings; a 64-step grey ramp with a marker under the grey level the chosen gamma field
// sets; red, green and blue ramps; swatches of the colours test 2 couldn't show (the heather palette, the vellum
// ground, the orange, the strip's greys); a copy of the strip and status line; then the buttons. The text and the
// buttons are white on black, which reads whatever the tone curve is doing. Everything here goes through the warmth,
// as the landscape does, so the ramps and swatches show what the landscape gets.
#include <stdio.h>
#include <string.h>
#include "lcd.h"
#include "tone.h"
#include "cal.h"

#define BTN_W (LCD_W / 5)
#define BTN_H 32

#define INFO_Y    1
#define GREY_Y   22
#define GREY_H   48
#define MARK_Y   71
#define MARK_H    8
#define TICK_Y   81
#define RAMP_H   22
#define R_Y      92
#define G_Y     116
#define B_Y     140
#define SW_Y    166
#define SW_H     30
#define SWL_Y   198
#define SAMPLE_Y 212
#define SAMPLE_H 36

static const char *BTN_NAME[CAL_BUTTONS] = {"WARM -", "WARM +", "VRH -", "VRH +", "START",
                                            "GAMMA", "FIELD", "VAL -", "VAL +", "LAND"};

// The gamma starting curves. The datasheet gives E0h/E1h's fields but leaves their power-on values, and the chapter
// on how they shape the curve, as "TBD". So these are built from the fields alone: every field at the middle of its
// range (MID), or at its grey level's share of its range (RISING), or the mirror of that (FALLING). Whichever looks
// closest is the one to tune field by field.
enum { G_CHIP, G_MID, G_RISING, G_FALLING, G_PRESETS };
static const char *G_NAME[G_PRESETS] = {"CHIP", "MID", "RISING", "FALLING"};
static int preset = G_FALLING;  // test 3's pick, and what lcd_init sends at start-up
static bool edited;             // a field stepped since the preset was chosen
static int field = 6;   // VP20, a dark-middle grey
static int pressed = -1;

static void preset_fill(int g, uint8_t *out) {
    for (int i = 0; i < PANEL_GAMMA_FIELDS; i++) {
        int max = (1 << PANEL_GAMMA_BITS[i]) - 1, lv = PANEL_GAMMA_LEVEL[i];
        int rising = (lv * max + 31) / 63;
        int v = g == G_MID ? (max + 1) / 2 : g == G_RISING ? rising : g == G_FALLING ? max - rising : 0;
        out[i] = (uint8_t)v;
    }
}

// Test 3's pick (Stephen, 2026-11-09): FALLING gave an even grey ramp and dark darks, where the chip's own gamma
// came up differently from one start to the next. E0h/E1h = 0F 3E 3D 0E 1C 0C 57 69 28 03 03 01 02 01 00.
const panel_t CAL_START_PANEL = {
    .gamma_set = true,
    .gamma = {15, 62, 61, 14, 28, 12, 87, 9, 6, 40, 3, 3, 1, 2, 1, 0}, // VP0 ... VP63, as preset_fill(G_FALLING)
};

// ---------------------------------------------------------------- numbers

// VCOM = -2 + vcm / 64 V, in millivolts (exact: 1/64 V = 15.625 mV, so to the nearest).
static int vcom_mv(int vcm) { return -2000 + (vcm * 15625 + 500) / 1000; }

// VREG1OUT = 3.625 + (vrh - 1) * 0.0625 V, in tenths of a millivolt.
static int vrh_tmv(int vrh) { return 36250 + (vrh - 1) * 625; }

static void fmt_volts_mv(char *out, size_t n, int mv) {
    int a = mv < 0 ? -mv : mv;
    snprintf(out, n, "%s%d.%03d", mv < 0 ? "-" : "", a / 1000, a % 1000);
}

static void fmt_volts_tmv(char *out, size_t n, int tmv) {
    snprintf(out, n, "%d.%03d", tmv / 10000, (tmv % 10000 + 5) / 10);
}

static int field_max(int f) { return (1 << PANEL_GAMMA_BITS[f]) - 1; }

void cal_describe(char *out, size_t n) {
    const panel_t *p = lcd_panel();
    char v[16], a[48], b[40], c[96], w[48];
    int ws = tone_step();
    if (ws) snprintf(w, sizeof w, "warmth %d of %d (G x%d/256, B x%d/256)", ws, TONE_STEPS - 1, tone_g, tone_b);
    else snprintf(w, sizeof w, "warmth 0 (none)");
    if (p->vcom_set) {
        fmt_volts_mv(v, sizeof v, vcom_mv(p->vcm));
        snprintf(a, sizeof a, "VCOM reg 0x%02X = %s V", p->vcm, v);
    } else {
        snprintf(a, sizeof a, "VCOM from NV memory (chip)");
    }
    if (p->vrh_set) {
        fmt_volts_tmv(v, sizeof v, vrh_tmv(p->vrh));
        snprintf(b, sizeof b, "VRH 0x%02X = %s V", p->vrh, v);
    } else {
        snprintf(b, sizeof b, "VRH chip (0x0E)");
    }
    if (p->gamma_set) {
        uint8_t g[15];
        lcd_gamma_bytes(p->gamma, g);
        int k = snprintf(c, sizeof c, "gamma %s%s, E0/E1 =", G_NAME[preset], edited ? " edited" : "");
        for (int i = 0; i < 15 && k > 0 && k < (int)sizeof c; i++) k += snprintf(c + k, sizeof c - (size_t)k, " %02X", g[i]);
    } else {
        snprintf(c, sizeof c, "gamma chip");
    }
    snprintf(out, n, "%s | %s | %s | %s", w, a, b, c);
}

// ---------------------------------------------------------------- drawing

static uint16_t rgb24(uint32_t c) { return RGB((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF); }

static int x64(int i) { return i * LCD_W / 64; }

// Grey level i of 64: green carries 6 bits, red and blue 5.
static uint16_t grey64(int i) { return (uint16_t)(((i >> 1) << 11) | (i << 5) | (i >> 1)); }

static void text_centred(int x, int w, int y, const char *s, int scale, uint16_t fg, uint16_t bg) {
    int tw = (int)strlen(s) * 6 * scale;
    lcd_text(x + (w - tw + 1) / 2, y, s, scale, fg, bg);
}

static void draw_button(int b, bool down) {
    int x = (b % 5) * BTN_W, y = CAL_BTN_Y + (b / 5) * BTN_H;
    uint16_t bg = down ? C_WHITE : C_BLACK, fg = down ? C_BLACK : C_WHITE;
    lcd_fill_rect(x, y, BTN_W, BTN_H, C_WHITE);
    lcd_fill_rect(x + 2, y + 2, BTN_W - 4, BTN_H - 4, bg);
    text_centred(x, BTN_W, y + (BTN_H - 16) / 2, BTN_NAME[b], 2, fg, bg);
}

static void draw_marker(void) {
    lcd_fill_rect(0, MARK_Y, LCD_W, MARK_H, C_BLACK);
    for (int i = 0; i < PANEL_GAMMA_FIELDS; i++) {
        int lv = PANEL_GAMMA_LEVEL[i], x = x64(lv), w = x64(lv + 1) - x;
        if (i == field) lcd_fill_rect(x, MARK_Y, w, MARK_H, C_WHITE);
        else lcd_fill_rect(x + w / 2 - 1, MARK_Y, 2, 3, C_WHITE);
    }
}

void cal_draw_info(void) {
    char line[192], s[32];
    snprintf(s, sizeof s, "SPI %lu MHZ", (unsigned long)(lcd_hz() / 1000000));
    snprintf(line, sizeof line, "AISLING VIZ TEST 5: PANEL CALIBRATION%*s", LCD_W / 6 - 37, s);
    lcd_text(0, INFO_Y, line, 1, C_WHITE, C_BLACK);

    const panel_t *p = lcd_panel();
    char v[16], a[40], b[40], c[80];
    if (p->vcom_set) {
        fmt_volts_mv(v, sizeof v, vcom_mv(p->vcm));
        snprintf(a, sizeof a, "VCOM %02X %sV", p->vcm, v);
    } else {
        snprintf(a, sizeof a, "VCOM NV");
    }
    if (p->vrh_set) {
        fmt_volts_tmv(v, sizeof v, vrh_tmv(p->vrh));
        snprintf(b, sizeof b, "VRH %02X %sV", p->vrh, v);
    } else {
        snprintf(b, sizeof b, "VRH CHIP");
    }
    int vp = PANEL_GAMMA_LEVEL[field];
    if (p->gamma_set)
        snprintf(c, sizeof c, "GAMMA %s%s VP%d=%d/%d", G_NAME[preset], edited ? "*" : "", vp, p->gamma[field],
                 field_max(field));
    else
        snprintf(c, sizeof c, "GAMMA CHIP (FIELD VP%d)", vp);
    int k = snprintf(line, sizeof line, "WARM %d/%d  %s  %s  %s", tone_step(), TONE_STEPS - 1, a, b, c);
    int room = LCD_W / 6;
    if (k > room) k = room;
    if (k < room) memset(line + k, ' ', (size_t)(room - k));
    line[room] = 0;
    lcd_text(0, INFO_Y + 10, line, 1, C_WHITE, C_BLACK);
    draw_marker();
}

// The colours test 2's photos showed going wrong.
static const uint32_t SWATCH[] = {
    0x1d2230, 0x3e2a22, 0x6a2a3c, 0x9a4a63,            // heather: drone (bog)
    0x2e3b20, 0x536a2a, 0x7f8a35, 0xb0ab55,            // sequence (fields)
    0x7a3a1c, 0xb8622a, 0xd99b5a, 0xe8cfa0,            // lead (sky)
    0x262a4a, 0x4a4272, 0x7a7898, 0xa7a6b0,            // sequence 2 (hills)
    0xf5edda, 0xff5f1f, 0x1a161e, 0x28222e, 0x968c7d, 0x6e6e6e, // vellum ground, orange, strip, strip, BPM, grey
};
#define N_SWATCH ((int)(sizeof SWATCH / sizeof SWATCH[0]))
static const char *SWATCH_LABEL[6] = {"GND", "ORG", "S1", "S2", "BPM", "GRY"};

static void draw_sample_cell(int x, const char *name, const char *bpm, uint16_t bg, uint16_t fg, uint16_t fg2) {
    lcd_fill_rect(x, SAMPLE_Y, 60, SAMPLE_H, bg);
    text_centred(x, 60, SAMPLE_Y + 8, name, 1, fg, bg);
    text_centred(x, 60, SAMPLE_Y + 21, bpm, 1, fg2, bg);
}

void cal_draw(void) {
    lcd_fill_rect(0, 0, LCD_W, LCD_H, C_BLACK);
    cal_draw_info();

    for (int i = 0; i < 64; i++) lcd_fill_rect(x64(i), GREY_Y, x64(i + 1) - x64(i), GREY_H, grey64(i));
    static const int TICKS[5] = {0, 16, 32, 48, 63};
    for (int i = 0; i < 5; i++) {
        char t[4];
        snprintf(t, sizeof t, "%d", TICKS[i]);
        int x = x64(TICKS[i]);
        if (i == 4) x = LCD_W - 12;
        lcd_text(x, TICK_Y, t, 1, C_WHITE, C_BLACK);
    }
    for (int i = 0; i < 32; i++) {
        lcd_fill_rect(i * 15, R_Y, 15, RAMP_H, (uint16_t)(i << 11));
        lcd_fill_rect(i * 15, B_Y, 15, RAMP_H, (uint16_t)i);
    }
    for (int i = 0; i < 64; i++) lcd_fill_rect(x64(i), G_Y, x64(i + 1) - x64(i), RAMP_H, (uint16_t)(i << 5));

    for (int i = 0; i < N_SWATCH; i++) {
        int x = i * LCD_W / N_SWATCH, w = (i + 1) * LCD_W / N_SWATCH - x - 2;
        lcd_fill_rect(x, SW_Y, w, SW_H, rgb24(SWATCH[i]));
    }
    static const char *GROUP[4] = {"DRONE", "SEQ", "LEAD", "SEQ 2"};
    for (int g = 0; g < 4; g++) lcd_text(g * 4 * LCD_W / N_SWATCH, SWL_Y, GROUP[g], 1, C_WHITE, C_BLACK);
    for (int i = 0; i < 6; i++) lcd_text((16 + i) * LCD_W / N_SWATCH, SWL_Y, SWATCH_LABEL[i], 1, C_WHITE, C_BLACK);

    // The strip and status line as test 2 draws them.
    draw_sample_cell(0, "SKELLIG", "76 BPM", RGB(26, 22, 30), C_VELLUM, RGB(150, 140, 125));
    draw_sample_cell(60, "BURREN", "84 BPM", C_ORANGE, C_BLACK, C_BLACK);
    draw_sample_cell(120, "ARAN", "96 BPM", RGB(40, 34, 46), C_VELLUM, RGB(150, 140, 125));
    lcd_text(190, SAMPLE_Y + 6, "FM-1 OK", 1, C_GREEN, C_BLACK);
    lcd_text(238, SAMPLE_Y + 6, " 96 BPM  WIND 0.312", 1, C_GREY, C_BLACK);
    lcd_text(190, SAMPLE_Y + 22, "VELLUM TEXT ON BLACK", 1, C_VELLUM, C_BLACK);

    for (int b = 0; b < CAL_BUTTONS; b++) draw_button(b, b == pressed);
}

// ---------------------------------------------------------------- buttons

int cal_button_at(int x, int y) {
    if (y < CAL_BTN_Y || x < 0) return -1;
    int r = (y - CAL_BTN_Y) / BTN_H, c = x / BTN_W;
    if (r > 1) r = 1;
    if (c > 4) c = 4;
    return r * 5 + c;
}

void cal_press_show(int b) {
    if (b == pressed) return;
    if (pressed >= 0) draw_button(pressed, false);
    pressed = b;
    if (b >= 0) draw_button(b, true);
}

static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

bool cal_field_step(int d) {
    field = (field + d + PANEL_GAMMA_FIELDS) % PANEL_GAMMA_FIELDS;
    const panel_t *p = lcd_panel();
    say("gamma field VP%d (grey level %d of 63, %d bits)%s%d", PANEL_GAMMA_LEVEL[field], PANEL_GAMMA_LEVEL[field],
        PANEL_GAMMA_BITS[field], p->gamma_set ? ", now " : ", gamma not sent; MID would make it ",
        p->gamma_set ? p->gamma[field] : (field_max(field) + 1) / 2);
    return false;
}

bool cal_do(int b, bool fine) {
    panel_t p = *lcd_panel();
    switch (b) {
    case CAL_WARM_DN:
    case CAL_WARM_UP: {
        int was = tone_step();
        tone_set(was + (b == CAL_WARM_UP ? 1 : -1));
        char d[256];
        cal_describe(d, sizeof d);
        say("panel: %s%s", d, tone_step() == was ? " (at the end of the warmth steps)" : "");
        return tone_step() != was;
    }
    case CAL_START: {
        tone_set(TONE_START);
        preset = G_FALLING;
        edited = false;
        bool reset = lcd_panel_set(&CAL_START_PANEL);
        char d[256];
        cal_describe(d, sizeof d);
        say("panel: back to the start: %s%s", d, reset ? " (screen reset to undo a setting)" : "");
        return true;
    }
    case CAL_VCOM_DN:
    case CAL_VCOM_UP:
        if (!p.vcom_set) {
            p.vcom_set = true;
            p.vcm = 0x60; // the register's power-on value, -0.5 V, as the place to start
        } else {
            int s = fine ? 1 : 4;
            p.vcm = (uint8_t)clamp(p.vcm + (b == CAL_VCOM_UP ? s : -s), 0, 0x80);
        }
        break;
    case CAL_VRH_DN:
    case CAL_VRH_UP:
        if (!p.vrh_set) {
            p.vrh_set = true;
            p.vrh = 0x0E; // the power-on value
        } else {
            p.vrh = (uint8_t)clamp(p.vrh + (b == CAL_VRH_UP ? 1 : -1), 1, 0x1F);
        }
        break;
    case CAL_CHIP:
        p.vcom_set = p.vrh_set = p.gamma_set = false;
        preset = G_CHIP;
        edited = false;
        tone_set(0);
        break;
    case CAL_GAMMA:
        preset = (preset + 1) % G_PRESETS;
        edited = false;
        p.gamma_set = preset != G_CHIP;
        preset_fill(preset, p.gamma);
        break;
    case CAL_FIELD:
        return cal_field_step(+1);
    case CAL_VAL_DN:
    case CAL_VAL_UP:
        if (!p.gamma_set) {
            preset = G_MID;
            edited = false;
            p.gamma_set = true;
            preset_fill(G_MID, p.gamma);
        } else {
            int max = field_max(field), s = fine ? 1 : (max + 1) / 16;
            if (s < 1) s = 1;
            p.gamma[field] = (uint8_t)clamp(p.gamma[field] + (b == CAL_VAL_UP ? s : -s), 0, max);
            edited = true;
        }
        break;
    default:
        return false;
    }
    bool reset = lcd_panel_set(&p);
    char d[256];
    cal_describe(d, sizeof d);
    say("panel: %s%s", d, reset ? " (screen reset to undo a setting)" : "");
    return reset || b == CAL_CHIP; // CHIP also takes the warmth off, which every colour on screen shows
}
