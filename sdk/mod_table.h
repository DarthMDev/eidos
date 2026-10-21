/*
 * sdk/mod_table.h -- a mod's own data tables (docs/mod-tables.md).
 *
 * A mod names a table in its manifest:
 *
 *     table = <name> <path>
 *
 * The file at <path> (relative to the mod's folder) is text, one entry per
 * line:
 *
 *     # comment
 *     <key> = <value>
 *     default = <value>
 *
 * A key and a value are signed 32-bit numbers, decimal or 0x hex.  A key
 * that is not in the table reads the `default` line's value, or 0 when
 * there is none.  Later lines win over earlier ones for the same key.
 *
 * A table belongs to the mod that names it: two mods can each have a table
 * called `caps`, and each reads its own.  The rules read a table as
 * `table.<name>[key]`; a mod with code reads it through the services
 * table's table_get.
 */
#ifndef BW_PORT_MOD_TABLE_H
#define BW_PORT_MOD_TABLE_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* How many tables the port holds at once, for every mod together, and how
 * long a table's name may be. */
#define BW_MOD_TABLES_MAX     128
#define BW_MOD_TABLE_NAME_MAX 32

/*
 * Read the file at `path` as the table `name` of the mod whose namespace
 * is `ns`.  A table of the same mod and name read before is replaced.
 * 0, or -1 (the file is missing, a line is bad, or there is no room);
 * the reason goes to stderr with the mod's label.
 */
int bw_mod_table_load(const char *ns, const char *name, const char *path,
                      const char *label);

/* Whether the mod `ns` has a table `name`. */
int bw_mod_table_exists(const char *ns, const char *name);

/*
 * The value of `key` in the table `name` of the mod `ns`.  0 when the key
 * is in the table, 1 when it is not and *out holds the default, -1 when the
 * mod has no such table (*out is then 0).
 */
int bw_mod_table_get(const char *ns, const char *name, s32 key, s32 *out);

/* The services table's table_get: the same, for the mod that is calling. */
int bw_mod_table_get_calling(const char *name, s32 key, s32 *out);

/* Drop every table: at power-on and when the mod list changes. */
void bw_mod_table_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_MOD_TABLE_H */
