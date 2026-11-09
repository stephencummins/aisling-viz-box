// reader.c against AISLING's own screen-reader code (fm1-aisling's firmware/src/announce.c, included as the firmware
// includes it): the box's HELLO turns the reader on there; what it then says, packed into USB-MIDI packets the way the
// FM-1 packs them (usb.c: CIN 4 for three bytes that go on, 5/6/7 for an end), names the right piece here, with notes
// and clock arriving between the packets as they can. Then the texts that must not name one.
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "reader.h"

static uint8_t sent[1024];   // what the FM-1 sent, F0..F7 each
static uint32_t sent_n, sent_frames;

static int ann_wire_send(const uint8_t *p, uint32_t n);
#include ANNOUNCE_C
static int ann_wire_send(const uint8_t *p, uint32_t n) {
    if (sent_n + n > sizeof sent) return 1;
    memcpy(sent + sent_n, p, n);
    sent_n += n;
    sent_frames++;
    return 0;
}

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint32_t rng = 1;
static uint32_t rnd(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }

// The FM-1's sent bytes, as its usb.c packs them, into the reader; noise between packets if asked. Returns the last
// piece named (-1 none).
static int feed(reader_t *r, const uint8_t *p, uint32_t n, int noise) {
    int got = -1;
    for (uint32_t i = 0; i < n;) {
        uint32_t k = n - i < 3 ? n - i : 3;
        uint8_t pk[4] = {0};
        pk[0] = (uint8_t)(k == 3 && i + 3 < n ? 4 : k == 3 ? 7 : 4 + k);
        if (p[i + k - 1] != 0xF7 && k < 3) pk[0] = 4;   // (only an end is short)
        memcpy(pk + 1, p + i, k);
        int g = reader_packet(r, pk);
        if (g >= 0) got = g;
        i += k;
        if (noise && rnd() % 3 == 0) {   // a note, a note off, a clock, a CC: none touches the SysEx
            static const uint8_t N[4][4] = {{0x09, 0x90, 60, 100}, {0x08, 0x80, 60, 0}, {0x0F, 0xF8, 0, 0},
                                            {0x0B, 0xB0, 7, 99}};
            CHECK(reader_packet(r, N[rnd() % 4]) == -1, "noise named a piece");
        }
    }
    return got;
}

static const char *const NAMES[8] = {"Skellig", "Burren", "Aran", "Carolan", "Blasket", "Samhain", "Connemara",
                                     "Tir na nOg"};

