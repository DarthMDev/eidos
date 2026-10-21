/*
 * sdk/mod_bw.c -- unpack a .bw mod package into a cache folder.
 *
 * Why a cache folder and not a reader inside the file layer.  Every mod root
 * the file layer knows is a directory (sdk/vfs.c), and member-level NARC
 * overrides are directories of loose files too (sdk/vfs_narc.c).  A reader
 * that served zip members in place would have to teach all of that a second
 * kind of root.  Unpacking once into a folder keeps one code path: the
 * folder is exactly the mod folder kenon packed, so the layering rules of
 * docs/mods.md apply to it unchanged.
 *
 * Why everything is validated first.  A package comes from outside.  The
 * reader parses the whole central directory, checks every header, every path
 * and every limit, and only then writes.  The checks are the ones kenon's
 * own unpack applies (kenon/docs/bw-format.md), so a package either unpacks
 * everywhere or is refused everywhere: the portable-path rules exist so the
 * same package does not unpack on Linux and fail, or worse collide, on
 * Windows.  The files go into "<cache_dir>.part" first and that folder is
 * renamed into place at the end, so a crash or a bad CRC in member 900 leaves
 * no half-written mod that a later start would trust.
 *
 * Zip subset.  Classic zip only: end-of-central-directory record, central
 * directory, local headers.  Sizes and CRCs come from the central directory
 * (a local header may defer them to a data descriptor, which we never read).
 * zip64 markers, encryption, multi-disk archives, directory entries and any
 * method but stored (0) and deflate (8) are refused.
 */

/* rmdir is POSIX, not C99: ask for it under -std=c99. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "inflate.h"
#include "mod_bw.h"
#include "plat_dir.h"
#include "plat_save.h"

#ifdef _WIN32
#include <direct.h>
#define BW_RMDIR(p) _rmdir(p)
#else
#include <unistd.h>
#define BW_RMDIR(p) rmdir(p)
#endif

#define MANIFEST_NAME "eidos_mod.txt"
#define COMMENT_PREFIX "eidos-mod bw "
#define MAX_MEMBER   ((u64)1 << 30)
#define MAX_TOTAL    ((u64)4 << 30)
#define MAX_RATIO    1000u
#define MAX_PATH_LEN 1000

typedef struct {
    char *name;
    u32 method;
    u32 crc;
    u32 csize;
    u32 usize;
    u32 local_off;
} Entry;

static void set_err(char *err, size_t size, const char *fmt, ...)
{
    va_list ap;

    if (err == NULL || size == 0)
        return;
    va_start(ap, fmt);
    vsnprintf(err, size, fmt, ap);
    va_end(ap);
}

static u32 rd16(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8);
}

static u32 rd32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

/* Read a whole file.  The caller frees *out. */
static int read_file(const char *path, u8 **out, size_t *size, char *err,
                     size_t err_size)
{
    FILE *f = fopen(path, "rb");
    long n;
    u8 *buf;

    if (f == NULL) {
        set_err(err, err_size, "cannot open the package");
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0
        || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        set_err(err, err_size, "cannot read the package");
        return -1;
    }
    buf = (u8 *)malloc(n > 0 ? (size_t)n : 1);
    if (buf == NULL) {
        fclose(f);
        set_err(err, err_size, "out of memory reading the package");
        return -1;
    }
    if (n > 0 && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        free(buf);
        set_err(err, err_size, "cannot read the package");
        return -1;
    }
    fclose(f);
    *out = buf;
    *size = (size_t)n;
    return 0;
}

/* ---- path rules ---------------------------------------------------- */

static int reserved_part(const char *part, size_t n)
{
    char up[8];
    size_t i, stem = 0;

    while (stem < n && part[stem] != '.')
        stem++;
    if (stem != 3 && stem != 4)
        return 0;
    for (i = 0; i < stem; i++) {
        char c = part[i];

        up[i] = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
    }
    up[stem] = '\0';
    if (stem == 3)
        return strcmp(up, "CON") == 0 || strcmp(up, "PRN") == 0
            || strcmp(up, "AUX") == 0 || strcmp(up, "NUL") == 0;
    return (memcmp(up, "COM", 3) == 0 || memcmp(up, "LPT", 3) == 0)
        && up[3] >= '1' && up[3] <= '9';
}

