// The picture's warmth, set on our side before anything is sent.
//
// Test 3 found the panel's tone curve (gamma FALLING gives an even grey ramp and dark darks) but the vellum ground
// still showed white, not cream. The panel's gamma is one curve for red, green and blue alike: it can make a colour
// lighter or darker, not warmer. So the warmth is ours: green and blue are scaled down by a step's gains, red is
// left alone. A scale on the 0-255 values is a white-balance scale on the light for a screen with a power-law
// response, which is near enough what an LCD is.
//
// Step 0 sends every colour unchanged (as tests 2 and 3 did). Each step up is warmer.
#ifndef VIZ_TONE_H_INCLUDED
#define VIZ_TONE_H_INCLUDED
#include <stdint.h>

#define TONE_STEPS 5
#define TONE_START 2   // the middle, where test 4 starts

// The gains now, in 256ths. Red's is always 256.
extern uint16_t tone_g, tone_b;

void tone_set(int step);   // clamped to 0 .. TONE_STEPS - 1
int tone_step(void);
// The step's gains in 256ths, for the log.
uint16_t tone_gain_g(int step);
uint16_t tone_gain_b(int step);

// An 8-bit level through a gain: (v * gain + 128) >> 8, so a gain of 256 leaves it as it was.
static inline int tone8(int v, uint16_t gain) { return (v * gain + 128) >> 8; }

// An RGB565 colour through the gains, for flat colours and text (the landscape takes tone8 before its dither).
static inline uint16_t tone_565(uint16_t c) {
    uint32_t g6 = (c >> 5) & 63, b5 = c & 31;
    g6 = (g6 * tone_g + 128) >> 8;
    b5 = (b5 * tone_b + 128) >> 8;
    return (uint16_t)((c & 0xF800) | (g6 << 5) | b5);
}

#endif
