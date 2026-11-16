// Linked into another test's check to have it draw its pictures in the light look: set before its main() starts.
#include "look.h"
__attribute__((constructor)) static void light(void) { lcd_look_set(LOOK_LIGHT); }
