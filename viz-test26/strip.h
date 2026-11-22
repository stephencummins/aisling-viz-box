// (test 16) Where the strip's eight pieces are.
//
// Up to test 15 the strip was eight cells of 60 across all 480 of the screen, and TIR NA NOG, ten letters of 6,
// filled the last one to its edge. In the Lego tray a brick on that side covers the screen's last few pixels, and
// the name's end with them. So the strip stops at STRIP_W, 16 short of the edge, and the cells are no longer all
// one width: the long names have what they need, the short ones give some up. Each name keeps 10 or more pixels
// about it. A touch right of STRIP_W is under the brick and is nobody's.
//
// Pure, so the host checks can build it: no screen, no touch.
#ifndef VIZ_STRIP_H
#define VIZ_STRIP_H
#include "ui.h"

#define STRIP_N 8
#define STRIP_W UI_W   // (test 18) 464 of LCD_W's 480: now every page's, so it is ui.h's

//                                  SKELLIG BURREN ARAN CAROLAN BLASKET SAMHAIN CONNEMARA TIR NA NOG
static const unsigned char STRIP_CW[STRIP_N] = {56, 54, 50, 56, 56, 58, 64, 70};

// The left edge of cell i (i = STRIP_N: the strip's end).
static inline int strip_x(int i) {
    int x = 0;
    for (int k = 0; k < i && k < STRIP_N; k++) x += STRIP_CW[k];
    return x;
}

// The cell a touch at x is in, or -1.
static inline int strip_cell(int x) {
    if (x < 0) return -1;
    for (int i = 0; i < STRIP_N; i++) {
        if (x < STRIP_CW[i]) return i;
        x -= STRIP_CW[i];
    }
    return -1;
}

#endif
