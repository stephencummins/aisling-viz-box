// The FM-1's screen on the box (tests 6 and 7): what AISLING says to a screen reader, read back into the screen it
// describes, and drawn. fm1scr.h has the overview. The grammar is AISLING's own (fm1-aisling's ui.c: sp_page,
// sp_garden, speak, say_as, warn), and its names below are copied from there: our own MIT code.
#include <string.h>
#include "fm1scr.h"
#include "lcd.h"
#include "pflash.h"

// ---- AISLING's names (ui.c: VIEW_NAME, KNOB_LABEL, CHORD_Q), in its order ----

static const char *const VIEW_NAME[FM1_VIEWS] = {
    "Aisling", "Drone", "Sequence", "Lead", "Sequence 2", "Space", "Setup", "Mix", "Drone sound", "Seq sound",
    "Lead sound", "Seq2 sound", "Drone move", "Repeats", "Lead move", "Chords", "Pattern", "Perform", "Key",
    "Atmosphere", "Echo", "Atmos sound", "Arc", "Seq move", "Seq2 move", "Atmos move", "Lead style",
};
enum { V_AISLING = 0, V_SETUP = 6, V_CHORDS = 15, V_PATTERN = 16, V_PERFORM = 17, V_KEY = 18 };

static const char *const KNOB_LABEL[FM1_VIEWS][4] = {
    {"Filter", "Evolve", "Motion", "Drift"}, {"Pedal", "Reso", "Phaser", "Attack"},
    {"Density", "Octave", "Length", "Grid"}, {"Phrases", "Octave", "Ornament", "Vibrato"},
    {"Chance", "Octave", "Length", "Level"}, {"Echo", "Echo time", "Feedback", "Reverb"},
    {"Tune", "MIDI out", "Lights", "Save"}, {"Drone", "Seq", "Lead", "Seq 2"},
    {"Colour", "Release", "Reverb", "Echo"}, {"Cutoff", "Decay", "Reverb", "Echo"},
    {"Tone", "Attack", "Reverb", "Echo"}, {"Cutoff", "Decay", "Reverb", "Echo"},
    {"Width", "Time", "Rate", "Intro"}, {"Repeat A", "A time", "Repeat B", "B time"},
    {"Width", "Time", "Rate", "Intro"}, {"Slot", "Chord", "Type", "Play"},
    {"Lane", "Accent", "Chance", "Play"}, {"Filter", "Wet", "Energy", "Chorus"},
    {"Tempo", "Root", "Mode", "Transpose"}, {"Density", "Width", "Drift", "Air"},
    {"Blur", "Tone", "Width", "Size"}, {"Colour", "Ring", "Reverb", "Echo"},
    {"Lead in", "Bridge", "Arc", "Memory"}, {"Width", "Time", "Rate", "Intro"},
    {"Width", "Time", "Rate", "Intro"}, {"Width", "Time", "Rate", "Intro"},
    {"Ornament", "Glide", "Vibrato", "Memory"},
};

// The chord's name after its root, as the FM-1's screen shows it, and as it's said.
static const char *const CHORD_Q[][2] = {
    {"", "major"}, {"m", "minor"}, {"7", "7"}, {"M7", "major 7"}, {"m7", "minor 7"}, {"6", "6"},
    {"m6", "minor 6"}, {"add9", "add 9"}, {"madd9", "minor add 9"}, {"aug", "augmented"}, {"dim", "diminished"},
    {"dim7", "diminished 7"}, {"m7b5", "half diminished"}, {"mM7", "minor major 7"}, {"sus2", "suspended 2"},
    {"sus4", "suspended 4"}, {"7sus4", "7 suspended 4"}, {"5", "5"},
};
#define NCHORD_Q ((int)(sizeof CHORD_Q / sizeof CHORD_Q[0]))

// announce.c sends at most 240 characters (ANN_MAX), and ui.c's sp_cat stops at 240 even mid-word. A text this long
// may have lost its end: its last item is left out.
#define CUT_AT 230

#define MSG_MS 1500u    // a message over the header (the FM-1 shows its own for 1.3 s)
#define WARN_MS 4000u
#define TURNED_MS 2000u // a knob just turned stays bright

const char *fm1scr_view_name(int view) { return view >= 0 && view < FM1_VIEWS ? VIEW_NAME[view] : "?"; }
const char *fm1scr_label(int view, int i) {
    return view >= 0 && view < FM1_VIEWS && i >= 0 && i < 4 ? KNOB_LABEL[view][i] : "?";
}

// ---- small helpers ----

static bool letter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

// s after the prefix p, or NULL.
static char *after(char *s, const char *p) {
    size_t n = strlen(p);
    return strncmp(s, p, n) ? NULL : s + n;
}

// Sets d (cap bytes) to the n bytes at s; if that changes it, marks bit dirty.
static void setn(fm1scr_t *m, char *d, size_t cap, const char *s, size_t n, uint32_t bit) {
    if (n >= cap) n = cap - 1;
    if (strlen(d) == n && !memcmp(d, s, n)) return;
    memcpy(d, s, n);
    d[n] = 0;
    m->dirty |= bit;
}
#define SETS(m, field, s, bit) setn((m), (m)->field, sizeof (m)->field, (s), strlen(s), (bit))

