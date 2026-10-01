/*
 * sdk/save.c -- the save folder and the atomic whole-file write
 * (sdk/save.h says why both exist).  Everything OS-specific is in
 * sdk/plat_save.c.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "plat_save.h"
#include "save.h"

#define BW_SAVE_PATH_MAX 1024

static struct {
    int  set;                       /* bw_set_save_dir was given a path    */
    char dir[BW_SAVE_PATH_MAX];     /* what it was given                    */
    char resolved[BW_SAVE_PATH_MAX];
} g_save;

void bw_set_save_dir(const char *path)
{
    if (path == NULL || strlen(path) >= sizeof g_save.dir) {
        g_save.set = 0;
        g_save.dir[0] = '\0';
        return;
    }
    strcpy(g_save.dir, path);
    g_save.set = 1;
}

const char *bw_save_dir(void)
{
    const char *env;

    if (g_save.set)
        return g_save.dir;
    env = getenv("BW_SAVE_DIR");
    if (env != NULL && env[0] != '\0' && strlen(env) < sizeof g_save.resolved) {
        strcpy(g_save.resolved, env);
        return g_save.resolved;
    }
    if (bw_plat_default_save_dir(g_save.resolved, sizeof g_save.resolved) != 0)
        g_save.resolved[0] = '\0';
    return g_save.resolved;
}

int bw_save_path(char *out, size_t out_size, const char *name)
{
    const char *dir = bw_save_dir();
    size_t ld = strlen(dir), ln = strlen(name);

    if (ld == 0 || ld + 1 + ln + 1 > out_size)
        return -1;
    memcpy(out, dir, ld);
    out[ld] = '/';
    memcpy(out + ld + 1, name, ln + 1);
    return 0;
}

long bw_save_read(const char *path, u8 *buf, u32 size)
{
    FILE *f;
    long length;
    size_t want;

    if (path == NULL || path[0] == '\0')
        return -1;
    f = fopen(path, "rb");
    if (f == NULL)
        return -1;
    if (fseek(f, 0, SEEK_END) != 0 || (length = ftell(f)) < 0
        || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }
    want = (size_t)length < (size_t)size ? (size_t)length : (size_t)size;
    if (want > 0 && fread(buf, 1, want, f) != want) {
        fclose(f);
        return -1;
    }
    fclose(f);
    return length;
}

/* The folder part of `path`, for creating it.                              */
static void parent_of(char *out, size_t out_size, const char *path)
{
    const char *a = strrchr(path, '/');
    const char *b = strrchr(path, '\\');
    const char *cut = (a == NULL || (b != NULL && b > a)) ? b : a;
    size_t n = cut == NULL ? 0 : (size_t)(cut - path);

    if (n >= out_size)
        n = 0;
    memcpy(out, path, n);
    out[n] = '\0';
}

int bw_save_write_atomic(const char *path, const void *data, u32 size)
{
    char tmp[BW_SAVE_PATH_MAX + 8];
    char dir[BW_SAVE_PATH_MAX];
    FILE *f;
    int ok;

    if (path == NULL || path[0] == '\0' || strlen(path) >= BW_SAVE_PATH_MAX)
        return -1;
    parent_of(dir, sizeof dir, path);
    if (dir[0] != '\0' && bw_plat_mkdirs(dir) != 0)
        return -1;
    sprintf(tmp, "%s.tmp", path);
    f = fopen(tmp, "wb");
    if (f == NULL)
        return -1;
    ok = (size == 0 || fwrite(data, 1, size, f) == size);
    ok = ok && bw_plat_sync_file(f) == 0;
    ok = (fclose(f) == 0) && ok;
    if (!ok || bw_plat_replace_file(tmp, path) != 0) {
        remove(tmp);
        return -1;
    }
    return 0;
}
