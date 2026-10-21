/*
 * sdk/mod_restart.c -- the run restart: a mod's way of starting a new game
 * (docs/mod-services.md, "Run restart"; docs/mod-modes.md, section 3, "Run
 * restart"; docs/modding.md, section 3).
 *
 * What this file is
 * -----------------
 *
 * The services table's call (bw_mod_restart) and the port's own seam
 * (bw_mod_restart_set_handler).
 *
 * The rules it keeps
 * ------------------
 *
 * (docs/mod-services.md, "Run restart"; the words are the document's)
 *
 *   - "Resets the game the way the port's own reset does and, with
 *     BW_RESTART_NEW_GAME, starts a new game directly, without stopping at
 *     the title."
 *   - "With BW_RESTART_DELETE_SAVE the save and the mod's saved data are
 *     removed first."
 *   - "Ironmon uses it after a loss; a permadeath mod uses it with the
 *     delete flag."
 *
 * What is the port's and what is this file's
 * ------------------------------------------
 *
 * The reset is the port's own (bw_port_reset, sdk/port_reset.c): it puts
 * the emulated machine back to power-on and joins the layers again.  The
 * new game is the boot's (demo/eidos_boot.c): the boot owns the run's
 * start-up, and only the boot can start the game again and drive it into a
 * new game without stopping at the title.
 *
 * So this file keeps the decision and the port carries it out:
 *
 *   - the decision is here: what is removed, and in what order (the game
 *     save first, then the mod's own saved data), and the flags that say
 *     what the run should do next;
 *   - the port's own seam is the handler the port installs: the handler is
 *     told the flags, and the port does the reset and the new game when it
 *     can.
 *
 * The handler is called on the game's thread, where the reset cannot be
 * done: the reset clears the main RAM the game is running in, so the port's
 * handler records the request and does the work at the next safe point of
 * its own loop (the boot's frame loop).
 *
 * Why the save file and the chip's image
 * --------------------------------------
 *
 * The game's save is the backup chip's image, and the port keeps it in a
 * file (sdk/arm7/fs.c).  The reset writes the chip's image back to that
 * file, so a file that was removed would come back with the reset.  The
 * image is therefore dropped first (the chip loads the file again at the
 * next IDENTIFY, and a file that is not there is an erased chip, which the
 * game reads as "no save"), and the file is removed after it.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "types.h"
#include "mod_state.h"
#include "save.h"

/* ------------------------------------------------------------------ */
/* The port's own API                                                  */
/* ------------------------------------------------------------------ */

/*
 * This file has no header of its own yet, so the prototypes are here (the
 * saved-data layer does the same, sdk/mod_savedata.c).  The loader binds
 * the first of them into the services table:
 *
 *     m->services.run_restart = bw_mod_restart;
 *
 * and the port installs the second one at start-up:
 *
 *     bw_mod_restart_set_handler(port_restart);
 *
 * The third one is for a port that drives the restart from its own loop
 * instead of through a handler.
 */
void bw_mod_restart(u32 flags);
void bw_mod_restart_set_handler(void (*fn)(u32 flags));
int  bw_mod_restart_take(u32 *flags);

/* ------------------------------------------------------------------ */
/* What the other layers provide                                       */
/* ------------------------------------------------------------------ */

/*
 * The saved-data layer (sdk/mod_savedata.c): the mod's own file, which the
 * clear removes.  That file holds its prototypes until the header which
 * will carry them for the rest of the port is written, so the prototype is
 * here.  It is weak, so a link without the layer still succeeds: the mod's
 * own data is then not removed (sdk/mod_frame.c does the same).
 */
#if defined(__GNUC__) || defined(__clang__)
#define BW_PORT_WEAK __attribute__((weak))
#else
#define BW_PORT_WEAK
#endif

extern int  bw_mod_savedata_clear(void) BW_PORT_WEAK;

/*
 * The backup chip (sdk/arm7/fs.c): the game's save.  The chip's own image
 * is dropped before the file is removed, so the reset does not write the
 * save back.  Weak, for the same reason.
 */
extern void bw_arm7_backup_set_path(const char *path) BW_PORT_WEAK;

/* ------------------------------------------------------------------ */
/* The limits                                                          */
/* ------------------------------------------------------------------ */

