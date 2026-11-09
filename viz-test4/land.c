// The landscape, ported from the web visualiser's Land scene (web/visualiser/index.html in fm1-aisling), at the
// sizes the web uses for a 480 x 272 window: a 60 x 34 grid of wind and eddies over a 120 x 68 grid of paint.
//
// What is the web's, line for line: the drops (how they open, spread in fingers, shed eddies and fade), the wind and
// its slow swirl, the paint's drift, feathering and fading, the ground, the colours, the canvas grain.
// What is left out to fit the board: the full fluid solve (the eddies' own drift, vorticity confinement and the
// pressure projection), the sharpened advection, and the runs of paint down the canvas. The eddies are still kicked
// in where the web kicks them; here they stand where they land and die away.
//
// The screen: only the 16 x 16 tiles whose paint has changed are sent again. Each paint cell's colour is worked out
// once a frame, and a cell whose colour has moved by 3 or more (of 255) is updated in shown[]; the pixels are drawn
// only from shown[], so what is on the screen is always exactly what shown[] says, whatever was skipped.
#include <math.h>
#include <string.h>
#include "land.h"
#include "tone.h"

#define NX    60
#define NY    34
#define SX    (NX + 2)
#define NV    (SX * (NY + 2))
#define R     2
#define DX    (NX * R)            // 120
#define DY    (NY * R)            // 68
#define NP    (DX * DY)
#define SCALE 0.625f              // the web's cell size over its 12.8 px reference, at 480 wide
#define SMALL 0.8f                // the web's small-screen drop size
#define AMAX  3.2f
#define PN    64
#define PK    (PN / 6.2831853f)
#define NB    28

static inline float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }

// ---------------------------------------------------------------- dice, noise and fast maths

static uint32_t seed;
// Mulberry32, as the web's rand().
static float rnd(void) {
    seed += 0x6d2b79f5u;
    uint32_t t = (seed ^ (seed >> 15)) * (1u | seed);
    t = (t + (t ^ (t >> 7)) * (61u | t)) ^ t;
    return (float)((t ^ (t >> 14)) >> 8) * (1.0f / 16777216.0f); // 24 bits: a float can't hold 32 below 1
}

static inline float hash2(int32_t x, int32_t y) {
    uint32_t h = (uint32_t)x * 374761393u ^ (uint32_t)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (float)((h ^ (h >> 16)) >> 8) * (1.0f / 16777216.0f);
}

static float vnoise(float x, float y) {
    float xf0 = floorf(x), yf0 = floorf(y);
    int32_t xi = (int32_t)xf0, yi = (int32_t)yf0;
    float xf = x - xf0, yf = y - yf0;
    float sx = xf * xf * (3 - 2 * xf), sy = yf * yf * (3 - 2 * yf);
    float a = hash2(xi, yi), b = hash2(xi + 1, yi), c = hash2(xi, yi + 1), d = hash2(xi + 1, yi + 1);
    return a + (b - a) * sx + (c - a) * sy + (a - b - c + d) * sx * sy;
}

// e^x for x <= 0, to about 0.1%: 2^(x log2 e), the fraction by its series, the whole part into the exponent.
static inline float fast_exp(float x) {
    if (x < -80.0f) return 0.0f;
    float t = x * 1.44269504f, fi = floorf(t), f = t - fi;
    float p = 1.0f + f * (0.693147f + f * (0.240227f + f * (0.0555041f + f * 0.00961813f)));
    union { float f; int32_t i; } u = {p};
    u.i += (int32_t)((uint32_t)(int32_t)fi << 23); // (shifted unsigned: a negative int shifted left is undefined)
    return u.f;
}

// atan2, near enough for a 64-step lookup round the circle.
static inline float fast_atan2(float y, float x) {
    float ax = fabsf(x), ay = fabsf(y), mx = ax > ay ? ax : ay, mn = ax > ay ? ay : ax;
    if (mx == 0.0f) return 0.0f;
    float a = mn / mx, s = a * a;
    float r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;
    if (ay > ax) r = 1.57079637f - r;
    if (x < 0) r = 3.14159274f - r;
    return y < 0 ? -r : r;
}

// ---------------------------------------------------------------- colours

typedef struct { const char *name; uint8_t n[4]; uint32_t c[4][4]; } palette_t;

