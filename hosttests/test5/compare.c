// Test 5's land.c against test 4's, side by side in one program: the same notes, steps and prepares into both, and
// after every one the whole state compared byte for byte (eu and ev aside: test 5 keeps them with the wind added).
// Every picture test 5 renders must be test 4's, pixel for pixel: whole screens at every warmth, and runs of every
// width at every x, aligned to 4 (test 5's fast path) and not (its reference). Test 5 also renders from inside its
// step and prepare (the yield), as the board does, and that mustn't change anything either.
// With "time": no checks, just the speed of the two renders and the runs a frame.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "land.h"
#include "tone.h"
#define DECL(P)                                                                                                    \
    void P##land_init(uint32_t); void P##land_note_on(uint8_t, uint8_t, uint8_t); void P##land_note_off(uint8_t, uint8_t); \
    void P##land_all_off(int); void P##land_set_bpm(float); void P##land_step(float); int P##land_prepare(bool);   \
    bool P##land_next_run(int, int, int *, int *); void P##land_render(int, int, int, int, uint16_t *);           \
    const char *P##land_next_palette(void); const char *P##land_next_ground(void); int P##land_blooms_live(void); \
    const void *P##state(const char *, size_t *);
DECL(t4_)
DECL(t5_)
void t5_land_set_yield(void (*fn)(void));

static int pass, fail;
static void check(bool ok, const char *what, long at) {
    if (ok) pass++;
    else if (++fail < 20) printf("FAIL %s (at %ld)\n", what, at);
}
static uint32_t rs = 12345;
static uint32_t rnd(uint32_t n) { rs = rs * 1664525u + 1013904223u; return (rs >> 8) % n; }

static const char *NAMES[] = {"bufA", "bufR", "bufG", "bufB", "u", "v", "G0", "G1", "G2", "SR", "SG", "SB", "dirty",
                              "blooms", "simT", "windNow", "bpm", "seed", "pal", "gnd", "redraw_all", "colI", "rowJ",
                              "colF", "rowF", "COV", "GRAIN"};
static long state_checks;
static bool same_state(long at) {
    bool all = true;
    for (size_t i = 0; i < sizeof NAMES / sizeof *NAMES; i++) {
        size_t n4, n5;
        const void *a = t4_state(NAMES[i], &n4), *b = t5_state(NAMES[i], &n5);
        bool ok = a && b && n4 == n5 && !memcmp(a, b, n4);
        if (!ok && fail < 20) printf("  state %s differs\n", NAMES[i]);
        all = all && ok;
    }
    state_checks++;
    check(all, "the state", at);
    return all;
}

static uint16_t s4[LAND_W * LAND_H], s5[LAND_W * LAND_H], tile[LAND_W * LAND_TILE];
static void screen(void (*render)(int, int, int, int, uint16_t *), uint16_t *px) {
    for (int ty = 0; ty < LAND_TH; ty++) {
        render(0, ty * LAND_TILE, LAND_W, LAND_TILE, tile);
        memcpy(&px[ty * LAND_TILE * LAND_W], tile, sizeof tile);
    }
}
static long pixels_compared, aligned_runs, other_runs;
static void same_runs(long at, int n) {
    static uint16_t a[LAND_W * LAND_TILE], b[LAND_W * LAND_TILE];
    for (int i = 0; i < n; i++) {
        int al = rnd(2);
        int px = al ? 4 * (int)rnd(LAND_W / 4) : (int)rnd(LAND_W);
        int w = 1 + (int)rnd((uint32_t)(LAND_W - px));
        if (al) { w &= ~3; if (!w) w = 4; if (px + w > LAND_W) px = LAND_W - w; }
        int h = 1 + (int)rnd(LAND_TILE), py = (int)rnd((uint32_t)(LAND_H - h + 1));
        memset(a, 0xA5, sizeof a);
        memset(b, 0x5A, sizeof b);
        t4_land_render(px, py, w, h, a);
        t5_land_render(px, py, w, h, b);
        bool ok = !memcmp(a, b, (size_t)(w * h) * 2) && b[w * h] == 0x5A5A;
        if (!ok && fail < 20) printf("  run x %d y %d w %d h %d\n", px, py, w, h);
        check(ok, "a run's pixels", at);
        if (!(px & 3) && !(w & 3)) aligned_runs++; else other_runs++;
        pixels_compared += w * h;
    }
}
static void same_screen(long at) {
    screen(t4_land_render, s4);
    screen(t5_land_render, s5);
    check(!memcmp(s4, s5, sizeof s4), "the whole screen", at);
    pixels_compared += LAND_W * LAND_H;
}
// What the board does inside the step: render a run now and then. Into scratch, so it can't be seen.
static long yields;
static void yield_render(void) {
    static uint16_t junk[LAND_W * LAND_TILE];
    yields++;
    if (rnd(3)) return;
    int tx = (int)rnd(LAND_TW), n = 1 + (int)rnd((uint32_t)(LAND_TW - tx));
    t5_land_render(tx * LAND_TILE, (int)rnd(LAND_TH) * LAND_TILE, n * LAND_TILE, LAND_TILE, junk);
}

