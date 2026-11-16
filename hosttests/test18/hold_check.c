// (test 18) The two-second hold for calibration, on the Mac: main.c's own poll_touch() and show_cal(), cut out of
// main.c by hold.sh (defs.inc, cal.inc, touch.inc) and driven with a pretend finger and a pretend clock, against
// the real perf.c (the landscape's drag), knobctl.c, cal.c, strip.h and guest.c. What isn't main.c's own here: the
// other pages' show_ functions (go(), below: what they do to the touch's state and no drawing), the card's three
// lookups (no button anywhere), and what is sent (counted, not sent).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "sdk_stub.h"
#include "lcd.h"
#include "ui.h"
#include "strip.h"
#include "cal.h"
#include "card.h"
#include "perf.h"
#include "fm1scr.h"
#include "knobctl.h"
#include "guest.h"

extern int nrec;
static uint32_t t_now = 1000;
static bool f_down, f_read = true;
static int f_x, f_y;
static bool fake_down(void) { return f_down; }
static bool fake_screen(int *x, int *y) {
    if (!f_read) return false;
    *x = f_x, *y = f_y;
    return true;
}
#define to_ms_since_boot(t) (t_now)
#define get_absolute_time() 0
#define touch_down fake_down
#define touch_screen fake_screen

#include "defs.inc"   // STATUS_Y, STRIP_Y; the pages' enum, screen, want_screen, HOLD_MS, the touch's state

static fm1scr_t fm1;
static knobctl_t kc;
static perf_t pf;
static guest_t gu;
static int pressed_cell = -1, pf_shown = -1;
static bool strip_dirty, status_due, log_touch;
static int n_cal_button, last_cal_button, n_card_do, n_piece, n_knob, n_perf_slots, n_forget, n_cal_shown;
void say(const char *fmt, ...) { (void)fmt; }
static bool may_send(void) { return true; }
static void cal_button(int b, bool fine) { (void)fine; n_cal_button++, last_cal_button = b; }
static void card_do(int d) { (void)d; n_card_do++; }
int card_button_at(int x, int y) { (void)x, (void)y; return -1; }
void card_press_show(int b) { (void)b; }
int card_press(int b) { (void)b; return CARD_DO_NOTHING; }
static void send_piece(int i) { (void)i; n_piece++; }
static void knob_send(unsigned m) { if (m) n_knob++; }
static void perf_send(void) {
    for (uint32_t d = pf.due; d; d &= d - 1) n_perf_slots++;
    pf.due = 0;
}
static void forget_runs(void) { n_forget++; }

#include "cal.inc"     // show_cal
#include "touch.inc"   // land_touching ... poll_touch

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL (line %d): ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

// The other pages, as main.c's show_land / show_fm1 / show_perf leave the touch (nothing drawn).
static void go(int w) {
    if (w == SCR_CAL) { show_cal(); n_cal_shown++; nrec = 0; return; }
    touch_btn = -1;
    touching = false;
    knobctl_release(&kc, &fm1, 0);
    if (w != SCR_FM1) knobctl_reset(&kc, &fm1);
    if (w != SCR_LAND) pressed_cell = -1;
    screen = w;
}
static uint32_t cal_at;   // when calibration last came up
static void frame(void) {   // one turn of main.c's loop, 10 ms
    t_now += 10;
    poll_touch();
    if (want_screen >= 0) {
        int w = want_screen;
        want_screen = -1;
        if (w == SCR_CAL) cal_at = t_now;
        go(w);
    }
}
static void up(int ms) { f_down = false; for (int t = 0; t < ms; t += 10) frame(); }
static void down(int x, int y, int ms) { f_down = true, f_x = x, f_y = y; for (int t = 0; t < ms; t += 10) frame(); }
static void fresh(int scr) {
    up(200);
    perf_init(&pf), fm1scr_init(&fm1), knobctl_init(&kc), guest_init(&gu);
    gu.who = WHO_AISLING;
    pressed_cell = -1;
    go(scr);
    n_cal_button = n_card_do = n_piece = n_knob = n_perf_slots = n_cal_shown = 0;
    f_read = true;
    nrec = 0;
}
// a point of a calibration button, and of none
static bool cal_point(int b, int *px, int *py) {
    for (int y = 0; y < LCD_H; y += 2)
        for (int x = 0; x < UI_W; x += 2)
            if (cal_button_at(x, y) == b && cal_button_at(x + 1, y + 1) == b && y < STATUS_Y) { *px = x, *py = y; return true; }
    return false;
}

