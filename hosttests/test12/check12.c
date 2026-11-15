// Test 12's host checks: sd.c, settings.c, card.c, msc.c and the block of main.c that decides when the settings are
// read and written, against model.c's card (the SD card's SPI mode, byte by byte) and FatFs as the box builds it.
//
//   check12 sd                  the card's start-up for every kind of card, reads, writes, the ways it fails
//   check12 units               SETTINGS.TXT's text: written and read back, and the lines the box can't use
//   check12 names               settings.c's names, for run.sh to compare with land.c's, perf.c's and main.c's
//   check12 fs IMG OUT TXT      a FAT image as the card: no file, written, read, written over; the card as it ends
//                               goes to OUT, and the text SETTINGS.TXT must then hold to TXT
//   check12 read IMG            the card's SETTINGS.TXT as the box reads it (one put there by a computer)
//   check12 notfat IMG          a card that isn't FAT
//   check12 full IMG OUT        a card with no room left
//   check12 pull IMG            the card pulled out (or the power gone) at every stage of a save
//   check12 msc IMG OTHER FAT   the card as a USB drive: lent, read, written, ejected, unplugged, taken back; the
//                               card's page and its buttons. OTHER: an image that isn't FAT, FAT: one to "format" it
//   check12 main IMG            main.c's block: the file wins, a change saved after two seconds, nothing while lent
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sdk_stub.h"
#include "tusb.h"
#include "ff.h"
#include "lcd.h"
#include "sd.h"
#include "tone.h"
#include "settings.h"
#include "card.h"
#include "msc.h"
#include "perf.h"
#include "model.h"

