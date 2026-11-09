// Replays the FM-1's logged MIDI through land.c at the device's pace (5 frames a second, dt clamped to 6),
// and reports, per band, how much of the picture differs from the bare ground and how much changes per second.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "land.h"
int land_check(float *, float *);
static uint16_t px[LAND_W * LAND_H], prev[LAND_W * LAND_H], ground[LAND_W * LAND_H], tile[LAND_W * LAND_TILE];
static void render_all(void) {
    for (int ty = 0; ty < LAND_TH; ty++) {
        land_render(0, ty * LAND_TILE, LAND_W, LAND_TILE, tile);
        memcpy(&px[ty * LAND_TILE * LAND_W], tile, sizeof tile);
    }
}
static int far(uint16_t a, uint16_t b, int t) {
    int dr = abs(((a >> 11) & 31) - ((b >> 11) & 31)), dg = abs(((a >> 5) & 63) - ((b >> 5) & 63)) / 2, db = abs((a & 31) - (b & 31));
    return dr + dg + db > t;
}
// bands by the layers' own fractions: sky .07-.30, hills .28-.45, fields .42-.66, bog .66-.93
static const float BAND[5] = {0, 0.30f, 0.45f, 0.66f, 1.0f};
static const char *BN[4] = {"sky", "hills", "fields", "bog"};
static void report(const char *what, const uint16_t *a, const uint16_t *b, int t) {
    printf("  %-26s", what);
    for (int q = 0; q < 4; q++) {
        int y0 = (int)(BAND[q] * LAND_H), y1 = (int)(BAND[q + 1] * LAND_H), n = 0, tot = 0;
        for (int y = y0; y < y1; y++) for (int x = 0; x < LAND_W; x++) { tot++; n += far(a[y * LAND_W + x], b[y * LAND_W + x], t); }
        printf(" %s %3.0f%%", BN[q], 100.0 * n / tot);
    }
    printf("\n");
}
static void stats(const uint16_t *a) {
    printf("  %-26s", "mean RGB / luminance sd:");
    for (int q = 0; q < 4; q++) {
        int y0 = (int)(BAND[q] * LAND_H), y1 = (int)(BAND[q + 1] * LAND_H), n = 0;
        double R = 0, G = 0, B = 0, L = 0, L2 = 0;
        for (int y = y0; y < y1; y++) for (int x = 0; x < LAND_W; x++) {
            uint16_t c = a[y * LAND_W + x];
            double r = ((c >> 11) & 31) * 255 / 31.0, g = ((c >> 5) & 63) * 255 / 63.0, b = (c & 31) * 255 / 31.0, l = 0.299 * r + 0.587 * g + 0.114 * b;
            R += r; G += g; B += b; L += l; L2 += l * l; n++;
        }
        double m = L / n;
        printf(" %s %02x%02x%02x/%.0f", BN[q], (int)(R / n), (int)(G / n), (int)(B / n), __builtin_sqrt(L2 / n - m * m));
    }
    printf("\n");
}
int main(int argc, char **argv) {
    FILE *fp = fopen(argv[1], "r");
    float fps = argc > 2 ? (float)atof(argv[2]) : 5;
    unsigned ev[512][3]; int ne = 0; char line[128];
    while (fgets(line, sizeof line, fp) && ne < 512) {
        unsigned cin, st, d1, d2;
        if (sscanf(line, "midi %x %x %x %x", &cin, &st, &d1, &d2) == 4) { ev[ne][0] = st; ev[ne][1] = d1; ev[ne][2] = d2; ne++; }
    }
    fclose(fp);
    const float logsecs = 6;
    land_init(0x9e3779b9u);
    land_set_bpm(76);
    land_prepare(true); render_all(); memcpy(ground, px, sizeof px); memcpy(prev, px, sizeof px);
    float cap = argc > 3 ? (float)atof(argv[3]) : 6, dt = 60 / fps; if (dt > cap) dt = cap;
    int frames = (int)(300 * fps), e = 0; double tsec = 0;
    printf("%d events from the log, replayed for 300 s at %.0f frames a second, dt %.1f\n", ne, fps, dt);
    for (int f = 1; f <= frames; f++) {
        tsec += 1 / fps;
        // events due by now, looping the 6 s log
        while ((double)(e / ne) * logsecs + (double)(e % ne) * logsecs / ne <= tsec) {
            unsigned *m = ev[e % ne];
            if ((m[0] & 0xF0) == 0x90 && m[2]) land_note_on(m[0] & 15, m[1], m[2]);
            else if ((m[0] & 0xF0) == 0x80 || (m[0] & 0xF0) == 0x90) land_note_off(m[0] & 15, m[1]);
            e++;
        }
        land_step(dt);
        land_prepare(false);
        if (f % (int)fps == 0) {
            render_all();
            int s = f / (int)fps;
            if (s == 30 || s == 60 || s == 120 || s == 300) {
                printf("at %2d s, %d blooms live:\n", s, land_blooms_live());
                report("differs from bare ground:", px, ground, 3);
                report("changed in the last second:", px, prev, 3);
                stats(px); { float am, vm; int bad = land_check(&am, &vm); printf("  non-finite %d, most paint %.2f, fastest flow %.3f\n", bad, am, vm); }
            }
            memcpy(prev, px, sizeof px);
        }
    }
    return 0;
}
