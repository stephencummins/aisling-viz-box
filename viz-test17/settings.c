// (test 12) The settings file. See settings.h.
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "ff.h"
#include "tone.h"
#include "settings.h"

const char *const SET_PALETTES[SET_N_PALETTES] = {"heather", "atlantic", "kells", "evening"};
const char *const SET_GROUNDS[SET_N_GROUNDS] = {"vellum", "night", "mist"};
const char *const SET_PADS[SET_N_PADS] = {"Filter", "Wet", "Energy", "Chorus"};
const uint8_t SET_SPEEDS_MHZ[SET_N_SPEEDS] = {15, 20, 30};

#define VCM_MAX  0x80   // lcd.h: 0x81-0xFE are inhibited
#define VRH_MIN  0x01
#define VRH_MAX  0x1F
#define LINE_MAX 160    // a longer line is one the box can't use

// ---------------------------------------------------------------- reading

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; }
static bool space(int c) { return c == ' ' || c == '\t' || c == '\r' || c == '\v' || c == '\f'; }

static bool same_word(const char *a, const char *b) {
    for (; *a || *b; a++, b++)
        if (lower((unsigned char)*a) != lower((unsigned char)*b)) return false;
    return true;
}

static int index_of(const char *v, const char *const *names, int n) {
    for (int i = 0; i < n; i++)
        if (same_word(v, names[i])) return i;
    return -1;
}

// A number at *p, decimal or hex after 0x, no more than max. *p moves past it and the spaces or commas after it.
static bool number(const char **p, unsigned max, unsigned *out) {
    const char *s = *p;
    unsigned base = 10, v = 0;
    int digits = 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) base = 16, s += 2;
    for (;; s++, digits++) {
        int c = lower((unsigned char)*s), d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (base == 16 && c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else break;
        v = v * base + (unsigned)d;
        if (v > max) return false;   // max is at most 255, so v never wraps
    }
    if (!digits || (*s && !space((unsigned char)*s) && *s != ',')) return false;
    while (space((unsigned char)*s) || *s == ',') s++;
    *p = s;
    *out = v;
    return true;
}

static bool one_number(const char *v, unsigned max, unsigned *out) { return number(&v, max, out) && !*v; }

// One setting from its name and value into s. Returns its SET_ bit, 0 if the name isn't a setting's, or -1 if the
// value isn't one the setting can take (*why says what it should be). s changes only for a good value.
static int one(const char *name, const char *v, settings_t *s, const char **why) {
    unsigned n;
    int i;
    if (same_word(name, "warmth")) {
        *why = "should be 0 to 4";
        if (!one_number(v, TONE_STEPS - 1, &n)) return -1;
        s->warmth = (uint8_t)n;
        return SET_WARMTH;
    }
    if (same_word(name, "screen_spi")) {
        *why = "should be 15, 20 or 30";
        if (!one_number(v, 255, &n)) return -1;
        for (i = 0; i < SET_N_SPEEDS && SET_SPEEDS_MHZ[i] != n; i++) {}
        if (i == SET_N_SPEEDS) return -1;
        s->spi = (uint8_t)i;
        return SET_SPI;
    }
    if (same_word(name, "palette")) {
        *why = "should be heather, atlantic, kells or evening";
        if ((i = index_of(v, SET_PALETTES, SET_N_PALETTES)) < 0) return -1;
        s->palette = (uint8_t)i;
        return SET_PALETTE;
    }
    if (same_word(name, "ground")) {
        *why = "should be vellum, night or mist";
        if ((i = index_of(v, SET_GROUNDS, SET_N_GROUNDS)) < 0) return -1;
        s->ground = (uint8_t)i;
        return SET_GROUND;
    }
    if (same_word(name, "vcom")) {
        *why = "should be chip or 0x00 to 0x80";
        if (same_word(v, "chip")) s->panel.vcom_set = false;
        else if (one_number(v, VCM_MAX, &n)) s->panel.vcom_set = true, s->panel.vcm = (uint8_t)n;
        else return -1;
        return SET_VCOM;
    }
    if (same_word(name, "vrh")) {
        *why = "should be chip or 0x01 to 0x1F";
        if (same_word(v, "chip")) s->panel.vrh_set = false;
        else if (one_number(v, VRH_MAX, &n) && n >= VRH_MIN) s->panel.vrh_set = true, s->panel.vrh = (uint8_t)n;
        else return -1;
        return SET_VRH;
    }
    if (same_word(name, "gamma")) {
        *why = "should be chip or 16 numbers, each in range";
        if (same_word(v, "chip")) {
            s->panel.gamma_set = false;
            return SET_GAMMA;
        }
        uint8_t g[PANEL_GAMMA_FIELDS];
        for (i = 0; i < PANEL_GAMMA_FIELDS; i++) {
            if (!number(&v, (1u << PANEL_GAMMA_BITS[i]) - 1, &n)) return -1;
            g[i] = (uint8_t)n;
        }
        if (*v) return -1;   // more than 16
        s->panel.gamma_set = true;
        memcpy(s->panel.gamma, g, sizeof g);
        return SET_GAMMA;
    }
    for (int a = 0; a < 2; a++) {
        if (!same_word(name, a ? "pad_up" : "pad_across")) continue;
        *why = "should be Filter, Wet, Energy or Chorus";
        if ((i = index_of(v, SET_PADS, SET_N_PADS)) < 0) return -1;
        s->pad[a] = (uint8_t)i;
        return a ? SET_PAD_UP : SET_PAD_ACROSS;
    }
    return 0;
}