static void set8(fm1scr_t *m, int8_t *d, int v, uint32_t bit) {
    if (*d == v) return;
    *d = (int8_t)v;
    m->dirty |= bit;
}

// Splits s in place at each sep; returns the items (at most max: the rest is dropped).
static int split(char *s, const char *sep, char **item, int max) {
    int n = 0;
    size_t k = strlen(sep);
    if (!*s) return 0;
    while (n < max) {
        item[n++] = s;
        char *e = strstr(s, sep);
        if (!e) break;
        *e = 0;
        s = e + k;
    }
    return n;
}

void fm1scr_caps(char *out, const char *s, int max) {
    int i = 0;
    for (; s[i] && i < max; i++) {
        char c = s[i];
        bool flat = c == 'b' && i > 0 && s[i - 1] >= 'A' && s[i - 1] <= 'G' && (i < 2 || !letter(s[i - 2])) &&
                    !letter(s[i + 1]);
        out[i] = c >= 'a' && c <= 'z' && !flat ? (char)(c - 32) : c;
    }
    out[i] = 0;
}

// ---- the model ----

// The pages an effect's flash belongs to: PERFORM, or none heard yet.
static bool flash_page(const fm1scr_t *m) { return m->view == V_PERFORM || m->view < 0; }

void fm1scr_init(fm1scr_t *m) {
    memset(m, 0, sizeof *m);
    m->playing = m->unsaved = -1;
    m->view = -1;
    m->turned = -1;
    m->flash = -1;
    m->next_pct = -1;
    m->hold = -1;
    m->drawn_conn = -1;
    m->dirty = FM1_D_ALL;
}

void fm1scr_set_preview(fm1scr_t *m, int i, const char *text, int idx, int n) {
    if (i < 0 || i > 3) return;
    if (!text) idx = n = 0;
    if (idx < 0) idx = 0;
    if (n < 0) n = 0;
    if (n > 255) n = 255;
    if (idx > n) idx = n;
    if (m->pv[i] == text && m->pv_idx[i] == idx && m->pv_n[i] == n) return;
    m->pv[i] = text;
    m->pv_idx[i] = (uint8_t)idx;
    m->pv_n[i] = (uint8_t)n;
    m->dirty |= FM1_D_KNOB << i;
}

void fm1scr_set_locked(fm1scr_t *m, uint8_t mask) {
    mask &= 15u;
    uint8_t d = m->locked ^ mask;
    m->locked = mask;
    m->dirty |= (uint32_t)d << 2;
}

void fm1scr_note(fm1scr_t *m, const char *text, uint32_t now) {
    size_t n = strlen(text);
    if (n >= sizeof m->msg) n = sizeof m->msg - 1;
    memcpy(m->msg, text, n);
    m->msg[n] = 0;
    m->warn = true;
    m->msg_until = now + WARN_MS;
    m->dirty |= FM1_D_HEAD;
    m->want_where = true;   // and the FM-1's own values again
}

void fm1scr_redraw(fm1scr_t *m) {
    m->dirty = FM1_D_ALL;
    m->drawn_conn = -1;
}

static void message(fm1scr_t *m, const char *t, uint8_t flags, uint32_t now) {
    m->warn = (flags & FM1_SAY_FLAG_KEEP) != 0;
    size_t n = strlen(t);
    if (n >= sizeof m->msg) n = sizeof m->msg - 1;
    memcpy(m->msg, t, n);
    m->msg[n] = 0;
    m->msg_until = now + (m->warn ? WARN_MS : MSG_MS);
    m->dirty |= FM1_D_HEAD;
    m->n_msg++;
    m->want_where = true;   // a button may have changed what's on the screen without saying it
}

static void turned(fm1scr_t *m, int i, uint32_t now) {
    if (m->turned >= 0 && m->turned != i) m->dirty |= FM1_D_KNOB << m->turned;
    m->turned = (int8_t)i;
    m->turned_ms = now;
    m->dirty |= FM1_D_KNOB << i;
}

// The knob labelled at the start of item on this page ("Echo time 1/4." is Echo time's, not Echo's): its index,
// and its value in *value; -1 if none.
static int knob_of(int view, char *item, char **value) {
    int best = -1;
    size_t bl = 0;
    for (int i = 0; i < 4; i++) {
        const char *l = KNOB_LABEL[view][i];
        size_t n = strlen(l);
        if (n > bl && !strncmp(item, l, n) && item[n] == ' ' && item[n + 1]) best = i, bl = n;
    }
    if (best >= 0) *value = item + bl + 1;
    return best;
}

