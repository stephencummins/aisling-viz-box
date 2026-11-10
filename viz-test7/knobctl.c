// Test 7: the FM-1's knobs turned from the box (knobctl.h has the overview).
#include <stdlib.h>
#include <string.h>
#include "knobctl.h"
#include "knobtab.h"

int knobctl_idx_of(int cc, int n) {
    if (n <= 1) return 0;
    if (cc < 0) cc = 0;
    if (cc > 127) cc = 127;
    return (cc * (n - 1) + 63) / 127;
}

int knobctl_cc_for(int idx, int n) {
    int lo = -1, hi = -1;
    for (int v = 0; v < 128; v++)
        if (knobctl_idx_of(v, n) == idx) {
            if (lo < 0) lo = v;
            hi = v;
        }
    return lo < 0 ? -1 : (lo + hi) / 2;
}

int knobctl_step_px(int n) {
    int s = n > 1 ? KC_TRAVEL_PX / (n - 1) : KC_STEP_MAX;
    return s < KC_STEP_MIN ? KC_STEP_MIN : s > KC_STEP_MAX ? KC_STEP_MAX : s;
}

int knobctl_list_n(int list) { return list >= 0 && list < KT_NLISTS ? KT_LISTS[list].n : 0; }

const char *knobctl_text(int list, int idx) {
    return idx >= 0 && idx < knobctl_list_n(list) ? KT_LISTS[list].text[idx] : NULL;
}

int knobctl_list_of(int view, int i) {
    return view >= 0 && view < FM1_VIEWS && i >= 0 && i < 4 ? KT_LIST[view][i] : -1;
}

bool knobctl_moves(int view, int i) {
    return view >= 0 && view < FM1_VIEWS && i >= 0 && i < 4 && (KT_MOVES[view] >> i & 1u);
}

static size_t undotted(const char *s) {
    size_t n = strlen(s);
    while (n && s[n - 1] == '.') n--;
    return n;
}

static bool same(const char *a, const char *b, bool dots) {
    if (!dots) return !strcmp(a, b);
    size_t n = undotted(a);
    return n == undotted(b) && !memcmp(a, b, n);
}

// Some values say the same (Space's Feedback, in tenths of a second): the one nearest near. Not there as said, the
// MODE say's dropped full stop: again without them.
int knobctl_find(int list, const char *text, int near) {
    int n = knobctl_list_n(list), best = -1;
    if (!text) return -1;
    for (int dots = 0; dots < 2 && best < 0; dots++)
        for (int j = 0; j < n; j++)
            if (same(KT_LISTS[list].text[j], text, dots) && (best < 0 || (near >= 0 && abs(j - near) < abs(best - near))))
                best = j;
    return best;
}

void knobctl_init(knobctl_t *k) {
    memset(k, 0, sizeof *k);
    k->knob = k->view = -1;
    for (int i = 0; i < 4; i++) {
        k->c_view[i] = -2;   // never a page: worked out on first sight
        k->c_list[i] = -1;
        k->c_idx[i] = -1;
        k->from[i] = k->to[i] = -1;
    }
}

void knobctl_reset(knobctl_t *k, fm1scr_t *m) {
    if (k->in_row) k->dead = true;   // (the touch carries on, for nothing)
    k->dragging = k->want_where = false;
    for (int i = 0; i < 4; i++) {
        k->w[i].waiting = false;
        k->c_view[i] = -2;
        if (m) fm1scr_set_preview(m, i, NULL, 0, 0);
    }
}

