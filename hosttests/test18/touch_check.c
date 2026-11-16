// (test 18) A touch right of UI_W (x 464 to 479: under the Lego tray's brick) on the box's own pages is nobody's:
// main.c's own poll_touch (cut out by run.sh as hold.sh does, with hold_check.c's pretend finger and clock) is given
// a finger down there on every row of every page, dragged left on to the page and lifted, and nothing is to come of
// it: no button, piece, knob, effect, page or drawing. The landscape above the status row is the one place a touch
// there is still somebody's (the picture is the whole screen's). And at x 460 everything still answers.
// The card's page is not here (its lookups are pretended in hold_check.c): ui_check.c has card_button_at's own.
#define main hold_main
#include "hold_check.c"
#undef main

static int n_sweep;
static void quiet(const char *what, int scr, int x, int y) {
    CHECK(screen == scr && want_screen < 0, "%s from (%d,%d): the page changed (%d)", what, x, y, screen);
    CHECK(!n_cal_button && !n_card_do && !n_piece && !n_knob && !n_perf_slots && !n_cal_shown,
          "%s from (%d,%d): %d buttons, %d pieces, %d knobs, %d effects sent, calibration %d", what, x, y, n_cal_button, n_piece,
          n_knob, n_perf_slots, n_cal_shown);
    CHECK(touch_btn == -1 && pressed_cell == -1 && !strip_dirty && !status_due && !pf.l_drag && !perf_nheld(&pf) && !kc.in_row &&
          !land_touching && !perf_touching && pf.xy_press < 0 && !pf.xy_dirty && !pf.fx_dirty && !pf.in_pad,
          "%s from (%d,%d): button %d, piece %d, strip %d, status %d, drag %d, held %d, knob %d", what, x, y, touch_btn, pressed_cell,
          strip_dirty, status_due, pf.l_drag, perf_nheld(&pf), kc.in_row);
    CHECK(nrec == 0, "%s from (%d,%d): something was drawn", what, x, y);
}
static void start(int scr, bool guest) {
    fresh(scr);
    if (guest) gu.who = WHO_GUEST;
    strip_dirty = status_due = false;
    pf.xy_dirty = pf.fx_dirty = 0;
    nrec = 0;
}