// A knob's value, said in a page or as it turned. The Key page's knobs are also the header's key and tempo, and
// Setup's Save knob says when it has saved.
static void knob_value(fm1scr_t *m, int i, const char *v) {
    setn(m, m->value[i], sizeof m->value[i], v, strlen(v), FM1_D_KNOB << i);
    if (m->view == V_KEY) {
        if (i == 0) SETS(m, tempo, v, FM1_D_HEAD);
        if (i == 1) SETS(m, root, v, FM1_D_HEAD);
        if (i == 2) SETS(m, mode, v, FM1_D_HEAD);
    }
    if (m->view == V_SETUP && i == 3 && !strcmp(v, "Saved")) set8(m, &m->unsaved, 0, FM1_D_HEAD);
}

// The header's items: "Skellig", "D Minor", "tempo 76", "playing", "not saved yet".
static void header(fm1scr_t *m, char **it, int k, bool whole) {
    bool said_play = false, said_unsaved = false;
    for (int j = 0; j < k; j++) {
        char *v;
        if (j == 0) {
            SETS(m, piece, it[0], FM1_D_HEAD);
        } else if (j == 1) {
            char *sp = strchr(it[1], ' ');   // "D Minor", "G Hex min", "C# Mixolyd."
            if (sp) {
                setn(m, m->root, sizeof m->root, it[1], (size_t)(sp - it[1]), FM1_D_HEAD);
                SETS(m, mode, sp + 1, FM1_D_HEAD);
            }
        } else if ((v = after(it[j], "tempo "))) {
            SETS(m, tempo, v, FM1_D_HEAD);
        } else if (!strcmp(it[j], "playing") || !strcmp(it[j], "resting")) {
            set8(m, &m->playing, it[j][0] == 'p', FM1_D_HEAD);
            said_play = true;
        } else if (!strcmp(it[j], "not saved yet")) {
            set8(m, &m->unsaved, 1, FM1_D_HEAD);
            said_unsaved = true;
        }
    }
    // "not saved yet" comes after "playing" or "resting": with the header whole, its absence says it's saved.
    if (whole && said_play && !said_unsaved) set8(m, &m->unsaved, 0, FM1_D_HEAD);
}

// "Drone page, voice Pipes: Pedal 90, Reso 0, Phaser 15, Attack 40" (a new page), and with ". Piece Skellig, D
// Minor, tempo 76, playing" after it, the whole screen; on Setup, then ". CPU 3%, peak 12%, on USB power".
static void page_text(fm1scr_t *m, int view, char *rest, bool cut) {
    char *head = strstr(rest, ". Piece "), *extra = NULL, *detail = NULL, *knobs = NULL;
    if (head) {   // (the first: a last value ending in "." gives "1/4.. Piece")
        *head = 0;
        head += 8;
        if ((extra = strstr(head, ". CPU "))) *extra = 0, extra += 2;
    }
    if (rest[0] == ',' && rest[1] == ' ') detail = rest + 2;
    char *colon = strstr(rest, ": ");
    if (colon) *colon = 0, knobs = colon + 2;
    // which part is last, and so may have lost its end
    int last = extra ? 3 : head ? 2 : knobs ? 1 : 0;

    bool same = view == m->view;
    if (!same) {
        m->view = (int8_t)view;
        m->turned = -1;
        m->dirty |= FM1_D_PAGE | FM1_D_KNOBS | FM1_D_LINE;
        m->line[0] = 0;          // the moving filter was the last page's: the next WHERE says this one's
        m->want_where = true;
    }
    m->n_page++;

    // its voice, character, walk, sleep timer or evolve timing
    const char *d = "";
    if (detail && !(cut && last == 0)) {
        char *v;
        d = (v = after(detail, "voice ")) ? v : (v = after(detail, "character ")) ? v : detail;
    } else if (cut && last == 0) {
        d = m->detail;   // (as it was)
    }
    if (d != m->detail) SETS(m, detail, d, FM1_D_PAGE);

    bool got[4] = {false, false, false, false};
    if (knobs) {
        char *it[8];
        int k = split(knobs, ", ", it, 8);
        if (cut && last == 1) k--;
        for (int j = 0; j < k; j++) {
            char *v;
            int i = knob_of(view, it[j], &v);
            if (i < 0) {
                m->n_unmatched++;
                m->want_where = true;
                continue;
            }
            got[i] = true;
            knob_value(m, i, v);
        }
    }
    for (int i = 0; i < 4; i++) {
        if (got[i] || (cut && same && last <= 1)) continue;   // (cut: a value not said may still be right)
        if (m->value[i][0]) m->value[i][0] = 0, m->dirty |= FM1_D_KNOB << i;
    }

    if (head) {
        char *it[8];
        int k = split(head, ", ", it, 8);
        if (cut && last == 2) k--;
        header(m, it, k, !(cut && last == 2));
        m->n_header++;
    }
    if (view == V_SETUP) {
        char e[sizeof m->extra] = "";
        if (extra) {   // "CPU 3%", "peak 12%", "on USB power" or "battery about 60%", "2 dropouts"
            char *it[6];
            int k = split(extra, ", ", it, 6);
            if (cut) k--;
            size_t n = 0;
            for (int j = 0; j < k; j++) {
                const char *s = it[j];
                char *v;
                if (!strcmp(s, "on USB power")) s = "USB power";
                else if ((v = after(it[j], "battery about "))) {
                    static char b[24];
                    size_t bn = strlen(v);
                    if (bn > 15) bn = 15;
                    memcpy(b, "battery ", 8);
                    memcpy(b + 8, v, bn);
                    b[8 + bn] = 0;
                    s = b;
                }
                size_t sn = strlen(s);
                if (n + (j ? 2 : 0) + sn >= sizeof e) break;
                if (j) e[n++] = ' ', e[n++] = ' ';
                memcpy(e + n, s, sn);
                n += sn;
                e[n] = 0;
            }
        } else if (cut) {
            memcpy(e, m->extra, sizeof e);
        }
        SETS(m, extra, e, FM1_D_LINE);
    }
}

