/*
 * sdk/mod_panel.h -- the run-status panel: the model's sixteen lines and
 * the two calls of the services table (docs/mod-services.md, "Run-status
 * panel").
 *
 * The panel is a small text panel beside the DS screens, filled by the mod:
 * deaths, encounters by route, the level cap, a timer.  It is drawn by the
 * port outside the game's screens (to the side in the stacked layout, below
 * in the side-by-side one), so it never covers the game and is not part of
 * a screenshot of the DS screens, and the player can hide it from the port's
 * settings.
 *
 * The model is here; the drawing is engine/platform/mod_panel.c, and it
 * reads the lines through the calls at the bottom of this header.  The two
 * services-table calls are bound by the loader (sdk/mod_loader.c) into every
 * mod's own table.
 *
 * One panel per mod
 * -----------------
 *
 * The services table is filled per mod and the loader binds the same
 * functions into every one of them, so the state cannot be one set of
 * variables: two mods that both fill the panel would share it, and one
 * mod's lines would appear in the other's panel.  The port says which mod
 * is calling the way it says so everywhere else: the loader puts the mod's
 * namespace in force around every call it makes into a mod
 * (loader_enter_mod), and the namespace is the mod's own name.  This file
 * keys its panels on it, the way sdk/mod_timer.c keys its timers.
 *
 * The panel the port draws
 * ------------------------
 *
 * There is one panel on the screen, so the port names the one it draws:
 * bw_mod_panel_select() chooses it, and the port's own calls (the splits,
 * which belong to the mod whose run they are) write into the panel of the
 * mod they name.  With no choice made, the port draws the first panel that
 * has a line in it, which is the panel of the only mod that uses the
 * service: the common case needs nothing from the port.
 *
 * The port's own calls (the ones below the two services-table calls) are
 * not part of the mod ABI: a mod reaches the panel through the services
 * table (sdk/mod_state.h), and the loader binds the two calls above into
 * it.
 */

#ifndef BW_PORT_MOD_PANEL_H
#define BW_PORT_MOD_PANEL_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* The panel's shape                                                   */
/* ------------------------------------------------------------------ */

/* How many lines a panel has.  The services table's panel_set takes a line
 * from 0 to this many (docs/mod-services.md: "lines 0 to 15"), and the
 * panel's own picture is as tall as these lines. */
#define BW_MOD_PANEL_LINES 16u

/* The longest text one line carries.  A longer text is cut here; the
 * picture cuts it again to what fits its width. */
#define BW_MOD_PANEL_TEXT_MAX 64u

/* ------------------------------------------------------------------ */
/* The services table's calls                                          */
/* ------------------------------------------------------------------ */

/*
 * The `panel_set` call: write one line of the panel.  The lines are 0 to
 * BW_MOD_PANEL_LINES - 1; a line out of that range is ignored, so a mod
 * that asks for a line the panel does not have gets nothing rather than
 * a crash.  A text that is empty (or no text at all) clears the line, and
 * a text that is longer than BW_MOD_PANEL_TEXT_MAX is cut there.
 *
 * The line belongs to the mod that is calling: the loader says which mod
 * that is (see the note above).
 */
void bw_mod_panel_set(u32 line, const char *text);

/*
 * The `panel_clear` call: empty every line of the calling mod's panel.
 */
void bw_mod_panel_clear(void);

/* ------------------------------------------------------------------ */
/* The port's own calls                                                */
/* ------------------------------------------------------------------ */

/*
 * These name a mod by its namespace, the way the timer service's own calls
 * do (sdk/mod_timer.h): the port writes into a mod's panel (the splits,
 * which belong to the mod whose run they are) and clears one.
 */
void bw_mod_panel_set_for(const char *ns, u32 line, const char *text);
void bw_mod_panel_clear_for(const char *ns);

/*
 * The panel the port draws: the one of the mod named by `ns`.  A NULL name
 * drops the choice, and the port draws the first panel that has a line in
 * it again.  Returns 0.
 */
int bw_mod_panel_select(const char *ns);

/*
 * Forget every panel and the choice: at power-on and at every reset
 * (bw_port_reset, beside bw_mod_timer_reset).
 */
void bw_mod_panel_reset(void);

/* ------------------------------------------------------------------ */
/* What the drawing reads                                              */
/* ------------------------------------------------------------------ */

/* How many lines a panel has: the services table's panel_set takes a line
 * from 0 to this many. */
u32 bw_mod_panel_line_count(void);
/* The namespace of the panel the port draws, "" when there is none. */
const char *bw_mod_panel_shown(void);

/* One line of the drawn panel: the text it carries, "" for a line that is
 * empty.  The text points into the model's own storage and stays valid
 * until the next call that changes the panel. */
const char *bw_mod_panel_shown_line(u32 line);

/* How many lines of the drawn panel carry a text. */
u32 bw_mod_panel_shown_used(void);

/*
 * A counter that grows every time a panel changes: the drawing uses it to
 * know when its picture is out of date, so the picture is drawn again only
 * when a line really changed.
 */
u32 bw_mod_panel_generation(void);

/* The same two answers for a named mod's panel. */
const char *bw_mod_panel_line_for(const char *ns, u32 line);
u32         bw_mod_panel_used_for(const char *ns);

#ifdef __cplusplus
}
#endif
#endif /* BW_PORT_MOD_PANEL_H */
