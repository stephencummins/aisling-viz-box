// AISLING visualiser, test 1.
//
// Answers four questions and nothing else:
//   1. Does the screen work, the right way round and in the right colours? (colour bars, labelled)
//   2. Which of GPIO28/29 is D+ on the PIO-USB socket? (read once, before the host starts)
//   3. Does the FM-1 enumerate on PIO-USB, and what does it say it is? (IDs, name, interfaces)
//   4. Do its MIDI messages arrive? (the last few, decoded, with counters)
// Touch readings are shown too, raw. A log of the same goes to the Mac over the "USB" socket as a serial
// port; opening that port at 1200 baud reboots the board into its bootloader for the next load.
//
// Core 0: screen, touch, the serial port to the Mac (native USB, TinyUSB root port 0).
// Core 1: USB host on PIO-USB (TinyUSB root port 1), which needs a core to itself for its 1 ms frames.
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/bootrom.h"
#include "pico/util/queue.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "pio_usb.h"
#include "tusb.h"
#include "lcd.h"

#define PIN_USB_A 28
#define PIN_USB_B 29

// ---------------------------------------------------------------- events, core 1 -> core 0

enum {
    EV_LINES,     // b[0], b[1]: levels on GPIO28, GPIO29
    EV_HOST_UP,   // b[0]: D+ pin; b[1]: 1 if D- is D+ minus 1
    EV_MOUNT,     // w[0] VID, w[1] PID, b[0] speed
    EV_NAME,      // text
    EV_ITF,       // b[0] number, b[1] class, b[2] subclass, b[3] alt
    EV_MIDI_EP,   // b[0] endpoint address (0 = none found), w[0] max packet size
    EV_MIDI,      // b[0..3]: one USB-MIDI event packet
    EV_UMOUNT,
    EV_ERROR,     // text
};

typedef struct {
    uint8_t type;
    uint8_t b[4];
    uint16_t w[2];
    char text[36];
} event_t;

static queue_t events;
static volatile uint32_t n_dropped, n_clock, n_sense;

static void post(const event_t *e) {
    if (!queue_try_add(&events, e)) n_dropped++;
}

static void post_text(uint8_t type, const char *fmt, ...) {
    event_t e = {.type = type};
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(e.text, sizeof e.text, fmt, ap);
    va_end(ap);
    post(&e);
}

// ---------------------------------------------------------------- core 1: USB host

static uint8_t str_buf[128];
static uint8_t cfg_buf[1024];
static uint8_t midi_buf[64];
static uint8_t midi_ep;
static uint16_t midi_mps;
static tusb_desc_endpoint_t midi_ep_desc;

static void midi_read(uint8_t daddr);

static void midi_done(tuh_xfer_t *x) {
    if (x->result != XFER_RESULT_SUCCESS) {
        post_text(EV_ERROR, "MIDI READ FAILED, RESULT %d", (int)x->result);
        return; // don't spin on a failing endpoint
    }
    for (uint32_t i = 0; i + 4 <= x->actual_len; i += 4) {
        const uint8_t *p = &midi_buf[i];
        if (!(p[0] | p[1] | p[2] | p[3])) continue; // padding
        if ((p[0] & 0x0F) == 0x0F && p[1] == 0xF8) { n_clock++; continue; }
        if ((p[0] & 0x0F) == 0x0F && p[1] == 0xFE) { n_sense++; continue; }
        event_t e = {.type = EV_MIDI, .b = {p[0], p[1], p[2], p[3]}};
        post(&e);
    }
    midi_read(x->daddr);
}

static void midi_read(uint8_t daddr) {
    tuh_xfer_t x = {
        .daddr = daddr,
        .ep_addr = midi_ep,
        .buflen = midi_mps > sizeof midi_buf ? sizeof midi_buf : midi_mps,
        .buffer = midi_buf,
        .complete_cb = midi_done,
        .user_data = 0,
    };
    if (!tuh_edpt_xfer(&x)) post_text(EV_ERROR, "MIDI READ WOULDN'T START");
}