/* Returns NULL when the path is acceptable, else the reason. */
static const char *check_path(const char *p)
{
    const char *part = p;
    const char *q;

    if (p[0] == '\0')
        return "an empty path";
    if (strlen(p) > MAX_PATH_LEN)
        return "a path that is too long";
    if (p[0] == '/')
        return "an absolute path";
    for (q = p;; q++) {
        unsigned char c = (unsigned char)*q;

        if (c == '/' || c == '\0') {
            size_t n = (size_t)(q - part);

            if (n == 0)
                return "an empty path part";
            if (n == 1 && part[0] == '.')
                return "a '.' path part";
            if (n == 2 && part[0] == '.' && part[1] == '.')
                return "a '..' path part";
            if (part[n - 1] == '.' || part[n - 1] == ' ')
                return "a path part ending in a dot or a space";
            if (reserved_part(part, n))
                return "a Windows reserved name";
            if (c == '\0')
                break;
            part = q + 1;
            continue;
        }
        if (c == '\\')
            return "a backslash in a path";
        if (c == ':')
            return "a colon or drive prefix in a path";
        if (c < 0x20 || c == 0x7F || c == '*' || c == '?' || c == '"'
            || c == '<' || c == '>' || c == '|')
            return "a character that is not portable in a path";
    }
    return NULL;
}

typedef struct {
    char *key;
} Key;

static int key_cmp(const void *a, const void *b)
{
    return strcmp(((const Key *)a)->key, ((const Key *)b)->key);
}

/*
 * No two paths may differ only by case, and no path may be a folder of
 * another once case is ignored.  Sort lowercase keys with '/' mapped to a
 * byte below every printable one, so a folder's children follow it directly.
 */
static const char *check_collisions(const Entry *e, size_t n)
{
    Key *k = (Key *)calloc(n > 0 ? n : 1, sizeof *k);
    const char *why = NULL;
    size_t i;

    if (k == NULL)
        return "out of memory";
    for (i = 0; i < n && why == NULL; i++) {
        char *s = (char *)malloc(strlen(e[i].name) + 1);
        char *c;

        if (s == NULL) {
            why = "out of memory";
            break;
        }
        memcpy(s, e[i].name, strlen(e[i].name) + 1);
        for (c = s; *c != '\0'; c++) {
            if (*c >= 'A' && *c <= 'Z')
                *c = (char)(*c - 'A' + 'a');
            else if (*c == '/')
                *c = '\x01';
        }
        k[i].key = s;
    }
    if (why == NULL) {
        qsort(k, n, sizeof *k, key_cmp);
        for (i = 0; i + 1 < n; i++) {
            size_t len = strlen(k[i].key);

            if (strcmp(k[i].key, k[i + 1].key) == 0) {
                why = "two paths that differ only by letter case";
                break;
            }
            if (strncmp(k[i].key, k[i + 1].key, len) == 0
                && k[i + 1].key[len] == '\x01') {
                why = "a file and a folder with the same name";
                break;
            }
        }
    }
    for (i = 0; i < n; i++)
        free(k[i].key);
    free(k);
    return why;
}

/* ---- zip parsing --------------------------------------------------- */

static void free_entries(Entry *e, size_t n)
{
    size_t i;

    if (e == NULL)
        return;
    for (i = 0; i < n; i++)
        free(e[i].name);
    free(e);
}

/*
 * Parse and validate the whole package.  On success *entries holds `*count`
 * checked members, in central directory order.
 */
