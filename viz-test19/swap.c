// SPDX-License-Identifier: GPL-3.0-only
// (test 19) The host's side of Felucca's update: see swap.h. A port to C of tools/fm1_install.py in fm1-aisling,
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments (GPL-3.0-only).
#include "swap.h"
#include <string.h>

static const uint8_t HS_QUERY[] = {0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7};
static const uint8_t UPGRADE[] = {0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7};
static const char MARK[] = "FELUCCA-LOADER-1";
#define BLOCKS 20u
#define BLK 0x30u
#define KEEP 0x2Fu

// ---------------------------------------------------------------- 7 bits

// Bytes in, 7-bit bytes out, least significant bits first.
typedef struct {
    uint32_t acc, nb, o;
    uint8_t *out;
} p7_t;
static void p7_put(p7_t *p, uint8_t b) {
    p->acc |= (uint32_t)b << p->nb;
    p->nb += 8;
    while (p->nb >= 7) {
        p->out[p->o++] = p->acc & 0x7F;
        p->acc >>= 7;
        p->nb -= 7;
    }
}
static void p7_end(p7_t *p) {
    if (p->nb) p->out[p->o++] = p->acc & 0x7F;
    p->nb = 0;
}

uint32_t swap_pack7(const uint8_t *in, uint32_t n, uint8_t *out) {
    p7_t p = {.out = out};
    for (uint32_t i = 0; i < n; i++) p7_put(&p, in[i]);
    p7_end(&p);
    return p.o;
}

// The bits left over at the end (fewer than 8) are dropped.
uint32_t swap_unpack7(const uint8_t *in, uint32_t n, uint8_t *out, uint32_t max) {
    uint32_t acc = 0, nb = 0, o = 0;
    for (uint32_t i = 0; i < n; i++) {
        acc |= (uint32_t)(in[i] & 0x7F) << nb;
        nb += 7;
        while (nb >= 8) {
            if (o < max) out[o] = acc & 0xFF;
            o++;
            acc >>= 8;
            nb -= 8;
        }
    }
    return o;   // what there was: more than max means it didn't fit
}

// ---------------------------------------------------------------- the messages

static bool framed(const uint8_t *sx, uint32_t n) { return n >= 2 && sx[0] == 0xF0 && sx[n - 1] == 0xF7; }

bool swap_request(const uint8_t *sx, uint32_t n, uint8_t *flags, uint32_t *addr, uint32_t *len) {
    uint8_t u[15];
    if (!framed(sx, n)) return false;
    if (swap_unpack7(sx + 1, n - 2, u, sizeof u) != 15) return false;
    if (u[0] != 0x00 || u[1] != 0x59 || u[2] != 0x30) return false;
    uint32_t sum = 0;
    for (int i = 6; i < 14; i++) sum += u[i];
    if ((uint8_t)~sum != u[14]) return false;
    *flags = u[6];
    *addr = (uint32_t)u[7] | (uint32_t)u[8] << 8 | (uint32_t)u[9] << 16 | (uint32_t)u[10] << 24;
    *len = (uint32_t)u[11] | (uint32_t)u[12] << 8 | (uint32_t)u[13] << 16;
    return true;
}

uint32_t swap_response(uint32_t addr, const uint8_t *data, uint32_t n, uint8_t flags, uint8_t *out) {
    if (n > SWAP_MAXDATA) n = SWAP_MAXDATA;
    uint8_t head[14] = {0x00, 0x59, 0x30, (uint8_t)(n + 8), (uint8_t)((n + 8) >> 8), 0, flags,
                        (uint8_t)addr, (uint8_t)(addr >> 8), (uint8_t)(addr >> 16), (uint8_t)(addr >> 24),
                        (uint8_t)n, (uint8_t)(n >> 8), 0};
    p7_t p = {.out = out + 1};
    uint32_t sum = 0;
    out[0] = 0xF0;
    for (int i = 0; i < 14; i++) {
        if (i >= 6) sum += head[i];
        p7_put(&p, head[i]);
    }
    for (uint32_t i = 0; i < n; i++) {
        sum += data[i];
        p7_put(&p, data[i]);
    }
    p7_put(&p, (uint8_t)~sum);
    p7_end(&p);
    out[1 + p.o] = 0xF7;
    return p.o + 2;
}

