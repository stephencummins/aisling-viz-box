// Test 19's end-to-end host check: the box's swap.c installs real packages on an FM-1 made of fm1-aisling's own
// update code (e2e_dev.c: firmware/src/ota.c as the running firmware, firmware/loader/ldr_core.c as the loader) and
// a megabyte of pretend flash.
//
//   e2e_check INSTALLED.fwsc NEW.fwsc
//
// The flash starts as INSTALLED's (as tests/ldr_test.c sets it up). What the FM-1 is after a restart is read from the
// flash: its loader while an update record is at 0xE4F00 (the SPL's rule, ldr_core.c), else whichever package's
// firmware is in the app area, else nothing. A millisecond is one ota_idle(); the box runs in each. The packages
// are read as the card would give them (the file's bytes, swap_file_off), not from a ready-made logical image.
// What isn't here: the USB itself, the chip's boot ROM and SPL, and how long an erase takes (none).
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "swap.h"
#include "e2e.h"

static int checks, failed;
#define CHECK(c, ...) do { checks++; if (!(c)) { if (++failed <= 60) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } } while (0)

#define APP_LO 0x4000u
#define APP_HI 0x93000u
typedef struct {
    uint8_t *raw, *lg;       // the file; its logical image (made here, for comparing: the box never sees it)
    uint32_t n, fo;          // the file's length; where flash.bin is in the logical image
    char product[SWAP_NAME_MAX];
} pkg_t;
static pkg_t P[2];

static uint8_t nor[0x100000];
static uint32_t now_ms;
static int bad_app_write, bad_ldr_write, erases, progs, commits, record_clears, rec_erases, last_show_code;
static uint8_t rx[700];
static uint32_t rx_n;
static bool rx_full, ota_req;
static struct { uint8_t b[80]; uint32_t n; } fifo[64];
static int fh, ft;

enum { M_OFF, M_OLD, M_NEW, M_LOADER, M_DEAD };
static int mode = M_OFF;
static bool linked;                 // the box has been told it is there
static swap_t sw;
static const pkg_t *serving;        // the package the box is installing
static int flip_read;               // the card gives one wrong bit in the read of this number (0: never)
static int reads, sends, dev_requests, dev_retries;
static uint32_t last_req_addr, last_req_len, last_req_ms;
static int reads_step1;             // the card's reads before the loader first ran
static int session_rc[5], sessions[5];

// ---- the FM-1's hooks
int e2e_wire_send(const uint8_t *p, uint32_t n) {
    if (n > sizeof fifo[0].b || (ft + 1) % 64 == fh) { printf("FAIL: harness fifo (%u bytes)\n", n); exit(2); }
    if (n == 20) {   // a read request: is it the last one again, after the FM-1's five seconds of waiting?
        uint8_t fl;
        uint32_t a, l;
        if (swap_request(p, n, &fl, &a, &l)) {
            if (dev_requests && a == last_req_addr && l == last_req_len && now_ms - last_req_ms >= 4000u) dev_retries++;
            dev_requests++, last_req_addr = a, last_req_len = l, last_req_ms = now_ms;
        }
    }
    memcpy(fifo[ft].b, p, n);
    fifo[ft].n = n;
    ft = (ft + 1) % 64;
    return 0;
}
int e2e_frame_get(const uint8_t **p, uint32_t *n) { if (!rx_full) return 0; *p = rx; *n = rx_n; return 1; }
void e2e_frame_done(void) { rx_full = false; }
uint32_t e2e_now(void) { return now_ms; }
int e2e_fread(uint32_t off, void *p, uint32_t n) { if (off + n > sizeof nor) return -1; memcpy(p, nor + off, n); return 0; }
void e2e_show(uint32_t step, int32_t code) { if (step == 9) last_show_code = code; }
int e2e_app_erase(uint32_t off) {   // the running firmware: only the staging area (tests/ota_test.c's rule)
    if (off < 0xE0000u || off >= 0xE5000u || (off & 0xFFFu)) { bad_app_write++; return -8; }
    memset(nor + off, 0xFF, 4096);
    return 0;
}
int e2e_app_prog(uint32_t off, const void *src, uint32_t n) {
    if (off < 0xE0000u || off + n > 0xE5000u) { bad_app_write++; return -8; }
    for (uint32_t i = 0; i < n; i++) nor[off + i] &= ((const uint8_t *)src)[i];
    return 0;
}
void e2e_commit(const uint8_t *parm) { (void)parm; commits++; }
int e2e_ldr_erase(uint32_t off) {   // the loader: never the head, never Felucca's own sectors (tests/ldr_test.c's rule)
    if (off < 0x4000u || off >= 0xFC000u || (off & 0xFFFu)) { bad_ldr_write++; return -1; }
    if (off < APP_HI) erases++; else rec_erases++;
    memset(nor + off, 0xFF, 0x1000);
    return 0;
}
int e2e_ldr_prog(uint32_t off, const void *p, uint32_t n) {
    if (off < 0x4000u || off + n > APP_HI || ((off & 0xFFu) + n) > 256u) { bad_ldr_write++; return -1; }
    progs++;
    for (uint32_t i = 0; i < n; i++) nor[off + i] &= ((const uint8_t *)p)[i];
    return 0;
}
void e2e_record_clear(void) { record_clears++; }

