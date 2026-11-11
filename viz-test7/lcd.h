// ELEGOO 3.5" SPI screen: an ILI9486 behind 16-bit shift registers, plus an XPT2046 touch chip.
#ifndef VIZ_LCD_H_INCLUDED
#define VIZ_LCD_H_INCLUDED
#include <stdint.h>
#include <stdbool.h>

#define LCD_W 480
#define LCD_H 320

// The screen's SPI at start-up, in MHz. At a 120 MHz clock, 20 (120 / 6, exact). At 144 the SPI can't make 20 (it
// gets 18, 144 / 8), so 24 (144 / 6): lighter than 18, and Stephen liked its softness (2026-11-09).
#if defined(VIZ_SYS_MHZ) && VIZ_SYS_MHZ == 144
#define LCD_START_MHZ 24
#else
#define LCD_START_MHZ 20
#endif

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

// Flat colours and text go through the warmth (tone.h) on the way out; pixels pushed by DMA are sent as they are
// (the landscape takes the warmth itself, before its dither).
void lcd_fill_rect(int x, int y, int w, int h, uint16_t colour);
// 5x7 font scaled by `scale`; a character cell is 6*scale wide and 8*scale tall (scale 1 to 8).
void lcd_text(int x, int y, const char *s, int scale, uint16_t fg, uint16_t bg);

// Touch: the pen IRQ alone (cheap, no SPI).
bool touch_down(void);
// Touch: true while pressed, with the XPT2046's raw 12-bit readings.
bool touch_read(uint16_t *x, uint16_t *y);
// The same, mapped to screen pixels with the calibration from test 1.
bool touch_screen(int *x, int *y);

// SPI speed: asks for hz, returns what was set (clk_peri over an even divider).
uint32_t lcd_set_hz(uint32_t hz);
uint32_t lcd_hz(void);
// Sends the screen's settings again (after a speed it couldn't take).
void lcd_resetup(void);

// The panel's analogue settings, from the ILI9486 datasheet (section 8.2). Each one is either left at the chip's
// power-on value (not sent) or set here. The screen can't be read back, so "not sent" is the only way to know a
// setting is the chip's own.
#define PANEL_GAMMA_FIELDS 16
typedef struct {
    bool vcom_set;   // C5h with VCM_REG_EN = 1; otherwise VCOM comes from the chip's NV memory
    uint8_t vcm;     // VCM_REG, 0x00-0x80: VCOM = -2 + vcm / 64 volts (0x80 = 0 V); its power-on value is 0x60
    bool vrh_set;    // C0h, the same for both polarities
    uint8_t vrh;     // VRH1 = VRH2, 0x01-0x1F: VREG1OUT = 3.625 + (vrh - 1) / 16 volts; power-on 0x0E = 4.4375 V
    bool gamma_set;  // E0h and E1h, the same curve for both polarities
    uint8_t gamma[PANEL_GAMMA_FIELDS]; // VP0 VP1 VP2 VP4 VP6 VP13 VP20 VP27 VP36 VP43 VP50 VP57 VP59 VP61 VP62 VP63
} panel_t;

// For each gamma field: the grey level (of 64) it belongs to, and its width in bits.
extern const uint8_t PANEL_GAMMA_LEVEL[PANEL_GAMMA_FIELDS];
extern const uint8_t PANEL_GAMMA_BITS[PANEL_GAMMA_FIELDS];

// Starts the screen with these panel settings (sent at once, and again after any hardware reset). Returns the SPI
// clock actually set, in Hz.
uint32_t lcd_init(const panel_t *start);

// Gives the screen these settings. Returns true if it had to be reset to do it (a setting going back to the chip's
// own needs a hardware reset), which leaves the screen black and wanting a full redraw.
bool lcd_panel_set(const panel_t *p);
const panel_t *lcd_panel(void);
// The 15 bytes E0h and E1h take, packed from the 16 fields.
void lcd_gamma_bytes(const uint8_t *g, uint8_t out[15]);

// Pixels by DMA: starts sending w*h RGB565 words into the window (x, y, w, h) and returns at once.
// px must stay untouched until lcd_push_wait() returns. Every other screen call waits for the push first.
void lcd_push_begin(int x, int y, int w, int h, const uint16_t *px);
bool lcd_push_busy(void);
void lcd_push_wait(void);

#endif
