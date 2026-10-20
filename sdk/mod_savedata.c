/*
 * sdk/mod_savedata.c -- a mod's own saved data: the file it lives in, the
 * envelope around it, and the versions it carries (docs/mod-state.md,
 * sections 1, 2 and 5).
 *
 * What this file is:
 *
 *   - the state namespace: one folder per mod under the save directory,
 *     <save dir>/mods/<namespace>/, and one state file per game save in
 *     it, <save>.bin, so a mod's data always belongs to the save it was
 *     written with;
 *   - the envelope: the port's own 24-byte header (BwModStateHeader,
 *     sdk/mod_state.h) followed by the mod's opaque payload.  The port
 *     owns the header and the mod never sees it; the mod owns the bytes
 *     after it;
 *   - the versions: a file older than the port's is read with its own
 *     payload size and rewritten in the current version, a file newer
 *     than the port's is not read and is moved aside, and a file the
 *     port cannot read is moved aside and reported;
 *   - the five calls the services table carries (sdk/mod_state.h,
 *     BwModServices): state_load, state_store, state_clear, state_info and
 *     state_dir.
 *
 * The write discipline is the port's own whole-file atomic write
 * (bw_save_write_atomic, sdk/save.h): a temporary file, a sync and the
 * replace.  The unit of that discipline is one file, so one file per mod:
 * a region per mod inside one shared file would move every later region
 * when one mod's format changed, and a bad write in one region would
 * damage another mod's bytes (docs/mod-state.md, section 1).
 *
 * The file is written, not patched: a store replaces the whole file, so
 * there is no partial state, and a file the port cannot read is moved
 * aside rather than deleted, so a player can recover it.
 *
 * Who calls what:
 *
 *   - the loader (sdk/mod_loader.c) tells the layer which mod is calling
 *     (bw_mod_savedata_set_namespace) and which manifest api is in force
 *     (bw_mod_savedata_set_api), and binds the five calls below into the
 *     mod's services table.  The namespace is set around every call the
 *     port makes into a mod, so a mod's state call always knows whose
 *     file it is about;
 *   - the port tells the layer the checksum of the game save in force
 *     (bw_mod_savedata_set_save_check) and drives the save handoff
 *     (bw_mod_savedata_save_begin and bw_mod_savedata_save_done).
 *
 * The layer keeps no game state of its own: it is a file and an envelope
 * around a payload the mod owns.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "types.h"
#include "mod_state.h"
#include "plat_save.h"
#include "save.h"

/* ------------------------------------------------------------------ */
/* The layer's own surface                                             */
/* ------------------------------------------------------------------ */

/*
 * The port's own calls.  They are not part of the mod ABI: a mod reaches
 * the state through the services table (sdk/mod_state.h), and the loader
 * binds the five calls below into it.  The other calls are the port's
 * own seam: the loader and the save handoff use them.
 *
 * The prototypes are here so the file reads in one order; the header
 * that will carry them for the rest of the port is not written yet.
 */

/* The mod that is calling (the loader sets it around its calls). */
void        bw_mod_savedata_set_namespace(const char *ns);
const char *bw_mod_savedata_namespace(void);

/* The manifest api in force for the mod that is calling. */
void bw_mod_savedata_set_api(u32 api);

/* The game save the state belongs to, and its checksum. */
void bw_mod_savedata_set_save(const char *name);
void bw_mod_savedata_set_save_check(u32 check);
u32  bw_mod_savedata_save_check(void);
int  bw_mod_savedata_in_step(void);

/* The save handoff: the port's save.begin and save.done. */
void bw_mod_savedata_save_begin(void);
int  bw_mod_savedata_save_done(int ok, u32 check);

/* Forget everything the layer holds: at power-on and at every reset
 * (bw_port_reset).  The files on the disk are untouched. */
void bw_mod_savedata_reset(void);

/* The five calls the services table carries. */
s32  bw_mod_savedata_load(void *buf, u32 size);
int  bw_mod_savedata_store(const void *buf, u32 size);
int  bw_mod_savedata_clear(void);
int  bw_mod_savedata_info(BwModStateHeader *out);
int  bw_mod_savedata_dir(char *out, u32 out_size);

/* The checksum the port and the layer share. */
u32  bw_mod_savedata_crc(const void *data, u32 size);

