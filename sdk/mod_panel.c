/*
 * sdk/mod_panel.c -- the run-status panel: the model's sixteen lines and
 * the two calls of the services table (docs/mod-services.md, "Run-status
 * panel").
 *
 * What this file is
 * -----------------
 *
 * The model behind the small text panel the port draws beside the DS
 * screens: sixteen lines, each a string the port keeps and draws.  A mod
 * writes a line with panel_set and empties the panel with panel_clear; the
 * port's own drawing (engine/platform/mod_panel.c) reads the lines through
 * the calls at the bottom of sdk/mod_panel.h.
 *
 * The rules the model keeps
 * -------------------------
 *
 *   - The lines are 0 to 15.  A line out of that range is ignored: a mod
 *     that asks for a line the panel does not have gets nothing rather
 *     than a crash.
 *   - A line set empty is cleared.  A line set with a text is that text,
 *     cut at the length the model keeps.
 *   - One panel per mod.  The loader binds the same two functions into
 *     every mod's own table and puts the mod's namespace in force around
 *     every call it makes into a mod, so a service that keeps state keeps
 *     it per mod: two mods that both fill the panel do not share one.
 *   - The port draws one panel: the one bw_mod_panel_select() names, or
 *     the first one that has a line in it when the port has named none.
 *
 * The file
 * --------
 *
 * The port's own calls (the ones below the two services-table calls) are
 * not part of the mod ABI: a mod reaches the panel through the services
 * table (sdk/mod_state.h), and the loader binds the two calls above into
 * it.
 */

#include <stddef.h>
#include <string.h>

#include "types.h"
#include "mod_panel.h"

/* ------------------------------------------------------------------ */
/* What the other layers provide                                       */
/* ------------------------------------------------------------------ */

/*
 * The mod that is calling: the loader sets the namespace around every call
 * it makes into a mod, so a service call always knows whose it is (the
 * loader says the same about the saved-data layer, sdk/mod_loader.c).
 * The saved-data layer has no header yet, so the prototype is here.
 */
extern const char *bw_mod_savedata_namespace(void);

/* ------------------------------------------------------------------ */
/* The limits                                                          */
/* ------------------------------------------------------------------ */

/* How many mods may hold a panel at once.  The loader holds sixteen mods
 * with code, so sixteen panels are enough. */
#define BW_MOD_PANEL_MAX 16

/* The longest namespace a panel may carry (the mod's own name). */
#define BW_MOD_PANEL_NS_MAX 96

/* ------------------------------------------------------------------ */
/* The state                                                           */
/* ------------------------------------------------------------------ */

/*
 * One mod's panel: its namespace (the key) and its sixteen lines.  The
 * lines are kept as text, not as pointers: the port keeps them, and a mod
 * that goes away leaves its own panel behind until the reset.
 */
typedef struct BwModPanelState {
    char ns[BW_MOD_PANEL_NS_MAX + 1];
    int  used;
    char line[BW_MOD_PANEL_LINES][BW_MOD_PANEL_TEXT_MAX + 1];
} BwModPanelState;

static BwModPanelState g_panels[BW_MOD_PANEL_MAX];

/*
 * The panel the port draws: the namespace the port named, and whether it
 * named one at all.  With no name, the first panel that has a line in it
 * is drawn (see bw_mod_panel_shown_state).
 */
static char g_shown[BW_MOD_PANEL_NS_MAX + 1];
static int  g_shown_set;

/*
 * A counter that grows every time a panel changes.  The drawing uses it
 * to know when its picture is out of date (engine/platform/mod_panel.c).
 */
static u32 g_generation;

/* ------------------------------------------------------------------ */
/* The small helpers                                                   */
/* ------------------------------------------------------------------ */

/*
 * Copy `src` into `dst`, cutting it at the buffer's end.  The cut goes
 * back to the last byte that starts a UTF-8 sequence, so a text is never
 * left with half a character at its end.
 */