static unsigned ev[512][3];
static int ne;
static void load_log(const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) { printf("FAIL no %s\n", path); exit(1); }
    char line[128];
    while (fgets(line, sizeof line, fp) && ne < 512) {
        unsigned cin, st, d1, d2;
        if (sscanf(line, "midi %x %x %x %x", &cin, &st, &d1, &d2) == 4) { ev[ne][0] = st; ev[ne][1] = d1; ev[ne][2] = d2; ne++; }
    }
    fclose(fp);
}
static void midi(unsigned *m) {
    if ((m[0] & 0xF0) == 0x90 && m[2]) { t4_land_note_on(m[0] & 15, (uint8_t)m[1], (uint8_t)m[2]); t5_land_note_on(m[0] & 15, (uint8_t)m[1], (uint8_t)m[2]); }
    else if ((m[0] & 0xF0) == 0x80 || (m[0] & 0xF0) == 0x90) { t4_land_note_off(m[0] & 15, (uint8_t)m[1]); t5_land_note_off(m[0] & 15, (uint8_t)m[1]); }
}
static long dirty_tiles, run_count, frames_counted, runs_hist[8];
static bool same_lists(long at) {
    long runs = 0, tiles = 0;
    bool ok = true;
    for (int ty = 0; ty < LAND_TH; ty++) {
        int a = 0, b = 0, x4, n4, x5, n5;
        for (;;) {
            bool g4 = t4_land_next_run(ty, a, &x4, &n4), g5 = t5_land_next_run(ty, b, &x5, &n5);
            if (g4 != g5 || (g4 && (x4 != x5 || n4 != n5))) { ok = false; break; }
            if (!g4) break;
            runs++; tiles += n4; a = x4 + n4; b = x5 + n5;
        }
    }
    check(ok, "the dirty list", at);
    dirty_tiles += tiles; run_count += runs; frames_counted++;
    runs_hist[runs < 8 ? 0 : runs < 16 ? 1 : runs < 32 ? 2 : runs < 64 ? 3 : runs < 128 ? 4 : runs < 256 ? 5 : 6]++;
    return ok;
}

