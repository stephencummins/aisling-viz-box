// Test 24's check of hdmipic.c, on the Mac. The decoder is test 23's, written from the DVI 1.0 specification's
// decoder (figure 3-6) apart from the encoder; the picture a row should hold is worked out here a point at a time,
// with a division where hdmipic.c has its eight weights written out.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hdmipic.h"
#include "tmds.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { if (fails++ < 20) { printf("  FAIL: " __VA_ARGS__); printf("\n"); } } } while (0)

static int ones(unsigned v, int n) { int c = 0; for (int i = 0; i < n; i++) c += (v >> i) & 1; return c; }

static unsigned decode(unsigned q) {
    unsigned d = q & 0xFF;
    if (q & 0x200) d ^= 0xFF;
    unsigned out = d & 1;
    for (int i = 1; i < 8; i++) {
        unsigned x = ((d >> i) ^ (d >> (i - 1))) & 1;
        if (!(q & 0x100)) x ^= 1;
        out |= x << i;
    }
    return out;
}

// Symbol n of a line, as the serialiser sends it: bit 0 of word 0 first.
static unsigned symbol(const uint32_t *line, int n) {
    unsigned q = 0;
    for (int j = 0; j < 10; j++) {
        int bit = n * 10 + j;
        q |= ((line[bit >> 5] >> (bit & 31)) & 1u) << j;
    }
    return q;
}

static uint8_t R[PIC_CX * PIC_CY], G[PIC_CX * PIC_CY], B[PIC_CX * PIC_CY];
static int level_lo[PIC_LEVELS], level_hi[PIC_LEVELS];   // the two pixels a level is sent as

static int fdiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }   // rounds down

// The level (0 to 64) point (p, row) should have, from one colour's cells.
static int want_level(const uint8_t *cells, int p, int row, int shape) {
    int top = shape == PIC_BAND ? 15 : 0, n = shape == PIC_BAND ? 90 : PIC_ROWS;
    int k = row - top;
    if (k < 0 || k >= n) return -1;   // black
    // down: the row's middle, in eighths of a cell from the top row of cells' middle
    int y8 = fdiv((2 * k + 1) * PIC_CY * 8, 2 * n) - 4;
    if (y8 < 0) y8 = 0;
    if (y8 > (PIC_CY - 1) * 8) y8 = (PIC_CY - 1) * 8;
    int ry = y8 / 8, wy = y8 % 8, ry1 = ry + 1 < PIC_CY ? ry + 1 : ry;
    // across: a point is 3/8 of a cell wide; its middle in sixteenths of a cell from the first cell's middle
    int x16 = (2 * p + 1) * 3 - 8;
    int cx = fdiv(x16, 16), wx = x16 - cx * 16, cx1 = cx + 1;
    if (cx < 0) cx = 0;
    if (cx1 > PIC_CX - 1) cx1 = PIC_CX - 1;
    int left = cells[ry * PIC_CX + cx] * (8 - wy) + cells[ry1 * PIC_CX + cx] * wy;
    int right = cells[ry * PIC_CX + cx1] * (8 - wy) + cells[ry1 * PIC_CX + cx1] * wy;
    static const int D[2][2] = {{64, 320}, {448, 192}};   // [row & 1][point & 1]
    return (left * (16 - wx) + right * wx + D[row & 1][p & 1]) >> 9;
}

static uint32_t line[2][3][TMDS_LINE_WORDS], blank[3][TMDS_LINE_WORDS];
static long points_checked;

static void check_picture(const char *what, int shape) {
    const uint8_t *cells[3] = {B, G, R};   // lane 0 blue, 1 green, 2 red
    for (int row = 0; row < PIC_ROWS; row++) {
        uint32_t (*out)[TMDS_LINE_WORDS] = line[row & 1];
        // as the board does it: three turns
        pic_prep(row, shape, R, G, B);
        pic_groups(0, 10, out[0], out[1], out[2]);
        pic_groups(10, 25, out[0], out[1], out[2]);
        pic_groups(25, PIC_GROUPS, out[0], out[1], out[2]);
        for (int lane = 0; lane < 3; lane++) {
            CHECK(!memcmp(out[lane], blank[lane], PIC_WORD0 * 4), "%s row %d lane %d: the blanking was touched", what,
                  row, lane);
            int disparity = 0;
            for (int p = 0; p < 320; p++) {
                int lv = want_level(cells[lane], p, row, shape);
                if (lv < 0) lv = 0;
                unsigned q0 = symbol(out[lane], TMDS_H_BLANK + 2 * p), q1 = symbol(out[lane], TMDS_H_BLANK + 2 * p + 1);
                int d0 = (int)decode(q0), d1 = (int)decode(q1);
                CHECK(d0 == level_lo[lv] && d1 == level_hi[lv], "%s row %d lane %d point %d: sent %d %d, level %d is %d %d",
                      what, row, lane, p, d0, d1, lv, level_lo[lv], level_hi[lv]);
                // and the stream is what the coding gives for those pixels, its balance carried along the line
                CHECK(q0 == tmds_data((uint8_t)d0, &disparity), "%s row %d lane %d point %d: first symbol not the coding's", what, row, lane, p);
                CHECK(q1 == tmds_data((uint8_t)d1, &disparity), "%s row %d lane %d point %d: second symbol not the coding's", what, row, lane, p);
                points_checked++;
            }
        }
    }
}