// "Chord F minor 7, next chord 40% of the way. Section Flow. Cutoff moving darker, about -1 octave. Scale D, E,
// F, G, A, Bb and B, C": the middle of the screen.
static bool garden_text(fm1scr_t *m, char *t, bool cut) {
    char *s[8];
    int k = split(t, ". ", s, 8);
    char *c = k >= 2 ? after(s[0], "Chord ") : NULL;
    if (!c) return false;
    if (cut) k--;
    char *how = strstr(c, ", ");
    if (how) {
        *how = 0;
        how += 2;
        char *v;
        if (!strcmp(how, "hold")) {
            set8(m, &m->hold, 1, FM1_D_NEXT);
            set8(m, &m->next_pct, -1, FM1_D_NEXT);
        } else if (!strcmp(how, "held")) {
            set8(m, &m->hold, 2, FM1_D_NEXT);
            set8(m, &m->next_pct, -1, FM1_D_NEXT);
        } else if ((v = after(how, "next chord "))) {
            int p = 0;
            while (*v >= '0' && *v <= '9' && p < 1000) p = p * 10 + (*v++ - '0');
            set8(m, &m->hold, 0, FM1_D_NEXT);
            set8(m, &m->next_pct, p > 100 ? 100 : p, FM1_D_NEXT);
        }
    }
    SETS(m, chord, c, FM1_D_CHORD);
    bool bright = false;
    for (int j = 1; j < k; j++) {
        char *v;
        if ((v = after(s[j], "Section "))) {
            SETS(m, section, v, FM1_D_CHORD);
        } else if (!strncmp(s[j], "Scale", 5) && (s[j][5] == ' ' || !s[j][5])) {
            // "D, E, F, G, A, Bb and B, C" shown as "D E F G A Bb/B C"
            char out[sizeof m->scale];
            size_t n = 0;
            for (v = s[j] + 5 + (s[j][5] == ' '); *v && n + 1 < sizeof out;) {
                if (!strncmp(v, ", ", 2)) out[n++] = ' ', v += 2;
                else if (!strncmp(v, " and ", 5)) out[n++] = '/', v += 5;
                else out[n++] = *v++;
            }
            out[n] = 0;
            SETS(m, scale, out, FM1_D_SCALE);
        } else if ((v = strstr(s[j], " moving "))) {
            // "Cutoff moving darker, about -1 octave" shown as "Cutoff darker -1 octave"
            char out[sizeof m->line];
            size_t n = 0, ln = (size_t)(v - s[j]);
            char *rest = v + 8, *about = strstr(rest, ", about ");
            const char *amount = about ? about + 8 : "";
            if (about) *about = 0;
            if (!strcmp(amount, "half an octave")) amount = "1/2 octave";
            size_t rn = strlen(rest), an = strlen(amount);
            if (ln + 1 + rn + 1 + an < sizeof out) {
                memcpy(out, s[j], ln);
                n = ln;
                out[n++] = ' ';
                memcpy(out + n, rest, rn);
                n += rn;
                if (an) out[n++] = ' ', memcpy(out + n, amount, an), n += an;
            }
            out[n] = 0;
            SETS(m, line, out, FM1_D_LINE);
            bright = true;
        }
    }
    if (!bright && !cut) SETS(m, line, "", FM1_D_LINE);
    m->n_garden++;
    return true;
}

// "Chord Bb minor": a chord change, not the Chords page's Chord knob ("Chord Empty", "Chord Aug", "Chord Dim").
static bool chord_change(const char *r) {
    if (r[0] < 'A' || r[0] > 'G') return false;
    const char *p = r + 1;
    if (*p == '#' || (*p == 'b' && (p[1] == ' ' || !p[1] || p[1] == ','))) p++;
    return *p == ' ' || !*p || *p == ',';
}

