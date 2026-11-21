// Test 19's host check of swap.c: the box's side of Felucca's update.
//
//   swap_check vectors [PACKAGE...]   the messages swap.c makes and what it reads out of messages, as lines of text:
//                                     run.sh compares them with what fm1-aisling's tools/fm1_install.py gives for the
//                                     same inputs (vec.py), the file this is a port of. With packages: their product
//                                     and the logical image's sum.
//   swap_check                        the packing, the packets, the package's head; then whole installs against a
//                                     pretend FM-1 (simdev.h), a millisecond at a time: one that goes through, and
//                                     each way it can end otherwise.
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "swap.h"
#include "simdev.h"

static int checks, failed;
#define CHECK(c, ...) do { checks++; if (!(c)) { if (++failed <= 60) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } } while (0)

static void hex(const uint8_t *p, uint32_t n) { for (uint32_t i = 0; i < n; i++) printf("%02x", p[i]); }
static uint32_t frame(const uint8_t *body, uint32_t n, uint8_t *w) {   // with simdev.h's packing, not swap.c's
    w[0] = 0xF0;
    uint32_t k = d_pack7(body, n, w + 1);
    w[1 + k] = 0xF7;
    return k + 2;
}

// ---------------------------------------------------------------- vectors
static const char *IDS[] = {"FM-1_900", "ota-FM-1_900", "OTA-FM-1_1", "Ota-FM-1_77", "FM-1_353010401", "nounderscore",
                            "FM-1_", "_12", "FM-1_12a", "FM_1_12", "ABCDEFGHIJKLMNOPQRSTU_12345", "ABCDEFGHIJKLMNOPQR_12345",
                            "ABCDEFGHIJKLMNOPQ_12345", "otaFM-1_3", "X_0", ""};
#define N_IDS (int)(sizeof IDS / sizeof *IDS)
static const char *PRODUCTS[] = {"FM-1_900", "FM-1_353010401", "FM-1_391010100", "ABCDEFGHIJKLMNOP_123", "FM-1", "FM-1_x",
                                 "_5", "A_1", ""};
#define N_PRODUCTS (int)(sizeof PRODUCTS / sizeof *PRODUCTS)
static const char *MODELS[][2] = {{"FM-1_900", "ota-FM-1_900"}, {"FM-1_900", "FM-1_1"}, {"FM-1_900", "FM-2_900"},
                                  {"ota-FM-1_900", "ota-FM-1_2"}, {"FM-1_900", "FM-10_900"}, {"ota-X_1", "X_2"},
                                  {"ota-_1", "_1"}, {"FM-1_1", "fm-1_1"}};

