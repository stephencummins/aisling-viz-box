// (test 24) The landscape's paint cells made into lines for the HDMI socket. See hdmipic.c. No board code here but
// where the functions are kept (RAM, on the board): the host checks build this file on the Mac.
#ifndef VIZ_HDMIPIC_H_INCLUDED
#define VIZ_HDMIPIC_H_INCLUDED
#include <stdbool.h>
#include <stdint.h>

#define PIC_CX 120          // the paint cells, across and down (land.c's DX and DY)
#define PIC_CY 68
#define PIC_ROWS 120        // the picture's rows: four of the screen's 480 lines each
#define PIC_ROW_LINES 4
#define PIC_GROUPS 40       // a row in groups: 3 cells, 8 pairs of pixels, 16 pixels, 5 words on each lane
#define PIC_WORD0 50        // a line's first word of picture: the blanking before it is 160 symbols, 1600 bits
#define PIC_LEVELS 65       // 0 to 63, and 64 (the same as 63) for a level the dither pushes over the top

// The picture's shape in the 640 x 480 sent. A screen that spreads 640 x 480 over its whole 16:9 face wants the
// whole frame; one that keeps it 4:3 wants the band (the middle 360 lines, black above and below).
enum { PIC_WHOLE, PIC_BAND, PIC_SHAPES };

// Works the table of levels out (tmds.c). Once, before anything else here.
void pic_table(void);
const uint32_t *pic_levels(void);   // PIC_LEVELS words: two symbols, the first in the low ten bits

// Row `row` (0 to PIC_ROWS - 1) of the picture: blends the cells above and below it into a row of its own. r, g, b:
// PIC_CX x PIC_CY cells, a row at a time (any NULL: black). Then pic_groups writes groups [g0, g1) of that row into
// the three lanes' lines (lane 0 blue, 1 green, 2 red), each a whole line of 250 words: only words PIC_WORD0 + 5 g0
// to PIC_WORD0 + 5 g1 - 1 are touched.
void pic_prep(int row, int shape, const uint8_t *r, const uint8_t *g, const uint8_t *b);
void pic_groups(int g0, int g1, uint32_t *lane0, uint32_t *lane1, uint32_t *lane2);

#endif
