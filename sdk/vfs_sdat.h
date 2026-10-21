/*
 * sdk/vfs_sdat.h -- the sound archive append merge.
 *
 * Why this exists.  The game's sound archive is one 51 MB file.  Every count
 * in it is read from the file and no code table depends on those counts, so
 * new sequences, banks and wave archives are a data problem: they can be
 * appended.  Without a merge a mod would have to ship the whole archive and
 * would conflict with every other sound mod.  With it a mod ships small
 * files and a text declaration, and the loader builds one archive that holds
 * every original byte plus the additions of every mod.
 *
 * The mod side.  A mod holds a DIRECTORY at the archive's path.  In it:
 *
 *   sdat.txt   the declaration (grammar below)
 *   *.sseq *.sbnk *.swar   the files the declaration names
 *
 * Declaration grammar, one entry per line, `#` at a line start or after
 * white space begins a comment (so `bank=#12` is a reference, not a comment):
 *
 *   wavarc <name> = <file>
 *   bank   <name> = <file> wavarc=<ref>[,<ref>...]            1 to 4 refs
 *   seq    <name> = <file> bank=<ref> volume=<0-127> cpr=<0-127>
 *                          ppr=<0-127> player=<0-7>
 *
 * volume, cpr, ppr and player are optional (defaults 127, 64, 64, 0).  A <ref>
 * names an entry of the same mod (`name`), an entry of another mod
 * (`mod:name`), an original entry by its name in the archive's SYMB block, or
 * an original or merged entry by number (`#<id>`).
 *
 * The merge.  The output keeps every original byte of the archive and
 * appends, in this order: the new file data, a copy of the INFO block with
 * grown record tables, a grown FAT, a copy of the SYMB block with grown name
 * tables.  Old record tables stay in place as dead space; the new tables
 * point at the same old records.  Only the 0x40-byte header (file size and
 * the block offsets and sizes) and the 8 table offsets and the size field
 * at the head of INFO and SYMB differ from the original.  The original FILE
 * block keeps its own size field; the appended files sit after it and are
 * reached through FAT, which is the only thing the sound code reads.
 *
 * Numbering.  New ids start at the original counts and follow the mod load
 * order, then declaration order inside a mod and kind.  With mods A then B,
 * A's first sequence is 2076.  Ids are assigned at load and are not stable
 * when the mod list changes, so mods should name sounds, not number them.
 *
 * Naming.  A new entry's SYMB name is `<modname>:<name>`.  bw_sdat_lookup
 * finds an id from a qualified name in a merged image.
 */

#ifndef BW_PORT_VFS_SDAT_H
#define BW_PORT_VFS_SDAT_H

#include <stddef.h>

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Record kinds.  The values are the archive's own table order, the same as
 * BwSdatKind in engine/audio/audio.h. */
typedef enum {
    BW_SDATM_SEQ    = 0,
    BW_SDATM_BANK   = 2,
    BW_SDATM_WAVARC = 3
} BwSdatMKind;

#define BW_SDATM_NAME_MAX 64
#define BW_SDATM_FILE_MAX 128
#define BW_SDATM_REFS_MAX 4
#define BW_SDATM_REF_MAX  160   /* "mod:name" or "#id" */

/* One parsed declaration line. */
typedef struct {
    BwSdatMKind kind;
    char        name[BW_SDATM_NAME_MAX];
    char        file[BW_SDATM_FILE_MAX];
    /* bank: the wave archive refs.  seq: refs[0] is the bank ref. */
    char        refs[BW_SDATM_REFS_MAX][BW_SDATM_REF_MAX];
    u32         nrefs;
    u32         volume;
    u32         cpr;
    u32         ppr;
    u32         player;
} BwSdatEntry;

/* One parsed sdat.txt.  Free with bw_sdat_decl_free. */
typedef struct {
    BwSdatEntry *entries;
    u32          count;
} BwSdatDecl;

/* A file named by a declaration, with its bytes (borrowed). */
typedef struct {
    const char *name;
    const u8   *data;
    u32         size;
} BwSdatFile;

/* One mod's additions: its name, its parsed declaration and its files. */
typedef struct {
    const char       *mod_name;
    const BwSdatDecl *decl;
    const BwSdatFile *files;
    u32               file_count;
} BwSdatAdd;

/*
 * Parse the text of a sdat.txt.  Returns 0, or -1 with a one-line reason in
 * `err` (when err is not NULL).  Checks the grammar, the value ranges and
 * duplicate names; it does not resolve references.
 */
int  bw_sdat_parse_decl(const char *text, BwSdatDecl *out, char *err, size_t err_size);
void bw_sdat_decl_free(BwSdatDecl *decl);

/*
 * Merge `count` mods' additions into the archive image `base`.  On success
 * returns 0 and stores a malloc'ed image in *out (release with free) and its
 * size in *out_size.  With count 0 the output is a plain copy.  On failure
 * returns -1, sets *out to NULL and prints the reason to stderr.  Fails
 * without output on: a malformed base, an unresolved reference, a missing or
 * wrongly typed file, a file over 16 MB, or a full table (ids above 65535
 * for banks and wave archives, which the 16-bit record fields cannot hold).
 */
int bw_vfs_sdat_merge(const u8 *base, u32 base_size, const BwSdatAdd *adds,
                      u32 count, u8 **out, u32 *out_size);

/*
 * Find the id of the entry named `qualified_name` in a (merged) image: for an
 * added entry that is `<modname>:<name>`, for an original one its plain SYMB
 * name.  Returns 0 and stores the id, or -1 when absent.
 */
int bw_sdat_lookup(const u8 *sdat, u32 size, BwSdatMKind kind,
                   const char *qualified_name, u32 *id);

#ifdef __cplusplus
}
#endif

#endif
