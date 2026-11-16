// Test 16's host check: the strip's cells (strip.h), with the pieces' names as main.c has them (pieces.h, cut from
// main.c by run.sh). Nothing of the screen is built: what is checked is where things would be drawn and which cell
// a touch lands in.
#include <stdio.h>
#include <string.h>
#include "strip.h"
#include "pieces.h"

#define LCD_W 480
#define SPARE 10   // pixels a name keeps about it in its cell, both sides together
#define BRICK 12   // the least the strip must stop short of the screen's edge

static int checks, failed;
#define CHECK(c, ...) do { checks++; if (!(c)) { failed++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main(void) {
    // the cells fill the strip exactly, and the strip stops short of the edge
    int sum = 0;
    for (int i = 0; i < STRIP_N; i++) sum += STRIP_CW[i];
    CHECK(sum == STRIP_W, "the cells are %d across, the strip %d", sum, STRIP_W);
    CHECK(STRIP_W <= LCD_W - BRICK, "the strip ends at %d: under the brick", STRIP_W);
    CHECK(sizeof PIECES / sizeof PIECES[0] == STRIP_N, "%zu pieces for %d cells", sizeof PIECES / sizeof PIECES[0], STRIP_N);
    CHECK(strip_x(0) == 0 && strip_x(STRIP_N) == STRIP_W, "the strip runs %d to %d", strip_x(0), strip_x(STRIP_N));
    for (int i = 0; i < STRIP_N; i++) {
        CHECK(strip_x(i + 1) - strip_x(i) == STRIP_CW[i], "cell %d: %d to %d, its width %d", i, strip_x(i), strip_x(i + 1), STRIP_CW[i]);
        CHECK(STRIP_CW[i] >= 48, "cell %d is %d wide: small for a finger", i, STRIP_CW[i]);
    }
    // every pixel of the row: the cell it is in, by its edges
    for (int x = -8; x < LCD_W + 8; x++) {
        int want = -1;
        for (int i = 0; i < STRIP_N; i++)
            if (x >= strip_x(i) && x < strip_x(i + 1)) want = i;
        CHECK(strip_cell(x) == want, "a touch at %d: cell %d, should be %d", x, strip_cell(x), want);
    }
    // the names and the tempos, placed as draw_strip() places them: inside their cells, with room about them
    for (int i = 0; i < STRIP_N; i++) {
        char bpm[12];
        snprintf(bpm, sizeof bpm, "%u BPM", PIECES[i].bpm);
        const char *t[2] = {PIECES[i].name, bpm};
        for (int k = 0; k < 2; k++) {
            int cw = STRIP_CW[i], w = (int)strlen(t[k]) * 6, x = strip_x(i) + (cw - w + 1) / 2;
            CHECK(w + SPARE <= cw, "%s: %d wide in a cell of %d", t[k], w, cw);
            CHECK(x >= strip_x(i) + SPARE / 2 && x + w <= strip_x(i + 1) - SPARE / 2, "%s: drawn %d to %d, its cell %d to %d",
                  t[k], x, x + w, strip_x(i), strip_x(i + 1));
            CHECK(x + w <= LCD_W - BRICK - SPARE / 2, "%s ends at %d: too near the brick", t[k], x + w);
        }
    }
    printf("strip: %d checks, %d failed\n", checks, failed);
    return failed != 0;
}