static void problem(settings_report_t *r, int line, const char *name, const char *what) {
    if (r->first_line) return;
    r->first_line = line;
    // At most 16 of a name and 46 of a reason (the longest here): 63 characters, all of first
    snprintf(r->first, sizeof r->first, "%.16s%s%.46s", name ? name : "", name ? " " : "", what);
}

void settings_parse(const char *text, size_t len, settings_t *s, settings_report_t *r) {
    memset(r, 0, sizeof *r);
    const uint8_t pad_was[2] = {s->pad[0], s->pad[1]};
    int pad_line = 0;
    size_t i = 0;
    if (len >= 3 && !memcmp(text, "\xEF\xBB\xBF", 3)) i = 3;   // a UTF-8 mark some editors start a file with
    for (int ln = 1; i < len; ln++) {
        size_t a = i, b;
        while (i < len && text[i] != '\n') i++;
        b = i;
        if (i < len) i++;
        for (size_t h = a; h < b; h++)
            if (text[h] == '#') b = h;   // a note: the rest of the line
        while (a < b && space((unsigned char)text[a])) a++;
        while (b > a && space((unsigned char)text[b - 1])) b--;
        if (a == b) continue;
        if (b - a >= LINE_MAX) {
            r->bad++;
            problem(r, ln, NULL, "is too long");
            continue;
        }
        char line[LINE_MAX];
        memcpy(line, text + a, b - a);
        line[b - a] = 0;
        char *eq = strchr(line, '=');
        if (!eq) {
            r->bad++;
            problem(r, ln, NULL, "has no \"=\"");
            continue;
        }
        char *ne = eq, *v = eq + 1;
        while (ne > line && space((unsigned char)ne[-1])) ne--;
        *ne = 0;
        while (space((unsigned char)*v)) v++;
        const char *why = "";
        int got = one(line, v, s, &why);
        if (got > 0) {
            r->got |= (unsigned)got;
            if (got & (SET_PAD_ACROSS | SET_PAD_UP)) pad_line = ln;
        } else if (got < 0) {
            r->bad++;
            problem(r, ln, line, why);
        } else {
            r->unknown++;
            problem(r, ln, line, "isn't a setting");
        }
    }
    // The pad's two knobs must differ: if the file left them the same, it has both as they were.
    if ((r->got & (SET_PAD_ACROSS | SET_PAD_UP)) && s->pad[0] == s->pad[1]) {
        s->pad[0] = pad_was[0], s->pad[1] = pad_was[1];
        r->got &= ~(unsigned)(SET_PAD_ACROSS | SET_PAD_UP);
        r->bad++;
        problem(r, pad_line, "pad_across", "and pad_up are the same knob");
    }
}

