/*
 * tests/test_mod_restart.c -- the run restart: a mod's way of starting a new
 * game.
 *
 * The layer is sdk/mod_restart.c; the contract it keeps is the one of
 * docs/mod-services.md, "Run restart":
 *
 *   - the flags: BW_RESTART_NEW_GAME starts a new game directly, without
 *     stopping at the title, and BW_RESTART_DELETE_SAVE removes the save and
 *     the mod's own saved data first;
 *   - the order: the game save goes first, then the mod's own saved data,
 *     and then the flags say what the run should do next;
 *   - the decision is the service's and the work is the port's: the call
 *     hands the request to the port's own handler, with the flags;
 *   - a port that installs no handler takes the request with its own call
 *     when it is ready, and the request is not lost.
 *
 * What is stood in for:
 *
 *   - the backup chip (sdk/arm7/fs.c): the game's save is the port's own
 *     file under the save folder, and the chip's image is dropped before
 *     the file is removed, because the reset writes the image back and a
 *     file that was removed would come back with it.  The test's chip
 *     remembers the drop and what was on the disk when it happened, which
 *     is how the order of the two removals is checked.
 *
 * The saved-data layer is the port's own (sdk/mod_savedata.c): the mod's own
 * data is a real file, and the clear that removes it is the real one.  The
 * port's own handler is stood in for too: the real one resets the machine
 * and starts the game again, which is the boot's work (demo/eidos_boot.c)
 * and not the test's.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mod_state.h"
#include "save.h"
#include "types.h"

#include "port_test.h"

#ifdef _WIN32
#include <direct.h>
#include <sys/stat.h>
#define BW_RMDIR(p) _rmdir(p)
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#define BW_RMDIR(p) rmdir(p)
#endif

/*
 * The saved data layer's own calls (sdk/mod_savedata.c).  That file holds
 * its prototypes until the header which will carry them for the rest of the
 * port is written, so the test declares the ones it uses; they are the ones
 * the file's own header comment names.
 */
void        bw_mod_savedata_set_namespace(const char *ns);
void        bw_mod_savedata_set_save(const char *name);
int         bw_mod_savedata_store(const void *buf, u32 size);

/*
 * The run restart's own calls (sdk/mod_restart.c).  That file holds its
 * prototypes until the header which will carry them for the rest of the
 * port is written, so the test declares the ones it uses.
 */
void bw_mod_restart(u32 flags);
void bw_mod_restart_set_handler(void (*fn)(u32 flags));
int  bw_mod_restart_take(u32 *flags);

/* ------------------------------------------------------------------ */
/* The tree the test writes                                            */
/* ------------------------------------------------------------------ */

/* The save folder the test owns, under the test's own working directory. */
#define T_SAVE_DIR "mod_restart_tmp"

/* The game's save: the port's own file under the save folder. */
#define T_SAVE_FILE "pokeblack.sav"

/* The name of the game save the mod's own data belongs to. */
#define T_SAVE_NAME "pokeblack"

/* The namespace of the mod that asks for the restart. */
#define T_NS "testmod"

/* The namespace of another mod, whose data the restart must not touch. */
#define T_OTHER_NS "othermod"

/* The mod's own data: the bytes the layer keeps. */
#define T_STATE "the mod's own bytes"

/* The game's save: the bytes the port's own file holds. */
#define T_SAVE_BYTES 512

/* ------------------------------------------------------------------ */
/* The helpers                                                         */
/* ------------------------------------------------------------------ */

/* The path of the game's save file. */
static int save_path(char *out, size_t size)
{
    return bw_save_path(out, size, T_SAVE_FILE);
}

/* The path of a mod's own state file. */
static int state_path(char *out, size_t size, const char *ns)
{
    char rel[256];
    int n = snprintf(rel, sizeof rel, "mods/%s/%s.bin", ns, T_SAVE_NAME);

    if (n < 0 || (size_t)n >= sizeof rel) {
        return -1;
    }
    return bw_save_path(out, size, rel);
}

/* The path of a mod's own folder. */
static int state_dir_path(char *out, size_t size, const char *ns)
{
    char rel[256];
    int n = snprintf(rel, sizeof rel, "mods/%s", ns);

    if (n < 0 || (size_t)n >= sizeof rel) {
        return -1;
    }
    return bw_save_path(out, size, rel);
}

