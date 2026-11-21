// (test 19) Firmware for the FM-1, from the card: the page that lists the packages in the card's folder FM1 and
// puts the one picked on the FM-1 (swap.h does the talking). Opened from the card's page (its bar FIRMWARE FOR THE
// FM-1, or key z).
//
// The card has 8.3 names only (ffconf.h): a package is a file in FM1 whose extension begins FWS, which is what a
// Mac's "aisling.fwsc" is there. A name of more than eight letters shows as its short form (AISLIN~1.FWS).
#ifndef VIZ_FWPAGE_H
#define VIZ_FWPAGE_H
#include <stdbool.h>
#include <stdint.h>

#define FW_DIR "FM1"

// The bar on the card's page.
void fw_door_draw(bool down);
bool fw_door_at(int x, int y);

// The page.
void fw_open(bool linked, uint32_t now);   // looks through the folder; linked: an FM-1 can be sent to now
void fw_draw(void);                        // all of it
void fw_touch(int x, int y);               // a finger is here
enum { FW_DO_NOTHING, FW_DO_BACK };
int fw_lift(uint32_t now);                 // the finger let go: what main.c is to do
void fw_close(void);

// The install, whichever page is up (main.c keeps this page up while one runs).
bool fw_busy(void);
void fw_link(bool up, uint32_t now);                         // the FM-1's MIDI OUT came or went
void fw_rx(const uint8_t *sysex, uint32_t n, uint32_t now);  // a whole SysEx from it, F0 to F7
void fw_tick(uint32_t now);                                  // often: sends, waits, and draws what changed
const char *fw_news(void);                                   // a line for the log when something happened, else NULL

// main.c's: one message to the FM-1. False: not now.
bool fw_send(const uint8_t *wire, uint32_t n);
#endif
