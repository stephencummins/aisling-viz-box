// HOT(f): a function the landscape runs every frame. On the board it goes in RAM, copied there at start-up, so it
// doesn't wait on flash, whose cache core 1's USB host shares. On the Mac (the host checks) it's an ordinary function.
#ifndef VIZ_HOT_H_INCLUDED
#define VIZ_HOT_H_INCLUDED
#ifdef VIZ_BOARD
#include "pico.h"
#define HOT(f) __not_in_flash_func(f)
#else
#define HOT(f) f
#endif
#endif
