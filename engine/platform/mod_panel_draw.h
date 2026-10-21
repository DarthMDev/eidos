/*
 * engine/platform/mod_panel.h -- the run-status panel: the port's own text
 * panel, drawn beside the DS screens (docs/mod-services.md, "Run-status
 * panel").
 *
 * What the panel is
 * -----------------
 *
 * A small text panel with sixteen lines, filled by a mod through the
 * services table (sdk/mod_panel.h): deaths, encounters by route, the level
 * cap, a timer.  The port draws it outside the game's screens, so it never
 * covers the game and is not part of a screenshot of the DS screens:
 *
 *   - in the stacked layout it is beside the two screens, on their right;
 *   - in the side-by-side layout it is below them.
 *
 * The player's choice
 * -------------------
 *
 * The player can hide the panel from the port's settings.  The setting's
 * own row is in engine/platform/settings.c, which is the port's settings
 * model and not this file's; the row's apply calls
 * eidos_mod_panel_set_visible() with the player's choice, and the drawing
 * asks eidos_mod_panel_visible() before it draws anything.
 *
 * How the panel is drawn
 * ----------------------
 *
 * The panel's picture is drawn on the CPU, into a buffer of the port's own
 * resolution, with the port's own bitmap font (engine/platform/font.h) --
 * the same font the first-run window uses.  The platform's presenter shows
 * that picture beside the two screens: the drawing here produces the
 * picture and the rectangle it goes to, and the platform's own path (the
 * render() in engine/platform/platform.c) puts it on the screen.
 *
 * The picture is redrawn only when the model's lines change, and the view
 * carries the number that says so (generation), so the platform does not
 * upload it again every frame.
 *
 * The model's lines are read from sdk/mod_panel.c.  That file's header is
 * not included here: the two files have the same name and the include path
 * carries both directories, so the calls the drawing needs are declared
 * below, the way engine/platform/mod_options_proc.c declares the ones it
 * needs.
 */

#ifndef EIDOS_ENGINE_MOD_PANEL_H
#define EIDOS_ENGINE_MOD_PANEL_H

#include "platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* The panel's own resolution                                          */
/* ------------------------------------------------------------------ */

/*
 * The panel's picture, in pixels: the font's cell is 6 x 10 pixels
 * (engine/platform/font.h), so a line is ten pixels tall and a character
 * six pixels wide.  The picture is a border, a margin of two pixels and
 * the sixteen lines of the panel, which is twenty characters a line.
 */
#define EIDOS_MOD_PANEL_W 128
#define EIDOS_MOD_PANEL_H 168

/* ------------------------------------------------------------------ */
/* The panel's place in the arrangement                                */
/* ------------------------------------------------------------------ */

/*
 * The panel's rectangle in the arrangement, in DS pixels, and how much the
 * arrangement grows for it.  The arrangement is the two screens and the
 * panel together (engine/platform/platform.h, EidosScreenPlacement): the
 * port's geometry places the screens and then adds the panel's rectangle
 * here, and the arrangement's own size grows by grow_w and grow_h.
 *
 * The rectangle is the panel's own picture at the arrangement's scale, so
 * the panel's text is as sharp as the game's own picture.
 */
typedef struct EidosModPanelSpace {
    EidosRect rect;    /* the panel's rectangle in the arrangement */
    int grow_w;        /* how much the arrangement grows in width */
    int grow_h;        /* ... and in height */
} EidosModPanelSpace;

/*
 * The panel's place, from the arrangement of the two screens: beside them
 * when they are stacked, below them when they are side by side.
 */
void eidos_mod_panel_space(const EidosScreenPlacement *pl,
                           EidosModPanelSpace *out);

/* ------------------------------------------------------------------ */
/* The panel's picture                                                 */
/* ------------------------------------------------------------------ */

/*
 * What the platform's presenter shows: the picture and the rectangle it
 * goes to, in output pixels.
 */
typedef struct EidosModPanelView {
    const u8 *rgba;    /* the picture: w x h pixels, RGBA (R, G, B, A) */
    int w, h;          /* the picture's size */
    EidosRect dst;     /* where it goes in the output, in output pixels */
    u32 generation;    /* grows when the picture changes */
} EidosModPanelView;

/*
 * The panel's picture and where it goes.
 *
 *   `vp`        the platform's viewport: the arrangement's area in the
 *               output is its content rectangle;
 *   `space`     the panel's place in the arrangement (eidos_mod_panel_space);
 *   `arr_w`     the arrangement's width in DS pixels, the screens and the
 *   `arr_h`     panel together: the rectangle is mapped through the
 *               viewport the way the screens' rectangles are.
 *
 * Returns 0 when there is a panel to show, and -1 when there is none: the
 * player hid it, or no mod has filled a line.
 */
int eidos_mod_panel_view(EidosModPanelView *view, const EidosViewport *vp,
                         const EidosModPanelSpace *space,
                         int arr_w, int arr_h);

/* ------------------------------------------------------------------ */
/* The player's choice                                                 */
/* ------------------------------------------------------------------ */

/* Whether the panel is shown.  It is shown until the player hides it. */
int  eidos_mod_panel_visible(void);
void eidos_mod_panel_set_visible(int visible);

#ifdef __cplusplus
}
#endif
#endif /* EIDOS_ENGINE_MOD_PANEL_H */
