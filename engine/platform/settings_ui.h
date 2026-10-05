/*
 * engine/platform/settings_ui.h -- the settings screen's model: tabs, rows,
 * navigation and text.  No drawing and no game calls: the screen itself
 * (stubs/ov/reimpl_ov088_*.c, docs/settings-design.md) asks this module what
 * to show and tells it which buttons were pressed.
 *
 * Layout of the screen: two levels, like the game's own Options screen.
 * Level 0 lists the sections (Display, Layout, Controls, Sound); A enters one.
 * Level 1 shows the section name as the header and up to EIDOS_SUI_VISIBLE
 * option rows (more rows scroll); B returns to level 0 with the cursor on
 * that section.  Every section ends with two action rows, "Revert changes"
 * and "Restore defaults".  B at level 0 leaves the screen.  Leaving
 * the screen commits (eidos_sui_leave); Revert undoes everything since the
 * screen was opened or last committed.
 *
 * Text is plain ASCII.  All of it is in one table in settings_ui.c
 * ("UI TEXT"), so a translation or a mod replaces that table.
 */
#ifndef EIDOS_ENGINE_SETTINGS_UI_H
#define EIDOS_ENGINE_SETTINGS_UI_H

#ifdef __cplusplus
extern "C" {
#endif

#define EIDOS_SUI_VISIBLE 5   /* option rows shown; the grey bar is a sixth */

/* Result flags of eidos_sui_input. */
#define EIDOS_SUI_REDRAW      0x01   /* rebuild the rows                      */
#define EIDOS_SUI_EXIT        0x02   /* the player left the screen            */
#define EIDOS_SUI_SFX_MOVE    0x10
#define EIDOS_SUI_SFX_SELECT  0x20
#define EIDOS_SUI_SFX_BACK    0x40

/* Button bits as the game reads them (A 1, B 2, right 0x10, left 0x20, up
 * 0x40, down 0x80, R 0x100, L 0x200). */

void eidos_sui_enter(void);     /* level 0, first row, nothing capturing    */
void eidos_sui_leave(void);     /* commit; cancels a capture                  */

/* One frame.  `pressed` are the buttons that went down this frame, `held`
 * the buttons down now (used for repeat).  Returns EIDOS_SUI_* flags. */
int eidos_sui_input(unsigned pressed, unsigned held);

/* What to draw.  Row 0 is the header; rows 1..eidos_sui_row_count()-2 are the
 * visible option rows and the last row, eidos_sui_row_count()-1, is the grey
 * bar ("Done" on the list of sections, "Back" inside one).  `label` and `value` are ASCII; value may be "". */
int  eidos_sui_row_count(void);
void eidos_sui_row_text(int row, char *label, int nl, char *value, int nv);
/* A row that is an option with two or three named choices (window mode, screen
 * layout, swap): how many, which is active (0..count-1) and each name.  The
 * game's Options screen shows all the choices side by side and marks the
 * active one; 0 means draw label and value instead. */
int  eidos_sui_row_choices(int row, int *active, char names[3][20]);
/* The highlighted row, 1..eidos_sui_row_count()-1. */
int  eidos_sui_cursor_row(void);
int  eidos_sui_row_adjustable(int row);     /* left and right change its value */

/* The stylus: see eidos_sui_touch in settings_ui.c. */
#define EIDOS_SUI_ZONE_ROW (-1)
#define EIDOS_SUI_ZONE_DEC (-2)
#define EIDOS_SUI_ZONE_INC (-3)
int  eidos_sui_touch(int row, int zone);
/* Non-zero while waiting for a key or pad control. */
int  eidos_sui_capturing(void);

/* Help text for the bottom screen: eidos_sui_help_count() lines for the
 * highlighted row (a line may be ""). */
int  eidos_sui_help_count(void);
void eidos_sui_help_line(int i, char *out, int n);

/* Test hooks.  eidos_sui_tab is the section index, -1 at level 0. */
int  eidos_sui_level(void);
int  eidos_sui_tab(void);
int  eidos_sui_tab_count(void);

#ifdef __cplusplus
}
#endif
#endif