/* ------------------------------------------------------------------ */
/* The limits                                                          */
/* ------------------------------------------------------------------ */

/*
 * The port's own header version.  It is bumped when the header's layout
 * changes; a file with an older version is read with that version's
 * layout and written in this one at the next store (docs/mod-state.md,
 * section 5).  The number is the frozen header's (sdk/mod_state.h).
 */

/*
 * The header's size on the disk: 24 bytes, the seven fields of
 * BwModStateHeader in the order the document's table gives them.  The
 * fields are written one by one and little-endian, so the file is the
 * same on every host whatever the compiler does with the struct.
 */

/* The frozen header is 24 bytes (sdk/mod_state.h). */
typedef char bw_mod_savedata_header_is_24[
    (sizeof(BwModStateHeader) == BW_MOD_STATE_HEADER_SIZE) ? 1 : -1];

/*
 * The largest payload a mod may store.  A mod runs in the port's own
 * address space with the port's privilege (docs/mod-interface.md,
 * section 5), so the layer keeps one mod's payload from eating the
 * machine: a store above the cap is refused and reported.
 */
/*
 * A namespace is one path component and a short one: the port copies the
 * name into the path verbatim (bw_save_path, sdk/save.c), so a name that
 * carried a separator or a ".." would leave the mods/ folder.
 */
#define BW_MOD_NS_MAX 64

/* The save name ("pokeblack"), the game save the port is running. */
#define BW_MOD_SAVE_NAME_MAX 64

/* How many mods the layer keeps a record of. */
#define BW_MOD_SAVEDATA_MAX_MODS 32

/* The path buffers. */
#define BW_MOD_SAVEDATA_PATH_MAX 1200

/* ------------------------------------------------------------------ */
/* The records                                                         */
/* ------------------------------------------------------------------ */

/*
 * One record per mod the layer has served.  It holds the namespace, the
 * manifest api in force (the header's api_written) and the payload a
 * save handoff is holding for the mod.
 */
typedef struct BwModSaveRec {
    char ns[BW_MOD_NS_MAX + 1];   /* the namespace                   */
    u32  api;                 /* the manifest api in force         */
    int  no_folder_reported;  /* the "no save folder" report is done */
    int  store_reported;      /* the "the store failed" report is done */
    u8  *staged;              /* the payload a save handoff holds   */
    u32  staged_size;
    u32  staged_cap;
    int  staged_valid;
} BwModSaveRec;

static BwModSaveRec g_mods[BW_MOD_SAVEDATA_MAX_MODS];
static u32          g_mod_count;
static int          g_current = -1;    /* the mod that is calling        */
static int          g_save_pending;    /* a save handoff is in progress */
static u32          g_save_check;      /* the checksum of the save in force */
static int          g_save_check_known;
static char         g_save_name[BW_MOD_SAVE_NAME_MAX] = "pokeblack";

/* ------------------------------------------------------------------ */
/* The reports                                                         */
/* ------------------------------------------------------------------ */

/*
 * The port's reports, the shape the documents give:
 *
 *   eidos: mod <ns>: state version <n> is newer than <m>
 *   eidos: mod <ns>: state file is damaged
 *
 * A mod's namespace is a name the port chose, so it is safe to print.
 */
