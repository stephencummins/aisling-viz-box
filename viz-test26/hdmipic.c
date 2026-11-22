// (test 24) The landscape out of the HDMI socket: its 120 x 68 paint cells made into lines of TMDS, a row at a time,
// while the picture is being sent.
//
// There is no room to keep a picture (a frame of TMDS is over a megabyte, the board has 7 KB spare) and no time to
// work every pixel out (a line is 8000 ticks of the clock and holds 1920 symbols). So:
//   - pixels go in pairs. A pair's two symbols are looked up together, 20 bits, chosen so that between them they
//     send as many ones as zeroes: then any pair can follow any other, and nothing has to be carried along the
//     line. The two pixels' values differ by one step at most (pic_table finds them; the host check holds every
//     pair to tmds.c's coding and to a decoder written apart from it).
//   - a level is 6 bits, 0 to 63, with a 2 x 2 dither on the way down from the blend.
//   - a row of the picture is four lines of the screen's, sent from one buffer four times. So the picture is
//     320 x 120, every point of it blended from the four cells around it: more points than cells both ways.
//   - 3 cells make 8 pairs, with the same eight weights every time, and 8 pairs are 160 bits: 5 words exactly. A row
//     is 40 such groups, written out in full below.
// The LCD's canvas grain and its warmth are not here: the grain is finer than this picture's points, and the warmth
// is a correction for that panel.
#include "hdmipic.h"
#include "tmds.h"

#ifdef VIZ_BOARD
#include "pico.h"
#define PIC_RAM(f) __not_in_flash_func(f)   // in RAM: it runs inside the line interrupt
#else
#define PIC_RAM(f) f
#endif

static uint32_t TM[PIC_LEVELS];
static uint32_t BLACK[5];                 // a group of black
static uint32_t VRB[PIC_CX + 2];          // the row: red | blue << 16, in eighths; cell c at [c + 1], ends doubled
static uint32_t VG[PIC_CX + 2];
static bool row_black;
static uint32_t row_d0, row_d1;           // this row's dither for even and odd pairs, in both halves of a word

const uint32_t *pic_levels(void) { return TM; }

void pic_table(void) {
    for (int v = 0; v < PIC_LEVELS; v++) {
        int x = ((v > 63 ? 63 : v) * 255 + 31) / 63;
        int best = 1 << 30;
        uint32_t word = 0;
        for (int a = x - 4; a <= x + 4; a++)
            for (int b = x - 4; b <= x + 4; b++) {
                if (a < 0 || a > 255 || b < 0 || b > 255) continue;
                int disparity = 0;
                uint32_t q0 = tmds_data((uint8_t)a, &disparity), q1 = tmds_data((uint8_t)b, &disparity);
                if (disparity) continue;
                int cost = (a > x ? a - x : x - a) + (b > x ? b - x : x - b);
                if (cost < best) { best = cost; word = q0 | q1 << 10; }
            }
        TM[v] = word;
    }
    const uint32_t t = TM[0];
    BLACK[0] = t | t << 20;
    BLACK[1] = t >> 12 | t << 8 | t << 28;
    BLACK[2] = t >> 4 | t << 16;
    BLACK[3] = t >> 16 | t << 4 | t << 24;
    BLACK[4] = t >> 8 | t << 12;
}

void PIC_RAM(pic_prep)(int row, int shape, const uint8_t *r, const uint8_t *g, const uint8_t *b) {
    int top = 0, n = PIC_ROWS;
    if (shape == PIC_BAND) { top = 15; n = 90; }
    int k = row - top;
    row_black = !r || !g || !b || k < 0 || k >= n;
    if (row_black) return;
    // The row's middle, in eighths of a cell from the first row of cells' middle.
    int pos = ((2 * k + 1) * PIC_CY * 8) / (2 * n) - 4;
    if (pos < 0) pos = 0;
    int r0 = pos >> 3, wy = pos & 7;
    if (r0 >= PIC_CY - 1) { r0 = PIC_CY - 1; wy = 0; }
    const int o0 = r0 * PIC_CX, o1 = wy ? o0 + PIC_CX : o0;
    const uint32_t iy = (uint32_t)(8 - wy), uy = (uint32_t)wy;
    const uint8_t *ra = r + o0, *rb = r + o1, *ga = g + o0, *gb = g + o1, *ba = b + o0, *bb = b + o1;
    for (int c = 0; c < PIC_CX; c++) {
        VRB[c + 1] = (ra[c] * iy + rb[c] * uy) | (ba[c] * iy + bb[c] * uy) << 16;
        VG[c + 1] = ga[c] * iy + gb[c] * uy;
    }
    VRB[0] = VRB[1]; VRB[PIC_CX + 1] = VRB[PIC_CX];
    VG[0] = VG[1]; VG[PIC_CX + 1] = VG[PIC_CX];
    // The dither: a quarter, three quarters, a half and none of a level's step (512), by pair and row.
    const uint32_t d0 = (row & 1) ? 448 : 64, d1 = (row & 1) ? 192 : 320;
    row_d0 = d0 | d0 << 16;
    row_d1 = d1 | d1 << 16;
}