// Each layer's stops, in channel order (Drone, Sequence, Lead, Sequence 2), low note to high.
static const palette_t PALETTES[4] = {
    {"heather", {4, 4, 4, 4}, {{0x1d2230, 0x3e2a22, 0x6a2a3c, 0x9a4a63}, {0x2e3b20, 0x536a2a, 0x7f8a35, 0xb0ab55},
                               {0x7a3a1c, 0xb8622a, 0xd99b5a, 0xe8cfa0}, {0x262a4a, 0x4a4272, 0x7a7898, 0xa7a6b0}}},
    {"atlantic", {4, 4, 4, 3}, {{0x1b2029, 0x3d2a22, 0x5e3238, 0x8a4d5c}, {0x2f3d22, 0x4f5e2c, 0x7a7c3a, 0xa39a5a},
                                {0x56606e, 0x8e8f8c, 0xc9a46a, 0xe3c58e}, {0x2e3448, 0x4d5470, 0x7d7f99}}},
    {"kells", {4, 4, 3, 3}, {{0x3b1f3e, 0x5e2e5c, 0x86497a, 0xad6f8f}, {0x163f37, 0x245f52, 0x3f8270, 0x7fae96},
                             {0x9a3f1c, 0xc0782a, 0xd8ac3e}, {0x1c2550, 0x2f4683, 0x6a80b0}}},
    {"evening", {4, 4, 3, 3}, {{0x4e1a4e, 0x7a1030, 0xa3264a, 0xb24a78}, {0x1e4a30, 0x3a6a36, 0x5c8a3a, 0xa6b03e},
                               {0x7a3414, 0xc05a1e, 0xd98a3c}, {0x2a2456, 0x4a3f7a, 0x8a86a6}}},
};

typedef struct { const char *name; uint32_t top, bottom; float mottle, vignette; } ground_t;

static const ground_t GROUNDS[3] = {
    {"vellum", 0xf5edda, 0xd8c9a8, 9, 0.16f},
    {"night", 0x17131d, 0x0d0b11, 8, 0.4f},
    {"mist", 0xd9d6cf, 0x8e8b85, 7, 0.12f},
};

// The layers, in channel order. band: top and foot as fractions of the height; range: the notes it spans.
typedef struct { float band[2]; float range[2]; bool held; float size, conc; } layer_t;

static const layer_t LAYERS[4] = {
    {{0.66f, 0.93f}, {33, 69}, true, 1.45f, 0.95f},   // Drone, the bog
    {{0.42f, 0.66f}, {45, 86}, false, 0.85f, 1.1f},   // Sequence, the fields
    {{0.07f, 0.30f}, {57, 93}, true, 1.05f, 1.0f},    // Lead, the sky
    {{0.28f, 0.45f}, {40, 86}, false, 0.85f, 1.0f},   // Sequence 2, the hills
};
#define FLAT 0.4f // the landscape's: how far a drop spreads up and down against across

static int pal, gnd;
static bool redraw_all; // a new ground: every tile again

static void colour_of(int li, float note, float out[3]) {
    const layer_t *L = &LAYERS[li];
    int n = PALETTES[pal].n[li];
    const uint32_t *st = PALETTES[pal].c[li];
    float t = clampf((note - L->range[0]) / (L->range[1] - L->range[0]), 0, 1) * (float)(n - 1);
    int i = (int)t;
    if (i > n - 2) i = n - 2;
    float f = t - (float)i, s = 0.9f + 0.2f * rnd();
    for (int c = 0; c < 3; c++) {
        float a = (float)((st[i] >> (16 - 8 * c)) & 0xFF), b = (float)((st[i + 1] >> (16 - 8 * c)) & 0xFF);
        out[c] = clampf((a + (b - a) * f) * s, 0, 255);
    }
}

// ---------------------------------------------------------------- state

static float bufA[2][NP], bufR[2][NP], bufG[2][NP], bufB[2][NP];
static float *A, *CR, *CG, *CB, *TA, *TR, *TG, *TB;
static float u[NV], v[NV], eu[NV], ev[NV];
static uint8_t G0[NP], G1[NP], G2[NP];
static uint8_t SR[NP], SG[NP], SB[NP];      // what the screen shows, a paint cell at a time
static uint8_t colI[DX], rowJ[DY];
static float colF[DX], rowF[DY];
static float COV[1100];
static int8_t GRAIN[64][176];
static uint8_t dirty[LAND_TH][LAND_TW];
static float simT, windNow = 0.01f, bpm;

typedef struct {
    bool on, held;
    int16_t key;
    uint8_t li;
    float x, y, age, flow, q0, tau, rc, conc, spin, shed, maxR, tx, ty, area;
    float rgb[3];
    float prof[PN];
} bloom_t;

static bloom_t blooms[NB];

static void swap(float **a, float **b) { float *t = *a; *a = *b; *b = t; }

// ---------------------------------------------------------------- the ground and the grain

