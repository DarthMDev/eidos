/*
 * sdk/mod_title.h -- state shared by the title menu hooks
 * (stubs/ov/reimpl_ov088_*.c, hook name "title_settings", docs/hooks.md).
 *
 * The menu proc of overlay 88 keeps its rows in an 8 byte list and has
 * exactly eight row kinds, all with a window and a row buffer of their own
 * (docs/settings-design.md).  The hook gives the last kind, 7, to the
 * SETTINGS row while the game's own use of it (the conditional row whose
 * test is func_0200C308(save, 3)) is off.
 */
#ifndef BW_PORT_MOD_TITLE_H
#define BW_PORT_MOD_TITLE_H

#define BW_HOOK_TITLE_SETTINGS "title_settings"

/* Row-list value of the SETTINGS row, and the proc selection (+0x129) the
 * exit function maps to the settings proc. */
#define BW_TITLE_ROW_SETTINGS 7
#define BW_TITLE_SEL_SETTINGS 9

#ifdef __cplusplus
extern "C" {
#endif

/* 1 when the hook is on. */
int  bw_mod_title_settings_on(void);

/* 1 while the settings screen (placeholder) is up: the menu proc is then
 * restarted showing only the SETTINGS row. */
int  bw_mod_title_in_settings(void);
int  bw_mod_title_set_in_settings(int on);

#ifdef __cplusplus
}
#endif
#endif