static int parse_zip(const u8 *z, size_t size, Entry **entries, size_t *count,
                     char *err, size_t err_size)
{
    size_t i, eocd = (size_t)-1, lo, cd_off, cd_size, pos, n;
    u32 clen, total_entries;
    Entry *e = NULL;
    u64 total = 0;
    const char *why;

    if (size < 22) {
        set_err(err, err_size, "not a zip file (too small)");
        return -1;
    }
    lo = size - 22 > 65535 ? size - 22 - 65535 : 0;
    for (i = size - 22 + 1; i-- > lo;) {
        if (rd32(z + i) == 0x06054B50u && i + 22 + rd16(z + i + 20) == size) {
            eocd = i;
            break;
        }
    }
    if (eocd == (size_t)-1) {
        set_err(err, err_size, "not a zip file (no end record)");
        return -1;
    }
    clen = rd16(z + eocd + 20);
    if (clen != 0) {
        /* "eidos-mod bw <major>[.minor...]": only major 1 is known. */
        const char *c = (const char *)z + eocd + 22;
        size_t pl = strlen(COMMENT_PREFIX);
        u32 major = 0;
        size_t k = pl;
        int digits = 0;

        if (clen < pl || memcmp(c, COMMENT_PREFIX, pl) != 0) {
            set_err(err, err_size, "the package comment is not an eidos-mod version");
            return -1;
        }
        while (k < clen && c[k] >= '0' && c[k] <= '9' && major < 100000) {
            major = major * 10 + (u32)(c[k] - '0');
            k++;
            digits++;
        }
        if (digits == 0 || (k < clen && c[k] != '.')) {
            set_err(err, err_size, "the package comment is not an eidos-mod version");
            return -1;
        }
        if (major != 1) {
            set_err(err, err_size,
                    "unsupported package version %u (this build reads version 1)",
                    (unsigned)major);
            return -1;
        }
    }
    if (rd16(z + eocd + 4) != 0 || rd16(z + eocd + 6) != 0
        || rd16(z + eocd + 8) != rd16(z + eocd + 10)) {
        set_err(err, err_size, "multi-disk zip files are not supported");
        return -1;
    }
    total_entries = rd16(z + eocd + 10);
    cd_size = rd32(z + eocd + 12);
    cd_off = rd32(z + eocd + 16);
    if (total_entries == 0xFFFFu || cd_size == 0xFFFFFFFFu
        || cd_off == 0xFFFFFFFFu) {
        set_err(err, err_size, "zip64 packages are not supported");
        return -1;
    }
    if (cd_off > eocd || cd_size > eocd - cd_off) {
        set_err(err, err_size, "the zip central directory is out of range");
        return -1;
    }
    if (total_entries == 0) {
        set_err(err, err_size, "the package is empty (no %s)", MANIFEST_NAME);
        return -1;
    }

    e = (Entry *)calloc(total_entries, sizeof *e);
    if (e == NULL) {
        set_err(err, err_size, "out of memory");
        return -1;
    }
    pos = cd_off;
    for (n = 0; n < total_entries; n++) {
        u32 flags, method, nlen, xlen, clen2, csize, usize, off, lnlen, lxlen;
        const u8 *h, *l;

        if (cd_off + cd_size - pos < 46 || rd32(z + pos) != 0x02014B50u) {
            set_err(err, err_size, "the zip central directory is damaged");
            goto fail;
        }
        h = z + pos;
        flags = rd16(h + 8);
        method = rd16(h + 10);
        nlen = rd16(h + 28);
        xlen = rd16(h + 30);
        clen2 = rd16(h + 32);
        csize = rd32(h + 20);
        usize = rd32(h + 24);
        off = rd32(h + 42);
        if (cd_off + cd_size - pos - 46 < (size_t)nlen + xlen + clen2) {
            set_err(err, err_size, "the zip central directory is damaged");
            goto fail;
        }
        e[n].name = (char *)malloc((size_t)nlen + 1);
        if (e[n].name == NULL) {
            set_err(err, err_size, "out of memory");
            goto fail;
        }
        memcpy(e[n].name, h + 46, nlen);
        e[n].name[nlen] = '\0';
        if (strlen(e[n].name) != nlen) {
            set_err(err, err_size, "a member name contains a NUL byte");
            n++;
            goto fail;
        }
        e[n].method = method;
        e[n].crc = rd32(h + 16);
        e[n].csize = csize;
        e[n].usize = usize;
        e[n].local_off = off;
        pos += 46 + (size_t)nlen + xlen + clen2;

        if (nlen > 0 && e[n].name[nlen - 1] == '/') {
            set_err(err, err_size, "directory entries are not allowed (%s)", e[n].name);
            n++;
            goto fail;
        }
        why = check_path(e[n].name);
        if (why != NULL) {
            set_err(err, err_size, "member \"%.80s\" has %s", e[n].name, why);
            n++;
            goto fail;
        }
        if (flags & 1u) {
            set_err(err, err_size, "member \"%.80s\" is encrypted", e[n].name);
            n++;
            goto fail;
        }
        if (method != 0 && method != 8) {
            set_err(err, err_size, "member \"%.80s\" uses compression method %u",
                    e[n].name, (unsigned)method);
            n++;
            goto fail;
        }
        if (csize == 0xFFFFFFFFu || usize == 0xFFFFFFFFu || off == 0xFFFFFFFFu) {
            set_err(err, err_size, "zip64 packages are not supported");
            n++;
            goto fail;
        }
        if (method == 0 && csize != usize) {
            set_err(err, err_size, "stored member \"%.80s\" has unequal sizes", e[n].name);
            n++;
            goto fail;
        }
        if ((u64)usize > MAX_MEMBER) {
            set_err(err, err_size, "member \"%.80s\" is larger than 1 GiB", e[n].name);
            n++;
            goto fail;
        }
        if ((u64)usize > (u64)csize * MAX_RATIO) {
            set_err(err, err_size, "member \"%.80s\" expands more than %u times",
                    e[n].name, MAX_RATIO);
            n++;
            goto fail;
        }
        total += usize;
        if (total > MAX_TOTAL) {
            set_err(err, err_size, "the package is larger than 4 GiB unpacked");
            n++;
            goto fail;
        }
        /* The local header must sit before the central directory and hold
         * the whole compressed member.                                      */
        if ((size_t)off > cd_off || cd_off - off < 30
            || rd32(z + off) != 0x04034B50u) {
            set_err(err, err_size, "member \"%.80s\" has a bad local header", e[n].name);
            n++;
            goto fail;
        }
        l = z + off;
        lnlen = rd16(l + 26);
        lxlen = rd16(l + 28);
        if (lnlen != nlen || memcmp(l + 30, e[n].name, nlen) != 0) {
            set_err(err, err_size, "member \"%.80s\" local name differs", e[n].name);
            n++;
            goto fail;
        }
        if (cd_off - off - 30 < (size_t)lnlen + lxlen
            || cd_off - off - 30 - lnlen - lxlen < csize) {
            set_err(err, err_size, "member \"%.80s\" data is out of range", e[n].name);
            n++;
            goto fail;
        }
    }
    if (strcmp(e[0].name, MANIFEST_NAME) != 0) {
        set_err(err, err_size, "the first member is not %s", MANIFEST_NAME);
        goto fail;
    }
    why = check_collisions(e, total_entries);
    if (why != NULL) {
        set_err(err, err_size, "%s", why);
        goto fail;
    }
    *entries = e;
    *count = total_entries;
    return 0;

fail:
    free_entries(e, total_entries);
    return -1;
}

