/*
 * engine/platform/settings_ui.c -- see settings_ui.h.
 */
#include "settings_ui.h"

#include <stdio.h>
#include <string.h>

#include "settings.h"

/* ================================================================== */
/* UI TEXT: every string the settings screen shows.  ASCII only.       */
/* A translation or a mod replaces this block.                         */
/* ================================================================== */

#define STR_TITLE "Settings"
static const char *const k_tab_names[] = { "Display", "Layout", "Controls", "Sound" };

/* Short button names for the Controls rows (the backend labels are long). */
static const char *const k_button_short[] = {
    "A", "B", "X", "Y", "L", "R", "Start", "Select", "Up", "Down", "Left", "Right"
};

/* Short gamepad control names: the value column is narrow.  Indexes match
 * the backend's EIDOS_PADBIND_BUTTON order (SDL game controller buttons). */
static const char *const k_pad_short[] = {
    "Pad S", "Pad E", "Pad W", "Pad N", "Back", "Guide", "Start", "LS Click",
    "RS Click", "LB", "RB", "DP Up", "DP Down", "DP Left", "DP Right"
};
static const char *const k_axis_pos_short[] = { "LS Right", "LS Down", "RS Right", "RS Down", "LT", "RT" };
static const char *const k_axis_neg_short[] = { "LS Left", "LS Up", "RS Left", "RS Up", "LT -", "RT -" };

/* Help text (bottom screen).  Two lines per option id, then fixed texts. */
static const char *const k_option_help[EIDOS_OPT_COUNT][2] = {
    { "How the game window is shown:", "windowed, fullscreen or borderless." },
    { "Size of the 3D and 2D rendering.", "Fit follows the window size." },
    { NULL, NULL }, { NULL, NULL },
    { "Put the two screens above each", "other or side by side." },
    { "Show the touch screen on top", "and the upper screen below." },
    { "Space between the two screens,", "in pixels." },
    { "Master volume, 0 to 100.", "Left and right change it by 5." },
};
#define HELP_BIND1     "Press A, then press the key or pad"
#define HELP_BIND2     "button to bind. B cancels."
#define HELP_DEFAULTS1 "Set every option and every control"
#define HELP_DEFAULTS2 "to its default."
#define HELP_CAPTURE1  "Waiting for a key or pad button."
#define HELP_CAPTURE2  "Press B to cancel."
#define HELP_SECTION_DISPLAY  "Window mode and resolution."
#define HELP_SECTION_LAYOUT   "Screen arrangement, swap and gap."
#define HELP_SECTION_CONTROLS "Keyboard and gamepad bindings."
#define HELP_SECTION_SOUND    "Master volume."
#define HELP_KEYS_L0   "Up/Down: choose   A: open"
#define HELP_KEYS_L0B  "B: save and go back"
#define HELP_KEYS_L1   "Up/Down: choose   Left/Right: change"
#define HELP_KEYS_L1B  "A: select   B: back"

/* Row labels by option id (EIDOS_OPT_*); NULL for hidden options. */
static const char *const k_option_labels[EIDOS_OPT_COUNT] = {
    "Window mode",     /* DISPLAY_MODE        */
    "Resolution",      /* DISPLAY_RESOLUTION  */
    NULL, NULL,        /* width, height: hidden */
    "Screen layout",   /* LAYOUT_ARRANGEMENT  */
    "Swap screens",    /* LAYOUT_SWAP         */
    "Screen gap",      /* LAYOUT_GAP          */
    "Master volume",   /* SOUND_VOLUME        */
};

/* Value names that differ from the backend's lowercase choice names:
 * {option id, choice index, text}. */
typedef struct ValueName { int id; int index; const char *text; } ValueName;
static const ValueName k_value_names[] = {
    { EIDOS_OPT_DISPLAY_MODE,        0, "Windowed" },
    { EIDOS_OPT_DISPLAY_MODE,        1, "Fullscreen" },
    { EIDOS_OPT_DISPLAY_MODE,        2, "Borderless" },
    { EIDOS_OPT_LAYOUT_ARRANGEMENT,  0, "Stacked" },
    { EIDOS_OPT_LAYOUT_ARRANGEMENT,  1, "Side by side" },
    { EIDOS_OPT_LAYOUT_SWAP,         0, "Off" },
    { EIDOS_OPT_LAYOUT_SWAP,         1, "On" },
};

