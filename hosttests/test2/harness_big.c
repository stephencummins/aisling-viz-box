// Host check of land.c: no NaN, paint appears where it should, tiles sensible. Prints numbers only.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "land.h"
static uint16_t px[LAND_W * LAND_H], tile[LAND_W * LAND_TILE];
static uint32_t seed = 1;
static uint32_t r(uint32_t n) { seed = seed * 1664525u + 1013904223u; return (seed >> 16) % n; }
static void render_all(void) {
    for (int ty = 0; ty < LAND_TH; ty++) {
        land_render(0, ty * LAND_TILE, LAND_W, LAND_TILE, tile);
        memcpy(&px[ty * LAND_TILE * LAND_W], tile, sizeof tile);
    }
}
static void lum_grid(const char *label) {
    // 8 bands down x 12 across, mean luminance 0-9
    printf("%s\n", label);
    for (int by = 0; by < 8; by++) {
        printf("  ");
        for (int bx = 0; bx < 12; bx++) {
            double s = 0; int n = 0;
            for (int y = by * 34; y < by * 34 + 34; y++)
                for (int x = bx * 40; x < bx * 40 + 40; x++) {
                    uint16_t c = px[y * LAND_W + x];
                    double R = ((c >> 11) & 31) * 255 / 31.0, G = ((c >> 5) & 63) * 255 / 63.0, B = (c & 31) * 255 / 31.0;
                    s += 0.299 * R + 0.587 * G + 0.114 * B; n++;
                }
            printf("%d", (int)(s / n / 25.6));
        }
        printf("\n");
    }
}
static uint16_t ground[LAND_W * LAND_H];
static void diff_bands(void) {
    // fraction of pixels moved from the bare ground, per quarter of the height (sky, hills, fields, bog roughly)
    int band[4] = {0}, tot[4] = {0};
    for (int y = 0; y < LAND_H; y++) for (int x = 0; x < LAND_W; x++) {
        uint16_t a = px[y * LAND_W + x], b = ground[y * LAND_W + x];
        int dr = abs(((a >> 11) & 31) - ((b >> 11) & 31)), dg = abs(((a >> 5) & 63) - ((b >> 5) & 63)) / 2, db = abs((a & 31) - (b & 31));
        int q = y * 4 / LAND_H; tot[q]++;
        if (dr + dg + db > 3) band[q]++;
    }
    printf("  painted: top %.0f%%, upper-mid %.0f%%, lower-mid %.0f%%, bottom %.0f%%\n",
           100.0 * band[0] / tot[0], 100.0 * band[1] / tot[1], 100.0 * band[2] / tot[2], 100.0 * band[3] / tot[3]);
}
int main(int argc, char **argv) {
    int only = argc > 1 ? atoi(argv[1]) : -1; // a single layer (0-3), or -1 for all
    land_init(0x9e3779b9u);
    int n0 = land_prepare(true);
    render_all();
    memcpy(ground, px, sizeof px);
    printf("first prepare(force): %d tiles (expect 510)\n", n0);
    int n1 = land_prepare(false);
    printf("second prepare, nothing playing: %d tiles (expect 0)\n", n1);
    uint8_t lead = 0, drone = 0;
    long tiles = 0; int maxt = 0; double tstep = 0, tprep = 0;
    int frames = 900; // 15 s at 60
    for (int f = 0; f < frames; f += (int)STEP) {
        int s = f * 60 / 64; // ~16ths at 96 BPM is 9.4 frames; good enough: a note every few frames
        if (f % 9 == 0) {
            if ((only < 0 || only == 1) && r(10) < 6) land_note_on(1, 50 + r(32), 70 + r(50));
            if ((only < 0 || only == 3) && (f / 9) % 2 == 0 && r(10) < 7) land_note_on(3, 45 + r(37), 60 + r(50));
            if ((only < 0 || only == 2) && (f / 9) % 16 == 0) { if (lead) land_note_off(2, lead); lead = 62 + r(27); land_note_on(2, lead, 90); }
            if ((only < 0 || only == 0) && (f / 9) % 64 == 0) { if (drone) land_note_off(0, drone); drone = 38 + r(25); land_note_on(0, drone, 100); }
        }
        (void)s;
        land_set_bpm(f < 600 ? 96 : 0);
        clock_t a = clock();
        land_step(STEP);
        clock_t b = clock();
        int n = land_prepare(false);
        clock_t c = clock();
        tstep += (double)(b - a) / CLOCKS_PER_SEC; tprep += (double)(c - b) / CLOCKS_PER_SEC;
        tiles += n; if (n > maxt) maxt = n;
        if (f == 300 || f == 899) {
            render_all();
            int nan = 0;
            printf("frame %d: blooms %d, wind %.3f, tiles this frame %d\n", f, land_blooms_live(), land_wind(), n);
            char lab[64]; snprintf(lab, sizeof lab, "  luminance grid (0 dark - 9 light), frame %d:", f);
            lum_grid(lab); diff_bands(); (void)nan;
        }
    }
    printf("tiles a frame: mean %.0f, max %d (of 510)\n", (double)tiles / frames, maxt);
    printf("Mac time a frame: step %.2f ms, prepare %.2f ms (the M33 at 120 MHz is many times slower)\n",
           1000 * tstep / frames, 1000 * tprep / frames);
    land_all_off(-1);
    for (int f = 0; f < 600; f++) { land_step(1.0f); land_prepare(false); }
    printf("10 s after all off: blooms %d\n", land_blooms_live());
    printf("palette %s -> %s, ground %s -> ", land_palette(), land_next_palette(), land_ground());
    printf("%s; prepare after ground change: %d tiles (expect 510)\n", land_next_ground(), land_prepare(false));
    // velocity 0 counts as note off; note 0 and 127; channel 15
    land_note_on(15, 0, 127); land_note_on(14, 127, 1); land_note_on(15, 0, 0);
    for (int f = 0; f < 120; f++) land_step(STEP); // the biggest step main.c allows
    render_all();
    int odd = 0;
    for (int i = 0; i < LAND_W * LAND_H; i++) if (px[i] == 0xF81F) odd++;
    printf("after big steps and edge notes: blooms %d, magenta pixels %d, wind %.3f%s\n", land_blooms_live(), odd,
           land_wind(), isfinite(land_wind()) ? "" : " NOT FINITE");
    return 0;
}
