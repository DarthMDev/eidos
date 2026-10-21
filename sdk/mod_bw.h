/*
 * sdk/mod_bw.h -- reading a mod shipped as a .bw package.
 *
 * A .bw is a zip file that kenon writes (kenon/docs/bw-format.md): the
 * packed form of one mod folder.  The port reads a mod folder already
 * (docs/mods.md, sdk/vfs.h), so the simplest reader unpacks the package into
 * a cache folder and lets the caller add that folder as a mod root.  Nothing
 * in the file layer changes.
 *
 * What a package must satisfy (all of it is checked before one byte is
 * written, so an unpack either produces the whole folder or nothing):
 *   - a zip with deflate members (stored members are accepted too), no
 *     directory entries, no zip64, no encryption;
 *   - the archive comment is "eidos-mod bw 1" (a missing comment means
 *     version 1); any other major version is refused;
 *   - the first member is eidos_mod.txt;
 *   - every path is relative with forward slashes: no "..", no empty part,
 *     no backslash, no drive prefix or colon, no control or Windows-illegal
 *     characters, no Windows reserved device names (CON, PRN, AUX, NUL,
 *     COM1-9, LPT1-9, with or without an extension), no part ending in a dot
 *     or a space, and no two paths that differ only by letter case (nor a
 *     file and a folder of the same name);
 *   - zip bomb limits: no member above 1 GiB unpacked, no package above
 *     4 GiB unpacked in total, no member that grows more than 1000 times;
 *   - every member's CRC-32 matches after inflating.
 *
 * The unpack writes into a temporary sibling folder ("<cache_dir>.part") and
 * renames it to cache_dir when every member is written and checked, so a
 * failed unpack leaves no cache_dir behind.
 */

#ifndef BW_PORT_MOD_BW_H
#define BW_PORT_MOD_BW_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Validate the package at `bw_path` and unpack it into the new folder
 * `cache_dir` (missing parents are created).  Returns 0 on success.  If
 * cache_dir already exists as a folder the package is validated and the
 * existing folder is kept (the folder name carries a hash of the package, so
 * an existing one is the same content, and a rename into place is atomic so
 * it is never half written).  Returns -1 on any failure and writes a one
 * line English reason into `err` (may be NULL).  On failure cache_dir does
 * not exist.
 */
int bw_mod_bw_unpack(const char *bw_path, const char *cache_dir,
                     char *err, size_t err_size);

/*
 * Name the cache folder for a package: "<dir of the package>/bw-cache/
 * <file name without .bw>-<16 hex digits>", where the digits hash the
 * package's bytes, so an updated package gets a new folder and unpacks
 * again.  Returns 0 on success, -1 when the file cannot be read or `size` is
 * too small.  The caller adds the result as a mod root after a successful
 * bw_mod_bw_unpack.  Old folders are not removed here.
 */
int bw_mod_bw_cache_path(const char *bw_path, char *out, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_MOD_BW_H */
