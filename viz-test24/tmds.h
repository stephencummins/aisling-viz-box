// (test 23) TMDS, the coding DVI and HDMI send their picture in, written from the DVI 1.0 specification (3.2):
// every 8-bit value becomes 10 bits, least significant first, chosen so the wire changes seldom and carries as many
// ones as zeroes. No board code here: the host checks build this file on the Mac.
#ifndef VIZ_TMDS_H_INCLUDED
#define VIZ_TMDS_H_INCLUDED
#include <stdbool.h>
#include <stdint.h>

// 640 x 480 at 60 Hz (the one picture every screen takes): 800 symbols a line, 525 lines.
#define TMDS_H_FRONT 16
#define TMDS_H_SYNC 96
#define TMDS_H_BACK 48
#define TMDS_H_BLANK (TMDS_H_FRONT + TMDS_H_SYNC + TMDS_H_BACK)   // 160
#define TMDS_H_ACTIVE 640
#define TMDS_H_TOTAL (TMDS_H_BLANK + TMDS_H_ACTIVE)               // 800
#define TMDS_V_ACTIVE 480
#define TMDS_V_FRONT 10
#define TMDS_V_SYNC 2
#define TMDS_V_BACK 33
#define TMDS_V_TOTAL (TMDS_V_ACTIVE + TMDS_V_FRONT + TMDS_V_SYNC + TMDS_V_BACK)   // 525
// A line as the serialiser takes it: one bit for each tick, 32 to a word, the first bit sent in bit 0.
#define TMDS_LINE_WORDS (TMDS_H_TOTAL * 10 / 32)                   // 250

// The four symbols sent while the picture is blanked; they carry two bits (on lane 0, the syncs).
uint16_t tmds_ctrl(bool c0, bool c1);

// One 8-bit value as 10 bits. *disparity is how many more ones than zeroes the lane has sent since the last blanking
// (it starts each line's picture at 0), and is brought up to date.
uint16_t tmds_data(uint8_t d, int *disparity);

// One whole line for one lane, TMDS_LINE_WORDS words: the blanking first (front porch, sync, back porch), then the
// picture. px: the lane's 640 values, or NULL for a line of the vertical blanking. sync_lane: lane 0, which carries
// the syncs in its blanking (both are low while they pulse, as 640 x 480 has them); vsync: this line is inside the
// vertical sync pulse.
void tmds_line(uint32_t *out, const uint8_t *px, bool sync_lane, bool vsync);

#endif