int main(void) {
    reader_t r = {0};
    uint8_t h[12];

    // 1. The box's HELLO, unpacked from its packets, is one the FM-1's code takes: the reader goes on, the FM-1
    //    answers with its own HELLO, and that answer is counted here.
    uint32_t hn = reader_hello(h), m = 0;
    uint8_t sx[16];
    CHECK(hn == 12, "hello is %u bytes", hn);
    for (uint32_t i = 0; i < hn; i += 4) {
        uint32_t cin = h[i] & 15, nb = cin == 4 || cin == 7 ? 3 : cin == 6 ? 2 : 1;
        CHECK(cin >= 4 && cin <= 7 && (h[i] >> 4) == 0, "hello packet %u: CIN %u", i / 4, cin);
        for (uint32_t j = 0; j < nb; j++) sx[m++] = h[i + 1 + j];
    }
    CHECK(m == 7 && sx[0] == 0xF0 && sx[m - 1] == 0xF7, "hello isn't F0..F7");
    CHECK(!ann_on(), "the reader is on before the hello");
    CHECK(ann_frame(sx + 1, m - 2, 1000) == 1, "announce.c didn't take the hello as the reader's");
    CHECK(ann_on(), "the hello didn't turn the reader on");
    CHECK(sent_frames == 1 && sent[4] == ANN_HELLO, "no HELLO answer");
    CHECK(feed(&r, sent, sent_n, 0) == -1 && r.answered == 1, "the answer wasn't counted (%u)", r.answered);
    CHECK(ann_where(), "a new reader wasn't promised the whole screen");

    // 2. Every piece, as AISLING says a change of piece (sp_piece_text) and as part of the whole screen (sp_page),
    //    through ann_say, with and without noise between packets.
    static const char *const KEYS[] = {"D Minor", "A Dorian", "E Mixolydian", "C# Phrygian", "Bb Major"};
    for (int round = 0; round < 200; round++) {
        int i = (int)(rnd() % 8), noise = round & 1;
        char t[300];
        if (round % 3 == 0)
            snprintf(t, sizeof t, "Home page: Filter 0, Wet 30, Energy 12, Motion 4. Piece %s, %s, tempo %u",
                     NAMES[i], KEYS[rnd() % 5], 60 + rnd() % 60);
        else
            snprintf(t, sizeof t, "PIECE %s, %s, tempo %u", NAMES[i], KEYS[rnd() % 5], 60 + rnd() % 60);
        sent_n = 0;
        ann_say(t, 0, ANN_NOW);
        int got = feed(&r, sent, sent_n, noise);
        CHECK(got == i, "\"%s\" gave %d, not %d", t, got, i);
    }

    // 3. Things the FM-1 says that don't name a piece, or not whole.
    static const char *const NOT[] = {
        "RESET Skellig",                                  // a reset: the same piece
        "PIECE Skellig",                                  // no comma: not the change-of-piece text
        "Home page: Filter 0, Wet 30. Piece Conne",       // cut off (announce.c cuts at a word)
        "Stabs 30", "Chord E minor 7", "Tempo EXT", "PIECE Timewind, D Minor, tempo 96",
        "SAVED", "",
    };
    for (unsigned i = 0; i < sizeof NOT / sizeof NOT[0]; i++) {
        sent_n = 0;
        ann_say(NOT[i], 0, ANN_NOW);
        int got = feed(&r, sent, sent_n, 1);
        CHECK(got == -1, "\"%s\" named %d", NOT[i], got);
    }

    // 4. Broken and foreign SysEx: too long, broken off by a status byte, another maker's, an editor's (link.c, not
    //    the reader's header), and a good one straight after each.
    {
        uint8_t big[400];
        big[0] = 0xF0;
        big[1] = 0x7D, big[2] = 0x53, big[3] = 0x52, big[4] = 16, big[5] = 1;
        memcpy(big + 6, "PIECE Aran, D Minor, tempo 96", 29);
        for (int i = 35; i < 398; i++) big[i] = 'x';
        big[398] = 0xF7;
        CHECK(feed(&r, big, 399, 1) == -1, "a SysEx past READER_MAX was read");
        // (the name whole before the break, so a reader that kept going would find it)
        static const uint8_t BROKE[] = {0xF0, 0x7D, 0x53, 0x52, 16, 1, 'P', 'I', 'E', 'C', 'E', ' ', 'A', 'r', 'a',
                                        'n', ',', 0x90, 60, 100};
        CHECK(feed(&r, BROKE, sizeof BROKE, 0) == -1, "a broken-off SysEx was read");
        static const uint8_t TAIL[] = {' ', 'D', ' ', 'M', 'i', 'n', 'o', 'r', 0xF7};
        CHECK(feed(&r, TAIL, sizeof TAIL, 0) == -1, "the rest of a broken-off SysEx was read");
        static const uint8_t OTHER[] = {0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7};
        CHECK(feed(&r, OTHER, sizeof OTHER, 1) == -1, "a universal SysEx named a piece");
        static const uint8_t LINK[] = {0xF0, 0x7D, 0x41, 0x4C, 16, 1, 'P', 'I', 'E', 'C', 'E', ' ', 'A', 'r', 'a',
                                       'n', ',', 0xF7};
        CHECK(feed(&r, LINK, sizeof LINK, 1) == -1, "another 7D header named a piece");
        sent_n = 0;
        ann_say("PIECE Tir na nOg, D Dorian, tempo 80", 0, ANN_NOW);
        CHECK(feed(&r, sent, sent_n, 1) == 7, "the good one after the broken ones");
    }

    // 5. reader_piece straight: capitals as the strip has them, and the whole screen's text where the name ends it.
    CHECK(reader_piece("PIECE TIR NA NOG, x", 19) == 7, "capitals");
    CHECK(reader_piece(". Piece Aran,", 13) == 2, "the name at the very end");
    CHECK(reader_piece(". Piece Aran", 12) == -1, "no comma at the end");
    CHECK(reader_piece("PIECE Aran, D", 6) == -1, "n respected");
    CHECK(reader_piece("PIECE Arana, D Minor", 20) == -1, "a longer name taken for Aran");
    CHECK(reader_piece("Home page. Piece Skellig Michael, D", 35) == -1, "a name with more after it");

    // 6. The BYE path: a reader gone quiet for 5 s is off (so the box's HELLO every 2 s keeps it on).
    ann_tick(1000 + 4999);
    CHECK(ann_on(), "off before 5 s");
    ann_tick(1000 + 5001);
    CHECK(!ann_on(), "still on after 5 s without a hello");

    printf("reader: %d checks, %d failed (%u SAYs read, %u hellos answered)\n", checks, fails, r.said, r.answered);
    printf(fails ? "reader FAILED\n" : "reader passed\n");
    return fails != 0;
}
