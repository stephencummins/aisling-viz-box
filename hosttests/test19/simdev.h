// (test 19) What the checks share: a package made up for them, and a pretend FM-1 that speaks Felucca's update from
// the other end (written from the protocol as fm1-aisling's firmware/src/ota.c and tools/fm1_install.py have it, and
// sharing no code with the box's swap.c: its own packing, its own sums).
//
// The pretend FM-1: answers "who are you"; on UPGRADE asks for parts of the package (step 1) and the finish; leaves
// the USB; comes back as its loader; on UPGRADE asks for the whole package in order and the finish; leaves; comes
// back as what was written. Each request waits five seconds for its answer and is asked three times, as ota.c's.
// Every answer is looked at: its frame, its length, the address, length and flags given back, the sum, and the
// bytes against the package's logical image. How it misbehaves is set in fm1_t's first half.
#ifndef SIMDEV_H
#define SIMDEV_H
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t lcg_x;
static uint8_t lcg(void) {
    lcg_x = (lcg_x * 1103515245u + 12345u) & 0x7FFFFFFFu;
    return (uint8_t)(lcg_x >> 16);
}
static uint64_t fnv(const uint8_t *p, uint32_t n) {
    uint64_t h = 0xCBF29CE484222325ull;
    for (uint32_t i = 0; i < n; i++) h = (h ^ p[i]) * 0x100000001B3ull;
    return h;
}

// ---- a package: n bytes from the seed; the product in the marker bytes of its first 20 blocks of 48; Felucca's
// loader's name at mark_at (0: not in it). And its logical image, made here by the rule and not by swap.c.
static void make_pkg(uint8_t *raw, uint32_t n, const char *product, uint32_t mark_at, uint32_t seed) {
    lcg_x = seed;
    for (uint32_t i = 0; i < n; i++) {
        raw[i] = lcg();
        if (raw[i] == 'F') raw[i] = 'G';   // so the loader's name is nowhere by chance
    }
    uint32_t len = (uint32_t)strlen(product);
    for (uint32_t i = 0; i < 20 && i * 48 + 47 < n; i++) raw[i * 48 + 47] = i < len ? (uint8_t)(product[i] + i + 1) : 0x7D;
    if (mark_at) memcpy(raw + mark_at, "FELUCCA-LOADER-1", 16);
}
static uint32_t make_logical(const uint8_t *raw, uint32_t n, uint8_t *lg) {
    for (uint32_t i = 0; i < 20; i++) memcpy(lg + i * 47, raw + i * 48, 47);
    memcpy(lg + 940, raw + 960, n - 960);
    return n - 20;
}

// ---- the pretend FM-1
static uint32_t d_pack7(const uint8_t *in, uint32_t n, uint8_t *out) {
    uint32_t acc = 0, nb = 0, o = 0;
    while (n--) {
        acc |= (uint32_t)*in++ << nb;
        nb += 8;
        while (nb >= 7) { out[o++] = acc & 0x7F; acc >>= 7; nb -= 7; }
    }
    if (nb) out[o++] = acc & 0x7F;
    return o;
}
static uint32_t d_unpack7(const uint8_t *in, uint32_t n, uint8_t *out) {
    uint32_t acc = 0, nb = 0, o = 0;
    while (n--) {
        acc |= (uint32_t)*in++ << nb;
        nb += 7;
        if (nb >= 8) { out[o++] = (uint8_t)acc; acc >>= 8; nb -= 8; }
    }
    return o;
}

enum { D_APP, D_CHECK, D_AWAY1, D_LOADER, D_WRITE, D_AWAY2, D_AFTER, D_OFF };
typedef struct {
    // what it is and how it behaves
    const uint8_t *image;
    uint32_t image_len;
    const char *id_app, *id_loader, *id_after;
    uint32_t chip;                 // not 0: in the app's identity, bytes 24..26
    int check_reqs;                // step 1's reads
    uint32_t chunk;                // step 2's reads, bytes each
    uint32_t away1_ms, away2_ms;   // off the USB between the steps, and after
    bool silent;                   // answers nothing
    bool no_loader, no_return;     // never comes back from away
    int stop_check_at, stop_write_at;   // goes quiet for good before this request (-1: doesn't)
    int bad_at;                    // step 2's request of this number asks past the package (-1)
    int long_at;                   // ... or for 513 bytes (-1)
    int pause_at;                  // step 2: before this request, nothing for pause_ms (an erase)
    uint32_t pause_ms;
    void (*to_host)(const uint8_t *wire, uint32_t n);
    // how it is going
    int mode;
    uint32_t t_mode;
    int k;                         // the request now out
    bool waiting, finishing, paused;
    int tries;
    uint32_t t_req, addr, len;
    uint8_t flags;
    // what it saw
    int hs, upgrades, answers, bad_answers, retries, gave_up, sessions, while_away;
    uint32_t fastest_ms;           // the quickest answer with data
    uint32_t written;              // step 2: bytes taken, in order
    bool wrote_all;
    char bad[160];
} fm1_t;

