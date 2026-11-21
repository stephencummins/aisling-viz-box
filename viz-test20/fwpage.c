// (test 19) Firmware for the FM-1, from the card. See fwpage.h.
//
// The page, from the top: a title line; what is going on, large; the packages, five to a page (name, what it says it
// is, size), or while one is being put on, a bar; two lines of news in orange; whether an FM-1 is there; and three
// buttons:
//
//   INSTALL / SURE?    MORE    BACK
//
// A package is picked by its row; INSTALL asks once more (SURE?) and the second tap begins. Before it begins the
// whole file is read once: it has to say what it is for and carry Felucca's update loader, as fm1_install.py asks.
// While it runs no button does anything.
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "ff.h"
#include "lcd.h"
#include "card.h"
#include "swap.h"
#include "fwpage.h"
#include "ui.h"

#define TITLE_Y   1
#define HEAD_Y   14   // scale 3
#define ROW_Y    46
#define ROW_H    30
#define ROWS      5
#define NEWS_Y  200   // scale 2, two lines
#define LINK_Y  246   // scale 1
#define BTN_Y   264
#define BTN_H   (LCD_H - BTN_Y)
#define BTN_X(b) ui_col(b, 3)
#define BAR_Y   (ROW_Y + 60)
#define BAR_H    30
// The bar on the card's page: in the gap between its lines and its news.
#define DOOR_Y  164
#define DOOR_H   30

#define FW_MAX 20
typedef struct {
    char name[13];                 // the short one: what it is opened by
    char shown[31];                // (test 20) the name as written, without its ".fwsc"
    char product[SWAP_NAME_MAX];   // "" if its head doesn't say
    uint32_t size;
} fw_file_t;

enum { BTN_GO, BTN_MORE, BTN_BACK };
enum { ST_PICK, ST_SURE, ST_RUN };
enum { T_NONE = -1, T_ROW0 = 0, T_BTN0 = 100 };

static fw_file_t files[FW_MAX];
static int n_files, n_more;        // n_more: packages past FW_MAX, not listed
static int dir_res;                // 0 read; 1 no card; 2 the card is lent; 3 no folder; 4 couldn't be read
static int top, sel = -1, stage = ST_PICK;
static int touched = T_NONE;
static bool shown, linked_now;
static char news[100], log_line[100];
static bool log_new;
static swap_t sw;
static FIL fil;
static bool fil_open;
static uint8_t head[SWAP_HEAD_LOGICAL];
static uint8_t chunk[SWAP_HEAD_FILE];   // the file's first bytes; then the pieces of the look through it
static int state_shown = -1;
static uint32_t pct_shown = UINT32_MAX;

// ---------------------------------------------------------------- the card

