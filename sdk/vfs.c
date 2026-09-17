/*
 * sdk/vfs.c -- the file layer: the data root, the optional mod overlay, and
 * the resolution of a NitroFS-relative path to a host path.
 *
 * This is the first piece of the port's data pipeline.  It owns two roots:
 *
 *     data root   the directory tools/ndsrom.py's `extract` wrote, the one
 *                 holding `a/0/0/2` (the player's own extracted ROM)
 *     mod root    an optional overlay, laid at the same relative layout,
 *                 whose readable files shadow the data root's
 *
 * A path resolves to the overlay copy when one exists and to the base copy
 * otherwise, so a mod ships whole replacement files and an absent file falls
 * through.  The lookup happens here and nowhere else; every loader that
 * opens a NitroFS path calls bw_data_path, so there is one resolution.
 *
 * What this is NOT, stated so a later reader does not assume it: the overlay
 * is file granular.  It replaces a whole NitroFS file (a NARC, a raw asset),
 * not one member inside a NARC.  Overriding a single decoded asset inside an
 * archive needs the engine to load the decoded form and repack it (or a
 * member-level side table), neither of which exists yet; see README.md, "The
 * file layer".  No game data is in this file and nothing here interprets a
 * byte of it.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port.h"

/* ------------------------------------------------------------------ */
/* Roots                                                               */
/* ------------------------------------------------------------------ */

static char g_data_root[1024] = "extract/data";
static int  g_data_root_set;

static char g_mod_root[1024];
static int  g_mod_root_set;

static void set_root(char *dst, size_t cap, int *flag,
                     const char *path, int empty_is_clear)
{
    size_t n;

    if (path == NULL || *path == '\0') {
        if (empty_is_clear) {
            dst[0] = '\0';
            *flag = 1;
        }
        return;
    }
    n = strlen(path);
    if (n >= cap) {
        n = cap - 1;
    }
    memcpy(dst, path, n);
    dst[n] = '\0';
    while (n > 0 && (dst[n - 1] == '/' || dst[n - 1] == '\\')) {
        dst[--n] = '\0';
    }
    *flag = 1;
}

void bw_set_data_root(const char *path)
{
    set_root(g_data_root, sizeof(g_data_root), &g_data_root_set, path, 0);
}

const char *bw_data_root(void)
{
    if (!g_data_root_set) {
        const char *env = getenv("BW_DATA_ROOT");

        if (env != NULL && *env != '\0') {
            bw_set_data_root(env);
        } else {
            g_data_root_set = 1;
        }
    }
    return g_data_root;
}

void bw_set_mod_root(const char *path)
{
    /*
     * An empty or NULL path clears the overlay, which is what lets a caller
     * turn one off after a run; for the data root an empty path is a no-op
     * because there is no sane empty data root.
     */
    set_root(g_mod_root, sizeof(g_mod_root), &g_mod_root_set, path, 1);
}

const char *bw_mod_root(void)
{
    if (!g_mod_root_set) {
        const char *env = getenv("BW_MOD_ROOT");

        bw_set_mod_root(env);   /* NULL or empty disables, and marks it set */
    }
    return g_mod_root;
}

/* ------------------------------------------------------------------ */
/* Resolution                                                          */
/* ------------------------------------------------------------------ */

static int file_readable(const char *path)
{
    FILE *fp = fopen(path, "rb");

    if (fp == NULL) {
        return 0;
    }
    fclose(fp);
    return 1;
}

static int join(char *out, size_t out_size, const char *root, const char *rel)
{
    size_t need = strlen(root) + 1 + strlen(rel) + 1;

    if (need > out_size) {
        return -1;
    }
    /*
     * A forward slash is a valid separator on every host this targets,
     * Windows included, so the NitroFS spelling is used verbatim.
     */
    sprintf(out, "%s/%s", root, rel);
    return 0;
}

int bw_data_path(char *out, size_t out_size, const char *rel)
{
    const char *mod = bw_mod_root();

    if (mod != NULL && *mod != '\0') {
        char cand[1200];

        if (join(cand, sizeof(cand), mod, rel) == 0 && file_readable(cand)) {
            return join(out, out_size, mod, rel);
        }
    }
    return join(out, out_size, bw_data_root(), rel);
}

int bw_data_exists(const char *rel)
{
    char path[1200];

    if (bw_data_path(path, sizeof(path), rel) != 0) {
        return 0;
    }
    return file_readable(path) ? 1 : 0;
}
