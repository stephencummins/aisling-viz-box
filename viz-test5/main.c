// AISLING visualiser, test 5: test 4, smoother.
//
// Test 4 drew about 5 frames a second: the paint's step (53 ms), then the changed tiles (139 ms), one after the
// other, all of it run from flash, whose cache core 1's USB host shares. Test 5 changes how fast, not what: every
// pixel it sends is the one test 4 would have sent (the host checks compare the two, bit for bit).
//   1. The step, the colours and the render run from RAM (hot.h).
//   2. The render does four pixels at a time through tables (land.c): 2.7 times as fast on the Mac, more here, where
//      the floating-point maths it replaces is slower.
//   3. The screen's DMA keeps going while the paint steps: the step and the prepare stop every row of paint to let
//      pump() start the next run of tiles and render the one after (land_set_yield). Before, the screen sat idle
//      through the step and the step waited for the screen.
//   4. The log's "frames:" line breaks a frame down: step, prepare, the rest of the tiles, waiting, rendering.
// Everything else is test 4's: gamma FALLING at start-up, warmth 2, the calibration screen, starting on the landscape.
//
// The landscape screen: the landscape in y 0-271, a status row in 272-283, the eight pieces in 284-319.
// A log goes to the Mac over the "USB" socket as a serial port (keys listed in log_snapshot); opening that port at
// 1200 baud reboots the board into its bootloader.
//
// Core 0: the paint, the screen, touch, the serial port (native USB, TinyUSB root port 0).
// Core 1: USB host on PIO-USB (root port 1; D+ GP28, D- GP29, as test 1 found), MIDI in and out, the clock's tempo.
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
#include "hot.h"
#include "land.h"
#include "cal.h"
#include "tone.h"

#define USB_DP 28 // test 1: D+ on GP28, D- on GP29

#define STATUS_Y 272
#define STRIP_Y  284
#define STRIP_H  (LCD_H - STRIP_Y)
#define CELL_W   (LCD_W / 8)

static const struct { const char *name; uint8_t bpm; } PIECES[8] = {
    {"SKELLIG", 76}, {"BURREN", 84},  {"ARAN", 96},      {"CAROLAN", 88},
    {"BLASKET", 70}, {"SAMHAIN", 64}, {"CONNEMARA", 92}, {"TIR NA NOG", 80},
};

// ---------------------------------------------------------------- events, core 1 -> core 0

enum {
    EV_HOST_UP,   // b[0]: D+ pin
    EV_MOUNT,     // w[0] VID, w[1] PID, b[0] speed
    EV_NAME,      // text
    EV_MIDI_EP,   // b[0] IN endpoint, b[1] OUT endpoint (0 = none), w[0], w[1] their sizes
    EV_MIDI,      // b[0..3]: one USB-MIDI event packet (not clock or active sensing)
    EV_UMOUNT,
    EV_ERROR,     // text
    EV_PC_SENT,   // b[0] program, b[1] transfer result
};

typedef struct {
    uint8_t type;
    uint8_t b[4];
    uint16_t w[2];
    char text[24];
} event_t;

static queue_t events;   // core 1 -> core 0
static queue_t to_fm1;   // programs to send, core 0 -> core 1
static volatile uint32_t n_dropped, n_clock;
static volatile uint32_t clock_last_us;  // when the last F8 came
static volatile float clock_iv;          // microseconds between F8s, smoothed

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
static uint8_t midi_buf[64] __attribute__((aligned(4)));
static uint8_t out_buf[4] __attribute__((aligned(4)));
static uint8_t in_ep, out_ep, dev_addr;
static uint16_t in_mps;
static tusb_desc_endpoint_t in_desc, out_desc;
static volatile bool out_busy;

// The web's onClock: intervals under 200 ms count (so a stop and start doesn't), smoothed 0.92 / 0.08.
static void on_clock(void) {
    uint32_t now = time_us_32(), d = now - clock_last_us;
    if (clock_last_us && d < 200000) clock_iv = clock_iv > 0 ? 0.92f * clock_iv + 0.08f * (float)d : (float)d;
    clock_last_us = now;
}

static void midi_read(uint8_t daddr);

static void midi_done(tuh_xfer_t *x) {
    if (x->result != XFER_RESULT_SUCCESS) {
        post_text(EV_ERROR, "MIDI READ FAILED %d", (int)x->result);
        return; // don't spin on a failing endpoint
    }
    for (uint32_t i = 0; i + 4 <= x->actual_len; i += 4) {
        const uint8_t *p = &midi_buf[i];
        if (!(p[0] | p[1] | p[2] | p[3])) continue; // padding
        if ((p[0] & 0x0F) == 0x0F && p[1] == 0xF8) { n_clock++; on_clock(); continue; }
        if ((p[0] & 0x0F) == 0x0F && p[1] == 0xFE) continue;
        event_t e = {.type = EV_MIDI, .b = {p[0], p[1], p[2], p[3]}};
        post(&e);
    }
    midi_read(x->daddr);
}