/* Short names for the side-by-side choices (a choice has about 48 pixels). */
static const ValueName k_choice_names[] = {
    { EIDOS_OPT_DISPLAY_MODE,        0, "Window" },
    { EIDOS_OPT_DISPLAY_MODE,        1, "Full" },
    { EIDOS_OPT_DISPLAY_MODE,        2, "Border" },
    { EIDOS_OPT_LAYOUT_ARRANGEMENT,  0, "Stacked" },
    { EIDOS_OPT_LAYOUT_ARRANGEMENT,  1, "Wide" },
};

#define STR_FIT         "Fit"
#define STR_BACK_L0     "Done"
#define STR_BACK_L1     "Back"
#define HELP_BACK_L0    "Save the changes and leave"
#define HELP_BACK_L1    "Go back to the list of sections"
#define STR_DEFAULTS    "Restore defaults"
#define STR_PRESS_KEY   "Press a key"
#define STR_UNBOUND     "None"

/* ================================================================== */

enum { ROW_OPTION, ROW_BIND, ROW_DEFAULTS, ROW_SECTION };

typedef struct Row { int type; int arg; } Row;

#define MAX_ROWS 24
#define TAB_COUNT 4

static struct {
    int level;        /* 0 section list, 1 inside a section      */
    int tab;          /* the section                             */
    int cursor;       /* index into the tab's rows                */
    int scroll;       /* first visible row                        */
    int capturing;    /* button index + 1, 0 when not capturing   */
    int hold_dir;     /* held direction bit being repeated        */
    int hold_frames;
    Row rows[MAX_ROWS];
    int nrows;
} g;

static void add_row(int *n, int type, int arg)
{
    g.rows[*n].type = type;
    g.rows[*n].arg = arg;
    (*n)++;
}

static void build_rows(void)
{
    int n = 0, i;
    if (g.level == 0) {
        for (i = 0; i < TAB_COUNT; i++) {
            add_row(&n, ROW_SECTION, i);
        }
        g.nrows = n;
        return;
    }
    switch (g.tab) {
    case 0:
        add_row(&n, ROW_OPTION, EIDOS_OPT_DISPLAY_MODE);
        add_row(&n, ROW_OPTION, EIDOS_OPT_DISPLAY_RESOLUTION);
        break;
    case 1:
        add_row(&n, ROW_OPTION, EIDOS_OPT_LAYOUT_ARRANGEMENT);
        add_row(&n, ROW_OPTION, EIDOS_OPT_LAYOUT_SWAP);
        add_row(&n, ROW_OPTION, EIDOS_OPT_LAYOUT_GAP);
        break;
    case 2:
        for (i = 0; i < eidos_settings_button_count() && n < MAX_ROWS - 2; i++) {
            add_row(&n, ROW_BIND, i);
        }
        break;
    default:
        add_row(&n, ROW_OPTION, EIDOS_OPT_SOUND_VOLUME);
        break;
    }
    add_row(&n, ROW_DEFAULTS, 0);
    g.nrows = n;
}

/* The cursor runs over the rows and then one more place: the grey bar
 * ("Done" or "Back"), always shown below the visible rows. */
static int on_back(void)
{
    return g.cursor >= g.nrows;
}

static void clamp_scroll(void)
{
    if (on_back()) {
        return;
    }
    if (g.cursor < g.scroll) {
        g.scroll = g.cursor;
    }
    if (g.cursor >= g.scroll + EIDOS_SUI_VISIBLE) {
        g.scroll = g.cursor - EIDOS_SUI_VISIBLE + 1;
    }
    if (g.scroll > g.nrows - EIDOS_SUI_VISIBLE) {
        g.scroll = g.nrows - EIDOS_SUI_VISIBLE;
    }
    if (g.scroll < 0) {
        g.scroll = 0;
    }
}

void eidos_sui_enter(void)
{
    memset(&g, 0, sizeof g);
    build_rows();
}

void eidos_sui_leave(void)
{
    if (g.capturing) {
        eidos_settings_capture_cancel();
        g.capturing = 0;
    }
    eidos_settings_commit();
}

static void enter_section(int t)
{
    g.tab = t;
    g.level = 1;
    g.cursor = 0;
    g.scroll = 0;
    build_rows();
}

