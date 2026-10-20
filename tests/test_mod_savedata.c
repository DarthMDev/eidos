/*
 * tests/test_mod_savedata.c -- a mod's own saved data: the envelope, the
 * versions and the write discipline.
 *
 * The layer is sdk/mod_savedata.c; the contract it keeps is the one of
 * docs/mod-state.md, sections 1, 2 and 5:
 *
 *   - the file is <save dir>/mods/<namespace>/<save>.bin: one file per mod,
 *     one file per game save, the port owns the header and the mod owns the
 *     bytes after it;
 *   - the envelope round trip: a stored payload reads back byte for byte,
 *     and the header carries the magic, the version, the payload's size and
 *     checksum, the api in force and the game save's checksum;
 *   - a file older than the port's is read with its own payload size and
 *     rewritten in the current version at the next store;
 *   - a file newer than the port's is not read: it is moved aside, so a
 *     newer port can read it again, and the mod starts from its defaults;
 *   - a file whose checksum does not match is damaged: it is moved aside
 *     and the mod starts from its defaults;
 *   - a store writes the whole file: the old bytes do not stay behind, and
 *     no temporary is left over;
 *   - the folder belongs to the mod and the clear takes the file, not the
 *     folder;
 *   - a namespace that is not one path component never reaches the disk.
 *
 * Nothing here needs game data: the save folder is a directory in the test's
 * own working directory, set with bw_set_save_dir (sdk/save.h), the way
 * tests/test_vfs.c writes its own tree.
 */

#include <stdio.h>
#include <string.h>

#include "mod_state.h"
#include "save.h"
#include "types.h"

#include "port_test.h"

#ifdef _WIN32
#include <direct.h>
#define BW_RMDIR(p) _rmdir(p)
#else
#include <unistd.h>
#define BW_RMDIR(p) rmdir(p)
#endif

#ifdef _WIN32
#include <direct.h>
#include <sys/stat.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

/*
 * The saved data layer's own calls (sdk/mod_savedata.c).  That file holds
 * its prototypes until the header which will carry them for the rest of the
 * port is written, so the test declares the ones it uses; they are the ones
 * the file's own header comment names.
 */
void        bw_mod_savedata_set_namespace(const char *ns);
const char *bw_mod_savedata_namespace(void);
void        bw_mod_savedata_set_api(u32 api);
void        bw_mod_savedata_set_save(const char *name);
void        bw_mod_savedata_set_save_check(u32 check);
u32         bw_mod_savedata_save_check(void);
int         bw_mod_savedata_in_step(void);
s32         bw_mod_savedata_load(void *buf, u32 size);
int         bw_mod_savedata_store(const void *buf, u32 size);
int         bw_mod_savedata_clear(void);
int         bw_mod_savedata_info(BwModStateHeader *out);
int         bw_mod_savedata_dir(char *out, u32 out_size);
u32         bw_mod_savedata_crc(const void *data, u32 size);

/* The save folder the test owns, under the test's own working directory. */
#define T_SAVE_DIR "mod_savedata_tmp"

/* The namespace the test's mod keeps its state in. */
#define T_NS "testmod"

/* The game save the state belongs to. */
#define T_SAVE "pokeblack"

/* The checksum of the game save in force, for the header's save_check. */
#define T_SAVE_CHECK 0x12345678u

/* ------------------------------------------------------------------ */
/* The files                                                           */
/* ------------------------------------------------------------------ */

/* <save dir>/mods/<ns>/<save>.bin, with an optional suffix. */
static int state_path(char *out, size_t size, const char *suffix)
{
    char rel[256];
    int n = snprintf(rel, sizeof rel, "mods/%s/%s.bin%s", T_NS, T_SAVE,
                     suffix != NULL ? suffix : "");

    if (n < 0 || (size_t)n >= sizeof rel) {
        return -1;
    }
    return bw_save_path(out, size, rel);
}

/* The length of a file, or -1 when it is not there. */
static long file_length(const char *path)
{
    FILE *fp;
    long n;

    fp = fopen(path, "rb");
    if (fp == NULL) {
        return -1;
    }
    fseek(fp, 0, SEEK_END);
    n = ftell(fp);
    fclose(fp);
    return n;
}

