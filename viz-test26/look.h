// (test 17) The look of the box's own pages: dark, as every test before, or light.
//
// The box draws its strip, its status line, the FM-1's page, the effects, the pad, the card's page and the
// calibration page in colours chosen for a black screen. LIGHT is those same colours turned over for a white one:
// what was dark is light, what was light is dark, and a colour keeps its hue and is made deep enough to read on
// white (as AISLING 1.4.1's own Screen: Light does on the FM-1). Nothing is drawn differently: every flat colour the
// box sends goes through look_565() on its way out (lcd.c's lcd_fill_rect and lcd_text, and perf.c's uncial
// lettering), so a page written for the dark is the light one too.
//
// The landscape is not the box's page: it is the picture, pushed as pixels, and its own ground (vellum, night or
// mist) is the visualiser's. The look leaves it alone.
//
// The rule, on a colour's luma Y (0..255):
//   the luma wanted is 255 - Y, deepened (x -> x * (x + 255) / 510, which leaves black and white where they are and
//   takes the middle down: a mid grey that read on black is a darker grey on white);
//   a colour with hue is held to a luma that reads on white, the more so the purer it is (a pure red, green or
//   blue, bright or middling, to no more than 70; the nearly black ones, which were ground, are let go to white);
//   and the colour is brought to that luma with its hue kept: scaled down towards black, or mixed up towards white.
#ifndef VIZ_LOOK_H_INCLUDED
#define VIZ_LOOK_H_INCLUDED
#include <stdint.h>

enum { LOOK_DARK, LOOK_LIGHT, LOOK_N };

static inline int look_luma(int r, int g, int b) { return (r * 77 + g * 150 + b * 29 + 128) >> 8; }

// An RGB565 colour as the look has it. LOOK_DARK: the colour itself.
static inline uint16_t look_565(uint16_t c, int look) {
    if (look != LOOK_LIGHT) return c;
    int r = c >> 11 & 31, g = c >> 5 & 63, b = c & 31;
    r = r << 3 | r >> 2, g = g << 2 | g >> 4, b = b << 3 | b >> 2;   // to 8 bits each
    int y = look_luma(r, g, b);
    int max = r > g ? r : g, min = r < g ? r : g;
    if (b > max) max = b;
    if (b < min) min = b;
    int t = 255 - y;
    t = t * (t + 255) / 510;
    int cap = 255 - (max - min) * 185 / (max > 128 ? max : 128);
    if (t > cap) t = cap;
    if (t <= y) {            // deeper: towards black
        if (y > 0) r = (r * t + y / 2) / y, g = (g * t + y / 2) / y, b = (b * t + y / 2) / y;
    } else {                 // lighter: towards white
        int k = t - y, d = 255 - y;
        r += ((255 - r) * k + d / 2) / d, g += ((255 - g) * k + d / 2) / d, b += ((255 - b) * k + d / 2) / d;
    }
    return (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | b >> 3);
}

// The look now (lcd.c). Setting it draws nothing: the page is to be drawn again by whoever set it.
void lcd_look_set(int look);
int lcd_look(void);

#endif
