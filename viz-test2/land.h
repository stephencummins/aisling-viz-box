// The landscape: a cut-down port of the web visualiser's Land scene (web/visualiser/index.html in fm1-aisling).
// Lead (ch3) in the sky, Sequence 2 (ch4) in the hills, Sequence (ch2) in the fields, Drone (ch1) in the bog.
// Pitch sets x, velocity sets how much paint, MIDI clock tempo sets the wind.
#ifndef VIZ_LAND_H_INCLUDED
#define VIZ_LAND_H_INCLUDED
#include <stdint.h>
#include <stdbool.h>

// The painted area: 480 x 272 pixels at the top of the screen, as 120 x 68 paint cells of 4 x 4 pixels.
#define LAND_W  480
#define LAND_H  272
// Sent to the screen in 16 x 16 tiles, only those that have changed: 30 across, 17 down.
#define LAND_TILE 16
#define LAND_TW   (LAND_W / LAND_TILE)
#define LAND_TH   (LAND_H / LAND_TILE)

void land_init(uint32_t seed);

// MIDI in. ch is 0-15; the web takes ch & 3 as the layer, and so does this.
void land_note_on(uint8_t ch, uint8_t note, uint8_t vel);
void land_note_off(uint8_t ch, uint8_t note);
void land_all_off(int ch); // ch < 0: every layer

// Tempo from MIDI clock, or 0 when the clock has stopped. Sets the wind.
void land_set_bpm(float bpm);

// One step of the paint. dt is in 60ths of a second, as the web's is.
void land_step(float dt);

// Works out each paint cell's colour and which tiles have changed enough to send again (forgetting the last
// frame's). Returns how many tiles are dirty. force: all of them.
int land_prepare(bool force);

// The dirty tiles, a row of tiles at a time: tile row ty (0-16), the first dirty run at or after tile tx.
// Returns false when the row has no more runs; otherwise *x0 and *n (tiles) are the run.
bool land_next_run(int ty, int tx, int *x0, int *n);

// Renders pixels [px, px + w) x [py, py + h) into out (RGB565, row stride w), with the canvas grain and dither.
void land_render(int px, int py, int w, int h, uint16_t *out);

// Choices, as the web's C and G keys: palette (heather, atlantic, kells, evening) and ground (vellum, night, mist).
const char *land_next_palette(void);
const char *land_next_ground(void);   // forces a full redraw on the next land_prepare

const char *land_palette(void);
const char *land_ground(void);

// For the status line.
int land_blooms_live(void);
float land_wind(void);

#endif