static void paint_ground(void) {
    const ground_t *g = &GROUNDS[gnd];
    float t[3], bo[3];
    for (int c = 0; c < 3; c++) {
        t[c] = (float)((g->top >> (16 - 8 * c)) & 0xFF);
        bo[c] = (float)((g->bottom >> (16 - 8 * c)) & 0xFF);
    }
    for (int b = 0, k = 0; b < DY; b++) {
        float fy = (float)b / (DY - 1), py = (float)(b * LAND_H) / DY;
        for (int a = 0; a < DX; a++, k++) {
            float px = (float)(a * LAND_W) / DX;
            float n = 0.6f * vnoise(px / 150, py / 24) + 0.4f * vnoise(px / 46 + 31, py / 8 - 7) - 0.5f;
            float ex = (a + 0.5f) / DX - 0.5f, ey = (b + 0.5f) / DY - 0.5f;
            float vig = 1 - g->vignette * clampf((ex * ex + ey * ey) * 2.2f, 0, 1);
            G0[k] = (uint8_t)clampf((t[0] + (bo[0] - t[0]) * fy + n * g->mottle) * vig, 0, 255);
            G1[k] = (uint8_t)clampf((t[1] + (bo[1] - t[1]) * fy + n * g->mottle) * vig, 0, 255);
            G2[k] = (uint8_t)clampf((t[2] + (bo[2] - t[2]) * fy + n * g->mottle * 1.2f) * vig, 0, 255);
        }
    }
}

// The web's canvas grain, laid over at 0.55 in overlay: B + 1.1 min(B, 255 - B) d, d the grain's offset from grey.
// A tile of 176 x 64 pixels whose noise lattice wraps (8 x 40 squares), so it repeats without a seam. Stored as
// 1.1 d in 256ths.
static float vnoise_wrap(float x, float y) {
    float xf0 = floorf(x), yf0 = floorf(y);
    int32_t xi = (int32_t)xf0, yi = (int32_t)yf0;
    float xf = x - xf0, yf = y - yf0;
    float sx = xf * xf * (3 - 2 * xf), sy = yf * yf * (3 - 2 * yf);
    int32_t x0 = xi % 8, x1 = (xi + 1) % 8, y0 = yi % 40, y1 = (yi + 1) % 40;
    float a = hash2(x0, y0), b = hash2(x1, y0), c = hash2(x0, y1), d = hash2(x1, y1);
    return a + (b - a) * sx + (c - a) * sy + (a - b - c + d) * sx * sy;
}

static void make_grain(void) {
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 176; x++) {
            float s = 0.55f * hash2(x * 7 + 3, y * 13 + 1) + 0.45f * vnoise_wrap(x / 22.0f, y / 1.6f);
            float d = (s - 0.5f) * 70.0f / 255.0f;
            GRAIN[y][x] = (int8_t)lroundf(1.1f * d * 256.0f);
        }
}

// ---------------------------------------------------------------- the wind

static void bnd(int b, float *x) {
    for (int j = 1; j <= NY; j++) { int r = SX * j; x[r] = x[r + NX]; x[r + NX + 1] = x[r + 1]; }
    int top = SX * (NY + 1);
    float s = b == 2 ? -1.0f : 1.0f;
    for (int i = 0; i < SX; i++) { x[i] = s * x[i + SX]; x[top + i] = s * x[top - SX + i]; }
}

static float wind_goal(void) {
    return bpm > 0 ? 0.012f + 0.08f * clampf(bpm / 100, 0.3f, 1.6f) : 0.01f;
}

static void wind(float dt) {
    windNow += (wind_goal() - windNow) * fminf(1, 0.008f * dt);
    float relax = fminf(1, 0.006f * dt);
    for (int j = 1; j <= NY; j++) {
        float y = (j - 0.5f) / NY;
        float prof = (1 - 0.55f * y) * (0.62f + 0.38f * sinf(j * 0.23f + simT * 0.0041f) * cosf(j * 0.071f - simT * 0.0023f));
        float goal = windNow * prof;
        int k = 1 + SX * j;
        for (int i = 1; i <= NX; i++, k++) u[k] += (goal - u[k]) * relax;
    }
    // and a slow swirl, so still water still breathes
    static float sx[NX + 1], cx[NX + 1];
    const float amp = 0.0011f * dt, kx = 6.2831853f / NX, ky = 0.07f;
    for (int i = 1; i <= NX; i++) {
        float ax = i * kx + simT * 0.0021f;
        sx[i] = sinf(ax);
        cx[i] = cosf(ax);
    }
    for (int j = 1; j <= NY; j++) {
        float sy = sinf(j * ky - simT * 0.0017f), cy = cosf(j * ky - simT * 0.0017f);
        float fu = amp * ky * cy * 12, fv = amp * kx * sy * 12;
        int k = 1 + SX * j;
        for (int i = 1; i <= NX; i++, k++) {
            u[k] += fu * sx[i];
            v[k] -= fv * cx[i];
        }
    }
}

