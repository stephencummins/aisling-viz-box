// (test 12) The microSD card: found at start-up, its settings file read and written, lent to the computer as a USB
// drive and taken back; and its page (key u, or CARD on the calibration screen).
//
// Where the card is, settings live: whenever the box gets the card (at start-up, LOOK AGAIN, or back from the
// computer) it reads SETTINGS.TXT and the file's values win; with no file, it writes one with what it has now.
// main.c collects and applies the settings (settings.h); this keeps the card's state and draws its page.
#ifndef VIZ_CARD_H_INCLUDED
#define VIZ_CARD_H_INCLUDED
#include <stdbool.h>
#include <stdint.h>
#include "settings.h"

enum { CARD_NONE, CARD_OK, CARD_NOT_FAT, CARD_LENT };
const char *card_state_name(int state);   // "NO CARD", "CARD OK", "NOT FAT", "AT THE MAC"
int card_state(void);

// Looks for the card and mounts its filesystem. Returns the state (not while it's lent: it stays lent).
int card_start(void);
// SETTINGS.TXT over *s (settings_load's answer; -2 if the card isn't mounted). The page shows how it went.
int card_load(settings_t *s, settings_report_t *r);
bool card_save(const settings_t *s, const char *gamma_name);
// Lends it to the computer (FatFs let go first); takes it back (and mounts it again: the state). The computer's own
// way back (ejected, or the cable gone) is msc_came_back(); then card_take_back too.
bool card_lend(void);
int card_take_back(void);

// The page.
enum { CARD_BTN_MAIN, CARD_BTN_SAVE, CARD_BTN_BACK, CARD_BUTTONS };
enum { CARD_DO_NOTHING, CARD_DO_LEND, CARD_DO_TAKE_BACK, CARD_DO_LOOK, CARD_DO_SAVE, CARD_DO_BACK };
void card_open(void);                 // the page coming up afresh: no warning or armed button left from before
void card_draw(void);                 // all of it
void card_draw_info(void);            // the lines (not the buttons: after a change of state, card_draw)
void card_draw_counts(void);          // while lent: the blocks read and written, if they have changed
int card_button_at(int x, int y);     // -1: not on one
void card_press_show(int b);          // which button a finger is on now (-1: none)
int card_press(int b);                // a button let go: what main.c is to do (CARD_DO_)
void card_say(const char *warning);   // a line in orange under the rest ("" clears it)

#endif