static void midi_read(uint8_t daddr) {
    tuh_xfer_t x = {
        .daddr = daddr,
        .ep_addr = in_ep,
        .buflen = in_mps > sizeof midi_buf ? sizeof midi_buf : in_mps,
        .buffer = midi_buf,
        .complete_cb = midi_done,
        .user_data = 0,
    };
    if (!tuh_edpt_xfer(&x)) post_text(EV_ERROR, "MIDI READ WOULDN'T START");
}

static void out_done(tuh_xfer_t *x) {
    event_t e = {.type = EV_PC_SENT, .b = {out_buf[2], (uint8_t)x->result}};
    post(&e);
    out_busy = false;
}

static void config_done(tuh_xfer_t *x) {
    if (x->result != XFER_RESULT_SUCCESS || x->actual_len < 9) {
        post_text(EV_ERROR, "CONFIG DESCRIPTOR FAILED");
        return;
    }
    uint32_t total = (uint32_t)cfg_buf[2] | ((uint32_t)cfg_buf[3] << 8);
    if (total > x->actual_len) total = x->actual_len;

    // MIDI Streaming (audio class, subclass 3): the first IN and the first OUT endpoint, bulk by the spec
    // (a few devices use interrupt).
    const uint8_t *p = cfg_buf, *end = cfg_buf + total;
    int cls = -1, sub = -1;
    uint8_t in = 0, out = 0;
    uint16_t out_mps = 0;
    while (p + 2 <= end && p[0] >= 2 && p + p[0] <= end) {
        if (p[1] == TUSB_DESC_INTERFACE && p[0] >= 9) {
            cls = p[5];
            sub = p[6];
        } else if (p[1] == TUSB_DESC_ENDPOINT && p[0] >= 7 && cls == TUSB_CLASS_AUDIO && sub == 3) {
            uint8_t addr = p[2], attr = p[3] & 0x03;
            if (attr == TUSB_XFER_BULK || attr == TUSB_XFER_INTERRUPT) {
                if ((addr & 0x80) && !in) {
                    memcpy(&in_desc, p, sizeof in_desc); // the 7 standard bytes; audio adds 2 more
                    in_desc.bLength = sizeof in_desc;
                    in = addr;
                    in_mps = (uint16_t)(p[4] | (p[5] << 8));
                } else if (!(addr & 0x80) && !out) {
                    memcpy(&out_desc, p, sizeof out_desc);
                    out_desc.bLength = sizeof out_desc;
                    out = addr;
                    out_mps = (uint16_t)(p[4] | (p[5] << 8));
                }
            }
        }
        p += p[0];
    }

    dev_addr = x->daddr;
    if (in && !tuh_edpt_open(x->daddr, &in_desc)) {
        post_text(EV_ERROR, "MIDI IN WOULDN'T OPEN");
        in = 0;
    }
    if (out && !tuh_edpt_open(x->daddr, &out_desc)) {
        post_text(EV_ERROR, "MIDI OUT WOULDN'T OPEN");
        out = 0;
    }
    in_ep = in;
    out_ep = out;
    event_t e = {.type = EV_MIDI_EP, .b = {in, out}, .w = {in_mps, out_mps}};
    post(&e);
    if (in_ep) midi_read(x->daddr);
}

