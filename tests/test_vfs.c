/*
 * tests/test_vfs.c -- the file layer, synthetically.
 *
 * No game data and no oracle: a small tree is written under the current
 * directory (the ctest build directory), with a base root and a mod root
 * that disagree about one file, and every expectation is about which copy
 * the resolver picks and whether the resolved path really reads back.
 *
 * What is checked:
 *
 *   - with no mod root, a path resolves under the base root;
 *   - with a mod root, a file present in both resolves to the overlay copy
 *     and reads back as the overlay's bytes;
 *   - a file present only in the base falls through to the base;
 *   - a file present only in the overlay resolves to the overlay;
 *   - bw_data_exists agrees with the resolution, absent file included;
 *   - clearing the mod root restores base-only resolution;
 *   - a trailing separator on a root is stripped, so resolution does not
 *     produce a doubled slash.
 *
 * What is NOT tested, because it does not exist: overriding one member
 * inside a NARC.  The overlay is file granular; see README.md, "The file
 * layer".
 */

#include <stdio.h>
#include <string.h>

#include "port.h"
#include "port_test.h"

#ifdef _WIN32
#include <direct.h>
#define BW_MKDIR(p) _mkdir(p)
#define BW_RMDIR(p) _rmdir(p)
#else
#include <sys/stat.h>
#include <sys/types.h>
#define BW_MKDIR(p) mkdir((p), 0777)
#define BW_RMDIR(p) rmdir(p)
#endif

#define T_ROOT "vfs_test_tmp"
#define T_BASE T_ROOT "/base"
#define T_MOD  T_ROOT "/mod"

static int write_file(const char *path, const char *text)
{
    FILE *fp = fopen(path, "wb");

    if (fp == NULL) {
        return -1;
    }
    fputs(text, fp);
    fclose(fp);
    return 0;
}

/* Reads a whole file and returns 1 if it is exactly `text`, else 0. */
static int file_is(const char *path, const char *text)
{
    char buf[64];
    size_t n;
    FILE *fp = fopen(path, "rb");

    if (fp == NULL) {
        return 0;
    }
    n = fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);
    buf[n] = '\0';
    return strcmp(buf, text) == 0;
}

static int starts_with(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

static void build_tree(void)
{
    BW_RMDIR(T_BASE);
    BW_RMDIR(T_MOD);
    BW_RMDIR(T_ROOT);

    BW_MKDIR(T_ROOT);
    BW_MKDIR(T_BASE);
    BW_MKDIR(T_MOD);

    write_file(T_BASE "/shared.txt", "base");
    write_file(T_BASE "/only_base.txt", "b");
    write_file(T_MOD "/shared.txt", "mod");
    write_file(T_MOD "/only_mod.txt", "m");
}

static void clean_tree(void)
{
    remove(T_BASE "/shared.txt");
    remove(T_BASE "/only_base.txt");
    remove(T_MOD "/shared.txt");
    remove(T_MOD "/only_mod.txt");
    BW_RMDIR(T_BASE);
    BW_RMDIR(T_MOD);
    BW_RMDIR(T_ROOT);
}

int main(void)
{
    char path[1200];

    build_tree();

    printf("base only (no mod root)\n");
    bw_set_data_root(T_BASE);
    bw_set_mod_root("");
    BW_REQUIRE("mod root is empty", bw_mod_root()[0] == '\0');

    BW_CHECK_EQ("resolve succeeds", bw_data_path(path, sizeof(path), "shared.txt"), 0);
    BW_CHECK_EQ("resolves under the base root", starts_with(path, T_BASE), 1);
    BW_CHECK_EQ("the base copy reads back", file_is(path, "base"), 1);
    BW_CHECK_EQ("exists: base file", bw_data_exists("shared.txt"), 1);
    BW_CHECK_EQ("exists: absent file", bw_data_exists("absent.txt"), 0);

    printf("mod overlay on\n");
    bw_set_mod_root(T_MOD);
    BW_REQUIRE("mod root is set", bw_mod_root()[0] != '\0');

    BW_CHECK_EQ("shared resolves", bw_data_path(path, sizeof(path), "shared.txt"), 0);
    BW_CHECK_EQ("overlay copy wins", starts_with(path, T_MOD), 1);
    BW_CHECK_EQ("the overlay copy reads back", file_is(path, "mod"), 1);

    BW_CHECK_EQ("base-only resolves", bw_data_path(path, sizeof(path), "only_base.txt"), 0);
    BW_CHECK_EQ("base-only falls through", starts_with(path, T_BASE), 1);
    BW_CHECK_EQ("the base-only copy reads back", file_is(path, "b"), 1);

    BW_CHECK_EQ("overlay-only resolves", bw_data_path(path, sizeof(path), "only_mod.txt"), 0);
    BW_CHECK_EQ("overlay-only uses the overlay", starts_with(path, T_MOD), 1);
    BW_CHECK_EQ("exists: overlay file", bw_data_exists("only_mod.txt"), 1);
    BW_CHECK_EQ("exists: base file still found", bw_data_exists("only_base.txt"), 1);

    printf("trailing separator on a root\n");
    bw_set_mod_root(T_MOD "/");
    BW_CHECK_EQ("shared still resolves to the overlay",
                bw_data_path(path, sizeof(path), "shared.txt") == 0
                && starts_with(path, T_MOD), 1);

    printf("mod root cleared\n");
    bw_set_mod_root("");
    BW_CHECK_EQ("only_mod now resolves under the base",
                bw_data_path(path, sizeof(path), "only_mod.txt") == 0
                && starts_with(path, T_BASE), 1);
    BW_CHECK_EQ("and it no longer exists", bw_data_exists("only_mod.txt"), 0);

    clean_tree();
    BW_REPORT("test_vfs");
}
