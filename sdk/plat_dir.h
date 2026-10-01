/*
 * sdk/plat_dir.h -- the two directory questions the file layer asks the
 * host: what is at a path, and which names a directory holds.
 *
 * Member-level NARC overrides (sdk/vfs_narc.c) are directories of loose
 * files in a mod tree, so the file layer has to tell a file from a
 * directory and list one.  Standard C has neither, so sdk/plat_dir.c holds
 * the two implementations (Win32 and POSIX) behind this interface, the same
 * arrangement as sdk/plat_thread.c.  Paths are passed as the rest of the
 * file layer passes them to fopen: narrow strings, forward slashes accepted
 * on every host.
 */

#ifndef BW_PORT_PLAT_DIR_H
#define BW_PORT_PLAT_DIR_H

#ifdef __cplusplus
extern "C" {
#endif

#define BW_PATH_NONE 0   /* nothing there, or not accessible */
#define BW_PATH_FILE 1   /* a regular file                    */
#define BW_PATH_DIR  2   /* a directory                       */
#define BW_PATH_OTHER 3  /* something else (device, socket)   */

int bw_plat_path_kind(const char *path);

/*
 * Call `fn(ctx, name)` once for every entry of directory `path` except "."
 * and "..", in the order the host returns them (callers that need an order
 * sort).  `name` is the bare entry name, valid only during the call.  A
 * non-zero return from `fn` stops the walk and is returned.  Returns 0 after
 * a full walk, or -1 if the directory cannot be opened.
 */
int bw_plat_list_dir(const char *path, int (*fn)(void *ctx, const char *name),
                     void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_PLAT_DIR_H */
