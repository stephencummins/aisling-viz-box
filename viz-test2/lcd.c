// ELEGOO 3.5" SPI screen driver, written from the ILI9486 and XPT2046 datasheets.
//
// The ILI9486 sits on a 16-bit parallel bus fed by shift registers, so every SPI transfer is a 16-bit word:
// a command or parameter goes out as 0x00nn, a pixel as one RGB565 word. RS (D/C) is a GPIO and may only
// change while the SPI is idle.
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "hardware/dma.h"
#include "lcd.h"
#include "font5x7.h"

// Header pin -> RP2350 GPIO, from the board's silkscreen.
#define PIN_SCK    10 // pin 23
#define PIN_MOSI   11 // pin 19
#define PIN_MISO   12 // pin 21
#define PIN_DC     24 // pin 18, LCD RS
#define PIN_RST    25 // pin 22
#define PIN_LCD_CS  8 // pin 24
#define PIN_TP_CS   7 // pin 26
#define PIN_TP_IRQ 17 // pin 11, low while touched

#define LCD_SPI spi1
#define LCD_HZ  (20 * 1000 * 1000)   // 120 MHz clk_peri / 6, exactly
#define TP_HZ   (1 * 1000 * 1000)   // the XPT2046 is good to about 2 MHz

// Memory access control: MV swaps rows and columns for landscape; 0x08 selects BGR order.
// If the picture comes out mirrored or upside down, this is the byte to change (MX 0x40, MY 0x80).
#ifndef VIZ_MADCTL
#define VIZ_MADCTL 0x28
#endif

// Pixels go out by DMA on a fixed channel. PIO-USB claims channel 0 for itself (and panics if it is taken),
// and it starts on core 1 at a time of its own, so this one is chosen high rather than claimed as "unused".
#define LCD_DMA_CH 11

static uint32_t lcd_baud;
static bool pushing;

static inline void dc(bool data) { gpio_put(PIN_DC, data); }
static inline void lcd_cs(bool sel) { gpio_put(PIN_LCD_CS, !sel); }

static void cmd(uint8_t c, const uint8_t *params, size_t n) {
    uint16_t w = c;
    lcd_cs(true);
    dc(false);
    spi_write16_blocking(LCD_SPI, &w, 1);
    dc(true);
    for (size_t i = 0; i < n; i++) {
        w = params[i];
        spi_write16_blocking(LCD_SPI, &w, 1);
    }
    lcd_cs(false);
}

static void window(int x, int y, int w, int h) {
    uint16_t x1 = (uint16_t)(x + w - 1), y1 = (uint16_t)(y + h - 1);
    uint8_t ca[4] = {(uint8_t)(x >> 8), (uint8_t)x, (uint8_t)(x1 >> 8), (uint8_t)x1};
    uint8_t pa[4] = {(uint8_t)(y >> 8), (uint8_t)y, (uint8_t)(y1 >> 8), (uint8_t)y1};
    cmd(0x2A, ca, 4); // column address set
    cmd(0x2B, pa, 4); // page address set
}

// Starts a RAM write into the current window and leaves the screen selected for pixels.
static void ram_write_begin(void) {
    uint16_t w = 0x2C;
    lcd_cs(true);
    dc(false);
    spi_write16_blocking(LCD_SPI, &w, 1);
    dc(true);
}

static void ram_write_end(void) { lcd_cs(false); }

uint32_t lcd_init(void) {
    gpio_init(PIN_LCD_CS); gpio_set_dir(PIN_LCD_CS, GPIO_OUT); gpio_put(PIN_LCD_CS, 1);
    gpio_init(PIN_TP_CS);  gpio_set_dir(PIN_TP_CS, GPIO_OUT);  gpio_put(PIN_TP_CS, 1);
    gpio_init(PIN_DC);     gpio_set_dir(PIN_DC, GPIO_OUT);     gpio_put(PIN_DC, 1);
    gpio_init(PIN_RST);    gpio_set_dir(PIN_RST, GPIO_OUT);    gpio_put(PIN_RST, 1);
    gpio_init(PIN_TP_IRQ); gpio_set_dir(PIN_TP_IRQ, GPIO_IN);  gpio_pull_up(PIN_TP_IRQ);

    lcd_baud = spi_init(LCD_SPI, LCD_HZ);
    spi_set_format(LCD_SPI, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);

    // Hardware reset, then the datasheet's waits.
    sleep_ms(5);
    gpio_put(PIN_RST, 0);
    sleep_ms(20);
    gpio_put(PIN_RST, 1);
    sleep_ms(150);

    cmd(0x01, NULL, 0);            // software reset
    sleep_ms(150);
    cmd(0x11, NULL, 0);            // sleep out
    sleep_ms(150);
    const uint8_t pix = 0x55;      // 16 bits a pixel
    cmd(0x3A, &pix, 1);
    const uint8_t mad = VIZ_MADCTL;
    cmd(0x36, &mad, 1);
    cmd(0x20, NULL, 0);            // inversion off
    cmd(0x13, NULL, 0);            // normal display mode
    lcd_fill_rect(0, 0, LCD_W, LCD_H, C_BLACK);
    cmd(0x29, NULL, 0);            // display on
    sleep_ms(20);
    return lcd_baud;
}

static uint16_t line[LCD_W];

void lcd_fill_rect(int x, int y, int w, int h, uint16_t colour) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > LCD_W) w = LCD_W - x;
    if (y + h > LCD_H) h = LCD_H - y;
    if (w <= 0 || h <= 0) return;
    lcd_push_wait();
    for (int i = 0; i < w; i++) line[i] = colour;
    window(x, y, w, h);
    ram_write_begin();
    for (int r = 0; r < h; r++) spi_write16_blocking(LCD_SPI, line, (size_t)w);
    ram_write_end();
}

