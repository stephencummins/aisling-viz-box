// Test 7: the FM-1's four knobs turned from the box. On the FM-1's page, a finger dragged sideways across a knob
// (y 80-141) steps it through its values, and the box sends each as AISLING 1.2's CC 16-19 (ui.c's cc_in: the four
// knobs of the page on the FM-1's screen, 0..127 across the knob's range). Right is more. It's relative: the drag
// starts from the value the FM-1 last said, so nothing jumps.
//
// What's being sent shows in orange with a bar (fm1scr_set_preview) until the FM-1 says the knob back. AISLING doesn't
// always: a turn that moves another knob too is said as that one (Play to Endless turns the slots, and is said as
// "Chord Minor"), Motion moving a knob in the same moment is said instead, and now and then a turn isn't said at all
// (the host sweep found all three). So if it hasn't in 300 ms, the box asks for the whole page (a WHERE), and the
// page that comes back settles it. If that shows something else, or nothing has come in 1.5 seconds, the FM-1's
// own value comes back and THE FM-1 DIDN'T TAKE IT shows over the header (the grid up, a firmware without the CCs,
// ...). But Filter, Wet, Energy and Sequence 2's Octave are moved by Motion too (a turn holds the macros only till
// the next section change, then they glide back): for those, a page nearer what was sent than what the knob was
// before the drag is taken, then moved (one step back from a one-step drag can't be told from not taken: warned). The knobs AISLING takes no CC for (Setup's four), and a value the box doesn't know (Mode on
// "Custom"), are dimmed and don't move.
//
// The MODE say drops the mode's full stop ("Mode Mixolyd." on the page, said "MODE Mixolyd": ui.c's msg_text), so a
// value that isn't in its list as said is looked for without its trailing full stops; only then, since Echo time's
// "1/4" and "1/4." are both in its list, and both said as they are (knobctl_find).
//
// A touch that starts on a knob the box can turn is the knobs' and nothing else's: not a tap, not a hold, so a finger
// resting on a knob never flips the page. One on a dimmed or empty knob is too if it moves (a drag never flips the
// page); if it doesn't, it's a tap or a hold as anywhere else on the page (test 6's, main.c).
//
// No hardware here: main.c feeds it the touch and sends what it says, so the host checks can drive it.
#ifndef VIZ_KNOBCTL_H_INCLUDED
#define VIZ_KNOBCTL_H_INCLUDED
#include <stdint.h>
#include <stdbool.h>
#include "fm1scr.h"

#define KC_ROW_Y0 80          // the knob row: y 80-141 (the knobs and the rule under them)
#define KC_ROW_Y1 142
#define KC_CELL_W 120
#define KC_START_PX 10        // a touch in the row moves this far before it's a drag
#define KC_TRAVEL_PX 300      // the whole of a knob's range, about (each step 3 to 24 px)
#define KC_STEP_MIN 3
#define KC_STEP_MAX 24
#define KC_JUMP_PX 40         // a reading this far from the last is held until the next one agrees (a glitch)
#define KC_ASK_MS 300u        // the FM-1 says a knob back in about 120 ms; not by this, the box asks for a WHERE
#define KC_ANSWER_MS 1500u    // nothing settled by this (no page back either): it didn't take it
#define KC_CC_FIRST 16        // AISLING's CC_PAGE

typedef struct {
    // the touch
    bool in_row;              // this touch started in the knob row
    bool dead;                // ... and is over for the knobs (the page changed under it, the FM-1 went)
    bool dragging;
    bool cand;                // a reading far from the last, held (cand_x)
    bool locked_press;        // it started on a knob the box can't turn (or an empty one)
    bool moved;               // ... and has moved KC_START_PX since
    int8_t knob, view;        // the knob touched, and the FM-1's page then
    int8_t list;              // its list in knobtab.h
    int16_t x, x0, cand_x;    // the last reading taken; where the steps count from
    int16_t base, at;         // the index the drag started at, and the one it's at
    // each knob's last send, until the FM-1 says it back
    struct {
        bool waiting;
        int8_t view, list;
        int16_t idx;
        int16_t before;       // the knob's value before the drag (Motion's: a page nearer idx than this is taken)
        uint32_t ms;
        uint16_t heard;       // fm1scr's heard[] then
        bool asked;           // the box asked for a WHERE for it (KC_ASK_MS after the send) ...
        uint32_t pages;       // ... when fm1scr's n_page was this: the next page line settles it
    } w[4];
    // what each knob's value is in its list (worked out again when the value or the page changes)
    int8_t c_view[4];
    int8_t c_list[4];
    int16_t c_idx[4];
    char c_val[4][24];
    // for main.c to send: CC 16+i = cc[i], each time gen[i] changes; and a WHERE when want_where (main.c clears it)
    uint8_t cc[4];
    uint32_t gen[4];
    bool want_where;
    // for the log: knob i's last send was from[i] to to[i] (indexes in its list)
    int16_t from[4], to[4];
    // counts
    uint32_t n_drags, n_sent, n_answered, n_not_taken, n_locked, n_cancelled, n_glitches;
    uint32_t n_asked, n_paged;   // WHEREs asked for; waits the page then settled (answered or not)
    uint32_t n_moved;            // ... of those answered, the page a step or two off: Motion's (KT_MOVES)
} knobctl_t;

void knobctl_init(knobctl_t *k);
// A touch reading on the FM-1's page (first: the touch's first). Returns the knobs with a new CC to send (bit i).
unsigned knobctl_touch(knobctl_t *k, fm1scr_t *m, int x, int y, bool first, uint32_t now_ms);
// The touch is over. True if it was the knobs': then it's not a tap or a hold.
bool knobctl_release(knobctl_t *k, fm1scr_t *m, uint32_t now_ms);
// After the FM-1's says and before drawing: its answers, the WHEREs to ask for, the time-outs, the dimmed knobs, what's
// shown being sent.
void knobctl_tick(knobctl_t *k, fm1scr_t *m, uint32_t now_ms);
// The FM-1 unplugged, or its page left: the drag and the waits forgotten (the counts kept), and what was shown being
// sent, if m.
void knobctl_reset(knobctl_t *k, fm1scr_t *m);

// The parts, for the checks and the log.
int knobctl_idx_of(int cc, int n);    // AISLING's: CC cc to the index of n (cc_in: (cc*(n-1)+63)/127)
int knobctl_cc_for(int idx, int n);   // the CC for idx: the middle of the CCs AISLING turns into it
int knobctl_step_px(int n);           // a step of a knob with n values, in px
int knobctl_find(int list, const char *text, int near);   // text's index in the list (the nearest to near), -1; if
                                                          // it isn't there, without trailing full stops
int knobctl_list_n(int list);
const char *knobctl_text(int list, int idx);
int knobctl_list_of(int view, int i);  // the knob's list, or -1: AISLING takes no CC for it
bool knobctl_moves(int view, int i);   // the engine moves it by itself after a turn (Motion's)
// Knob i's list now if the box can turn it (its value known), else -1; and the value's index in *idx.
int knobctl_turnable(knobctl_t *k, const fm1scr_t *m, int i, int *idx);
#endif
