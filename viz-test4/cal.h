// Test 4's calibration screen: ramps and swatches to judge the picture by eye, and buttons to step our warmth
// (tone.h) and the ILI9486's gamma reference voltage (VRH) and gamma curve. It starts on test 3's pick, gamma
// FALLING, which lcd_init sends at every start-up.
#ifndef VIZ_CAL_H_INCLUDED
#define VIZ_CAL_H_INCLUDED
#include <stdbool.h>
#include <stddef.h>
#include "lcd.h"

// The buttons, two rows of five along the bottom, then the jobs only keys do.
enum {
    CAL_WARM_DN, CAL_WARM_UP, CAL_VRH_DN, CAL_VRH_UP, CAL_START,
    CAL_GAMMA, CAL_FIELD, CAL_VAL_DN, CAL_VAL_UP, CAL_LAND,
    CAL_BUTTONS,
    CAL_VCOM_DN = CAL_BUTTONS, CAL_VCOM_UP, // VCOM made no difference in test 3, so it is keys only
    CAL_CHIP,                               // the chip's own settings and no warmth: the picture as test 2 sent it
};
#define CAL_BTN_Y 256

// The panel settings test 4 starts with (and START goes back to): gamma FALLING, VCOM and VRH the chip's own.
extern const panel_t CAL_START_PANEL;

void cal_draw(void);               // the whole screen
void cal_draw_info(void);          // the two lines of settings at the top, and the field marker
int cal_button_at(int x, int y);   // -1: not on a button
void cal_press_show(int b);        // which button a finger is on now (-1: none)

// Does one of the buttons' or keys' jobs (not CAL_LAND). fine: the keys' small steps rather than a finger's.
// Returns true if the whole screen wants drawing again: after a reset (the screen is black) or a warmth change.
bool cal_do(int b, bool fine);
bool cal_field_step(int d);        // the field marker back or on

// One line describing the settings now (panel and warmth), for the log.
void cal_describe(char *out, size_t n);

// From main.c.
void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif
