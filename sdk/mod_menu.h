/*
 * sdk/mod_menu.h -- the mod menu's model: the installed mods, the switch,
 * the clear action, the orphan report and a mod's own settings.
 *
 * What this file is
 * -----------------
 *
 * The model behind the port's own Mods section (docs/mod-services.md, "The
 * mod menu").  It answers the screen's questions -- how many mods are
 * installed, what the i-th one is called, whether it is on, what its
 * settings are, and which of the saved-data folders on the disk belong to a
 * mod that is no longer installed -- and it holds the state behind those
 * answers.  It draws nothing: the screen (engine/platform/mod_options_proc.c
 * and the screen's own model, engine/platform/settings_ui.c) draws what this
 * file answers.
 *
 * The rules the model lives by
 * ----------------------------
 *
 * The four constraints of docs/mod-rules.md, section 2, as docs/mod-state.md,
 * section 2 ("The in-game management constraints") states them:
 *
 *   1. The roots are rebuildable at the menu, not installed once at
 *      start-up.  bw_set_mod_root, bw_add_mod_root, bw_mod_set_order and
 *      bw_clear_mods set them, and every change drops the cached merged
 *      archives (bw_vfs_flush).  The state namespace is per mod, so a root
 *      rebuild moves no state.  Enabling a mod adds its root, and the load
 *      point reads its state.
 *   2. A code mod is disabled in order: the mod's state is flushed, then
 *      every (region, address) pair it installed is reverted to its
 *      original, and only then is its library unloaded.  The model drives
 *      that order; the loader (sdk/mod_loader.c) does the reverting and the
 *      unloading.
 *   3. A disabled mod's effects stay: its state file stays on the disk and
 *      the game bytes it wrote stay until the game writes over them.  The
 *      port restores nothing, and turning the mod on again reads its state
 *      again.
 *   4. The port starts with no mods: with no mod root there is no state
 *      read, the state folder is untouched, and BW_MOD_ROOT is read once.
 *      The model reads the port's list; it writes the list only when the
 *      player applies the menu, and at start-up only to restore the list
 *      the player chose last time (bw_mod_menu_start).
 *
 * What the model holds
 * --------------------
 *
 * The port's own record of the player's list is <save dir>/mods.ini: the
 * mods the player knows about, the state of their switches and the order
 * the player chose for them.  It is the port's file, written the way every
 * other port file is written (a whole file, atomically: sdk/save.h).  With
 * no record and no mod root the port starts with no mods, which is
 * constraint 4; with a record the player has already chosen, and the list
 * is restored -- the switches and the order.
 *
 * The order the player chose is the player's own, because the order the
 * mods load in is the order their overrides and their rules take effect
 * (docs/mod-services.md, "The mod menu": "The order can be changed").  The
 * manifest's `order` key is the default: it decides the list while the
 * player has not said otherwise.  The model holds the list in the order
 * the player chose, the apply hands that order to the file layer
 * (bw_mod_set_order, sdk/vfs.h) and the loader loads the mods in it.  The
 * state namespace is per mod, so moving a mod in the list moves no state.
 *
 * A mod's own settings (docs/mod-services.md, "Mod settings") are declared
 * in the file the manifest's `settings` key names and the player's choices
 * live beside the mod's saved data, in <save dir>/mods/<namespace>/.  The
 * mod reads them through the services table's setting_get, which this file
 * answers (bw_mod_menu_setting_get).
 *
 * The calls
 * ---------
 *
 * The model is one model for the whole process.  The port opens it when the
 * menu opens (bw_mod_menu_open), the screen asks it questions, the player
 * changes a switch (bw_mod_menu_set_enabled) or moves a mod
 * (bw_mod_menu_move_up and its siblings), and the changes are applied when
 * the player leaves the menu (bw_mod_menu_apply).  The port calls
 * bw_mod_menu_start once at start-up, before the loader runs.
 */

#ifndef BW_PORT_MOD_MENU_H
#define BW_PORT_MOD_MENU_H

#include <stddef.h>

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* The model                                                           */
/* ------------------------------------------------------------------ */

/*
 * The port's start-up: restore the list the player chose last time, if
 * there is one, and build the model.  The port calls this before the
 * loader runs (bw_mod_loader_load_all), so the mods the player left on are
 * loaded and their state is read at the load point.
 *
 * The restore is the player's own: the mods that are on, in the order the
 * player chose, are the list the port holds from then on.
 *
 * With no record of the player's own (no <save dir>/mods.ini) the port's
 * list stands as it is: the environment's BW_MOD_ROOT, or what an explicit
 * call set.  Nothing is written and no mod's state is read.
 */
void bw_mod_menu_start(void);

