// perf.h: (test 8) the box's two playing pages, with no hardware in them: main.c feeds the touch in and sends the
// CCs they ask for; the host checks drive them the same way.
//
//   The effects page: AISLING 1.3's 27 PERFORM effects as a grid. A tap holds an effect, a second tap lets it go
//   (its CC, 20-31 and 102-116: 127 holds, 0 lets go; ui.c's cc_in takes them on any page). ALL OFF lets all 27 go.
//   The pad: one finger moves two of PERFORM's four knobs at once, by their fixed CCs (74 Filter, 78 Wet,
//   79 Energy, 80 Chorus), each value the middle of the CCs AISLING turns into it (knobtab.h, as the knobs' page).
//
// The box shows what it sent: AISLING says an effect's name when it's held and nothing when it's let go, and says a
// fixed CC's knob only on PERFORM. No change to AISLING.
#ifndef VIZ_PERF_H_INCLUDED
#define VIZ_PERF_H_INCLUDED
#include <stdbool.h>
#include <stdint.h>
#include "pflash.h"

#define PERF_BOTTOM 272                 // the pages' area: y 0 .. 271 (the status row and the strip stay)
#define PERF_VIEW   17                  // PERFORM in fm1scr.c's pages: its four knobs are the pad's
#define PERF_PADS   4                   // the pad's choices: PERFORM's knobs Filter, Wet, Energy, Chorus
#define PERF_NSLOT  (PFLASH_N + PERF_PADS)   // what's sent: slot k < 27 effect k's CC, then the four knobs'

// the effects page: 5 x 6 cells, 27 effects and three buttons
#define FX_COLS 5
#define FX_ROWS 6
#define FX_CW   96
#define FX_CH   45
#define FX_CELLS (FX_COLS * FX_ROWS)
enum { FXB_ALLOFF = -2, FXB_PAD = -3, FXB_LAND = -4 };   // a cell that isn't an effect (-1: nothing)

// the pad page: the pad left, five buttons right
#define XY_PAD_W 384
#define XY_MARGIN 16                    // the pad's ends are this far in, so a finger reaches them
#define XY_BTN_H 54
enum { XYB_X, XYB_Y, XYB_RESET, XYB_FX, XYB_LAND, XY_BTNS };
#define XY_JUMP_PX 40                   // a reading this far from the last is held until the next agrees (a glitch)
#define XY_SLACK_PX 3                   // a value changes only when the finger has moved this far since it last did
#define XY_DETENT 3                     // CCs either side of the middle that are the middle (Filter, Wet, Energy)

enum { PERF_STAY, PERF_GO_FX, PERF_GO_PAD, PERF_GO_LAND };   // what a release asks of main.c

typedef struct {
    // the effects
    uint8_t held[PFLASH_N];             // 1: the box holds it (the last CC sent for it was 127)
    int8_t fx_cell, fx_n;               // the touch now: the cell under it (-1 none) and readings in a row there
    bool fx_done;                       // this touch has done its one thing
    int8_t fx_press;                    // a button shown pressed (cell), -1
    uint32_t fx_dirty;                  // bit c: cell c to draw; bit 31: the whole page
    // the pad
    uint8_t axis[2];                    // which of the four knobs X and Y turn
    int16_t idx[PERF_PADS];             // each knob's value as last sent (its list's index), -1: not sent yet
    bool in_pad, have, pend;            // the touch started on the pad; a place accepted; one waiting for the next
    int16_t hx, hy, px, py;             // accepted, waiting
    int16_t at[2];                      // where the finger was when each axis last changed
    int8_t xy_btn, xy_press;            // the button this touch started on, and the one shown pressed (-1)
    int16_t cross[2];                   // the cross drawn: x and y (-1 none)
    uint32_t xy_dirty;                  // XYD_*
    // out: main.c sends out[s] on perf_slot_cc(s) for each bit of due, and clears it
    uint8_t out[PERF_NSLOT];
    uint32_t due;
    uint32_t n_holds, n_lets, n_refused, n_moves, n_glitches;
} perf_t;
enum { XYD_ALL = 1, XYD_CROSS = 2, XYD_BTN0 = 4 };   // XYD_BTN0 << b: button b

void perf_init(perf_t *p);
void perf_gone(perf_t *p);              // the FM-1 unplugged, or it left PERFORM (which lets every effect go): nothing
                                        // held, nothing sent; unplugged also forgets the pad (forget_pad)
void perf_forget_pad(perf_t *p);
int perf_slot_cc(int slot);             // the CC a slot is sent on
int perf_fx_cc(int k);                  // effect k's (AISLING's pkey_cc)
int perf_nheld(const perf_t *p);
int perf_fx_at(int cell);               // the effect in a cell, or FXB_*
int perf_fx_cell_at(int x, int y);      // -1: off the page
const char *perf_pad_name(int knob);    // "Filter" ...
const char *perf_pad_text(const perf_t *p, int knob);   // its value as AISLING says it, "" if not sent yet
int perf_xy_cc(int knob, int px, bool y_axis);   // the CC a place on the pad is, before its value's middle is taken

// live: an FM-1 is there to send to (else a tap changes nothing and counts in n_refused)
void perf_fx_touch(perf_t *p, int x, int y, bool first, bool live);
int perf_fx_release(perf_t *p, bool ok);
void perf_fx_redraw(perf_t *p);
void perf_fx_draw(perf_t *p);

void perf_xy_touch(perf_t *p, int x, int y, bool first, bool live);
int perf_xy_release(perf_t *p, bool ok, bool live);
void perf_xy_redraw(perf_t *p);
void perf_xy_draw(perf_t *p);

// The uncial hand (ufont.h), capitals: s drawn with its top left at x, y, fg on bg; returns its width. size 15 or 24.
int perf_utext_w(int size, const char *s);
int perf_utext(int x, int y, const char *s, int size, uint16_t fg, uint16_t bg);
#endif
