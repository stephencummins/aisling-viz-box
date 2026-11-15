// Test 15's check of the half turn (lcd.c's VIZ_FLIP): the byte the screen is sent, and the touch.
// run.sh builds this twice, with -DVIZ_FLIP=1 and without. lcd.c is built with its pen IRQ and its touch read
// renamed to the two functions here, so a touch can be played in; everything else is test 4's stub.
#include <stdio.h>
#include "sdk_stub.h"
#include "lcd.h"
#ifndef VIZ_FLIP
#define VIZ_FLIP 0
#endif
extern uint32_t rec[];
extern int nrec;

static int raw_x, raw_y;
static bool pressed;
static int fails, checks;
#define CHECK(ok, ...) do { checks++; if (!(ok)) { fails++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

bool t15_gpio_get(unsigned p) { return p == 17 ? !pressed : false; }   // pin 17: the pen IRQ, low while touched
int t15_spi_write_read_blocking(spi_inst_t *s, const uint8_t *a, uint8_t *b, size_t n) {
    (void)s;
    int v = a[0] == 0xD0 ? raw_x : a[0] == 0x90 ? raw_y : 0;   // the XPT2046's X and Y commands
    b[0] = 0; b[1] = (uint8_t)((v << 3) >> 8); b[2] = (uint8_t)(v << 3);
    return (int)n;
}

static bool touch(int rx, int ry, int *x, int *y) {
    raw_x = rx; raw_y = ry; pressed = true;
    bool ok = touch_screen(x, y);
    pressed = false;
    return ok;
}

// How many times the screen was sent command 36h (memory access control), and whether each carried `want`.
static int madctl(uint8_t want, bool *all) {
    int n = 0;
    *all = true;
    for (int i = 0; i + 1 < nrec; i++)
        if (rec[i] == 0x0036) { n++; if (rec[i + 1] != (0x10000u | want)) *all = false; }
    return n;
}

int main(void) {
    const uint8_t want = VIZ_FLIP ? 0xE8 : 0x28;   // MV + BGR, and for the turn MX + MY
    panel_t p = {0};
    bool all;
    lcd_init(&p);
    CHECK(madctl(want, &all) == 1 && all, "start-up: 36h sent %d times, %s 0x%02X", madctl(want, &all), all ? "all" : "not all", want);
    lcd_resetup();
    CHECK(madctl(want, &all) == 2 && all, "lcd_resetup: 36h sent %d times in all, %s 0x%02X", madctl(want, &all), all ? "all" : "not all", want);

    // Not touched: nothing.
    int x = -1, y = -1;
    CHECK(!touch_screen(&x, &y) && x == -1 && y == -1, "a touch with the pen up");

    // Stephen's corners (test 1): the chip's X runs down the screen, its Y right to left. These three are the
    // unturned picture's top left, bottom right and centre; turned, the same spots are the opposite pixels.
    static const struct { int rx, ry, x, y; } spot[] = {
        {318, 3890, 0, 0}, {3800, 290, LCD_W - 1, LCD_H - 1}, {318, 290, LCD_W - 1, 0}, {3800, 3890, 0, LCD_H - 1},
        {2059, 2090, 240, 160},
    };
    for (unsigned i = 0; i < sizeof spot / sizeof spot[0]; i++) {
        int wx = VIZ_FLIP ? LCD_W - 1 - spot[i].x : spot[i].x, wy = VIZ_FLIP ? LCD_H - 1 - spot[i].y : spot[i].y;
        CHECK(touch(spot[i].rx, spot[i].ry, &x, &y) && x == wx && y == wy,
              "raw (%d, %d): (%d, %d), not (%d, %d)", spot[i].rx, spot[i].ry, x, y, wx, wy);
    }

    // Every raw reading: on the screen, and where test 1's calibration puts it (turned: the opposite pixel).
    int off = 0, wrong = 0;
    for (int rx = 0; rx < 4096; rx += 7)
        for (int ry = 0; ry < 4096; ry += 5) {
            if (!touch(rx, ry, &x, &y)) { wrong++; continue; }
            if (x < 0 || x >= LCD_W || y < 0 || y >= LCD_H) off++;
            int sx = ((3890 - ry) * LCD_W) / 3600, sy = ((rx - 318) * LCD_H) / 3482;
            sx = sx < 0 ? 0 : sx >= LCD_W ? LCD_W - 1 : sx;
            sy = sy < 0 ? 0 : sy >= LCD_H ? LCD_H - 1 : sy;
            if (VIZ_FLIP) { sx = LCD_W - 1 - sx; sy = LCD_H - 1 - sy; }
            if (x != sx || y != sy) wrong++;
        }
    CHECK(off == 0, "%d readings land off the screen", off);
    CHECK(wrong == 0, "%d readings land in the wrong place", wrong);

    // Which way a finger moves: down the glass (the chip's X rising) is down the unturned picture, up the turned one.
    int y0, y1, x0, x1;
    touch(1000, 2000, &x0, &y0); touch(3000, 2000, &x1, &y1);
    CHECK(VIZ_FLIP ? y1 < y0 : y1 > y0, "the chip's X rising: y goes %d to %d", y0, y1);
    touch(2000, 1000, &x0, &y0); touch(2000, 3000, &x1, &y1);
    CHECK(VIZ_FLIP ? x1 > x0 : x1 < x0, "the chip's Y rising: x goes %d to %d", x0, x1);

    printf("flip %d: %d checks, %d failed\n", VIZ_FLIP, checks, fails);
    return fails != 0;
}