// ---- the box
static bool io_read(void *ctx, uint32_t logical, uint8_t *dst, uint32_t n) {
    (void)ctx;
    reads++;
    if ((uint64_t)logical + n > serving->n - 20 || n > SWAP_MAXDATA) { printf("FAIL: the box read past the package\n"); exit(2); }
    for (uint32_t i = 0; i < n; i++) dst[i] = serving->raw[swap_file_off(logical + i)];
    if (reads == flip_read) dst[n / 2] ^= 0x04;
    return true;
}
static bool io_send(void *ctx, const uint8_t *w, uint32_t n) {
    static const uint8_t UP[] = {0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7};
    (void)ctx;
    if (mode == M_OFF || mode == M_DEAD) return true;   // into the air
    if (n == sizeof UP && !memcmp(w, UP, n)) { ota_req = true; sends++; return true; }   // firmware/src/usb.c's part
    if (rx_full) return false;                          // the FM-1 hasn't taken the last one yet
    if (n < 2 || n - 2 > sizeof rx) { printf("FAIL: the box sent %u bytes\n", n); exit(2); }
    memcpy(rx, w + 1, n - 2);
    rx_n = n - 2;
    rx_full = true;
    sends++;
    return true;
}
static const swap_io_t IO = {io_read, io_send, NULL};

static jmp_buf power;
static uint32_t cut_at_written;     // the power goes when the loader has programmed this many pages (0: never)
static bool cut_done;
static void box_pass(void) {
    while (fh != ft) {
        int i = fh;
        fh = (fh + 1) % 64;
        if (linked) swap_rx(&sw, fifo[i].b, fifo[i].n, now_ms);
    }
    swap_tick(&sw, now_ms);
}
void e2e_idle(void) {
    now_ms++;
    box_pass();
    if (cut_at_written && !cut_done && mode == M_LOADER && (uint32_t)progs >= cut_at_written) {
        cut_done = true;
        longjmp(power, 1);
    }
}
static void link_set(bool up) {
    if (up == linked) return;
    linked = up;
    if (!up) fh = ft, rx_full = false;
    swap_link(&sw, up, now_ms);
}
static void wait_ms(uint32_t ms) { while (ms--) { now_ms++; box_pass(); } }

static bool app_is(const pkg_t *p) { return !memcmp(nor + APP_LO, p->lg + p->fo + APP_LO, APP_HI - APP_LO); }
static int boot(void) {
    if (e2e_record_ok(nor + 0xE4F00)) return M_LOADER;
    if (app_is(&P[0])) { old_cleanup(); return M_OLD; }
    if (app_is(&P[1])) { new_cleanup(); return M_NEW; }
    return M_DEAD;
}
// The FM-1 from switch-on until the box's install has ended and `after` ms more.
static uint32_t off_ms, after_ms;
static void fm1_run(void) {
    static uint32_t t_end;
    static bool ended;
    ended = false;
    if (setjmp(power)) {   // the power went
        mode = M_OFF;
        link_set(false);
        wait_ms(off_ms);
    }
    for (;;) {
        mode = boot();
        if (mode == M_LOADER && !reads_step1) reads_step1 = reads;
        ota_req = false, rx_full = false;
        wait_ms(200);   // the USB finds it
        link_set(mode != M_DEAD);
        for (;;) {
            e2e_idle();
            if (!swap_busy(&sw)) {
                if (!ended) ended = true, t_end = now_ms;
                if (now_ms - t_end >= after_ms) return;
            }
            if (mode == M_DEAD) continue;
            if (mode == M_OLD) old_service(); else if (mode == M_NEW) new_service(); else loader_service();
            if (!ota_req) continue;
            ota_req = false;
            int rc = mode == M_OLD ? old_session() : mode == M_NEW ? new_session() : loader_session();
            session_rc[mode] = rc, sessions[mode]++;
            if (rc == 0) break;   // it restarts
        }
        mode = M_OFF;
        link_set(false);
        wait_ms(1500);
    }
}

