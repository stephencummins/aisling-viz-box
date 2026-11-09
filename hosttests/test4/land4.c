// Test 4's warmth in the landscape: red untouched, green and blue scaled by the step's gains, before the dither.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "land.h"
#include "tone.h"
static uint16_t px[LAND_W * LAND_H], tile[LAND_W * LAND_TILE];
static void render_all(void) {
    for (int ty = 0; ty < LAND_TH; ty++) {
        land_render(0, ty * LAND_TILE, LAND_W, LAND_TILE, tile);
        memcpy(&px[ty * LAND_TILE * LAND_W], tile, sizeof tile);
    }
}
static double sum[TONE_STEPS][3];
static uint16_t first[LAND_W * LAND_H];
int main(void) {
    int pass = 0, fail = 0;
    land_init(0x9e3779b9u);
    land_prepare(true);
    for (int i = 0; i < 40; i++) { land_note_on(i % 4, 40 + (i * 7) % 40, 100); land_step(1.0f); }
    for (int st = 0; st < TONE_STEPS; st++) {
        tone_set(st);
        render_all();
        if (!st) memcpy(first, px, sizeof px);
        long redsame = 0;
        for (int i = 0; i < LAND_W * LAND_H; i++) {
            sum[st][0] += px[i] >> 11; sum[st][1] += (px[i] >> 5) & 63; sum[st][2] += px[i] & 31;
            if ((px[i] >> 11) == (first[i] >> 11)) redsame++;
        }
        double gr = sum[st][1] / sum[0][1], br = sum[st][2] / sum[0][2];
        double wg = tone_gain_g(st) / 256.0, wb = tone_gain_b(st) / 256.0;
        printf("step %d: red %s, green x%.3f (gain %.3f), blue x%.3f (gain %.3f)\n", st,
               redsame == LAND_W * LAND_H ? "unchanged" : "CHANGED", gr, wg, br, wb);
        bool ok = redsame == LAND_W * LAND_H && gr > wg - 0.02 && gr < wg + 0.02 && br > wb - 0.03 && br < wb + 0.03;
        if (ok) pass++; else { fail++; printf("FAIL step %d\n", st); }
    }
    tone_set(0); render_all();
    bool same = !memcmp(px, first, sizeof px);
    printf("back to step 0: %s\n", same ? "the same picture as before" : "DIFFERENT");
    if (same) pass++; else fail++;
    printf("%d passed, %d failed\n", pass, fail);
    return fail != 0;
}