static void name_done(tuh_xfer_t *x) {
    if (x->result == XFER_RESULT_SUCCESS && x->actual_len >= 2 && str_buf[1] == TUSB_DESC_STRING) {
        char name[24];
        uint32_t n = (str_buf[0] < x->actual_len ? str_buf[0] : x->actual_len);
        n = n >= 2 ? (n - 2) / 2 : 0;
        if (n > sizeof name - 1) n = sizeof name - 1;
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
    in_ep = 0;
    out_ep = 0;
    out_busy = false;
    uint8_t prog;
    while (queue_try_remove(&to_fm1, &prog)) {}
    event_t e = {.type = EV_UMOUNT};
    post(&e);
}

static void core1_main(void) {
    pio_usb_configuration_t cfg = PIO_USB_DEFAULT_CONFIG;
    cfg.pin_dp = USB_DP;
    cfg.pinout = PIO_USB_PINOUT_DPDM; // D- = D+ + 1
    event_t e = {.type = EV_HOST_UP, .b = {USB_DP}};
    post(&e);

    tuh_configure(1, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &cfg);
    tuh_init(1);
    for (;;) {
        tuh_task();
        uint8_t prog;
        if (out_ep && !out_busy && queue_try_remove(&to_fm1, &prog)) {
            // Program change on channel 1: AISLING takes 0-7 on any channel, over USB or the jack.
            out_buf[0] = 0x0C;
            out_buf[1] = 0xC0;
            out_buf[2] = prog & 0x7F;
            out_buf[3] = 0;
            tuh_xfer_t x = {
                .daddr = dev_addr,
                .ep_addr = out_ep,
                .buflen = 4,
                .buffer = out_buf,
                .complete_cb = out_done,
                .user_data = 0,
            };
            out_busy = true;
            if (!tuh_edpt_xfer(&x)) {
                out_busy = false;
                post_text(EV_ERROR, "PROGRAM CHANGE WOULDN'T START");
            }
        }
    }
}

// ---------------------------------------------------------------- core 0: the log

static bool log_open, log_midi, log_touch;

static void log_str(const char *s) {
    if (!tud_cdc_connected()) return;
    tud_cdc_write_str(s);
    tud_cdc_write_str("\r\n");
}

void say(const char *fmt, ...) {
    char line[320];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    log_str(line);
}

// ---------------------------------------------------------------- core 0: state

static char fm1_name[24], fm1_error[24];
static uint16_t fm1_vid, fm1_pid;
static bool fm1_mounted, fm1_in, fm1_out;
static int last_sent = -1;      // the piece last sent (or the FM-1 said it changed to)
static int pressed_cell = -1;   // the strip cell under a finger now
static bool strip_dirty = true, status_due = true, force_redraw = true;
static uint32_t n_msgs, n_notes;

static int bpm_now(void);

static void handle(const event_t *e) {
    switch (e->type) {
    case EV_HOST_UP:
        say("USB host started: D+ GP%u, D- GP%u", e->b[0], e->b[0] + 1);
        break;
    case EV_MOUNT:
        fm1_mounted = true;
        fm1_vid = e->w[0];
        fm1_pid = e->w[1];
        fm1_error[0] = 0;
        say("device %04X:%04X, %s speed", e->w[0], e->w[1],
             e->b[0] == TUSB_SPEED_FULL ? "full" : e->b[0] == TUSB_SPEED_LOW ? "low" : "?");
        status_due = true;
        break;
    case EV_NAME:
        snprintf(fm1_name, sizeof fm1_name, "%s", e->text);
        say("name: %s", e->text);
        break;
    case EV_MIDI_EP:
        fm1_in = e->b[0] != 0;
        fm1_out = e->b[1] != 0;
        say("MIDI IN endpoint %02X (size %u), OUT endpoint %02X (size %u)", e->b[0], e->w[0], e->b[1], e->w[1]);
        if (!fm1_in) say("no MIDI IN endpoint: nothing will paint");
        if (!fm1_out) say("no MIDI OUT endpoint: the strip can't change the piece");
        status_due = true;
        break;
    case EV_MIDI: {
        n_msgs++;
        uint8_t st = e->b[1], kind = st & 0xF0, ch = st & 0x0F, d1 = e->b[2], d2 = e->b[3];
        if (kind == 0x90) {
            land_note_on(ch, d1, d2);
            if (d2) n_notes++;
        } else if (kind == 0x80) {
            land_note_off(ch, d1);
        } else if (kind == 0xB0 && (d1 == 120 || d1 == 123)) {
            land_all_off(ch);
        } else if (st == 0xFC) {
            land_all_off(-1);
        } else if (kind == 0xC0 && d1 < 8 && d1 != last_sent) {
            last_sent = d1;
            strip_dirty = true;
        }
        if (log_midi) say("midi %02X %02X %02X %02X", e->b[0], st, d1, d2);
        break;
    }
    case EV_UMOUNT:
        fm1_mounted = fm1_in = fm1_out = false;
        fm1_name[0] = 0;
        land_all_off(-1);
        say("device unplugged");
        status_due = true;
        break;
    case EV_ERROR:
        snprintf(fm1_error, sizeof fm1_error, "%s", e->text);
        say("error: %s", e->text);
        status_due = true;
        break;
    case EV_PC_SENT:
        if (e->b[1] == XFER_RESULT_SUCCESS) {
            last_sent = e->b[0];
            say("sent program change %u: %s", e->b[0], e->b[0] < 8 ? PIECES[e->b[0]].name : "?");
        } else {
            say("program change %u FAILED, result %u", e->b[0], e->b[1]);
            snprintf(fm1_error, sizeof fm1_error, "PROGRAM CHANGE FAILED");
            status_due = true;
        }
        strip_dirty = true;
        break;
    }
}

static void drain_events(void) {
    event_t e;
    while (queue_try_remove(&events, &e)) handle(&e);
}

// ---------------------------------------------------------------- the two screens

static bool cal_mode = false;   // starts on the landscape (as test 4); a tap (or key c) brings up calibration
static bool want_cal;           // a tap on the landscape: acted on between frames, not in the middle of one

static bool touching;
static int touch_reads, touch_btn = -1, touch_y;
static uint32_t touch_last_ms;

static void show_land(void) {
    cal_press_show(-1);
    touch_btn = -1;
    touching = false;
    cal_mode = false;
    lcd_fill_rect(0, STATUS_Y, LCD_W, STRIP_Y - STATUS_Y, C_BLACK);
    force_redraw = strip_dirty = status_due = true;
    say("screen: the landscape (tap it to come back to calibration)");
}

static void forget_runs(void);

static void show_cal(void) {
    forget_runs();   // the landscape's tiles still to send would land on the calibration screen
    touching = false;
    pressed_cell = -1;
    cal_mode = true;
    cal_draw();
    say("screen: calibration");
}

// After a panel setting has changed: what to draw again. A reset leaves the screen black.
static void panel_changed(bool reset) {
    if (cal_mode) {
        if (reset) cal_draw();
        else cal_draw_info();
    } else if (reset) {
        force_redraw = strip_dirty = status_due = true;
    }
}

static void cal_button(int b, bool fine) {
    if (b == CAL_LAND) show_land();
    else panel_changed(cal_do(b, fine));
}

// ---------------------------------------------------------------- touch

// A press counts on release (no touch for 60 ms), if it was read at least twice (a single reading can be a glitch).
// It acts where it was last read: on the calibration screen, the button under it; on the landscape screen, the
// piece in the strip, or anywhere on the landscape itself to go back to calibration.

static void send_piece(int i) {
    if (!fm1_out) {
        say("touched %s, but no FM-1 to send it to", PIECES[i].name);
        snprintf(fm1_error, sizeof fm1_error, "NO FM-1 TO SEND TO");
        status_due = true;
        return;
    }
    uint8_t p = (uint8_t)i;
    if (queue_try_add(&to_fm1, &p)) say("sending program change %d: %s", i, PIECES[i].name);
}

static void poll_touch(void) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (touch_down()) {
        int x, y;
        if (touch_screen(&x, &y)) {
            if (!touching) { touching = true; touch_reads = 0; }
            touch_reads++;
            touch_last_ms = now;
            touch_y = y;
            if (cal_mode) {
                int b = cal_button_at(x, y);
                if (b != touch_btn) { touch_btn = b; cal_press_show(b); }
                if (log_touch) say("touch x %d y %d button %d", x, y, b);
            } else {
                int cell = y >= STRIP_Y ? x / CELL_W : -1;
                if (cell != pressed_cell) { pressed_cell = cell; strip_dirty = true; }
                if (log_touch) say("touch x %d y %d cell %d", x, y, cell);
            }
        }
        return;
    }
    if (touching && now - touch_last_ms >= 60) {
        touching = false;
        bool ok = touch_reads >= 2;
        if (cal_mode) {
            int b = touch_btn;
            touch_btn = -1;
            cal_press_show(-1);
            if (ok && b >= 0) cal_button(b, false);
        } else {
            int cell = pressed_cell;
            pressed_cell = -1;
            strip_dirty = true;
            if (ok && cell >= 0) send_piece(cell);
            else if (ok && touch_y < STATUS_Y) want_cal = true;
        }
    }
}