// ---------------------------------------------------------------- writing

typedef struct { char *out; size_t cap, n; bool fit; } text_t;

static void put(text_t *t, const char *fmt, ...) {
    if (!t->fit) return;
    va_list ap;
    va_start(ap, fmt);
    int k = vsnprintf(t->out + t->n, t->cap - t->n, fmt, ap);
    va_end(ap);
    if (k < 0 || (size_t)k >= t->cap - t->n) t->fit = false;
    else t->n += (size_t)k;
}

size_t settings_format(const settings_t *s, const char *gamma_name, char *out, size_t cap) {
    if (!cap) return 0;
    text_t t = {out, cap, 0, true};
    const panel_t *p = &s->panel;
    put(&t, "# AISLING visualiser box: its settings.\n"
            "#\n"
            "# The box reads this file when it starts, and when it gets the card back from a computer. A couple of\n"
            "# seconds after a setting changes on the box, it writes the whole file again, so notes added here are\n"
            "# lost: change only the values. A line the box can't use is left out (that setting stays as it was) and\n"
            "# its log says which line. Words can be in any case; a number can be decimal or 0x hex.\n"
            "\n");
    put(&t, "# How warm the picture is: 0 (colours as sent) to 4 (the warmest).\nwarmth = %u\n\n", s->warmth);
    put(&t, "# The screen's SPI clock, in MHz: 15, 20 or 30.\nscreen_spi = %u\n\n", SET_SPEEDS_MHZ[s->spi]);
    put(&t, "# The landscape's colours: heather, atlantic, kells or evening.\npalette = %s\n\n",
        SET_PALETTES[s->palette]);
    put(&t, "# Its ground: vellum, night or mist.\nground = %s\n\n", SET_GROUNDS[s->ground]);
    put(&t, "# The panel, as the calibration page sets it. chip leaves a setting at the chip's own value.\n"
            "# vcom: VCOM's register, 0x00 to 0x80 (VCOM = -2 + vcom / 64 volts).\n");
    if (p->vcom_set) {
        int mv = -2000 + (p->vcm * 15625 + 500) / 1000;
        put(&t, "vcom = 0x%02X   # %s%d.%03d V\n", p->vcm, mv < 0 ? "-" : "", (mv < 0 ? -mv : mv) / 1000,
            (mv < 0 ? -mv : mv) % 1000);
    } else {
        put(&t, "vcom = chip\n");
    }
    put(&t, "# vrh: 0x01 to 0x1F (VREG1OUT = 3.625 + (vrh - 1) / 16 volts).\n");
    if (p->vrh_set) {
        int tmv = 36250 + (p->vrh - 1) * 625;   // tenths of a millivolt
        put(&t, "vrh = 0x%02X   # %d.%04d V\n", p->vrh, tmv / 10000, tmv % 10000);
    } else {
        put(&t, "vrh = chip\n");
    }
    put(&t, "# gamma: 16 numbers, VP0 VP1 VP2 VP4 VP6 VP13 VP20 VP27 VP36 VP43 VP50 VP57 VP59 VP61 VP62 VP63,\n"
            "# each no more than");
    for (int i = 0; i < PANEL_GAMMA_FIELDS; i++) put(&t, " %u", (1u << PANEL_GAMMA_BITS[i]) - 1);
    put(&t, ".\n");
    if (p->gamma_set) {
        put(&t, "gamma =");
        for (int i = 0; i < PANEL_GAMMA_FIELDS; i++) put(&t, " %u", p->gamma[i]);
        if (gamma_name && *gamma_name) put(&t, "   # %s", gamma_name);
        put(&t, "\n\n");
    } else {
        put(&t, "gamma = chip\n\n");
    }
    put(&t, "# The X/Y page's pad: the knob it turns across, and the one it turns up. Filter, Wet, Energy or\n"
            "# Chorus, a different one each.\npad_across = %s\npad_up = %s\n",
        SET_PADS[s->pad[0]], SET_PADS[s->pad[1]]);
    if (!t.fit) {
        out[0] = 0;
        return 0;
    }
    return t.n;
}