int main(void) {
    pic_table();
    const uint32_t *tm = pic_levels();
    int worst = 0;
    for (int v = 0; v < PIC_LEVELS; v++) {
        unsigned q0 = tm[v] & 0x3FF, q1 = (tm[v] >> 10) & 0x3FF;
        CHECK(!(tm[v] >> 20), "level %d: more than 20 bits", v);
        int a = (int)decode(q0), b = (int)decode(q1), x = ((v > 63 ? 63 : v) * 255 + 31) / 63;
        level_lo[v] = a; level_hi[v] = b;
        int disparity = 0;
        CHECK(q0 == tmds_data((uint8_t)a, &disparity) && q1 == tmds_data((uint8_t)b, &disparity) && disparity == 0,
              "level %d: its pair (%d, %d) is not the coding's from level balance, or does not end level", v, a, b);
        CHECK(ones(tm[v], 20) == 10, "level %d: %d ones in 20 bits", v, ones(tm[v], 20));
        int e = abs(a - x) + abs(b - x);
        if (e > worst) worst = e;
        CHECK(abs(a - x) <= 1 && abs(b - x) <= 1, "level %d wants %d, its pixels are %d and %d", v, x, a, b);
        if (v) CHECK(a + b > level_lo[v - 1] + level_hi[v - 1] || v == 64, "level %d is no brighter than %d", v, v - 1);
    }
    CHECK(level_lo[63] + level_hi[63] >= 509, "white is %d %d", level_lo[63], level_hi[63]);
    CHECK(level_lo[0] + level_hi[0] <= 1, "black is %d %d", level_lo[0], level_hi[0]);
    CHECK(tm[64] == tm[63], "level 64 is not 63");
    printf("  65 levels: every pair is the coding's, ends level, and is within 1 of its value (black %d %d, white %d %d)\n",
           level_lo[0], level_hi[0], level_lo[63], level_hi[63]);

    static const uint8_t black[TMDS_H_ACTIVE];
    for (int lane = 0; lane < 3; lane++) {
        tmds_line(blank[lane], black, lane == 0, false);
        for (int s = 0; s < 2; s++) memcpy(line[s][lane], blank[lane], sizeof blank[lane]);
    }
    srand(24);
    for (int i = 0; i < PIC_CX * PIC_CY; i++) { R[i] = (uint8_t)rand(); G[i] = (uint8_t)rand(); B[i] = (uint8_t)rand(); }
    check_picture("noise", PIC_WHOLE);
    check_picture("noise", PIC_BAND);
    for (int y = 0; y < PIC_CY; y++)
        for (int x = 0; x < PIC_CX; x++) {
            int i = y * PIC_CX + x;
            R[i] = (uint8_t)(x * 255 / (PIC_CX - 1)); G[i] = (uint8_t)(y * 255 / (PIC_CY - 1)); B[i] = 255;
        }
    check_picture("slopes", PIC_WHOLE);
    check_picture("slopes", PIC_BAND);
    memset(R, 255, sizeof R); memset(G, 255, sizeof G); memset(B, 255, sizeof B);
    check_picture("white", PIC_WHOLE);
    memset(R, 0, sizeof R); memset(G, 0, sizeof G); memset(B, 0, sizeof B);
    check_picture("black", PIC_WHOLE);
    // no cells yet: black, and nothing read
    pic_prep(5, PIC_WHOLE, NULL, NULL, NULL);
    pic_groups(0, PIC_GROUPS, line[0][0], line[0][1], line[0][2]);
    for (int p = 0; p < 640; p++) CHECK((int)decode(symbol(line[0][1], TMDS_H_BLANK + p)) <= 1, "no cells: not black at %d", p);
    printf("  %ld points of picture (noise, slopes, white, black; both shapes): each the level worked out here, sent as the coding has it\n",
           points_checked);
    if (fails) { printf("  %d FAILED\n", fails); return 1; }
    return 0;
}