static void draw_strip(void) {
    for (int i = 0; i < 8; i++) {
        int x = i * CELL_W;
        bool pressed = i == pressed_cell, sent = i == last_sent;
        uint16_t bg = pressed ? C_GREY : sent ? C_ORANGE : (i & 1) ? RGB(40, 34, 46) : RGB(26, 22, 30);
        uint16_t fg = sent && !pressed ? C_BLACK : C_VELLUM;
        uint16_t fg2 = sent && !pressed ? C_BLACK : RGB(150, 140, 125);
        lcd_fill_rect(x, STRIP_Y, CELL_W, STRIP_H, bg);
        const char *name = PIECES[i].name;
        int w = (int)strlen(name) * 6;
        lcd_text(x + (CELL_W - w + 1) / 2, STRIP_Y + 8, name, 1, fg, bg);
        char bpm[12];
        snprintf(bpm, sizeof bpm, "%u BPM", PIECES[i].bpm);
        w = (int)strlen(bpm) * 6;
        lcd_text(x + (CELL_W - w + 1) / 2, STRIP_Y + 21, bpm, 1, fg2, bg);
    }
}

// ---------------------------------------------------------------- the demo: notes made here, at 96 BPM

// To tell a screen problem from a USB one: the landscape painted with no FM-1 at all.
static bool demo;
static uint64_t demo_t0;
static uint32_t demo_step, demo_seed = 1;
static uint8_t demo_drone, demo_lead;