static int vectors(int argc, char **argv) {
    static uint8_t in[600], out[700];
    static const uint32_t NS[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 13, 14, 15, 16, 34, 100, 511, 512};
    for (unsigned k = 0; k < sizeof NS / sizeof *NS; k++) {
        for (uint32_t i = 0; i < NS[k]; i++) in[i] = (uint8_t)(i * 37 + 11 + k);
        uint32_t n = swap_pack7(in, NS[k], out);
        printf("pack7 %u ", NS[k]);
        hex(out, n);
        printf("\n");
    }
    static const uint32_t RA[] = {0, 1, 0x3FF, 0x94D00, 0xE0000000u, 0xF0000000u, 0xFFFFFFFFu};
    static const uint32_t RN[] = {0, 1, 8, 47, 64, 256, 511, 512};
    for (unsigned a = 0; a < sizeof RA / sizeof *RA; a++)
        for (unsigned k = 0; k < sizeof RN / sizeof *RN; k++) {
            uint8_t fl = (uint8_t)(a * 16 + k * 31);
            for (uint32_t i = 0; i < RN[k]; i++) in[i] = (uint8_t)(i * 151 + a * 7 + k);
            uint32_t n = swap_response(RA[a], in, RN[k], fl, out);
            printf("resp %08x %u %02x ", RA[a], RN[k], fl);
            hex(out, n);
            printf("\n");
        }
    // read requests: good ones, and each kind of bad one
    for (int k = 0; k < 40; k++) {
        uint8_t b[16] = {0x00, 0x59, 0x30, 8, 0, 0};
        uint32_t n = 15, s = 0;
        for (int i = 6; i < 14; i++) b[i] = (uint8_t)(k * 53 + i * 29);
        if (k == 30) b[10] = 0xF0, b[9] = b[8] = b[7] = 0;
        for (int i = 6; i < 14; i++) s += b[i];
        b[14] = (uint8_t)~s;
        if (k == 31) b[14] ^= 1;
        if (k == 32) b[2] = 0x11;
        if (k == 33) b[1] = 0x58;
        if (k == 34) b[0] = 1;
        if (k == 35) n = 14;
        if (k == 36) n = 16, b[15] = 0;
        if (k == 37) b[3] = 9;    // the length it says isn't looked at
        uint32_t w = frame(b, n, out);
        if (k == 38) out[0] = 0xF1;
        if (k == 39) out[w - 1] = 0x00;
        uint8_t fl;
        uint32_t addr, len;
        printf("req %d ", k);
        hex(out, w);
        if (swap_request(out, w, &fl, &addr, &len)) printf(" %02x %08x %u\n", fl, addr, len);
        else printf(" none\n");
    }
    // identities
    for (int k = 0; k < N_IDS + 6; k++) {
        uint8_t b[35] = {0x00, 0x59, 0x11, 27, 0, 0};
        const char *t = IDS[k < N_IDS ? k : 0];
        uint32_t n = 34, s = 0;
        memcpy(b + 6, t, strlen(t));
        if (k % 2) b[30] = 0xEF, b[31] = 0x40, b[32] = (uint8_t)(0x14 + k);
        if (k == N_IDS) b[6 + 8] = 0, b[6 + 9] = 'Z';        // text after the zero isn't part of it
        if (k == N_IDS + 1) b[2] = 0x30;
        if (k == N_IDS + 2) n = 33;
        if (k == N_IDS + 3) n = 35;
        for (int i = 6; i < 33; i++) s += b[i];
        b[33] = (uint8_t)~s;
        if (k == N_IDS + 4) b[33] ^= 0x55;                    // its sum isn't looked at
        uint32_t w = frame(b, n, out);
        if (k == N_IDS + 5) out[w - 1] = 0x01;
        swap_id_t id;
        printf("ident %d ", k);
        hex(out, w);
        if (swap_identity(out, w, &id)) printf(" [%s] %d %06x\n", id.text, id.loader, id.chip);
        else printf(" none\n");
    }
    for (int k = 0; k < N_PRODUCTS; k++) {
        static uint8_t raw[960];
        char text[SWAP_NAME_MAX];
        make_pkg(raw, sizeof raw, PRODUCTS[k], 0, 100u + (uint32_t)k);
        printf("product %d ", k);
        if (swap_product(raw, text)) printf("[%s]\n", text);
        else printf("none\n");
    }
    for (unsigned k = 0; k < sizeof MODELS / sizeof *MODELS; k++)
        printf("model %s %s %d\n", MODELS[k][0], MODELS[k][1], swap_same_model(MODELS[k][0], MODELS[k][1]));
    for (int a = 2; a < argc; a++) {
        FILE *f = fopen(argv[a], "rb");
        static uint8_t raw[1 << 20], lg[1 << 20];
        if (!f) { printf("pkg can't open\n"); continue; }
        uint32_t n = (uint32_t)fread(raw, 1, sizeof raw, f);
        fclose(f);
        char text[SWAP_NAME_MAX] = "";
        bool ok = n >= 960 && swap_product(raw, text);
        uint32_t st = 0;
        bool mark = swap_mark(&st, raw, n);
        if (ok) {
            swap_head(raw, lg);
            for (uint32_t i = 940; i < n - 20; i++) lg[i] = raw[swap_file_off(i)];
        }
        printf("pkg %u [%s] %d %d %016llx\n", n, text, ok, mark, ok ? (unsigned long long)fnv(lg, n - 20) : 0ull);
    }
    return 0;
}