static void config_done(tuh_xfer_t *x) {
    if (x->result != XFER_RESULT_SUCCESS || x->actual_len < 9) {
        post_text(EV_ERROR, "CONFIG DESCRIPTOR FAILED");
        return;
    }
    uint32_t total = (uint32_t)cfg_buf[2] | ((uint32_t)cfg_buf[3] << 8);
    if (total > x->actual_len) total = x->actual_len;

    const uint8_t *p = cfg_buf, *end = cfg_buf + total;
    int cls = -1, sub = -1;
    midi_ep = 0;
    while (p + 2 <= end && p[0] >= 2 && p + p[0] <= end) {
        if (p[1] == TUSB_DESC_INTERFACE && p[0] >= 9) {
            cls = p[5];
            sub = p[6];
            event_t e = {.type = EV_ITF, .b = {p[2], p[5], p[6], p[3]}};
            post(&e);
        } else if (p[1] == TUSB_DESC_ENDPOINT && p[0] >= 7 && cls == TUSB_CLASS_AUDIO && sub == 3) {
            // MIDI Streaming: take the first IN endpoint, bulk by the spec (a few devices use interrupt)
            uint8_t addr = p[2], attr = p[3] & 0x03;
            if (!midi_ep && (addr & 0x80) && (attr == TUSB_XFER_BULK || attr == TUSB_XFER_INTERRUPT)) {
                memcpy(&midi_ep_desc, p, sizeof midi_ep_desc); // the 7 standard bytes; audio adds 2 more
                midi_ep_desc.bLength = sizeof midi_ep_desc;
                midi_ep = addr;
                midi_mps = (uint16_t)(p[4] | (p[5] << 8));
            }
        }
        p += p[0];
    }

    event_t e = {.type = EV_MIDI_EP, .b = {midi_ep}, .w = {midi_mps}};
    post(&e);
    if (!midi_ep) return;
    if (!tuh_edpt_open(x->daddr, &midi_ep_desc)) {
        post_text(EV_ERROR, "MIDI ENDPOINT WOULDN'T OPEN");
        midi_ep = 0;
        return;
    }
    midi_read(x->daddr);
}

static void name_done(tuh_xfer_t *x) {
    if (x->result == XFER_RESULT_SUCCESS && x->actual_len >= 2 && str_buf[1] == TUSB_DESC_STRING) {
        char name[33];
        uint32_t n = (str_buf[0] < x->actual_len ? str_buf[0] : x->actual_len);
        n = n >= 2 ? (n - 2) / 2 : 0;
        if (n > 32) n = 32;
        for (uint32_t i = 0; i < n; i++) {
            uint16_t c = (uint16_t)(str_buf[2 + 2 * i] | (str_buf[3 + 2 * i] << 8));
            name[i] = (c >= 32 && c < 127) ? (char)c : '?';
        }
        name[n] = 0;
        post_text(EV_NAME, "%s", name);
    } else {
        post_text(EV_NAME, "(NO NAME GIVEN)");
    }
    // One control transfer at a time: ask for the configuration only now.
    if (!tuh_descriptor_get_configuration(x->daddr, 0, cfg_buf, sizeof cfg_buf, config_done, 0))
        post_text(EV_ERROR, "CONFIG REQUEST WOULDN'T START");
}

void tuh_mount_cb(uint8_t daddr) {
    uint16_t vid = 0, pid = 0;
    tuh_vid_pid_get(daddr, &vid, &pid);
    event_t e = {.type = EV_MOUNT, .w = {vid, pid}, .b = {(uint8_t)tuh_speed_get(daddr)}};
    post(&e);
    if (!tuh_descriptor_get_product_string(daddr, 0x0409, str_buf, sizeof str_buf, name_done, 0)) {
        tuh_xfer_t fake = {.daddr = daddr, .result = XFER_RESULT_FAILED};
        name_done(&fake);
    }
}

void tuh_umount_cb(uint8_t daddr) {
    (void)daddr;
    midi_ep = 0;
    event_t e = {.type = EV_UMOUNT};
    post(&e);
}