static uint32_t drand(uint32_t n) {
    demo_seed = demo_seed * 1664525u + 1013904223u;
    return (demo_seed >> 16) % n;
}

// A note of A Dorian between lo and hi.
static uint8_t pick(int lo, int hi) {
    static const uint8_t MODE[7] = {0, 2, 3, 5, 7, 9, 10};
    for (;;) {
        int n = lo + (int)drand((uint32_t)(hi - lo + 1)), pc = (n - 9 + 120) % 12;
        for (int i = 0; i < 7; i++) if (MODE[i] == pc) return (uint8_t)n;
    }
}

static void demo_play(uint32_t s) {
    if (drand(10) < 6) land_note_on(1, pick(50, 81), (uint8_t)(70 + drand(50)));    // Sequence: 16ths, some
    if (!(s & 1) && drand(10) < 7) land_note_on(3, pick(45, 81), (uint8_t)(60 + drand(50))); // Sequence 2: 8ths
    if (!(s & 15)) {                                                                     // Lead: a note a bar, held
        if (demo_lead) land_note_off(2, demo_lead);
        demo_lead = pick(62, 88);
        land_note_on(2, demo_lead, 90);
    }
    if (!(s & 63)) {                                                                     // Drone: every 4 bars, held
        if (demo_drone) land_note_off(0, demo_drone);
        demo_drone = pick(38, 62);
        land_note_on(0, demo_drone, 100);
    }
}

static void demo_tick(uint64_t now) {
    uint32_t s = (uint32_t)((now - demo_t0) / 156250); // 16ths at 96 BPM
    if (s > demo_step + 4) demo_step = s;              // after a stall, no burst to catch up
    while (demo_step <= s) demo_play(demo_step++);
}

static void demo_set(bool on) {
    demo = on;
    if (on) {
        demo_t0 = time_us_64();
        demo_step = 0;
    } else {
        if (demo_lead) land_note_off(2, demo_lead);
        if (demo_drone) land_note_off(0, demo_drone);
        demo_lead = demo_drone = 0;
    }
    say("demo %s", on ? "ON: notes made here at 96 BPM" : "off");
    status_due = true;
}

// The tempo: the demo's, or the clock's while F8s keep coming (the web counts it stopped after 700 ms).
static int bpm_now(void) {
    if (demo) return 96;
    uint32_t last = clock_last_us;
    float iv = clock_iv;
    if (!last || time_us_32() - last > 700000 || !(iv > 0)) return 0;
    return (int)(60e6f / (24 * iv) + 0.5f);
}

// ---------------------------------------------------------------- the frame

// ---------------------------------------------------------------- the landscape, to the screen

// The changed tiles go out a run at a time (a tile row's neighbouring dirty tiles), each rendered into one buffer
// while the other goes out by DMA. Test 4 did all that after the step, so the screen sat idle through the step and
// the step waited for the screen. Test 5 overlaps them: the step and the prepare call pump() every row of paint
// (land_set_yield), and it starts a rendered run if the DMA is free and renders the next if a buffer is.
// The list is the last frame's dirty tiles, copied (land_prepare starts the next list in the same place). A run
// rendered while the prepare is changing colours may show some of the new ones: harmless, because every tile a
// changed colour touches is on the next list, so the screen still ends up exactly as the colours say.
static uint16_t strip[2][LAND_W * LAND_TILE];
typedef struct { uint8_t x0, ty, n; } run_t;
static run_t runs[LAND_TW * LAND_TH];   // at most a run per tile
static int runs_n, runs_next;           // the list, and the next run to render
static int ready = -1, ready_run;       // a buffer rendered and waiting for the DMA (or -1), and its run
static int sent_buf = 1;                // the buffer the DMA was last given: it may still be reading it

static uint32_t frames, tiles_sum, step_us_sum, prep_us_sum, rest_us_sum, wait_us_sum, render_us_sum, last_tiles;
static uint32_t render_us;              // this frame's, so far

static void HOT(start_ready)(void) {
    const run_t *r = &runs[ready_run];
    lcd_push_begin(r->x0 * LAND_TILE, r->ty * LAND_TILE, r->n * LAND_TILE, LAND_TILE, strip[ready]);
    sent_buf = ready;
    ready = -1;
}