static void report(const char *ns, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

static void report(const char *ns, const char *fmt, ...)
{
    va_list ap;

    if (ns == NULL || ns[0] == '\0') {
        ns = "?";
    }
    fprintf(stderr, "eidos: mod %s: ", ns);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
}

/* ------------------------------------------------------------------ */
/* The checksum                                                        */
/* ------------------------------------------------------------------ */

/*
 * The payload's checksum: the CRC-32 every host library computes (the
 * reflected IEEE polynomial 0xEDB88320, the one zlib and PNG use).  The
 * port's own choice, and the value the header's payload_crc holds.
 */
static u32 g_crc_table[256];
static int g_crc_table_ready;

static void crc_table_build(void)
{
    u32 i;

    for (i = 0; i < 256u; i++) {
        u32 c = i;
        int b;

        for (b = 0; b < 8; b++) {
            c = (c & 1u) != 0u ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
        g_crc_table[i] = c;
    }
    g_crc_table_ready = 1;
}

u32 bw_mod_savedata_crc(const void *data, u32 size)
{
    const u8 *p = (const u8 *)data;
    u32 crc = 0xFFFFFFFFu;
    u32 i;

    if (p == NULL) {
        return 0;
    }
    if (!g_crc_table_ready) {
        crc_table_build();
    }
    for (i = 0; i < size; i++) {
        crc = g_crc_table[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

/* ------------------------------------------------------------------ */
/* The names                                                           */
/* ------------------------------------------------------------------ */

/*
 * The names Windows reserves for a device: a folder cannot be called one
 * of them, so a namespace that is one of them would fail on the disk.
 */
static const char *const g_reserved[] = {
    "CON", "PRN", "AUX", "NUL",
    "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
    "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"
};

/*
 * Is the name one the host reserves for a device?  The device name is the
 * part before the first dot: "CON.txt" is CON.  The rule is the manifest
 * layer's (sdk/mod_manifest.c, bw_mod_manifest_namespace_valid), so the
 * two gates agree on every name.
 */
static int name_is_reserved(const char *ns)
{
    size_t i;
    size_t stem_len = 0;

    while (ns[stem_len] != '\0' && ns[stem_len] != '.') {
        stem_len++;
    }
    for (i = 0; i < sizeof g_reserved / sizeof g_reserved[0]; i++) {
        const char *r = g_reserved[i];
        size_t j;

        if (strlen(r) != stem_len) {
            continue;
        }
        for (j = 0; j < stem_len; j++) {
            char a = ns[j];
            char b = r[j];

            if (a >= 'a' && a <= 'z') {
                a = (char)(a - 'a' + 'A');
            }
            if (b >= 'a' && b <= 'z') {
                b = (char)(b - 'a' + 'A');
            }
            if (a != b) {
                break;
            }
        }
        if (j == stem_len) {
            return 1;
        }
    }
    return 0;
}

/*
 * A namespace is one path component: it must not be empty, must not
 * carry a separator or a "." or ".." component, must not be a reserved
 * device name, and must stay inside the small character set the port
 * allows (docs/mod-state.md, section 1).  The manifest layer validates
 * the name too and falls back to a derived one; this is the second gate,
 * the one that stands between a bad name and the disk, and it keeps the
 * manifest layer's rules (the same length cap, the same character set,
 * the same device names).
 */
static int namespace_ok(const char *ns)
{
    size_t i;
    size_t n;

    if (ns == NULL || ns[0] == '\0') {
        return 0;
    }
    n = strlen(ns);
    if (n > BW_MOD_NS_MAX) {
        return 0;
    }
    if (strcmp(ns, ".") == 0 || strcmp(ns, "..") == 0) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        char c = ns[i];

        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') {
            continue;
        }
        return 0;
    }
    return !name_is_reserved(ns);
}

/* The save name is the port's own: letters, digits, dash and underscore. */
static int save_name_ok(const char *name)
{
    size_t i;
    size_t n;

    if (name == NULL || name[0] == '\0') {
        return 0;
    }
    n = strlen(name);
    if (n >= BW_MOD_SAVE_NAME_MAX) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        char c = name[i];

        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_') {
            continue;
        }
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* The paths                                                           */
/* ------------------------------------------------------------------ */

/* <save dir>/mods/<ns> */
static int state_dir_path(char *out, size_t out_size, const char *ns)
{
    char rel[256];
    int n = snprintf(rel, sizeof rel, "mods/%s", ns);

    if (n < 0 || (size_t)n >= sizeof rel) {
        return -1;
    }
    return bw_save_path(out, out_size, rel);
}

/* <save dir>/mods/<ns>/<save>.bin */
static int state_file_path(char *out, size_t out_size, const char *ns)
{
    char rel[256];
    int n = snprintf(rel, sizeof rel, "mods/%s/%s.bin", ns, g_save_name);

    if (n < 0 || (size_t)n >= sizeof rel) {
        return -1;
    }
    return bw_save_path(out, out_size, rel);
}

/* ------------------------------------------------------------------ */
/* The records                                                         */
/* ------------------------------------------------------------------ */

static BwModSaveRec *rec_find(const char *ns)
{
    u32 i;

    for (i = 0; i < g_mod_count; i++) {
        if (strcmp(g_mods[i].ns, ns) == 0) {
            return &g_mods[i];
        }
    }
    return NULL;
}

/*
 * The record for a namespace, made when it is not there yet.  NULL when
 * the table is full: the port serves a handful of mods, and a mod that
 * does not fit keeps no state (its calls report a failure).
 */
static BwModSaveRec *rec_get(const char *ns)
{
    BwModSaveRec *rec = rec_find(ns);

    if (rec != NULL) {
        return rec;
    }
    if (g_mod_count >= BW_MOD_SAVEDATA_MAX_MODS) {
        report(ns, "too many mods keep a state; this one's is not kept");
        return NULL;
    }
    rec = &g_mods[g_mod_count++];
    memset(rec, 0, sizeof *rec);
    memcpy(rec->ns, ns, strlen(ns) + 1);
    return rec;
}

/* The record of the mod that is calling, or NULL. */
static BwModSaveRec *current_rec(void)
{
    if (g_current < 0 || (u32)g_current >= g_mod_count) {
        return NULL;
    }
    return &g_mods[g_current];
}

/* ------------------------------------------------------------------ */
/* The files                                                           */
/* ------------------------------------------------------------------ */

/* The length of a file, or -1 when it cannot be opened. */
static long file_length(const char *path)
{
    FILE *f;
    long n;

    f = fopen(path, "rb");
    if (f == NULL) {
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    n = ftell(f);
    fclose(f);
    return n;
}

/* Read `size` bytes from `offset` in a file.  0 on success. */
static int read_at(const char *path, u8 *buf, u32 size, u32 offset)
{
    FILE *f;
    size_t got;

    if (size == 0) {
        return 0;
    }
    f = fopen(path, "rb");
    if (f == NULL) {
        return -1;
    }
    if (fseek(f, (long)offset, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }
    got = fread(buf, 1, size, f);
    fclose(f);
    return got == (size_t)size ? 0 : -1;
}

/* The header, packed little-endian into 24 bytes. */
static void header_pack(u8 *out, const BwModStateHeader *h)
{
    bw_wr32(out + 0, h->magic);
    bw_wr16(out + 4, h->version);
    bw_wr16(out + 6, h->flags);
    bw_wr32(out + 8, h->payload_size);
    bw_wr32(out + 12, h->payload_crc);
    bw_wr32(out + 16, h->api_written);
    bw_wr32(out + 20, h->save_check);
}

/* The header, read out of 24 bytes. */
static void header_unpack(const u8 *in, BwModStateHeader *h)
{
    h->magic = bw_rd32(in + 0);
    h->version = bw_rd16(in + 4);
    h->flags = bw_rd16(in + 6);
    h->payload_size = bw_rd32(in + 8);
    h->payload_crc = bw_rd32(in + 12);
    h->api_written = bw_rd32(in + 16);
    h->save_check = bw_rd32(in + 20);
}

/*
 * Move a file aside and keep it: the port never deletes a mod's state
 * (docs/mod-state.md, section 1), so a file it cannot read is moved
 * rather than removed and a player can recover it.  The moved file is
 * named <file><suffix>; a name that is taken gets a number, so no moved
 * file is ever lost.
 */
static int move_aside(const char *path, const char *suffix,
                      char *out, size_t out_size)
{
    u32 n = 0;

    for (;;) {
        char name[BW_MOD_SAVEDATA_PATH_MAX];
        int len;

        if (n == 0) {
            len = snprintf(name, sizeof name, "%s%s", path, suffix);
        } else {
            len = snprintf(name, sizeof name, "%s%s.%u", path, suffix,
                           (unsigned int)n);
        }
        if (len < 0 || (size_t)len >= sizeof name) {
            return -1;
        }
        if (file_length(name) < 0) {
            if (rename(path, name) != 0) {
                /* The name is free but the move failed: try once more
                   with the name removed, in case the platform refuses to
                   replace a file it thinks is there. */
                remove(name);
                if (rename(path, name) != 0) {
                    return -1;
                }
            }
            if (out != NULL && out_size > 0) {
                snprintf(out, out_size, "%s", name);
            }
            return 0;
        }
        n++;
        if (n > 1000u) {
            return -1;
        }
    }
}

/*
 * The file is damaged: report it and move it aside, so the mod starts
 * from its defaults and the player keeps the old bytes.
 */
static void damaged(BwModSaveRec *rec, const char *path, const char *reason)
{
    char moved[BW_MOD_SAVEDATA_PATH_MAX];

    report(rec->ns, "state file is damaged");
    if (move_aside(path, ".bad", moved, sizeof moved) == 0) {
        report(rec->ns, "the state file was moved aside to %s (%s)", moved,
               reason);
    } else {
        report(rec->ns, "the state file is damaged (%s) and could not be "
               "moved aside", reason);
    }
}

/* ------------------------------------------------------------------ */
/* The writing                                                         */
/* ------------------------------------------------------------------ */

/*
 * Write the whole file: the port's header and the mod's payload, in one
 * atomic write (sdk/save.h).  The folder is created by the write
 * itself, so the first store creates the file and its folder.
 */
static int write_state(BwModSaveRec *rec, const void *payload, u32 size,
                        u32 save_check)
{
    BwModStateHeader h;
    char path[BW_MOD_SAVEDATA_PATH_MAX];
    u8 *blob;
    int rc;

    if (state_file_path(path, sizeof path, rec->ns) != 0) {
        if (!rec->no_folder_reported) {
            rec->no_folder_reported = 1;
            report(rec->ns, "no save folder; the mod's data is not kept");
        }
        return -1;
    }
    h.magic = BW_MOD_STATE_MAGIC;
    h.version = (u16)BW_MOD_STATE_VERSION;
    h.flags = 0;
    h.payload_size = size;
    h.payload_crc = bw_mod_savedata_crc(payload, size);
    h.api_written = rec->api;
    h.save_check = save_check;
    blob = (u8 *)malloc((size_t)BW_MOD_STATE_HEADER_SIZE + (size_t)size);
    if (blob == NULL) {
        report(rec->ns, "the state could not be written (out of memory)");
        return -1;
    }
    header_pack(blob, &h);
    if (size > 0) {
        memcpy(blob + BW_MOD_STATE_HEADER_SIZE, payload, size);
    }
    rc = bw_save_write_atomic(path, blob,
                               BW_MOD_STATE_HEADER_SIZE + (u32)size);
    free(blob);
    if (rc != 0) {
        if (!rec->store_reported) {
            rec->store_reported = 1;
            report(rec->ns, "the state file could not be written; the old "
                   "one is kept");
        }
        return -1;
    }
    return 0;
}

/* Keep the payload a save handoff is holding for a mod. */
static int stage(BwModSaveRec *rec, const void *payload, u32 size)
{
    if (size > rec->staged_cap) {
        u8 *grown = (u8 *)realloc(rec->staged, size > 0 ? size : 1);

        if (grown == NULL) {
            return -1;
        }
        rec->staged = grown;
        rec->staged_cap = size;
    }
    if (size > 0) {
        memcpy(rec->staged, payload, size);
    }
    rec->staged_size = size;
    rec->staged_valid = 1;
    return 0;
}

/* Forget the payload a save handoff was holding for a mod. */
static void drop_staged(BwModSaveRec *rec)
{
    rec->staged_size = 0;
    rec->staged_valid = 0;
}

/* ------------------------------------------------------------------ */
/* The port's seam                                                     */
/* ------------------------------------------------------------------ */

/*
 * The mod that is calling.  The port knows which mod it is serving and
 * the layer does not: the loader sets the namespace around every call it
 * makes into a mod, so a mod's state call always names its own file.
 * NULL or "" clears it.
 */
void bw_mod_savedata_set_namespace(const char *ns)
{
    BwModSaveRec *rec;

    if (ns == NULL || ns[0] == '\0') {
        g_current = -1;
        return;
    }
    if (!namespace_ok(ns)) {
        report(ns, "the state namespace is not a usable name; the mod's "
               "data is not kept");
        g_current = -1;
        return;
    }
    rec = rec_get(ns);
    if (rec == NULL) {
        g_current = -1;
        return;
    }
    g_current = (int)(rec - g_mods);
}

/* The namespace of the mod that is calling, or "" when there is none. */
const char *bw_mod_savedata_namespace(void)
{
    if (g_current < 0) {
        return "";
    }
    return g_mods[g_current].ns;
}

/*
 * The manifest api in force for the mod that is calling: the header's
 * api_written.  The port reports a mismatch to the mod at load and the
 * mod decides what to do with it (docs/mod-state.md, section 5).
 */
void bw_mod_savedata_set_api(u32 api)
{
    BwModSaveRec *rec = current_rec();

    if (rec == NULL) {
        return;
    }
    rec->api = api;
}

/*
 * The game save the state belongs to: the name of the save file the
 * port is running ("pokeblack" for pokeblack.sav).  The state file is
 * named after it, so a player who keeps several game saves gets one
 * state file per save.
 */
void bw_mod_savedata_set_save(const char *name)
{
    if (!save_name_ok(name)) {
        report(bw_mod_savedata_namespace(),
               "the game save's name is not usable; the default stands");
        return;
    }
    snprintf(g_save_name, sizeof g_save_name, "%s", name);
}

/*
 * The checksum of the game save in force: the header's save_check.  The
 * port owns the checksum (it is a checksum of the game save written in
 * the same step); the layer only carries it and compares it.
 */
void bw_mod_savedata_set_save_check(u32 check)
{
    g_save_check = check;
    g_save_check_known = 1;
}

/* The checksum of the game save in force. */
u32 bw_mod_savedata_save_check(void)
{
    return g_save_check;
}

/*
 * Whether the mod's state was written with the game save in force.
 *
 * 1 when the state file's save_check is the checksum of the save in
 * force, 0 when they differ (the game save was written without the mod:
 * an older backup, or a save made while the mod was disabled), and -1
 * when the question cannot be answered (no state file, or the port has
 * not told the layer which save is in force).
 */
int bw_mod_savedata_in_step(void)
{
    BwModStateHeader h;

    if (!g_save_check_known) {
        return -1;
    }
    if (bw_mod_savedata_info(&h) != 0) {
        return -1;
    }
    return h.save_check == g_save_check ? 1 : 0;
}

/*
 * The save handoff.  The port's save.begin asks the mod for its payload
 * (the mod's handler calls state_store) and the port's save.done writes
 * the file with the new game save's checksum.  Between the two the
 * layer holds the payload and does not write anything, so a game save
 * that fails leaves the old state file in place and the two never drift
 * apart (docs/mod-state.md, section 2).
 */
void bw_mod_savedata_save_begin(void)
{
    g_save_pending = 1;
}

/*
 * The port's save.done.  `ok` is the game save's own answer: with it the
 * held payloads are written, each with the new save's checksum; without
 * it they are dropped and the old state files stay.  The return value
 * is 0 when every held payload was written, -1 otherwise.
 */
int bw_mod_savedata_save_done(int ok, u32 check)
{
    u32 i;
    int failed = 0;

    g_save_pending = 0;
    if (!ok) {
        for (i = 0; i < g_mod_count; i++) {
            drop_staged(&g_mods[i]);
        }
        return 0;
    }
    g_save_check = check;
    g_save_check_known = 1;
    for (i = 0; i < g_mod_count; i++) {
        BwModSaveRec *rec = &g_mods[i];

        if (!rec->staged_valid) {
            continue;
        }
        if (write_state(rec, rec->staged, rec->staged_size, check) != 0) {
            failed = 1;
        }
        drop_staged(rec);
    }
    return failed ? -1 : 0;
}

/*
 * Forget everything the layer holds: at power-on and at every reset
 * (bw_port_reset).  The files on the disk are untouched, because a mod's
 * state belongs to the save and not to the run: a reset does not rewind
 * a run the player may go on to.
 */
void bw_mod_savedata_reset(void)
{
    u32 i;

    for (i = 0; i < g_mod_count; i++) {
        free(g_mods[i].staged);
        g_mods[i].staged = NULL;
        g_mods[i].staged_size = 0;
        g_mods[i].staged_cap = 0;
        g_mods[i].staged_valid = 0;
    }
    g_mod_count = 0;
    g_current = -1;
    g_save_pending = 0;
}

/* ------------------------------------------------------------------ */
/* The five calls                                                      */
/* ------------------------------------------------------------------ */

/*
 * Read the mod's payload.  The number of bytes read, or -1 when the file
 * is there and cannot be used (damaged, or written by a newer port, in
 * which case it is moved aside and the mod starts from its defaults).
 * An absent file is 0 bytes: the port writes no empty file at load, so
 * an absent file and an empty one are the same to a mod.
 */
s32 bw_mod_savedata_load(void *buf, u32 size)
{
    BwModSaveRec *rec = current_rec();
    BwModStateHeader h;
    char path[BW_MOD_SAVEDATA_PATH_MAX];
    u8 head[BW_MOD_STATE_HEADER_SIZE];
    u8 *payload;
    u32 want;
    long len;
    s32 got;

    if (rec == NULL || buf == NULL) {
        return -1;
    }
    if (state_file_path(path, sizeof path, rec->ns) != 0) {
        if (!rec->no_folder_reported) {
            rec->no_folder_reported = 1;
            report(rec->ns, "no save folder; the mod's data is not kept");
        }
        return -1;
    }
    len = file_length(path);
    if (len < 0) {
        return 0;               /* no file: the mod starts from its defaults */
    }
    if (len == 0) {
        return 0;               /* an empty file is nothing, like no file */
    }
    if ((u32)len < BW_MOD_STATE_HEADER_SIZE) {
        damaged(rec, path, "the file is shorter than the header");
        return -1;
    }
    if (read_at(path, head, sizeof head, 0) != 0) {
        damaged(rec, path, "the header cannot be read");
        return -1;
    }
    header_unpack(head, &h);
    if (h.magic != BW_MOD_STATE_MAGIC) {
        damaged(rec, path, "the magic is wrong");
        return -1;
    }
    if (h.version > BW_MOD_STATE_VERSION) {
        /*
         * A newer file is not read: reading a newer layout would
         * misinterpret the payload.  It is moved aside, so a newer port
         * can read it again.
         */
        char moved[BW_MOD_SAVEDATA_PATH_MAX];
        char suffix[32];

        snprintf(suffix, sizeof suffix, ".v%u", (unsigned int)h.version);
        report(rec->ns, "state version %u is newer than %u",
               (unsigned int)h.version, (unsigned int)BW_MOD_STATE_VERSION);
        if (move_aside(path, suffix, moved, sizeof moved) == 0) {
            report(rec->ns, "the state file was moved aside to %s", moved);
        }
        return -1;
    }
    if (h.payload_size > (u32)len - BW_MOD_STATE_HEADER_SIZE) {
        if (h.version < BW_MOD_STATE_VERSION) {
            /*
             * An older file whose payload cannot be located: the old
             * header does not say where the payload is, so the file is
             * moved aside and the mod starts fresh.
             */
            char moved[BW_MOD_SAVEDATA_PATH_MAX];
            char suffix[32];

            snprintf(suffix, sizeof suffix, ".v%u",
                     (unsigned int)h.version);
            report(rec->ns, "state version %u cannot be read",
                   (unsigned int)h.version);
            if (move_aside(path, suffix, moved, sizeof moved) == 0) {
                report(rec->ns, "the state file was moved aside to %s", moved);
            }
            return -1;
        }
        damaged(rec, path, "the payload size exceeds the file");
        return -1;
    }
    if (h.payload_size > BW_MOD_SAVEDATA_MAX_PAYLOAD) {
        damaged(rec, path, "the payload is too large");
        return -1;
    }
    want = h.payload_size;
    payload = (u8 *)malloc(want > 0 ? want : 1);
    if (payload == NULL) {
        report(rec->ns, "the state could not be read (out of memory)");
        return -1;
    }
    if (want > 0 &&
        read_at(path, payload, want, BW_MOD_STATE_HEADER_SIZE) != 0) {
        free(payload);
        damaged(rec, path, "the payload cannot be read");
        return -1;
    }
    if (bw_mod_savedata_crc(payload, want) != h.payload_crc) {
        free(payload);
        damaged(rec, path, "the payload's checksum does not match");
        return -1;
    }
    /*
     * The bytes read are the bytes the mod can take: a payload larger
     * than the caller's buffer is cut short, and the mod reads the whole
     * payload's size through state_info.
     */
    got = (s32)(want < size ? want : size);
    if (got > 0) {
        memcpy(buf, payload, (size_t)got);
    }
    free(payload);
    return got;
}

/*
 * Write the mod's payload.  0 on success, -1 on failure; a failed store
 * leaves the old file in place.  While a save handoff is in progress the
 * payload is held and written when the handoff ends, so the state file
 * and the game save are written in the same step.
 */
int bw_mod_savedata_store(const void *buf, u32 size)
{
    BwModSaveRec *rec = current_rec();

    if (rec == NULL) {
        return -1;
    }
    if (buf == NULL && size != 0) {
        return -1;
    }
    if (size > BW_MOD_SAVEDATA_MAX_PAYLOAD) {
        report(rec->ns, "the payload is too large (%u bytes); the state was "
               "not written", (unsigned int)size);
        return -1;
    }
    if (g_save_pending) {
        if (stage(rec, buf, size) != 0) {
            report(rec->ns, "the payload could not be held; the state was "
                   "not written");
            return -1;
        }
        return 0;
    }
    return write_state(rec, buf, size, g_save_check);
}

/*
 * Clear the mod's state: the state file goes and the folder stays.  The
 * port does not delete a mod's folder when the mod is removed, so a
 * re-enable reads the file again (docs/mod-state.md, section 1).
 */
int bw_mod_savedata_clear(void)
{
    BwModSaveRec *rec = current_rec();
    char path[BW_MOD_SAVEDATA_PATH_MAX];

    if (rec == NULL) {
        return -1;
    }
    if (state_file_path(path, sizeof path, rec->ns) != 0) {
        if (!rec->no_folder_reported) {
            rec->no_folder_reported = 1;
            report(rec->ns, "no save folder; the mod's data is not kept");
        }
        return -1;
    }
    if (remove(path) != 0) {
        if (file_length(path) < 0) {
            drop_staged(rec);
            return 0;       /* there was no state file: the goal holds */
        }
        report(rec->ns, "the state file could not be removed");
        return -1;
    }
    drop_staged(rec);
    return 0;
}

/*
 * The header fields of the mod's state file, so the mod can migrate its
 * payload when the version or the api in force changed
 * (docs/mod-state.md, section 5).  0 on success, -1 when there is no
 * state file to describe.
 */
int bw_mod_savedata_info(BwModStateHeader *out)
{
    BwModSaveRec *rec = current_rec();
    BwModStateHeader h;
    char path[BW_MOD_SAVEDATA_PATH_MAX];
    u8 head[BW_MOD_STATE_HEADER_SIZE];
    long len;

    if (rec == NULL || out == NULL) {
        return -1;
    }
    if (state_file_path(path, sizeof path, rec->ns) != 0) {
        return -1;
    }
    len = file_length(path);
    if (len <= 0 || (u32)len < BW_MOD_STATE_HEADER_SIZE) {
        return -1;
    }
    if (read_at(path, head, sizeof head, 0) != 0) {
        return -1;
    }
    header_unpack(head, &h);
    if (h.magic != BW_MOD_STATE_MAGIC) {
        return -1;
    }
    if (h.version > BW_MOD_STATE_VERSION) {
        return -1;
    }
    *out = h;
    return 0;
}

/*
 * The mod's own folder: <save dir>/mods/<namespace>.  A mod that keeps
 * files of its own asks for the path here and writes them with the same
 * whole-file discipline (docs/mod-state.md, section 1).  The folder is
 * made if it is not there yet.
 */
int bw_mod_savedata_dir(char *out, u32 out_size)
{
    BwModSaveRec *rec = current_rec();
    char path[BW_MOD_SAVEDATA_PATH_MAX];
    size_t n;

    if (rec == NULL || out == NULL || out_size == 0) {
        return -1;
    }
    if (state_dir_path(path, sizeof path, rec->ns) != 0) {
        if (!rec->no_folder_reported) {
            rec->no_folder_reported = 1;
            report(rec->ns, "no save folder; the mod's data is not kept");
        }
        return -1;
    }
    if (bw_plat_mkdirs(path) != 0) {
        report(rec->ns, "the state folder could not be made");
        return -1;
    }
    n = strlen(path);
    if (n + 1 > (size_t)out_size) {
        return -1;
    }
    memcpy(out, path, n + 1);
    return 0;
}
