// (test 23) TMDS: see tmds.h. Written from the DVI 1.0 specification's encoder (figure 3-5) and its table of control
// symbols, not from anyone's code.
#include "tmds.h"
#include <string.h>

uint16_t tmds_ctrl(bool c0, bool c1) {
    // q_out[9:0] for (c1, c0) = 00, 01, 10, 11
    static const uint16_t SYM[4] = {0x354, 0x0AB, 0x154, 0x2AB};
    return SYM[(c1 ? 2 : 0) | (c0 ? 1 : 0)];
}

static int ones8(unsigned v) {
    int n = 0;
    for (int i = 0; i < 8; i++) n += (v >> i) & 1;
    return n;
}

uint16_t tmds_data(uint8_t d, int *disparity) {
    // Stage 1: 9 bits with the fewest changes. Each bit is the one before XOR (or XNOR) the next of d.
    int n1 = ones8(d);
    bool use_xnor = n1 > 4 || (n1 == 4 && !(d & 1));
    unsigned qm = d & 1;
    for (int i = 1; i < 8; i++) {
        unsigned prev = (qm >> (i - 1)) & 1, in = (d >> i) & 1;
        unsigned b = use_xnor ? !(prev ^ in) : (prev ^ in);
        qm |= b << i;
    }
    bool qm8 = !use_xnor;
    // Stage 2: sent as it is or turned over, whichever pulls the count of ones and zeroes back level.
    int q1 = ones8(qm), q0 = 8 - q1, cnt = *disparity;
    unsigned q;
    if (cnt == 0 || q1 == q0) {
        q = (qm8 ? 0x100u : 0x200u) | (qm8 ? qm : (~qm & 0xFFu));
        cnt += qm8 ? (q1 - q0) : (q0 - q1);
    } else if ((cnt > 0 && q1 > q0) || (cnt < 0 && q0 > q1)) {
        q = 0x200u | (qm8 ? 0x100u : 0) | (~qm & 0xFFu);
        cnt += (qm8 ? 2 : 0) + (q0 - q1);
    } else {
        q = (qm8 ? 0x100u : 0) | qm;
        cnt += (qm8 ? 0 : -2) + (q1 - q0);
    }
    *disparity = cnt;
    return (uint16_t)q;
}

static void put(uint32_t *out, int symbol, uint16_t q) {
    int bit = symbol * 10, w = bit >> 5, s = bit & 31;
    out[w] |= (uint32_t)q << s;
    if (s > 22) out[w + 1] |= (uint32_t)q >> (32 - s);
}

void tmds_line(uint32_t *out, const uint8_t *px, bool sync_lane, bool vsync) {
    memset(out, 0, TMDS_LINE_WORDS * sizeof *out);
    // The syncs are low while they pulse and high otherwise; the other two lanes carry 0, 0.
    bool v = sync_lane && !vsync;
    for (int i = 0; i < TMDS_H_BLANK; i++) {
        bool in_pulse = i >= TMDS_H_FRONT && i < TMDS_H_FRONT + TMDS_H_SYNC;
        put(out, i, tmds_ctrl(sync_lane && !in_pulse, v));
    }
    int disparity = 0;
    for (int i = 0; i < TMDS_H_ACTIVE; i++)
        put(out, TMDS_H_BLANK + i, px ? tmds_data(px[i], &disparity) : tmds_ctrl(sync_lane, v));
}