// One turn. Only renders and sends: it runs inside land_step and land_prepare, so it mustn't touch the paint (no
// MIDI, no touch). Returns whether it did anything.
static bool HOT(pump)(void) {
    if (cal_mode) return false;
    bool did = false;
    if (ready >= 0 && !lcd_push_busy()) {
        start_ready();
        did = true;
    }
    if (ready < 0 && runs_next < runs_n) {
        const run_t *r = &runs[runs_next];
        const int b = 1 - sent_buf;
        uint32_t t = time_us_32();
        land_render(r->x0 * LAND_TILE, r->ty * LAND_TILE, r->n * LAND_TILE, LAND_TILE, strip[b]);
        render_us += time_us_32() - t;
        ready = b;
        ready_run = runs_next++;
        if (!lcd_push_busy()) start_ready();
        did = true;
    }
    if (did) tud_task();
    return did;
}

static void yield_pump(void) { (void)pump(); }

// After the prepare: the rest of the list, with MIDI and touch seen to while the DMA is busy. It doesn't wait for
// the last run to go: that carries on into the next frame. Returns the time spent waiting on the screen.
static uint32_t finish_runs(void) {
    uint32_t waited = 0;
    while (runs_next < runs_n || ready >= 0) {
        if (pump()) continue;
        uint32_t t = time_us_32();
        drain_events(); // notes change the paint, not the colours being sent, so this is safe here
        poll_touch();
        while (ready >= 0 && lcd_push_busy()) tight_loop_contents();
        waited += time_us_32() - t;
    }
    drain_events();
    poll_touch();
    return waited;
}

// The next list: the tiles the prepare just found changed.
static void take_runs(void) {
    runs_n = runs_next = 0;
    for (int ty = 0; ty < LAND_TH; ty++) {
        int tx = 0, x0, nt;
        while (land_next_run(ty, tx, &x0, &nt)) {
            runs[runs_n++] = (run_t){(uint8_t)x0, (uint8_t)ty, (uint8_t)nt};
            tx = x0 + nt;
        }
    }
}

static void forget_runs(void) {
    runs_n = runs_next = 0;
    ready = -1;
}

// 20 MHz to start at 120 MHz: test 2 found 30 a bit too light. At 144, asking 30 gets the 24 it starts at (lcd.h).
static int spi_i = LCD_START_MHZ == 20 ? 1 : 2;
#define N_SPEEDS 3
static const uint32_t SPEEDS_MHZ[N_SPEEDS] = {15, 20, 30};

static void draw_status(int fps) {
    char a[16], b[80];
    uint16_t afg;
    if (fm1_error[0]) {
        snprintf(a, sizeof a, "%s", "FM-1 !");
        afg = C_RED;
    } else if (fm1_in) {
        snprintf(a, sizeof a, "%s", "FM-1 OK");
        afg = C_GREEN;
    } else {
        snprintf(a, sizeof a, "%s", fm1_mounted ? "FM-1 ..." : "NO FM-1");
        afg = C_ORANGE;
    }
    int bpm = bpm_now(), wind = (int)(land_wind() * 1000 + 0.5f);
    // At most 68 of the 72 characters. The tile count is in the log's snapshot; WARM took its place here.
    int n = snprintf(b, sizeof b, " %3d BPM  WIND 0.%03d  BLOOMS %2d  %2d FPS  SPI %lu  WARM %d%s%s", bpm, wind,
                     land_blooms_live(), fps, (unsigned long)(lcd_hz() / 1000000), tone_step(),
                     n_dropped ? "  DROPS" : "", demo ? "  DEMO" : "");
    int room = LCD_W / 6 - 8;
    if (n > room) n = room;
    memset(b + n, ' ', (size_t)(room - n));
    b[room] = 0;
    char a8[16];
    snprintf(a8, sizeof a8, "%-8.8s", a);
    lcd_text(0, STATUS_Y + 2, a8, 1, afg, C_BLACK);
    lcd_text(48, STATUS_Y + 2, b, 1, C_GREY, C_BLACK);
}