static void sample_vel(float x, float y, float *su, float *sv) {
    float gx = x + 0.5f;
    gx = fmodf(fmodf(gx - 0.5f, NX) + NX, NX) + 0.5f;
    float gy = clampf(y + 0.5f, 0.5f, NY + 0.5f);
    int i0 = (int)gx, j0 = (int)gy;
    float fx = gx - i0, fy = gy - j0;
    int q = i0 + SX * j0;
    float w00 = (1 - fx) * (1 - fy), w10 = fx * (1 - fy), w01 = (1 - fx) * fy, w11 = fx * fy;
    *su = w00 * u[q] + w10 * u[q + 1] + w01 * u[q + SX] + w11 * u[q + SX + 1];
    *sv = w00 * v[q] + w10 * v[q + 1] + w01 * v[q + SX] + w11 * v[q + SX + 1];
}

static inline int wrapi(int i, int n) { i %= n; return i < 0 ? i + n : i; }

static void vortex(float cx, float cy, float rad, float s) {
    int box = (int)ceilf(rad * 3), ci = (int)floorf(cx), cj = (int)floorf(cy);
    float inv = 1 / (rad * rad), ir = 1 / rad;
    for (int dj = -box; dj <= box; dj++) {
        int c = cj + dj;
        if (c < 0 || c >= NY) continue;
        float dy = c + 0.5f - cy;
        int row = SX * (c + 1);
        for (int di = -box; di <= box; di++) {
            float dx = ci + di + 0.5f - cx;
            int k = row + wrapi(ci + di, NX) + 1;
            float g = fast_exp(-(dx * dx + dy * dy) * inv) * s;
            u[k] -= dy * g * ir;
            v[k] += dx * g * ir;
        }
    }
}

// ---------------------------------------------------------------- paint

// A dab, with bristle lines from the brush's seed (tx, ty).
static void stamp(float x, float y, float rad, const float rgb[3], float amt, float tx, float ty) {
    if (!(amt > 0)) return;
    float ax = x * R, ay = y * R, rd = fmaxf(0.8f, rad * R), ir2 = 1 / (rd * rd);
    int box = (int)ceilf(rd * 2.4f);
    const float bv = 1.6f / R;
    int ca = (int)floorf(ax), b0 = (int)floorf(ay) - box, b1 = (int)floorf(ay) + box;
    if (b0 < 0) b0 = 0;
    if (b1 > DY - 1) b1 = DY - 1;
    for (int b = b0; b <= b1; b++) {
        float dy = b + 0.5f - ay;
        for (int a = ca - box; a <= ca + box; a++) {
            float dx = a + 0.5f - ax;
            float w = fast_exp(-(dx * dx + dy * dy) * ir2) * amt;
            if (w < 0.002f) continue;
            float n = vnoise(tx + dx * 0.09f, ty + dy * bv) - 0.5f;
            w *= 1 + 1.1f * n;
            float j = 1 + 0.75f * n;
            int k = wrapi(a, DX) + DX * b;
            float na = A[k] + w;
            CR[k] += w * rgb[0] * j; CG[k] += w * rgb[1] * j; CB[k] += w * rgb[2] * j;
            // fresh paint over old: past the most a cell holds, the new colour pushes the old one out
            if (na > AMAX) { float s = AMAX / na; CR[k] *= s; CG[k] *= s; CB[k] *= s; na = AMAX; }
            A[k] = na;
        }
    }
}

static bloom_t *spawn(int li, float note, float vel) {
    const layer_t *L = &LAYERS[li];
    bloom_t *b = NULL;
    for (int i = 0; i < NB && !b; i++) if (!blooms[i].on) b = &blooms[i];
    if (!b) {
        // the oldest that isn't held; if all are held, the first
        for (int i = 0; i < NB; i++) if (!blooms[i].held && (!b || blooms[i].age > b->age)) b = &blooms[i];
        if (!b) b = &blooms[0];
    }
    float t = clampf((note - L->range[0]) / (L->range[1] - L->range[0]), 0, 1);
    float x = fmodf(fmodf((0.04f + 0.92f * t + (rnd() - 0.5f) * 0.05f) * NX, NX) + NX, NX);
    float y = (L->band[0] + (L->band[1] - L->band[0]) * (0.15f + 0.7f * rnd())) * NY;
    float size = L->size * (0.55f + (0.75f * vel) / 127) * SMALL;
    b->on = true; b->li = (uint8_t)li; b->x = x; b->y = y; b->age = 0; b->flow = 0; b->held = L->held; b->key = -1;
    b->q0 = (4.2f + rnd() * 2) * size * size * SCALE * SCALE;
    b->tau = 18 + rnd() * 12;
    b->rc = (0.7f + 0.3f * size) * SCALE;
    colour_of(li, note, b->rgb);
    b->conc = L->conc * (0.7f + (0.6f * vel) / 127);
    b->spin = (rnd() - 0.5f) * 0.04f;
    b->shed = 4 + rnd() * 6;
    b->maxR = 7 * L->size * SCALE;
    b->tx = rnd() * 500;
    b->ty = rnd() * 500;
    b->area = 3.14159274f * b->rc * b->rc;
    // an uneven edge: the paint pushes out in fingers
    int k1 = 3 + (int)(rnd() * 4);
    float p1 = rnd() * 6.283f, p2 = rnd() * 6.283f, p3 = rnd() * 6.283f, mn = 1e9f, mx = -1e9f;
    for (int i = 0; i < PN; i++) {
        float th = (float)i / PN * 6.2831853f;
        float n = sinf(k1 * th + p1) + 0.6f * sinf((2 * k1 + 1) * th + p2) + 0.4f * sinf(11 * th + p3);
        b->prof[i] = n;
        if (n < mn) mn = n;
        if (n > mx) mx = n;
    }
    float mean = 0;
    for (int i = 0; i < PN; i++) {
        b->prof[i] = 0.35f + 2 * powf(fmaxf(0, b->prof[i] - mn) / (mx - mn + 1e-9f), 1.7f);
        mean += b->prof[i];
    }
    for (int i = 0; i < PN; i++) b->prof[i] /= mean / PN;
    stamp(x, y, b->rc * 1.1f, b->rgb, b->conc * 0.8f, b->tx, b->ty);
    // eddies from the landing, so no two drops open alike
    int kicks = 3 + (int)(rnd() * 3);
    float est = sqrtf(b->q0 * b->tau / 3.14159274f);
    for (int q = 0; q < kicks; q++) {
        float a = rnd() * 6.283f, d = est * (0.5f + rnd());
        float rad = (1.2f + rnd() * 1.6f) * SCALE;
        float s = (rnd() < 0.5f ? -1 : 1) * (0.05f + rnd() * 0.08f) * size;
        vortex(x + cosf(a) * d, y + sinf(a) * d * 0.7f, rad, s);
    }
    return b;
}

