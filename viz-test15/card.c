// (test 12) The microSD card and its page. See card.h.
//
// The page, from the top: a title line; the card's state, large; up to four lines on what's on it (or why there's
// nothing); a line of news in orange (what a button just did, or a warning); then three buttons along the bottom:
//
//   TO THE MAC / TAKE IT BACK    SAVE NOW / LOOK AGAIN    BACK
//
// TO THE MAC lends the card to the computer (a card that isn't FAT too, so the computer can format it). SAVE NOW
// writes SETTINGS.TXT whether or not anything changed; with no card, or one that isn't FAT, the same button is LOOK
// AGAIN (there's no card-detect switch in the slot, so the box looks when asked). While the card is lent, TAKE IT
// BACK and BACK take it straight back unless the computer has written to it since: then the first tap warns (eject
// it on the computer first) and a second tap of the same button takes it anyway.
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "ff.h"
#include "lcd.h"
#include "sd.h"
#include "msc.h"
#include "card.h"

static FATFS fs;
static int state = CARD_NONE;
static int mount_fr;                 // the last f_mount's FRESULT
static int load_res = -3;            // card_load's last answer for this card (-3: not read yet)
static settings_report_t load_rep;
static int n_saves;                  // since this card was mounted
static bool save_failed;
static char save_err[40];
static char news[100];
static int armed = -1;               // the button whose second tap takes the card back anyway
static int pressed = -1;
static uint32_t shown_read, shown_written;

const char *card_state_name(int s) {
    return s == CARD_OK ? "CARD OK" : s == CARD_NOT_FAT ? "NOT FAT" : s == CARD_LENT ? "AT THE MAC" : "NO CARD";
}

int card_state(void) { return state; }

static void unmount(void) { f_mount(NULL, "", 0); }

// After a read or write failed: if the card has stopped answering, it's gone.
static void check_gone(void) {
    if (state == CARD_OK && !sd_ok()) {
        unmount();
        state = CARD_NONE;
    }
}

int card_start(void) {
    if (state == CARD_LENT) return state;
    unmount();
    news[0] = 0;   // a new state: what was said about the old one no longer holds
    load_res = -3;
    memset(&load_rep, 0, sizeof load_rep);
    n_saves = 0;
    save_failed = false;
    if (!sd_init()) {
        state = CARD_NONE;
        return state;
    }
    mount_fr = f_mount(&fs, "", 1);   // 1: now, so a card that isn't FAT says so here
    state = mount_fr == FR_OK ? CARD_OK : sd_ok() ? CARD_NOT_FAT : CARD_NONE;
    if (state != CARD_OK) unmount();
    return state;
}

int card_load(settings_t *s, settings_report_t *r) {
    if (state != CARD_OK) {
        memset(r, 0, sizeof *r);
        return -2;
    }
    int k = settings_load(s, r);
    load_res = k;
    load_rep = *r;
    if (k < 0) check_gone();
    return k;
}

bool card_save(const settings_t *s, const char *gamma_name) {
    if (state != CARD_OK) return false;
    if (settings_save(s, gamma_name)) {
        n_saves++;
        save_failed = false;
        return true;
    }
    save_failed = true;
    snprintf(save_err, sizeof save_err, "%s", settings_error());
    check_gone();
    return false;
}

bool card_lend(void) {
    if (state == CARD_LENT || state == CARD_NONE || !sd_ok()) return false;
    unmount();   // FatFs holds nothing open between saves; this lets go of its copy of the FAT and directory
    msc_lend();
    state = CARD_LENT;
    news[0] = 0;
    armed = -1;
    shown_read = shown_written = UINT32_MAX;
    return true;
}

int card_take_back(void) {
    if (state != CARD_LENT) return state;
    msc_take_back();
    armed = -1;
    state = CARD_NONE;
    return card_start();   // from scratch: the computer may have formatted it, or it may be another card
}

// ---------------------------------------------------------------- the page

#define TITLE_Y   1
#define STATE_Y  20   // scale 3
#define L1_Y     58   // scale 2 lines
#define L2_Y     82
#define L3_Y    110   // scale 1 notes
#define L4_Y    124
#define L5_Y    146   // scale 2
#define NEWS_Y  196   // scale 2, two lines
#define BTN_Y   264
#define BTN_H   (LCD_H - BTN_Y)
#define BTN_W   (LCD_W / CARD_BUTTONS)

// One line, in capitals, the screen's width (the rest spaces, so it covers what was there).
static void put(int y, int scale, uint16_t fg, const char *s) {
    char u[96];
    int room = LCD_W / (6 * scale), k = 0;
    if (room > (int)sizeof u - 1) room = (int)sizeof u - 1;
    for (; s[k] && k < room; k++) u[k] = (char)toupper((unsigned char)s[k]);
    for (; k < room; k++) u[k] = ' ';
    u[k] = 0;
    lcd_text(0, y, u, scale, fg, C_BLACK);
}