void fm1scr_say(fm1scr_t *m, const char *text, uint32_t n, uint8_t flags, uint32_t now) {
    char t[256];
    uint32_t k = 0;
    for (uint32_t i = 0; i < n && k < sizeof t - 1; i++)   // (announce.c sends 7-bit text; anything else is noise)
        if (text[i] >= 32 && text[i] < 127) t[k++] = text[i];
    t[k] = 0;
    bool cut = n >= CUT_AT;
    m->n_said++;
    if (cut) m->n_cut++;
    if (!m->seen) m->seen = true, m->dirty = FM1_D_ALL;
    if (!k) return;

    char *r;
    int view = -1;
    for (int v = 0; v < FM1_VIEWS; v++) {   // the page's name, then " page" (the longest that fits)
        size_t l = strlen(VIEW_NAME[v]);
        if (!strncmp(t, VIEW_NAME[v], l) && !strncmp(t + l, " page", 5) &&
            (!t[l + 5] || t[l + 5] == ':' || t[l + 5] == ',' || t[l + 5] == '.') &&
            (view < 0 || l > strlen(VIEW_NAME[view])))
            view = v;
    }

    if (after(t, "Chord ") && strstr(t, ". Section ") && garden_text(m, t, cut)) return;
    if (view >= 0) {
        page_text(m, view, t + strlen(VIEW_NAME[view]) + 5, cut);
        return;
    }
    if ((r = after(t, "Chord ")) && chord_change(r)) {
        SETS(m, chord, r, FM1_D_CHORD);
        if (m->hold == 0) set8(m, &m->next_pct, 0, FM1_D_NEXT);   // (a new chord starts the way to the next)
        m->n_chord++;
        return;
    }
    if ((r = after(t, "PIECE "))) {   // "PIECE Aran, A Dorian, tempo 96": the header, and "PIECE ARAN" over it
        char *it[4];
        int c = split(r, ", ", it, 4);
        if (c >= 3) header(m, it, c, false);
        const char *name = c ? it[0] : "";
        char msg[32] = "PIECE ";
        size_t pn = strlen(name);
        if (pn > sizeof msg - 7) pn = sizeof msg - 7;
        memcpy(msg + 6, name, pn);
        msg[6 + pn] = 0;
        message(m, msg, flags, now);
        return;
    }
    if ((r = after(t, "MODE "))) {   // the MODE button, or (test 7) the Key page's Mode knob turned by a CC
        SETS(m, mode, r, FM1_D_HEAD);
        if (m->view == V_KEY) {   // ui.c's set_mode says this instead of the knob: it's the knob's value too
            knob_value(m, 2, r);
            turned(m, 2, now);
            m->heard[2]++;
        }
    }
    if ((r = after(t, "TEMPO "))) SETS(m, tempo, r, FM1_D_HEAD);
    if ((r = after(t, "Tempo "))) {   // the clock came or went (or the Key page's Tempo knob turned)
        SETS(m, tempo, r, FM1_D_HEAD);
        if (m->view == V_KEY) {
            knob_value(m, 0, r);
            if (flags & FM1_SAY_FLAG_NOW) turned(m, 0, now), m->n_knob++, m->heard[0]++;
        }
        return;
    }
    if (!strcmp(t, "PLAYING") || !strcmp(t, "RESTING")) {
        set8(m, &m->playing, t[1] == 'L', FM1_D_HEAD);
        if (flags & FM1_SAY_FLAG_NOW) message(m, t, flags, now);   // (a button; MIDI start and stop aren't)
        return;
    }
    if (!strcmp(t, "SAVED")) set8(m, &m->unsaved, 0, FM1_D_HEAD);
    if (after(t, "sleep ") && m->view == V_SETUP) SETS(m, detail, t, FM1_D_PAGE);
    if (after(t, "evolve ") && m->view == V_AISLING)
        SETS(m, detail, strcmp(t, "evolve now and then") ? t : "", FM1_D_PAGE);

    if (m->view >= 0) {   // "Ornament 35", "MIDI out On", "A time 1/8." (labels match by case: "DRONE 55" is a message)
        char *v;
        int i = knob_of(m->view, t, &v);
        if (i >= 0) {
            knob_value(m, i, v);
            if (m->view == V_KEY) m->want_where = true;   // a new key: a new scale
            // (test 7) AISLING says only the first knob that changed: a new slot's chord and type, a new lane's
            // chance, a chord set to Empty's type aren't said
            if (m->view == V_CHORDS || m->view == V_PATTERN) m->want_where = true;
            turned(m, i, now);
            m->n_knob++;
            m->heard[i]++;
            return;
        }
        // (a knob of a page the box missed, as its capital-then-lowercase label: shown as a message; the WHERE
        // puts it right)
        // (AISLING 1.3: on PERFORM a held key says its effect's name, "Stutter", "Tape stop": a message, nothing missed)
        if (m->view != V_PERFORM && t[0] >= 'A' && t[0] <= 'Z' && t[1] >= 'a' && t[1] <= 'z') m->n_unmatched++;
    }
    // (AISLING 1.3) an effect's name, whole: its flash. Also with no page heard yet (the box started on the
    // landscape with the FM-1 already on PERFORM, and has asked for no WHERE): only PERFORM says these names alone.
    if (flash_page(m))
        for (int k = 0; k < PFLASH_N; k++)
            if (!strcmp(t, PFLASH[k].name)) {
                m->flash = (int8_t)k;
                m->flash_until = now + FM1_FLASH_MS;
                break;
            }
    message(m, t, flags, now);
}

