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
 * port's own drawing (engine/platform/mod_panel_draw.c) reads the lines through
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
 *   - The mod's lines and the port's own lines are kept apart.  The mod's
 *     lines are the mod's and the port's lines are the port's: the port's
 *     own line is never drawn on a line the mod has used, so a mod's own
 *     line is never overwritten, and the mod's own line never lands on
 *     the port's own line either.  The port's own lines are the timer
 *     line and the split lines (sdk/mod_timer.c).
 *   - The picture is marked as changed only when a line really changed,
 *     so the drawing is done again only then.
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
 * One mod's panel: its namespace (the key), its sixteen lines and the
 * port's own lines.
 *
 * The lines are kept as text, not as pointers: the port keeps them, and a mod
 * that goes away leaves its own panel behind until the reset.
 *
 * The port's own lines (the timer line, the split lines) are kept apart
 * from the mod's lines, in their own array: the mod's lines are the mod's
 * and the port's lines are the port's, and neither ever overwrites the
 * other.  Each of the port's own lines is drawn on a line of the panel the
 * mod has not used, and the model keeps which line that is, so the port's
 * own lines hold their place while the mod's own lines come and go.
 */
typedef struct BwModPanelState {
    char ns[BW_MOD_PANEL_NS_MAX + 1];
    int  used;
    char line[BW_MOD_PANEL_LINES][BW_MOD_PANEL_TEXT_MAX + 1];

    /* The port's own lines: the timer line and the split lines. */
    char port[BW_MOD_PANEL_LINES][BW_MOD_PANEL_TEXT_MAX + 1];
    int  port_slot[BW_MOD_PANEL_LINES];  /* the line each one is drawn on */
    u32  port_count;                     /* how many of them there are    */
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
 * A counter that grows every time a panel changes.  The drawing uses it to
 * know when its picture is out of date (engine/platform/mod_panel_draw.c).
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

/*
 * Write a text into a line and say whether the line really changed.  A
 * text that is empty (or no text at all) clears the line.
 *
 * The change is what the drawing's counter counts, so a line that is
 * written again with the same text does not make the picture stale.
 */
static int put_text(char *dst, size_t size, const char *text)
{
    char tmp[BW_MOD_PANEL_TEXT_MAX + 1];
    size_t n;
    int changed;

    copy_text(tmp, sizeof(tmp), text);
    n = strlen(tmp);
    if (n >= size) {
        n = size - 1;
        tmp[n] = '\0';
    }
    changed = (strcmp(dst, tmp) != 0);
    if (changed) {
        memcpy(dst, tmp, n + 1);
    }
    return changed;
}

/* The namespace of the mod that is calling, or "" when there is none (the
 * port itself is calling). */
static const char *current_namespace(void)
{
    const char *ns = bw_mod_savedata_namespace();

    return (ns != NULL) ? ns : "";
}

/* A panel with nothing in it: the port's own lines are not on any line. */
static void panel_init(BwModPanelState *p)
{
    u32 i;

    memset(p, 0, sizeof(*p));
    for (i = 0; i < BW_MOD_PANEL_LINES; i++) {
        p->port_slot[i] = -1;
    }
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
        panel_init(&g_panels[i]);
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

/*
 * The port's own line that is drawn on the line `line` of the panel, or
 * -1 when there is none.
 */
static int port_line_at(const BwModPanelState *p, u32 line)
{
    u32 i;

    for (i = 0; i < p->port_count; i++) {
        if (p->port_slot[i] == (int)line) {
            return (int)i;
        }
    }
    return -1;
}

/*
 * A line of the panel that neither the mod's own lines nor the port's own
 * lines use, or -1 when the panel has none.
 */
static int free_line(const BwModPanelState *p)
{
    u32 line;

    for (line = 0; line < BW_MOD_PANEL_LINES; line++) {
        if (p->line[line][0] != '\0') {
            continue;               /* the mod's own line */
        }
        if (port_line_at(p, line) >= 0) {
            continue;               /* the port's own line */
        }
        return (int)line;
    }
    return -1;
}

/*
 * The line the port's own line `index` is drawn on: the one it is already
 * on when the mod has not used it, a free one otherwise, and -1 when the
 * panel has no room for it.
 */
static int place_port_line(BwModPanelState *p, u32 index)
{
    int line;

    if (index >= BW_MOD_PANEL_LINES) {
        return -1;
    }
    line = p->port_slot[index];
    if (line >= 0 && (u32)line < BW_MOD_PANEL_LINES &&
        p->line[line][0] == '\0') {
        return line;                /* the place it held is still free */
    }
    line = free_line(p);
    if (line < 0) {
        return -1;                  /* every line is the mod's */
    }
    p->port_slot[index] = line;
    return line;
}

/*
 * The text of one line of the drawn panel: the mod's own line when the
 * mod uses that line, the port's own line when the port's own line is
 * drawn there, and "" otherwise.
 */
static const char *drawn_line(const BwModPanelState *p, u32 line)
{
    int port;

    if (p == NULL || line >= BW_MOD_PANEL_LINES) {
        return "";
    }
    if (p->line[line][0] != '\0') {
        return p->line[line];
    }
    port = port_line_at(p, line);
    if (port >= 0) {
        return p->port[port];
    }
    return "";
}

/* How many lines of a drawn panel carry a text. */
static u32 drawn_used(const BwModPanelState *p)
{
    u32 line, n = 0;

    if (p == NULL) {
        return 0;
    }
    for (line = 0; line < BW_MOD_PANEL_LINES; line++) {
        if (drawn_line(p, line)[0] != '\0') {
            n++;
        }
    }
    return n;
}

/*
 * The panel the port draws: the one the port named, or the first panel
 * that has a line in it.  NULL when there is none.
 *
 * The port's own panel (the port's own namespace, the empty one) is the
 * last one considered: the panels the mods fill are what the player sees,
 * and the port's own panel, which holds the port's own run, is drawn when
 * there is no other panel with a line in it.  The port names the panel it
 * wants (bw_mod_panel_select) when it wants its own.
 */
static BwModPanelState *shown_state(void)
{
    u32 i;

    if (g_shown_set) {
        return state_of(g_shown);
    }
    for (i = 0; i < BW_MOD_PANEL_MAX; i++) {
        if (!g_panels[i].used ||
            strcmp(g_panels[i].ns, BW_MOD_PANEL_PORT_NS) == 0) {
            continue;               /* the port's own panel comes last */
        }
        if (drawn_used(&g_panels[i]) > 0) {
            return &g_panels[i];
        }
    }
    for (i = 0; i < BW_MOD_PANEL_MAX; i++) {
        if (g_panels[i].used && drawn_used(&g_panels[i]) > 0) {
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
    if (put_text(p->line[line], sizeof(p->line[line]), text)) {
        g_generation++;
    }
}

/* The `panel_clear` call: every line of the calling mod's panel. */
void bw_mod_panel_clear(void)
{
    BwModPanelState *p = state_for(current_namespace());
    u32 i;
    int changed = 0;

    if (p == NULL) {
        return;
    }
    for (i = 0; i < BW_MOD_PANEL_LINES; i++) {
        if (put_text(p->line[i], sizeof(p->line[i]), "")) {
            changed = 1;
        }
    }
    if (changed) {
        g_generation++;
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
    if (put_text(p->line[line], sizeof(p->line[line]), text)) {
        g_generation++;
    }
}

/*
 * Every line of a named mod's panel: the mod's own lines and the port's
 * own lines on it.
 */
void bw_mod_panel_clear_for(const char *ns)
{
    BwModPanelState *p = state_for(ns);
    u32 i;
    int changed = 0;

    if (p == NULL) {
        return;
    }
    for (i = 0; i < BW_MOD_PANEL_LINES; i++) {
        if (put_text(p->line[i], sizeof(p->line[i]), "")) {
            changed = 1;
        }
        if (put_text(p->port[i], sizeof(p->port[i]), "")) {
            changed = 1;
        }
        p->port_slot[i] = -1;
    }
    p->port_count = 0;
    if (changed) {
        g_generation++;
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
    u32 i;

    for (i = 0; i < BW_MOD_PANEL_MAX; i++) {
        panel_init(&g_panels[i]);
    }
    g_shown[0] = '\0';
    g_shown_set = 0;
    g_generation++;
}

/*
 * The port's own line of a named mod's panel: the timer line and the
 * split lines (sdk/mod_timer.c).
 *
 * The line is drawn on a line of the panel the mod has not used, so a
 * mod's own line is never overwritten.  The model keeps the line it is
 * drawn on, so the port's own lines hold their place while the mod's own
 * lines come and go.
 *
 * Returns the line the port's own line is drawn on, or -1 when the panel
 * has no room for it or when the line was cleared.
 */
int bw_mod_panel_set_port(const char *ns, u32 index, const char *text)
{
    BwModPanelState *p = state_for(ns);
    int old_slot;
    int line;
    int changed = 0;

    if (p == NULL || index >= BW_MOD_PANEL_LINES) {
        return -1;
    }
    if (text == NULL || text[0] == '\0') {
        /* The port's own line is cleared: the line it was drawn on is
         * free again for the mod's own lines. */
        if (put_text(p->port[index], sizeof(p->port[index]), "")) {
            changed = 1;
        }
        if (p->port_slot[index] != -1) {
            p->port_slot[index] = -1;
            changed = 1;
        }
        if (changed) {
            g_generation++;
        }
        return -1;
    }
    old_slot = p->port_slot[index];
    line = place_port_line(p, index);
    if (line < 0) {
        return -1;                  /* every line of the panel is the mod's */
    }
    if (put_text(p->port[index], sizeof(p->port[index]), text)) {
        changed = 1;
    }
    if (old_slot != p->port_slot[index]) {
        changed = 1;                /* the line it is drawn on moved */
    }
    if (index + 1u > p->port_count) {
        p->port_count = index + 1u;
    }
    if (changed) {
        g_generation++;
    }
    return line;
}

/* How many of the port's own lines the panel holds. */
u32 bw_mod_panel_port_count(const char *ns)
{
    BwModPanelState *p = state_of(ns);

    return (p == NULL) ? 0u : p->port_count;
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
    return drawn_line(shown_state(), line);
}

u32 bw_mod_panel_shown_used(void)
{
    return drawn_used(shown_state());
}

u32 bw_mod_panel_generation(void)
{
    return g_generation;
}

const char *bw_mod_panel_line_for(const char *ns, u32 line)
{
    return drawn_line(state_of(ns), line);
}

u32 bw_mod_panel_used_for(const char *ns)
{
    return drawn_used(state_of(ns));
}