static void leave_section(void)
{
    g.level = 0;
    g.cursor = g.tab;
    g.scroll = 0;
    build_rows();
    clamp_scroll();
}

static void move_cursor(int d)
{
    g.cursor = (g.cursor + d + g.nrows + 1) % (g.nrows + 1);
    clamp_scroll();
}

/* Left or right on an option row (d = -1 or +1). */
static int change_option(int id, int d, int wrap)
{
    const EidosOption *o = eidos_settings_option(id);
    int v, nv;
    if (o == NULL) {
        return 0;
    }
    v = eidos_settings_get(id);
    nv = v + d * (o->step > 0 ? o->step : 1);
    if (nv > o->max) {
        nv = wrap ? o->min : o->max;
    } else if (nv < o->min) {
        nv = wrap ? o->max : o->min;
    }
    if (nv == v) {
        return 0;
    }
    return eidos_settings_set(id, nv) == 0;
}

static int poll_capture(void)
{
    EidosCaptured c;
    int r;
    memset(&c, 0, sizeof c);
    r = eidos_settings_capture_poll(&c);
    if (r == EIDOS_CAPTURE_KEY) {
        eidos_settings_set_key(g.capturing - 1, 0, c.scancode);
    } else if (r == EIDOS_CAPTURE_PAD) {
        eidos_settings_set_pad(g.capturing - 1, 0, c.pad_kind, c.pad_index);
    } else if (r == EIDOS_CAPTURE_CANCELLED) {
        g.capturing = 0;
        return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_BACK;
    } else {
        return 0;
    }
    g.capturing = 0;
    return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
}

/* Direction repeat: the first press acts at once, then after 20 frames every
 * 3 frames while held. */
static unsigned repeat_dirs(unsigned pressed, unsigned held)
{
    static const unsigned dirs[] = { 0x40u, 0x80u, 0x10u, 0x20u };
    unsigned out = pressed & 0xF0u;
    unsigned h = held & 0xF0u;
    int i;
    for (i = 0; i < 4; i++) {
        if (h & dirs[i]) {
            if (g.hold_dir != (int)dirs[i]) {
                g.hold_dir = (int)dirs[i];
                g.hold_frames = 0;
            } else {
                g.hold_frames++;
                if (g.hold_frames >= 20 && (g.hold_frames - 20) % 3 == 0) {
                    out |= dirs[i];
                }
            }
            return out;
        }
    }
    g.hold_dir = 0;
    g.hold_frames = 0;
    return out;
}