static void log_snapshot(int fps) {
    uint32_t f = frames ? frames : 1;
    log_str("---- AISLING viz test 5 ----");
    say("screen: %s", cal_mode ? "calibration" : "the landscape");
    char d[256];
    cal_describe(d, sizeof d);
    say("panel: %s", d);
    say("clocks: sys %lu MHz, peri %lu MHz, screen SPI %lu.%lu MHz",
         (unsigned long)(clock_get_hz(clk_sys) / 1000000), (unsigned long)(clock_get_hz(clk_peri) / 1000000),
         (unsigned long)(lcd_hz() / 1000000), (unsigned long)((lcd_hz() / 100000) % 10));
    if (fm1_mounted)
        say("FM-1: %04X:%04X \"%s\", MIDI in %s, out %s", fm1_vid, fm1_pid, fm1_name, fm1_in ? "open" : "NONE",
             fm1_out ? "open" : "NONE");
    else
        log_str("FM-1: not plugged in (PIO-USB, GP28/29)");
    if (fm1_error[0]) say("last error: %s", fm1_error);
    say("tempo %d BPM (%s), wind 0.%03d", bpm_now(), demo ? "demo" : "MIDI clock",
         (int)(land_wind() * 1000 + 0.5f));
    say("MIDI: %lu messages, %lu notes, %lu clocks, %lu dropped", (unsigned long)n_msgs, (unsigned long)n_notes,
         (unsigned long)n_clock, (unsigned long)n_dropped);
    say("paint: %d blooms live, palette %s, ground %s, demo %s", land_blooms_live(), land_palette(), land_ground(),
         demo ? "on" : "off");
    say("frames: %d a second; step %lu ms, prepare %lu, the rest of the tiles %lu (waiting on the screen %lu)", fps,
         (unsigned long)(step_us_sum / f / 1000), (unsigned long)(prep_us_sum / f / 1000),
         (unsigned long)(rest_us_sum / f / 1000), (unsigned long)(wait_us_sum / f / 1000));
    say("  rendering %lu ms of those; %lu tiles of 510 a frame (last %lu), %lu ms of SPI at this speed",
         (unsigned long)(render_us_sum / f / 1000), (unsigned long)(tiles_sum / f), (unsigned long)last_tiles,
         (unsigned long)((uint64_t)(tiles_sum / f) * LAND_TILE * LAND_TILE * 16 * 1000 / lcd_hz()));
    say("piece last sent: %s", last_sent >= 0 ? PIECES[last_sent].name : "none");
    log_str("keys: s SPI speed, p palette, g ground, f redraw, d demo, t touch log, m MIDI log");
    log_str("panel keys: w/W warmth down/up, r/R VRH down/up, v/V VCOM down/up, y gamma preset, [ ] gamma field,");
    log_str("  -/+ its value, b back to the start (FALLING, warmth 2), k the chip's own and no warmth (as test 2),");
    log_str("  c calibration screen, l landscape; any other key: this snapshot");
}

// ---------------------------------------------------------------- the serial port to the Mac

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
    log_open = dtr; // a snapshot when it opens
}

static int fps_shown;

static void keys(void) {
    while (tud_cdc_available()) {
        int32_t c = tud_cdc_read_char();
        if (c < 0) break;
        switch (c) {
        case 's': {
            spi_i = (spi_i + 1) % N_SPEEDS;
            uint32_t got = lcd_set_hz(SPEEDS_MHZ[spi_i] * 1000000);
            lcd_resetup(); // in case a speed the screen couldn't take garbled a command
            if (cal_mode) cal_draw();
            else force_redraw = strip_dirty = status_due = true;
            say("screen SPI asked %lu MHz, got %lu.%lu MHz (s again for the next)", (unsigned long)SPEEDS_MHZ[spi_i],
                 (unsigned long)(got / 1000000), (unsigned long)((got / 100000) % 10));
            break;
        }
        case 'p': say("palette %s", land_next_palette()); break;
        case 'g': say("ground %s", land_next_ground()); break;
        case 'f':
            if (cal_mode) cal_draw();
            else force_redraw = strip_dirty = status_due = true;
            log_str("full redraw");
            break;
        case 'd': demo_set(!demo); break;
        case 't': log_touch = !log_touch; say("touch log %s", log_touch ? "on" : "off"); break;
        case 'm': log_midi = !log_midi; say("MIDI log %s", log_midi ? "on" : "off"); break;
        // The panel: the calibration screen's buttons in finer steps, and on either screen.
        case 'v': cal_button(CAL_VCOM_DN, true); break;
        case 'V': cal_button(CAL_VCOM_UP, true); break;
        case 'r': cal_button(CAL_VRH_DN, true); break;
        case 'R': cal_button(CAL_VRH_UP, true); break;
        case 'y': cal_button(CAL_GAMMA, true); break;
        case '[': panel_changed(cal_field_step(-1)); break;
        case ']': panel_changed(cal_field_step(+1)); break;
        case '-': cal_button(CAL_VAL_DN, true); break;
        case '+': case '=': cal_button(CAL_VAL_UP, true); break;
        case 'w': cal_button(CAL_WARM_DN, true); break;
        case 'W': cal_button(CAL_WARM_UP, true); break;
        case 'b': cal_button(CAL_START, true); break;
        case 'k': cal_button(CAL_CHIP, true); break;
        case 'c': show_cal(); break;
        case 'l': show_land(); break;
        case '\r': case '\n': break;
        default: log_snapshot(fps_shown); break;
        }
    }
}