/* Whether a file is there. */
static int file_exists(const char *path)
{
    FILE *fp = fopen(path, "rb");

    if (fp == NULL) {
        return 0;
    }
    fclose(fp);
    return 1;
}

/* Whether a folder is there. */
static int dir_exists(const char *path)
{
#ifdef _WIN32
    struct _stat st;

    return _stat(path, &st) == 0 && (st.st_mode & _S_IFDIR) != 0;
#else
    struct stat st;

    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

/* Write a file of bytes, the port's own way: the whole file, atomically, and
 * the folder it is in is made by the write. */
static int write_file(const char *path, const void *data, size_t size)
{
    return bw_save_write_atomic(path, data, (u32)size);
}

/* Take the tree an earlier run left away. */
static void clean_tree(void)
{
    static const char *const files[] = {
        T_SAVE_DIR "/" T_SAVE_FILE,
        T_SAVE_DIR "/" T_SAVE_FILE ".tmp",
        T_SAVE_DIR "/mods/" T_NS "/" T_SAVE_NAME ".bin",
        T_SAVE_DIR "/mods/" T_NS "/" T_SAVE_NAME ".bin.tmp",
        T_SAVE_DIR "/mods/" T_OTHER_NS "/" T_SAVE_NAME ".bin",
        T_SAVE_DIR "/mods/" T_OTHER_NS "/" T_SAVE_NAME ".bin.tmp"
    };
    static const char *const dirs[] = {
        T_SAVE_DIR "/mods/" T_NS,
        T_SAVE_DIR "/mods/" T_OTHER_NS,
        T_SAVE_DIR "/mods",
        T_SAVE_DIR
    };
    size_t i;

    for (i = 0; i < sizeof files / sizeof files[0]; i++) {
        remove(files[i]);
    }
    for (i = 0; i < sizeof dirs / sizeof dirs[0]; i++) {
        BW_RMDIR(dirs[i]);
    }
}

/* ------------------------------------------------------------------ */
/* The backup chip, stood in for                                       */
/* ------------------------------------------------------------------ */

/*
 * The backup chip (sdk/arm7/fs.c): the port drops the chip's own image
 * before it removes the save file, because the reset writes the image back
 * and a file that was removed would come back with it.  The stand-in
 * remembers the drop and what was on the disk at that moment, which is how
 * the order of the two removals is checked.
 */
static int  g_chip_dropped;
static int  g_chip_saw_save;      /* the save file was there ...        */
static int  g_chip_saw_state;     /* ... and so was the mod's own data  */
static char g_chip_save_path[1200];
static char g_chip_state_path[1200];

void bw_arm7_backup_set_path(const char *path)
{
    (void)path;
    g_chip_dropped = 1;
    g_chip_saw_save = file_exists(g_chip_save_path);
    g_chip_saw_state = file_exists(g_chip_state_path);
}

/* ------------------------------------------------------------------ */
/* The port's own handler, stood in for                                */
/* ------------------------------------------------------------------ */

/*
 * The port's own handler: it is told the flags of the restart and does the
 * work where it can (the boot's frame loop, demo/eidos_boot.c).  The test's
 * handler only remembers the flags.
 */
static int g_handler_calls;
static u32 g_handler_flags;

static void restart_handler(u32 flags)
{
    g_handler_calls++;
    g_handler_flags = flags;
}

/* ------------------------------------------------------------------ */
/* The test                                                            */
/* ------------------------------------------------------------------ */

/* Put the tree back the way the port's own files are. */
static int build_tree(void)
{
    u8 save[T_SAVE_BYTES];
    size_t i;

    for (i = 0; i < sizeof save; i++) {
        save[i] = (u8)(i & 0xFFu);
    }
    if (write_file(g_chip_save_path, save, sizeof save) != 0) {
        return -1;
    }
    bw_mod_savedata_set_namespace(T_NS);
    if (bw_mod_savedata_store(T_STATE, (u32)strlen(T_STATE)) != 0) {
        return -1;
    }
    bw_mod_savedata_set_namespace(T_OTHER_NS);
    if (bw_mod_savedata_store(T_STATE, (u32)strlen(T_STATE)) != 0) {
        return -1;
    }
    bw_mod_savedata_set_namespace(T_NS);
    return 0;
}

int main(void)
{
    char path[1200];
    char dir[1200];
    u32 flags = 0;

    bw_set_save_dir(T_SAVE_DIR);
    bw_mod_savedata_set_save(T_SAVE_NAME);
    clean_tree();
    BW_REQUIRE("the save file's path is known",
               save_path(g_chip_save_path, sizeof g_chip_save_path) == 0);
    BW_REQUIRE("the mod's own file's path is known",
               state_path(g_chip_state_path, sizeof g_chip_state_path, T_NS)
               == 0);
    BW_REQUIRE("the tree is written", build_tree() == 0);

    printf("a restart without the delete flag\n");
    bw_mod_restart_set_handler(restart_handler);
    g_handler_calls = 0;
    g_chip_dropped = 0;
    bw_mod_restart(BW_RESTART_NEW_GAME);
    BW_CHECK_EQ("the port is told of the restart", g_handler_calls, 1);
    BW_CHECK_EQ("with the flags of it", (long)g_handler_flags,
                (long)BW_RESTART_NEW_GAME);
    BW_CHECK_EQ("the game save is kept", file_exists(g_chip_save_path), 1);
    BW_CHECK_EQ("the mod's own data is kept",
                file_exists(g_chip_state_path), 1);
    BW_CHECK_EQ("the chip's own image is not dropped", g_chip_dropped, 0);
    BW_CHECK_EQ("and the request does not wait for the port",
                bw_mod_restart_take(&flags), 0);

    printf("a restart with the delete flag\n");
    g_handler_calls = 0;
    g_chip_dropped = 0;
    g_chip_saw_save = 0;
    g_chip_saw_state = 0;
    bw_mod_restart(BW_RESTART_NEW_GAME | BW_RESTART_DELETE_SAVE);
    BW_CHECK_EQ("the port is told of the restart", g_handler_calls, 1);
    BW_CHECK_EQ("with the flags of it", (long)g_handler_flags,
                (long)(BW_RESTART_NEW_GAME | BW_RESTART_DELETE_SAVE));
    BW_CHECK_EQ("the chip's own image is dropped first", g_chip_dropped, 1);
    BW_CHECK_EQ("the save file was still there then", g_chip_saw_save, 1);
    BW_CHECK_EQ("and so was the mod's own data", g_chip_saw_state, 1);
    BW_CHECK_EQ("the game save is gone", file_exists(g_chip_save_path), 0);
    BW_CHECK_EQ("the mod's own data is gone",
                file_exists(g_chip_state_path), 0);
    BW_REQUIRE("the mod's own folder's path is known",
               state_dir_path(dir, sizeof dir, T_NS) == 0);
    BW_CHECK_EQ("and the folder stays", dir_exists(dir), 1);
    BW_REQUIRE("the other mod's file's path is known",
               state_path(path, sizeof path, T_OTHER_NS) == 0);
    BW_CHECK_EQ("another mod's data is not touched", file_exists(path), 1);

    printf("a request with no handler in place\n");
    bw_mod_restart_set_handler(NULL);
    BW_REQUIRE("the tree is written again", build_tree() == 0);
    g_handler_calls = 0;
    g_chip_dropped = 0;
    bw_mod_restart(BW_RESTART_DELETE_SAVE);
    BW_CHECK_EQ("nobody is told at once", g_handler_calls, 0);
    BW_CHECK_EQ("the decision is the service's: the save is gone",
                file_exists(g_chip_save_path), 0);
    BW_CHECK_EQ("and the mod's own data is gone",
                file_exists(g_chip_state_path), 0);
    BW_CHECK_EQ("the request waits for the port",
                bw_mod_restart_take(&flags), 1);
    BW_CHECK_EQ("with its flags", (long)flags, (long)BW_RESTART_DELETE_SAVE);
    BW_CHECK_EQ("and it is taken once", bw_mod_restart_take(&flags), 0);

    printf("a restart with no delete flag and no handler\n");
    BW_REQUIRE("the tree is written again", build_tree() == 0);
    g_chip_dropped = 0;
    bw_mod_restart(BW_RESTART_NEW_GAME);
    BW_CHECK_EQ("the game save is kept", file_exists(g_chip_save_path), 1);
    BW_CHECK_EQ("the mod's own data is kept",
                file_exists(g_chip_state_path), 1);
    BW_CHECK_EQ("the chip's own image is not dropped", g_chip_dropped, 0);
    BW_CHECK_EQ("the request waits for the port",
                bw_mod_restart_take(&flags), 1);
    BW_CHECK_EQ("with its flags", (long)flags, (long)BW_RESTART_NEW_GAME);

    clean_tree();
    BW_REPORT("test_mod_restart");
}