int main(void) {
    static const panel_t P0 = {0};
    lcd_init(&P0);
    int cx, cy, lx, ly;
    bool has_card = cal_point(CAL_CARD, &cx, &cy), has_land = false;
    for (int y = 0; y < LCD_H && !has_land; y++)
        for (int x = 0; x < UI_W && !has_land; x++)
            if (cal_button_at(x, y) == CAL_LAND) lx = x + 4, ly = y + 4, has_land = cal_button_at(x + 4, y + 4) == CAL_LAND;
    CHECK(has_card && has_land, "no CARD or LAND button found on the calibration page");
    printf("calibration's CARD button has a point above the status row at (%d,%d); LAND at (%d,%d)\n", cx, cy, lx, ly);

    // 1. the landscape, a still finger: calibration at the two-second mark, the finger still down
    fresh(SCR_LAND);
    uint32_t t0 = t_now + 10;
    down(200, 100, 1990);
    CHECK(screen == SCR_LAND && !n_cal_shown, "calibration before two seconds (screen %d)", screen);
    down(200, 100, 30);
    CHECK(screen == SCR_CAL && n_cal_shown == 1, "a finger down 2.02 s on the landscape: screen %d", screen);
    CHECK(cal_at - t0 >= 2000 && cal_at - t0 <= 2020, "calibration came up %u ms after the touch began", (unsigned)(cal_at - t0));
    down(200, 100, 1500), up(500);
    CHECK(screen == SCR_CAL && n_cal_shown == 1 && !n_cal_button && touch_btn == -1, "the lift after the hold did something");

    // 2. the finger that held is over the CARD button when calibration comes up, stays, wanders over it, lifts: nothing
    fresh(SCR_LAND);
    down(cx, cy, 2500);
    CHECK(screen == SCR_CAL, "a hold at (%d,%d): screen %d", cx, cy, screen);
    for (int i = 0; i < 100; i++) { down(cx + i % 3, cy, 10); CHECK(touch_btn == -1, "the holding finger pressed button %d", touch_btn); }
    up(40), down(cx, cy, 300);   // a 40 ms dropout isn't a lift
    CHECK(touch_btn == -1 && !n_cal_button, "after a 40 ms dropout the holding finger pressed a button");
    up(500);
    CHECK(screen == SCR_CAL && !n_cal_button && n_cal_shown == 1, "the holding finger's lift pressed %d button(s)", n_cal_button);
    // and the next touch is a new one
    down(cx, cy, 100), up(100);
    CHECK(n_cal_button == 1 && last_cal_button == CAL_CARD, "the next tap on CARD: %d presses, button %d", n_cal_button, last_cal_button);
    down(lx, ly, 100), up(100);
    CHECK(n_cal_button == 2 && last_cal_button == CAL_LAND, "then a tap on LAND: %d presses, button %d", n_cal_button, last_cal_button);

    // 3. a finger that rolls and drifts, less than LAND_TRAVEL_PX each way: still a hold
    for (int far = 0; far < LAND_TRAVEL_PX; far++) {
        fresh(SCR_LAND);
        down(200, 100, 100);
        for (int t = 0; t < 2000; t += 10) down(200 + (t / 10 % 4 < 2 ? far : -far), 100 + (t / 10 % 6 < 3 ? -far : far), 10);
        CHECK(screen == SCR_CAL && !pf.l_drag && !n_perf_slots, "a hold drifting %d px: screen %d, drag %d", far, screen, pf.l_drag);
        up(300);
        CHECK(screen == SCR_CAL && !n_cal_button, "a drifting hold's lift");
    }
    CHECK(LAND_TRAVEL_PX == 20, "LAND_TRAVEL_PX is %d", LAND_TRAVEL_PX);

    // 4. a tap is still a tap, up to two seconds
    for (int ms = 60; ms <= 1980; ms += 240) {
        fresh(SCR_LAND);
        down(200, 100, ms), up(200);
        CHECK(screen == SCR_FM1 && !n_cal_shown, "a %d ms tap on the landscape: screen %d", ms, screen);
    }

    // 5. a drag is a drag: LAND_TRAVEL_PX or more on two readings, however long it's held, and its lift changes no page
    for (int still = 0; still <= 1900; still += 950) {
        fresh(SCR_LAND);
        down(100, 100, 20 + still);
        down(100 + LAND_TRAVEL_PX, 100, 20);
        CHECK(pf.l_drag, "%d px on two readings after %d ms: not a drag", LAND_TRAVEL_PX, still);
        int sent = n_perf_slots;
        for (int t = 0; t < 4000; t += 10) down(100 + LAND_TRAVEL_PX + t / 20, 100 + t / 40, 10);
        CHECK(screen == SCR_LAND && !n_cal_shown && pf.l_drag, "a drag held 4 s: screen %d", screen);
        CHECK(n_perf_slots > sent, "a drag sent nothing");
        up(300);
        CHECK(screen == SCR_LAND && !n_cal_shown, "a drag's lift: screen %d", screen);
    }
    // one wild reading in a hold isn't a drag (as test 10)
    fresh(SCR_LAND);
    down(200, 100, 500), down(400, 30, 10), down(200, 100, 1600);
    CHECK(screen == SCR_CAL && !n_perf_slots, "a hold with one wild reading: screen %d", screen);

    // 6. the landscape is the whole screen: a hold under the brick's edge counts
    fresh(SCR_LAND);
    down(472, 100, 2100), up(300);
    CHECK(screen == SCR_CAL && !n_cal_button, "a hold at x 472 on the landscape: screen %d", screen);
    // the status row and the strip are not the landscape
    fresh(SCR_LAND);
    down(200, STATUS_Y + 4, 3000);
    CHECK(screen == SCR_LAND, "a hold on the status row: screen %d", screen);
    up(300);
    CHECK(screen == SCR_LAND, "the status row's lift: screen %d", screen);
    fresh(SCR_LAND);
    down(50, STRIP_Y + 10, 3000);
    CHECK(screen == SCR_LAND && pressed_cell >= 0, "a hold on the strip: screen %d, cell %d", screen, pressed_cell);
    up(300);
    CHECK(screen == SCR_LAND && n_piece == 1, "the strip's lift: screen %d, %d pieces sent", screen, n_piece);

    // 7. the FM-1's page: a hold off the knobs, a tap, a knob's touch, a locked knob's
    fresh(SCR_FM1);
    down(200, 200, 1990);
    CHECK(screen == SCR_FM1, "the FM-1's page: calibration early");
    down(203, 204, 30);
    CHECK(screen == SCR_CAL, "a hold on the FM-1's page: screen %d", screen);
    down(200, 200, 800), up(300);
    CHECK(screen == SCR_CAL && !n_cal_button && n_cal_shown == 1, "its lift");
    fresh(SCR_FM1);
    down(200, 200, 300), up(200);
    CHECK(screen == SCR_FX, "a tap on the FM-1's page: screen %d", screen);
    fresh(SCR_FM1);   // a knob the box can't turn (no page read yet): a press on it is a tap or a hold, as it was
    down(60, 100, 2100);
    CHECK(kc.locked_press || screen == SCR_CAL, "the knob row with nothing read isn't a locked press");
    CHECK(screen == SCR_CAL, "a hold on a locked knob: screen %d", screen);
    up(300);
    fresh(SCR_FM1);   // a knob's own touch (made one here by hand: the row's touch, not locked), still or dragged
    down(60, 100, 10);
    kc.locked_press = kc.dead = false;
    CHECK(kc.in_row, "y 100 isn't in the knob row");
    down(60, 100, 3000);
    CHECK(screen == SCR_FM1, "a hold on a knob: screen %d", screen);
    up(300);
    CHECK(screen == SCR_FM1 && !n_cal_shown, "a knob's lift: screen %d", screen);
    fresh(SCR_FM1);
    down(50, STRIP_Y + 10, 3000), up(300);
    CHECK(screen == SCR_FM1 && n_piece == 1, "a hold on the FM-1 page's strip: screen %d, %d sent", screen, n_piece);
    fresh(SCR_FM1);   // the status row isn't the page
    down(200, STATUS_Y + 4, 3000);
    CHECK(screen == SCR_FM1, "a hold on the FM-1 page's status row: screen %d", screen);
    up(300);
    CHECK(screen == SCR_FM1 && !n_cal_shown, "its lift: screen %d", screen);
    fresh(SCR_FM1);   // under the brick: nobody's
    down(470, 200, 3000), up(300);
    CHECK(screen == SCR_FM1 && !n_cal_shown, "a hold at x 470 on the FM-1's page: screen %d", screen);

    // 8. a guest's page: a hold anywhere above the status row (no knobs there), a tap for the landscape
    fresh(SCR_FM1);
    gu.who = WHO_GUEST;
    down(60, 100, 2100);
    CHECK(screen == SCR_CAL, "a hold on a guest's page: screen %d", screen);
    up(300);
    fresh(SCR_FM1);   // whatever a knob's touch was left as (a guest's page doesn't ask the knobs)
    gu.who = WHO_GUEST;
    down(60, 100, 10);
    kc.in_row = true, kc.locked_press = false;
    down(60, 100, 2100);
    CHECK(screen == SCR_CAL, "a hold on a guest's page, a knob's state left over: screen %d", screen);
    up(300);
    fresh(SCR_FM1);
    gu.who = WHO_GUEST;
    down(60, 100, 300), up(300);
    CHECK(screen == SCR_LAND, "a tap on a guest's page: screen %d", screen);

    // 9. the effects, the pad, calibration itself: no hold
    for (int s = SCR_FX; s <= SCR_XY; s++) {
        fresh(s);
        down(200, 100, 3000);
        CHECK(!n_cal_shown && screen == s, "a hold on page %d: screen %d", s, screen);
        up(300);
        CHECK(!n_cal_shown, "its lift opened calibration");
    }
    fresh(SCR_CAL);
    n_cal_shown = 0;
    down(lx, ly, 3000);
    CHECK(!n_cal_shown && touch_btn == CAL_LAND, "a hold on calibration's LAND");
    up(300);
    CHECK(n_cal_button == 1 && last_cal_button == CAL_LAND, "LAND held 3 s and let go: %d presses", n_cal_button);

    // 10. the panel's dropouts: readings that fail, and the finger off for less than 60 ms, don't start the hold again
    fresh(SCR_LAND);
    t0 = t_now + 10;
    down(200, 100, 700);
    f_read = false, down(200, 100, 200), f_read = true;
    down(200, 100, 400), up(40), down(202, 99, 800);
    CHECK(screen == SCR_CAL && cal_at - t0 >= 2000 && cal_at - t0 <= 2020, "a hold with dropouts: screen %d, %u ms", screen,
          (unsigned)(cal_at - t0));
    up(300);
    // (not changed) off for 60 ms or more is a lift: here a tap, and the page changes
    fresh(SCR_LAND);
    down(200, 100, 1000), up(100);
    CHECK(screen == SCR_FM1, "a 100 ms gap in a hold: screen %d", screen);

    printf("hold: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