/* Whether a file is there. */
static int file_exists(const char *path)
{
    return file_length(path) >= 0;
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

/*
 * Write a state file by hand: the port's 24-byte header, little-endian the
 * way the port writes it, and the payload.  The test writes the older, the
 * newer and the damaged files the port has to cope with.
 */
static int write_state(const char *path, u16 version, u32 payload_size,
                       u32 crc, u32 api_written, u32 save_check,
                       const void *payload, u32 payload_len)
{
    u8 head[BW_MOD_STATE_HEADER_SIZE];
    FILE *fp;

    memset(head, 0, sizeof head);
    bw_wr32(head + 0, BW_MOD_STATE_MAGIC);
    bw_wr16(head + 4, version);
    bw_wr16(head + 6, 0);
    bw_wr32(head + 8, payload_size);
    bw_wr32(head + 12, crc);
    bw_wr32(head + 16, api_written);
    bw_wr32(head + 20, save_check);
    fp = fopen(path, "wb");
    if (fp == NULL) {
        return -1;
    }
    if (fwrite(head, 1, sizeof head, fp) != sizeof head) {
        fclose(fp);
        return -1;
    }
    if (payload_len > 0 && fwrite(payload, 1, payload_len, fp) != payload_len) {
        fclose(fp);
        return -1;
    }
    fclose(fp);
    return 0;
}

/* Take the tree the earlier runs left away. */
static void clean_tree(void)
{
    static const char *const suffixes[] = {
        "", ".bad", ".v0", ".v1", ".v2", ".v3", ".tmp"
    };
    char path[1200];
    size_t i;

    for (i = 0; i < sizeof suffixes / sizeof suffixes[0]; i++) {
        if (state_path(path, sizeof path, suffixes[i]) == 0) {
            remove(path);
        }
    }
    /* The mod's own folder, and the folders above it. */
    {
        static const char *const folders[] = {
            "mods/" T_NS, "mods", ""
        };
        size_t f;

        for (f = 0; f < sizeof folders / sizeof folders[0]; f++) {
            if (bw_save_path(path, sizeof path, folders[f]) == 0) {
                BW_RMDIR(path);
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* The test                                                            */
/* ------------------------------------------------------------------ */

int main(void)
{
    BwModStateHeader h;
    char path[1200];
    char moved[1200];
    char suffix[32];
    char dir[1200];
    u8 buf[256];
    u8 big[100];
    const char *payload = "the mod's own bytes";
    u32 n = (u32)strlen(payload);
    u32 crc;
    u16 version;

    bw_set_save_dir(T_SAVE_DIR);
    bw_mod_savedata_set_save(T_SAVE);
    bw_mod_savedata_set_namespace(T_NS);
    bw_mod_savedata_set_api(BW_MOD_API_VERSION);
    clean_tree();

    printf("no state yet\n");
    BW_CHECK_EQ("an absent file is an empty payload",
                (long)bw_mod_savedata_load(buf, (u32)sizeof(buf)), 0);
    BW_CHECK_EQ("and there is no header to describe",
                (long)bw_mod_savedata_info(&h), -1);

    printf("the envelope round trip\n");
    bw_mod_savedata_set_save_check(T_SAVE_CHECK);
    BW_REQUIRE("the store succeeds", bw_mod_savedata_store(payload, n) == 0);
    memset(buf, 0, sizeof buf);
    BW_CHECK_EQ("the load gives the payload's bytes back",
                (long)bw_mod_savedata_load(buf, (u32)sizeof(buf)), (long)n);
    BW_CHECK_EQ("and they are the mod's own bytes",
                memcmp(buf, payload, (size_t)n), 0);

    BW_REQUIRE("the header is there", bw_mod_savedata_info(&h) == 0);
    BW_CHECK_EQ("the magic is the port's", (long)h.magic,
                (long)BW_MOD_STATE_MAGIC);
    BW_CHECK_EQ("the version is the port's own", (long)h.version,
                (long)BW_MOD_STATE_VERSION);
    BW_CHECK_EQ("the flags are zero", (long)h.flags, 0);
    BW_CHECK_EQ("the header says how many payload bytes there are",
                (long)h.payload_size, (long)n);
    crc = bw_mod_savedata_crc(payload, n);
    BW_CHECK_EQ("the header carries the payload's checksum",
                (long)h.payload_crc, (long)crc);
    BW_CHECK_EQ("the header carries the api in force",
                (long)h.api_written, (long)BW_MOD_API_VERSION);
    BW_CHECK_EQ("the header carries the game save's checksum",
                (long)h.save_check, (long)T_SAVE_CHECK);
    BW_CHECK_EQ("the port's checksum is the one in force",
                (long)bw_mod_savedata_save_check(), (long)T_SAVE_CHECK);
    BW_CHECK_EQ("the state was written with the save in force",
                (long)bw_mod_savedata_in_step(), 1);

    BW_REQUIRE("the state file is where the port says",
               state_path(path, sizeof path, "") == 0);
    BW_CHECK_EQ("the file is the header and the payload",
                file_length(path), (long)(BW_MOD_STATE_HEADER_SIZE + n));
    {
        u8 head[BW_MOD_STATE_HEADER_SIZE];
        FILE *fp = fopen(path, "rb");

        BW_REQUIRE("the file reads back", fp != NULL);
        BW_REQUIRE("the header reads back",
                   fread(head, 1, sizeof head, fp) == sizeof head);
        fclose(fp);
        BW_CHECK_EQ("the file carries the port's magic",
                    (long)bw_rd32(head + 0), (long)BW_MOD_STATE_MAGIC);
        BW_CHECK_EQ("and the version the header reported",
                    (long)bw_rd16(head + 4), (long)h.version);
    }
    version = h.version;

    printf("the state is out of step with the save\n");
    bw_mod_savedata_set_save_check(T_SAVE_CHECK + 1u);
    BW_CHECK_EQ("the state was written with another save",
                (long)bw_mod_savedata_in_step(), 0);
    bw_mod_savedata_set_save_check(T_SAVE_CHECK);
    BW_CHECK_EQ("and it is in step again",
                (long)bw_mod_savedata_in_step(), 1);

    printf("a file older than the port's\n");
    BW_REQUIRE("the old file is written",
               write_state(path, (u16)(version - 1u), n, crc,
                           BW_MOD_API_VERSION, T_SAVE_CHECK,
                           payload, n) == 0);
    memset(buf, 0, sizeof buf);
    BW_CHECK_EQ("the old file is read",
                (long)bw_mod_savedata_load(buf, (u32)sizeof(buf)), (long)n);
    BW_CHECK_EQ("and its payload is the mod's own",
                memcmp(buf, payload, (size_t)n), 0);
    BW_CHECK_EQ("the old file was not moved aside",
                file_exists(path), 1);
    BW_REQUIRE("the header describes the old file",
               bw_mod_savedata_info(&h) == 0);
    BW_CHECK_EQ("the old version is in the header", (long)h.version,
                (long)(version - 1u));
    BW_REQUIRE("the store rewrites the file",
               bw_mod_savedata_store(payload, n) == 0);
    BW_REQUIRE("the header describes the new file",
               bw_mod_savedata_info(&h) == 0);
    BW_CHECK_EQ("the file is in the port's version now",
                (long)h.version, (long)version);

    printf("a file newer than the port's\n");
    BW_REQUIRE("the new file is written",
               write_state(path, (u16)(version + 1u), n, crc,
                           BW_MOD_API_VERSION, T_SAVE_CHECK,
                           payload, n) == 0);
    memset(buf, 0, sizeof buf);
    BW_CHECK_EQ("the new file is not read",
                (long)bw_mod_savedata_load(buf, (u32)sizeof(buf)), -1);
    BW_CHECK_EQ("the file is gone", file_exists(path), 0);
    snprintf(suffix, sizeof suffix, ".v%u", (unsigned int)(version + 1u));
    BW_REQUIRE("the moved file's name is known",
               state_path(moved, sizeof moved, suffix) == 0);
    BW_CHECK_EQ("and it is kept", file_exists(moved), 1);
    memset(buf, 0, sizeof buf);
    BW_CHECK_EQ("the mod starts from its defaults",
                (long)bw_mod_savedata_load(buf, (u32)sizeof(buf)), 0);

    printf("a file with a damaged checksum\n");
    BW_REQUIRE("the damaged file is written",
               write_state(path, version, n, crc ^ 0xFFFFu,
                           BW_MOD_API_VERSION, T_SAVE_CHECK,
                           payload, n) == 0);
    memset(buf, 0, sizeof buf);
    BW_CHECK_EQ("the damaged file is not read",
                (long)bw_mod_savedata_load(buf, (u32)sizeof(buf)), -1);
    BW_CHECK_EQ("the file is gone", file_exists(path), 0);
    BW_REQUIRE("the file was moved aside",
               state_path(moved, sizeof moved, ".bad") == 0);
    BW_CHECK_EQ("and it is kept", file_exists(moved), 1);
    memset(buf, 0, sizeof buf);
    BW_CHECK_EQ("the mod starts from its defaults",
                (long)bw_mod_savedata_load(buf, (u32)sizeof(buf)), 0);

    printf("the whole-file write\n");
    memset(big, 0xA5, sizeof big);
    BW_REQUIRE("the long payload is stored",
               bw_mod_savedata_store(big, (u32)sizeof big) == 0);
    BW_REQUIRE("the state file is where the port says",
               state_path(path, sizeof path, "") == 0);
    BW_CHECK_EQ("the file is the header and the long payload",
                file_length(path), (long)(BW_MOD_STATE_HEADER_SIZE + (long)sizeof big));
    BW_REQUIRE("the short payload is stored",
               bw_mod_savedata_store(payload, 3) == 0);
    BW_CHECK_EQ("the file is the header and the short payload",
                file_length(path), (long)(BW_MOD_STATE_HEADER_SIZE + 3));
    {
        u8 head[BW_MOD_STATE_HEADER_SIZE + 3];
        FILE *fp = fopen(path, "rb");

        BW_REQUIRE("the file reads back", fp != NULL);
        BW_REQUIRE("the header and the payload read back",
                   fread(head, 1, sizeof head, fp) == sizeof head);
        fclose(fp);
        BW_CHECK_EQ("the old bytes are gone",
                    memcmp(head + BW_MOD_STATE_HEADER_SIZE, "the", 3), 0);
    }
    BW_REQUIRE("no temporary is left over",
               state_path(moved, sizeof moved, ".tmp") == 0);
    BW_CHECK_EQ("the write left nothing behind", file_exists(moved), 0);

    printf("the folder and the clear\n");
    BW_REQUIRE("the mod's folder is the port's to give",
               bw_mod_savedata_dir(dir, (u32)sizeof dir) == 0);
    BW_CHECK_EQ("the folder is under the save folder",
                strncmp(dir, T_SAVE_DIR "/mods/" T_NS,
                        strlen(T_SAVE_DIR "/mods/" T_NS)), 0);
    BW_REQUIRE("the state is stored again",
               bw_mod_savedata_store(payload, n) == 0);
    BW_REQUIRE("the clear succeeds", bw_mod_savedata_clear() == 0);
    BW_REQUIRE("the state file is where the port says",
               state_path(path, sizeof path, "") == 0);
    BW_CHECK_EQ("the file is gone", file_exists(path), 0);
    BW_CHECK_EQ("and the folder stays", dir_exists(dir), 1);
    memset(buf, 0, sizeof buf);
    BW_CHECK_EQ("the mod starts from its defaults",
                (long)bw_mod_savedata_load(buf, (u32)sizeof(buf)), 0);

    printf("a namespace that is not one path component\n");
    bw_mod_savedata_set_namespace("bad/name");
    BW_CHECK_EQ("the namespace is refused",
                strcmp(bw_mod_savedata_namespace(), ""), 0);
    BW_CHECK_EQ("and the mod's calls fail",
                (long)bw_mod_savedata_load(buf, (u32)sizeof(buf)), -1);
    BW_CHECK_EQ("and nothing is stored",
                (long)bw_mod_savedata_store(payload, n), -1);

    clean_tree();
    BW_REPORT("test_mod_savedata");
}