static int fails, checks;
#define OK(c, ...) \
    do { \
        checks++; \
        if (!(c)) { \
            fails++; \
            printf("FAIL %d: %s: ", __LINE__, #c); \
            printf(__VA_ARGS__); \
            printf("\n"); \
        } \
    } while (0)

// The bus as the screen needs it, and nothing done that a real card wouldn't stand.
#define CLEAN(what) \
    do { \
        OK(m_idle(), "%s: the bus isn't the screen's again", what); \
        OK(m_errors == 0, "%s: %s", what, m_err); \
    } while (0)

static int sense[3], senses;
bool tud_msc_set_sense(uint8_t lun, uint8_t key, uint8_t asc, uint8_t ascq) {
    (void)lun;
    sense[0] = key, sense[1] = asc, sense[2] = ascq;
    senses++;
    return true;
}

static card_cfg_t hc(uint32_t sectors) {
    return (card_cfg_t){.present = true, .ver = 2, .hc = true, .csd = 1, .bl = 9, .sectors = sectors,
                        .init_ms = 40, .busy_us = 1500, .name = "AISLG"};
}

static void board(void) {
    static bool up;
    if (up) return;
    up = true;
    panel_t p;
    memset(&p, 0, sizeof p);
    lcd_init(&p);
    m_no_pullup_is_error(false);
}

static void fill(uint8_t *b, uint32_t n, uint32_t seed) {
    for (uint32_t i = 0; i < n; i++) seed = seed * 1664525u + 1013904223u, b[i] = (uint8_t)(seed >> 24);
}

// ---------------------------------------------------------------- sd

static void sd_one(const char *what, card_cfg_t c) {
    static uint8_t a[8 * 512], b[8 * 512];
    m_wipe();
    m_insert(c);
    OK(sd_init(), "%s: %s", what, sd_error());
    OK(sd_ok(), "%s", what);
    OK(sd_sectors() == c.sectors, "%s: %lu sectors, the card has %lu", what, (unsigned long)sd_sectors(),
       (unsigned long)c.sectors);
    OK(!strcmp(sd_name(), c.name), "%s: named \"%s\"", what, sd_name());
    CLEAN(what);
    uint32_t at[] = {0, 1, 7, 2047, 2048, c.sectors / 2 + 3, c.sectors - 8, c.sectors - 1};
    for (unsigned i = 0; i < sizeof at / sizeof at[0]; i++) {
        uint32_t n = at[i] + 8 <= c.sectors && i % 2 == 0 ? 8 : 1;
        fill(a, n * 512, at[i] + 1);
        OK(sd_write(at[i], a, n), "%s: write %lu x%lu: %s", what, (unsigned long)at[i], (unsigned long)n, sd_error());
        for (uint32_t k = 0; k < n; k++)   // where it landed: the sector asked for, however the card is addressed
            OK(!memcmp(m_sector(at[i] + k), a + k * 512, 512), "%s: sector %lu isn't what was written", what,
               (unsigned long)(at[i] + k));
        memset(b, 0xEE, sizeof b);
        OK(sd_read(at[i], b, n), "%s: read %lu x%lu: %s", what, (unsigned long)at[i], (unsigned long)n, sd_error());
        OK(!memcmp(a, b, n * 512), "%s: read %lu isn't what was written", what, (unsigned long)at[i]);
        CLEAN(what);
    }
    OK(sd_sync(), "%s: sync", what);
    uint64_t was = m_bytes;
    OK(!sd_read(c.sectors, b, 1) && !sd_write(c.sectors - 1, a, 2) && !sd_read(0, b, 0), "%s: past the end", what);
    OK(m_bytes == was, "%s: a sector past the end was asked of the card", what);
    OK(sd_ok(), "%s: asking past the end lost the card", what);
    CLEAN(what);
}

static void sd_bad(const char *what, card_cfg_t c, const char *says) {
    m_insert(c);
    OK(!sd_init(), "%s: started", what);
    OK(!sd_ok(), "%s", what);
    OK(!strcmp(sd_error(), says), "%s: said \"%s\", not \"%s\"", what, sd_error(), says);
    uint8_t b[512];
    uint64_t was = m_bytes;
    OK(!sd_read(0, b, 1) && !sd_write(0, b, 1), "%s: read or written all the same", what);
    OK(m_bytes == was, "%s: a card that didn't start was read or written", what);
    CLEAN(what);
}

static void mode_sd(void) {
    board();
    m_no_pullup_is_error(true);
    OK(sd_crc7((const uint8_t *)"\x40\x00\x00\x00\x00", 5) == 0x95, "CRC7 of CMD0");          // the spec's examples
    OK(sd_crc7((const uint8_t *)"\x51\x00\x00\x00\x00", 5) == 0x55, "CRC7 of CMD17");
    OK(sd_crc7((const uint8_t *)"\x48\x00\x00\x01\xAA", 5) == 0x87, "CRC7 of CMD8");

    card_cfg_t c = hc(15523840);   // an 8 GB card
    sd_one("SDHC 8 GB", c);
    c = hc(62333952);
    sd_one("SDHC 32 GB", c);
    c = hc(250347520);
    sd_one("SDXC 128 GB", c);
    c = hc(4194304), c.hc = false, c.csd = 0, c.bl = 10;
    sd_one("SD 2 GB, version 2, addressed in bytes", c);
    c = hc(2097152), c.hc = false, c.csd = 0, c.bl = 9;
    sd_one("SD 1 GB, version 2, addressed in bytes", c);
    c = hc(131072), c.hc = false, c.ver = 1, c.csd = 0, c.bl = 9;
    sd_one("SD 64 MB, version 1", c);
    c = hc(15523840), c.init_ms = 900;
    sd_one("a card slow to start (0.9 s)", c);
    c = hc(15523840), c.busy_us = 200000;
    sd_one("a card slow to write (0.2 s a block)", c);

    c = hc(15523840), c.present = false;
    sd_bad("no card", c, "no card");
    c = hc(15523840), c.mmc = true;
    sd_bad("an MMC", c, "not an SD card");
    c = hc(15523840), c.bad_voltage = true;
    sd_bad("a card that can't take 3.3 V", c, "the card can't take 3.3 V");
    c = hc(15523840), c.init_ms = UINT32_MAX;
    uint32_t t0 = m_now_ms();
    sd_bad("a card that never starts", c, "the card didn't finish starting");
    OK(m_now_ms() - t0 < 2500, "a card that never starts held the box %lu ms", (unsigned long)(m_now_ms() - t0));
    c = hc(15523840), c.csd = 2;
    sd_bad("an SDUC card", c, "a card of a size this doesn't know (CSD)");

    // Busy for ever after a write: the write fails, in a bounded time, and the card is given up
    uint8_t b[8 * 512];
    c = hc(15523840), c.busy_us = UINT32_MAX;
    m_insert(c);
    OK(sd_init(), "busy card: %s", sd_error());
    t0 = m_now_ms();
    fill(b, 512, 9);
    bool w = sd_write(100, b, 1) && sd_write(101, b, 1) && sd_sync();
    OK(!w, "a card busy for ever: written and synced");
    OK(!sd_ok(), "a card busy for ever: still thought good");
    OK(m_now_ms() - t0 < 3000, "a card busy for ever held the box %lu ms", (unsigned long)(m_now_ms() - t0));
    CLEAN("a card busy for ever");

    // Pulled out part-way through a read and a write, at every byte of each
    for (int wr = 0; wr < 2; wr++) {
        c = hc(15523840);
        m_insert(c);
        uint64_t s0 = m_bytes;
        OK(sd_init(), "pull: %s", sd_error());
        uint64_t start = m_bytes - s0;
        uint64_t one = m_bytes;
        OK(wr ? sd_write(40, b, 8) : sd_read(40, b, 8), "pull: the whole one");
        one = m_bytes - one;
        int lost = 0;
        for (uint64_t k = 1; k < one; k += k < 40 ? 1 : 97) {
            c.pull_at = start + k;
            m_insert(c);
            if (!sd_init()) { OK(0, "pull at %llu: didn't start", (unsigned long long)k); break; }
            bool good = wr ? sd_write(40, b, 8) && sd_sync() : sd_read(40, b, 8);
            if (!good) {
                lost++;
                OK(!sd_ok(), "pulled at byte %llu of a %s: still thought good", (unsigned long long)k,
                   wr ? "write" : "read");
            }
            CLEAN("a card pulled");
        }
        OK(lost > 20, "pulled during a %s: only %d of them failed", wr ? "write" : "read", lost);
    }
    // And put back: found again
    m_insert(hc(15523840));
    OK(sd_init() && sd_ok(), "put back: %s", sd_error());
    OK(!strcmp(sd_error(), "none"), "put back: says \"%s\"", sd_error());

    // No pull-ups on the card's lines is a board fault the model can see
    printf("sd: %d checks, %llu bytes clocked on the card's pins\n", checks, (unsigned long long)m_bytes);
}

// ---------------------------------------------------------------- units

static const unsigned ALL = SET_WARMTH | SET_SPI | SET_PALETTE | SET_GROUND | SET_VCOM | SET_VRH | SET_GAMMA |
                            SET_PAD_ACROSS | SET_PAD_UP;

static settings_t defaults(void) {
    settings_t s;
    memset(&s, 0, sizeof s);
    s.warmth = 2, s.spi = 1, s.pad[0] = 0, s.pad[1] = 1;
    return s;
}

static settings_t other(void) {   // every setting different from defaults()
    settings_t s;
    memset(&s, 0, sizeof s);
    s.warmth = 4, s.spi = 2, s.palette = 3, s.ground = 1, s.pad[0] = 3, s.pad[1] = 2;
    s.panel.vcom_set = true, s.panel.vcm = 0x5A;
    s.panel.vrh_set = true, s.panel.vrh = 0x11;
    s.panel.gamma_set = true;
    for (int i = 0; i < PANEL_GAMMA_FIELDS; i++) s.panel.gamma[i] = (uint8_t)((1u << PANEL_GAMMA_BITS[i]) - 1 - (unsigned)i % 3);
    return s;
}

// The text parsed from memory exactly its length long, so reading past it is caught.
static void parse(const char *text, settings_t *s, settings_report_t *r) {
    size_t n = strlen(text);
    char *m = malloc(n ? n : 1);
    memcpy(m, text, n);
    settings_parse(m, n, s, r);
    free(m);
}

static void bad_line(const char *line, int bad, int unknown, const char *first) {
    settings_t s = other(), was = s;
    settings_report_t r;
    char text[600];
    snprintf(text, sizeof text, "warmth = 3\n%s\nground = mist\n", line);
    parse(text, &s, &r);
    was.warmth = 3, was.ground = 2;
    OK(settings_same(&s, &was), "\"%.40s\" changed a setting", line);
    OK(r.bad == bad && r.unknown == unknown, "\"%.40s\": %d bad, %d unknown", line, r.bad, r.unknown);
    OK(r.got == (SET_WARMTH | SET_GROUND), "\"%.40s\": got %x", line, r.got);
    OK(r.first_line == 2, "\"%.40s\": first bad line %d", line, r.first_line);
    OK(!strcmp(r.first, first), "\"%.40s\" said \"%s\", not \"%s\"", line, r.first, first);
}

static void good_text(const char *text, unsigned got, settings_t want) {
    settings_t s = defaults();
    settings_report_t r;
    parse(text, &s, &r);
    OK(r.got == got && !r.bad && !r.unknown, "\"%.50s\": got %x, %d bad, %d unknown (%s)", text, r.got, r.bad,
       r.unknown, r.first);
    OK(settings_same(&s, &want), "\"%.50s\": not the settings it gives", text);
}

static void mode_units(void) {
    static char text[SETTINGS_MAX + 1], t2[SETTINGS_MAX + 1];
    settings_report_t r;
    size_t longest = 0;
    int n = 0;
    uint32_t seed = 12;
    // Every value of every setting, written and read back over settings that differ in all of them
    for (int w = 0; w < TONE_STEPS; w++)
        for (int sp = 0; sp < SET_N_SPEEDS; sp++)
            for (int pa = 0; pa < SET_N_PALETTES; pa++)
                for (int g = 0; g < SET_N_GROUNDS; g++)
                    for (int ax = 0; ax < SET_N_PADS; ax++)
                        for (int up = 0; up < SET_N_PADS; up++) {
                            if (ax == up) continue;
                            settings_t s;
                            memset(&s, 0, sizeof s);
                            s.warmth = (uint8_t)w, s.spi = (uint8_t)sp, s.palette = (uint8_t)pa, s.ground = (uint8_t)g;
                            s.pad[0] = (uint8_t)ax, s.pad[1] = (uint8_t)up;
                            seed = seed * 1664525u + 1013904223u;
                            s.panel.vcom_set = seed >> 31, s.panel.vcm = (uint8_t)((seed >> 8) % 0x81);
                            s.panel.vrh_set = (seed >> 30) & 1, s.panel.vrh = (uint8_t)(1 + (seed >> 16) % 0x1F);
                            s.panel.gamma_set = (seed >> 29) & 1;
                            for (int i = 0; i < PANEL_GAMMA_FIELDS; i++) {
                                seed = seed * 1664525u + 1013904223u;
                                s.panel.gamma[i] = (uint8_t)((seed >> 20) & ((1u << PANEL_GAMMA_BITS[i]) - 1));
                            }
                            size_t len = settings_format(&s, n % 2 ? "warm, edited" : NULL, text, sizeof text);
                            if (len > longest) longest = len;
                            settings_t back = n % 3 ? other() : defaults();
                            parse(text, &back, &r);
                            if (!len || strlen(text) != len || r.got != ALL || r.bad || r.unknown || r.first_line ||
                                !settings_same(&s, &back)) {
                                OK(0, "written and read back: len %zu, got %x, %d bad, %d unknown (%s)", len, r.got,
                                   r.bad, r.unknown, r.first);
                                w = sp = pa = g = ax = up = 99;
                            }
                            n++;
                        }
    OK(n == TONE_STEPS * SET_N_SPEEDS * SET_N_PALETTES * SET_N_GROUNDS * 12, "%d written and read back", n);
    OK(longest < SETTINGS_MAX - 512, "the longest file is %zu bytes of the %d read", longest, SETTINGS_MAX);

    // The ends of each range
    settings_t s = defaults();
    s.panel.vcom_set = s.panel.vrh_set = true;
    s.panel.vcm = 0x80, s.panel.vrh = 0x1F;
    good_text("vcom = 0x80\nvrh = 31\n", SET_VCOM | SET_VRH, s);
    s.panel.vcm = 0, s.panel.vrh = 1;
    good_text("vcom = 0\nvrh = 0x01\n", SET_VCOM | SET_VRH, s);
    OK(settings_format(&s, NULL, text, sizeof text) && strstr(text, "vcom = 0x00   # -2.000 V\n"), "VCOM 0 in volts");
    OK(strstr(text, "vrh = 0x01   # 3.6250 V\n") != NULL, "VRH 1 in volts");
    s.panel.vcm = 0x80, s.panel.vrh = 0x0E;
    OK(settings_format(&s, NULL, text, sizeof text) && strstr(text, "vcom = 0x80   # 0.000 V\n"), "VCOM 0x80 in volts");
    OK(strstr(text, "vrh = 0x0E   # 4.4375 V\n") != NULL, "VRH 0x0E in volts");
    s.panel.vcm = 0x60;
    OK(settings_format(&s, NULL, text, sizeof text) && strstr(text, "vcom = 0x60   # -0.500 V\n"), "VCOM 0x60 in volts");

    // What a person's editor does to it
    s = defaults(), s.warmth = 4, s.palette = 2;
    good_text("\xEF\xBB\xBFwarmth = 4\r\npalette = kells\r\n", SET_WARMTH | SET_PALETTE, s);
    good_text("WARMTH=4\n\n\n   Palette\t=\tKELLS   # my favourite\n# warmth = 0\n", SET_WARMTH | SET_PALETTE, s);
    good_text("warmth = 0x4\npalette = kells", SET_WARMTH | SET_PALETTE, s);   // no newline at the end
    good_text("warmth = 1\nwarmth = 4\npalette = kells\n", SET_WARMTH | SET_PALETTE, s);   // the last one wins
    good_text("", 0, defaults());
    good_text("\n\n# nothing but notes\n", 0, defaults());
    s = defaults(), s.pad[0] = 1, s.pad[1] = 0;
    good_text("pad_across = wet\npad_up = FILTER\n", SET_PAD_ACROSS | SET_PAD_UP, s);   // a swap is a swap
    good_text("pad_up = filter\npad_across = wet\n", SET_PAD_ACROSS | SET_PAD_UP, s);
    s = defaults(), s.pad[1] = 3;
    good_text("pad_up = chorus\n", SET_PAD_UP, s);
    s = defaults(), s.panel.gamma_set = true;
    for (int i = 0; i < PANEL_GAMMA_FIELDS; i++) s.panel.gamma[i] = (uint8_t)i;
    good_text("gamma = 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15\n", SET_GAMMA, s);
    good_text("gamma = 0,1,2,3, 4, 5 ,6,7,0x8,9,10,11,12,13,14,15\n", SET_GAMMA, s);
    s = defaults();
    good_text("gamma = chip\nvcom = CHIP\nvrh = chip\n", SET_GAMMA | SET_VCOM | SET_VRH, s);

    // Lines the box can't use: nothing changes, and it says which
    bad_line("warmth = 5", 1, 0, "warmth should be 0 to 4");
    bad_line("warmth = -1", 1, 0, "warmth should be 0 to 4");
    bad_line("warmth = 2 3", 1, 0, "warmth should be 0 to 4");
    bad_line("warmth =", 1, 0, "warmth should be 0 to 4");
    bad_line("warmth = 99999999999999999999", 1, 0, "warmth should be 0 to 4");
    bad_line("warmth = two", 1, 0, "warmth should be 0 to 4");
    bad_line("screen_spi = 25", 1, 0, "screen_spi should be 15, 20 or 30");
    bad_line("screen_spi = 62", 1, 0, "screen_spi should be 15, 20 or 30");
    bad_line("palette = pink", 1, 0, "palette should be heather, atlantic, kells or evening");
    bad_line("ground = vellum night", 1, 0, "ground should be vellum, night or mist");
    bad_line("vcom = 0x81", 1, 0, "vcom should be chip or 0x00 to 0x80");
    bad_line("vcom = 0xFF", 1, 0, "vcom should be chip or 0x00 to 0x80");
    bad_line("vrh = 0", 1, 0, "vrh should be chip or 0x01 to 0x1F");
    bad_line("vrh = 0x20", 1, 0, "vrh should be chip or 0x01 to 0x1F");
    bad_line("gamma = 1 2 3", 1, 0, "gamma should be chip or 16 numbers, each in range");
    bad_line("gamma = 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16", 1, 0, "gamma should be chip or 16 numbers, each in range");
    bad_line("gamma = 16 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15", 1, 0, "gamma should be chip or 16 numbers, each in range");
    bad_line("pad_up = Reverb", 1, 0, "pad_up should be Filter, Wet, Energy or Chorus");
    bad_line("warmth 4", 1, 0, "has no \"=\"");
    bad_line("volume = 11", 0, 1, "volume isn't a setting");
    bad_line("= 4", 0, 1, " isn't a setting");
    bad_line("a_name_much_longer_than_any_setting_has = 4", 0, 1, "a_name_much_long isn't a setting");
    char longl[400];
    memset(longl, 'x', sizeof longl);
    memcpy(longl, "warmth = 1", 10);
    longl[300] = 0;
    bad_line(longl, 1, 0, "is too long");
    longl[9] = ' ', longl[159 + 0] = 0;   // 159 characters: the longest line read
    bad_line(longl, 1, 0, "warmth should be 0 to 4");

    // The pad's two knobs the same: both stay as they were
    s = defaults();
    parse("pad_across = Wet\nwarmth = 0\n", &s, &r);   // Wet across, and Wet was up already
    OK(s.pad[0] == 0 && s.pad[1] == 1 && s.warmth == 0, "the same knob twice: the pad changed");
    OK(r.bad == 1 && r.got == SET_WARMTH && r.first_line == 1 &&
       !strcmp(r.first, "pad_across and pad_up are the same knob"), "the same knob twice: %d bad, got %x, line %d, \"%s\"",
       r.bad, r.got, r.first_line, r.first);
    s = defaults();
    parse("pad_across = Energy\npad_up = energy\n", &s, &r);
    OK(s.pad[0] == 0 && s.pad[1] == 1 && r.bad == 1 && r.first_line == 2, "the same knob twice in the file");

    // Too small a buffer: nothing, never half a file; and one byte more than it needs is enough
    s = other();
    size_t len = settings_format(&s, "warm", text, sizeof text);
    for (size_t cap = 0; cap <= len; cap++) {
        char *m = malloc(cap ? cap : 1);
        size_t k = settings_format(&s, "warm", m, cap);
        if (k || (cap && m[0])) { OK(0, "%zu bytes written into room for %zu", k, cap); cap = len; }
        free(m);
    }
    char *m = malloc(len + 1);
    OK(settings_format(&s, "warm", m, len + 1) == len && !strcmp(m, text), "room for exactly the file");
    free(m);

    // Any bytes at all: nothing read past the end, nothing out of its range
    for (int i = 0; i < 20000; i++) {
        seed = seed * 1664525u + 1013904223u;
        size_t l2 = settings_format(&s, "warm", t2, sizeof t2);
        int hits = 1 + (int)(seed >> 28);
        for (int h = 0; h < hits; h++) {
            seed = seed * 1664525u + 1013904223u;
            t2[(seed >> 8) % l2] = (char)(seed >> 24);
        }
        seed = seed * 1664525u + 1013904223u;
        size_t cut = i % 4 ? l2 : (seed >> 8) % (l2 + 1);
        char *q = malloc(cut ? cut : 1);
        memcpy(q, t2, cut);
        settings_t x = defaults();
        settings_parse(q, cut, &x, &r);
        free(q);
        bool in = x.warmth < TONE_STEPS && x.spi < SET_N_SPEEDS && x.palette < SET_N_PALETTES &&
                  x.ground < SET_N_GROUNDS && x.pad[0] < SET_N_PADS && x.pad[1] < SET_N_PADS && x.pad[0] != x.pad[1] &&
                  (!x.panel.vcom_set || x.panel.vcm <= 0x80) &&
                  (!x.panel.vrh_set || (x.panel.vrh >= 1 && x.panel.vrh <= 0x1F));
        for (int g = 0; in && x.panel.gamma_set && g < PANEL_GAMMA_FIELDS; g++)
            in = x.panel.gamma[g] < (1u << PANEL_GAMMA_BITS[g]);
        if (!in || strlen(r.first) >= sizeof r.first) { OK(0, "a damaged file gave a setting out of its range"); break; }
        // and what it gave can be written again
        if (!settings_format(&x, NULL, t2, sizeof t2)) { OK(0, "a damaged file's settings couldn't be written"); break; }
    }
    checks += 20000;
    OK(!strcmp(settings_fr_name(FR_NO_FILESYSTEM), "no FAT filesystem") && !strcmp(settings_fr_name(FR_DENIED), "denied (full, or read-only)") &&
       !strcmp(settings_fr_name(FR_INVALID_PARAMETER), "bad parameter") && !strcmp(settings_fr_name(99), "FatFs error") &&
       !strcmp(settings_fr_name(-1), "FatFs error"), "FatFs's results in words");
    printf("units: %d checks; the longest file %zu bytes\n", checks, longest);
}

static void mode_names(void) {
    for (int i = 0; i < SET_N_PALETTES; i++) printf("palette %s\n", SET_PALETTES[i]);
    for (int i = 0; i < SET_N_GROUNDS; i++) printf("ground %s\n", SET_GROUNDS[i]);
    for (int i = 0; i < SET_N_PADS; i++) printf("pad %s\n", SET_PADS[i]);
    for (int i = 0; i < SET_N_SPEEDS; i++) printf("speed %u\n", SET_SPEEDS_MHZ[i]);
    printf("steps %d\n", TONE_STEPS);
}

// ---------------------------------------------------------------- a FAT image as the card

static uint32_t img_sectors;

static void card_in(const char *img) {
    board();
    img_sectors = m_load(img);
    m_insert(hc(img_sectors));
}

static void mode_fs(const char *img, const char *out, const char *txt) {
    static char text[SETTINGS_MAX + 1];
    settings_t s, t;
    settings_report_t r;
    card_in(img);
    OK(card_start() == CARD_OK, "the card: %s (%s)", card_state_name(card_state()), sd_error());
    s = defaults();
    OK(card_load(&s, &r) == 0, "a new card: SETTINGS.TXT read");
    t = defaults();
    OK(settings_same(&s, &t), "no file changed a setting");
    OK(!strcmp(settings_error(), "no SETTINGS.TXT"), "no file: says \"%s\"", settings_error());
    OK(card_save(&s, "chip"), "first save: %s", settings_error());
    s = other();
    OK(card_load(&s, &r) == 1 && r.got == ALL && !r.bad && !r.unknown && !r.cut, "read back: got %x", r.got);
    OK(settings_same(&s, &t), "read back: not what was saved");
    CLEAN("the first save");
    // Written over, shorter and longer, many times: the card's free space must not leak (fsck, in run.sh)
    uint32_t seed = 5;
    for (int i = 0; i < 40; i++) {
        seed = seed * 1664525u + 1013904223u;
        t = i % 2 ? other() : defaults();
        t.warmth = (uint8_t)(i % TONE_STEPS), t.palette = (uint8_t)(seed >> 30);
        if (!card_save(&t, i % 3 ? "warm, edited" : "")) { OK(0, "save %d: %s", i, settings_error()); break; }
        s = i % 2 ? defaults() : other();
        if (card_load(&s, &r) != 1 || r.got != ALL || !settings_same(&s, &t)) { OK(0, "save %d read back wrong", i); break; }
    }
    CLEAN("forty saves");
    // The card taken out and put back (or the box restarted): the same file
    m_insert(hc(img_sectors));
    OK(card_start() == CARD_OK, "put back: %s", card_state_name(card_state()));
    s = defaults();
    OK(card_load(&s, &r) == 1 && settings_same(&s, &t), "put back: not the last save");
    // Taken out and put back without the box knowing (it asks nothing of the slot between saves): the next save
    // fails, the card is given up, and LOOK AGAIN finds it. (The save's commands reach a card that hasn't been
    // started, at the screen's speed: nothing the box can know, so the model's complaint about that is put aside.)
    m_insert(hc(img_sectors));
    OK(!card_save(&t, "") && card_state() == CARD_NONE, "a card swapped unseen: %s", card_state_name(card_state()));
    OK(m_idle(), "a card swapped unseen: the bus isn't the screen's again");
    m_errors = 0;
    OK(card_start() == CARD_OK, "a card swapped unseen, looked for again: %s (%s)", card_state_name(card_state()), sd_error());
    s = defaults();
    OK(card_load(&s, &r) == 1 && settings_same(&s, &t), "a card swapped unseen: not the last save");
    OK(card_save(&t, "warm, edited"), "a card swapped unseen: %s", settings_error());
    size_t len = settings_format(&t, "warm, edited", text, sizeof text);
    FILE *f = fopen(txt, "wb");
    fwrite(text, 1, len, f);
    fclose(f);
    OK(m_save(out, img_sectors), "couldn't write %s", out);
    CLEAN("fs");
    printf("fs: %d checks, %lu MB card, %lu blocks written\n", checks, (unsigned long)(img_sectors / 2048),
           (unsigned long)m_blocks_written);
}

static void mode_read(const char *img) {
    settings_t s = defaults();
    settings_report_t r;
    card_in(img);
    int st = card_start();
    int k = card_load(&s, &r);
    CLEAN("read");
    printf("%s load=%d got=%x bad=%d unknown=%d cut=%d line=%d first=\"%s\"\n", card_state_name(st), k, r.got, r.bad,
           r.unknown, r.cut, r.first_line, r.first);
    printf("warmth=%u spi=%u palette=%s ground=%s vcom=%d:%02X vrh=%d:%02X gamma=%d pad=%s,%s\n", s.warmth,
           SET_SPEEDS_MHZ[s.spi], SET_PALETTES[s.palette], SET_GROUNDS[s.ground], s.panel.vcom_set, s.panel.vcm,
           s.panel.vrh_set, s.panel.vrh, s.panel.gamma_set, SET_PADS[s.pad[0]], SET_PADS[s.pad[1]]);
}

static void mode_notfat(const char *img) {
    settings_t s = defaults();
    settings_report_t r;
    card_in(img);
    uint32_t w = m_blocks_written;
    OK(card_start() == CARD_NOT_FAT, "it's %s", card_state_name(card_state()));
    OK(sd_ok(), "a card that isn't FAT: lost");
    OK(card_load(&s, &r) == -2 && !card_save(&s, ""), "a card that isn't FAT: read or written");
    OK(m_blocks_written == w, "a card that isn't FAT was written to");
    card_draw();
    CLEAN("notfat");
    printf("notfat: %d checks\n", checks);
}

static void mode_full(const char *img, const char *out) {
    static FATFS fs;
    static FIL fil;
    static uint8_t junk[4096];
    settings_t s = other(), t;
    settings_report_t r;
    card_in(img);
    // Filled by FatFs itself, as a computer's copy would
    OK(f_mount(&fs, "", 1) == FR_OK, "mount");
    OK(f_open(&fil, "FILL.BIN", FA_WRITE | FA_CREATE_ALWAYS) == FR_OK, "FILL.BIN");
    UINT put = sizeof junk;
    uint32_t kb = 0;
    while (put == sizeof junk && f_write(&fil, junk, sizeof junk, &put) == FR_OK) kb += put / 1024;
    OK(f_close(&fil) == FR_OK, "close");
    f_mount(NULL, "", 0);
    OK(card_start() == CARD_OK, "the full card: %s", card_state_name(card_state()));
    OK(card_load(&s, &r) == 0, "no file yet");
    OK(!card_save(&s, ""), "saved on a full card");
    OK(!strcmp(settings_error(), "denied (full, or read-only)"), "full: says \"%s\"", settings_error());
    OK(card_state() == CARD_OK, "a full card isn't a card gone: %s", card_state_name(card_state()));
    card_draw();
    // Room made (the fill deleted): the save works, and the file is whole
    OK(f_unlink("FILL.BIN") == FR_OK, "unlink");
    OK(card_save(&s, ""), "after room was made: %s", settings_error());
    t = defaults();
    OK(card_load(&t, &r) == 1 && r.got == ALL && settings_same(&s, &t), "after room was made: read back wrong");
    OK(m_save(out, img_sectors), "couldn't write %s", out);
    CLEAN("full");
    printf("full: %d checks, filled with %lu KB\n", checks, (unsigned long)kb);
}

// The card gone at every stage of a save (pulled out, or the power lost): whatever it leaves, the card still mounts,
// and what the box reads from it is the old settings, the new ones, or none (never a mixture, never out of range).
static void mode_pull(const char *img) {
    settings_t old = defaults(), new = other(), s;
    settings_report_t r;
    card_in(img);
    OK(card_start() == CARD_OK && card_save(&old, ""), "the file to start with");
    static char keep[300];
    snprintf(keep, sizeof keep, "%s.pull", img);
    OK(m_save(keep, img_sectors), "keep");
    m_insert(hc(img_sectors));
    uint64_t b0 = m_bytes;
    OK(card_start() == CARD_OK, "start");
    uint64_t start = m_bytes - b0;
    b0 = m_bytes;
    OK(card_save(&new, "warm"), "the whole save");
    uint64_t one = m_bytes - b0;
    int n = 0, n_old = 0, n_new = 0, n_none = 0, n_part = 0, n_unreadable = 0, n_fail = 0;
    for (uint64_t k = 1; k < one + 600; k += 131) {
        m_load(keep);
        card_cfg_t c = hc(img_sectors);
        c.pull_at = start + k;
        m_insert(c);
        if (card_start() != CARD_OK) { OK(0, "pull %llu: didn't start", (unsigned long long)k); break; }
        bool saved = card_save(&new, "warm");
        if (!saved) {
            n_fail++;
            OK(card_state() == CARD_NONE, "pulled at byte %llu: the card is still %s", (unsigned long long)k,
               card_state_name(card_state()));
            OK(card_load(&s, &r) == -2, "pulled: read all the same");
        }
        CLEAN("pulled during a save");
        // Put back
        m_insert(hc(img_sectors));
        if (card_start() != CARD_OK) { OK(0, "pulled at byte %llu: the card no longer mounts", (unsigned long long)k); continue; }
        s = defaults(), s.warmth = 1;   // neither old nor new
        settings_t neither = s;
        int got = card_load(&s, &r);
        n++;
        if (got == 0) n_none++;
        else if (got < 0) n_unreadable++;
        else if (settings_same(&s, &new) && r.got == ALL) n_new++;
        else if (settings_same(&s, &old) && r.got == ALL) n_old++;
        else if (settings_same(&s, &neither) && r.got == 0) n_none++;   // an empty file
        else n_part++;
        if (saved) OK(got == 1 && settings_same(&s, &new), "pulled after the save said done, and it isn't there");
        // and it can be saved again
        OK(card_save(&new, "warm"), "after a pull at byte %llu: can't save again (%s)", (unsigned long long)k, settings_error());
        s = defaults();
        OK(card_load(&s, &r) == 1 && settings_same(&s, &new), "after a pull: the next save isn't read back");
    }
    remove(keep);
    OK(n_fail > 10, "only %d pulls made the save fail", n_fail);
    OK(n_unreadable == 0, "%d pulls left a file that can't be read", n_unreadable);
    printf("pull: %d checks; %d pulls (a save is %llu bytes on the wire): after, the file was the old one %d times, "
           "the new one %d, empty or gone %d, part of the new one %d\n", checks, n, (unsigned long long)one, n_old,
           n_new, n_none, n_part);
}

// ---------------------------------------------------------------- the card as a USB drive, and its page

static void copy_in(const char *img) {   // the computer writes this image over the card, through the drive
    FILE *f = fopen(img, "rb");
    static uint8_t b[4096];
    uint32_t lba = 0;
    size_t k;
    static const uint8_t zero[4096];
    while ((k = fread(b, 1, sizeof b, f)) > 0) {
        memset(b + k, 0, sizeof b - k);
        // (a computer's format writes only what it needs: here, the sectors that aren't zero, and the first)
        if (lba == 0 || memcmp(b, zero, sizeof b))
            if (tud_msc_write10_cb(0, lba, 0, b, sizeof b) != (int32_t)sizeof b) { OK(0, "format: write %lu", (unsigned long)lba); break; }
        lba += 8;
    }
    fclose(f);
}

static void mode_msc(const char *img, const char *other_img, const char *fat_img) {
    static uint8_t b[4096], a[4096];
    settings_t s = defaults(), t;
    settings_report_t r;
    uint32_t n;
    uint16_t bs;
    board();
    m_mounted = true;

    // No card at all: an empty reader
    m_insert((card_cfg_t){0});
    OK(card_start() == CARD_NONE, "no card: %s", card_state_name(card_state()));
    OK(!tud_msc_test_unit_ready_cb(0), "no card: ready");
    tud_msc_capacity_cb(0, &n, &bs);
    OK(n == 0 && bs == 512, "no card: %lu blocks", (unsigned long)n);
    OK(!card_lend() && card_state() == CARD_NONE, "no card: lent");
    OK(card_press(CARD_BTN_MAIN) == CARD_DO_NOTHING && card_press(CARD_BTN_SAVE) == CARD_DO_LOOK &&
       card_press(CARD_BTN_BACK) == CARD_DO_BACK, "no card: the buttons");
    card_open();
    card_draw();
    CLEAN("no card");

    // A card the box has: the computer still sees an empty reader
    card_in(img);
    OK(card_start() == CARD_OK && card_save(&s, ""), "the card: %s", card_state_name(card_state()));
    uint64_t was = m_bytes;
    senses = 0;
    OK(!tud_msc_test_unit_ready_cb(0) && senses == 0, "not lent: ready");
    tud_msc_capacity_cb(0, &n, &bs);
    OK(n == 0, "not lent: %lu blocks", (unsigned long)n);
    fill(a, sizeof a, 3);
    OK(tud_msc_read10_cb(0, 0, 0, b, 512) == -1 && tud_msc_write10_cb(0, 0, 0, a, 512) == -1, "not lent: read or written");
    uint8_t sync[16] = {0x35};
    OK(tud_msc_scsi_cb(0, sync, NULL, 0) == -1 && sense[0] == SCSI_SENSE_NOT_READY && sense[1] == 0x3A, "not lent: synced");
    OK(m_bytes == was && !msc_lent() && !msc_written(), "not lent: the computer reached the card");
    OK(card_press(CARD_BTN_MAIN) == CARD_DO_LEND && card_press(CARD_BTN_SAVE) == CARD_DO_SAVE &&
       card_press(CARD_BTN_BACK) == CARD_DO_BACK, "the box's card: the buttons");
    card_draw();
    CLEAN("the box's card");

    // Lent
    OK(card_lend() && card_state() == CARD_LENT && msc_lent(), "lent: %s", card_state_name(card_state()));
    OK(!card_lend(), "lent twice");
    {   // while it's lent the box has let go of it: FatFs has no volume, and a save sends the card nothing
        static FIL lent_fil;
        uint64_t was = m_bytes;
        OK(f_open(&lent_fil, "SETTINGS.TXT", FA_READ) == FR_NOT_ENABLED, "lent: FatFs still has the card");
        t = other();
        OK(!card_save(&t, "") && m_bytes == was && card_state() == CARD_LENT, "lent: saved, or the card spoken to (%s)",
           card_state_name(card_state()));
    }
    senses = 0;
    OK(!tud_msc_test_unit_ready_cb(0) && senses == 1 && sense[0] == SCSI_SENSE_UNIT_ATTENTION && sense[1] == 0x28,
       "lent: no unit attention first (%d senses, %x/%x)", senses, sense[0], sense[1]);
    OK(tud_msc_test_unit_ready_cb(0) && tud_msc_test_unit_ready_cb(0) && senses == 1, "lent: not ready after");
    tud_msc_capacity_cb(0, &n, &bs);
    OK(n == img_sectors && bs == 512, "lent: %lu blocks of %u", (unsigned long)n, bs);
    OK(tud_msc_is_writable_cb(0), "writable");
    uint8_t v[8], p[16], rev[4];
    memset(v, ' ', 8), memset(p, ' ', 16), memset(rev, ' ', 4);
    tud_msc_inquiry_cb(0, v, p, rev);
    OK(!memcmp(v, "stephen8", 8) && !memcmp(p, "AISLING card    ", 16) && !memcmp(rev, "12  ", 4), "inquiry");
    // the box keeps off it
    was = m_bytes;
    t = other();
    OK(!card_save(&t, "") && card_load(&t, &r) == -2 && card_start() == CARD_LENT, "lent: the box read or wrote it");
    OK(m_bytes == was, "lent: the box clocked the card");
    // the computer reads and writes it
    OK(tud_msc_read10_cb(0, 0, 0, b, 512) == 512 && !memcmp(b, m_sector(0), 512), "lent: sector 0");
    OK(tud_msc_read10_cb(0, 64, 0, b, 4096) == 4096 && !memcmp(b, m_sector(64), 512) && !memcmp(b + 3584, m_sector(71), 512),
       "lent: eight sectors");
    OK(!msc_written(), "reading counted as writing");
    uint32_t free_at = img_sectors - 16;
    OK(tud_msc_write10_cb(0, free_at, 0, a, 4096) == 4096 && !memcmp(a, m_sector(free_at), 512) &&
       !memcmp(a + 3584, m_sector(free_at + 7), 512), "lent: written");
    OK(msc_written() && msc_blocks_read() == 9 && msc_blocks_written() == 8, "lent: counts %lu read, %lu written",
       (unsigned long)msc_blocks_read(), (unsigned long)msc_blocks_written());
    OK(tud_msc_read10_cb(0, 0, 512, b, 512) == -1 && tud_msc_read10_cb(0, 0, 0, b, 100) == -1 &&
       tud_msc_read10_cb(0, 0, 0, b, 768) == -1 && tud_msc_write10_cb(0, free_at, 0, a, 768) == -1 &&
       tud_msc_write10_cb(0, 0, 0, a, 0) == -1 && tud_msc_read10_cb(0, img_sectors, 0, b, 512) == -1 &&
       tud_msc_write10_cb(0, img_sectors - 1, 0, a, 1024) == -1, "lent: a piece that isn't whole sectors, or past the end");
    OK(sd_ok(), "a refused piece lost the card");
    OK(tud_msc_scsi_cb(0, sync, NULL, 0) == 0, "lent: sync");
    uint8_t odd[16] = {0xA1};
    OK(tud_msc_scsi_cb(0, odd, NULL, 0) == -1 && sense[0] == SCSI_SENSE_ILLEGAL_REQUEST && sense[1] == 0x20, "an odd command");
    card_draw();
    card_draw_counts();
    CLEAN("lent");

    // The buttons while it's written to: the first tap warns, the second takes it
    OK(card_press(CARD_BTN_SAVE) == CARD_DO_NOTHING, "lent: SAVE");
    OK(card_press(CARD_BTN_MAIN) == CARD_DO_NOTHING, "written: taken back at the first tap");
    OK(card_press(CARD_BTN_BACK) == CARD_DO_NOTHING, "written: BACK at its first tap, after another button's");
    OK(card_press(CARD_BTN_MAIN) == CARD_DO_NOTHING, "written: the first tap of TAKE IT BACK again");
    OK(card_press(CARD_BTN_MAIN) == CARD_DO_TAKE_BACK, "written: the second tap");
    OK(card_state() == CARD_LENT, "card_press took it back itself");
    card_open();
    OK(card_press(CARD_BTN_BACK) == CARD_DO_NOTHING && card_press(CARD_BTN_BACK) == CARD_DO_BACK, "written: BACK's two taps");

    // Ejected on the computer: back by itself, once, and the reader is empty again
    OK(tud_msc_start_stop_cb(0, 0, true, false) && msc_came_back() == MSC_BACK_NONE && msc_lent(), "a start isn't an eject");
    OK(tud_msc_start_stop_cb(0, 0, false, false) && msc_came_back() == MSC_BACK_NONE && msc_lent(), "a stop isn't an eject");
    OK(tud_msc_start_stop_cb(0, 0, false, true), "eject");
    OK(!msc_lent() && !msc_written(), "ejected: still lent");
    OK(!tud_msc_test_unit_ready_cb(0) && tud_msc_read10_cb(0, 0, 0, b, 512) == -1 && tud_msc_write10_cb(0, 0, 0, a, 512) == -1,
       "ejected: the computer still has it");
    OK(msc_came_back() == MSC_BACK_EJECTED && msc_came_back() == MSC_BACK_NONE, "ejected: said once");
    OK(card_take_back() == CARD_OK, "back: %s", card_state_name(card_state()));
    t = other();
    OK(card_load(&t, &r) == 1 && settings_same(&t, &s), "back: the file");
    OK(card_press(CARD_BTN_MAIN) == CARD_DO_LEND, "back: the button");
    CLEAN("ejected");

    // Not written to: taken straight back
    OK(card_lend(), "lent again");
    OK(card_press(CARD_BTN_MAIN) == CARD_DO_TAKE_BACK && card_press(CARD_BTN_BACK) == CARD_DO_BACK, "not written: a warning");
    senses = 0;
    OK(!tud_msc_test_unit_ready_cb(0) && senses == 1, "lent again: no unit attention");

    // The cable gone: no computer for a second
    uint32_t now = 5000;
    msc_tick(now);
    OK(msc_came_back() == MSC_BACK_NONE, "came back with the computer there");
    m_mounted = false;
    msc_tick(now += 10);
    msc_tick(now += 900);
    OK(msc_came_back() == MSC_BACK_NONE && msc_lent(), "gone 0.9 s: back already");
    m_mounted = true;   // a port reset: configured again within the second
    msc_tick(now += 50);
    m_mounted = false;
    msc_tick(now += 10);
    msc_tick(now += 990);
    OK(msc_came_back() == MSC_BACK_NONE && msc_lent(), "gone, back, gone 0.99 s: back already");
    msc_tick(now += 10);
    OK(msc_came_back() == MSC_BACK_UNPLUGGED && !msc_lent(), "gone a second: not back");
    OK(card_take_back() == CARD_OK, "unplugged: %s", card_state_name(card_state()));
    msc_tick(now += 5000);
    OK(msc_came_back() == MSC_BACK_NONE, "not lent, no computer: came back");
    m_mounted = true;
    // the tick's clock wrapping (49 days)
    OK(card_lend(), "lent");
    m_mounted = false;
    msc_tick(0xFFFFFF00u);
    msc_tick(0x00000100u);
    OK(msc_came_back() == MSC_BACK_NONE, "0.5 s across the clock's wrap: back");
    msc_tick(0x00000300u);
    OK(msc_came_back() == MSC_BACK_UNPLUGGED, "a second across the clock's wrap: not back");
    card_take_back();
    m_mounted = true;
    // unconfigured by the computer
    OK(card_lend(), "lent");
    tud_umount_cb();
    OK(msc_came_back() == MSC_BACK_UNPLUGGED && !msc_lent(), "unconfigured: not back");
    OK(card_take_back() == CARD_OK, "unconfigured: %s", card_state_name(card_state()));
    tud_umount_cb();
    OK(msc_came_back() == MSC_BACK_NONE, "unconfigured while the box had it: came back");

    // The card pulled while the computer has it
    OK(card_lend(), "lent");
    tud_msc_test_unit_ready_cb(0);
    card_cfg_t c = hc(img_sectors);
    c.present = false;
    m_insert(c);
    OK(tud_msc_read10_cb(0, 0, 0, b, 512) == -1, "pulled: read");
    OK(!tud_msc_test_unit_ready_cb(0), "pulled: ready");
    OK(card_take_back() == CARD_NONE, "pulled: %s", card_state_name(card_state()));
    card_draw();
    CLEAN("pulled while lent");

    // A card that isn't FAT: lent all the same, formatted there, and the box's when it's back
    card_in(other_img);
    OK(card_start() == CARD_NOT_FAT, "the other card: %s", card_state_name(card_state()));
    OK(card_press(CARD_BTN_MAIN) == CARD_DO_LEND && card_press(CARD_BTN_SAVE) == CARD_DO_LOOK, "not FAT: the buttons");
    card_draw();
    OK(card_lend(), "not FAT: not lent");
    tud_msc_test_unit_ready_cb(0);
    copy_in(fat_img);
    OK(msc_written(), "formatted: not written");
    OK(tud_msc_scsi_cb(0, sync, NULL, 0) == 0 && tud_msc_start_stop_cb(0, 0, false, true), "formatted: eject");
    OK(msc_came_back() == MSC_BACK_EJECTED, "formatted: back");
    OK(card_take_back() == CARD_OK, "formatted: %s", card_state_name(card_state()));
    t = defaults();
    OK(card_load(&t, &r) == 0 && card_save(&t, "") && card_load(&t, &r) == 1 && r.got == ALL, "formatted: the file");
    card_draw();
    CLEAN("formatted on the computer");

    // The page's buttons: the whole bottom strip, three across
    OK(card_button_at(0, 263) == -1 && card_button_at(0, 264) == CARD_BTN_MAIN && card_button_at(159, 319) == CARD_BTN_MAIN &&
       card_button_at(160, 300) == CARD_BTN_SAVE && card_button_at(319, 300) == CARD_BTN_SAVE &&
       card_button_at(320, 300) == CARD_BTN_BACK && card_button_at(479, 319) == CARD_BTN_BACK &&
       card_button_at(-1, 300) == -1 && card_button_at(240, 0) == -1, "the buttons' places");
    card_press_show(1), card_press_show(1), card_press_show(2), card_press_show(-1);
    card_say("A LINE OF NEWS LONGER THAN THE SCREEN IS WIDE, SO IT BREAKS AT A SPACE AND RUNS ON TO A SECOND LINE BELOW IT");
    card_say("ANEWSLINEWITHNOSPACESATALLANDLONGERTHANTHESCREENISWIDESOITHASTOBECUTSOMEWHEREABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");
    card_say("");
    CLEAN("the page");
    printf("msc: %d checks\n", checks);
}

// ---------------------------------------------------------------- main.c's block

#ifdef MAIN_BLOCK
#include "cal.h"
enum { SCR_LAND, SCR_FM1, SCR_CAL, SCR_FX, SCR_XY, SCR_CARD };
static int screen = SCR_LAND, want_screen = -1;
#define N_SPEEDS 3
static const uint32_t SPEEDS_MHZ[N_SPEEDS] = {15, 20, 30};
static int spi_i = 1;
static perf_t pf;
static int pal_i, gnd_i, redraws, cal_loaded;
const char *land_palette(void) { return SET_PALETTES[pal_i]; }
const char *land_ground(void) { return SET_GROUNDS[gnd_i]; }
const char *land_next_palette(void) { pal_i = (pal_i + 1) % SET_N_PALETTES; return land_palette(); }
const char *land_next_ground(void) { gnd_i = (gnd_i + 1) % SET_N_GROUNDS; return land_ground(); }
void cal_settings_loaded(void) { cal_loaded++; }
const char *cal_gamma_name(void) { return "mid"; }
static void redraw_all(void) { redraws++; }
static char said[4000];
static int n_said;
void say(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    size_t k = strlen(said);
    vsnprintf(said + k, sizeof said - k, fmt, ap);
    va_end(ap);
    k = strlen(said);
    if (k + 2 < sizeof said) said[k] = '\n', said[k + 1] = 0;
    n_said++;
}
static void hush(void) { said[0] = 0; n_said = 0; }
#include MAIN_BLOCK

static uint32_t now_ms = 1000;
static void run_ms(uint32_t ms) {   // the main loop going round, every 10 ms
    for (uint32_t t = 0; t < ms; t += 10) {
        now_ms += 10;
        m_advance_ms(10);
        card_tick(now_ms);
    }
}
static int count(const char *needle) {
    int n = 0;
    for (const char *p = said; (p = strstr(p, needle)); p += strlen(needle)) n++;
    return n;
}
static void box_set(int warmth, int pal, int gnd, int ax, int up) {
    tone_set(warmth);
    pal_i = pal, gnd_i = gnd;
    pf.axis[0] = (uint8_t)ax, pf.axis[1] = (uint8_t)up;
}
static bool box_is(int warmth, int pal, int gnd, int ax, int up) {
    return tone_step() == warmth && pal_i == pal && gnd_i == gnd && pf.axis[0] == ax && pf.axis[1] == up;
}

static void mode_main(const char *img) {
    settings_t s;
    settings_report_t r;
    card_in(img);
    m_mounted = true;
    pf.pad_fx[0] = pf.pad_fx[1] = -1;
    box_set(2, 0, 0, 0, 1);

    // Start-up, a card with no file: one is written with what the box is doing, and nothing on the box changes
    card_start();
    card_got("at start-up");
    OK(count("no SETTINGS.TXT yet") == 1 && count("SETTINGS.TXT written (there wasn't one)") == 1, "start-up: said\n%s", said);
    OK(box_is(2, 0, 0, 0, 1) && redraws == 0, "start-up with no file changed the box");
    s = defaults(), s.warmth = 0;
    OK(card_load(&s, &r) == 1 && r.got == ALL && s.warmth == 2 && s.spi == 1, "start-up: the file written");
    uint32_t w = m_blocks_written;
    hush();
    run_ms(10000);
    OK(m_blocks_written == w && n_said == 0, "nothing changed, and the card was written (%s)", said);

    // A change on the box: written once it has stood two seconds, once
    tone_set(4);
    run_ms(1400);
    OK(m_blocks_written == w, "a change written before it had stood 2 s");
    land_next_palette();   // another, 1.4 s in: the two seconds start again
    run_ms(1900);
    OK(m_blocks_written == w, "a second change, and the first's two seconds weren't started again");
    run_ms(700);
    OK(m_blocks_written > w && count("written (changed on the box)") == 1, "a change that stood 2 s wasn't written: %s", said);
    w = m_blocks_written;
    run_ms(8000);
    OK(m_blocks_written == w && n_said == 1, "written more than once");
    s = defaults();
    OK(card_load(&s, &r) == 1 && s.warmth == 4 && s.palette == 1, "the change isn't in the file");
    // A change and back again inside the two seconds: nothing written
    hush();
    tone_set(1);
    run_ms(1500);
    tone_set(4);
    run_ms(5000);
    OK(m_blocks_written == w && n_said == 0, "a change taken back was written");
    // The panel and the pad are settings too
    panel_t p = *lcd_panel();
    p.vcom_set = true, p.vcm = 0x55;
    lcd_panel_set(&p);
    pf.axis[0] = 2;
    run_ms(3000);
    s = defaults();
    OK(card_load(&s, &r) == 1 && s.panel.vcom_set && s.panel.vcm == 0x55 && s.pad[0] == 2 && s.pad[1] == 1, "VCOM and the pad in the file");
    w = m_blocks_written;

    // The file wins: the card back in a box doing something else
    box_set(0, 3, 2, 1, 3);
    p.vcom_set = false;
    lcd_panel_set(&p);
    spi_i = 2;
    lcd_set_hz(SPEEDS_MHZ[spi_i] * 1000000u);
    redraws = cal_loaded = 0;
    hush();
    card_start();
    card_got("looked again");
    OK(box_is(4, 1, 0, 2, 1) && spi_i == 1 && lcd_panel()->vcom_set && lcd_panel()->vcm == 0x55, "the file didn't win");
    OK(lcd_hz() == SPEEDS_MHZ[spi_i] * 1000000u, "the screen's clock is %lu Hz, the file's speed %lu MHz",
       (unsigned long)lcd_hz(), (unsigned long)SPEEDS_MHZ[spi_i]);
    OK(lcd_hz() <= 20000000 && lcd_hz() > 15000000, "the screen's SPI is %lu Hz", (unsigned long)lcd_hz());
    OK(redraws == 1 && cal_loaded == 1, "%d redraws, calibration told %d times", redraws, cal_loaded);
    OK(count("SETTINGS.TXT read, 9 settings from it") == 1, "said: %s", said);
    OK(m_blocks_written == w, "reading the file wrote the card");
    run_ms(6000);
    OK(m_blocks_written == w, "the file's own settings were written back");
    redraws = 0;
    card_start();
    card_got("looked again");
    OK(redraws == 0, "the same file read again drew the page again");
    CLEAN("the file wins");

    // An axis with an effect on it keeps its knob, whatever the file says
    box_set(4, 1, 0, 3, 1);
    pf.pad_fx[0] = 5;
    card_start();
    card_got("looked again");
    OK(pf.axis[0] == 3 && pf.axis[1] == 1, "an axis with an effect on it changed its knob");
    pf.pad_fx[0] = -1;
    box_set(4, 1, 0, 2, 1);
    run_ms(3000);
    w = m_blocks_written;

    // On the card's page: the page drawn, not the one behind it
    screen = SCR_CARD;
    redraws = 0;
    uint32_t words = m_lcd_words;
    tone_set(3);
    run_ms(3000);
    OK(m_blocks_written > w && redraws == 0 && m_lcd_words > words, "a save on the card's page");
    w = m_blocks_written;

    // Lent: a change on the box first is saved, then nothing is written however long it stands
    tone_set(0);
    hush();
    card_do(CARD_DO_LEND);
    OK(card_state() == CARD_LENT && count("written (before the Mac has it)") == 1 && m_blocks_written > w, "lending: %s", said);
    w = m_blocks_written;
    tud_msc_test_unit_ready_cb(0);
    box_set(2, 2, 1, 0, 3);
    uint64_t bytes = m_bytes;
    hush();
    run_ms(20000);
    OK(m_blocks_written == w && m_bytes == bytes, "the box used the card while the Mac had it");
    OK(n_said == 0, "a save tried while the Mac had the card (%s)", said);
    // Ejected: the file's settings over what the box changed meanwhile
    hush();
    tud_msc_start_stop_cb(0, 0, false, true);
    run_ms(20);
    OK(card_state() == CARD_OK && count("ejected on the Mac") == 1, "ejected: %s", said);
    OK(box_is(0, 1, 0, 2, 1), "ejected: the file didn't win over what was changed meanwhile");
    run_ms(5000);
    OK(m_blocks_written == w, "ejected: written");

    // No computer: not lent, and it says so
    m_mounted = false;
    hush();
    card_do(CARD_DO_LEND);
    OK(card_state() == CARD_OK && count("no computer") == 1, "lent to no computer: %s", said);
    m_mounted = true;
    // Lent, and the cable pulled
    card_do(CARD_DO_LEND);
    tone_set(1);
    m_mounted = false;
    hush();
    run_ms(900);
    OK(card_state() == CARD_LENT, "the cable gone 0.9 s: back");
    run_ms(200);
    OK(card_state() == CARD_OK && count("the USB cable went") == 1 && tone_step() == 0, "the cable gone: %s", said);
    m_mounted = true;
    // Lent, written to, and BACK: taken back, and the page behind comes up
    card_from = SCR_CAL;
    card_do(CARD_DO_LEND);
    want_screen = -1;
    card_do(CARD_DO_BACK);
    OK(card_state() == CARD_OK && want_screen == SCR_CAL, "BACK while lent: %s, to page %d", card_state_name(card_state()), want_screen);
    // TAKE IT BACK and SAVE NOW
    card_do(CARD_DO_LEND);
    hush();
    card_do(CARD_DO_TAKE_BACK);
    OK(card_state() == CARD_OK && count("card: taken back\n") == 1, "taken back: %s", said);
    w = m_blocks_written;
    hush();
    card_do(CARD_DO_SAVE);
    OK(m_blocks_written > w && count("written (SAVE NOW)") == 1, "SAVE NOW: %s", said);
    CLEAN("lent and back");

    // The card gone: nothing asked of the slot until LOOK AGAIN, and a change isn't lost when it's back with no file
    card_cfg_t none = hc(img_sectors);
    none.present = false;
    m_insert(none);
    tone_set(2);
    hush();
    run_ms(3000);
    OK(count("couldn't write") == 1 && card_state() == CARD_NONE, "the card gone at a save: %s", said);
    bytes = m_bytes;
    tone_set(3);
    hush();
    run_ms(10000);
    OK(m_bytes == bytes && n_said == 0, "no card, and the slot was asked again (%s)", said);
    m_insert(hc(img_sectors));
    card_do(CARD_DO_LOOK);
    OK(card_state() == CARD_OK && tone_step() == 0, "LOOK AGAIN: %s, warmth %d", card_state_name(card_state()), tone_step());

    // A save that fails with the card still there (it's full, and has no SETTINGS.TXT) isn't tried again and again:
    // once when the card is got, once more when something on the box changes
    static FATFS fs;
    static FIL fil;
    static uint8_t junk[4096];
    card_in(img);
    card_start();
    OK(f_mount(&fs, "", 1) == FR_OK && f_open(&fil, "FILL.BIN", FA_WRITE | FA_CREATE_ALWAYS) == FR_OK, "the full card");
    UINT put = sizeof junk;
    while (put == sizeof junk && f_write(&fil, junk, sizeof junk, &put) == FR_OK) {}
    OK(f_close(&fil) == FR_OK, "the full card: close");
    f_mount(NULL, "", 0);
    hush();
    card_start();
    card_got("looked again");
    run_ms(30000);
    OK(card_state() == CARD_OK && count("couldn't write") == 1, "a full card: tried %d times: %s", count("couldn't write"), said);
    tone_set(tone_step() == 1 ? 2 : 1);
    run_ms(30000);
    OK(count("couldn't write") == 2, "a full card, and a change: tried %d times: %s", count("couldn't write"), said);
    CLEAN("main");
    printf("main: %d checks\n", checks);
}
#endif

int main(int argc, char **argv) {
    const char *m = argc > 1 ? argv[1] : "";
    if (!strcmp(m, "sd")) mode_sd();
    else if (!strcmp(m, "units")) mode_units();
    else if (!strcmp(m, "names")) mode_names();
    else if (!strcmp(m, "fs") && argc == 5) mode_fs(argv[2], argv[3], argv[4]);
    else if (!strcmp(m, "read") && argc == 3) mode_read(argv[2]);
    else if (!strcmp(m, "notfat") && argc == 3) mode_notfat(argv[2]);
    else if (!strcmp(m, "full") && argc == 4) mode_full(argv[2], argv[3]);
    else if (!strcmp(m, "pull") && argc == 3) mode_pull(argv[2]);
    else if (!strcmp(m, "msc") && argc == 5) mode_msc(argv[2], argv[3], argv[4]);
#ifdef MAIN_BLOCK
    else if (!strcmp(m, "main") && argc == 3) mode_main(argv[2]);
#endif
    else {
        fprintf(stderr, "check12: which check?\n");
        return 2;
    }
    if (fails) printf("%d FAILED\n", fails);
    return fails ? 1 : 0;
}