static void core1_main(void) {
    // Which line is D+? A full-speed device (and USB-MIDI is always full speed) pulls D+ up through
    // 1.5k, so with both lines pulled down the one that goes high is D+. Wait for one, and only one,
    // to hold high for 50 ms.
    gpio_init(PIN_USB_A);
    gpio_init(PIN_USB_B);
    gpio_set_dir(PIN_USB_A, GPIO_IN);
    gpio_set_dir(PIN_USB_B, GPIO_IN);
    gpio_pull_down(PIN_USB_A);
    gpio_pull_down(PIN_USB_B);
    sleep_ms(5);

    int last = -1, steady = 0;
    for (;;) {
        int a = gpio_get(PIN_USB_A), b = gpio_get(PIN_USB_B);
        int now = a | (b << 1);
        if (now != last) {
            event_t e = {.type = EV_LINES, .b = {(uint8_t)a, (uint8_t)b}};
            post(&e);
            last = now;
            steady = 0;
        }
        if (now == 1 || now == 2) {
            if (++steady >= 50) break;
        } else {
            steady = 0;
        }
        sleep_ms(1);
    }

    pio_usb_configuration_t cfg = PIO_USB_DEFAULT_CONFIG;
    if (last == 1) {
        cfg.pin_dp = PIN_USB_A;
        cfg.pinout = PIO_USB_PINOUT_DPDM; // D- = 29
    } else {
        cfg.pin_dp = PIN_USB_B;
        cfg.pinout = PIO_USB_PINOUT_DMDP; // D- = 28
    }
    event_t e = {.type = EV_HOST_UP, .b = {cfg.pin_dp, cfg.pinout == PIO_USB_PINOUT_DMDP}};
    post(&e);

    tuh_configure(1, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &cfg);
    tuh_init(1);
    for (;;) tuh_task();
}

// ---------------------------------------------------------------- core 0: screen and log

#define ROW_Y0    104
#define ROW_PITCH 18
#define ROWS      12
#define COLS      40

enum { R_CLOCKS, R_LINES, R_HOST, R_DEVICE, R_NAME, R_ITFS, R_MIDI_EP, R_COUNTS,
       R_LAST0, R_LAST1, R_LAST2, R_TOUCH };

static char rows[ROWS][COLS + 1];
static bool log_open;

static void log_str(const char *s) {
    if (!tud_cdc_connected()) return;
    tud_cdc_write_str(s);
    tud_cdc_write_str("\r\n");
}

static void row_draw(int r, uint16_t fg) {
    lcd_text(0, ROW_Y0 + r * ROW_PITCH, rows[r], 2, fg, C_BLACK);
}

// Sets a status row, pads it to the full width so old text is covered, draws it, and logs it if asked.
static void row_set(int r, bool log, uint16_t fg, const char *fmt, ...) {
    char tmp[COLS + 1];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    size_t n = strlen(tmp);
    memset(tmp + n, ' ', COLS - n);
    tmp[COLS] = 0;
    if (strcmp(tmp, rows[r]) == 0) return;
    memcpy(rows[r], tmp, sizeof tmp);
    row_draw(r, fg);
    if (log) {
        char line[COLS + 1];
        memcpy(line, tmp, sizeof line);
        for (int i = COLS - 1; i >= 0 && line[i] == ' '; i--) line[i] = 0;
        log_str(line);
    }
}

static void log_snapshot(void) {
    log_str("---- AISLING viz test 1: status ----");
    for (int r = 0; r < ROWS; r++) {
        char line[COLS + 1];
        memcpy(line, rows[r], sizeof line);
        for (int i = COLS - 1; i >= 0 && line[i] == ' '; i--) line[i] = 0;
        if (line[0]) log_str(line);
    }
}

static const char *note_name(uint8_t n, char *out) {
    static const char *names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    sprintf(out, "%s%d", names[n % 12], (int)(n / 12) - 1); // MIDI 60 = C4
    return out;
}

static void describe_midi(const uint8_t p[4], char *out, size_t len) {
    uint8_t s = p[1], d1 = p[2], d2 = p[3];
    uint8_t kind = s & 0xF0, ch = (uint8_t)((s & 0x0F) + 1);
    char nn[8];
    switch (kind) {
    case 0x80: snprintf(out, len, "%02X %02X %02X CH%-2u OFF %s", s, d1, d2, ch, note_name(d1, nn)); break;
    case 0x90:
        if (d2) snprintf(out, len, "%02X %02X %02X CH%-2u ON  %s V%u", s, d1, d2, ch, note_name(d1, nn), d2);
        else    snprintf(out, len, "%02X %02X %02X CH%-2u OFF %s", s, d1, d2, ch, note_name(d1, nn));
        break;
    case 0xA0: snprintf(out, len, "%02X %02X %02X CH%-2u POLY AT %s %u", s, d1, d2, ch, note_name(d1, nn), d2); break;
    case 0xB0: snprintf(out, len, "%02X %02X %02X CH%-2u CC %u = %u", s, d1, d2, ch, d1, d2); break;
    case 0xC0: snprintf(out, len, "%02X %02X    CH%-2u PROGRAM %u", s, d1, ch, d1 + 1); break;
    case 0xD0: snprintf(out, len, "%02X %02X    CH%-2u PRESSURE %u", s, d1, ch, d1); break;
    case 0xE0: snprintf(out, len, "%02X %02X %02X CH%-2u BEND %d", s, d1, d2, ch, (int)((d2 << 7) | d1) - 8192); break;
    default:
        if (s == 0xFA)      snprintf(out, len, "FA       START");
        else if (s == 0xFB) snprintf(out, len, "FB       CONTINUE");
        else if (s == 0xFC) snprintf(out, len, "FC       STOP");
        else snprintf(out, len, "%02X %02X %02X %02X  CIN %X", p[0], p[1], p[2], p[3], p[0] & 0x0F);
        break;
    }
}