// ---------------------------------------------------------------- the small parts
static void units(void) {
    static uint8_t in[600], p[700], back[700], w[700];
    for (uint32_t n = 0; n <= 530; n++) {
        lcg_x = n + 1;
        for (uint32_t i = 0; i < n; i++) in[i] = lcg();
        uint32_t k = swap_pack7(in, n, p), top = 0;
        for (uint32_t i = 0; i < k; i++) top |= p[i] & 0x80;
        uint32_t m = swap_unpack7(p, k, back, sizeof back);
        if (k != (n * 8 + 6) / 7 || top || m != n || memcmp(in, back, n)) { CHECK(0, "pack7 and back, %u bytes", n); break; }
        uint8_t mine[700];
        if (d_pack7(in, n, mine) != k || memcmp(mine, p, k)) { CHECK(0, "pack7 against the check's own, %u bytes", n); break; }
    }
    CHECK(1, "pack7");
    // unpack7 never writes past max, and says how much there was
    memset(back, 0xAA, sizeof back);
    memset(p, 0x7F, 100);
    uint32_t m = swap_unpack7(p, 100, back, 10);
    CHECK(m == 87 && back[9] == 0xFF && back[10] == 0xAA, "unpack7 past max: %u %02x %02x", m, back[9], back[10]);

    // an answer as the pretend FM-1's own unpacking reads it
    for (uint32_t n = 0; n <= 512; n += n < 20 ? 1 : 41) {
        for (uint32_t i = 0; i < n; i++) in[i] = (uint8_t)(i * 7 + n);
        memset(w, 0xEE, sizeof w);
        uint32_t k = swap_response(0x12345678u, in, n, 0xA5, w);
        uint8_t u[600];
        uint32_t un = d_unpack7(w + 1, k - 2, u), s = 0;
        for (uint32_t i = 6; i + 1 < un; i++) s += u[i];
        CHECK(k <= SWAP_WIRE_MAX && w[0] == 0xF0 && w[k - 1] == 0xF7 && w[k] == 0xEE && un == n + 15 &&
              u[0] == 0 && u[1] == 0x59 && u[2] == 0x30 && (uint32_t)(u[3] | u[4] << 8) == n + 8 && u[6] == 0xA5 &&
              u[7] == 0x78 && u[8] == 0x56 && u[9] == 0x34 && u[10] == 0x12 && (uint32_t)(u[11] | u[12] << 8) == n &&
              !memcmp(u + 14, in, n) && u[un - 1] == (uint8_t)~s, "an answer of %u bytes (%u on the wire)", n, k);
    }
    {
        uint32_t k = swap_response(0, in, 512, 0, w);
        CHECK(k == 605, "512 bytes are %u on the wire, not 605", k);
    }

    // the package's head
    static uint8_t raw[5000], lg[5000], mine[5000];
    char text[SWAP_NAME_MAX];
    make_pkg(raw, sizeof raw, "FM-1_353010401", 3000, 7);
    uint32_t ln = make_logical(raw, sizeof raw, mine);
    CHECK(swap_product(raw, text) && !strcmp(text, "FM-1_353010401"), "the product: [%s]", text);
    swap_head(raw, lg);
    CHECK(!memcmp(lg, mine, 940), "the head made logical");
    int off_bad = 0;
    for (uint32_t i = 0; i < ln; i++) off_bad += raw[swap_file_off(i)] != mine[i];
    CHECK(!off_bad && swap_file_off(0) == 0 && swap_file_off(46) == 46 && swap_file_off(47) == 48 &&
          swap_file_off(939) == 958 && swap_file_off(940) == 960, "where a logical byte is in the file: %d wrong", off_bad);
    make_pkg(raw, sizeof raw, "FM-1_353010401_", 0, 7);
    CHECK(!swap_product(raw, text), "a product with a second _");
    make_pkg(raw, sizeof raw, "FM-1_12345678901234X", 0, 7);
    CHECK(!swap_product(raw, text), "20 letters, the last not a digit");
    make_pkg(raw, sizeof raw, "FM-1_123456789012345", 0, 7);
    CHECK(swap_product(raw, text) && strlen(text) == 20, "20 letters");
    make_pkg(raw, sizeof raw, "FM-1_9", 0, 7);
    raw[2 * 48 + 47] = 3;   // a zero for a letter
    CHECK(!swap_product(raw, text), "a zero in the product");

    // the loader's name, through pieces cut anywhere
    static const char M[] = "FELUCCA-LOADER-1";
    int mark_bad = 0;
    for (uint32_t at = 0; at < 100; at += 7)
        for (uint32_t cut = 1; cut < 140; cut++) {
            uint8_t b[160];
            memset(b, 'x', sizeof b);
            memcpy(b + at, M, 16);
            uint32_t st = 0;
            bool a = swap_mark(&st, b, cut), z = swap_mark(&st, b + cut, sizeof b - cut);
            if (a != (cut >= at + 16) || !z) mark_bad++;
        }
    CHECK(!mark_bad, "the loader's name across a cut: %d wrong", mark_bad);
    static const struct { const char *s; bool yes; } MK[] = {
        {"FELUCCA-LOADER-1", 1}, {"FFELUCCA-LOADER-1", 1}, {"FELUCCFELUCCA-LOADER-1", 1}, {"FELUCCA-LOADER-2", 0},
        {"FELUCCA-LOADER-", 0}, {"ELUCCA-LOADER-1", 0}, {"FELUCCA-LOADERFELUCCA-LOADER-1", 1}, {"felucca-loader-1", 0},
        {"FELUCCA LOADER-1", 0}, {"", 0}};
    for (unsigned i = 0; i < sizeof MK / sizeof *MK; i++) {
        uint32_t st = 0;
        bool got = false;
        for (const char *c = MK[i].s; *c; c++) got = swap_mark(&st, (const uint8_t *)c, 1);   // a byte at a time
        CHECK(got == MK[i].yes, "the loader's name in \"%s\": %d", MK[i].s, got);
    }

    // packets: a message of every length out and back together
    int pk_bad = 0;
    for (uint32_t n = 2; n <= 640; n++) {
        static uint8_t pk[1000];
        uint8_t got[700];
        uint32_t at = 0, o = 0, g = 0, max = 4 * (1 + n % 16);
        w[0] = 0xF0;
        for (uint32_t i = 1; i + 1 < n; i++) w[i] = (uint8_t)((i * 13 + n) & 0x7F);
        w[n - 1] = 0xF7;
        while (at < n) {
            uint32_t k = swap_packets(w, n, &at, pk + o, max);
            if (!k || k % 4 || k > max) { pk_bad++; break; }
            o += k;
        }
        for (uint32_t i = 0; i < o; i += 4) {   // by the USB-MIDI rule, written out here again
            int cin = pk[i], k = cin == 4 || cin == 7 ? 3 : cin == 6 ? 2 : cin == 5 ? 1 : -1;
            if (k < 0 || (cin != 4 && i + 4 != o) || (cin == 4 && i + 4 == o)) { pk_bad++; break; }
            for (int j = 0; j < k; j++) got[g++] = pk[i + 1 + j];
            for (int j = k; j < 3; j++) pk_bad += pk[i + 1 + j] != 0;
        }
        if (g != n || memcmp(got, w, n) || o != (n + 2) / 3 * 4) pk_bad++;
        if (n <= SWAP_MSG_MAX) {
            swap_col_t c = {0};
            int r = 0;
            for (uint32_t i = 0; i < o; i += 4) {
                if (r) pk_bad++;
                r = swap_collect(&c, pk + i);
            }
            if (r != (int)n || memcmp(c.buf, w, n)) pk_bad++;
        }
    }
    CHECK(!pk_bad, "packets: %d wrong", pk_bad);
    {
        swap_col_t c = {0};
        int r = 0;
        static const uint8_t LONG_[4] = {0x04, 0x11, 0x22, 0x33}, START[4] = {0x04, 0xF0, 0x01, 0x02},
                             END[4] = {0x06, 0x03, 0xF7, 0}, NOTE[4] = {0x09, 0x90, 0x40, 0x7F},
                             CUT[4] = {0x04, 0x05, 0x90, 0x06}, CLOCK[4] = {0x0F, 0xF8, 0, 0},
                             ONE[4] = {0x05, 0xF7, 0, 0};
        r |= swap_collect(&c, LONG_);   // the middle of one whose start was missed
        r |= swap_collect(&c, END);
        CHECK(r == 0, "a message without its start: %d", r);
        r = swap_collect(&c, START);
        for (int i = 0; i < 20 && !r; i++) r = swap_collect(&c, LONG_);   // 63 bytes: too long
        r |= swap_collect(&c, END);
        CHECK(r == 0, "a message too long to keep: %d", r);
        r = swap_collect(&c, START) | swap_collect(&c, NOTE) | swap_collect(&c, CLOCK);
        int e = swap_collect(&c, END);
        CHECK(r == 0 && e == 5 && c.buf[0] == 0xF0 && c.buf[1] == 1 && c.buf[2] == 2 && c.buf[3] == 3 && c.buf[4] == 0xF7,
              "a note and a clock between a message's packets: %d %d", r, e);
        r = swap_collect(&c, START) | swap_collect(&c, CUT) | swap_collect(&c, END);
        CHECK(r == 0, "a message cut short by another status: %d", r);
        r = swap_collect(&c, START);
        e = swap_collect(&c, ONE);
        CHECK(r == 0 && e == 4, "the one after is whole: %d", e);
    }
    CHECK(swap_name_ok("FM-1_900") && !swap_name_ok("FM-1") && !swap_name_ok("_1") && !swap_name_ok("A_") &&
          !swap_name_ok("A_1_2") && !swap_name_ok(""), "names");
}

