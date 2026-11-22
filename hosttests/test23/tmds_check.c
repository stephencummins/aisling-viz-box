// Test 23's check of tmds.c, on the Mac. The decoder here is written from the DVI 1.0 specification's decoder
// (figure 3-6), apart from the encoder, so a slip in one is not repeated in the other.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tmds.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("  FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static int ones(unsigned v, int n) { int c = 0; for (int i = 0; i < n; i++) c += (v >> i) & 1; return c; }
static int changes(unsigned q) { int c = 0; for (int i = 1; i < 10; i++) c += ((q >> i) ^ (q >> (i - 1))) & 1; return c; }

static int changes8(unsigned q) { int c = 0; for (int i = 1; i < 8; i++) c += ((q >> i) ^ (q >> (i - 1))) & 1; return c; }

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

static const unsigned CTRL[4] = {0x354, 0x0AB, 0x154, 0x2AB};   // DVI 1.0, 3.2.2: 1101010100, 0010101011, ...

static void check_line(const char *what, const uint8_t *px, bool sync_lane, bool vsync) {
    uint32_t guarded[TMDS_LINE_WORDS + 2];
    guarded[0] = guarded[TMDS_LINE_WORDS + 1] = 0xDEADBEEF;
    uint32_t *line = guarded + 1;
    memset(line, 0xA5, TMDS_LINE_WORDS * 4);   // tmds_line must not count on a clean buffer
    tmds_line(line, px, sync_lane, vsync);
    CHECK(guarded[0] == 0xDEADBEEF && guarded[TMDS_LINE_WORDS + 1] == 0xDEADBEEF, "%s: wrote outside its words", what);
    int bad = 0, disparity = 0, worst = 0;
    for (int i = 0; i < TMDS_H_TOTAL; i++) {
        unsigned q = symbol(line, i);
        bool blank = i < TMDS_H_BLANK || !px;
        if (blank) {
            bool pulse = i >= 16 && i < 16 + 96;
            unsigned c0 = sync_lane ? !pulse : 0, c1 = sync_lane ? !vsync : 0;
            if (q != CTRL[c1 * 2 + c0]) bad++;
        } else {
            if (decode(q) != px[i - TMDS_H_BLANK]) bad++;
            for (int k = 0; k < 4; k++) if (q == CTRL[k]) bad++;
            disparity += 2 * ones(q, 10) - 10;
            if (abs(disparity) > worst) worst = abs(disparity);
        }
    }
    CHECK(!bad, "%s: %d symbols wrong", what, bad);
    CHECK(worst <= 10, "%s: ran %d ones out of balance", what, worst);
}

int main(void) {
    CHECK(TMDS_H_TOTAL == 800 && TMDS_V_TOTAL == 525 && TMDS_LINE_WORDS == 250 && TMDS_H_TOTAL * 10 % 32 == 0,
          "the timing: 800 x 525, 250 whole words a line");
    for (int k = 0; k < 4; k++) {
        CHECK(tmds_ctrl(k & 1, k >> 1) == CTRL[k], "control symbol %d is %03X", k, tmds_ctrl(k & 1, k >> 1));
        CHECK(changes(CTRL[k]) >= 7, "control symbol %d has too few changes to be told from a value", k);
    }

    // Every value at every balance the encoder can reach.
    bool seen[64] = {false};
    int todo[64], n_todo = 0, n_seen = 0;
    seen[32] = true; todo[n_todo++] = 0;
    int pairs = 0;
    while (n_todo) {
        int start = todo[--n_todo];
        n_seen++;
        for (int d = 0; d < 256; d++) {
            int disp = start;
            unsigned q = tmds_data((uint8_t)d, &disp);
            pairs++;
            CHECK(q < 1024, "value %d: more than 10 bits", d);
            CHECK(decode(q) == (unsigned)d, "value %d at balance %d: %03X decodes to %u", d, start, q, decode(q));
            CHECK(disp - start == 2 * ones(q, 10) - 10, "value %d at balance %d: the count is out", d, start);
            // The point of the coding: the eight bits change at most 4 times (7 for the plain value), so no value
            // can be taken for a control symbol, which changes 7 times or more.
            CHECK(changes8(q) <= 4, "value %d at balance %d: its eight bits change %d times", d, start, changes8(q));
            CHECK(changes(q) <= 6, "value %d at balance %d: %d changes", d, start, changes(q));
            for (int k = 0; k < 4; k++) CHECK(q != CTRL[k], "value %d is sent as a control symbol", d);
            if (disp < -16 || disp > 16) { CHECK(0, "value %d at balance %d: balance runs to %d", d, start, disp); continue; }
            if (!seen[disp + 32]) { seen[disp + 32] = true; todo[n_todo++] = disp; }
        }
    }
    printf("  %d value-and-balance pairs, %d balances reached\n", pairs, n_seen);

    // Lines.
    uint8_t px[TMDS_H_ACTIVE];
    check_line("vertical blanking, lane 0", NULL, true, false);
    check_line("vertical sync, lane 0", NULL, true, true);
    check_line("vertical blanking, lanes 1 and 2", NULL, false, false);
    memset(px, 255, sizeof px);
    check_line("white, lane 0", px, true, false);
    check_line("white, lanes 1 and 2", px, false, false);
    memset(px, 0, sizeof px);
    check_line("black", px, false, false);
    static const uint8_t ON[3] = {0x55, 0x0F, 0x33};
    for (int lane = 0; lane < 3; lane++) {
        for (int i = 0; i < TMDS_H_ACTIVE; i++) px[i] = ((ON[lane] >> (i / 80)) & 1) ? 255 : 0;
        px[0] = px[639] = 255;
        check_line("bars", px, lane == 0, false);
    }
    for (int i = 0; i < TMDS_H_ACTIVE; i++) px[i] = (uint8_t)i;
    check_line("a ramp", px, true, false);
    srand(23);
    for (int n = 0; n < 200; n++) {
        for (int i = 0; i < TMDS_H_ACTIVE; i++) px[i] = (uint8_t)(rand() >> 7);
        check_line("noise", px, n & 1, false);
    }

    // The bars as hdmi.c makes them: white yellow cyan green magenta red blue black, red on lane 2, blue on lane 0.
    static const char *NAME[8] = {"white", "yellow", "cyan", "green", "magenta", "red", "blue", "black"};
    static const uint8_t RGB[8] = {7, 6, 3, 2, 5, 4, 1, 0};   // bit 2 red, bit 1 green, bit 0 blue
    for (int bar = 0; bar < 8; bar++) {
        unsigned rgb = (((ON[2] >> bar) & 1) << 2) | (((ON[1] >> bar) & 1) << 1) | ((ON[0] >> bar) & 1);
        CHECK(rgb == RGB[bar], "bar %d should be %s", bar, NAME[bar]);
    }

    printf(fails ? "tmds_check: %d FAILED\n" : "tmds_check: all passed\n", fails);
    return fails != 0;
}