static void putf(int y, int scale, uint16_t fg, const char *fmt, ...) __attribute__((format(printf, 4, 5)));
static void putf(int y, int scale, uint16_t fg, const char *fmt, ...) {
    char t[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(t, sizeof t, fmt, ap);
    va_end(ap);
    put(y, scale, fg, t);
}

// Two lines at scale 2, broken at a space.
static void put_two(int y, uint16_t fg, const char *s) {
    int room = LCD_W / 12, n = (int)strlen(s), cut = n;
    if (n > room) {
        cut = room;
        while (cut > 0 && s[cut] != ' ') cut--;
        if (cut == 0) cut = room;
    }
    char a[48];
    snprintf(a, sizeof a, "%.*s", cut, s);
    put(y, 2, fg, a);
    put(y + 20, 2, fg, s[cut] == ' ' ? s + cut + 1 : s + cut);
}

static void card_line(char *out, size_t n) {
    uint64_t bytes = (uint64_t)sd_sectors() * 512u;
    unsigned tenths = (unsigned)((bytes + 50000000u) / 100000000u);   // GB as the Mac counts them
    snprintf(out, n, "%u.%u GB CARD, %s", tenths / 10, tenths % 10, sd_name());
}

static bool live(int b) {
    if (b == CARD_BTN_MAIN) return state != CARD_NONE;
    if (b == CARD_BTN_SAVE) return state != CARD_LENT;
    return b == CARD_BTN_BACK;
}

static const char *button_name(int b) {
    if (b == CARD_BTN_MAIN) return state == CARD_LENT ? "TAKE IT BACK" : "TO THE MAC";
    if (b == CARD_BTN_SAVE) return state == CARD_OK || state == CARD_LENT ? "SAVE NOW" : "LOOK AGAIN";
    return "BACK";
}

static void draw_button(int b, bool down) {
    int x = b * BTN_W;
    bool on = live(b);
    uint16_t frame = on ? C_WHITE : C_GREY;
    uint16_t bg = down && on ? C_WHITE : C_BLACK, fg = down && on ? C_BLACK : frame;
    const char *s = button_name(b);
    lcd_fill_rect(x, BTN_Y, BTN_W, BTN_H, frame);
    lcd_fill_rect(x + 2, BTN_Y + 2, BTN_W - 4, BTN_H - 4, bg);
    int tw = (int)strlen(s) * 12;
    lcd_text(x + (BTN_W - tw + 1) / 2, BTN_Y + (BTN_H - 16) / 2, s, 2, fg, bg);
}

static int count_bits(unsigned v) {
    int n = 0;
    for (; v; v &= v - 1) n++;
    return n;
}

static void draw_counts(void) {
    uint32_t r = msc_blocks_read(), w = msc_blocks_written();
    if (r == shown_read && w == shown_written) return;
    shown_read = r, shown_written = w;
    putf(L5_Y, 2, C_WHITE, "READ %lu KB, WRITTEN %lu KB", (unsigned long)(r / 2), (unsigned long)(w / 2));
}

void card_draw_info(void) {
    char s[64];
    snprintf(s, sizeof s, "SD SPI %u MHZ", (unsigned)(SD_FAST_HZ / 1000000u));
    putf(TITLE_Y, 1, C_WHITE, "AISLING VIZ TEST 15: THE CARD%*s", LCD_W / 6 - 29, s);
    uint16_t big = state == CARD_OK ? C_WHITE : state == CARD_NONE ? C_GREY : C_ORANGE;
    put(STATE_Y, 3, big, card_state_name(state));

    switch (state) {
    case CARD_NONE:
        putf(L1_Y, 1, C_GREY, "WHAT THE SLOT SAID: %s", sd_error());
        put(L2_Y, 2, C_WHITE, "PUT ONE IN, THEN LOOK AGAIN");
        put(L3_Y, 1, C_GREY, "THE BOX READS FAT (FAT32 OR FAT16). A CARD THAT ISN'T CAN BE LENT TO THE MAC.");
        put(L4_Y, 1, C_GREY, "");
        put(L5_Y, 2, C_WHITE, "");
        break;
    case CARD_NOT_FAT:
        card_line(s, sizeof s);
        put(L1_Y, 2, C_WHITE, s);
        putf(L2_Y, 2, C_ORANGE, "WHY: %s", settings_fr_name(mount_fr));
        put(L3_Y, 1, C_GREY, "TO USE IT HERE: TO THE MAC, THEN ERASE IT IN DISK UTILITY AS MS-DOS (FAT),");
        put(L4_Y, 1, C_GREY, "SCHEME MASTER BOOT RECORD. THAT ERASES EVERYTHING ON IT.");
        put(L5_Y, 2, C_WHITE, "");
        break;
    case CARD_OK:
        card_line(s, sizeof s);
        put(L1_Y, 2, C_WHITE, s);
        if (load_res == 1)
            putf(L2_Y, 2, C_WHITE, "%s: %d READ%s", SETTINGS_FILE, count_bits(load_rep.got),
                 load_rep.cut ? ", CUT" : "");
        else if (load_res == 0)
            putf(L2_Y, 2, C_WHITE, "%s: NONE, SO %s", SETTINGS_FILE, n_saves ? "WRITTEN" : "NOT YET");
        else if (load_res == -1)
            putf(L2_Y, 2, C_ORANGE, "%s: COULDN'T READ IT", SETTINGS_FILE);
        else
            putf(L2_Y, 2, C_WHITE, "%s: NOT READ YET", SETTINGS_FILE);
        if (load_res == 1 && (load_rep.bad || load_rep.unknown)) {   // the first of them on a line of its own
            int n = load_rep.bad + load_rep.unknown;
            putf(L3_Y, 1, C_ORANGE, "%d LINE%s IGNORED. THE FIRST, LINE %d:", n, n == 1 ? "" : "S",
                 load_rep.first_line);
            put(L4_Y, 1, C_ORANGE, load_rep.first);
        } else {
            put(L3_Y, 1, C_GREY, load_res == 1 ? "EVERY LINE USED." : "");
            put(L4_Y, 1, C_GREY, "A CHANGE ON THE BOX IS SAVED TWO SECONDS LATER. TO EDIT IT: TO THE MAC.");
        }
        if (save_failed) putf(L5_Y, 2, C_ORANGE, "NOT SAVED: %s", save_err);
        else if (n_saves) putf(L5_Y, 2, C_WHITE, "SAVED %d TIME%s", n_saves, n_saves == 1 ? "" : "S");
        else put(L5_Y, 2, C_WHITE, "NOTHING SAVED YET");
        break;
    case CARD_LENT:
        card_line(s, sizeof s);
        put(L1_Y, 2, C_WHITE, s);
        put(L2_Y, 2, C_ORANGE, "THE MAC HAS IT: THE BOX KEEPS OFF");
        put(L3_Y, 1, C_GREY, "EJECT IT ON THE MAC AND IT COMES BACK BY ITSELF; SETTINGS.TXT IS READ AGAIN.");
        put(L4_Y, 1, C_GREY, "WHAT THE FILE SAYS THEN WINS OVER WHAT WAS CHANGED ON THE BOX MEANWHILE.");
        shown_read = shown_written = UINT32_MAX;
        draw_counts();
        break;
    }
    put_two(NEWS_Y, C_ORANGE, news);
}

void card_open(void) {
    news[0] = 0;
    armed = -1;
    pressed = -1;
}

void card_draw(void) {
    lcd_fill_rect(0, 0, LCD_W, LCD_H, C_BLACK);
    card_draw_info();
    for (int b = 0; b < CARD_BUTTONS; b++) draw_button(b, b == pressed);
}

// While lent, the counts as they change (main.c calls this often; it draws only when they have).
void card_draw_counts(void) {
    if (state == CARD_LENT) draw_counts();
}

int card_button_at(int x, int y) {
    if (y < BTN_Y || x < 0) return -1;
    int b = x / BTN_W;
    return b >= CARD_BUTTONS ? CARD_BUTTONS - 1 : b;
}

void card_press_show(int b) {
    if (b == pressed) return;
    if (pressed >= 0) draw_button(pressed, false);
    pressed = b;
    if (b >= 0) draw_button(b, true);
}

void card_say(const char *s) {
    snprintf(news, sizeof news, "%s", s);
    put_two(NEWS_Y, C_ORANGE, news);
}

// Taking the card from the computer while it may still be writing: the first tap warns.
static bool hold_off(int b) {
    if (armed == b || !msc_written()) return false;
    armed = b;
    card_say("THE MAC HAS WRITTEN TO IT, SO EJECT IT THERE, OR TAP AGAIN TO TAKE IT ANYWAY");
    return true;
}

int card_press(int b) {
    if (b != armed) armed = -1;
    switch (b) {
    case CARD_BTN_MAIN:
        if (state == CARD_LENT) {
            if (hold_off(b)) return CARD_DO_NOTHING;
            return CARD_DO_TAKE_BACK;
        }
        return state == CARD_NONE ? CARD_DO_NOTHING : CARD_DO_LEND;
    case CARD_BTN_SAVE:
        if (state == CARD_OK) return CARD_DO_SAVE;
        return state == CARD_LENT ? CARD_DO_NOTHING : CARD_DO_LOOK;
    case CARD_BTN_BACK:
        if (state == CARD_LENT && hold_off(b)) return CARD_DO_NOTHING;
        return CARD_DO_BACK;
    }
    return CARD_DO_NOTHING;
}