// ---------------------------------------------------------------- whole installs
#define IMG_MAX 700000u
static uint8_t raw_pkg[IMG_MAX + 20], image[IMG_MAX];
static uint32_t image_len;
static const char PRODUCT[] = "FM-1_353010401";

static fm1_t dev;
static swap_t sw;
static uint32_t now_ms;
static struct { uint8_t b[80]; uint32_t n; } fifo[64];   // what the FM-1 sent, given to the box on the next pass
static int fh, ft;
static struct {
    int reads, read_oob, fail_from, fail_to;   // the card: reads of these numbers fail
    int offers, sends, busy;                   // the USB: only every busy'th offer is taken (0: all)
    int drop_send;                             // this send is lost on the way
    int hs, up, other;
    bool cable_out;
    uint32_t enum_ms;                          // from on the bus to the box knowing
    uint32_t t_on;
    bool on, told;
    uint32_t t_end;
    int late_sends;                            // sent after the install ended
} h;

static void to_host(const uint8_t *w, uint32_t n) {
    if (n > sizeof fifo[0].b || (ft + 1) % 64 == fh) { printf("FAIL: harness fifo\n"); exit(2); }
    memcpy(fifo[ft].b, w, n);
    fifo[ft].n = n;
    ft = (ft + 1) % 64;
}
static bool io_read(void *ctx, uint32_t logical, uint8_t *dst, uint32_t n) {
    (void)ctx;
    h.reads++;
    if ((uint64_t)logical + n > image_len || n > SWAP_MAXDATA) { h.read_oob++; return false; }
    if (h.reads >= h.fail_from && h.reads < h.fail_to) return false;
    memcpy(dst, image + logical, n);
    return true;
}
static bool io_send(void *ctx, const uint8_t *w, uint32_t n) {
    static const uint8_t HS[] = {0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7};
    static const uint8_t UP[] = {0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7};
    (void)ctx;
    h.offers++;
    if (h.busy && h.offers % h.busy) return false;
    h.sends++;
    if (!swap_busy(&sw)) h.late_sends++;
    if (n == sizeof HS && !memcmp(w, HS, n)) h.hs++;
    else if (n == sizeof UP && !memcmp(w, UP, n)) h.up++;
    else h.other++;
    if (h.sends == h.drop_send) return true;
    if (!h.cable_out) dev_rx(&dev, w, n, now_ms);
    return true;
}
static const swap_io_t IO = {io_read, io_send, NULL};