static void copy_text(char *dst, size_t size, const char *src)
{
    size_t n;

    if (size == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    n = strlen(src);
    if (n >= size) {
        n = size - 1;
        while (n > 0 && ((unsigned char)src[n] & 0xC0u) == 0x80u) {
            n--;
        }
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/* The namespace of the mod that is calling, or "" when there is none (the
 * port itself is calling). */
static const char *current_namespace(void)
{
    const char *ns = bw_mod_savedata_namespace();

    return (ns != NULL) ? ns : "";
}

/*
 * The panel of the mod named by `ns`, created when it is not there yet.
 * NULL when the table is full.
 */
static BwModPanelState *state_for(const char *ns)
{
    u32 i;

    if (ns == NULL) {
        ns = "";
    }
    for (i = 0; i < BW_MOD_PANEL_MAX; i++) {
        if (g_panels[i].used && strcmp(g_panels[i].ns, ns) == 0) {
            return &g_panels[i];
        }
    }
    for (i = 0; i < BW_MOD_PANEL_MAX; i++) {
        if (g_panels[i].used) {
            continue;
        }
        memset(&g_panels[i], 0, sizeof(g_panels[i]));
        copy_text(g_panels[i].ns, sizeof(g_panels[i].ns), ns);
        g_panels[i].used = 1;
        return &g_panels[i];
    }
    return NULL;
}

/* The panel of the mod named by `ns`, or NULL when it has none yet. */
static BwModPanelState *state_of(const char *ns)
{
    u32 i;

    if (ns == NULL) {
        ns = "";
    }
    for (i = 0; i < BW_MOD_PANEL_MAX; i++) {
        if (g_panels[i].used && strcmp(g_panels[i].ns, ns) == 0) {
            return &g_panels[i];
        }
    }
    return NULL;
}

/* How many lines of a panel carry a text. */
static u32 used_lines(const BwModPanelState *p)
{
    u32 i, n = 0;

    if (p == NULL) {
        return 0;
    }
    for (i = 0; i < BW_MOD_PANEL_LINES; i++) {
        if (p->line[i][0] != '\0') {
            n++;
        }
    }
    return n;
}

/*
 * The panel the port draws: the one the port named, or the first panel
 * that has a line in it.  NULL when there is none.
 */
static BwModPanelState *shown_state(void)
{
    u32 i;

    if (g_shown_set) {
        return state_of(g_shown);
    }
    for (i = 0; i < BW_MOD_PANEL_MAX; i++) {
        if (g_panels[i].used && used_lines(&g_panels[i]) > 0) {
            return &g_panels[i];
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* The services table's calls                                          */
/* ------------------------------------------------------------------ */

/* The `panel_set` call: one line of the calling mod's panel. */
void bw_mod_panel_set(u32 line, const char *text)
{
    BwModPanelState *p = state_for(current_namespace());

    if (p == NULL || line >= BW_MOD_PANEL_LINES) {
        return;
    }
    g_generation++;
    if (text == NULL || text[0] == '\0') {
        p->line[line][0] = '\0';       /* a line set empty is cleared */
        return;
    }
    copy_text(p->line[line], sizeof(p->line[line]), text);
}

/* The `panel_clear` call: every line of the calling mod's panel. */
void bw_mod_panel_clear(void)
{
    BwModPanelState *p = state_for(current_namespace());
    u32 i;

    if (p == NULL) {
        return;
    }
    g_generation++;
    for (i = 0; i < BW_MOD_PANEL_LINES; i++) {
        p->line[i][0] = '\0';
    }
}

/* ------------------------------------------------------------------ */
/* The port's own calls                                                */
/* ------------------------------------------------------------------ */

/* One line of a named mod's panel: the port's own call, for the splits. */
void bw_mod_panel_set_for(const char *ns, u32 line, const char *text)
{
    BwModPanelState *p = state_for(ns);

    if (p == NULL || line >= BW_MOD_PANEL_LINES) {
        return;
    }
    g_generation++;
    if (text == NULL || text[0] == '\0') {
        p->line[line][0] = '\0';
        return;
    }
    copy_text(p->line[line], sizeof(p->line[line]), text);
}

/* Every line of a named mod's panel. */
void bw_mod_panel_clear_for(const char *ns)
{
    BwModPanelState *p = state_for(ns);
    u32 i;

    if (p == NULL) {
        return;
    }
    g_generation++;
    for (i = 0; i < BW_MOD_PANEL_LINES; i++) {
        p->line[i][0] = '\0';
    }
}

/* The panel the port draws: the one of the mod named by `ns`. */
int bw_mod_panel_select(const char *ns)
{
    g_generation++;
    if (ns == NULL) {
        g_shown_set = 0;
        g_shown[0] = '\0';
        return 0;
    }
    copy_text(g_shown, sizeof(g_shown), ns);
    g_shown_set = 1;
    return 0;
}

/* Forget every panel and the choice: at power-on and at every reset. */
void bw_mod_panel_reset(void)
{
    memset(g_panels, 0, sizeof(g_panels));
    g_shown[0] = '\0';
    g_shown_set = 0;
    g_generation++;
}

/* ------------------------------------------------------------------ */
/* What the drawing reads                                              */
/* ------------------------------------------------------------------ */

u32 bw_mod_panel_line_count(void)
{
    return BW_MOD_PANEL_LINES;
}

const char *bw_mod_panel_shown(void)
{
    BwModPanelState *p = shown_state();

    return (p != NULL) ? p->ns : "";
}

const char *bw_mod_panel_shown_line(u32 line)
{
    BwModPanelState *p = shown_state();

    if (p == NULL || line >= BW_MOD_PANEL_LINES) {
        return "";
    }
    return p->line[line];
}

u32 bw_mod_panel_shown_used(void)
{
    return used_lines(shown_state());
}

u32 bw_mod_panel_generation(void)
{
    return g_generation;
}

const char *bw_mod_panel_line_for(const char *ns, u32 line)
{
    BwModPanelState *p = state_of(ns);

    if (p == NULL || line >= BW_MOD_PANEL_LINES) {
        return "";
    }
    return p->line[line];
}

u32 bw_mod_panel_used_for(const char *ns)
{
    return used_lines(state_of(ns));
}