int fm1scr_flash(const fm1scr_t *m, uint32_t now) {
    return m->flash >= 0 && flash_page(m) && (int32_t)(now - m->flash_until) < 0 ? m->flash : -1;
}

void fm1scr_tick(fm1scr_t *m, uint32_t now) {
    if (m->msg[0] && (int32_t)(now - m->msg_until) >= 0) {
        m->msg[0] = 0;
        m->warn = false;
        m->dirty |= FM1_D_HEAD;
    }
    if (m->turned >= 0 && (int32_t)(now - m->turned_ms - TURNED_MS) >= 0) {
        m->dirty |= FM1_D_KNOB << m->turned;
        m->turned = -1;
    }
}

// ---- drawing ----
//
// y 0-271, in bands; each band is boxes that tile it exactly. A box is drawn as its text plus the box around it
// filled, so nothing is drawn twice and a change doesn't flicker.
//    0- 43  header: the piece, PLAYING; the key and tempo, NOT SAVED (or a message over both)
//   44- 45  a rule
//   46- 79  the page's name, and its voice (or character, ...)
//   80-139  the four knobs: the label, the value
//  140-141  a rule
//  142-185  the chord, the section
//  186-209  NEXT CHORD and how far
//  210-211  (gap)
//  212-235  the filter on the move (a sound page), or Setup's CPU and power
//  236-271  SCALE and its notes

#define BG C_BLACK
#define FG C_WHITE
#define LABEL RGB(150, 150, 150)
#define RULE RGB(45, 45, 45)
#define PLAYING RGB(90, 210, 110)
#define BAR_OFF RGB(55, 55, 55)
#define DIM RGB(70, 70, 70)       // (test 7) a knob the box can't turn: its label
#define KBAR_Y 133                // (test 7) the bar under a value being sent
#define KBAR_H 3
#define KBAR_W 108

enum { AL_L, AL_R, AL_C };

static void fill(int x, int y, int w, int h, uint16_t c) {
    if (w > 0 && h > 0) lcd_fill_rect(x, y, w, h, c);
}

// The largest scale up to max at which n characters fit w pixels (1 if none does).
static int fit(int n, int w, int max) {
    for (int s = max; s > 1; s--)
        if (n * 6 * s <= w) return s;
    return 1;
}

// s in the box (bx, by, bw, bh), at ty from its top, pad from its side; the rest of the box filled with bg. Cut to
// what fits. s is drawn as it is (no capitals): the callers have done that.
static void box_text(int bx, int by, int bw, int bh, int ty, int pad, const char *s, int scale, int align,
                     uint16_t fg, uint16_t bg) {
    char t[81];
    int cw = 6 * scale, ch = 8 * scale;
    int maxc = (bw - 2 * pad) / cw;
    if (maxc < 0) maxc = 0;
    if (maxc > 80) maxc = 80;
    int n = (int)strlen(s);
    if (n > maxc) n = maxc;
    memcpy(t, s, (size_t)n);
    t[n] = 0;
    if (ty + ch > bh) ty = bh - ch;
    if (ty < 0) ty = 0;
    int tw = n * cw, tx = align == AL_L ? bx + pad : align == AL_R ? bx + bw - pad - tw : bx + (bw - tw) / 2;
    int th = n ? ch : 0;
    fill(bx, by, bw, ty, bg);
    fill(bx, by + ty + th, bw, bh - ty - th, bg);
    fill(bx, by + ty, tx - bx, th, bg);
    fill(tx + tw, by + ty, bx + bw - tx - tw, th, bg);
    if (n) lcd_text(tx, by + ty, t, scale, fg, bg);
}

// (AISLING 1.3) PERFORM's flash: the lettering black on the effect's colour, as the FM-1's.
#define PF_NAME_MAX 8   // the name's scale at most: "TAPE STOP" is 432 px
#define PF_LINE 2
#define PF_GAP 20
void fm1scr_draw_flash(int k) {
    if (k < 0 || k >= PFLASH_N) return;
    char t[81];
    uint16_t c = PFLASH[k].col;
    fm1scr_caps(t, PFLASH[k].name, 80);
    int sc = fit((int)strlen(t), LCD_W - 32, PF_NAME_MAX);
    int top = (FM1SCR_BOTTOM - (8 * sc + PF_GAP + 8 * PF_LINE)) / 2, mid = top + 8 * sc + PF_GAP;
    box_text(0, 0, LCD_W, mid, top, 16, t, sc, AL_C, C_BLACK, c);
    fm1scr_caps(t, PFLASH[k].line, 80);
    box_text(0, mid, LCD_W, FM1SCR_BOTTOM - mid, 0, 16, t, PF_LINE, AL_C, C_BLACK, c);
}

// The same, in capitals (but a flat's b), at the largest scale up to max that fits, centred in the box's height.
static void box_caps(int bx, int by, int bw, int bh, int pad, const char *s, int max, int align, uint16_t fg) {
    char t[81];
    fm1scr_caps(t, s, 80);
    int sc = fit((int)strlen(t), bw - 2 * pad, max);
    box_text(bx, by, bw, bh, (bh - 8 * sc) / 2, pad, t, sc, align, fg, BG);
}

