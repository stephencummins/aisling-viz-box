// Test 5's pump, main.c's own code (run.sh cuts it out of main.c into pump_block.inc), against a pretend DMA that
// takes a random while to send each run, and a pretend screen it lands on when it's done.
// Checks: no buffer changes while it's being sent (the DMA reads it all the while); no run starts while another is
// going; every window on the landscape; and the screen ends up exactly as the colours say, at each settling point,
// through notes, warmth and palette changes, new grounds, and trips to the calibration screen and back.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "hot.h"
#include "land.h"
#include "tone.h"

static int pass, fail;
static void check(bool ok, const char *what, long at) {
    if (ok) pass++;
    else if (++fail < 20) printf("FAIL %s (at %ld)\n", what, at);
}
static uint32_t rs = 777;
static uint32_t rnd(uint32_t n) { rs = rs * 1664525u + 1013904223u; return (rs >> 8) % n; }
static long now_f;

// ---- the pretend screen and DMA
static bool cal_mode;
static uint16_t scr[LAND_W * LAND_H];
static const uint16_t *dma_src;
static int dx, dy, dw, dh, dma_left, dma_max;
static uint16_t snap[LAND_W * LAND_TILE];
static long pushes, pushes_inside;
static bool inside;   // in land_step or land_prepare
static void dma_done(void) {
    check(!memcmp(dma_src, snap, (size_t)(dw * dh) * 2), "the buffer stayed as it was while it was sent", now_f);
    for (int y = 0; y < dh; y++) memcpy(&scr[(dy + y) * LAND_W + dx], &snap[y * dw], (size_t)dw * 2);
    dma_src = NULL;
}
static bool lcd_push_busy(void) {
    if (dma_src && dma_left-- <= 0) dma_done();
    return dma_src != NULL;
}
static void lcd_push_begin(int x, int y, int w, int h, const uint16_t *px) {
    check(!dma_src, "nothing started while a run was still going", now_f);
    check(x >= 0 && y >= 0 && w > 0 && h > 0 && x + w <= LAND_W && y + h <= LAND_H, "the window on the landscape", now_f);
    check(!cal_mode, "nothing sent on the calibration screen", now_f);
    memcpy(snap, px, (size_t)(w * h) * 2);
    dma_src = px; dx = x; dy = y; dw = w; dh = h;
    dma_left = (int)rnd((uint32_t)dma_max + 1);
    pushes++;
    if (inside) pushes_inside++;
}
static void lcd_push_wait(void) { while (lcd_push_busy()) {} }
static inline void tight_loop_contents(void) {}
static uint32_t fake_us;
static uint32_t time_us_32(void) { return fake_us += 3; }
static void tud_task(void) {}

// ---- MIDI from the log, a few at a time as drain_events would find them
static unsigned ev[512][3];
static int ne, e_next;
static void drain_events(void) {
    for (int k = (int)rnd(3); k > 0; k--) {
        unsigned *m = ev[e_next++ % ne];
        if ((m[0] & 0xF0) == 0x90 && m[2]) land_note_on(m[0] & 15, (uint8_t)m[1], (uint8_t)m[2]);
        else if ((m[0] & 0xF0) == 0x80 || (m[0] & 0xF0) == 0x90) land_note_off(m[0] & 15, (uint8_t)m[1]);
    }
}
static void poll_touch(void) {}

#include "pump_block.inc"

static uint16_t want[LAND_W * LAND_H], tile[LAND_W * LAND_TILE];
static void settled(const char *what) {
    finish_runs();
    lcd_push_wait();
    for (int ty = 0; ty < LAND_TH; ty++) {
        land_render(0, ty * LAND_TILE, LAND_W, LAND_TILE, tile);
        memcpy(&want[ty * LAND_TILE * LAND_W], tile, sizeof tile);
    }
    long off = 0;
    for (int i = 0; i < LAND_W * LAND_H; i++) off += scr[i] != want[i];
    if (off && fail < 20) printf("  %s: %ld pixels off\n", what, off);
    check(!off, "the screen as the colours say", now_f);
}

int main(int argc, char **argv) {
    FILE *fp = fopen(argc > 1 ? argv[1] : "../test2/midilog.txt", "r");
    if (!fp) { printf("FAIL no log\n"); return 1; }
    char line[128];
    while (fgets(line, sizeof line, fp) && ne < 512) {
        unsigned cin, st, d1, d2;
        if (sscanf(line, "midi %x %x %x %x", &cin, &st, &d1, &d2) == 4) { ev[ne][0] = st; ev[ne][1] = d1; ev[ne][2] = d2; ne++; }
    }
    fclose(fp);
    const int maxes[] = {0, 3, 40, 400};   // the DMA instant, quick, slow, very slow (in polls)
    for (int si = 0; si < 4; si++) {
        dma_max = maxes[si];
        memset(scr, 0, sizeof scr);   // black, as after lcd_init
        cal_mode = false;
        forget_runs();
        sent_buf = 1;
        land_init(0x9e3779b9u + (uint32_t)si);
        land_set_yield(yield_pump);
        land_set_bpm(76);
        tone_set(TONE_START);
        bool force = true;
        long settles = 0;
        for (now_f = 1; now_f <= 900; now_f++) {
            // the main loop: events, then (on the landscape) pump, step, prepare, the rest of the list
            drain_events();
            float dt = 3.0f + (float)rnd(40) * 0.1f;
            if (now_f % 97 == 0) dt = 15;
            pump();
            inside = true;
            land_step(dt);
            if (!cal_mode) {
                land_prepare(force);
                force = false;
                inside = false;
                finish_runs();
                take_runs();
            }
            inside = false;
            // keys and taps, at the top of the next turn while the last run may still be going
            int k = (int)(now_f % 300);
            if (k == 40) { tone_set((int)rnd(TONE_STEPS)); if (!cal_mode) force = true; }
            if (k == 90) land_next_palette();
            if (k == 140) land_next_ground();
            if (k == 180) {   // show_cal: forget the list, then the calibration screen draws (waiting for the DMA)
                forget_runs();
                cal_mode = true;
                lcd_push_wait();
                for (int i = 0; i < LAND_W * LAND_H; i++) scr[i] = (uint16_t)(0x1234 + i);
            }
            if (k == 220) { cal_mode = false; force = true; }   // show_land
            if (k == 60 || k == 299) { settled("a settling point"); settles++; }
        }
        cal_mode = false;
        force = true;
        pump(); land_step(4); land_prepare(force); finish_runs(); take_runs();
        settled("the end");
        printf("seed %d, the DMA up to %d polls: %ld pushes, %ld of them (%.0f%%) while the paint stepped or prepared, %ld settles\n",
               si, dma_max, pushes, pushes_inside, 100.0 * pushes_inside / (pushes ? pushes : 1), settles + 1);
        pushes = pushes_inside = 0;
    }
    printf("%d passed, %d failed\n", pass, fail);
    return fail != 0;
}