/*
 * The menu opens: rebuild the model from the port's list and the player's
 * record.  The list is not changed: the changes apply when the player
 * leaves the menu (bw_mod_menu_apply).
 *
 * The screen calls this once, when the Mods section is entered.  A rebuild
 * drops the switches the player has not applied yet, so it is not a call
 * for every frame.
 */
void bw_mod_menu_open(void);

/* Forget the model (the port's power-on).  The files on the disk are
 * untouched. */
void bw_mod_menu_reset(void);

/* ------------------------------------------------------------------ */
/* The list                                                            */
/* ------------------------------------------------------------------ */

/* How many mods the menu shows. */
u32 bw_mod_menu_count(void);

/* The name the manifest gives the mod at `index`, or "" when there is
 * none. */
const char *bw_mod_menu_name_at(u32 index);

/* The mod's state namespace (docs/mod-state.md, section 1), or "" when
 * there is none. */
const char *bw_mod_menu_namespace_at(u32 index);

/* The path of the mod's settings declaration, relative to the mod's own
 * folder, or "" when the manifest names none. */
const char *bw_mod_menu_settings_at(u32 index);

/* The mod's own folder, or "" when there is none. */
const char *bw_mod_menu_root_at(u32 index);

/* The manifest's order key: the load order the port sorts by, and the
 * order a mod keeps when the player has not moved it.  It is the mod
 * author's own key (sdk/vfs.h, "The mod list"). */
int bw_mod_menu_order_at(u32 index);

/* The player's switch: 1 when the mod is on. */
int bw_mod_menu_enabled_at(u32 index);

/* 1 when the mod's root is in the port's list right now, so the mod runs
 * (or runs its rules) as it stands. */
int bw_mod_menu_running_at(u32 index);

/* 1 when the mod's own folder is there.  A mod the player knows about
 * whose folder is gone is still in the list, with its switch as it was. */
int bw_mod_menu_present_at(u32 index);

/* 1 when the mod ships code, so it has a library the loader opens and
 * unloads. */
int bw_mod_menu_has_code_at(u32 index);

/* ------------------------------------------------------------------ */
/* The order                                                           */
/* ------------------------------------------------------------------ */

/*
 * The player's own order.
 *
 * The list the screen shows is in the order the player chose, and the
 * order the mods load in is the order their overrides and their rules take
 * effect, so it is the player's to choose (docs/mod-services.md, "The mod
 * menu": "The order can be changed").  The move calls change it, and the
 * change is held until the player leaves the menu, like the switches: the
 * mods that are running do not change until then.
 *
 * The manifest's `order` key is the default: a mod the player has not
 * moved keeps the place its author gave it (docs/modding.md, section 3).
 * The player's own order is in the port's record (<save dir>/mods.ini), so
 * it outlives the port, and the start-up restore reads it back.
 *
 * The state namespace is per mod, so a mod that moves in the list keeps
 * its own state: the namespace is the manifest's `state` key and it
 * travels with the mod.
 */

/* Move the mod at `index` one place towards the front of the list.  0 on
 * success, -1 when there is no such mod or it is already at the front. */
int bw_mod_menu_move_up(u32 index);

/* Move the mod at `index` one place towards the back of the list.  0 on
 * success, -1 when there is no such mod or it is already at the back. */
int bw_mod_menu_move_down(u32 index);

/* Move the mod at `index` to `to`, the place it takes in the list: 0 is
 * the front.  A `to` past the end of the list is the last place.  0 on
 * success, -1 when there is no such mod. */
int bw_mod_menu_move_to(u32 index, u32 to);

/* ------------------------------------------------------------------ */
/* The switch                                                          */
/* ------------------------------------------------------------------ */

/*
 * The player's switch.  The change is held until the player leaves the
 * menu; the mods that are running do not change until then.  0 on success,
 * -1 when there is no such mod.
 */
int bw_mod_menu_set_enabled(u32 index, int on);

/* Non-zero when the switches or the order differ from the port's list, so
 * there is something to apply. */
int bw_mod_menu_dirty(void);

/*
 * Apply the switches and the order: the changes apply when the player
 * leaves the menu.
 *
 * The order is the one docs/mod-state.md, section 2, constraint 2, fixes:
 * the mods' state is flushed, the mods that are running are stopped (the
 * loader reverts the slots they installed before it unloads their
 * libraries, the last loaded first), the roots are rebuilt from the
 * switches (which drops the cached merged archives) and the mods that are
 * wanted are loaded again, so the load point reads their state.  A mod's
 * own state file stays where it is, and so do the game bytes the mod wrote
 * (constraint 3).
 *
 * The roots are rebuilt in the order the player chose: the order is handed
 * to the file layer as it is (bw_mod_set_order, sdk/vfs.h), so the loader
 * loads the mods in it and a later mod wins over an earlier one as the
 * player chose it.
 *
 * Every running mod is stopped and loaded again, not only the ones that
 * are going off: the loader loads every mod in the list, so a mod that
 * stays on must not be in the list twice.
 *
 * 0 on success.  A mod whose folder cannot be added to the list is
 * reported and the rest of the list stands.
 */