static void draw_head(fm1scr_t *m) {
    char a[96], b[96];
    if (m->msg[0]) {   // a message over the header, as the FM-1 shows its own over its header
        uint16_t c = m->warn ? C_RED : FG;
        fm1scr_caps(a, m->msg, 95);
        int n = (int)strlen(a);
        if (n <= 25) {   // (25 at scale 3 is 450 pixels: with the sides, the most that fits 480)
            box_text(0, 0, LCD_W, 44, 10, 8, a, 3, AL_L, c, BG);
            return;
        }
        int cut = n > 38 ? 38 : n;   // two lines at scale 2, broken at a space
        if (n > 38)
            for (int i = 38; i > 20; i--)
                if (a[i] == ' ') { cut = i; break; }
        memcpy(b, a, (size_t)cut);
        b[cut] = 0;
        const char *rest = a + cut + (a[cut] == ' ');
        box_text(0, 0, LCD_W, 22, 4, 8, b, 2, AL_L, c, BG);
        box_text(0, 22, LCD_W, 22, 2, 8, rest, 2, AL_L, c, BG);
        return;
    }
    fm1scr_caps(a, m->piece, 40);
    box_text(0, 0, 330, 22, 4, 8, a, 2, AL_L, FG, BG);
    box_text(330, 0, 150, 22, 4, 8, m->playing == 1 ? "PLAYING" : m->playing == 0 ? "RESTING" : "", 2, AL_R,
             m->playing == 1 ? PLAYING : LABEL, BG);
    b[0] = 0;
    if (m->root[0] || m->mode[0] || m->tempo[0]) {
        char k[40];
        int n = 0;
        if (m->root[0] || m->mode[0]) {
            size_t rn = strlen(m->root), mn = strlen(m->mode);
            memcpy(k, m->root, rn);
            k[rn] = ' ';
            memcpy(k + rn + 1, m->mode, mn);
            n = (int)(rn + 1 + mn);
        }
        if (m->tempo[0]) {
            const char *tp = n ? "   TEMPO " : "TEMPO ";
            size_t tn = strlen(tp), vn = strlen(m->tempo);
            memcpy(k + n, tp, tn);
            memcpy(k + n + tn, m->tempo, vn);
            n += (int)(tn + vn);
        }
        k[n] = 0;
        fm1scr_caps(b, k, 40);
    }
    box_text(0, 22, 330, 22, 2, 8, b, 2, AL_L, C_VELLUM, BG);
    box_text(330, 22, 150, 22, 2, 8, m->unsaved == 1 ? "NOT SAVED" : "", 2, AL_R, C_ORANGE, BG);
}

static void draw_page(fm1scr_t *m) {
    char a[48];
    fm1scr_caps(a, m->view >= 0 ? VIEW_NAME[m->view] : "", 40);
    box_text(0, 46, 214, 34, 5, 8, a, 3, AL_L, C_ORANGE, BG);
    box_caps(214, 46, 266, 34, 8, m->detail, 2, AL_R, C_VELLUM);
}

// A knob: its label (dimmed if the box can't turn it, test 7), its value, or (test 7) the value the box is sending,
// with a bar under it for where that is in the knob's range.
static void draw_knob(fm1scr_t *m, int i) {
    int x = i * 120;
    bool on = m->view >= 0 && m->value[i][0];
    bool pv = on && m->pv[i];
    char a[32];
    fm1scr_caps(a, on ? KNOB_LABEL[m->view][i] : "", 30);
    box_text(x, 80, 120, 22, 4, 6, a, 2, AL_L, on && (m->locked >> i & 1u) ? DIM : LABEL, BG);
    fm1scr_caps(a, pv ? m->pv[i] : on ? m->value[i] : "", 30);
    int sc = fit((int)strlen(a), 108, 3);
    uint16_t fg = pv || m->turned == i ? C_ORANGE : FG;
    if (!pv) {
        box_text(x, 102, 120, 38, 4 + (3 - sc) * 4, 6, a, sc, AL_L, fg, BG);
        return;
    }
    // y 133-135 the bar: below the value's 24 rows at the largest scale (106-129), above the rule at 140. The value's
    // box stops above it, and the bar's row is filled once, so nothing is drawn twice.
    box_text(x, 102, 120, KBAR_Y - 102, 4 + (3 - sc) * 4, 6, a, sc, AL_L, fg, BG);
    int n = m->pv_n[i], w = n > 1 && m->pv_idx[i] < n ? (KBAR_W * m->pv_idx[i] + (n - 1) / 2) / (n - 1) : 0;
    fill(x, KBAR_Y, 6, KBAR_H, BG);
    fill(x + 6, KBAR_Y, w, KBAR_H, C_ORANGE);
    fill(x + 6 + w, KBAR_Y, KBAR_W - w, KBAR_H, BAR_OFF);
    fill(x + 6 + KBAR_W, KBAR_Y, 120 - 6 - KBAR_W, KBAR_H, BG);
    fill(x, KBAR_Y + KBAR_H, 120, 140 - KBAR_Y - KBAR_H, BG);
}