static void update_blooms(float dt) {
    for (int bi = 0; bi < NB; bi++) {
        bloom_t *b = &blooms[bi];
        if (!b->on) continue;
        const layer_t *L = &LAYERS[b->li];
        b->age += dt;
        if (!b->held) b->flow += dt;
        float rf = sqrtf(b->area / 3.14159274f);
        float Q = b->q0 * (b->held ? 0.3f : fast_exp(-b->flow / b->tau));
        if (b->held && rf > b->maxR) Q *= 0.15f;   // a held note stops spreading but goes on feeding
        b->area += Q * dt;
        if (!b->held && Q < 0.03f && b->flow > 40) { b->on = false; continue; }
        // the spread moves paint, not water: it goes in eu/ev, which carry the paint
        float reach = fminf(rf * 2.2f + 4, 28), reach2 = reach * reach, ir2 = -1.6f / reach2;
        float qf = Q / 6.2831853f, rc2 = b->rc * b->rc, sp6 = b->spin * 6;
        int box = (int)ceilf(reach), ci = (int)floorf(b->x), cj = (int)floorf(b->y);
        for (int dj = -box; dj <= box; dj++) {
            int c = cj + dj;
            if (c < 0 || c >= NY) continue;
            float dy = c + 0.5f - b->y;
            int row = SX * (c + 1);
            for (int di = -box; di <= box; di++) {
                float dx = ci + di + 0.5f - b->x, r2 = dx * dx + dy * dy;
                if (r2 > reach2) continue;
                int k = row + wrapi(ci + di, NX) + 1;
                float pa = fast_atan2(dy, dx) * PK;
                if (pa < 0) pa += PN;
                int p0 = (int)pa;
                float pf = pa - p0;
                float mod = b->prof[p0 % PN] * (1 - pf) + b->prof[(p0 + 1) % PN] * pf;
                float f = qf * mod / (r2 + rc2) * fast_exp(r2 * ir2);
                eu[k] += dx * f - dy * f * sp6;
                ev[k] += FLAT * dy * f + dx * f * sp6;
            }
        }
        if (Q > 0.08f && (b->shed -= dt) <= 0) {
            b->shed = 5 + rnd() * 8;
            float a = rnd() * 6.283f;
            float rad = (1 + rnd() * 1.4f) * SCALE;
            float s = (rnd() < 0.5f ? -1 : 1) * (0.03f + rnd() * 0.05f) * fminf(1.5f, (3 * Q) / b->q0 + 0.3f);
            vortex(b->x + cosf(a) * rf, b->y + sinf(a) * rf * 0.7f, rad, s);
        }
        if (b->held) {
            // a held note rides the wind, so a drone draws a stroke
            float su, sv;
            sample_vel(b->x, b->y, &su, &sv);
            b->x = fmodf(fmodf(b->x + su * dt * 0.75f, NX) + NX, NX);
            b->y = clampf(b->y + sv * dt * 0.25f, L->band[0] * NY, L->band[1] * NY);
        }
        float conc = b->conc * (b->held ? 0.55f : 0.25f + 0.75f * fast_exp(-b->flow / 20));
        float mix = fminf(1, 0.05f * dt) * (b->held ? 1 : clampf((1.5f * Q) / b->q0, 0, 1));
        stamp(b->x, b->y, b->rc, b->rgb, conc * mix, b->tx, b->ty);
    }
}

