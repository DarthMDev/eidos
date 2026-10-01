/*
 * sdk/save.h -- where the player's files live and how they are written.
 *
 * Why it exists: three port modules keep state that outlives the process,
 * the cartridge's backup chip (sdk/arm7/fs.c, pokeblack.sav), the console's
 * firmware flash (sdk/arm7/nvram.c, firmware.bin) and the user settings
 * file (sdk/firmware.c, firmware.ini).  They share one folder and one
 * write discipline: the whole file is written to a sibling temporary,
 * synced, and swapped in with sdk/plat_save.h's replace, so a crash leaves
 * the old file or the new one and never a torn one.
 *
 * The folder, first match wins:
 *   1. bw_set_save_dir() (a front end's choice, --save-dir, the tests);
 *   2. the BW_SAVE_DIR environment variable;
 *   3. the platform default (docs/installer.md, "Where the saves go").
 * With none of the three (a mobile build whose front end has not set it
 * yet) the folder is empty and every load and store reports failure: the
 * modules run on their in-memory state and say so once.
 */

#ifndef BW_PORT_SAVE_H
#define BW_PORT_SAVE_H

#include <stddef.h>

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* NULL returns to the environment / platform default.                     */
void bw_set_save_dir(const char *path);

/* The folder in use ("" when there is none).  Never NULL.                 */
const char *bw_save_dir(void);

/* `<save dir>/<name>` into `out`.  0 on success, -1 with no folder or no room. */
int bw_save_path(char *out, size_t out_size, const char *name);

/*
 * Read up to `size` bytes of `path` into `buf`.  Returns the file's full
 * length (which may be larger or smaller than `size`; only min(size, length)
 * bytes are stored), or -1 when the file cannot be opened.
 */
long bw_save_read(const char *path, u8 *buf, u32 size);

/*
 * Write `size` bytes as the whole new contents of `path`, atomically:
 * `<path>.tmp`, synced, then replaced into place.  The folder is created
 * if missing.  0 on success, -1 on failure (the old file is untouched).
 */
int bw_save_write_atomic(const char *path, const void *data, u32 size);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_SAVE_H */
