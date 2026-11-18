// SPDX-License-Identifier: GPL-3.0-only
// (test 19) A firmware put on the FM-1 from the box: the host's side of Felucca's update, as tools/fm1_install.py
// of fm1-aisling has it (Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments; GPL-3.0-only), of
// which this is a port to C. The messages, the two steps and the waits are that file's; what is the box's own is
// said where it is.
//
// The update, as there: step 1, the firmware running on the FM-1 reads parts of the package and starts the update
// loader (the FM-1 leaves the USB and comes back as "ota-..."); step 2, the loader reads the whole package and writes
// it; then the FM-1 restarts and says who it now is. In both steps the FM-1 asks (SysEx read requests on the
// package's logical image) and the host answers. Nothing is written in step 1. From the moment step 2 can begin the
// FM-1 stays in its loader, through power cuts, until an install has finished.
//
// Pure, so the host checks can build it: no USB, no card, no clock. The caller gives the time in milliseconds,
// the SysEx that arrives, a way to read the package and a way to send.
#ifndef VIZ_SWAP_H
#define VIZ_SWAP_H
#include <stdbool.h>
#include <stdint.h>

#define SWAP_MAXDATA 512u           // the most a read request may ask for
#define SWAP_WIRE_MAX 640u          // a 512-byte answer is 605 bytes on the wire (the FM-1's own limit is 640)
#define SWAP_MSG_MAX 48             // what the FM-1 sends is short: a request 20 bytes, its identity 41
#define SWAP_FINISH_CHECK 0xE0000000u
#define SWAP_FINISH_WRITE 0xF0000000u
#define SWAP_HEAD_FILE 960u         // the package's first 20 blocks of 0x30: each ends in a marker byte
#define SWAP_HEAD_LOGICAL 940u      // ... which the logical image leaves out
#define SWAP_NAME_MAX 28            // an identity's text is at most 27 letters

// The waits, in ms (fm1_install.py's DELAY).
#define SWAP_HS_MS 1000u            // an identity asked for: this long for the answer
#define SWAP_HS_TRIES 3
#define SWAP_START_MS 2000u         // after UPGRADE, before the idle wait counts
#define SWAP_REPLY_MS 10u           // before each answer with data
#define SWAP_IDLE_CHECK_MS 8000u    // step 1: no request in this long, and it has stopped
#define SWAP_IDLE_WRITE_MS 180000u  // step 2: the same (an erase is slow)
#define SWAP_LOADER_MS 3000u        // after step 1, before the loader is looked for
#define SWAP_WAIT_LOADER_MS 30000u
#define SWAP_REBOOT_MS 3000u        // after step 2, before the new firmware is looked for
#define SWAP_WAIT_REBOOT_MS 40000u
#define SWAP_RETRY_MS 1000u         // between looks
// The box's own: a cable pulled and put back in the middle is looked at again for this long.
#define SWAP_WAIT_AGAIN_MS 30000u

// ---- the 7-bit packing, the messages, the package ----

uint32_t swap_pack7(const uint8_t *in, uint32_t n, uint8_t *out);                    // returns the 7-bit bytes
uint32_t swap_unpack7(const uint8_t *in, uint32_t n, uint8_t *out, uint32_t max);    // the bytes (never past max)
// A whole message, F0 to F7. A read request: its flags, address and length.
bool swap_request(const uint8_t *sx, uint32_t n, uint8_t *flags, uint32_t *addr, uint32_t *len);
// The answer to one, F0 to F7, into out (SWAP_WIRE_MAX): its length. n at most SWAP_MAXDATA.
uint32_t swap_response(uint32_t addr, const uint8_t *data, uint32_t n, uint8_t flags, uint8_t *out);
typedef struct {
    char text[SWAP_NAME_MAX];   // "FM-1_900", "ota-FM-1_900"
    bool loader;                // the update loader: its model begins "ota-"
    uint32_t chip;              // the flash's JEDEC ID if it gave one, else 0
} swap_id_t;
bool swap_identity(const uint8_t *sx, uint32_t n, swap_id_t *id);
// Is this "MODEL_digits", as an identity and a package's product are?
bool swap_name_ok(const char *text);
// "FM-1_900" and "ota-FM-1_900" are both the model "FM-1".
bool swap_same_model(const char *a, const char *b);
// The package's product, from its first SWAP_HEAD_FILE bytes (text: SWAP_NAME_MAX). False: not an FM-1 package.
bool swap_product(const uint8_t *head, char *text);
// The first SWAP_HEAD_FILE bytes of the file made the first SWAP_HEAD_LOGICAL of the logical image.
void swap_head(const uint8_t *file, uint8_t *logical);
// Where in the file the logical image's byte is.
uint32_t swap_file_off(uint32_t logical);
// "FELUCCA-LOADER-1" looked for through a file read in pieces: start with *st = 0, give every piece in order;
// true once it has been seen. (Only packages with Felucca's loader in them are installed.)
bool swap_mark(uint32_t *st, const uint8_t *p, uint32_t n);