static const uint8_t *glyph(char c) {
    static const uint8_t unknown_q = '?' - 32;
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    if (c < 32 || c > 126) return font5x7[unknown_q];
    const uint8_t *g = font5x7[c - 32];
    if (c != ' ') {
        bool blank = true;
        for (int i = 0; i < 7; i++) if (g[i]) blank = false;
        if (blank) return font5x7[unknown_q];
    }
    return g;
}

static uint16_t cell[24 * 32];

void lcd_text(int x, int y, const char *s, int scale, uint16_t fg, uint16_t bg) {
    lcd_push_wait();
    if (scale < 1) scale = 1;
    if (scale > 4) scale = 4;
    const int cw = 6 * scale, ch = 8 * scale;
    for (; *s; s++, x += cw) {
        if (x + cw > LCD_W || y + ch > LCD_H || x < 0 || y < 0) return;
        const uint8_t *g = glyph(*s);
        for (int py = 0; py < ch; py++) {
            int row = py / scale;
            uint8_t bits = row < 7 ? g[row] : 0;
            for (int px = 0; px < cw; px++) {
                int col = px / scale;
                bool on = col < 5 && (bits & (0x10 >> col));
                cell[py * cw + px] = on ? fg : bg;
            }
        }
        window(x, y, cw, ch);
        ram_write_begin();
        spi_write16_blocking(LCD_SPI, cell, (size_t)(cw * ch));
        ram_write_end();
    }
}

static uint16_t tp_sample(uint8_t command) {
    uint8_t tx[3] = {command, 0, 0}, rx[3];
    gpio_put(PIN_TP_CS, 0);
    spi_write_read_blocking(LCD_SPI, tx, rx, 3);
    gpio_put(PIN_TP_CS, 1);
    return (uint16_t)((((uint16_t)rx[1] << 8) | rx[2]) >> 3);
}

bool touch_down(void) { return !gpio_get(PIN_TP_IRQ); }

bool touch_read(uint16_t *x, uint16_t *y) {
    if (gpio_get(PIN_TP_IRQ)) return false;
    lcd_push_wait(); // the SPI idle and its receive FIFO empty, or the touch reads stale words
    spi_set_format(LCD_SPI, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    spi_set_baudrate(LCD_SPI, TP_HZ);
    uint32_t sx = 0, sy = 0;
    for (int i = 0; i < 4; i++) {
        sx += tp_sample(0xD0); // X, 12-bit, differential, power down between conversions
        sy += tp_sample(0x90); // Y
    }
    spi_set_baudrate(LCD_SPI, lcd_baud);
    spi_set_format(LCD_SPI, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    *x = (uint16_t)(sx / 4);
    *y = (uint16_t)(sy / 4);
    return !gpio_get(PIN_TP_IRQ); // still pressed, so the readings are good
}

// Stephen's corners and centre (test 1, 2026-11-09). The touch chip's axes are swapped against the screen's:
// its X runs down the screen, its Y right to left.
bool touch_screen(int *x, int *y) {
    uint16_t rx, ry;
    if (!touch_read(&rx, &ry)) return false;
    int sx = ((3890 - (int)ry) * LCD_W) / 3600;
    int sy = (((int)rx - 318) * LCD_H) / 3482;
    *x = sx < 0 ? 0 : sx >= LCD_W ? LCD_W - 1 : sx;
    *y = sy < 0 ? 0 : sy >= LCD_H ? LCD_H - 1 : sy;
    return true;
}

uint32_t lcd_set_hz(uint32_t hz) {
    lcd_push_wait();
    lcd_baud = spi_set_baudrate(LCD_SPI, hz);
    return lcd_baud;
}

uint32_t lcd_hz(void) { return lcd_baud; }

// After trying a speed the screen couldn't take: its settings again, at whatever speed is set now.
void lcd_resetup(void) {
    lcd_push_wait();
    const uint8_t pix = 0x55, mad = VIZ_MADCTL;
    cmd(0x11, NULL, 0);
    cmd(0x3A, &pix, 1);
    cmd(0x36, &mad, 1);
    cmd(0x20, NULL, 0);
    cmd(0x13, NULL, 0);
    cmd(0x29, NULL, 0);
}

void lcd_push_begin(int x, int y, int w, int h, const uint16_t *px) {
    lcd_push_wait();
    static bool claimed;
    if (!claimed) { dma_channel_claim(LCD_DMA_CH); claimed = true; }
    window(x, y, w, h);
    ram_write_begin();
    dma_channel_config c = dma_channel_get_default_config(LCD_DMA_CH);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_16);
    channel_config_set_dreq(&c, spi_get_dreq(LCD_SPI, true));
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    dma_channel_configure(LCD_DMA_CH, &c, &spi_get_hw(LCD_SPI)->dr, px, (uint32_t)(w * h), true);
    pushing = true;
}

bool lcd_push_busy(void) {
    return pushing && dma_channel_is_busy(LCD_DMA_CH);
}

// The DMA is done when the last word is in the FIFO, not when it has gone out: wait for the SPI to finish
// shifting before the screen is deselected, then empty the receive FIFO the push filled and clear its overrun.
void lcd_push_wait(void) {
    if (!pushing) return;
    dma_channel_wait_for_finish_blocking(LCD_DMA_CH);
    while (spi_get_hw(LCD_SPI)->sr & SPI_SSPSR_BSY_BITS) tight_loop_contents();
    while (spi_is_readable(LCD_SPI)) (void)spi_get_hw(LCD_SPI)->dr;
    spi_get_hw(LCD_SPI)->icr = SPI_SSPICR_RORIC_BITS;
    ram_write_end();
    pushing = false;
}