int knobctl_turnable(knobctl_t *k, const fm1scr_t *m, int i, int *idx) {
    if (i < 0 || i > 3) {
        if (idx) *idx = -1;
        return -1;
    }
    if (k->c_view[i] != m->view || strcmp(k->c_val[i], m->value[i])) {
        int list = knobctl_list_of(m->view, i), j = -1;
        if (list >= 0 && m->value[i][0]) {
            // the nearest to what was sent, or to where it was
            int near = k->w[i].waiting && k->w[i].list == list ? k->w[i].idx
                       : k->c_list[i] == list && k->c_view[i] == m->view ? k->c_idx[i]
                                                                           : -1;
            j = knobctl_find(list, m->value[i], near);
        }
        k->c_view[i] = m->view;
        memcpy(k->c_val[i], m->value[i], sizeof k->c_val[i]);
        k->c_val[i][sizeof k->c_val[i] - 1] = 0;
        k->c_list[i] = (int8_t)(j >= 0 ? list : -1);
        k->c_idx[i] = (int16_t)j;
    }
    if (idx) *idx = k->c_idx[i];
    return k->c_list[i];
}

static void send(knobctl_t *k, const fm1scr_t *m, int i, int list, int from, int idx, uint32_t now) {
    k->cc[i] = (uint8_t)knobctl_cc_for(idx, knobctl_list_n(list));
    k->gen[i]++;
    k->w[i].waiting = true;
    k->w[i].view = m->view;
    k->w[i].list = (int8_t)list;
    k->w[i].idx = (int16_t)idx;
    k->w[i].before = k->base;
    k->w[i].ms = now;
    k->w[i].heard = m->heard[i];
    k->w[i].asked = false;
    k->from[i] = (int16_t)from;
    k->to[i] = (int16_t)idx;
    k->n_sent++;
}

static void cancel(knobctl_t *k) {
    if (k->dragging) k->n_cancelled++;
    k->dragging = false;
    k->dead = true;
}

unsigned knobctl_touch(knobctl_t *k, fm1scr_t *m, int x, int y, bool first, uint32_t now) {
    if (first) {
        k->in_row = y >= KC_ROW_Y0 && y < KC_ROW_Y1;
        k->dead = k->dragging = k->cand = k->locked_press = k->moved = false;
        k->knob = (int8_t)(x < 0 ? 0 : x >= 4 * KC_CELL_W ? 3 : x / KC_CELL_W);
        k->view = m->view;
        k->x = k->x0 = (int16_t)x;
        if (k->in_row && knobctl_turnable(k, m, k->knob, NULL) < 0) {
            k->n_locked++;
            k->dead = k->locked_press = true;
        }
        return 0;
    }
    if (k->locked_press && abs(x - k->x0) >= KC_START_PX) k->moved = true;
    if (!k->in_row || k->dead) return 0;
    if (abs(x - k->x) > KC_JUMP_PX) {   // far from the last: taken only if the next agrees
        if (!k->cand || abs(x - k->cand_x) > KC_JUMP_PX / 2) {
            if (!k->cand) k->n_glitches++;
            k->cand = true;
            k->cand_x = (int16_t)x;
            return 0;
        }
    }
    k->cand = false;
    k->x = (int16_t)x;
    if (m->view != k->view) {   // the FM-1's page changed under the finger
        cancel(k);
        return 0;
    }
    int i = k->knob, idx, list = knobctl_turnable(k, m, i, &idx);
    if (list < 0 || i < 0 || i > 3) {   // (it became one the box can't turn: Mode on Custom)
        cancel(k);
        return 0;
    }
    int n = knobctl_list_n(list), s = knobctl_step_px(n);
    if (!k->dragging) {
        if (abs(x - k->x0) < KC_START_PX) return 0;
        k->dragging = true;
        k->n_drags++;
        k->list = (int8_t)list;
        k->x0 = (int16_t)x;   // the steps count from here: the first needs a whole step more
        k->base = k->at = (int16_t)(k->w[i].waiting && k->w[i].list == list ? k->w[i].idx : idx);
        return 0;
    }
    if (list != k->list) {   // (can't: the page is the same)
        cancel(k);
        return 0;
    }
    int dx = x - k->x0, off = dx - (k->at - k->base) * s;
    if ((k->at == n - 1 && off > 0) || (k->at == 0 && off < 0)) {   // on past an end: the end follows the finger,
        k->x0 = (int16_t)(k->x0 + off);                              // so coming back a step moves it
        return 0;
    }
    if (abs(off) < s) return 0;   // within a step of where it is: a whole step to move it either way
    // to the last whole step the finger is past, not the nearest: then a panel's jitter (under a step, end to end)
    // can't send it back while the finger goes on
    int t = k->at + (off > 0 ? off / s : -(-off / s));
    if (t > n - 1) {   // past the end: the end, and coming back moves it straight away
        t = n - 1;
        k->x0 = (int16_t)(x - (n - 1 - k->base) * s);
    } else if (t < 0) {
        t = 0;
        k->x0 = (int16_t)(x + k->base * s);
    }
    if (t == k->at) return 0;
    int from = k->at;
    k->at = (int16_t)t;
    send(k, m, i, list, from, t, now);
    return 1u << i;
}

