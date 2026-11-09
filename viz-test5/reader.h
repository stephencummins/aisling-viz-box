// The FM-1's screen reader, heard here to keep the strip's piece in sync (2026-11-09, Stephen: "keep the track
// highlighted in sync"). The strip only knew a piece it sent itself; one chosen on the FM-1 (PRESET, or a program
// change from elsewhere) never reached it, since AISLING sends no program change out.
//
// But AISLING talks to a screen reader on the computer (its announce.c: SysEx F0 7D 53 52 cmd .. F7), and only while
// one listens. So this says HELLO as a reader would, every 2 seconds, and from what the FM-1 then says takes the
// piece: "PIECE Burren, A Dorian, tempo 84" each time the piece changes, and "... Piece Burren, ..." in the whole
// screen it says when a reader first says hello. Nothing on the FM-1 changes: no firmware, no settings, nothing
// stored. A reader's hello doesn't wake it or keep it awake (its sleep timer counts touches, keys and notes).
// An FM-1 without the reader never answers, and the strip works as before.
#ifndef VIZ_READER_H_INCLUDED
#define VIZ_READER_H_INCLUDED
#include <stdint.h>
#include <stdbool.h>

#define READER_MAX 256   // the longest SysEx kept: announce.c's SAY is at most 240 characters and 7 more bytes

typedef struct {
    uint8_t buf[READER_MAX];   // the bytes between F0 and F7
    uint16_t n;
    bool in, over;             // inside a SysEx; it ran past READER_MAX (then dropped)
    uint32_t said, answered;   // SAYs and HELLO answers heard
} reader_t;

// The reader's HELLO as USB-MIDI packets, cable 0, into out (12 bytes); returns their length.
uint32_t reader_hello(uint8_t out[12]);

// One USB-MIDI event packet from the FM-1. Only SysEx (CIN 4-7) is looked at; anything else, including notes and
// clock arriving between a SysEx's packets, is left alone. Returns the piece (0-7, program-change order) when a
// SAY just ended that names one, else -1.
int reader_packet(reader_t *r, const uint8_t p[4]);

// The piece a SAY's text names ("PIECE Aran, ..." or "... Piece Aran, ..."), or -1. Exposed for the host check.
int reader_piece(const char *text, uint32_t n);

#endif