static void load(pkg_t *p, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(2); }
    p->raw = malloc(0x200000), p->lg = malloc(0x200000);
    p->n = (uint32_t)fread(p->raw, 1, 0x200000, f);
    fclose(f);
    for (uint32_t i = 0; i < 20; i++) memcpy(p->lg + i * 47, p->raw + i * 48, 47);
    memcpy(p->lg + 940, p->raw + 960, p->n - 960);
    p->fo = e2e_flash_off(p->lg);
    if (!swap_product(p->raw, p->product)) { printf("FAIL: %s has no product\n", path); exit(2); }
}
static void fresh(const pkg_t *installed) {   // an FM-1 with this package on it, switched off
    memset(nor, 0xFF, sizeof nor);
    memcpy(nor, installed->lg + installed->fo, APP_HI);
    bad_app_write = bad_ldr_write = erases = progs = commits = record_clears = rec_erases = 0;
    reads = sends = dev_requests = dev_retries = 0, flip_read = 0, reads_step1 = 0;
    memset(session_rc, 0, sizeof session_rc), memset(sessions, 0, sizeof sessions);
    cut_at_written = 0, cut_done = false, off_ms = 3000, after_ms = 2000;
    mode = M_OFF, linked = false, fh = ft = 0;
    swap_init(&sw);
}
static void install(const pkg_t *p) {
    serving = p;
    swap_start(&sw, &IO, p->product, p->n - 20, linked, now_ms);
    fm1_run();
}
static bool staging_clean(void) {
    for (uint32_t i = 0xE0000; i < 0xE5000; i++) if (nor[i] != 0xFF) return false;
    return true;
}