int eidos_sui_input(unsigned pressed, unsigned held)
{
    unsigned act;
    const Row *r;
    int res;

    if (g.capturing) {
        res = poll_capture();
        if (g.capturing && (pressed & 2u)) {          /* B cancels when no platform arms the capture */
            eidos_settings_capture_cancel();
            g.capturing = 0;
            res = EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_BACK;
        }
        return res;
    }

    act = repeat_dirs(pressed, held);
    if (pressed & 2u) {
        if (g.level == 0) {
            return EIDOS_SUI_EXIT | EIDOS_SUI_SFX_BACK;
        }
        leave_section();
        return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_BACK;
    }
    if (act & 0x40u) {
        move_cursor(-1);
        return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_MOVE;
    }
    if (act & 0x80u) {
        move_cursor(1);
        return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_MOVE;
    }
    if (on_back()) {
        if (pressed & 1u) {
            if (g.level == 0) {
                return EIDOS_SUI_EXIT | EIDOS_SUI_SFX_SELECT;
            }
            leave_section();
            return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
        }
        return 0;
    }
    r = &g.rows[g.cursor];
    if (r->type == ROW_SECTION) {
        if (pressed & 1u) {
            enter_section(r->arg);
            return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
        }
        return 0;
    }
    if (r->type == ROW_OPTION) {
        int d = (act & 0x10u) ? 1 : (act & 0x20u) ? -1 : 0;
        const EidosOption *o = eidos_settings_option(r->arg);
        int choice = o != NULL && o->kind == EIDOS_OPTKIND_CHOICE;
        if (d == 0 && (pressed & 1u) && choice) {
            d = 1;
        }
        if (d != 0 && change_option(r->arg, d, choice)) {
            return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
        }
        return 0;
    }
    if (pressed & 1u) {
        switch (r->type) {
        case ROW_BIND:
            eidos_settings_capture_begin();
            g.capturing = r->arg + 1;
            return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
        case ROW_DEFAULTS:
            eidos_settings_defaults();
            return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
        default:
            break;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */

int eidos_sui_row_count(void)
{
    int n = g.nrows - g.scroll;
    if (n > EIDOS_SUI_VISIBLE) {
        n = EIDOS_SUI_VISIBLE;
    }
    return 1 + n + 1;        /* the header, the visible rows, the grey bar */
}

int eidos_sui_cursor_row(void)
{
    if (on_back()) {
        return eidos_sui_row_count() - 1;
    }
    return 1 + g.cursor - g.scroll;
}

int eidos_sui_capturing(void)
{
    return g.capturing;
}

int eidos_sui_tab(void)
{
    return g.level == 0 ? -1 : g.tab;
}

int eidos_sui_level(void)
{
    return g.level;
}

int eidos_sui_tab_count(void)
{
    return TAB_COUNT;
}

static void put(char *dst, int n, const char *s)
{
    if (n > 0) {
        snprintf(dst, (size_t)n, "%s", s);
    }
}

static void option_value_of(int id, int v, char *out, int n)
{
    const EidosOption *o = eidos_settings_option(id);
    size_t i;
    if (o != NULL && o->kind == EIDOS_OPTKIND_CHOICE) {
        for (i = 0; i < sizeof k_value_names / sizeof k_value_names[0]; i++) {
            if (k_value_names[i].id == id && k_value_names[i].index == v) {
                put(out, n, k_value_names[i].text);
                return;
            }
        }
    }
    if (o != NULL && o->zero_name != NULL && v == 0) {
        put(out, n, STR_FIT);
        return;
    }
    eidos_settings_value_text(id, out, (size_t)n);
}

static void option_value(int id, char *out, int n)
{
    option_value_of(id, eidos_settings_get(id), out, n);
}

static void pad_short(int kind, int index, char *out, int n)
{
    char tmp[48];
    const char *t = NULL;
    if (kind == EIDOS_PADBIND_BUTTON) {
        if (index >= 0 && index < (int)(sizeof k_pad_short / sizeof k_pad_short[0])) {
            t = k_pad_short[index];
        }
    } else if (index >= 0 && index < 6) {
        t = kind == EIDOS_PADBIND_AXIS_POS ? k_axis_pos_short[index] : k_axis_neg_short[index];
    }
    if (t == NULL) {
        eidos_pad_control_name(kind, index, tmp, sizeof tmp);
        t = tmp;
    }
    put(out, n, t);
}

/* "Key / Pad": the first key and the first pad control, in short names. */
static void bind_value(int button, char *out, int n)
{
    char key[48], pad[32];
    int slot, kind, index;
    key[0] = '\0';
    pad[0] = '\0';
    for (slot = 0; slot < EIDOS_BIND_SLOTS && key[0] == '\0'; slot++) {
        int sc = eidos_settings_get_key(button, slot);
        if (sc != 0) {
            eidos_scancode_name(sc, key, sizeof key);
        }
    }
    for (slot = 0; slot < EIDOS_BIND_SLOTS; slot++) {
        if (eidos_settings_get_pad(button, slot, &kind, &index) == 0 && kind >= 0) {
            pad_short(kind, index, pad, sizeof pad);
            break;
        }
    }
    if (key[0] == '\0' && pad[0] == '\0') {
        put(out, n, STR_UNBOUND);
    } else if (pad[0] == '\0') {
        put(out, n, key);
    } else if (key[0] == '\0') {
        snprintf(out, (size_t)n, "- / %s", pad);
    } else {
        snprintf(out, (size_t)n, "%s / %s", key, pad);
    }
}

void eidos_sui_row_text(int row, char *label, int nl, char *value, int nv)
{
    const Row *r;
    if (nl > 0) { label[0] = '\0'; }
    if (nv > 0) { value[0] = '\0'; }
    if (row == 0) {
        put(label, nl, g.level == 0 ? STR_TITLE : k_tab_names[g.tab]);
        return;
    }
    if (row == eidos_sui_row_count() - 1) {
        put(label, nl, g.level == 0 ? STR_BACK_L0 : STR_BACK_L1);
        return;
    }
    if (row < 1 || g.scroll + row - 1 >= g.nrows) {
        return;
    }
    r = &g.rows[g.scroll + row - 1];
    switch (r->type) {
    case ROW_SECTION:
        put(label, nl, k_tab_names[r->arg]);
        break;
    case ROW_OPTION:
        put(label, nl, k_option_labels[r->arg] != NULL ? k_option_labels[r->arg] : "");
        option_value(r->arg, value, nv);
        break;
    case ROW_BIND: {
        const EidosButtonInfo *b = eidos_settings_button(r->arg);
        put(label, nl, r->arg >= 0 && r->arg < (int)(sizeof k_button_short / sizeof k_button_short[0])
                           ? k_button_short[r->arg] : (b != NULL ? b->label : ""));
        if (g.capturing == r->arg + 1) {
            put(value, nv, STR_PRESS_KEY);
        } else {
            bind_value(r->arg, value, nv);
        }
        break;
    }
    default:
        put(label, nl, STR_DEFAULTS);
        break;
    }
}

int eidos_sui_row_choices(int row, int *active, char names[3][20])
{
    const Row *r;
    const EidosOption *o;
    int i, count;

    if (row < 1 || row == eidos_sui_row_count() - 1
        || g.scroll + row - 1 >= g.nrows) {
        return 0;
    }
    r = &g.rows[g.scroll + row - 1];
    if (r->type != ROW_OPTION) {
        return 0;
    }
    o = eidos_settings_option(r->arg);
    if (o == NULL || o->kind != EIDOS_OPTKIND_CHOICE) {
        return 0;
    }
    count = o->max - o->min + 1;
    if (count < 2 || count > 3) {
        return 0;
    }
    for (i = 0; i < count; i++) {
        size_t k;
        int found = 0;

        for (k = 0; k < sizeof k_choice_names / sizeof k_choice_names[0]; k++) {
            if (k_choice_names[k].id == r->arg && k_choice_names[k].index == o->min + i) {
                put(names[i], 20, k_choice_names[k].text);
                found = 1;
            }
        }
        if (!found) {
            option_value_of(r->arg, o->min + i, names[i], 20);
        }
    }
    if (active != NULL) {
        *active = eidos_settings_get(r->arg) - o->min;
    }
    return count;
}

/* ------------------------------------------------------------------ */
/* Help text for the bottom screen */

int eidos_sui_help_count(void)
{
    return 4;
}

void eidos_sui_help_line(int i, char *out, int n)
{
    const char *l[4] = { "", "", "", "" };
    const Row *r = (g.nrows > 0 && !on_back()) ? &g.rows[g.cursor] : NULL;
    if (n > 0) {
        out[0] = '\0';
    }
    if (g.capturing) {
        l[0] = HELP_CAPTURE1;
        l[1] = HELP_CAPTURE2;
    } else if (on_back()) {
        l[0] = g.level == 0 ? HELP_BACK_L0 : HELP_BACK_L1;
        l[2] = g.level == 0 ? HELP_KEYS_L0 : HELP_KEYS_L1;
        l[3] = g.level == 0 ? HELP_KEYS_L0B : HELP_KEYS_L1B;
    } else if (g.level == 0 && r != NULL) {
        static const char *const sec[] = {
            HELP_SECTION_DISPLAY, HELP_SECTION_LAYOUT, HELP_SECTION_CONTROLS, HELP_SECTION_SOUND };
        l[0] = sec[r->arg];
        l[2] = HELP_KEYS_L0;
        l[3] = HELP_KEYS_L0B;
    } else if (r != NULL) {
        switch (r->type) {
        case ROW_OPTION:
            l[0] = k_option_help[r->arg][0] != NULL ? k_option_help[r->arg][0] : "";
            l[1] = k_option_help[r->arg][1] != NULL ? k_option_help[r->arg][1] : "";
            break;
        case ROW_BIND:
            l[0] = HELP_BIND1;
            l[1] = HELP_BIND2;
            break;
        default:
            l[0] = HELP_DEFAULTS1;
            l[1] = HELP_DEFAULTS2;
            break;
        }
        l[2] = HELP_KEYS_L1;
        l[3] = HELP_KEYS_L1B;
    }
    if (i >= 0 && i < 4) {
        put(out, n, l[i]);
    }
}