int bw_mod_menu_apply(void);

/* Drop the player's changes: the switches go back to the port's list and
 * the order goes back to the one the list had when the menu was opened. */
void bw_mod_menu_revert(void);

/* ------------------------------------------------------------------ */
/* The clear action                                                    */
/* ------------------------------------------------------------------ */

/*
 * The player's "clear saved data" for one mod: the mod's own state file
 * goes and its folder stays (docs/mod-state.md, section 1, "Removal").  The
 * port does not delete a mod's folder when the mod is removed, so a
 * re-enable reads the file again.
 *
 * 0 when the state is clear (there was none, or it is gone), -1 when the
 * file could not be removed.
 */
int bw_mod_menu_clear(u32 index);

/* ------------------------------------------------------------------ */
/* The orphan report                                                   */
/* ------------------------------------------------------------------ */

/*
 * The folders in <save dir>/mods/ that no mod in the list claims: a mod
 * that was removed and whose folder was left behind (docs/mod-state.md,
 * section 1, "Removal").  The port reports them at the menu.
 */
u32 bw_mod_menu_orphan_count(void);

/* The name of the orphan at `index` (its state namespace), or NULL. */
const char *bw_mod_menu_orphan_at(u32 index);

/* ------------------------------------------------------------------ */
/* A mod's own settings                                                */
/* ------------------------------------------------------------------ */

/* The kinds an option can have (docs/mod-services.md, "Mod settings"). */
#define BW_MOD_SETTING_BOOL   0   /* on or off            */
#define BW_MOD_SETTING_CHOICE 1   /* one of a list of names */

/* How many options the mod at `index` declares. */
u32 bw_mod_menu_option_count(u32 index);

/* The option's key: the name the mod asks setting_get for. */
const char *bw_mod_menu_option_key(u32 index, u32 option);

/* The option's label: the text the screen shows. */
const char *bw_mod_menu_option_label(u32 index, u32 option);

/* The option's kind: BW_MOD_SETTING_BOOL or BW_MOD_SETTING_CHOICE. */
int bw_mod_menu_option_kind(u32 index, u32 option);

/* The option's value: 0 or 1 for a bool, the index of the chosen name for
 * a choice.  The player's choice while it is in force, the default
 * otherwise. */
int bw_mod_menu_option_value(u32 index, u32 option);

/* The option's default, the value it has while the player has not chosen
 * one. */
int bw_mod_menu_option_default(u32 index, u32 option);

/* The player's choice for the option.  0 on success, -1 when there is no
 * such option or the value is not one it can take.  The choice is stored
 * beside the mod's saved data at once. */
int bw_mod_menu_option_set(u32 index, u32 option, int value);

/* How many names a choice option has. */
int bw_mod_menu_option_choice_count(u32 index, u32 option);

/* The name of the choice at `choice` (0 .. count - 1), or NULL. */
const char *bw_mod_menu_option_choice(u32 index, u32 option, int choice);

/* The option's value as text: "on" or "off" for a bool, the chosen name
 * for a choice.  Always NUL-terminated in `buf`. */
const char *bw_mod_menu_option_text(u32 index, u32 option, char *buf,
                                     size_t size);

/* ------------------------------------------------------------------ */
/* The mod that is calling                                             */
/* ------------------------------------------------------------------ */

/*
 * The port tells the model which mod is calling, the way the loader tells
 * the saved-data layer (bw_mod_savedata_set_namespace): the model's
 * setting_get answers for that mod alone.  NULL or "" clears it.
 */
void bw_mod_menu_set_namespace(const char *ns);

/* The namespace of the mod that is calling, or "" when there is none. */
const char *bw_mod_menu_namespace(void);

/*
 * The services table's setting_get (docs/mod-services.md, "Mod settings"):
 * the value of the option the key names, for the mod that is calling.  -1
 * when the key names no option of that mod, or when no mod is calling.
 *
 * The loader does not bind the call into the services table yet, so a mod
 * that calls it gets nothing.  When it does, it binds this one and sets the
 * namespace around the calls it makes into the mod, the way it sets the
 * saved-data layer's (bw_mod_menu_set_namespace).
 */
int bw_mod_menu_setting_get(const char *key);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_MOD_MENU_H */
