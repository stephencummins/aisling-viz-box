// (test 12) The box's settings in a text file on the card, SETTINGS.TXT: one "name = value" a line, a # starting a
// note. The box reads it at start-up and when it gets the card back from a computer, and writes it (all of it, with
// notes saying what each value does) a couple of seconds after one of them changes on the box.
//
// settings_t is plain data: main.c fills one from what the box is doing and applies one the file gave. The names of
// the palettes, grounds and pad knobs here are land.c's and perf.c's, in their order (the host checks compare them).
#ifndef VIZ_SETTINGS_H_INCLUDED
#define VIZ_SETTINGS_H_INCLUDED
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "lcd.h"

#define SETTINGS_FILE "SETTINGS.TXT"
#define SETTINGS_MAX  3072   // the most of the file read; the box's own is about 1.6 KB

#define SET_N_PALETTES 4
#define SET_N_GROUNDS  3
#define SET_N_PADS     4
#define SET_N_SPEEDS   3
extern const char *const SET_PALETTES[SET_N_PALETTES];   // land.c's PALETTES
extern const char *const SET_GROUNDS[SET_N_GROUNDS];     // land.c's GROUNDS
extern const char *const SET_PADS[SET_N_PADS];           // perf.c's PAD_NAME
extern const uint8_t SET_SPEEDS_MHZ[SET_N_SPEEDS];       // main.c's SPEEDS_MHZ

typedef struct {
    uint8_t warmth;            // tone_step(), 0 .. TONE_STEPS - 1
    uint8_t spi;               // main.c's spi_i: SET_SPEEDS_MHZ[spi] asked for the screen
    uint8_t palette, ground;   // indexes into SET_PALETTES and SET_GROUNDS
    panel_t panel;             // VCOM, VRH and gamma, each the chip's own or set
    uint8_t pad[2];            // the pad's knobs, across and up (perf_t's axis): indexes into SET_PADS, different
} settings_t;

enum {
    SET_WARMTH = 1 << 0, SET_SPI = 1 << 1, SET_PALETTE = 1 << 2, SET_GROUND = 1 << 3,
    SET_VCOM = 1 << 4, SET_VRH = 1 << 5, SET_GAMMA = 1 << 6, SET_PAD_ACROSS = 1 << 7, SET_PAD_UP = 1 << 8,
};

typedef struct {
    unsigned got;       // SET_ bits: the settings the text gave, with good values
    int bad;            // lines the box couldn't use: a value that isn't one, no "=", too long
    int unknown;        // names that aren't settings
    bool cut;           // the file was longer than SETTINGS_MAX: the rest wasn't read
    int first_line;     // the first bad or unknown line's number (0: none)
    char first[64];     // and what was wrong with it
} settings_report_t;

// Reads the text over *s: each setting it gives with a good value replaces s's; the rest stay as they were.
void settings_parse(const char *text, size_t len, settings_t *s, settings_report_t *r);
// The file's text for s, into out (at most cap bytes, with a 0 after). Returns its length, or 0 if it didn't fit.
// gamma_name: what the calibration screen calls the gamma now (cal_gamma_name), for a note.
size_t settings_format(const settings_t *s, const char *gamma_name, char *out, size_t cap);
// The same settings (a panel setting left to the chip is the same whatever value it holds).
bool settings_same(const settings_t *a, const settings_t *b);

// The file, on the card FatFs has mounted. settings_load: 1 read (over *s, as settings_parse), 0 no file, -1 it
// couldn't be read. settings_save: true if written; settings_error() says why not.
int settings_load(settings_t *s, settings_report_t *r);
bool settings_save(const settings_t *s, const char *gamma_name);
const char *settings_error(void);
const char *settings_fr_name(int fr);   // a FatFs FRESULT, in words

#endif
