// The FM-1's screen on the box (tests 6 and 7): rebuilt from what AISLING says to a screen reader (announce.c's SAY), and
// drawn in y 0-271 of the box's screen.
//
// AISLING says everything on its screen in two texts when a reader asks (WHERE, or a new reader's HELLO):
//   "Drone page, voice Pipes: Pedal 90, Reso 0, Phaser 15, Attack 40. Piece Skellig, D Minor, tempo 76, playing"
//   "Chord F minor 7, next chord 40% of the way. Section Flow. Scale D, E, F, G, A, Bb, C"
// and then what changes: a page ("Lead page, voice Flute: ..."), a knob ("Ornament 35"), a chord ("Chord A minor"),
// the tempo ("Tempo EXT"), PLAYING / RESTING, a new piece ("PIECE Aran, A Dorian, tempo 96"), and the messages its
// buttons show ("SEQ MUTED"). Its grammar is AISLING's ui.c (sp_page, sp_garden, speak); fm1scr.c reads it back.
// Some of the screen isn't said as it changes (how far to the next chord, the scale after a new mode, a value a
// button moved): fm1scr_say sets want_where, and the box asks with a WHERE (main.c).
// Test 7: the box turns the page's knobs too (knobctl.c). What it's sending is drawn here (fm1scr_set_preview), and
// heard[] counts the FM-1 saying each knob turned: its answer.
#ifndef VIZ_FM1SCR_H_INCLUDED
#define VIZ_FM1SCR_H_INCLUDED
#include <stdint.h>
#include <stdbool.h>

#define FM1_VIEWS 27          // AISLING's pages (ui.c's NVIEWS)
#define FM1_SAY_FLAG_NOW  1u  // announce.c's ANN_NOW: cuts off what's being said (a knob, a page, a button)
#define FM1_SAY_FLAG_KEEP 2u  // ANN_KEEP: a warning or an error

// The dirty bits: what fm1scr_draw has to draw again.
enum {
    FM1_D_HEAD = 1u << 0,     // the piece, key, tempo, playing, not saved; or a message over them
    FM1_D_PAGE = 1u << 1,     // the page's name and what's beside it (its voice, character, ...)
    FM1_D_KNOB = 1u << 2,     // << i for knob i (4 bits)
    FM1_D_KNOBS = 15u << 2,
    FM1_D_CHORD = 1u << 6,    // the chord and the section
    FM1_D_NEXT = 1u << 7,     // how far to the next chord
    FM1_D_LINE = 1u << 8,     // the line under it: the filter's motion, or Setup's CPU and power
    FM1_D_SCALE = 1u << 9,
    FM1_D_WAIT = 1u << 10,    // the waiting screen (nothing heard yet)
    FM1_D_CLEAR = 1u << 11,   // clear the whole area first
    FM1_D_ALL = 0xFFFu,
};

typedef struct {
    bool seen;                // anything heard since the start (or the FM-1 was plugged in)
    // the header
    char piece[24], root[4], mode[16], tempo[8];   // as said: "Tir na nOg", "Eb", "Mixolyd.", "76" or "EXT"
    int8_t playing, unsaved;                       // -1: not said yet
    // the page
    int8_t view;                                   // VIEW_NAME's index; -1 none yet
    char detail[40];                               // "voice Pipes", "character Surf", "walk Home", "sleep off", ...
    char value[4][24];                             // each knob's value as said ("16 bars", "1/4.", "Off")
    int8_t turned;                                 // the knob turned last (-1 none), bright for a moment
    uint32_t turned_ms;
    // the garden
    char chord[28];                                // "F minor 7"
    int8_t next_pct;                               // how far to the next chord (0-100); -1: not moving
    int8_t hold;                                   // 0 moving, 1 hold (HOLD on), 2 held (no drift)
    char section[12];
    char line[48];                                 // "Cutoff moving darker, about -1 octave" (a sound page)
    char extra[64];                                // Setup's "CPU 3%, peak 12%, on USB power"
    char scale[64];                                // "D, E, F, G, A, Bb and B, C"
    // a message over the header
    char msg[160];
    bool warn;
    uint32_t msg_until;
    // for the box
    uint32_t dirty;
    bool want_where;          // something changed that wasn't all said: ask for the whole screen
    uint32_t n_said, n_page, n_knob, n_garden, n_chord, n_msg, n_header, n_unmatched, n_cut;
    // the drawing's own: the plug state the screen was last drawn for
    int8_t drawn_conn;
    // (test 7) the FM-1 saying knob i turned ("Ornament 40", or on the Key page "Tempo 80", "MODE Dorian"): counted
    uint16_t heard[4];
    // (test 7) the box's own, set by knobctl.c: the knobs it can't turn (their labels dimmed), and the value it's
    // sending (in orange, with a bar for where it is in the knob's range) until the FM-1 says it back
    uint8_t locked;
    const char *pv[4];
    uint8_t pv_idx[4], pv_n[4];
} fm1scr_t;

void fm1scr_init(fm1scr_t *m);
// Everything drawn again (the FM-1 page has just come up).
void fm1scr_redraw(fm1scr_t *m);
// One SAY: its flags, then its n bytes of text (not 0-terminated).
void fm1scr_say(fm1scr_t *m, const char *text, uint32_t n, uint8_t flags, uint32_t now_ms);
// Times out the message and the turned knob's brightness.
void fm1scr_tick(fm1scr_t *m, uint32_t now_ms);

// conn: 0 no FM-1 plugged in, 1 plugged in but its screen reader hasn't answered, 2 answering.
// Draws what's dirty in y 0-271; nothing at or below 272.
void fm1scr_draw(fm1scr_t *m, int conn, uint32_t now_ms);

// (test 7) Knob i's value as the box is sending it (text NULL: none; idx of n, for the bar); the knobs it can't turn
// (bit i); a warning of the box's own over the header ("THE FM-1 DIDN'T TAKE IT"), as one of the FM-1's.
void fm1scr_set_preview(fm1scr_t *m, int i, const char *text, int idx, int n);
void fm1scr_set_locked(fm1scr_t *m, uint8_t mask);
void fm1scr_note(fm1scr_t *m, const char *text, uint32_t now_ms);

// For the screen and the log: s in capitals, except a flat's b ("Bb", "Eb MINOR"). out holds max chars and a 0.
void fm1scr_caps(char *out, const char *s, int max);
// The page's name (or "?"), for the log.
const char *fm1scr_view_name(int view);
const char *fm1scr_label(int view, int i);

#define FM1SCR_BOTTOM 272     // the area's end: the status row starts here
#endif