static void draw_static(uint32_t spi_hz) {
    lcd_fill_rect(0, 0, LCD_W, LCD_H, C_BLACK);
    lcd_text(6, 6, "AISLING VIZ TEST 1", 3, C_ORANGE, C_BLACK);

    // Colour bars, each named in its own colour's opposite, so a swapped or inverted panel shows at once.
    const struct { uint16_t c, fg; const char *name; } bars[4] = {
        {C_RED, C_BLACK, "RED"}, {C_GREEN, C_BLACK, "GREEN"},
        {C_BLUE, C_WHITE, "BLUE"}, {C_WHITE, C_BLACK, "WHITE"},
    };
    for (int i = 0; i < 4; i++) {
        int x = i * 120;
        lcd_fill_rect(x, 36, 120, 60, bars[i].c);
        int w = (int)strlen(bars[i].name) * 12;
        lcd_text(x + (120 - w) / 2, 58, bars[i].name, 2, bars[i].fg, bars[i].c);
    }
    // A corner mark, top left, to show which way up the picture is.
    lcd_fill_rect(0, 0, 4, 4, C_WHITE);

    row_set(R_CLOCKS, true, C_GREY, "SYS %lu MHZ PERI %lu SPI %lu.%lu",
            (unsigned long)(clock_get_hz(clk_sys) / 1000000), (unsigned long)(clock_get_hz(clk_peri) / 1000000),
            (unsigned long)(spi_hz / 1000000), (unsigned long)((spi_hz / 100000) % 10));
    row_set(R_HOST, true, C_VELLUM, "WAITING FOR THE FM-1 ON PIO-USB");
    row_set(R_COUNTS, false, C_VELLUM, "MSGS 0 CLOCK 0 SENSE 0 DROP 0");
    row_set(R_TOUCH, false, C_GREY, "TOUCH -");
}

static uint32_t n_msgs;
static char itfs[COLS + 1];

static void handle(const event_t *e) {
    char d[COLS + 1];
    switch (e->type) {
    case EV_LINES:
        row_set(R_LINES, true, C_VELLUM, "USB LINES GP28=%u GP29=%u", e->b[0], e->b[1]);
        break;
    case EV_HOST_UP:
        row_set(R_HOST, true, C_GREEN, "D+ IS GP%u, D- GP%u: HOST STARTED", e->b[0],
                e->b[1] ? e->b[0] - 1 : e->b[0] + 1);
        break;
    case EV_MOUNT:
        itfs[0] = 0;
        row_set(R_DEVICE, true, C_GREEN, "DEVICE %04X:%04X %s SPEED", e->w[0], e->w[1],
                e->b[0] == TUSB_SPEED_FULL ? "FULL" : e->b[0] == TUSB_SPEED_LOW ? "LOW" : "?");
        break;
    case EV_NAME:
        row_set(R_NAME, true, C_GREEN, "NAME %s", e->text);
        break;
    case EV_ITF: {
        char one[16];
        snprintf(one, sizeof one, "%s%u:%02X/%02X", itfs[0] ? " " : "ITF ", e->b[0], e->b[1], e->b[2]);
        if (strlen(itfs) + strlen(one) <= COLS) strcat(itfs, one);
        row_set(R_ITFS, false, C_VELLUM, "%s", itfs);
        char line[64];
        snprintf(line, sizeof line, "interface %u alt %u: class %02X subclass %02X", e->b[0], e->b[3], e->b[1], e->b[2]);
        log_str(line);
        break;
    }
    case EV_MIDI_EP:
        if (e->b[0]) row_set(R_MIDI_EP, true, C_GREEN, "MIDI IN EP %02X SIZE %u: READING", e->b[0], e->w[0]);
        else         row_set(R_MIDI_EP, true, C_RED, "NO MIDI IN ENDPOINT FOUND");
        break;
    case EV_MIDI:
        n_msgs++;
        describe_midi(e->b, d, sizeof d);
        memcpy(rows[R_LAST0], rows[R_LAST1], COLS + 1);
        memcpy(rows[R_LAST1], rows[R_LAST2], COLS + 1);
        row_draw(R_LAST0, C_GREY);
        row_draw(R_LAST1, C_GREY);
        rows[R_LAST2][0] = 0; // force a redraw even if the message repeats
        row_set(R_LAST2, true, C_WHITE, "%s", d);
        break;
    case EV_UMOUNT:
        row_set(R_DEVICE, true, C_RED, "DEVICE UNPLUGGED");
        row_set(R_NAME, false, C_GREY, "");
        row_set(R_ITFS, false, C_GREY, "");
        row_set(R_MIDI_EP, false, C_GREY, "");
        break;
    case EV_ERROR:
        row_set(R_MIDI_EP, true, C_RED, "%s", e->text);
        break;
    }
}