void PIC_RAM(pic_groups)(int g0, int g1, uint32_t *lane0, uint32_t *lane1, uint32_t *lane2) {
    uint32_t *o0 = lane0 + PIC_WORD0 + 5 * g0, *o1 = lane1 + PIC_WORD0 + 5 * g0, *o2 = lane2 + PIC_WORD0 + 5 * g0;
    if (row_black) {
        for (int i = (g1 - g0) * 5 - 1; i >= 0; i--) o0[i] = o1[i] = o2[i] = BLACK[i % 5];
        return;
    }
    const uint32_t d0 = row_d0, d1 = row_d1;
    const uint32_t *tm = TM;
    for (int gr = g0; gr < g1; gr++, o0 += 5, o1 += 5, o2 += 5) {
        // Cells 3 gr - 1 to 3 gr + 3. A pair's middle is 3/16, 9/16, 15/16 ... of a cell along, so it takes its two
        // nearest cells by sixteenths: 5:11, 15:1, 9:7, 3:13, 13:3, 7:9, 1:15, 11:5.
        const uint32_t *v = VRB + 3 * gr, *w = VG + 3 * gr;
        const uint32_t a0 = v[0], a1 = v[1], a2 = v[2], a3 = v[3], a4 = v[4];
        const uint32_t e0 = w[0], e1 = w[1], e2 = w[2], e3 = w[3], e4 = w[4];
        uint32_t r0, r1, r2, r3, r4, r5, r6, r7, g_0, g_1, g_2, g_3, g_4, g_5, g_6, g_7, b0, b1, b2, b3, b4, b5, b6, b7;
#define PAIR(n, A, B, EA, EB, wa, wb, d)                                              \
        {                                                                             \
            const uint32_t W = (A) * (wa) + (B) * (wb) + (d);                         \
            r##n = tm[(W >> 9) & 0x7F];                                               \
            b##n = tm[W >> 25];                                                       \
            g_##n = tm[((EA) * (wa) + (EB) * (wb) + ((d) & 0xFFFF)) >> 9];            \
        }
        PAIR(0, a0, a1, e0, e1, 5, 11, d0)
        PAIR(1, a1, a2, e1, e2, 15, 1, d1)
        PAIR(2, a1, a2, e1, e2, 9, 7, d0)
        PAIR(3, a1, a2, e1, e2, 3, 13, d1)
        PAIR(4, a2, a3, e2, e3, 13, 3, d0)
        PAIR(5, a2, a3, e2, e3, 7, 9, d1)
        PAIR(6, a2, a3, e2, e3, 1, 15, d0)
        PAIR(7, a3, a4, e3, e4, 11, 5, d1)
#undef PAIR
        // Eight pairs of 20 bits into five words, the first bit sent in bit 0.
#define PACK(o, t0, t1, t2, t3, t4, t5, t6, t7)                                       \
        (o)[0] = (t0) | (t1) << 20;                                                   \
        (o)[1] = (t1) >> 12 | (t2) << 8 | (t3) << 28;                                 \
        (o)[2] = (t3) >> 4 | (t4) << 16;                                              \
        (o)[3] = (t4) >> 16 | (t5) << 4 | (t6) << 24;                                 \
        (o)[4] = (t6) >> 8 | (t7) << 12;
        PACK(o0, b0, b1, b2, b3, b4, b5, b6, b7)
        PACK(o1, g_0, g_1, g_2, g_3, g_4, g_5, g_6, g_7)
        PACK(o2, r0, r1, r2, r3, r4, r5, r6, r7)
#undef PACK
    }
}