// ---- USB-MIDI's 4-byte packets ----

// A message F0..F7 from *at on, as packets into out (max a multiple of 4): the bytes written; *at moves on.
uint32_t swap_packets(const uint8_t *sx, uint32_t n, uint32_t *at, uint8_t *out, uint32_t max);
// The packets that arrive, put back together. Returns a finished message's length (it is in c->buf), else 0.
typedef struct {
    uint8_t buf[SWAP_MSG_MAX];
    uint8_t n;
    bool in, over;
} swap_col_t;
int swap_collect(swap_col_t *c, const uint8_t p[4]);

// ---- the install ----

enum { SWAP_IDLE, SWAP_ASK, SWAP_CHECK, SWAP_LOADER, SWAP_WRITE, SWAP_REBOOT, SWAP_DONE, SWAP_FAILED };
enum {
    SWAP_OK,
    SWAP_E_SILENT,     // nothing answered the question "who are you": nothing was written
    SWAP_E_MODEL,      // the package is for another instrument: nothing was written
    SWAP_E_STOPPED,    // step 1 stopped: nothing was written
    SWAP_E_NOLOADER,   // the loader didn't appear
    SWAP_E_HALF,       // step 2 stopped: the FM-1 stays in its loader until an install finishes
    SWAP_E_BADREQ,     // it asked for something outside the package
    SWAP_E_CARD,       // the package couldn't be read
    SWAP_E_NORETURN,   // written, but the FM-1 didn't come back
    SWAP_E_MISMATCH,   // written, and something else answers
};
typedef struct {
    // The logical image's bytes. False: couldn't.
    bool (*read)(void *ctx, uint32_t logical, uint8_t *dst, uint32_t n);
    // One message to the FM-1. False: not now (the last is still going, or no FM-1): asked again.
    bool (*send)(void *ctx, const uint8_t *wire, uint32_t n);
    void *ctx;
} swap_io_t;
typedef struct {
    swap_io_t io;
    char product[SWAP_NAME_MAX];
    uint32_t image_len;         // the logical image's
    int state, error;
    bool linked;                // an FM-1 is there to send to
    bool resumed;               // found already in its loader: only the write was done
    bool checked;               // step 1 was done in this install
    int again;                  // asking again after the cable came back: the error if nobody answers (0: not)
    int misreads;               // the card's failures in a row
    uint32_t t_state, t_last, t_next;
    int tries;
    uint32_t served, pct;
    char device[SWAP_NAME_MAX]; // who answered before the install
    char back[SWAP_NAME_MAX];   // who answered after
    uint32_t bad_addr, bad_n;
    bool pending;               // wire[] is waiting to go
    uint32_t pending_at;        // not before
    uint32_t wire_n;
    uint8_t wire[SWAP_WIRE_MAX];
    uint8_t data[SWAP_MAXDATA];
} swap_t;
void swap_init(swap_t *s);
// Begins. product and image_len are the package's; linked: an FM-1 is plugged in now.
void swap_start(swap_t *s, const swap_io_t *io, const char *product, uint32_t image_len, bool linked, uint32_t now);
void swap_link(swap_t *s, bool up, uint32_t now);                        // the FM-1 came or went
void swap_rx(swap_t *s, const uint8_t *sx, uint32_t n, uint32_t now);    // a whole message from it
void swap_tick(swap_t *s, uint32_t now);                                 // often
static inline bool swap_busy(const swap_t *s) { return s->state != SWAP_IDLE && s->state < SWAP_DONE; }
#endif