static void setup(uint32_t len, const char *app, const char *after) {
    image_len = len;
    dev_init(&dev, image, image_len, app, after, to_host);
    memset(&h, 0, sizeof h);
    h.fail_from = h.fail_to = -1;
    h.enum_ms = 200;
    h.on = h.told = true;
    fh = ft = 0;
    now_ms = 1000;
}
static void pass(void) {
    bool on = dev_on_bus(&dev) && !h.cable_out;
    if (on != h.on) {
        h.on = on;
        h.t_on = now_ms;
        if (!on) { swap_link(&sw, false, now_ms); h.told = false; fh = ft; }
    }
    if (on && !h.told && now_ms - h.t_on >= h.enum_ms) { swap_link(&sw, true, now_ms); h.told = true; }
    while (fh != ft) {
        int i = fh;
        fh = (fh + 1) % 64;
        if (h.told) swap_rx(&sw, fifo[i].b, fifo[i].n, now_ms);
    }
    dev_tick(&dev, now_ms);
    swap_tick(&sw, now_ms);
}
// Runs until the install ends (and two seconds more, in which the box must send nothing). The time it took.
static uint32_t run(void (*each)(void)) {
    uint32_t t0 = now_ms;
    while (swap_busy(&sw) && now_ms - t0 < 900000) {
        if (each) each();
        pass();
        now_ms++;
    }
    uint32_t took = now_ms - t0;
    for (int i = 0; i < 2000; i++) { pass(); now_ms++; }
    return took;
}
static void go(void) { swap_start(&sw, &IO, PRODUCT, image_len, h.told, now_ms); }

static int pull_at_k, pull_mode;
static uint32_t pull_ms, t_pull;
static bool pulled, back_in;
static void pull(void) {   // the cable out when the FM-1 is at request pull_at_k of pull_mode, and back pull_ms later
    if (!pulled && dev.mode == pull_mode && dev.k >= pull_at_k) {
        pulled = true;
        t_pull = now_ms;
        h.cable_out = true;
        dev_restart(&dev);
    }
    if (pulled && !back_in && pull_ms && now_ms - t_pull >= pull_ms) { back_in = true; h.cable_out = false; }
}
static void pull_set(int mode, int k, uint32_t ms) { pull_mode = mode, pull_at_k = k, pull_ms = ms, pulled = back_in = false; }