bool settings_same(const settings_t *a, const settings_t *b) {
    if (a->warmth != b->warmth || a->spi != b->spi || a->palette != b->palette || a->ground != b->ground ||
        a->pad[0] != b->pad[0] || a->pad[1] != b->pad[1])
        return false;
    const panel_t *p = &a->panel, *q = &b->panel;
    if (p->vcom_set != q->vcom_set || (p->vcom_set && p->vcm != q->vcm)) return false;
    if (p->vrh_set != q->vrh_set || (p->vrh_set && p->vrh != q->vrh)) return false;
    if (p->gamma_set != q->gamma_set || (p->gamma_set && memcmp(p->gamma, q->gamma, sizeof p->gamma))) return false;
    return true;
}

// ---------------------------------------------------------------- the file

static char buf[SETTINGS_MAX + 1];   // the file's text, read or to write
static FIL file;                     // FF_FS_TINY: no buffer of its own
static const char *err = "none";

const char *settings_fr_name(int fr) {
    static const char *const NAME[] = {
        "ok", "disk error", "FatFs internal error", "card not ready", "no file", "no path", "bad name",
        "denied (full, or read-only)", "exists", "bad object", "write-protected", "bad drive", "not mounted",
        "no FAT filesystem", "mkfs aborted", "timeout", "locked", "out of memory", "too many open files",
        "bad parameter"};
    return fr >= 0 && fr < (int)(sizeof NAME / sizeof NAME[0]) ? NAME[fr] : "FatFs error";
}

static void fail(FRESULT fr) { err = settings_fr_name((int)fr); }

int settings_load(settings_t *s, settings_report_t *r) {
    memset(r, 0, sizeof *r);
    FRESULT fr = f_open(&file, SETTINGS_FILE, FA_READ);
    if (fr == FR_NO_FILE) {
        err = "no " SETTINGS_FILE;
        return 0;
    }
    if (fr != FR_OK) {
        fail(fr);
        return -1;
    }
    FSIZE_t size = f_size(&file);
    UINT got = 0;
    fr = f_read(&file, buf, SETTINGS_MAX, &got);
    f_close(&file);
    if (fr != FR_OK) {
        fail(fr);
        return -1;
    }
    size_t len = got;
    bool cut = size > SETTINGS_MAX;
    if (cut)
        while (len && buf[len - 1] != '\n') len--;   // not half a line
    settings_parse(buf, len, s, r);
    r->cut = cut;
    err = "none";
    return 1;
}

bool settings_save(const settings_t *s, const char *gamma_name) {
    size_t len = settings_format(s, gamma_name, buf, sizeof buf);
    if (!len) {
        err = "too long to write";
        return false;
    }
    // Written over the old file in place: its entry and its clusters kept, the new text over the old, and what is
    // left of a longer old one cut off after. Nothing is deleted or renamed, so a long name a computer gave the
    // file stays with it; and nothing is emptied first, so a card pulled out (or the power lost) part-way leaves
    // the old text, the new, or some sectors of each, never an empty file. (Each line of a mixture is still read
    // on its own, and one cut across the join is a bad line, left alone.)
    FRESULT fr = f_open(&file, SETTINGS_FILE, FA_WRITE | FA_OPEN_ALWAYS);
    if (fr != FR_OK) {
        fail(fr);
        return false;
    }
    UINT put_n = 0;
    fr = f_write(&file, buf, (UINT)len, &put_n);
    if (fr == FR_OK && put_n == len && f_size(&file) > len) fr = f_truncate(&file);
    FRESULT fc = f_close(&file);   // which syncs: the directory entry and the FAT written
    if (fr == FR_OK && put_n != len) fr = FR_DENIED;   // the card is full
    if (fr == FR_OK) fr = fc;
    if (fr != FR_OK) {
        fail(fr);
        return false;
    }
    err = "none";
    return true;
}

const char *settings_error(void) { return err; }