/* ---- writing ------------------------------------------------------- */

static int join_path(char *out, size_t size, const char *root, const char *rel)
{
    size_t a = strlen(root), b = strlen(rel);

    if (a + 1 + b + 1 > size)
        return -1;
    memcpy(out, root, a);
    out[a] = '/';
    memcpy(out + a + 1, rel, b + 1);
    return 0;
}

/* Make every parent folder of root/rel (root itself exists). */
static int make_parents(const char *full)
{
    char buf[2048];
    char *slash;
    size_t n = strlen(full);

    if (n >= sizeof buf)
        return -1;
    memcpy(buf, full, n + 1);
    slash = strrchr(buf, '/');
    if (slash == NULL)
        return 0;
    *slash = '\0';
    return bw_plat_mkdirs(buf);
}

/* Remove what a failed unpack wrote: files first, then folders deepest first. */
static void cleanup_tmp(const char *tmp, const Entry *e, size_t n)
{
    char path[2048];
    size_t i;

    for (i = 0; i < n; i++)
        if (join_path(path, sizeof path, tmp, e[i].name) == 0)
            remove(path);
    for (i = 0; i < n; i++) {
        if (join_path(path, sizeof path, tmp, e[i].name) != 0)
            continue;
        for (;;) {
            char *slash = strrchr(path, '/');

            if (slash == NULL || strlen(path) <= strlen(tmp))
                break;
            *slash = '\0';
            if (strlen(path) <= strlen(tmp))
                break;
            BW_RMDIR(path);
        }
    }
    BW_RMDIR(tmp);
}

