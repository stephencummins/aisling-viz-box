// Made by hosttests/test7/gen_pflash.py from AISLING 1.3's ui.c (PKEY, PLINE, PG_COL, pkey_col): don't edit.
// PERFORM's effects in the keys' order: the name AISLING says when one is held, the line its flash shows under
// it, and its colour (RGB565).
#ifndef VIZ_PFLASH_H_INCLUDED
#define VIZ_PFLASH_H_INCLUDED
#include <stdint.h>

#define PFLASH_N 27
static const struct {
    const char *name, *line;
    uint16_t col;
} PFLASH[PFLASH_N] = {
    {"Stutter", "a slice, over and over", 0xFC89},
    {"Ceo", "fog rolling into the echo", 0x362E},
    {"Half time", "half the speed", 0xF50C},
    {"Fras", "a burst of rain", 0x3E2E},
    {"Gate", "chopped to the beat", 0xF58F},
    {"Sweep", "the Sequence's filter swept", 0x4E4F},
    {"Roll", "a beat rolled faster", 0xF612},
    {"Repeat", "the Sequence into the echo", 0x4DF6},
    {"Rise", "every filter opens", 0x5650},
    {"Macalla", "the echo rings on and builds", 0x6636},
    {"Close", "every filter closes", 0x6671},
    {"Tape stop", "the tape slowing down", 0x8677},
    {"Reverse", "backwards", 0xA6B8},
    {"Octave", "the Sequence an octave up", 0x6E91},
    {"Reverb", "into the plate", 0xC4BA},
    {"Dive", "falling away", 0x7E92},
    {"Shimmer", "the plate climbing an octave", 0xCD3A},
    {"Ring", "bells in it", 0x86B3},
    {"Freeze", "the cloud holds what it has", 0xD5BA},
    {"Wash", "everything into the echo", 0xDE3A},
    {"Vinyl", "dust and wobble", 0x96D4},
    {"Flange", "a jet passing", 0xF647},
    {"Comb", "a ringing pipe", 0x9ED4},
    {"Phaser", "a deep phaser", 0xF66A},
    {"Drive", "warm overdrive", 0xF6AE},
    {"Drop", "both sequences out", 0xAEF5},
    {"Crush", "fewer bits", 0xF6F1},
};

#endif
