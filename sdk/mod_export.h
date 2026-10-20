/*
 * sdk/mod_export.h -- a Pokemon's export and import
 * (docs/mod-services.md, "Pokemon export and import").
 *
 * The exported form is the game's own 0xDC-byte Pokemon record with the
 * port's header in front: a magic, a format version and a checksum.  A
 * Genlocke carries the record into the next game; Wonderlocke and Egglocke
 * send it through a peer session.
 *
 * The API here is the port's own; the services table in sdk/mod_state.h
 * carries the two calls a mod reaches.
 */

#ifndef BW_PORT_MOD_EXPORT_H
#define BW_PORT_MOD_EXPORT_H

#include "types.h"
#include "mod_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The services table's calls. */
int bw_mod_mon_export(BwMonRef mon, void *buf, u32 size);
int bw_mod_mon_import(const void *buf, u32 size, BwMonRef *out);

/* The port's own calls. */
u32  bw_mod_mon_crc(const void *data, u32 size);
int  bw_mod_mon_pack(void *buf, u32 size, const void *record);
int  bw_mod_mon_unpack(const void *buf, u32 size, void *record);
int  bw_mod_mon_record_ok(const void *record);
void bw_mod_export_set_game_data(void *game_data);
void bw_mod_export_reset(void);
int  bw_mod_mon_export_file(BwMonRef mon, const char *path);
int  bw_mod_mon_import_file(const char *path, BwMonRef *out);

#ifdef __cplusplus
}
#endif
#endif /* BW_PORT_MOD_EXPORT_H */