bool swap_name_ok(const char *t) {
    int i = 0;
    while (t[i] && t[i] != '_') i++;
    if (i == 0 || t[i] != '_') return false;
    int d = 0;
    for (i++; t[i]; i++, d++)
        if (t[i] < '0' || t[i] > '9') return false;
    return d > 0;
}

bool swap_identity(const uint8_t *sx, uint32_t n, swap_id_t *id) {
    uint8_t d[34];
    if (!framed(sx, n)) return false;
    if (swap_unpack7(sx + 1, n - 2, d, sizeof d) != 34) return false;
    if (d[0] != 0x00 || d[1] != 0x59 || d[2] != 0x11) return false;
    const uint8_t *body = d + 6;   // 27 bytes
    int len = 0;
    while (len < 27 && body[len]) len++;
    memcpy(id->text, body, (size_t)len);
    id->text[len] = 0;
    if (!swap_name_ok(id->text)) return false;
    // the text up to its first zero: bytes 24..26 may hold the chip
    id->chip = len < 24 ? (uint32_t)body[24] << 16 | (uint32_t)body[25] << 8 | body[26] : 0;
    const char *t = id->text;
    id->loader = (t[0] == 'o' || t[0] == 'O') && (t[1] == 't' || t[1] == 'T') && (t[2] == 'a' || t[2] == 'A') &&
                 t[3] == '-';
    return true;
}

// The model: up to the "_", without a leading "ota-".
static const char *model_of(const char *t, int *n) {
    int e = 0;
    while (t[e] && t[e] != '_') e++;
    if (e >= 4 && !memcmp(t, "ota-", 4)) t += 4, e -= 4;
    *n = e;
    return t;
}

bool swap_same_model(const char *a, const char *b) {
    int na, nb;
    const char *ma = model_of(a, &na), *mb = model_of(b, &nb);
    return na == nb && !memcmp(ma, mb, (size_t)na);
}

// One marker byte after each of the first 20 blocks: letter i is the marker less i + 1, and 0x7D is no letter.
// (A product with a zero byte in it is refused here; fm1_install.py would take it for a letter.)
bool swap_product(const uint8_t *head, char *text) {
    int o = 0;
    for (uint32_t i = 0; i < BLOCKS; i++) {
        uint8_t m = head[i * BLK + KEEP];
        if (m == 0x7D) continue;
        uint8_t c = (uint8_t)(m - i - 1u);
        if (!c) return false;
        text[o++] = (char)c;
    }
    text[o] = 0;
    return swap_name_ok(text);
}

void swap_head(const uint8_t *file, uint8_t *logical) {
    for (uint32_t i = 0; i < BLOCKS; i++) memcpy(logical + i * KEEP, file + i * BLK, KEEP);
}

uint32_t swap_file_off(uint32_t logical) {
    return logical < BLOCKS * KEEP ? logical / KEEP * BLK + logical % KEEP : logical + BLOCKS;
}

// "F" is in the mark once, at its start: after a letter that doesn't fit, the only way on is a new "F".
bool swap_mark(uint32_t *st, const uint8_t *p, uint32_t n) {
    uint32_t k = *st;
    for (uint32_t i = 0; i < n && k < sizeof MARK - 1; i++) k = p[i] == (uint8_t)MARK[k] ? k + 1 : p[i] == 'F';
    *st = k;
    return k >= sizeof MARK - 1;
}

// ---------------------------------------------------------------- USB-MIDI packets

// Cable 0. Code index 4: three bytes of a SysEx that goes on; 5, 6, 7: its last one, two or three.
uint32_t swap_packets(const uint8_t *sx, uint32_t n, uint32_t *at, uint8_t *out, uint32_t max) {
    uint32_t o = 0;
    while (o + 4 <= max && *at < n) {
        uint32_t left = n - *at, take = left > 3 ? 3 : left;
        out[o] = (uint8_t)(left > 3 ? 0x04 : 0x04 + left);
        for (uint32_t i = 0; i < 3; i++) out[o + 1 + i] = i < take ? sx[*at + i] : 0;
        *at += take;
        o += 4;
    }
    return o;
}