// The system clock, set at build time (CMakeLists.txt, VIZ_SYS_MHZ): 120 by default, 144 to try. PIO-USB works its
// timing out from it, but it has to be a multiple of 12 MHz; the RP2350 is rated to 150.
#ifndef VIZ_SYS_MHZ
#define VIZ_SYS_MHZ 120
#endif
_Static_assert(VIZ_SYS_MHZ % 12 == 0 && VIZ_SYS_MHZ <= 150, "the clock: a multiple of 12 MHz, within the rating");

int main(void) {
    // The SDK leaves the peripherals on the 48 MHz USB clock; the screen's SPI runs from clk_peri, so give it the
    // system clock. At 120 MHz the SPI's 20 is exact (120 / 6); at 144 it's 18 (144 / 8), and `s` gives 24.
    set_sys_clock_khz(VIZ_SYS_MHZ * 1000, true);
    clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS, VIZ_SYS_MHZ * MHZ, VIZ_SYS_MHZ * MHZ);

    queue_init(&events, sizeof(event_t), 128);
    queue_init(&to_fm1, sizeof(uint8_t), 8);
    tone_set(TONE_START);
    lcd_init(&CAL_START_PANEL);
    land_init(0x9e3779b9u);
    land_set_yield(yield_pump);
    // The screen is black after lcd_init; the landscape, strip and status line draw on the first frame.

    multicore_reset_core1();
    multicore_launch_core1(core1_main);

    tud_init(0);

    bool snapshot_sent = false;
    uint64_t last_us = time_us_64(), period_t0 = last_us;
    uint32_t period_frames = 0;

    for (;;) {
        tud_task();
        drain_events();
        keys();
        poll_touch();

        // The paint runs on both screens, so notes that come during calibration still land. On the calibration screen
        // it steps ten times a second and isn't drawn, which leaves the time between steps to touch.
        uint64_t t0 = time_us_64();
        if (!cal_mode || t0 - last_us >= 100000) {
            float dt = (float)(t0 - last_us) / 16666.7f;
            last_us = t0;
            // After a stall, one big step rather than a jump. Test 2 capped it at 6 frames, which slowed the paint
            // below 10 frames a second; 15 holds it to real time down to 4.
            if (dt > 15) dt = 15;
            if (dt < 0.05f) dt = 0.05f;
            if (demo) demo_tick(t0);
            land_set_bpm((float)bpm_now());
            pump();   // the first of the list's runs, before the step's first row
            land_step(dt);
        }

        if (cal_mode) {
            period_t0 = time_us_64();
            period_frames = 0;
        } else {
            uint64_t t1 = time_us_64();
            last_tiles = (uint32_t)land_prepare(force_redraw);
            force_redraw = false;
            uint64_t t15 = time_us_64();
            uint32_t waited = finish_runs();
            take_runs();
            uint64_t t2 = time_us_64();

            frames++;
            period_frames++;
            tiles_sum += last_tiles;
            step_us_sum += (uint32_t)(t1 - t0);
            prep_us_sum += (uint32_t)(t15 - t1);
            rest_us_sum += (uint32_t)(t2 - t15);
            wait_us_sum += waited;
            render_us_sum += render_us;
            render_us = 0;

            if (strip_dirty) {
                strip_dirty = false;
                draw_strip();
            }
            if (t2 - period_t0 >= 500000 || status_due) {
                if (t2 - period_t0 >= 500000) {
                    fps_shown = (int)((period_frames * 1000000ull + (t2 - period_t0) / 2) / (t2 - period_t0));
                    period_frames = 0;
                    period_t0 = t2;
                }
                status_due = false;
                draw_status(fps_shown);
            }
            // Averages over the last few seconds, for the snapshot.
            if (frames >= 64) {
                frames /= 2; tiles_sum /= 2; step_us_sum /= 2; prep_us_sum /= 2; rest_us_sum /= 2;
                wait_us_sum /= 2; render_us_sum /= 2;
            }
            if (want_cal) {
                want_cal = false;
                show_cal();
            }
        }

        if (log_open && !snapshot_sent) {
            log_snapshot(fps_shown);
            snapshot_sent = true;
        }
        if (!log_open) snapshot_sent = false;

        tud_cdc_write_flush();

        if (reboot_asked && time_reached(reboot_at)) {
            lcd_push_wait();
            lcd_text(0, STATUS_Y + 2, "REBOOTING INTO THE BOOTLOADER", 1, C_ORANGE, C_BLACK);
            sleep_ms(50);
            reset_usb_boot(0, 0);
        }
    }
}