int main(void) {
    static const panel_t P0 = {0};
    lcd_init(&P0);
    static const struct { int scr; bool guest; const char *name; } PG[] = {
        {SCR_FM1, false, "the FM-1's page"}, {SCR_FM1, true, "a guest's page"}, {SCR_FX, false, "the effects"},
        {SCR_XY, false, "the pad"},          {SCR_CAL, false, "calibration"},   {SCR_LAND, false, "the landscape's two rows"},
    };
    for (unsigned p = 0; p < sizeof PG / sizeof *PG; p++) {
        int scr = PG[p].scr;
        for (int y = scr == SCR_LAND ? STATUS_Y : 0; y < LCD_H; y += 3)
            for (int x = UI_W; x < LCD_W; x += x == UI_W ? 7 : 8) {   // 464, 471, 479
                start(scr, PG[p].guest);
                CHECK(PG[p].guest == guest_page(&gu), "%s isn't up", PG[p].name);
                down(x, y, 100);
                quiet(PG[p].name, scr, x, y);
                for (int k = 1; k <= 20; k++) down(x - k * 8, y, 10);   // on to the page, 160 px left
                quiet(PG[p].name, scr, x, y);
                up(200);
                quiet(PG[p].name, scr, x, y);
                n_sweep++;
            }
        // and held there three seconds: no calibration either
        start(scr, PG[p].guest);
        down(470, scr == SCR_LAND ? 276 : 100, 3000), up(200);
        quiet(PG[p].name, scr, 470, 100);
        // a tap, as short as a tap can be
        start(scr, PG[p].guest);
        down(464, scr == SCR_LAND ? 300 : 100, 20), up(100);
        quiet(PG[p].name, scr, 464, 100);
    }

    // the touch after a nobody's one is its own: a tap on the FM-1's page turns it
    start(SCR_FM1, false);
    down(470, 100, 100), up(100), down(200, 100, 100), up(100);
    CHECK(screen == SCR_FX, "a tap after a touch at x 470: screen %d", screen);

    // the landscape above the status row: x 464 to 479 is the picture's, as all of it is
    start(SCR_LAND, false);
    down(472, 100, 100), up(100);
    CHECK(screen == SCR_FM1, "a tap on the landscape at x 472: screen %d", screen);
    start(SCR_LAND, false);
    down(479, 271, 2100);
    CHECK(screen == SCR_CAL, "a hold on the landscape at (479,271): screen %d", screen);
    up(200);
    start(SCR_LAND, false);
    down(478, 100, 50);
    for (int k = 1; k <= 30; k++) down(478 - k * 8, 100 + k, 10);
    CHECK(pf.l_drag && n_perf_slots > 0, "a drag on the landscape from x 478: drag %d, %d sent", pf.l_drag, n_perf_slots);
    up(200);
    CHECK(screen == SCR_LAND, "the drag turned the page");

    // x 460 answers on each page
    int lx = UI_W - 4;
    start(SCR_CAL, false);
    down(lx, 300, 100), up(100);
    CHECK(n_cal_button == 1 && last_cal_button == CAL_LAND, "calibration's LAND at x %d: %d presses", lx, n_cal_button);
    start(SCR_CAL, false);
    down(lx, 230, 100), up(100);
    CHECK(n_cal_button == 1 && last_cal_button == CAL_CARD, "calibration's CARD at x %d: %d presses, button %d", lx, n_cal_button, last_cal_button);
    for (int scr = SCR_LAND; scr <= SCR_FM1; scr += SCR_FM1 - SCR_LAND) {
        start(scr, false);
        down(lx, 300, 100);
        CHECK(pressed_cell == STRIP_N - 1, "the strip's last piece at x %d (page %d): %d", lx, scr, pressed_cell);
        up(100);
        CHECK(n_piece == 1, "the strip's last piece at x %d (page %d): %d sent", lx, scr, n_piece);
    }
    int ky = -1;
    for (int y = 0; y < STATUS_Y && ky < 0; y += 4) {
        start(SCR_FM1, false);
        down(lx, y, 30);
        if (kc.in_row) ky = y;
        up(100);
    }
    CHECK(ky >= 0, "no knob at x %d on the FM-1's page", lx);
    start(SCR_FX, false);
    int cell = perf_fx_cell_at(lx, 100);
    down(lx, 100, 100);
    CHECK(cell >= 0 && perf_touching && (perf_nheld(&pf) || n_perf_slots || perf_fx_at(cell) < 0), "the effects at x %d: cell %d", lx, cell);
    up(100);
    start(SCR_XY, false);
    down(lx, 30, 100);
    CHECK(pf.xy_press >= 0 && pf.xy_dirty, "the pad's side buttons at x %d: none held", lx);
    up(100);

    // a finger that begins on a button and slides off the page's edge: let go there, it presses nothing
    start(SCR_CAL, false);
    down(lx, 300, 100);
    CHECK(touch_btn == CAL_LAND, "LAND not under the finger");
    down(470, 300, 100);
    CHECK(touch_btn == -1, "at x 470 the finger is still on button %d", touch_btn);
    up(100);
    CHECK(!n_cal_button, "LAND pressed by a finger let go at x 470");
    start(SCR_FM1, false);
    down(lx, 300, 100), down(470, 300, 100);
    CHECK(pressed_cell == -1, "at x 470 the finger is still on piece %d", pressed_cell);
    up(100);
    CHECK(!n_piece, "a piece sent by a finger let go at x 470");
    start(SCR_XY, false);
    down(lx, 30, 100);
    int held = pf.xy_press;
    down(470, 30, 100);
    CHECK(held >= 0 && pf.xy_press == -1, "the pad's side button %d: at x 470 the finger is still on %d", held, pf.xy_press);
    up(100);

    printf("touch: %d touches right of x %d over %d pages, each dragged on to the page and let go; a knob answers at (%d,%d)\n",
           n_sweep, UI_W, (int)(sizeof PG / sizeof *PG), lx, ky);
    printf("touch: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