int bw_mod_bw_unpack(const char *bw_path, const char *cache_dir,
                     char *err, size_t err_size)
{
    u8 *z = NULL;
    size_t zsize = 0, count = 0, i;
    Entry *e = NULL;
    char tmp[2048], path[2048];
    int rc = -1;

    set_err(err, err_size, "");
    if (bw_path == NULL || cache_dir == NULL || cache_dir[0] == '\0') {
        set_err(err, err_size, "no package or cache folder given");
        return -1;
    }
    if (strlen(cache_dir) + 8 > sizeof tmp - MAX_PATH_LEN - 2) {
        set_err(err, err_size, "the cache folder path is too long");
        return -1;
    }
    if (read_file(bw_path, &z, &zsize, err, err_size) != 0)
        return -1;
    if (parse_zip(z, zsize, &e, &count, err, err_size) != 0) {
        free(z);
        return -1;
    }
    if (bw_plat_path_kind(cache_dir) == BW_PATH_DIR) {
        free_entries(e, count);
        free(z);
        return 0;
    }
    sprintf(tmp, "%s.part", cache_dir);
    if (bw_plat_mkdirs(tmp) != 0) {
        set_err(err, err_size, "cannot create the cache folder");
        goto done;
    }

    for (i = 0; i < count; i++) {
        const u8 *l = z + e[i].local_off;
        const u8 *data = l + 30 + rd16(l + 26) + rd16(l + 28);
        u8 *buf;
        FILE *f;
        size_t want = e[i].usize;
        int ok;

        buf = (u8 *)malloc(want > 0 ? want : 1);
        if (buf == NULL) {
            set_err(err, err_size, "out of memory unpacking \"%.80s\"", e[i].name);
            goto fail;
        }
        if (e[i].method == 0) {
            if (want > 0)
                memcpy(buf, data, want);
        } else {
            int r = bw_inflate(data, e[i].csize, buf, want, NULL);

            if (r != BW_INFLATE_OK) {
                set_err(err, err_size, "member \"%.80s\": %s", e[i].name,
                        bw_inflate_error(r));
                free(buf);
                goto fail;
            }
        }
        if (bw_crc32(0, buf, want) != e[i].crc) {
            set_err(err, err_size, "member \"%.80s\" fails its CRC-32 check", e[i].name);
            free(buf);
            goto fail;
        }
        if (join_path(path, sizeof path, tmp, e[i].name) != 0
            || make_parents(path) != 0 || (f = fopen(path, "wb")) == NULL) {
            set_err(err, err_size, "cannot write \"%.80s\"", e[i].name);
            free(buf);
            goto fail;
        }
        ok = want == 0 || fwrite(buf, 1, want, f) == want;
        ok = (fclose(f) == 0) && ok;
        free(buf);
        if (!ok) {
            set_err(err, err_size, "cannot write \"%.80s\"", e[i].name);
            goto fail;
        }
    }
    if (bw_plat_replace_file(tmp, cache_dir) != 0) {
        set_err(err, err_size, "cannot move the unpacked folder into place");
        goto fail;
    }
    rc = 0;
    goto done;

fail:
    cleanup_tmp(tmp, e, count);
done:
    free_entries(e, count);
    free(z);
    return rc;
}

/* ---- cache folder name -------------------------------------------- */

int bw_mod_bw_cache_path(const char *bw_path, char *out, size_t size)
{
    u8 *z;
    size_t zsize, i, dirlen, stemlen;
    const char *base, *p;
    char stem[64], dir[1024];
    u32 crc, fnv = 2166136261u;
    char text[1200];
    int n;

    if (bw_path == NULL || out == NULL || size == 0)
        return -1;
    if (read_file(bw_path, &z, &zsize, NULL, 0) != 0)
        return -1;
    crc = bw_crc32(0, z, zsize);
    for (i = 0; i < zsize; i++) {
        fnv ^= z[i];
        fnv *= 16777619u;
    }
    free(z);

    base = bw_path;
    for (p = bw_path; *p != '\0'; p++)
        if (*p == '/' || *p == '\\')
            base = p + 1;
    dirlen = (size_t)(base - bw_path);
    if (dirlen == 0) {
        strcpy(dir, ".");
    } else {
        if (dirlen - 1 >= sizeof dir)
            return -1;
        memcpy(dir, bw_path, dirlen - 1);
        dir[dirlen - 1] = '\0';
        if (dir[0] == '\0')
            strcpy(dir, "/");
    }
    stemlen = strlen(base);
    if (stemlen > 3 && base[stemlen - 3] == '.'
        && (base[stemlen - 2] == 'b' || base[stemlen - 2] == 'B')
        && (base[stemlen - 1] == 'w' || base[stemlen - 1] == 'W'))
        stemlen -= 3;
    if (stemlen > sizeof stem - 1)
        stemlen = sizeof stem - 1;
    for (i = 0; i < stemlen; i++) {
        char c = base[i];
        int good = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                || (c >= '0' && c <= '9') || c == '_' || c == '-';

        stem[i] = good ? c : '_';
    }
    stem[stemlen] = '\0';
    if (stemlen == 0)
        strcpy(stem, "mod");

    /* The cache lives beside the save folder (<app data>/eidos/mod-cache),
     * not beside the package: a package in the mods folder must not leave
     * a folder there that the mod menu would list as a mod. */
    {
        char save[1024];

        if (bw_plat_default_save_dir(save, sizeof save) == 0) {
            char *cut = save + strlen(save);

            while (cut > save && (cut[-1] == '/' || cut[-1] == '\\'))
                *--cut = '\0';
            while (cut > save && cut[-1] != '/' && cut[-1] != '\\')
                cut--;
            if (cut > save) {
                cut[-1] = '\0';
                if (strlen(save) + 11 < sizeof dir)
                    snprintf(dir, sizeof dir, "%s/mod-cache", save);
            }
        }
    }
    n = snprintf(text, sizeof text, "%s/bw-cache/%s-%08x%08x", dir, stem,
                 (unsigned)crc, (unsigned)fnv);
    if (n < 0 || (size_t)n >= sizeof text || (size_t)n >= size)
        return -1;
    memcpy(out, text, (size_t)n + 1);
    return 0;
}
