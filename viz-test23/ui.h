// (test 18) How much of the screen the box's own pages use.
//
// In the Lego tray a brick on the right covers the screen's last few pixels. Test 16 stopped the strip 16 short of
// that edge; this is the same line for everything else the box draws itself: the status line, calibration, the
// effects, the pad, the FM-1's page (and a guest's), the flashes' lettering and the card's page. Nothing to read and
// nothing to touch is at x >= UI_W, and a touch that begins there on one of those pages is nobody's (main.c). The
// landscape's picture is still the whole 480: it has nothing to read, and a drag over it is still measured over all
// of it.
//
// Pure, so the host checks can build it: no screen, no touch.
#ifndef VIZ_UI_H
#define VIZ_UI_H

#define UI_W 464   // of LCD_W's 480: every page's columns are laid out to this, and strip.h's STRIP_W is it

// Is x under the brick?
static inline int ui_off(int x) { return x >= UI_W; }

// Edge i of n columns across the usable width (i = n: its end). The columns differ by a pixel where n doesn't divide.
static inline int ui_col(int i, int n) { return i * UI_W / n; }

#endif
