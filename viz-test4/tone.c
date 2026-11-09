// The picture's warmth. See tone.h.
#include "tone.h"

// Green and blue in 256ths, step 0 (none) to 4 (strongest). Blue comes down about three times as far as green,
// which moves the white towards a warm one rather than a green or magenta one. Step 2 takes the vellum ground
// (F5 ED DA) to F5 E1 BB and white to FF F2 DB.
static const uint16_t GAIN_G[TONE_STEPS] = {256, 250, 243, 237, 230};
static const uint16_t GAIN_B[TONE_STEPS] = {256, 238, 220, 202, 184};

static int step;          // 0 until tone_set: unchanged
uint16_t tone_g = 256, tone_b = 256;

void tone_set(int s) {
    if (s < 0) s = 0;
    if (s > TONE_STEPS - 1) s = TONE_STEPS - 1;
    step = s;
    tone_g = GAIN_G[s];
    tone_b = GAIN_B[s];
}

int tone_step(void) { return step; }

uint16_t tone_gain_g(int s) { return GAIN_G[s < 0 ? 0 : s >= TONE_STEPS ? TONE_STEPS - 1 : s]; }
uint16_t tone_gain_b(int s) { return GAIN_B[s < 0 ? 0 : s >= TONE_STEPS ? TONE_STEPS - 1 : s]; }