int swap_collect(swap_col_t *c, const uint8_t p[4]) {
    int cin = p[0] & 0x0F, k = cin == 4 || cin == 7 ? 3 : cin == 6 ? 2 : cin == 5 ? 1 : 0;
    for (int i = 1; i <= k; i++) {
        uint8_t b = p[i];
        if (b == 0xF0) {
            c->in = true;
            c->over = false;
            c->n = 0;
        } else if (!c->in) {
            continue;
        } else if (b >= 0x80 && b != 0xF7) {   // another status: it was cut short
            c->in = false;
            continue;
        }
        if (c->n < SWAP_MSG_MAX) c->buf[c->n++] = b;
        else c->over = true;
        if (b == 0xF7) {
            c->in = false;
            return c->over ? 0 : c->n;
        }
    }
    return 0;
}

// ---------------------------------------------------------------- the install

static bool due(uint32_t now, uint32_t t) { return (int32_t)(now - t) >= 0; }

static void queue_wire(swap_t *s, uint32_t at) {
    s->pending = true;
    s->pending_at = at;
}
static void queue_bytes(swap_t *s, const uint8_t *b, uint32_t n, uint32_t at) {
    memcpy(s->wire, b, n);
    s->wire_n = n;
    queue_wire(s, at);
}

static void fail(swap_t *s, int error) {
    s->state = SWAP_FAILED;
    s->error = error;
    s->pending = false;
}

void swap_init(swap_t *s) { memset(s, 0, sizeof *s); }

static void ask(swap_t *s, uint32_t now, int again_error) {
    s->state = SWAP_ASK;
    s->t_state = now;
    s->t_next = now;
    s->tries = 0;
    s->again = again_error;
    s->pending = false;
}

void swap_start(swap_t *s, const swap_io_t *io, const char *product, uint32_t image_len, bool linked, uint32_t now) {
    swap_init(s);
    s->io = *io;
    strncpy(s->product, product, sizeof s->product - 1);
    s->image_len = image_len;
    s->linked = linked;
    ask(s, now, 0);
}

// UPGRADE goes; the first request has SWAP_START_MS more than the others to come in.
static void serve_begin(swap_t *s, int state, uint32_t now) {
    s->state = state;
    s->t_state = now;
    s->t_last = now + SWAP_START_MS;
    s->pct = 0;
    s->misreads = 0;
    queue_bytes(s, UPGRADE, sizeof UPGRADE, now);
}

void swap_link(swap_t *s, bool up, uint32_t now) {
    bool was = s->linked;
    s->linked = up;
    if (!up) {
        s->pending = false;   // what was going is gone
        return;
    }
    // The box's own: the cable back in the middle of a step. Who is it now? Its loader: the write, from the start.
    if (!was && (s->state == SWAP_CHECK || s->state == SWAP_WRITE))
        ask(s, now, s->state == SWAP_WRITE ? SWAP_E_HALF : SWAP_E_STOPPED);
}

static void served(swap_t *s, uint32_t addr, uint32_t n, uint8_t flags, uint32_t now) {
    if (addr == SWAP_FINISH_CHECK || addr == SWAP_FINISH_WRITE) {
        static const uint8_t yes[8] = "success";
        s->wire_n = swap_response(addr, yes, sizeof yes, 0, s->wire);
        queue_wire(s, now);
        if (s->state == SWAP_CHECK && addr == SWAP_FINISH_CHECK) {
            s->state = SWAP_LOADER;
            s->checked = true;
            s->t_state = now;
            s->t_next = now + SWAP_LOADER_MS;
        } else if (s->state == SWAP_WRITE && addr == SWAP_FINISH_WRITE) {
            s->state = SWAP_REBOOT;
            s->pct = 100;
            s->t_state = now;
            s->t_next = now + SWAP_REBOOT_MS;
        }
        return;
    }
    if (n > SWAP_MAXDATA || (uint64_t)addr + n > s->image_len) {
        s->bad_addr = addr;
        s->bad_n = n;
        fail(s, SWAP_E_BADREQ);
        return;
    }
    if (!s->io.read(s->io.ctx, addr, s->data, n)) {
        // The box's own: the FM-1 asks three times, five seconds apart; the card has as many goes.
        if (++s->misreads >= 3) fail(s, SWAP_E_CARD);
        return;
    }
    s->misreads = 0;
    s->wire_n = swap_response(addr, s->data, n, flags, s->wire);
    queue_wire(s, now + SWAP_REPLY_MS);
    s->served++;
    if (s->state == SWAP_WRITE) {
        uint32_t pct = (uint32_t)(((uint64_t)addr + n) * 100u / s->image_len);
        s->pct = pct > 99 ? 99 : pct;
    }
}