static void dev_init(fm1_t *d, const uint8_t *image, uint32_t image_len, const char *app, const char *after,
                     void (*to_host)(const uint8_t *, uint32_t)) {
    memset(d, 0, sizeof *d);
    d->image = image, d->image_len = image_len;
    d->id_app = app, d->id_loader = "ota-FM-1_900", d->id_after = after;
    d->check_reqs = 40, d->chunk = 512, d->away1_ms = 1500, d->away2_ms = 1200;
    d->stop_check_at = d->stop_write_at = d->bad_at = d->long_at = d->pause_at = -1;
    d->to_host = to_host;
    d->fastest_ms = UINT32_MAX;
}
static bool dev_on_bus(const fm1_t *d) { return d->mode != D_AWAY1 && d->mode != D_AWAY2 && d->mode != D_OFF; }

static void dev_msg(fm1_t *d, const uint8_t *body, uint32_t n) {
    uint8_t w[80];
    w[0] = 0xF0;
    uint32_t k = d_pack7(body, n, w + 1);
    w[1 + k] = 0xF7;
    d->to_host(w, k + 2);
}
static void dev_identity(fm1_t *d, const char *text, uint32_t chip) {
    uint8_t m[34] = {0x00, 0x59, 0x11, 27, 0, 0};
    uint32_t s = 0;
    memcpy(m + 6, text, strlen(text));
    if (chip) m[30] = (uint8_t)(chip >> 16), m[31] = (uint8_t)(chip >> 8), m[32] = (uint8_t)chip;
    for (int i = 6; i < 33; i++) s += m[i];
    m[33] = (uint8_t)~s;
    dev_msg(d, m, sizeof m);
}
static void dev_ask(fm1_t *d, uint32_t now) {
    uint8_t m[15] = {0x00, 0x59, 0x30, 8, 0, 0, d->flags, (uint8_t)d->addr, (uint8_t)(d->addr >> 8),
                     (uint8_t)(d->addr >> 16), (uint8_t)(d->addr >> 24), (uint8_t)d->len, (uint8_t)(d->len >> 8),
                     (uint8_t)(d->len >> 16)};
    uint32_t s = 0;
    for (int i = 6; i < 14; i++) s += m[i];
    m[14] = (uint8_t)~s;
    d->t_req = now;
    d->waiting = true;
    dev_msg(d, m, sizeof m);
}
// The next request of the step, or the finish.
static void dev_next(fm1_t *d, uint32_t now) {
    d->tries = 0;
    d->flags = (uint8_t)(d->k * 37 + 1);
    d->finishing = false;
    if (d->mode == D_CHECK) {
        if (d->k == d->stop_check_at) { d->waiting = false; d->mode = D_APP; d->gave_up++; d->k = -1; return; }
        if (d->k >= d->check_reqs) {
            d->addr = 0xE0000000u, d->len = 8, d->finishing = true;
        } else {   // here and there in the package, the first at its head, the last at its end
            d->len = d->k % 3 == 0 ? 512 : d->k % 3 == 1 ? 64 : 256;
            uint32_t room = d->image_len - d->len;
            d->addr = d->k == 0 ? 0 : d->k == d->check_reqs - 1 ? room : (uint32_t)((uint64_t)room * (uint32_t)d->k / (uint32_t)d->check_reqs);
        }
    } else {
        if (d->k == d->stop_write_at) { d->waiting = false; d->mode = D_OFF; d->gave_up++; return; }
        if (d->written >= d->image_len) {
            d->addr = 0xF0000000u, d->len = 8, d->finishing = true;
        } else {
            d->addr = d->written;
            d->len = d->image_len - d->written < d->chunk ? d->image_len - d->written : d->chunk;
            if (d->k == d->bad_at) d->addr = d->image_len - 4, d->len = 16;
            if (d->k == d->long_at) d->len = 513;
            if (d->k == d->pause_at && !d->paused) { d->paused = true; d->waiting = false; d->t_req = now; return; }
        }
    }
    dev_ask(d, now);
}
static void dev_rx(fm1_t *d, const uint8_t *w, uint32_t n, uint32_t now) {
    static const uint8_t HS[] = {0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7};
    static const uint8_t UP[] = {0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7};
    uint8_t u[640];
    if (!dev_on_bus(d)) { d->while_away++; return; }
    if (d->silent) return;
    if (n == sizeof HS && !memcmp(w, HS, n)) {
        d->hs++;
        if (d->mode == D_APP || d->mode == D_CHECK) dev_identity(d, d->id_app, d->chip);
        else if (d->mode == D_LOADER || d->mode == D_WRITE) dev_identity(d, d->id_loader, 0);
        else dev_identity(d, d->id_after, d->chip);
        return;
    }
    if (n == sizeof UP && !memcmp(w, UP, n)) {
        d->upgrades++;
        if (d->mode == D_APP) d->mode = D_CHECK;
        else if (d->mode == D_LOADER) d->mode = D_WRITE, d->written = 0, d->paused = false;
        else return;   // in a session already (or the new firmware: for this check it does nothing)
        d->sessions++;
        d->k = 0;
        d->t_mode = now;
        dev_next(d, now);
        return;
    }
    if (!d->waiting || n < 2 || w[0] != 0xF0 || w[n - 1] != 0xF7 || n > sizeof u) {
        d->bad_answers++;
        if (!d->bad[0]) snprintf(d->bad, sizeof d->bad, "%u bytes it didn't ask for (mode %d)", n, d->mode);
        return;
    }
    for (uint32_t i = 1; i + 1 < n; i++)
        if (w[i] & 0x80) { d->bad_answers++; if (!d->bad[0]) snprintf(d->bad, sizeof d->bad, "a byte over 7 bits in an answer"); return; }
    uint32_t k = d_unpack7(w + 1, n - 2, u), s = 0;
    uint32_t want = d->len + 15;
    const char *why = NULL;
    if (k != want) why = "its length";
    else if (u[0] != 0 || u[1] != 0x59 || u[2] != 0x30) why = "its head";
    else if ((uint32_t)(u[3] | u[4] << 8 | u[5] << 16) != d->len + 8) why = "the length it says";
    else if ((uint32_t)u[7] != (d->addr & 0xFF) || (uint32_t)(u[7] | u[8] << 8 | u[9] << 16 | (uint32_t)u[10] << 24) != d->addr) why = "the address given back";
    else if ((uint32_t)(u[11] | u[12] << 8 | u[13] << 16) != d->len) why = "the length given back";
    else if (u[6] != (d->finishing ? 0 : d->flags)) why = "the flags given back";
    if (!why) {
        for (uint32_t i = 6; i + 1 < k; i++) s += u[i];
        if ((uint8_t)~s != u[k - 1]) why = "its sum";
        else if (d->finishing ? memcmp(u + 14, "success", 8) != 0 : memcmp(u + 14, d->image + d->addr, d->len) != 0) why = "its bytes";
    }
    if (why) {
        d->bad_answers++;
        if (!d->bad[0]) snprintf(d->bad, sizeof d->bad, "answer to %#x+%u wrong in %s", d->addr, d->len, why);
        return;   // as the real one: not taken, asked again after five seconds
    }
    d->answers++;
    d->waiting = false;
    if (!d->finishing && now - d->t_req < d->fastest_ms) d->fastest_ms = now - d->t_req;
    if (d->finishing) {
        if (d->mode == D_CHECK) d->mode = D_AWAY1;
        else d->mode = D_AWAY2, d->wrote_all = d->written == d->image_len;
        d->t_mode = now;
        return;
    }
    if (d->mode == D_WRITE) d->written += d->len;
    d->k++;
    dev_next(d, now);
}
static void dev_tick(fm1_t *d, uint32_t now) {
    if (d->mode == D_AWAY1 && !d->no_loader && now - d->t_mode >= d->away1_ms) d->mode = D_LOADER;
    if (d->mode == D_AWAY2 && !d->no_return && now - d->t_mode >= d->away2_ms) d->mode = D_AFTER;
    if (d->mode == D_WRITE && d->paused && !d->waiting && d->k == d->pause_at && now - d->t_req >= d->pause_ms) dev_ask(d, now);
    if ((d->mode == D_CHECK || d->mode == D_WRITE) && d->waiting && now - d->t_req >= 5000) {
        if (++d->tries >= 3) {   // the session over: the app as it was; the loader still the loader
            d->waiting = false;
            d->gave_up++;
            d->mode = d->mode == D_CHECK ? D_APP : D_LOADER;
        } else {
            d->retries++;
            dev_ask(d, now);
        }
    }
}
// The cable pulled or the power cut and back: the app as it was, or (once step 1 is done) its loader.
static void dev_restart(fm1_t *d) {
    d->waiting = false;
    if (d->mode == D_CHECK) d->mode = D_APP;
    else if (d->mode == D_WRITE) d->mode = D_LOADER;
}
#endif
