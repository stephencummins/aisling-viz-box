// (test 24) The landscape out of the board's mini-HDMI socket: 640 x 480 at 60 Hz, as DVI. See hdmi.c.
#ifndef VIZ_HDMI_H_INCLUDED
#define VIZ_HDMI_H_INCLUDED
#include <stdint.h>

#define HDMI_SYS_MHZ 252   // ten bits for each of 25.2 million pixels a second, one bit a tick

// Before the system clock is raised to HDMI_SYS_MHZ: the core's voltage up to 1.20 V and the flash's clock divided
// further, so neither is asked to go faster than it did. Call it first thing in main, before the second core starts.
void hdmi_before_clock(void);

// Builds the lines and starts the picture (black, until hdmi_picture). From core 0, once the clock is at HDMI_SYS_MHZ.
void hdmi_start(void);

// (test 24) The cells the picture is made from: 120 x 68 of each of red, green and blue, read as they stand, a row
// of the picture at a time. And its shape in the 640 x 480: the whole of it, or a 16:9 band across the middle.
void hdmi_picture(const uint8_t *r, const uint8_t *g, const uint8_t *b);
const char *hdmi_shape(void);
const char *hdmi_next_shape(void);

// How hard the eight pins drive the cable. hdmi_next_drive steps to the next of the choices and says which.
const char *hdmi_drive(void);
const char *hdmi_next_drive(void);

typedef struct {
    uint32_t frames;        // pictures sent
    uint32_t late;          // lines whose turn came late (the picture slips when it does)
    uint32_t stalls;        // times a lane ran out of bits since the last look
    uint32_t flash_was;     // the flash's timing word as the board started, and as it is now
    uint32_t flash_now;
    uint32_t work_longest;  // (test 24) the picture's work: the longest turn since the last look, microseconds
    uint32_t work_frame;    // and all of it in the last frame (a frame is 16667)
} hdmi_state_t;
void hdmi_state(hdmi_state_t *s);

#endif