int main(int argc, char **argv) {
    static uint8_t head[APP_LO];
    if (argc < 3) { printf("usage: e2e_check INSTALLED.fwsc NEW.fwsc\n"); return 2; }
    load(&P[0], argv[1]);
    load(&P[1], argv[2]);
    printf("  installed %s, new %s\n", P[0].product, P[1].product);
    CHECK(strcmp(P[0].product, P[1].product) && memcmp(P[0].lg + P[0].fo + APP_LO, P[1].lg + P[1].fo + APP_LO, APP_HI - APP_LO),
          "the two packages are the same firmware");

    // -- the new one over the installed one
    fresh(&P[0]);
    memcpy(head, nor, sizeof head);
    uint32_t t0 = now_ms;
    install(&P[1]);
    printf("  %d requests in %u ms, %d sectors erased, %d pages written\n", dev_requests, now_ms - t0 - after_ms, erases, progs);
    CHECK(sw.state == SWAP_DONE && sw.error == SWAP_OK && !sw.resumed, "install: state %d error %d", sw.state, sw.error);
    CHECK(!strcmp(sw.device, P[0].product) && !strcmp(sw.back, P[1].product), "it was [%s], it is [%s]", sw.device, sw.back);
    CHECK(app_is(&P[1]), "the app area is the new package's flash.bin");
    CHECK(!memcmp(nor, head, sizeof head), "the flash's head is as it was");
    CHECK(sessions[M_OLD] == 1 && session_rc[M_OLD] == 0 && commits == 1 && sessions[M_LOADER] == 1 && session_rc[M_LOADER] == 0,
          "step 1 rc %d (%d), step 2 rc %d (%d)", session_rc[M_OLD], sessions[M_OLD], session_rc[M_LOADER], sessions[M_LOADER]);
    CHECK(!bad_app_write && !bad_ldr_write, "writes outside what each may write: %d, %d", bad_app_write, bad_ldr_write);
    CHECK(record_clears == 1 && rec_erases == 1 && !e2e_record_ok(nor + 0xE4F00) && staging_clean() && mode == M_NEW,
          "the update record is gone and the staging area clean (mode %d)", mode);
    CHECK(dev_retries == 0, "the FM-1 asked %d times again", dev_retries);
    CHECK(reads == dev_requests - 2 && (int)sw.served == reads, "the card read %d times for %d requests", reads, dev_requests);

    // -- and back: the first one over that
    reads = dev_requests = dev_retries = 0, erases = progs = 0;
    memset(sessions, 0, sizeof sessions);
    install(&P[0]);
    CHECK(sw.state == SWAP_DONE && !strcmp(sw.device, P[1].product) && !strcmp(sw.back, P[0].product) && app_is(&P[0]) &&
          !memcmp(nor, head, sizeof head) && sessions[M_NEW] == 1 && sessions[M_LOADER] == 1 && !bad_app_write &&
          !bad_ldr_write && staging_clean() && dev_retries == 0 && mode == M_OLD,
          "the way back: state %d error %d, [%s] to [%s]", sw.state, sw.error, sw.device, sw.back);
    // -- the same one again: nothing to erase
    erases = progs = 0;
    install(&P[0]);
    CHECK(sw.state == SWAP_DONE && app_is(&P[0]) && erases == 0 && progs == 0, "the same again: state %d, %d erased", sw.state, erases);

    // -- the power goes in the middle of the write and comes back three seconds later: the box sees it go and come,
    //    finds the loader and writes from the start
    fresh(&P[0]);
    cut_at_written = 900;
    install(&P[1]);
    CHECK(cut_done && sw.state == SWAP_DONE && !sw.resumed && sw.checked && app_is(&P[1]) && sessions[M_LOADER] == 1 && session_rc[M_LOADER] == 0 &&
          !bad_ldr_write && !memcmp(nor, head, sizeof head) && staging_clean(),
          "power cut in the write, back in 3 s: state %d error %d, resumed %d checked %d", sw.state, sw.error, sw.resumed, sw.checked);

    // -- the power goes and stays off: HALF after three minutes. On again, it is its loader (neither firmware is
    //    whole); INSTALL again finishes it. And with the other package instead: that one goes on.
    for (int other = 0; other < 2; other++) {
        fresh(&P[0]);
        cut_at_written = 900, off_ms = 400000, after_ms = 0;
        install(&P[1]);
        CHECK(cut_done && sw.state == SWAP_FAILED && sw.error == SWAP_E_HALF && !app_is(&P[0]) && !app_is(&P[1]) &&
              e2e_record_ok(nor + 0xE4F00), "power cut for good: state %d error %d", sw.state, sw.error);
        wait_ms(400000);   // (the rest of the time it is off)
        after_ms = 2000;
        const pkg_t *p = &P[!other];
        install(p);
        CHECK(sw.state == SWAP_DONE && sw.resumed && !strcmp(sw.device, "ota-FM-1_900") && !strcmp(sw.back, p->product) &&
              app_is(p) && !memcmp(nor, head, sizeof head) && staging_clean() && !bad_ldr_write,
              "INSTALL again from the loader (%s): state %d error %d resumed %d", p->product, sw.state, sw.error, sw.resumed);
    }

    // -- the card gives one wrong bit, in each of step 1's reads in turn: the FM-1 refuses and nothing is written,
    //    or the bit was in a part step 1 doesn't use (the loader reads it all again) and the install is whole
    {
        int refused = 0, went = 0, n1;
        fresh(&P[0]);
        install(&P[1]);
        n1 = reads_step1;
        CHECK(n1 > 10 && n1 < 60, "step 1 read the card %d times", n1);
        for (int k = 1; k <= n1; k++) {
            fresh(&P[0]);
            flip_read = k, after_ms = 500;
            install(&P[1]);
            if (sw.state == SWAP_FAILED) {
                refused++;
                CHECK(sw.error == SWAP_E_STOPPED && app_is(&P[0]) && staging_clean() && !bad_app_write && mode == M_OLD &&
                      session_rc[M_OLD] < 0 && commits == 0 && progs == 0,
                      "a wrong bit in read %d of step 1: error %d, rc %d", k, sw.error, session_rc[M_OLD]);
            } else {
                went++;
                CHECK(sw.state == SWAP_DONE && app_is(&P[1]) && staging_clean(), "a wrong bit in read %d: state %d", k, sw.state);
            }
        }
        printf("  a wrong bit in one of step 1's %d reads: refused %d times, in a part not used %d times\n", n1, refused, went);
        CHECK(refused >= n1 - 4, "step 1 refused a wrong bit only %d times of %d", refused, n1);
        // in the loader's third read (the app area's first bytes, which it tests against the chip's key): it stops,
        // the box says HALF; INSTALL again finishes
        fresh(&P[0]);
        flip_read = n1 + 3, after_ms = 0;
        install(&P[1]);
        CHECK(sw.state == SWAP_FAILED && sw.error == SWAP_E_HALF && session_rc[M_LOADER] == -6 && progs == 0 && app_is(&P[0]) &&
              e2e_record_ok(nor + 0xE4F00), "a wrong bit in the loader's third read: state %d error %d rc %d", sw.state,
              sw.error, session_rc[M_LOADER]);
        after_ms = 2000;
        install(&P[1]);
        CHECK(sw.state == SWAP_DONE && sw.resumed && app_is(&P[1]) && staging_clean(), "and INSTALL again: state %d error %d",
              sw.state, sw.error);
    }

    printf("e2e: %d checks, %d failed\n", checks, failed);
    return failed != 0;
}
