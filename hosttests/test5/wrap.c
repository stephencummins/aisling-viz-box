// One version's land.c, its outside names prefixed (run.sh passes -Dland_init=t4_land_init and so on, from nm), and
// a look at its state for compare.c. LAND_C is the file, STATE_FN the look's name.
#include <string.h>
#include LAND_C
#define ST(x) if (!strcmp(name, #x)) { *n = sizeof x; return &x; }
const void *STATE_FN(const char *name, size_t *n) {
    ST(bufA) ST(bufR) ST(bufG) ST(bufB) ST(u) ST(v) ST(G0) ST(G1) ST(G2) ST(SR) ST(SG) ST(SB) ST(dirty) ST(blooms)
    ST(simT) ST(windNow) ST(bpm) ST(seed) ST(pal) ST(gnd) ST(redraw_all) ST(colI) ST(rowJ) ST(colF) ST(rowF) ST(COV)
    ST(GRAIN)
    *n = 0;
    return NULL;
}