#define WHOLE(len) (40 + 1 + ((len) + 511) / 512 + 1)   // the answers of an install that goes through
static void installs(void) {
    uint32_t took;
    make_pkg(raw_pkg, IMG_MAX + 20, PRODUCT, 0x4000, 42);
    make_logical(raw_pkg, IMG_MAX + 20, image);

    // -- it goes through
    setup(200000, "FM-1_391010100", PRODUCT);
    go();
    took = run(NULL);
    CHECK(sw.state == SWAP_DONE && sw.error == SWAP_OK, "an install: state %d error %d", sw.state, sw.error);
    CHECK(dev.wrote_all && dev.written == 200000 && dev.mode == D_AFTER, "the FM-1 took all of it: %u, mode %d", dev.written, dev.mode);
    CHECK(dev.answers == (int)WHOLE(200000) && !dev.bad_answers && !dev.retries && !dev.gave_up && !dev.while_away,
          "its answers: %d good (of %u), %d bad (%s), %d asked again, %d sent while it was away", dev.answers,
          WHOLE(200000), dev.bad_answers, dev.bad, dev.retries, dev.while_away);
    CHECK(dev.upgrades == 2 && h.up == 2 && dev.sessions == 2, "UPGRADE twice: %d", h.up);
    CHECK(!strcmp(sw.device, "FM-1_391010100") && !strcmp(sw.back, PRODUCT) && !sw.resumed && sw.checked && sw.pct == 100,
          "who it was [%s], who it is [%s], pct %u", sw.device, sw.back, sw.pct);
    CHECK(dev.fastest_ms >= SWAP_REPLY_MS, "an answer with data came after %u ms", dev.fastest_ms);
    CHECK(h.reads == (int)WHOLE(200000) - 2 && !h.read_oob && (int)sw.served == h.reads, "the card was read %d times", h.reads);
    CHECK(!h.late_sends && took < 30000, "it took %u ms; %d sent after", took, h.late_sends);
    CHECK(h.sends == h.hs + h.up + h.other && h.other == (int)WHOLE(200000), "what went: %d who-are-you, %d UPGRADE, %d answers", h.hs, h.up, h.other);

    // -- the same with other sizes of read, a USB that is often busy, a slow FM-1, the largest package
    static const struct { uint32_t len, chunk; int busy; uint32_t away1, away2, enum_ms; } V[] = {
        {960, 512, 0, 1500, 1200, 200},   {5000, 1, 0, 100, 100, 0},       {30011, 47, 3, 1500, 1200, 200},
        {30011, 511, 7, 25000, 30000, 2000}, {IMG_MAX, 512, 2, 4000, 6000, 500}, {609646, 256, 0, 1500, 1200, 200}};
    for (unsigned i = 0; i < sizeof V / sizeof *V; i++) {
        setup(V[i].len, "FM-1_900", PRODUCT);
        dev.chunk = V[i].chunk, dev.away1_ms = V[i].away1, dev.away2_ms = V[i].away2;
        dev.chip = 0xEF4014;
        h.busy = V[i].busy, h.enum_ms = V[i].enum_ms;
        go();
        took = run(NULL);
        CHECK(sw.state == SWAP_DONE && dev.wrote_all && !dev.bad_answers && !dev.retries && !h.read_oob && !h.late_sends,
              "variant %u: state %d error %d, written %u of %u, %d bad (%s), %d asked again", i, sw.state, sw.error,
              dev.written, V[i].len, dev.bad_answers, dev.bad, dev.retries);
    }
    // -- an erase: the loader says nothing for a minute and a half, then goes on
    setup(60000, "FM-1_900", PRODUCT);
    dev.pause_at = 9, dev.pause_ms = 90000;
    go();
    took = run(NULL);
    CHECK(sw.state == SWAP_DONE && dev.wrote_all && took > 90000, "a 90 s pause in the write: state %d error %d", sw.state, sw.error);

    // -- nothing answers
    setup(20000, "FM-1_900", PRODUCT);
    dev.silent = true;
    go();
    took = run(NULL);
    CHECK(sw.state == SWAP_FAILED && sw.error == SWAP_E_SILENT && h.hs == 3 && h.sends == 3 && took >= 3000 && took < 3100,
          "nothing answers: state %d error %d, asked %d times, %u ms", sw.state, sw.error, h.hs, took);
    // -- no FM-1 plugged in
    setup(20000, "FM-1_900", PRODUCT);
    dev.mode = D_OFF, h.on = h.told = false;
    go();
    took = run(NULL);
    CHECK(sw.state == SWAP_FAILED && sw.error == SWAP_E_SILENT && h.offers == 0 && took >= 3000 && took < 3100,
          "no FM-1: state %d error %d, %d offered, %u ms", sw.state, sw.error, h.offers, took);
    // -- ... plugged in two seconds after INSTALL
    setup(20000, "FM-1_900", PRODUCT);
    h.cable_out = true, h.on = h.told = false;
    pull_set(D_APP, 0, 0), pulled = true, t_pull = now_ms, pull_ms = 2000;
    go();
    took = run(pull);
    CHECK(sw.state == SWAP_DONE && dev.wrote_all, "plugged in 2 s after: state %d error %d", sw.state, sw.error);

    // -- another instrument
    setup(20000, "FM-2_900", PRODUCT);
    go();
    run(NULL);
    CHECK(sw.state == SWAP_FAILED && sw.error == SWAP_E_MODEL && h.up == 0 && dev.sessions == 0 && !strcmp(sw.device, "FM-2_900"),
          "another instrument: state %d error %d, UPGRADE %d", sw.state, sw.error, h.up);
    setup(20000, "ota-FM-2_900", PRODUCT);
    dev.mode = D_LOADER, dev.id_loader = "ota-FM-2_900";
    go();
    run(NULL);
    CHECK(sw.state == SWAP_FAILED && sw.error == SWAP_E_MODEL && h.up == 0, "another instrument's loader: error %d", sw.error);

    // -- it stops asking in the check
    setup(20000, "FM-1_900", PRODUCT);
    dev.stop_check_at = 10;
    go();
    took = run(NULL);
    CHECK(sw.state == SWAP_FAILED && sw.error == SWAP_E_STOPPED && dev.written == 0 && dev.sessions == 1 && took > 8000 && took < 9500,
          "stopped in the check: state %d error %d, %u ms", sw.state, sw.error, took);
    // -- it never asks at all (takes UPGRADE and says nothing): the first request has two seconds more
    setup(20000, "FM-1_900", PRODUCT);
    dev.stop_check_at = 0;
    go();
    took = run(NULL);
    CHECK(sw.state == SWAP_FAILED && sw.error == SWAP_E_STOPPED && took > 10000 && took < 10300, "never asked: error %d, %u ms", sw.error, took);

    // -- it stops in the write (still on the USB, silent): three minutes, then HALF; and the next install finishes it
    setup(60000, "FM-1_900", PRODUCT);
    dev.pause_at = 30, dev.pause_ms = 500000;
    go();
    took = run(NULL);
    CHECK(sw.state == SWAP_FAILED && sw.error == SWAP_E_HALF && took > 180000 && took < 190000 && dev.written == 30 * 512,
          "stopped in the write: state %d error %d, %u ms", sw.state, sw.error, took);
    dev.mode = D_LOADER, dev.waiting = false, dev.pause_at = -1;
    int before = dev.answers;
    go();
    run(NULL);
    CHECK(sw.state == SWAP_DONE && sw.resumed && !sw.checked && dev.wrote_all && h.up == 3 && dev.sessions == 3 &&
          dev.answers - before == (60000 + 511) / 512 + 1 && !strcmp(sw.device, "ota-FM-1_900"),
          "INSTALL again, the FM-1 in its loader: state %d error %d resumed %d", sw.state, sw.error, sw.resumed);
    // -- it goes off in the write (power): the same
    setup(60000, "FM-1_900", PRODUCT);
    dev.stop_write_at = 50;
    go();
    took = run(NULL);
    CHECK(sw.state == SWAP_FAILED && sw.error == SWAP_E_HALF && took > 180000, "off in the write: state %d error %d, %u ms", sw.state, sw.error, took);

    // -- the cable out in the check and back
    setup(60000, "FM-1_900", PRODUCT);
    pull_set(D_CHECK, 20, 2500);
    go();
    run(pull);
    CHECK(sw.state == SWAP_DONE && dev.wrote_all && dev.sessions == 3 && !sw.resumed && !dev.bad_answers,
          "the cable out in the check and back: state %d error %d, %d sessions, bad %d (%s)", sw.state, sw.error,
          dev.sessions, dev.bad_answers, dev.bad);
    // -- ... and not back
    setup(60000, "FM-1_900", PRODUCT);
    pull_set(D_CHECK, 20, 0);
    go();
    took = run(pull);
    CHECK(sw.state == SWAP_FAILED && sw.error == SWAP_E_STOPPED && dev.written == 0 && took < 10000,
          "the cable out in the check for good: state %d error %d, %u ms", sw.state, sw.error, took);
    // -- the cable out in the write and back: its loader answers, the write from the start
    setup(60000, "FM-1_900", PRODUCT);
    pull_set(D_WRITE, 60, 4000);
    go();
    run(pull);
    CHECK(sw.state == SWAP_DONE && dev.wrote_all && dev.sessions == 3 && !sw.resumed && sw.checked && !dev.bad_answers,
          "the cable out in the write and back: state %d error %d, %d sessions, bad %d (%s)", sw.state, sw.error,
          dev.sessions, dev.bad_answers, dev.bad);
    // -- ... back, but nothing answers any more
    setup(60000, "FM-1_900", PRODUCT);
    pull_set(D_WRITE, 60, 4000);
    go();
    while (swap_busy(&sw) && !back_in) { pull(); pass(); now_ms++; }
    dev.silent = true;
    took = run(NULL);
    CHECK(sw.state == SWAP_FAILED && sw.error == SWAP_E_HALF && took > 29000 && took < 31000,
          "back in the write but silent: state %d error %d, %u ms", sw.state, sw.error, took);

    // -- it asks for what isn't there
    setup(60000, "FM-1_900", PRODUCT);
    dev.bad_at = 5;
    go();
    run(NULL);
    CHECK(sw.state == SWAP_FAILED && sw.error == SWAP_E_BADREQ && sw.bad_addr == 60000 - 4 && sw.bad_n == 16 && !h.read_oob,
          "asked past the end: state %d error %d, %#x+%u", sw.state, sw.error, sw.bad_addr, sw.bad_n);
    setup(60000, "FM-1_900", PRODUCT);
    dev.long_at = 5;
    go();
    run(NULL);
    CHECK(sw.state == SWAP_FAILED && sw.error == SWAP_E_BADREQ && sw.bad_n == 513 && !h.read_oob, "asked for 513: error %d", sw.error);

    // -- the card misreads once, twice: the FM-1 asks again and it goes on. Three times: CARD.
    for (int n = 1; n <= 3; n++) {
        setup(60000, "FM-1_900", PRODUCT);
        h.fail_from = 60, h.fail_to = 60 + n;
        go();
        took = run(NULL);
        if (n < 3)
            CHECK(sw.state == SWAP_DONE && dev.wrote_all && dev.retries == n && !dev.bad_answers,
                  "%d misreads: state %d error %d, asked again %d", n, sw.state, sw.error, dev.retries);
        else
            CHECK(sw.state == SWAP_FAILED && sw.error == SWAP_E_CARD && dev.retries == 2, "3 misreads: state %d error %d", sw.state, sw.error);
    }
    // -- an answer lost on the way: asked again after five seconds, and it goes on
    setup(60000, "FM-1_900", PRODUCT);
    h.drop_send = 80;
    go();
    run(NULL);
    CHECK(sw.state == SWAP_DONE && dev.wrote_all && dev.retries == 1, "an answer lost: state %d error %d, asked again %d", sw.state, sw.error, dev.retries);
    // -- the finish's answer lost (step 1): asked again, answered again though the box is already waiting for the loader
    setup(60000, "FM-1_900", PRODUCT);
    h.drop_send = 1 + 1 + 40 + 1;
    go();
    run(NULL);
    CHECK(sw.state == SWAP_DONE && dev.wrote_all && dev.retries == 1, "step 1's finish lost: state %d error %d (asked again %d)", sw.state, sw.error, dev.retries);
    setup(60000, "FM-1_900", PRODUCT);
    h.drop_send = 1 + 1 + 40 + 1 + 1 + 1 + (60000 + 511) / 512 + 1;   // (one who-are-you finds the loader: it is 1.5 s away, looked for after 3)
    go();
    run(NULL);
    CHECK(sw.state == SWAP_DONE && dev.wrote_all && dev.retries == 1 && h.other == 41 + 119 + 1,
          "step 2's finish lost: state %d error %d (asked again %d, %d answers)", sw.state, sw.error, dev.retries, h.other);

    // -- the loader doesn't appear
    setup(60000, "FM-1_900", PRODUCT);
    dev.no_loader = true;
    go();
    took = run(NULL);
    CHECK(sw.state == SWAP_FAILED && sw.error == SWAP_E_NOLOADER && took > 33000 && took < 34000 && dev.written == 0,
          "no loader: state %d error %d, %u ms", sw.state, sw.error, took);
    // -- written, and it doesn't come back
    setup(60000, "FM-1_900", PRODUCT);
    dev.no_return = true;
    go();
    took = run(NULL);
    CHECK(sw.state == SWAP_FAILED && sw.error == SWAP_E_NORETURN && dev.wrote_all && took > 43000 && took < 50000,
          "no return: state %d error %d, %u ms", sw.state, sw.error, took);
    // -- written, and something else answers
    setup(60000, "FM-1_900", "FM-1_391010100");
    go();
    run(NULL);
    CHECK(sw.state == SWAP_FAILED && sw.error == SWAP_E_MISMATCH && dev.wrote_all && !strcmp(sw.back, "FM-1_391010100"),
          "something else answers: state %d error %d [%s]", sw.state, sw.error, sw.back);
    // -- after the write the loader answers a while longer before the new firmware: not taken for the answer
    setup(60000, "FM-1_900", PRODUCT);
    dev.away2_ms = 0;
    dev.id_after = "ota-FM-1_900";
    go();
    while (swap_busy(&sw) && sw.state != SWAP_REBOOT) { pass(); now_ms++; }
    for (int i = 0; i < 9000; i++) { pass(); now_ms++; }
    CHECK(sw.state == SWAP_REBOOT, "the loader still answering after the write: state %d", sw.state);
    dev.id_after = PRODUCT;
    run(NULL);
    CHECK(sw.state == SWAP_DONE, "... then the new firmware: state %d error %d", sw.state, sw.error);

    // -- the clock wraps in the middle
    setup(200000, "FM-1_900", PRODUCT);
    now_ms = 0xFFFFFFFFu - 9000;
    go();
    run(NULL);
    CHECK(sw.state == SWAP_DONE && dev.wrote_all && !dev.retries, "over the clock's wrap: state %d error %d", sw.state, sw.error);

    // -- nothing happens before swap_start or after the end
    swap_init(&sw);
    setup(20000, "FM-1_900", PRODUCT);
    for (int i = 0; i < 3000; i++) { pass(); now_ms++; }
    CHECK(sw.state == SWAP_IDLE && !swap_busy(&sw) && h.offers == 0, "idle: %d offered", h.offers);
}

int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "vectors")) return vectors(argc, argv);
    units();
    printf("swap units: %d checks, %d failed\n", checks, failed);
    int f0 = failed;
    checks = 0, failed = 0;
    installs();
    printf("swap installs: %d checks, %d failed\n", checks, failed);
    return f0 || failed;
}