/* The game's save: the file under the save folder (sdk/arm7/fs.c). */
#define BW_MOD_RESTART_SAVE_NAME "pokeblack.sav"

/* The longest path the layer builds. */
#define BW_MOD_RESTART_PATH_MAX 1200

/* ------------------------------------------------------------------ */
/* The state                                                           */
/* ------------------------------------------------------------------ */

/* The port's own handler: what a restart means to the port. */
static void (*g_handler)(u32 flags);

/* A restart that waits for the port to take it. */
static u32 g_flags;
static int g_waiting;

/* ------------------------------------------------------------------ */
/* The small helpers                                                   */
/* ------------------------------------------------------------------ */

/* Whether a file is there. */
static int file_exists(const char *path)
{
    FILE *f;

    if (path == NULL || path[0] == '\0') {
        return 0;
    }
    f = fopen(path, "rb");
    if (f == NULL) {
        return 0;
    }
    fclose(f);
    return 1;
}

/* ------------------------------------------------------------------ */
/* The deletion                                                        */
/* ------------------------------------------------------------------ */

/*
 * The game save goes: the chip's own image first, so the reset does not
 * write it back, and the file after it.
 */
static void delete_game_save(void)
{
    char path[BW_MOD_RESTART_PATH_MAX];

    if (bw_arm7_backup_set_path != NULL) {
        (void)bw_arm7_backup_set_path(NULL);
    }
    if (bw_save_path(path, sizeof path, BW_MOD_RESTART_SAVE_NAME) != 0) {
        fprintf(stderr, "eidos: restart: no save folder; the save was not "
                        "removed\n");
        return;
    }
    if (!file_exists(path)) {
        fprintf(stderr, "eidos: restart: there was no save to remove\n");
        return;
    }
    if (remove(path) != 0) {
        fprintf(stderr, "eidos: restart: the save could not be removed\n");
        return;
    }
}

/*
 * The mod's own saved data goes: the file of the mod that is calling (the
 * loader puts a mod's namespace in force around every call it makes into
 * that mod, so the clear reaches that mod's own file).
 */
static void delete_mod_data(void)
{
    if (bw_mod_savedata_clear == NULL) {
        fprintf(stderr, "eidos: restart: the mod's own data was not removed\n");
        return;
    }
    if (bw_mod_savedata_clear() != 0) {
        fprintf(stderr, "eidos: restart: the mod's own data was not removed\n");
    }
}

/* ------------------------------------------------------------------ */
/* The services table's call                                           */
/* ------------------------------------------------------------------ */

/*
 * `run_restart`: the game is reset the way the port's own reset does it,
 * and with BW_RESTART_NEW_GAME a new game starts directly, without
 * stopping at the title.  With BW_RESTART_DELETE_SAVE the save and the
 * mod's own saved data are removed first.
 *
 * The flags are the port's own (sdk/mod_state.h): BW_RESTART_NEW_GAME and
 * BW_RESTART_DELETE_SAVE.  The call is made by a mod, so the mod that asks
 * for the restart is the one the namespace in force names.
 */
void bw_mod_restart(u32 flags)
{
    if ((flags & BW_RESTART_DELETE_SAVE) != 0u) {
        delete_game_save();
        delete_mod_data();
    }
    g_flags = flags;
    g_waiting = 1;
    if (g_handler != NULL) {
        /* The port owns the request from here: it does the reset and the
         * new game when it can. */
        g_waiting = 0;
        g_handler(flags);
    }
}

/* ------------------------------------------------------------------ */
/* The port's own calls                                                */
/* ------------------------------------------------------------------ */

/*
 * The port's own seam: what a restart means to the port.  The handler is
 * called with the flags of the restart, on the game's thread, and does the
 * work where it can (the boot's frame loop, where the reset and the new
 * game belong).  NULL puts the default back: the request then waits, and
 * the port takes it with bw_mod_restart_take.
 */
void bw_mod_restart_set_handler(void (*fn)(u32 flags))
{
    g_handler = fn;
}

/*
 * Take a restart that waits: 1 when there was one, with the flags in
 * `flags`, and 0 when there was none.  A port that installs no handler
 * takes the request this way.
 */
int bw_mod_restart_take(u32 *flags)
{
    if (!g_waiting) {
        return 0;
    }
    g_waiting = 0;
    if (flags != NULL) {
        *flags = g_flags;
    }
    return 1;
}