// "Fm7", as the FM-1 shows the chord ("F minor 7" said); one it doesn't know, in capitals as said.
static void chord_symbol(const char *chord, char *out, int max) {
    const char *sp = strchr(chord, ' ');
    if (sp && sp - chord <= 2)
        for (int q = 0; q < NCHORD_Q; q++)
            if (!strcmp(sp + 1, CHORD_Q[q][1])) {
                size_t rn = (size_t)(sp - chord), qn = strlen(CHORD_Q[q][0]);
                if ((int)(rn + qn) > max) break;
                memcpy(out, chord, rn);
                memcpy(out + rn, CHORD_Q[q][0], qn);
                out[rn + qn] = 0;
                return;
            }
    fm1scr_caps(out, chord, max);
}

static void draw_chord(fm1scr_t *m) {
    char a[48];
    chord_symbol(m->chord, a, 40);
    int sc = fit((int)strlen(a), 284, 4);
    box_text(0, 142, 300, 44, (44 - 8 * sc) / 2, 8, a, sc, AL_L, FG, BG);
    box_caps(300, 142, 180, 44, 8, m->section, 3, AL_R, C_VELLUM);
}

static void draw_next(fm1scr_t *m) {
    bool known = m->chord[0] && (m->hold > 0 || m->next_pct >= 0);
    box_text(0, 186, 136, 24, 4, 8, known ? "NEXT CHORD" : "", 2, AL_L, LABEL, BG);
    if (m->hold > 0 || m->next_pct < 0) {
        box_text(136, 186, 268, 24, 4, 4, m->hold == 1 ? "HOLD" : m->hold == 2 ? "HELD" : "", 2, AL_L,
                 m->hold == 1 ? C_ORANGE : LABEL, BG);
        box_text(404, 186, 76, 24, 4, 8, "", 2, AL_R, FG, BG);
        return;
    }
    const int bx = 140, by = 192, bw = 260, bh = 12, on = bw * m->next_pct / 100;
    fill(136, 186, 268, by - 186, BG);
    fill(136, by + bh, 268, 186 + 24 - by - bh, BG);
    fill(136, by, bx - 136, bh, BG);
    fill(bx + bw, by, 136 + 268 - bx - bw, bh, BG);
    fill(bx, by, on, bh, C_ORANGE);
    fill(bx + on, by, bw - on, bh, BAR_OFF);
    char p[8] = {(char)('0' + m->next_pct / 100), (char)('0' + m->next_pct / 10 % 10),
                 (char)('0' + m->next_pct % 10), '%', 0};
    const char *ps = p;
    while (ps[0] == '0' && ps[1] != '%') ps++;
    box_text(404, 186, 76, 24, 4, 8, ps, 2, AL_R, FG, BG);
}

static void draw_line(fm1scr_t *m) {
    box_caps(0, 212, LCD_W, 24, 8, m->view == V_SETUP && m->extra[0] ? m->extra : m->line, 2, AL_L, C_VELLUM);
}

static void draw_scale(fm1scr_t *m) {
    box_text(0, 236, 96, 36, 10, 8, m->scale[0] ? "SCALE" : "", 2, AL_L, LABEL, BG);
    box_caps(96, 236, LCD_W - 96, 36, 8, m->scale, 3, AL_L, FG);
}

void fm1scr_draw(fm1scr_t *m, int conn, uint32_t now) {
    (void)now;
    if (conn != m->drawn_conn) {
        if (!m->seen) m->dirty |= FM1_D_ALL;
        m->drawn_conn = (int8_t)conn;
    }
    uint32_t d = m->dirty;
    m->dirty = 0;
    if (!d) return;
    if (!m->seen) {   // nothing heard yet
        box_text(0, 0, LCD_W, FM1SCR_BOTTOM, 124, 8,
                 conn == 0 ? "NO FM-1" : "WAITING FOR THE FM-1'S SCREEN READER", conn == 0 ? 3 : 2, AL_C,
                 conn == 0 ? C_ORANGE : LABEL, BG);
        return;
    }
    if (d & FM1_D_CLEAR) {   // the rules and the gap: the boxes tile everything else
        fill(0, 44, LCD_W, 2, RULE);
        fill(0, 140, LCD_W, 2, RULE);
        fill(0, 210, LCD_W, 2, BG);
    }
    if (d & FM1_D_HEAD) draw_head(m);
    if (d & FM1_D_PAGE) draw_page(m);
    for (int i = 0; i < 4; i++)
        if (d & (FM1_D_KNOB << i)) draw_knob(m, i);
    if (d & FM1_D_CHORD) draw_chord(m);
    if (d & FM1_D_NEXT) draw_next(m);
    if (d & FM1_D_LINE) draw_line(m);
    if (d & FM1_D_SCALE) draw_scale(m);
}