// Carries the paint on the wind plus the drops' spread: back along the flow, bilinear. The web sharpens this
// (MacCormack) when it can afford to; this is its plain step, the one it falls back to.
static void advect_paint(float dt) {
    bnd(0, eu);
    bnd(0, ev);
    const float dr = dt * R, ymax = DY - 1.001f;
    for (int b = 0, idx = 0; b < DY; b++) {
        int base = SX * rowJ[b];
        float fy = rowF[b], gy = 1 - fy;
        for (int a = 0; a < DX; a++, idx++) {
            int q = colI[a] + base;
            float fx = colF[a], gx = 1 - fx;
            float w00 = gx * gy, w10 = fx * gy, w01 = gx * fy, w11 = fx * fy;
            float vx = dr * (w00 * (u[q] + eu[q]) + w10 * (u[q + 1] + eu[q + 1]) +
                             w01 * (u[q + SX] + eu[q + SX]) + w11 * (u[q + SX + 1] + eu[q + SX + 1]));
            float vy = dr * (w00 * (v[q] + ev[q]) + w10 * (v[q + 1] + ev[q + 1]) +
                             w01 * (v[q + SX] + ev[q + SX]) + w11 * (v[q + SX + 1] + ev[q + SX + 1]));
            float x = a - vx, y = b - vy;
            x -= DX * floorf(x / DX);
            if (!(x >= 0 && x < DX)) x = 0; // NaN, or a rounding to DX
            if (y < 0) y = 0; else if (y > ymax) y = ymax;
            else if (!(y >= 0)) y = 0;
            int x0 = (int)x, y0 = (int)y;
            float sx = x - x0, sy = y - y0;
            int x1 = x0 + 1 < DX ? x0 + 1 : 0, r0 = DX * y0, r1 = r0 + DX;
            float a00 = (1 - sx) * (1 - sy), a10 = sx * (1 - sy), a01 = (1 - sx) * sy, a11 = sx * sy;
            int s00 = x0 + r0, s10 = x1 + r0, s01 = x0 + r1, s11 = x1 + r1;
            TA[idx] = a00 * A[s00] + a10 * A[s10] + a01 * A[s01] + a11 * A[s11];
            TR[idx] = a00 * CR[s00] + a10 * CR[s10] + a01 * CR[s01] + a11 * CR[s11];
            TG[idx] = a00 * CG[s00] + a10 * CG[s10] + a01 * CG[s01] + a11 * CG[s11];
            TB[idx] = a00 * CB[s00] + a10 * CB[s10] + a01 * CB[s01] + a11 * CB[s11];
        }
    }
    swap(&A, &TA); swap(&CR, &TR); swap(&CG, &TG); swap(&CB, &TB);
}

// The paint feathers out, more across than down, and slowly fades (by half in 32 s), so the picture never fills.
static void soften(float dt) {
    const float keep = powf(0.5f, dt / 1920), d = dt < 2.5f ? dt : 2.5f; // the web feathers at most 2.5 frames a step
    const float kx = 0.009f * d, ky = 0.0025f * d, c = 1 - 2 * kx - 2 * ky;
    for (int b = 0, i = 0; b < DY; b++) {
        int up = b > 0 ? -DX : 0, dn = b < DY - 1 ? DX : 0;
        for (int a = 0; a < DX; a++, i++) {
            int l = a > 0 ? -1 : DX - 1, r = a < DX - 1 ? 1 : 1 - DX;
            TA[i] = keep * (c * A[i] + kx * (A[i + l] + A[i + r]) + ky * (A[i + up] + A[i + dn]));
            TR[i] = keep * (c * CR[i] + kx * (CR[i + l] + CR[i + r]) + ky * (CR[i + up] + CR[i + dn]));
            TG[i] = keep * (c * CG[i] + kx * (CG[i + l] + CG[i + r]) + ky * (CG[i + up] + CG[i + dn]));
            TB[i] = keep * (c * CB[i] + kx * (CB[i + l] + CB[i + r]) + ky * (CB[i + up] + CB[i + dn]));
        }
    }
    swap(&A, &TA); swap(&CR, &TR); swap(&CG, &TG); swap(&CB, &TB);
}

// ---------------------------------------------------------------- the API