// ---- the serial port to the Mac

static absolute_time_t reboot_at;
static bool reboot_asked;

void tud_cdc_line_coding_cb(uint8_t itf, cdc_line_coding_t const *coding) {
    (void)itf;
    // The 1200-baud touch: `stty -f /dev/cu.usbmodem... 1200` reboots into the bootloader.
    if (coding->bit_rate == 1200) {
        reboot_asked = true;
        reboot_at = make_timeout_time_ms(100);
    }
}

void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts) {
    (void)itf;
    (void)rts;
    if (dtr && !log_open) log_open = true; // snapshot on the next pass of the loop
    if (!dtr) log_open = false;
}

int main(void) {
    // PIO-USB needs the system clock to be a multiple of 12 MHz.
    set_sys_clock_khz(120000, true);

    queue_init(&events, sizeof(event_t), 64);
    uint32_t spi_hz = lcd_init();
    draw_static(spi_hz);

    multicore_reset_core1();
    multicore_launch_core1(core1_main);

    tud_init(0);

    bool snapshot_sent = false;
    absolute_time_t next_tick = make_timeout_time_ms(100);
    uint32_t shown_clock = ~0u, shown_sense = ~0u, shown_drop = ~0u, shown_msgs = ~0u;

    for (;;) {
        tud_task();

        event_t e;
        int handled = 0;
        while (handled < 8 && queue_try_remove(&events, &e)) {
            handle(&e);
            handled++;
            tud_task();
        }

        if (log_open && !snapshot_sent) {
            log_snapshot();
            snapshot_sent = true;
        }
        if (!log_open) snapshot_sent = false;
        if (tud_cdc_available()) {
            uint8_t junk[64];
            tud_cdc_read(junk, sizeof junk);
            log_snapshot(); // any key: the whole status again
        }

        if (time_reached(next_tick)) {
            next_tick = make_timeout_time_ms(100);
            if (n_msgs != shown_msgs || n_clock != shown_clock || n_sense != shown_sense || n_dropped != shown_drop) {
                shown_msgs = n_msgs; shown_clock = n_clock; shown_sense = n_sense; shown_drop = n_dropped;
                row_set(R_COUNTS, false, C_VELLUM, "MSGS %lu CLOCK %lu SENSE %lu DROP %lu",
                        (unsigned long)shown_msgs, (unsigned long)shown_clock,
                        (unsigned long)shown_sense, (unsigned long)shown_drop);
            }
            uint16_t tx, ty;
            if (touch_read(&tx, &ty)) row_set(R_TOUCH, false, C_WHITE, "TOUCH X %4u Y %4u", tx, ty);
            else row_set(R_TOUCH, false, C_GREY, "TOUCH - UP %lus", (unsigned long)(to_ms_since_boot(get_absolute_time()) / 1000));
        }

        tud_cdc_write_flush();

        if (reboot_asked && time_reached(reboot_at)) {
            row_set(R_HOST, false, C_ORANGE, "REBOOTING INTO THE BOOTLOADER");
            sleep_ms(50);
            reset_usb_boot(0, 0);
        }
    }
}