static double now_s(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

int main(int argc, char **argv) {
    bool timing = argc > 2 && !strcmp(argv[2], "time");
    load_log(argc > 1 ? argv[1] : "../test2/midilog.txt");
    const uint32_t seeds[] = {0x9e3779b9u, 1, 0xdeadbeefu};
    const int nseeds = timing ? 1 : 3;
    for (int si = 0; si < nseeds; si++) {
        t4_land_init(seeds[si]);
        t5_land_init(seeds[si]);
        t5_land_set_yield(timing ? NULL : yield_render);
        if (!timing) same_state(-1);
        t4_land_set_bpm(76); t5_land_set_bpm(76);
        int r4 = t4_land_prepare(true), r5 = t5_land_prepare(true);
        if (!timing) { check(r4 == r5, "the first prepare's count", 0); same_lists(0); same_screen(0); }
        int e = 0;
        const int frames = timing ? 1500 : 1200;
        for (long f = 1; f <= frames; f++) {
            // ten seconds of the log at 12 frames a second, looping; dt the board's spread: mostly 5, now and then a
            // stall's 15 or a fast frame's 0.05
            double tsec = f / 12.0;
            while ((double)(e / ne) * 6 + (double)(e % ne) * 6 / ne <= tsec) midi(ev[e++ % ne]);
            float dt = f % 97 == 0 ? 15.0f : f % 89 == 0 ? 0.05f : 4.0f + (float)(f % 7) * 0.31f;
            if (f % 300 == 150) { float b = 60.0f + (float)(f % 113); t4_land_set_bpm(b); t5_land_set_bpm(b); }
            if (!timing && f % 400 == 200) { t4_land_next_palette(); t5_land_next_palette(); }
            if (!timing && f % 500 == 250) { t4_land_next_ground(); t5_land_next_ground(); }
            if (!timing && f % 450 == 300) { int ch = (int)(f % 5) - 1; t4_land_all_off(ch); t5_land_all_off(ch); }
            t4_land_step(dt);
            t5_land_step(dt);
            if (!timing) same_state(f);
            r4 = t4_land_prepare(false);
            r5 = t5_land_prepare(false);
            if (!timing) {
                check(r4 == r5, "a prepare's count", f);
                same_state(f);
            }
            same_lists(f);
            if (!timing) {
                same_runs(f, 8);
                if (f % 60 == 0) {
                    for (int st = 0; st < TONE_STEPS; st++) { tone_set(st); same_screen(f); same_runs(f, 40); }
                    tone_set(TONE_START);
                }
            }
        }
        if (!timing) printf("seed %08x: %d blooms live at the end, %ld yields\n", seeds[si], t5_land_blooms_live(), yields);
    }
    if (timing) {
        // The two renders over the same full screens, many times. The host's not the board, but the ratio's a guide.
        double best4 = 1e9, best5 = 1e9;
        for (int k = 0; k < 9; k++) {
            double t = now_s();
            for (int i = 0; i < 20; i++) screen(t4_land_render, s4);
            double d4 = now_s() - t;
            t = now_s();
            for (int i = 0; i < 20; i++) screen(t5_land_render, s5);
            double d5 = now_s() - t;
            if (d4 < best4) best4 = d4;
            if (d5 < best5) best5 = d5;
        }
        printf("render, a whole screen: test 4 %.0f us, test 5 %.0f us, %.1f times faster (the host, -O2)\n",
               best4 / 20 * 1e6, best5 / 20 * 1e6, best4 / best5);
        double t = now_s();
        for (int i = 0; i < 300; i++) t4_land_step(4.5f);
        double d4 = now_s() - t;
        t = now_s();
        for (int i = 0; i < 300; i++) t5_land_step(4.5f);
        double d5 = now_s() - t;
        printf("step: test 4 %.0f us, test 5 %.0f us (no yield)\n", d4 / 300 * 1e6, d5 / 300 * 1e6);
    }
    printf("per frame, over %ld frames of the log: %.0f dirty tiles in %.1f runs (%.1f tiles a run)\n", frames_counted,
           (double)dirty_tiles / frames_counted, (double)run_count / frames_counted, (double)dirty_tiles / run_count);
    printf("runs a frame: <8 %ld, <16 %ld, <32 %ld, <64 %ld, <128 %ld, <256 %ld, more %ld\n", runs_hist[0], runs_hist[1],
           runs_hist[2], runs_hist[3], runs_hist[4], runs_hist[5], runs_hist[6]);
    if (!timing) {
        printf("%ld state comparisons, %.1f M pixels compared (%ld runs aligned to 4, %ld not)\n", state_checks,
               pixels_compared / 1e6, aligned_runs, other_runs);
        printf("%d passed, %d failed\n", pass, fail);
    }
    return fail != 0;
}