static void told(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void told(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(log_line, sizeof log_line, fmt, ap);
    va_end(ap);
    log_new = true;
}

const char *fw_news(void) {
    if (!log_new) return NULL;
    log_new = false;
    return log_line;
}

// (test 20; Stephen: "FM-1_39 IS ON THE FM-1. Would be better if said AISLING FX-1") What the page calls a package: its
// file's name, in capitals. The product's number stays in the log.
static const char *called(const fw_file_t *f) {
    static char c[sizeof f->shown];
    size_t i = 0;
    for (; f->shown[i]; i++) c[i] = f->shown[i] >= 'a' && f->shown[i] <= 'z' ? (char)(f->shown[i] - 32) : f->shown[i];
    c[i] = 0;
    return c;
}
static void path_of(const fw_file_t *f, char *out, size_t n) { snprintf(out, n, FW_DIR "/%.12s", f->name); }

static bool is_package(const char *name) {
    const char *dot = strrchr(name, '.');
    return dot && toupper((unsigned char)dot[1]) == 'F' && toupper((unsigned char)dot[2]) == 'W' &&
           toupper((unsigned char)dot[3]) == 'S';
}

// What a file says it is, from its first 960 bytes.
static void look_at(fw_file_t *f) {
    char path[24];
    FIL fp;
    UINT got = 0;
    f->product[0] = 0;
    path_of(f, path, sizeof path);
    if (f->size < SWAP_HEAD_FILE || f_open(&fp, path, FA_READ) != FR_OK) return;
    if (f_read(&fp, chunk, SWAP_HEAD_FILE, &got) == FR_OK && got == SWAP_HEAD_FILE && !swap_product(chunk, f->product))
        f->product[0] = 0;
    f_close(&fp);
}

static void scan(void) {
    DIR d;
    FILINFO fno;
    n_files = n_more = 0;
    top = 0;
    sel = -1;
    if (card_state() != CARD_OK) {
        dir_res = card_state() == CARD_LENT ? 2 : 1;
        return;
    }
    FRESULT fr = f_opendir(&d, FW_DIR);
    if (fr != FR_OK) {
        dir_res = fr == FR_NO_PATH || fr == FR_NO_FILE ? 3 : 4;
        return;
    }
    dir_res = 0;
    while (f_readdir(&d, &fno) == FR_OK && fno.fname[0]) {
        if ((fno.fattrib & (AM_DIR | AM_HID | AM_SYS)) || fno.fname[0] == '.' || !is_package(fno.fname)) continue;
        if (n_files == FW_MAX) {
            n_more++;
            continue;
        }
        fw_file_t *f = &files[n_files++];
        snprintf(f->name, sizeof f->name, "%.12s", fno.altname[0] ? fno.altname : fno.fname);
        snprintf(f->shown, sizeof f->shown, "%.30s", fno.fname);
        char *dot = strrchr(f->shown, '.');
        if (dot && strlen(fno.fname) < sizeof f->shown) *dot = 0;
        f->size = (uint32_t)fno.fsize;
    }
    f_closedir(&d);
    for (int i = 0; i < n_files; i++) look_at(&files[i]);
}

static void shut(void) {
    if (fil_open) f_close(&fil);
    fil_open = false;
}

// The whole package, once: its head kept, Felucca's loader looked for. Leaves it open for the install.
static const char *take(const fw_file_t *f) {
    char path[24];
    UINT got;
    uint32_t mark = 0;
    bool has = false;
    shut();
    path_of(f, path, sizeof path);
    if (f_open(&fil, path, FA_READ) != FR_OK) return "THE CARD WOULDN'T OPEN IT";
    fil_open = true;
    if (f_read(&fil, chunk, SWAP_HEAD_FILE, &got) != FR_OK || got != SWAP_HEAD_FILE) return "THE CARD COULDN'T READ IT";
    char product[SWAP_NAME_MAX];
    if (!swap_product(chunk, product) || strcmp(product, f->product)) return "IT ISN'T WHAT IT WAS WHEN LISTED";
    swap_head(chunk, head);
    has = swap_mark(&mark, chunk, got);
    for (uint32_t left = f->size - SWAP_HEAD_FILE; left && !has;) {
        UINT want = left > sizeof chunk ? (UINT)sizeof chunk : (UINT)left;
        if (f_read(&fil, chunk, want, &got) != FR_OK || got != want) return "THE CARD COULDN'T READ IT";
        has = swap_mark(&mark, chunk, got);
        left -= got;
    }
    if (!has) return "IT HAS NO FELUCCA UPDATE LOADER IN IT, SO IT ISN'T PUT ON";
    return NULL;
}

// swap.h's read: the logical image, its first 940 bytes from the head kept, the rest from the file.
static bool read_image(void *ctx, uint32_t at, uint8_t *dst, uint32_t n) {
    (void)ctx;
    if (at < SWAP_HEAD_LOGICAL) {
        uint32_t k = SWAP_HEAD_LOGICAL - at < n ? SWAP_HEAD_LOGICAL - at : n;
        memcpy(dst, head + at, k);
        at += k, dst += k, n -= k;
    }
    if (!n) return true;
    UINT got;
    return fil_open && f_lseek(&fil, swap_file_off(at)) == FR_OK && f_read(&fil, dst, n, &got) == FR_OK && got == n;
}

static bool send_wire(void *ctx, const uint8_t *wire, uint32_t n) {
    (void)ctx;
    return fw_send(wire, n);
}

// ---------------------------------------------------------------- drawing

// One line, in capitals, the usable width (the rest spaces, so it covers what was there).
static void put(int y, int scale, uint16_t fg, const char *s) {
    char u[96];
    int room = UI_W / (6 * scale), k = 0;
    if (room > (int)sizeof u - 1) room = (int)sizeof u - 1;
    for (; s[k] && k < room; k++) u[k] = (char)toupper((unsigned char)s[k]);
    for (; k < room; k++) u[k] = ' ';
    u[k] = 0;
    lcd_text(0, y, u, scale, fg, C_BLACK);
}

static void put_two(int y, uint16_t fg, const char *s) {
    int room = UI_W / 12, n = (int)strlen(s), cut = n;
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

static void say_news(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say_news(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(news, sizeof news, fmt, ap);
    va_end(ap);
    if (shown) put_two(NEWS_Y, C_ORANGE, news);
    if (news[0]) told("firmware: %s", news);
}

static bool live(int b) {
    if (stage == ST_RUN) return false;
    if (b == BTN_GO) return sel >= 0 && files[sel].product[0];
    if (b == BTN_MORE) return n_files > ROWS;
    return true;
}

static const char *button_name(int b) {
    if (b == BTN_GO) return stage == ST_SURE ? "SURE?" : "INSTALL";
    return b == BTN_MORE ? "MORE" : "BACK";
}

static void draw_button(int b) {
    int x = BTN_X(b), w = BTN_X(b + 1) - x;
    bool on = live(b), down = on && touched == T_BTN0 + b;
    uint16_t frame = on ? (b == BTN_GO && stage == ST_SURE ? C_ORANGE : C_WHITE) : C_GREY;
    uint16_t bg = down ? frame : C_BLACK, fg = down ? C_BLACK : frame;
    const char *s = button_name(b);
    lcd_fill_rect(x, BTN_Y, w, BTN_H, frame);
    lcd_fill_rect(x + 2, BTN_Y + 2, w - 4, BTN_H - 4, bg);
    int tw = (int)strlen(s) * 12;
    lcd_text(x + (w - tw + 1) / 2, BTN_Y + (BTN_H - 16) / 2, s, 2, fg, bg);
}

static void draw_row(int r) {
    int i = top + r, y = ROW_Y + r * ROW_H;
    if (i >= n_files) {
        lcd_fill_rect(0, y, UI_W, ROW_H, C_BLACK);
        return;
    }
    const fw_file_t *f = &files[i];
    bool on = i == sel, down = touched == T_ROW0 + r;
    uint16_t bg = on ? C_WHITE : down ? C_GREY : C_BLACK;
    uint16_t fg = on ? C_BLACK : f->product[0] ? C_WHITE : down ? C_BLACK : C_GREY;
    char t[48], u[48];
    // (test 20) the name as written: two packages for one FM-1 say the same product
    if (f->product[0]) snprintf(t, sizeof t, "%-30.30s %4luK", f->shown, (unsigned long)((f->size + 512) / 1024));
    else snprintf(t, sizeof t, "%-16.16s NOT A PACKAGE %4luK", f->shown, (unsigned long)((f->size + 512) / 1024));
    int k = 0;
    for (; t[k]; k++) u[k] = (char)toupper((unsigned char)t[k]);
    u[k] = 0;
    lcd_fill_rect(0, y, UI_W, ROW_H - 2, bg);
    lcd_fill_rect(0, y + ROW_H - 2, UI_W, 2, C_BLACK);
    lcd_text(4, y + (ROW_H - 2 - 16) / 2, u, 2, fg, bg);
}

static void draw_bar(void) {
    int w = UI_W - 8, full = (int)((uint32_t)(w - 4) * sw.pct / 100u);
    lcd_fill_rect(4, BAR_Y, w, BAR_H, C_WHITE);
    lcd_fill_rect(6, BAR_Y + 2, w - 4, BAR_H - 4, C_BLACK);
    if (full > 0) lcd_fill_rect(6, BAR_Y + 2, full, BAR_H - 4, C_GREEN);
}

static const char *head_line(char *t, size_t n) {
    if (stage == ST_RUN) {
        switch (sw.state) {
        case SWAP_ASK: return "ASKING WHO IT IS";
        case SWAP_CHECK: return "THE FM-1 CHECKS IT";
        case SWAP_LOADER: return "TO ITS LOADER";
        case SWAP_WRITE: snprintf(t, n, "WRITING %lu%%", (unsigned long)sw.pct); return t;
        default: return "RESTARTING";
        }
    }
    if (dir_res == 1) return "NO CARD";
    if (dir_res == 2) return "THE CARD IS AT THE MAC";
    if (dir_res == 3) return "NO FOLDER " FW_DIR;
    if (dir_res == 4) return "CAN'T READ " FW_DIR;
    if (!n_files) return "NOTHING IN " FW_DIR;
    if (n_files > ROWS) snprintf(t, n, "PICK ONE: %d TO %d OF %d", top + 1, top + ROWS < n_files ? top + ROWS : n_files, n_files);
    else snprintf(t, n, "PICK ONE OF %d", n_files);
    return t;
}

static void draw_head(void) {
    char t[52];
    put(HEAD_Y, 3, stage == ST_RUN ? C_ORANGE : C_WHITE, head_line(t, sizeof t));
}

static void draw_link(void) {
    put(LINK_Y, 1, linked_now ? C_GREEN : C_GREY,
        linked_now ? "AN FM-1 IS PLUGGED IN" : "NO FM-1 TO SEND TO: PLUG IT INTO THE BOX'S USB HOST SOCKET");
}

static void draw_middle(void) {
    if (stage != ST_RUN) {
        for (int r = 0; r < ROWS; r++) draw_row(r);
        return;
    }
    char t[80];
    lcd_fill_rect(0, ROW_Y, UI_W, ROWS * ROW_H, C_BLACK);
    snprintf(t, sizeof t, "PUTTING %s ON THE FM-1", called(&files[sel]));
    put(ROW_Y + 4, 2, C_WHITE, t);
    put(ROW_Y + 28, 2, C_WHITE, "LEAVE THE FM-1 ON AND PLUGGED IN");
    draw_bar();
}

void fw_draw(void) {
    lcd_fill_rect(0, 0, LCD_W, LCD_H, C_BLACK);
    put(TITLE_Y, 1, C_GREY, "AISLING VIZ TEST 20: FIRMWARE FOR THE FM-1, FROM THE CARD'S FOLDER " FW_DIR);
    draw_head();
    draw_middle();
    put_two(NEWS_Y, C_ORANGE, news);
    draw_link();
    for (int b = 0; b < 3; b++) draw_button(b);
    state_shown = sw.state;
    pct_shown = sw.pct;
}

void fw_door_draw(bool down) {
    static const char name[] = "FIRMWARE FOR THE FM-1";
    uint16_t bg = down ? C_WHITE : C_BLACK, fg = down ? C_BLACK : C_WHITE;
    lcd_fill_rect(0, DOOR_Y, UI_W, 2, C_WHITE);
    lcd_fill_rect(0, DOOR_Y + DOOR_H - 2, UI_W, 2, C_WHITE);
    lcd_fill_rect(0, DOOR_Y, 2, DOOR_H, C_WHITE);
    lcd_fill_rect(UI_W - 2, DOOR_Y, 2, DOOR_H, C_WHITE);
    lcd_fill_rect(2, DOOR_Y + 2, UI_W - 4, 5, bg);
    lcd_fill_rect(2, DOOR_Y + DOOR_H - 7, UI_W - 4, 5, bg);
    int tw = (int)(sizeof name - 1) * 12, x = (UI_W - tw) / 2;
    lcd_fill_rect(2, DOOR_Y + 7, x - 2, 16, bg);
    lcd_fill_rect(x + tw, DOOR_Y + 7, UI_W - 2 - x - tw, 16, bg);
    lcd_text(x, DOOR_Y + 7, name, 2, fg, bg);
}

bool fw_door_at(int x, int y) { return x >= 0 && !ui_off(x) && y >= DOOR_Y && y < DOOR_Y + DOOR_H; }

// ---------------------------------------------------------------- the page's touch

static int target_at(int x, int y) {
    if (x < 0 || ui_off(x)) return T_NONE;
    if (y >= BTN_Y) {
        int b = 0;
        while (b < 2 && x >= BTN_X(b + 1)) b++;
        return live(b) ? T_BTN0 + b : T_NONE;
    }
    if (stage != ST_RUN && y >= ROW_Y && y < ROW_Y + ROWS * ROW_H) {
        int r = (y - ROW_Y) / ROW_H;
        return top + r < n_files ? T_ROW0 + r : T_NONE;
    }
    return T_NONE;
}

static void draw_target(int t) {
    if (t >= T_BTN0) draw_button(t - T_BTN0);
    else if (t >= T_ROW0) draw_row(t - T_ROW0);
}

void fw_touch(int x, int y) {
    int t = target_at(x, y), was = touched;
    if (t == was) return;
    touched = t;
    draw_target(was);
    draw_target(t);
}

static void unsure(void) {
    if (stage != ST_SURE) return;
    stage = ST_PICK;
    draw_button(BTN_GO);
}

static void begin(uint32_t now) {
    const fw_file_t *f = &files[sel];
    put_two(NEWS_Y, C_ORANGE, "READING THE PACKAGE");
    const char *why = card_state() == CARD_OK ? take(f) : "THE CARD HAS GONE";
    if (why) {
        shut();
        stage = ST_PICK;
        say_news("%s", why);
        draw_button(BTN_GO);
        return;
    }
    swap_io_t io = {.read = read_image, .send = send_wire};
    swap_start(&sw, &io, f->product, f->size - (SWAP_HEAD_FILE - SWAP_HEAD_LOGICAL), linked_now, now);
    stage = ST_RUN;
    news[0] = 0;
    told("firmware: putting %s (%s, %lu bytes) on the FM-1", f->product, f->shown, (unsigned long)f->size);
    fw_draw();
}

int fw_lift(uint32_t now) {
    int t = touched;
    touched = T_NONE;
    draw_target(t);
    if (t == T_NONE || stage == ST_RUN) return FW_DO_NOTHING;
    if (t < T_BTN0) {
        int was = sel;
        sel = top + (t - T_ROW0);
        unsure();
        if (was >= top && was < top + ROWS) draw_row(was - top);
        draw_row(sel - top);
        draw_button(BTN_GO);
        say_news("%s", files[sel].product[0] ? "" : "THAT FILE DOESN'T SAY WHAT IT IS FOR: NOT AN FM-1 PACKAGE");
        return FW_DO_NOTHING;
    }
    switch (t - T_BTN0) {
    case BTN_GO:
        if (stage == ST_PICK) {
            stage = ST_SURE;
            draw_button(BTN_GO);
            say_news("%s REPLACES WHAT IS ON THE FM-1. TAP AGAIN TO DO IT", called(&files[sel]));
        } else {
            begin(now);
        }
        break;
    case BTN_MORE:
        unsure();
        top = top + ROWS < n_files ? top + ROWS : 0;
        draw_head();
        draw_middle();
        break;
    default:
        return FW_DO_BACK;
    }
    return FW_DO_NOTHING;
}

void fw_open(bool linked, uint32_t now) {
    (void)now;
    shown = true;
    linked_now = linked;
    touched = T_NONE;
    if (stage == ST_RUN) return;   // one is running: the page as it is
    stage = ST_PICK;
    news[0] = 0;
    scan();
    if (n_more) snprintf(news, sizeof news, "%d MORE IN THE FOLDER THAN THE %d LISTED", n_more, FW_MAX);
    else if (dir_res == 3 || (!dir_res && !n_files))
        snprintf(news, sizeof news, "PUT .FWSC PACKAGES IN A FOLDER " FW_DIR " ON THE CARD");
}

void fw_close(void) {
    shown = false;
    touched = T_NONE;
    if (stage != ST_RUN) shut();
}

// ---------------------------------------------------------------- the install

bool fw_busy(void) { return stage == ST_RUN; }

void fw_link(bool up, uint32_t now) {
    linked_now = up;
    if (stage == ST_RUN) swap_link(&sw, up, now);
    if (shown) draw_link();
}

void fw_rx(const uint8_t *sysex, uint32_t n, uint32_t now) {
    if (stage == ST_RUN) swap_rx(&sw, sysex, n, now);
}

static void ended(void) {
    static const char *const why[] = {
        [SWAP_E_SILENT] = "NOTHING ANSWERED. NOTHING WAS WRITTEN",
        [SWAP_E_MODEL] = "THIS PACKAGE IS FOR ANOTHER INSTRUMENT. NOTHING WAS WRITTEN",
        [SWAP_E_STOPPED] = "IT STOPPED ASKING DURING THE CHECK. NOTHING WAS WRITTEN",
        [SWAP_E_NOLOADER] = "ITS UPDATE LOADER DIDN'T APPEAR. INSTALL AGAIN",
        [SWAP_E_HALF] = "IT STOPPED PART WAY. THE FM-1 WAITS IN ITS LOADER: INSTALL AGAIN",
        [SWAP_E_BADREQ] = "IT ASKED FOR WHAT ISN'T IN THE PACKAGE. INSTALL AGAIN, OR ANOTHER",
        [SWAP_E_CARD] = "THE CARD COULDN'T BE READ. IF THE FM-1 IS IN ITS LOADER, INSTALL AGAIN",
        [SWAP_E_NORETURN] = "WRITTEN, BUT IT DIDN'T ANSWER AFTER: SWITCH THE FM-1 OFF AND ON",
    };
    stage = ST_PICK;
    shut();
    if (sw.state == SWAP_DONE) snprintf(news, sizeof news, "%s IS ON THE FM-1%s", sel >= 0 ? called(&files[sel]) : sw.back, sw.resumed ? " (IT WAS IN ITS LOADER)" : "");
    else if (sw.error == SWAP_E_MISMATCH) snprintf(news, sizeof news, "WRITTEN, BUT IT ANSWERS AS %s", sw.back);
    else snprintf(news, sizeof news, "%s", why[sw.error]);
    told("firmware: %s (%s; %lu requests answered%s%s)", news, sw.back[0] ? sw.back : sw.product, (unsigned long)sw.served, sw.device[0] ? ", it was " : "", sw.device);
    if (shown) fw_draw();
}

void fw_tick(uint32_t now) {
    if (stage != ST_RUN) return;
    swap_tick(&sw, now);
    if (!swap_busy(&sw)) {
        ended();
        return;
    }
    if (!shown) return;
    if (sw.state != state_shown) {
        state_shown = sw.state;
        pct_shown = sw.pct;
        draw_head();
        draw_bar();
    } else if (sw.pct != pct_shown) {
        pct_shown = sw.pct;
        draw_head();
        draw_bar();
    }
}
