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

static const char *const k_tab_names[] = { "Display", "Layout", "Controls", "Sound" };

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

#define STR_FIT         "Fit"
#define STR_REVERT      "Revert changes"
#define STR_DEFAULTS    "Restore defaults"
#define STR_PRESS_KEY   "Press a key"
#define STR_UNBOUND     "None"

/* ================================================================== */

enum { ROW_OPTION, ROW_BIND, ROW_REVERT, ROW_DEFAULTS };

typedef struct Row { int type; int arg; } Row;

#define MAX_ROWS 24
#define TAB_COUNT 4

static struct {
    int tab;
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
    add_row(&n, ROW_REVERT, 0);
    add_row(&n, ROW_DEFAULTS, 0);
    g.nrows = n;
}

static void clamp_scroll(void)
{
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

static void set_tab(int t)
{
    g.tab = (t + TAB_COUNT) % TAB_COUNT;
    g.cursor = 0;
    g.scroll = 0;
    build_rows();
}

static void move_cursor(int d)
{
    g.cursor = (g.cursor + d + g.nrows) % g.nrows;
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
        return EIDOS_SUI_EXIT | EIDOS_SUI_SFX_BACK;
    }
    if (pressed & 0x200u) {                           /* L */
        set_tab(g.tab - 1);
        return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_MOVE;
    }
    if (pressed & 0x100u) {                           /* R */
        set_tab(g.tab + 1);
        return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_MOVE;
    }
    if (act & 0x40u) {
        move_cursor(-1);
        return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_MOVE;
    }
    if (act & 0x80u) {
        move_cursor(1);
        return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_MOVE;
    }
    r = &g.rows[g.cursor];
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
        case ROW_REVERT:
            eidos_settings_revert();
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
    return 1 + n;
}

int eidos_sui_cursor_row(void)
{
    return 1 + g.cursor - g.scroll;
}

int eidos_sui_capturing(void)
{
    return g.capturing;
}

int eidos_sui_tab(void)
{
    return g.tab;
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

static void option_value(int id, char *out, int n)
{
    const EidosOption *o = eidos_settings_option(id);
    int v = eidos_settings_get(id);
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

static void join(char *acc, size_t cap, const char *s, int *any)
{
    if (*any) {
        strncat(acc, ", ", cap - strlen(acc) - 1);
    }
    strncat(acc, s, cap - strlen(acc) - 1);
    *any = 1;
}

/* The primary key and the primary pad control: what a capture rebinds. */
static void bind_value(int button, char *out, int n)
{
    char tmp[48];
    char acc[128];
    int slot, kind, index, any = 0;
    acc[0] = '\0';
    for (slot = 0; slot < EIDOS_BIND_SLOTS && !any; slot++) {
        int sc = eidos_settings_get_key(button, slot);
        if (sc != 0) {
            eidos_scancode_name(sc, tmp, sizeof tmp);
            join(acc, sizeof acc, tmp, &any);
        }
    }
    for (slot = 0; slot < EIDOS_BIND_SLOTS; slot++) {
        if (eidos_settings_get_pad(button, slot, &kind, &index) == 0 && kind >= 0) {
            eidos_pad_control_name(kind, index, tmp, sizeof tmp);
            join(acc, sizeof acc, tmp, &any);
            break;
        }
    }
    put(out, n, any ? acc : STR_UNBOUND);
}

void eidos_sui_row_text(int row, char *label, int nl, char *value, int nv)
{
    const Row *r;
    if (nl > 0) { label[0] = '\0'; }
    if (nv > 0) { value[0] = '\0'; }
    if (row == 0) {
        char t[16];
        put(label, nl, k_tab_names[g.tab]);
        snprintf(t, sizeof t, "%d/%d", g.tab + 1, TAB_COUNT);
        put(value, nv, t);
        return;
    }
    if (row < 1 || g.scroll + row - 1 >= g.nrows) {
        return;
    }
    r = &g.rows[g.scroll + row - 1];
    switch (r->type) {
    case ROW_OPTION:
        put(label, nl, k_option_labels[r->arg] != NULL ? k_option_labels[r->arg] : "");
        option_value(r->arg, value, nv);
        break;
    case ROW_BIND: {
        const EidosButtonInfo *b = eidos_settings_button(r->arg);
        put(label, nl, b != NULL ? b->label : "");
        if (g.capturing == r->arg + 1) {
            put(value, nv, STR_PRESS_KEY);
        } else {
            bind_value(r->arg, value, nv);
        }
        break;
    }
    case ROW_REVERT:
        put(label, nl, STR_REVERT);
        break;
    default:
        put(label, nl, STR_DEFAULTS);
        break;
    }
}
