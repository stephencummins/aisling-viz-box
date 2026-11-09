// ELEGOO 3.5" SPI screen: an ILI9486 behind 16-bit shift registers, plus an XPT2046 touch chip.
#ifndef VIZ_LCD_H_INCLUDED
#define VIZ_LCD_H_INCLUDED
#include <stdint.h>
#include <stdbool.h>

#define LCD_W 480
#define LCD_H 320

// RGB565
#define RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
#define C_BLACK  RGB(0, 0, 0)
#define C_WHITE  RGB(255, 255, 255)
#define C_RED    RGB(255, 0, 0)
#define C_GREEN  RGB(0, 255, 0)
#define C_BLUE   RGB(0, 0, 255)
#define C_GREY   RGB(110, 110, 110)
#define C_ORANGE RGB(255, 95, 31)   // the gnome family's dark-theme accent
#define C_VELLUM RGB(243, 234, 215)

// Returns the SPI clock actually set, in Hz.
uint32_t lcd_init(void);
void lcd_fill_rect(int x, int y, int w, int h, uint16_t colour);
// 5x7 font scaled by `scale`; a character cell is 6*scale wide and 8*scale tall.
void lcd_text(int x, int y, const char *s, int scale, uint16_t fg, uint16_t bg);

// Touch: true while pressed, with the XPT2046's raw 12-bit readings.
bool touch_read(uint16_t *x, uint16_t *y);

#endif
