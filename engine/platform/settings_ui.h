/*
 * engine/platform/settings_ui.h -- the settings screen's model: tabs, rows,
 * navigation and text.  No drawing and no game calls: the screen itself
 * (stubs/ov/reimpl_ov088_*.c, docs/settings-design.md) asks this module what
 * to show and tells it which buttons were pressed.
 *
 * Layout of the screen: a header row (tab name, page number) and up to
 * EIDOS_SUI_VISIBLE option rows.  A tab with more rows scrolls.  Every tab
 * ends with two action rows, "Revert changes" and "Restore defaults".  Leaving
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

#define EIDOS_SUI_VISIBLE 6

/* Result flags of eidos_sui_input. */
#define EIDOS_SUI_REDRAW      0x01   /* rebuild the rows                      */
#define EIDOS_SUI_EXIT        0x02   /* the player left the screen            */
#define EIDOS_SUI_SFX_MOVE    0x10
#define EIDOS_SUI_SFX_SELECT  0x20
#define EIDOS_SUI_SFX_BACK    0x40

/* Button bits as the game reads them (A 1, B 2, right 0x10, left 0x20, up
 * 0x40, down 0x80, R 0x100, L 0x200). */

void eidos_sui_enter(void);     /* first tab, first row, nothing capturing    */
void eidos_sui_leave(void);     /* commit; cancels a capture                  */

/* One frame.  `pressed` are the buttons that went down this frame, `held`
 * the buttons down now (used for repeat).  Returns EIDOS_SUI_* flags. */
int eidos_sui_input(unsigned pressed, unsigned held);

/* What to draw.  Row 0 is the header; rows 1..eidos_sui_row_count()-1 are the
 * visible option rows.  `label` and `value` are ASCII; value may be "". */
int  eidos_sui_row_count(void);
void eidos_sui_row_text(int row, char *label, int nl, char *value, int nv);
/* The highlighted row, 1..eidos_sui_row_count()-1. */
int  eidos_sui_cursor_row(void);
/* Non-zero while waiting for a key or pad control. */
int  eidos_sui_capturing(void);

/* Test hooks. */
int  eidos_sui_tab(void);
int  eidos_sui_tab_count(void);

#ifdef __cplusplus
}
#endif
#endif