// The box's own: the answer to a step's finish may be lost on the way; the FM-1 then asks for it again, and gets it
// again, though the box has gone on to wait for what comes next.
static bool again_finish(swap_t *s, const uint8_t *sx, uint32_t n, uint32_t finish, uint32_t now) {
    static const uint8_t yes[8] = "success";
    uint8_t flags;
    uint32_t addr, len;
    if (!swap_request(sx, n, &flags, &addr, &len) || addr != finish) return false;
    s->wire_n = swap_response(addr, yes, sizeof yes, 0, s->wire);
    queue_wire(s, now);
    return true;
}

void swap_rx(swap_t *s, const uint8_t *sx, uint32_t n, uint32_t now) {
    swap_id_t id;
    uint8_t flags;
    uint32_t addr, len;
    switch (s->state) {
    case SWAP_ASK:
        if (!swap_identity(sx, n, &id)) break;
        memcpy(s->device, id.text, sizeof s->device);
        if (!swap_same_model(id.text, s->product)) {
            fail(s, SWAP_E_MODEL);
        } else if (id.loader) {   // already in update mode: only the write
            if (!s->checked) s->resumed = true;
            serve_begin(s, SWAP_WRITE, now);
        } else {
            serve_begin(s, SWAP_CHECK, now);
        }
        break;
    case SWAP_CHECK:
    case SWAP_WRITE:
        if (!swap_request(sx, n, &flags, &addr, &len)) break;
        s->t_last = now;
        served(s, addr, len, flags, now);
        break;
    case SWAP_LOADER:
        if (swap_identity(sx, n, &id) && id.loader) serve_begin(s, SWAP_WRITE, now);
        else again_finish(s, sx, n, SWAP_FINISH_CHECK, now);
        break;
    case SWAP_REBOOT:
        if (again_finish(s, sx, n, SWAP_FINISH_WRITE, now)) break;
        if (!swap_identity(sx, n, &id) || id.loader) break;
        memcpy(s->back, id.text, sizeof s->back);
        if (strcmp(id.text, s->product)) fail(s, SWAP_E_MISMATCH);
        else s->state = SWAP_DONE;
        break;
    }
}

// A look: "who are you", when one is due and nothing is waiting to go.
static void look(swap_t *s, uint32_t now, uint32_t again_ms) {
    if (!s->linked || s->pending || !due(now, s->t_next)) return;
    queue_bytes(s, HS_QUERY, sizeof HS_QUERY, now);
    s->t_next = now + again_ms;
    s->tries++;
}

void swap_tick(swap_t *s, uint32_t now) {
    if (!swap_busy(s)) return;
    if (s->pending && s->linked && due(now, s->pending_at) && s->io.send(s->io.ctx, s->wire, s->wire_n))
        s->pending = false;
    switch (s->state) {
    case SWAP_ASK:
        if (s->again) {   // the cable was out: as long as the loader is given to appear
            if (now - s->t_state >= SWAP_WAIT_AGAIN_MS) fail(s, s->again);
            else look(s, now, SWAP_RETRY_MS);
        } else if (s->linked && !s->pending && due(now, s->t_next) && s->tries >= SWAP_HS_TRIES) {
            fail(s, SWAP_E_SILENT);
        } else if (!s->linked && now - s->t_state >= SWAP_HS_TRIES * SWAP_HS_MS) {
            fail(s, SWAP_E_SILENT);
        } else {
            look(s, now, SWAP_HS_MS);
        }
        break;
    case SWAP_CHECK:
        if ((int32_t)(now - s->t_last) > (int32_t)SWAP_IDLE_CHECK_MS) fail(s, SWAP_E_STOPPED);
        break;
    case SWAP_WRITE:
        if ((int32_t)(now - s->t_last) > (int32_t)SWAP_IDLE_WRITE_MS) fail(s, SWAP_E_HALF);
        break;
    case SWAP_LOADER:
        if (now - s->t_state >= SWAP_LOADER_MS + SWAP_WAIT_LOADER_MS) fail(s, SWAP_E_NOLOADER);
        else look(s, now, SWAP_RETRY_MS);
        break;
    case SWAP_REBOOT:
        if (now - s->t_state >= SWAP_REBOOT_MS + SWAP_WAIT_REBOOT_MS) fail(s, SWAP_E_NORETURN);
        else look(s, now, SWAP_RETRY_MS);
        break;
    }
}