void land_init(uint32_t s) {
    seed = s;
    A = bufA[0]; TA = bufA[1]; CR = bufR[0]; TR = bufR[1]; CG = bufG[0]; TG = bufG[1]; CB = bufB[0]; TB = bufB[1];
    memset(bufA, 0, sizeof bufA); memset(bufR, 0, sizeof bufR);
    memset(bufG, 0, sizeof bufG); memset(bufB, 0, sizeof bufB);
    memset(u, 0, sizeof u); memset(v, 0, sizeof v);
    memset(blooms, 0, sizeof blooms);
    for (int i = 0; i < NB; i++) blooms[i].key = -1;
    for (int i = 0; i < 1100; i++) {
        float t = clampf((i / 256.0f - 0.03f) / 0.42f, 0, 1);
        COV[i] = t * t * (3 - 2 * t);
    }
    for (int a = 0; a < DX; a++) { float g = (a + 0.5f) / R + 0.5f; colI[a] = (uint8_t)g; colF[a] = g - (int)g; }
    for (int b = 0; b < DY; b++) { float g = (b + 0.5f) / R + 0.5f; rowJ[b] = (uint8_t)g; rowF[b] = g - (int)g; }
    simT = 0;
    windNow = 0.01f;
    bpm = 0;
    pal = 0;
    gnd = 0;
    paint_ground();
    make_grain();
}

static void release(int key) {
    for (int i = 0; i < NB; i++)
        if (blooms[i].on && blooms[i].key == key) { blooms[i].held = false; blooms[i].key = -1; }
}

void land_note_on(uint8_t ch, uint8_t note, uint8_t vel) {
    if (!vel) { land_note_off(ch, note); return; }
    int li = ch & 3, key = li * 128 + (note & 127);
    release(key);
    bloom_t *b = spawn(li, (float)note, (float)vel);
    if (LAYERS[li].held) b->key = (int16_t)key;
}

void land_note_off(uint8_t ch, uint8_t note) { release((ch & 3) * 128 + (note & 127)); }

void land_all_off(int ch) {
    for (int i = 0; i < NB; i++) {
        int key = blooms[i].key;
        if (key >= 0 && (ch < 0 || ((key >> 7) & 3) == (ch & 3))) release(key);
    }
}

void land_set_bpm(float b) { bpm = b; }

void land_step(float dt) {
    simT += dt;
    memset(eu, 0, sizeof eu);
    memset(ev, 0, sizeof ev);
    wind(dt);
    update_blooms(dt);
    const float du = powf(0.998f, dt), dw = powf(0.95f, dt);
    for (int i = 0; i < NV; i++) {
        float a = u[i] * du, b = v[i] * dw;
        u[i] = a == a ? clampf(a, -3, 3) : 0;
        v[i] = b == b ? clampf(b, -3, 3) : 0;
    }
    bnd(1, u);
    bnd(2, v);
    advect_paint(dt);
    soften(dt);
}

static void mark(int a, int b) {
    int x0 = (4 * a - 2) >> 4, x1 = (4 * a + 5) >> 4, y0 = (4 * b - 2) >> 4, y1 = (4 * b + 5) >> 4;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > LAND_TW - 1) x1 = LAND_TW - 1;
    if (y1 > LAND_TH - 1) y1 = LAND_TH - 1;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) dirty[y][x] = 1;
}

static inline uint8_t to8(float x) { return (uint8_t)(x < 0 ? 0 : x > 255 ? 255 : x); }
static inline int absdiff(int a, int b) { return a > b ? a - b : b - a; }

int land_prepare(bool force) {
    force = force || redraw_all;
    redraw_all = false;
    memset(dirty, force ? 1 : 0, sizeof dirty);
    for (int b = 0, i = 0; b < DY; b++) {
        int up = b > 0 ? -DX : 0, dn = b < DY - 1 ? DX : 0;
        for (int a = 0; a < DX; a++, i++) {
            float g0 = G0[i], g1 = G1[i], g2 = G2[i], am = A[i], r = g0, g = g1, bl = g2;
            if (am > 0.003f) {
                int ci = (int)(am * 256);
                if (ci > 1099) ci = 1099;
                float cov = COV[ci], inv = 1 / am;
                // a wet edge: darker where the amount changes fast; the paint's thickness lit from the upper left
                int l = a > 0 ? i - 1 : i + DX - 1, rr = a < DX - 1 ? i + 1 : i - DX + 1;
                float gx = A[rr] - A[l], gy = A[i + dn] - A[i + up];
                float lit = 1 + clampf(0.16f * (gx + gy), -0.2f, 0.24f) - 0.1f * fminf(1, sqrtf(gx * gx + gy * gy));
                r = g0 + (CR[i] * inv * lit - g0) * cov;
                g = g1 + (CG[i] * inv * lit - g1) * cov;
                bl = g2 + (CB[i] * inv * lit - g2) * cov;
            }
            uint8_t r8 = to8(r), g8 = to8(g), b8 = to8(bl);
            if (force || absdiff(r8, SR[i]) >= 3 || absdiff(g8, SG[i]) >= 3 || absdiff(b8, SB[i]) >= 3) {
                SR[i] = r8; SG[i] = g8; SB[i] = b8;
                if (!force) mark(a, b);
            }
        }
    }
    int n = 0;
    for (int y = 0; y < LAND_TH; y++)
        for (int x = 0; x < LAND_TW; x++) n += dirty[y][x];
    return n;
}

