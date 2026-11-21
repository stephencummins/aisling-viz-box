// (test 13) Any firmware on the FM-1, not only AISLING (2026-11-15, Stephen: "It works with my firmware. But could it
// work with any other?").
//
// The box already asks the FM-1 who it is: the screen reader's HELLO, every 2 seconds (reader.h). Some 47 firmwares
// carry the same reader (Charles Vestal's fm1-reader) and answer with their name ("X0X 1.1"); others say nothing.
// So the box knows which of four it has:
//
//   ASKING    plugged in, no answer yet (3 seconds at most)
//   AISLING   it named itself AISLING: everything as test 11
//   GUEST     it named itself something else: the landscape from its notes, and what it says on a page of text
//   SILENT    no answer: the landscape from its notes, nothing else
//
// What the box sends is AISLING's own: program changes 0-7 for the strip's pieces, CC 16-19 for a page's knobs, CCs
// for the effects and the pad. To another firmware those are someone else's controls turned at random, so the box
// sends them to AISLING only (guest_may_send). To anything else it sends the reader's HELLO and WHERE and nothing
// more.
//
// The landscape's four layers are AISLING's four MIDI channels. Another firmware's channels mean something else, so
// its notes are placed by pitch (guest_layer). Two of the layers hold their paint until the note is let go; a
// firmware that never sends a note off would hold them for ever, so a guest's held notes are let go here after
// GUEST_HOLD_MS (guest_note, guest_expire).
#ifndef VIZ_GUEST_H_INCLUDED
#define VIZ_GUEST_H_INCLUDED
#include <stdint.h>
#include <stdbool.h>

enum { WHO_NONE, WHO_ASKING, WHO_SILENT, WHO_AISLING, WHO_GUEST };

#define GUEST_ASK_MS  3000u   // no answer in this long: silent (a HELLO goes at once, then every 2 seconds)
#define GUEST_HOLD_MS 8000u   // a guest's held note, let go after this long without a note off
#define GUEST_BOTTOM  272     // the page is y 0..271, as the FM-1's
#define GUEST_TEXT    240     // the reader's longest SAY
#define GUEST_HIST    6       // earlier things said, kept one line each
#define GUEST_HIST_W  76      // characters of each

typedef struct {
    uint8_t who;
    bool forced;                  // a silent one taken for AISLING by hand (key i)
    uint32_t since_ms;            // when it was plugged in
    char name[24];                // as it named itself
    char now[GUEST_TEXT + 1];     // the newest thing said
    bool now_warn;                // ... was a warning (the reader's KEEP flag)
    char hist[GUEST_HIST][GUEST_HIST_W + 1];   // the ones before it, newest first
    uint8_t n_hist;
    uint32_t n_said;
    uint32_t held_ms[128];        // a held note's start (0: not held), by note
    uint8_t dirty;                // what of the page to draw
    int8_t drawn_scale;           // the size the newest was last drawn at
    uint8_t drawn_rows;
} guest_t;

void guest_init(guest_t *g);
// Plugged in, its MIDI endpoints open: asking. Unplugged: none.
void guest_plugged(guest_t *g, uint32_t now);
void guest_gone(guest_t *g);
// It answered the HELLO. name is from guest_hello_name; aisling is perf_fw's verdict. True if who it is changed.
bool guest_named(guest_t *g, const char *name, bool aisling);
// Asking for too long becomes silent. True if who it is changed.
bool guest_tick(guest_t *g, uint32_t now);
// Key i: a silent FM-1 taken for AISLING (one from before the reader), or put back. True if who it is changed.
bool guest_force(guest_t *g);

// The name out of a HELLO answer (the bytes between F0 and F7: 7D 53 52 01 ver name 00 ...), printable ASCII only,
// into out (n bytes, always ended). False if it isn't a HELLO answer; out is then empty.
bool guest_hello_name(const uint8_t *buf, int len, char *out, int n);

// The one rule for everything the box sends that is AISLING's: only with a MIDI OUT endpoint, and only to AISLING.
bool guest_may_send(const guest_t *g, bool midi_out);
// The guest's page stands in for the FM-1's page: for a guest and for a silent one.
bool guest_page(const guest_t *g);

// The landscape's layer for a note: AISLING's is its channel; anything else's is by pitch (0 bog under C3, 1 fields
// to B3, 3 hills to B4, 2 sky above).
int guest_layer(const guest_t *g, uint8_t ch, uint8_t note);
// A note on (vel > 0) or off from something that isn't AISLING, remembered so a held one can be let go.
void guest_note(guest_t *g, uint8_t note, uint8_t vel, uint32_t now);
// The next note held too long, forgotten and returned for the caller to let go; -1 when there is none.
int guest_expire(guest_t *g, uint32_t now);
void guest_notes_clear(guest_t *g);

// Something a guest said (a SAY's text, n bytes, and its flags).
void guest_say(guest_t *g, const char *text, int n, uint8_t flags);

// The page, y 0..GUEST_BOTTOM-1: guest_redraw marks all of it, guest_draw draws what has changed.
void guest_redraw(guest_t *g);
void guest_draw(guest_t *g);
// The strip, in place of AISLING's eight pieces: two lines of at most 78 characters. False for AISLING, asking and
// no FM-1 (the pieces are drawn).
bool guest_strip(const guest_t *g, char a[80], char b[80]);

// Text wrapped at spaces into rows of at most w characters; a word longer than w is cut. Returns the rows used (at
// most max_rows; the rest of the text is dropped). Each row is ended. Exposed for the host check.
int guest_wrap(const char *t, int w, int max_rows, char rows[][GUEST_HIST_W + 4]);

#endif