bool knobctl_release(knobctl_t *k, fm1scr_t *m, uint32_t now) {
    (void)m;
    (void)now;
    bool r = k->in_row && !(k->locked_press && !k->moved);
    k->in_row = k->dragging = k->dead = k->cand = k->locked_press = k->moved = false;
    return r;
}

void knobctl_tick(knobctl_t *k, fm1scr_t *m, uint32_t now) {
    // the page changed under the finger, or the knob became one the box can't turn (Mode on Custom)
    if (k->dragging && (m->view != k->view || knobctl_turnable(k, m, k->knob, NULL) != k->list)) cancel(k);
    uint8_t locked = 0;
    for (int i = 0; i < 4; i++) {
        int idx, list = knobctl_turnable(k, m, i, &idx);
        if (list < 0 && m->view >= 0 && m->value[i][0]) locked |= (uint8_t)(1u << i);
        if (k->w[i].waiting) {
            // the value shown is the one sent (of twins that say the same, it's taken to be)
            bool same = m->view == k->w[i].view && knobctl_find(k->w[i].list, m->value[i], k->w[i].idx) == k->w[i].idx;
            int32_t age = (int32_t)(now - k->w[i].ms);
            bool settle = false;
            if (m->view != k->w[i].view) {   // another page: there's no telling
                k->w[i].waiting = false;
            } else if (same && m->heard[i] != k->w[i].heard) {   // said back
                settle = true;
            } else if (k->w[i].asked && m->n_page != k->w[i].pages) {   // the whole page, since the box asked
                settle = true;
                k->n_paged++;
                // Motion's knobs: nearer what was sent than what was there before, it was taken, then moved
                if (!same && list == k->w[i].list && idx >= 0 && knobctl_moves(m->view, i) &&
                    abs(idx - k->w[i].idx) < abs(idx - k->w[i].before)) {
                    k->w[i].waiting = settle = false;
                    k->n_answered++;
                    k->n_moved++;
                }
            } else if (age >= (int32_t)KC_ANSWER_MS) {   // (the WHERE went unanswered too)
                settle = true;
            } else if (!k->w[i].asked && age >= (int32_t)KC_ASK_MS) {
                k->w[i].asked = true;
                k->w[i].pages = m->n_page;
                k->want_where = true;
                k->n_asked++;
            }
            if (settle) {
                k->w[i].waiting = false;
                if (same) {
                    k->c_idx[i] = k->w[i].idx;   // (a value that says the same as its neighbour: this one)
                    k->n_answered++;
                } else {
                    k->n_not_taken++;
                    fm1scr_note(m, "THE FM-1 DIDN'T TAKE IT", now);
                }
            }
        }
        // what's shown being sent: where the finger is, or the last send until it's said back
        if (k->dragging && i == k->knob)
            fm1scr_set_preview(m, i, knobctl_text(k->list, k->at), k->at, knobctl_list_n(k->list));
        else if (k->w[i].waiting)
            fm1scr_set_preview(m, i, knobctl_text(k->w[i].list, k->w[i].idx), k->w[i].idx,
                               knobctl_list_n(k->w[i].list));
        else
            fm1scr_set_preview(m, i, NULL, 0, 0);
    }
    fm1scr_set_locked(m, locked);
}
