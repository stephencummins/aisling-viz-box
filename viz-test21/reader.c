// The FM-1's screen reader, heard for the piece (reader.h).
#include <string.h>
#include "reader.h"

// The pieces as AISLING names them (its amb.c), in program-change order: the strip's PIECES in main.c, in capitals.
static const char *const NAMES[8] = {
    "Skellig", "Burren", "Aran", "Carolan", "Blasket", "Samhain", "Connemara", "Tir na nOg",
};

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; }

// A name at t, followed by its comma (announce.c's text: "<name>, <key> <mode>, tempo <n>").
static int name_at(const char *t, uint32_t n) {
    for (int i = 0; i < 8; i++) {
        uint32_t k = (uint32_t)strlen(NAMES[i]), j = 0;
        if (k >= n || t[k] != ',') continue;
        while (j < k && lower((unsigned char)t[j]) == lower((unsigned char)NAMES[i][j])) j++;
        if (j == k) return i;
    }
    return -1;
}

int reader_piece(const char *text, uint32_t n) {
    static const char A[] = "PIECE ", B[] = ". Piece ";   // a change of piece; the whole screen
    const uint32_t a = sizeof A - 1, b = sizeof B - 1;
    if (n >= a && !memcmp(text, A, a)) return name_at(text + a, n - a);
    for (uint32_t i = 0; i + b <= n; i++)
        if (!memcmp(text + i, B, b)) return name_at(text + i + b, n - i - b);
    return -1;
}

uint32_t reader_hello(uint8_t out[12]) {
    // F0 7D 53 52 01 01 F7: HELLO, version 1 (announce.c: ANN_HELLO, ANN_VER), in USB-MIDI's SysEx packets:
    // CIN 4 for three bytes that start or go on, CIN 5 for an end with one byte.
    static const uint8_t H[12] = {0x04, 0xF0, 0x7D, 0x53, 0x04, 0x52, 0x01, 0x01, 0x05, 0xF7, 0x00, 0x00};
    memcpy(out, H, sizeof H);
    return sizeof H;
}

uint32_t reader_where(uint8_t out[8]) {
    // F0 7D 53 52 03 F7: WHERE (announce.c: ANN_WHERE), as CIN 4 then CIN 7, an end with three bytes.
    static const uint8_t W[8] = {0x04, 0xF0, 0x7D, 0x53, 0x07, 0x52, 0x03, 0xF7};
    memcpy(out, W, sizeof W);
    return sizeof W;
}

// A SysEx ended (r->buf: the bytes between F0 and F7).
static int done(reader_t *r) {
    if (r->n < 4 || r->buf[0] != 0x7D || r->buf[1] != 0x53 || r->buf[2] != 0x52) return -1;   // not the reader's
    if (r->buf[3] == 1) {   // HELLO: the FM-1 answering ours
        r->answered++;
        return -1;
    }
    if (r->buf[3] != 16 || r->n < 5) return -1;   // only SAY: flags, then the text
    r->said++;
    r->said_new = true;
    r->said_flags = r->buf[4];
    r->said_n = (uint16_t)(r->n - 5u);
    return reader_piece((const char *)r->buf + 5, r->n - 5u);
}

int reader_packet(reader_t *r, const uint8_t p[4]) {
    uint32_t cin = p[0] & 0x0Fu;
    if (cin < 4 || cin > 7) return -1;
    uint32_t nb = cin == 4 || cin == 7 ? 3 : cin == 6 ? 2 : 1;   // 4: three that start or go on; 5/6/7: an end
    int got = -1;
    for (uint32_t i = 0; i < nb; i++) {
        uint8_t c = p[1 + i];
        if (c == 0xF0) {
            r->in = true;
            r->over = false;
            r->n = 0;
        } else if (c == 0xF7) {
            if (r->in && !r->over) got = done(r);
            r->in = false;
        } else if (c & 0x80) {
            r->in = false;   // any other status byte breaks a SysEx off
        } else if (r->in) {
            if (r->n < READER_MAX) r->buf[r->n++] = c;
            else r->over = true;
        }
    }
    return got;
}