bool land_next_run(int ty, int tx, int *x0, int *n) {
    while (tx < LAND_TW && !dirty[ty][tx]) tx++;
    if (tx >= LAND_TW) return false;
    int e = tx;
    while (e < LAND_TW && dirty[ty][e]) e++;
    *x0 = tx;
    *n = e - tx;
    return true;
}

static const uint8_t BAYER[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

// A pixel at 4c + k takes paint cells c - 1 and c (k = 0, 1) or c and c + 1 (k = 2, 3), the second weighted
// 5, 7, 1 or 3 eighths: bilinear between cell centres.
static inline void taps(int p, int n, int *c0, int *c1, int *w1) {
    int c = p >> 2, k = p & 3;
    static const int8_t W[4] = {5, 7, 1, 3};
    if (k < 2) { *c0 = c - 1; *c1 = c; } else { *c0 = c; *c1 = c + 1; }
    *w1 = W[k];
    if (*c0 < 0) *c0 = 0;
    if (*c1 > n - 1) *c1 = n - 1;
}

void land_render(int px, int py, int w, int h, uint16_t *out) {
    static uint16_t vr[DX], vg[DX], vb[DX];   // a row of cells, blended down, in eighths
    static int8_t xc0[LAND_W], xc1[LAND_W], xw[LAND_W];   // cell index offsets for this run, worked out once
    int cl = (px >> 2) - 1, ch = ((px + w - 1) >> 2) + 1;
    if (cl < 0) cl = 0;
    if (ch > DX - 1) ch = DX - 1;
    for (int x = 0; x < w; x++) {
        int c0, c1, w1;
        taps(px + x, DX, &c0, &c1, &w1);
        xc0[x] = (int8_t)(c0 - cl); xc1[x] = (int8_t)(c1 - cl); xw[x] = (int8_t)w1;
    }
    for (int y = 0; y < h; y++) {
        int yy = py + y, r0, r1, wy;
        taps(yy, DY, &r0, &r1, &wy);
        const int o0 = r0 * DX, o1 = r1 * DX, iy = 8 - wy;
        for (int c = cl; c <= ch; c++) {
            vr[c - cl] = (uint16_t)(SR[o0 + c] * iy + SR[o1 + c] * wy);
            vg[c - cl] = (uint16_t)(SG[o0 + c] * iy + SG[o1 + c] * wy);
            vb[c - cl] = (uint16_t)(SB[o0 + c] * iy + SB[o1 + c] * wy);
        }
        const int8_t *gr = GRAIN[yy & 63];
        const uint8_t *by = BAYER[yy & 3];
        int gx = px % 176;
        uint16_t *o = out + y * w;
        for (int x = 0; x < w; x++) {
            int a = xc0[x], b = xc1[x], wx = xw[x], ix = 8 - wx;
            int r = (vr[a] * ix + vr[b] * wx + 32) >> 6;
            int g = (vg[a] * ix + vg[b] * wx + 32) >> 6;
            int bl = (vb[a] * ix + vb[b] * wx + 32) >> 6;
            int q = gr[gx];
            if (++gx == 176) gx = 0;
            r += ((r < 255 - r ? r : 255 - r) * q) >> 8;
            g += ((g < 255 - g ? g : 255 - g) * q) >> 8;
            bl += ((bl < 255 - bl ? bl : 255 - bl) * q) >> 8;
            g = tone8(g, tone_g);    // test 4's warmth, before the dither so it keeps the fine steps
            bl = tone8(bl, tone_b);
            int d = by[(px + x) & 3];
            int r5 = (r + (d >> 1)) >> 3, g6 = (g + (d >> 2)) >> 2, b5 = (bl + (d >> 1)) >> 3;
            if (r5 > 31) r5 = 31;
            if (g6 > 63) g6 = 63;
            if (b5 > 31) b5 = 31;
            if (r5 < 0) r5 = 0;
            if (g6 < 0) g6 = 0;
            if (b5 < 0) b5 = 0;
            o[x] = (uint16_t)((r5 << 11) | (g6 << 5) | b5);
        }
    }
}

const char *land_next_palette(void) { pal = (pal + 1) % 4; return PALETTES[pal].name; }
const char *land_next_ground(void) { gnd = (gnd + 1) % 3; paint_ground(); redraw_all = true; return GROUNDS[gnd].name; }
const char *land_palette(void) { return PALETTES[pal].name; }
const char *land_ground(void) { return GROUNDS[gnd].name; }

int land_blooms_live(void) {
    int n = 0;
    for (int i = 0; i < NB; i++) n += blooms[i].on;
    return n;
}

float land_wind(void) { return windNow; }
