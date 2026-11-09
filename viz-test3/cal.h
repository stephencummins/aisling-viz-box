// Test 3's calibration screen: ramps and swatches to judge the panel's tone curve by eye, and buttons to step the
// ILI9486's VCOM, gamma reference voltage (VRH) and gamma curve.
#ifndef VIZ_CAL_H_INCLUDED
#define VIZ_CAL_H_INCLUDED
#include <stdbool.h>
#include <stddef.h>

// The buttons, two rows of five along the bottom.
enum {
    CAL_VCOM_DN, CAL_VCOM_UP, CAL_VRH_DN, CAL_VRH_UP, CAL_CHIP,
    CAL_GAMMA, CAL_FIELD, CAL_VAL_DN, CAL_VAL_UP, CAL_LAND,
    CAL_BUTTONS
};
#define CAL_BTN_Y 256

void cal_draw(void);               // the whole screen
void cal_draw_info(void);          // the two lines of settings at the top, and the field marker
int cal_button_at(int x, int y);   // -1: not on a button
void cal_press_show(int b);        // which button a finger is on now (-1: none)

// Does one of the buttons' jobs (not CAL_LAND). fine: the serial keys' small steps rather than a finger's.
// Returns true if the screen had to be reset (it is black now and wants a full redraw).
bool cal_do(int b, bool fine);
bool cal_field_step(int d);        // the field marker back or on

// One line describing the panel's settings now, for the log.
void cal_describe(char *out, size_t n);

// From main.c.
void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif
