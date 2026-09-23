/*
 * install/ndsrom.c -- identify a DS cartridge image and extract it.
 *
 * A C transcription of what aletheia's tools/ndsrom.py `extract` writes
 * (its module docstring is the format reference: the header fields, the
 * overlay table entry, the FAT and the FNT), plus the ARM9 decompression
 * tools/delink.py does into arm9_dec.bin.  It refuses what ndsrom.py
 * refuses, with the same checks in the same order, and where ndsrom.py
 * would crash rather than refuse (a cyclic FNT recurses until Python gives
 * up) this refuses with a message.
 *
 * The work is done in two passes.  The first reads the tables and builds
 * the complete list of regions to write, checking every one against the
 * image and against each other (ndsrom.py's RegionMap: nothing may run past
 * the end, nothing may overlap).  Only if the whole plan is sound does the
 * second pass write anything, so a malformed image leaves no files behind
 * except the output directory itself.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "install.h"

#ifdef _WIN32
#include <direct.h>
#include <wchar.h>
#include <windows.h>
#else
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#endif

#define HEADER_SNAPSHOT     0x1000u
#define OVERLAY_ENTRY_SIZE  32u
#define FAT_ENTRY_SIZE      8u
#define FNT_MAIN_ENTRY_SIZE 8u
#define PATH_MAX_LEN        1024u
#define COPY_BLOCK          (256u * 1024u)

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

static u16 rd16(const u8 *p) { return (u16)(p[0] | (p[1] << 8)); }
static u32 rd32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static void say(char *err, size_t err_len, const char *fmt, ...)
{
    va_list ap;

    if (err != NULL && err_len > 0) {
        va_start(ap, fmt);
        vsnprintf(err, err_len, fmt, ap);
        va_end(ap);
    }
}

FILE *bw_fopen_utf8(const char *path, const char *mode)
{
#ifdef _WIN32
    wchar_t wpath[PATH_MAX_LEN];
    wchar_t wmode[8];

    if (MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, PATH_MAX_LEN) == 0
        || MultiByteToWideChar(CP_UTF8, 0, mode, -1, wmode, 8) == 0) {
        return NULL;
    }
    return _wfopen(wpath, wmode);
#else
    return fopen(path, mode);
#endif
}

static int mkdir_one(const char *path)
{
#ifdef _WIN32
    wchar_t wpath[PATH_MAX_LEN];
    DWORD attr;

    if (MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, PATH_MAX_LEN) == 0) {
        return -1;
    }
    if (_wmkdir(wpath) == 0) {
        return 0;
    }
    attr = GetFileAttributesW(wpath);
    return (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY))
        ? 0 : -1;
#else
    struct stat st;

    if (mkdir(path, 0777) == 0) {
        return 0;
    }
    return (errno == EEXIST && stat(path, &st) == 0 && S_ISDIR(st.st_mode))
        ? 0 : -1;
#endif
}

int bw_mkdirs_utf8(const char *path)
{
    char buf[PATH_MAX_LEN];
    size_t n = strlen(path);
    size_t i;

    if (n == 0 || n >= sizeof(buf)) {
        return -1;
    }
    memcpy(buf, path, n + 1);
    for (i = 1; i < n; i++) {
        if (buf[i] == '/' || buf[i] == '\\') {
            char c = buf[i];
            /* "C:\" and a leading "/" are not directories to create. */
            if (!(i == 2 && buf[1] == ':')) {
                buf[i] = '\0';
                if (mkdir_one(buf) != 0) {
                    return -1;
                }
            }
            buf[i] = c;
        }
    }
    return mkdir_one(buf);
}

/* ------------------------------------------------------------------ */
/* The name mangling, exactly ndsrom.py's sanitize_name                 */
/* ------------------------------------------------------------------ */

static const char *const WIN_RESERVED[] = {
    "CON", "PRN", "AUX", "NUL",
    "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
    "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9",
};

void bw_fnt_sanitize(const u8 *raw, u32 len, char *out, size_t out_size)
{
    char tmp[260];
    char stem[260];
    size_t n = 0;
    size_t i;
    size_t k;

    if (out_size == 0) {
        return;
    }
    /* `[\x00-\x1f<>:"/\\|?*]` -> '_', and every byte a strict ASCII decode
     * refuses (>= 0x80) -> U+FFFD -> '_'. */
    for (i = 0; i < len && n < sizeof(tmp) - 2; i++) {
        u8 c = raw[i];
        if (c < 0x20 || c >= 0x80 || strchr("<>:\"/\\|?*", c) != NULL) {
            tmp[n++] = '_';
        } else {
            tmp[n++] = (char)c;
        }
    }
    /* rstrip(" .") */
    while (n > 0 && (tmp[n - 1] == ' ' || tmp[n - 1] == '.')) {
        n--;
    }
    if (n == 0) {
        tmp[n++] = '_';
    }
    tmp[n] = '\0';

    /* name.split(".")[0].upper() in the reserved set -> "_" + name */
    for (k = 0; k < n && tmp[k] != '.'; k++) {
        char c = tmp[k];
        stem[k] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
    }
    stem[k] = '\0';
    for (i = 0; i < sizeof(WIN_RESERVED) / sizeof(WIN_RESERVED[0]); i++) {
        if (strcmp(stem, WIN_RESERVED[i]) == 0) {
            snprintf(out, out_size, "_%s", tmp);
            return;
        }
    }
    snprintf(out, out_size, "%s", tmp);
}

/* ------------------------------------------------------------------ */
/* Reading the image                                                   */
/* ------------------------------------------------------------------ */

typedef struct Rom {
    FILE *fp;
    u32   size;
    u8    hdr[HEADER_SNAPSHOT];
} Rom;

static int rom_read(Rom *r, u32 off, void *dst, u32 len)
{
    if (off > r->size || len > r->size - off) {
        return -1;
    }
    if (len == 0) {
        return 0;
    }
    if (fseek(r->fp, (long)off, SEEK_SET) != 0) {
        return -1;
    }
    return fread(dst, 1, len, r->fp) == len ? 0 : -1;
}

static int rom_open(Rom *r, const char *path, char *err, size_t err_len)
{
    long end;

    memset(r, 0, sizeof(*r));
    r->fp = bw_fopen_utf8(path, "rb");
    if (r->fp == NULL) {
        say(err, err_len, "cannot open %s", path);
        return -1;
    }
    if (fseek(r->fp, 0, SEEK_END) != 0 || (end = ftell(r->fp)) < 0) {
        say(err, err_len, "cannot determine the size of %s", path);
        fclose(r->fp);
        r->fp = NULL;
        return -1;
    }
    r->size = (u32)end;
    if (r->size < HEADER_SNAPSHOT) {
        say(err, err_len, "%s is 0x%X bytes, smaller than a cartridge header",
            path, (unsigned)r->size);
        fclose(r->fp);
        r->fp = NULL;
        return -1;
    }
    if (rom_read(r, 0, r->hdr, HEADER_SNAPSHOT) != 0) {
        say(err, err_len, "cannot read the header of %s", path);
        fclose(r->fp);
        r->fp = NULL;
        return -1;
    }
    return 0;
}

static void rom_close(Rom *r)
{
    if (r->fp != NULL) {
        fclose(r->fp);
        r->fp = NULL;
    }
}

static u32 H32(const Rom *r, u32 off) { return rd32(r->hdr + off); }

/* ------------------------------------------------------------------ */
/* Identify                                                            */
/* ------------------------------------------------------------------ */

static void copy_field(char *dst, const u8 *src, u32 n)
{
    u32 i;

    for (i = 0; i < n; i++) {
        u8 c = src[i];
        dst[i] = (c >= 0x20 && c < 0x7F) ? (char)c : (c == 0 ? '\0' : '?');
    }
    dst[n] = '\0';
    /* rstrip NULs, as ndsrom.py does for the title */
    while (n > 0 && dst[n - 1] == '\0') {
        n--;
    }
}

int bw_rom_identify(const char *rom_path, BwRomInfo *info,
                    BwProgressFn progress, void *ctx,
                    char *err, size_t err_len)
{
    Rom r;
    BwSha1 sha;
    u8 *buf;
    u32 done = 0;

    memset(info, 0, sizeof(*info));
    if (rom_open(&r, rom_path, err, err_len) != 0) {
        return -1;
    }
    copy_field(info->title, r.hdr + 0x00, 12);
    copy_field(info->gamecode, r.hdr + 0x0C, 4);
    copy_field(info->maker, r.hdr + 0x10, 2);
    info->unit_code = r.hdr[0x12];
    info->rom_size = r.size;

    buf = (u8 *)malloc(COPY_BLOCK);
    if (buf == NULL) {
        say(err, err_len, "out of memory");
        rom_close(&r);
        return -1;
    }
    bw_sha1_init(&sha);
    if (fseek(r.fp, 0, SEEK_SET) != 0) {
        say(err, err_len, "cannot rewind %s", rom_path);
        free(buf);
        rom_close(&r);
        return -1;
    }
    while (done < r.size) {
        u32 n = r.size - done < COPY_BLOCK ? r.size - done : COPY_BLOCK;
        if (fread(buf, 1, n, r.fp) != n) {
            say(err, err_len, "read error in %s at 0x%X", rom_path, (unsigned)done);
            free(buf);
            rom_close(&r);
            return -1;
        }
        bw_sha1_update(&sha, buf, n);
        done += n;
        if (progress != NULL) {
            progress(ctx, done, r.size, "hashing");
        }
    }
    free(buf);
    rom_close(&r);
    bw_sha1_final(&sha, info->sha1);
    bw_sha1_hex(info->sha1, info->sha1_hex);

    /* The game codes: IRB? is Black, IRA? is White (the last letter is the
     * region). */
    if (memcmp(info->gamecode, "IRB", 3) == 0) {
        info->game = BW_GAME_BLACK;
    } else if (memcmp(info->gamecode, "IRA", 3) == 0) {
        info->game = BW_GAME_WHITE;
    }
    info->verified = strcmp(info->sha1_hex, BW_BLACK_SHA1) == 0;
    return 0;
}

/* ------------------------------------------------------------------ */
/* The plan                                                            */
/* ------------------------------------------------------------------ */

typedef struct Region {
    u32  offset;
    u32  size;
    char path[PATH_MAX_LEN];   /* relative, '/'-separated; "" = not written */
} Region;

typedef struct Plan {
    Region *r;
    u32     count;
    u32     cap;
} Plan;

static int plan_add(Plan *p, const Rom *rom, const char *name, u32 offset,
                    u32 size, const char *path, char *err, size_t err_len)
{
    Region *reg;

    if (size != 0 && (offset > rom->size || size > rom->size - offset)) {
        say(err, err_len, "region '%s' at 0x%X+0x%X runs past the end of the ROM (0x%X)",
            name, (unsigned)offset, (unsigned)size, (unsigned)rom->size);
        return -1;
    }
    if (p->count == p->cap) {
        u32 cap = p->cap ? p->cap * 2 : 256;
        Region *n = (Region *)realloc(p->r, cap * sizeof(Region));
        if (n == NULL) {
            say(err, err_len, "out of memory");
            return -1;
        }
        p->r = n;
        p->cap = cap;
    }
    reg = &p->r[p->count++];
    reg->offset = offset;
    reg->size = size;
    if (size == 0) {
        reg->path[0] = '\0';               /* recorded, not written */
    } else if (snprintf(reg->path, sizeof(reg->path), "%s", path)
               >= (int)sizeof(reg->path)) {
        say(err, err_len, "path too long: %s", path);
        return -1;
    }
    return 0;
}

static int cmp_region(const void *a, const void *b)
{
    const Region *x = *(const Region *const *)a;
    const Region *y = *(const Region *const *)b;

    if (x->offset != y->offset) {
        return x->offset < y->offset ? -1 : 1;
    }
    return x->size < y->size ? -1 : (x->size > y->size);
}

/* ndsrom.py's occupied_intervals: no two non-empty regions may overlap. */
static int plan_check_overlaps(const Plan *p, char *err, size_t err_len)
{
    const Region **v;
    u32 n = 0;
    u32 i;
    u32 end = 0;
    int have = 0;

    v = (const Region **)malloc((p->count + 1) * sizeof(*v));
    if (v == NULL) {
        say(err, err_len, "out of memory");
        return -1;
    }
    for (i = 0; i < p->count; i++) {
        if (p->r[i].size != 0) {
            v[n++] = &p->r[i];
        }
    }
    qsort(v, n, sizeof(*v), cmp_region);
    for (i = 0; i < n; i++) {
        if (have && v[i]->offset < end) {
            say(err, err_len, "overlapping regions detected near 0x%X (previous region ends at 0x%X)",
                (unsigned)v[i]->offset, (unsigned)end);
            free((void *)v);
            return -1;
        }
        if (!have || v[i]->offset + v[i]->size > end) {
            end = v[i]->offset + v[i]->size;
        }
        have = 1;
    }
    free((void *)v);
    return 0;
}

/* ------------------------------------------------------------------ */
/* The FNT walk (ndsrom.py's parse_fnt)                                 */
/* ------------------------------------------------------------------ */

typedef struct FntCtx {
    const u8 *fnt;
    u32       size;
    u32       dir_count;
    const u32 *fat;         /* start, end pairs */
    u32       fat_count;
    u8       *fid_seen;     /* one byte per FAT entry: 0 free, 1 fs, 2 overlay */
    Plan     *plan;
    const Rom *rom;
    u32       fs_files;
    char     *err;
    size_t    err_len;
} FntCtx;

static int fnt_walk(FntCtx *c, u32 dir, const char *prefix, u32 depth)
{
    u32 base;
    u32 p;
    u32 next_fid;

    if (dir >= c->dir_count) {
        say(c->err, c->err_len, "FNT directory index %u is out of range", (unsigned)dir);
        return -1;
    }
    /* A tree of dir_count directories is at most dir_count deep; anything
     * deeper is a cycle, which ndsrom.py would recurse on forever. */
    if (depth > c->dir_count) {
        say(c->err, c->err_len, "FNT directory %u: the directory tree is cyclic", (unsigned)dir);
        return -1;
    }
    base = dir * FNT_MAIN_ENTRY_SIZE;
    p = rd32(c->fnt + base);
    next_fid = rd16(c->fnt + base + 4);
    if (p >= c->size) {
        say(c->err, c->err_len, "FNT directory %u: subtable offset 0x%X is outside the FNT (size 0x%X)",
            (unsigned)dir, (unsigned)p, (unsigned)c->size);
        return -1;
    }
    for (;;) {
        u8 len;
        u32 name_len;
        char name[160];
        char path[PATH_MAX_LEN];

        if (p >= c->size) {
            say(c->err, c->err_len, "FNT directory %u: subtable ran off the end without a terminator",
                (unsigned)dir);
            return -1;
        }
        len = c->fnt[p++];
        if (len == 0) {
            break;
        }
        name_len = len & 0x7Fu;
        if (name_len == 0) {
            say(c->err, c->err_len, "FNT directory %u: zero-length name at offset 0x%X",
                (unsigned)dir, (unsigned)(p - 1));
            return -1;
        }
        /* Python slices past the end silently and then trips on the next
         * read; stop at the same place with the same verdict. */
        if (name_len > c->size - p) {
            say(c->err, c->err_len, "FNT directory %u: subtable ran off the end without a terminator",
                (unsigned)dir);
            return -1;
        }
        bw_fnt_sanitize(c->fnt + p, name_len, name, sizeof(name));
        p += name_len;
        if (snprintf(path, sizeof(path), "%s%s", prefix, name) >= (int)sizeof(path) - 2) {
            say(c->err, c->err_len, "FNT path too long under %s", prefix);
            return -1;
        }
        if (len & 0x80u) {
            u32 sub;
            if (p + 2 > c->size) {
                say(c->err, c->err_len, "FNT directory %u: truncated subdirectory id", (unsigned)dir);
                return -1;
            }
            sub = rd16(c->fnt + p);
            p += 2;
            strcat(path, "/");
            if (fnt_walk(c, sub & 0x0FFFu, path, depth + 1) != 0) {
                return -1;
            }
        } else {
            u32 fid = next_fid++;
            char rel[PATH_MAX_LEN + 8];

            if (fid < c->fat_count && c->fid_seen[fid] == 1) {
                say(c->err, c->err_len, "FNT lists file id %u more than once", (unsigned)fid);
                return -1;
            }
            if (fid >= c->fat_count) {
                say(c->err, c->err_len, "NitroFS file '%s': file id %u is outside the FAT",
                    path, (unsigned)fid);
                return -1;
            }
            if (c->fid_seen[fid] == 2) {
                say(c->err, c->err_len, "NitroFS file '%s' claims file id %u, which is already used by an overlay",
                    path, (unsigned)fid);
                return -1;
            }
            c->fid_seen[fid] = 1;
            snprintf(rel, sizeof(rel), "data/%s", path);
            if (plan_add(c->plan, c->rom, path, c->fat[2 * fid],
                         c->fat[2 * fid + 1] - c->fat[2 * fid], rel,
                         c->err, c->err_len) != 0) {
                return -1;
            }
            c->fs_files++;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Writing                                                             */
/* ------------------------------------------------------------------ */

static int join(char *out, size_t n, const char *dir, const char *rel)
{
    size_t dl = strlen(dir);
    int sep = dl > 0 && dir[dl - 1] != '/' && dir[dl - 1] != '\\';

    return snprintf(out, n, "%s%s%s", dir, sep ? "/" : "", rel) < (int)n ? 0 : -1;
}

static int ensure_parent(const char *full)
{
    char dir[PATH_MAX_LEN];
    char *slash;

    snprintf(dir, sizeof(dir), "%s", full);
    slash = strrchr(dir, '/');
    if (slash == NULL || slash == dir) {
        return 0;
    }
    *slash = '\0';
    return bw_mkdirs_utf8(dir);
}

static int write_bytes(const char *out_dir, const char *rel, const void *data,
                       u32 len, char *err, size_t err_len)
{
    char full[PATH_MAX_LEN];
    FILE *fp;

    if (join(full, sizeof(full), out_dir, rel) != 0 || ensure_parent(full) != 0) {
        say(err, err_len, "cannot create the directory for %s", rel);
        return -1;
    }
    fp = bw_fopen_utf8(full, "wb");
    if (fp == NULL) {
        say(err, err_len, "cannot write %s", full);
        return -1;
    }
    if (len != 0 && fwrite(data, 1, len, fp) != len) {
        fclose(fp);
        say(err, err_len, "write error on %s", full);
        return -1;
    }
    if (fclose(fp) != 0) {
        say(err, err_len, "write error on %s", full);
        return -1;
    }
    return 0;
}

static int copy_region(Rom *rom, const char *out_dir, const Region *reg,
                       u8 *buf, char *err, size_t err_len)
{
    char full[PATH_MAX_LEN];
    FILE *fp;
    u32 done = 0;

    if (join(full, sizeof(full), out_dir, reg->path) != 0 || ensure_parent(full) != 0) {
        say(err, err_len, "cannot create the directory for %s", reg->path);
        return -1;
    }
    fp = bw_fopen_utf8(full, "wb");
    if (fp == NULL) {
        say(err, err_len, "cannot write %s", full);
        return -1;
    }
    while (done < reg->size) {
        u32 n = reg->size - done < COPY_BLOCK ? reg->size - done : COPY_BLOCK;
        if (rom_read(rom, reg->offset + done, buf, n) != 0
            || fwrite(buf, 1, n, fp) != n) {
            fclose(fp);
            say(err, err_len, "copy error on %s", full);
            return -1;
        }
        done += n;
    }
    if (fclose(fp) != 0) {
        say(err, err_len, "write error on %s", full);
        return -1;
    }
    return 0;
}

/* A JSON string: printable ASCII as itself, everything else escaped. */
static void json_str(FILE *fp, const char *s)
{
    fputc('"', fp);
    for (; *s; s++) {
        u8 c = (u8)*s;
        if (c == '"' || c == '\\') {
            fprintf(fp, "\\%c", c);
        } else if (c < 0x20 || c >= 0x7F) {
            fprintf(fp, "\\u%04x", c);
        } else {
            fputc(c, fp);
        }
    }
    fputc('"', fp);
}

/* ------------------------------------------------------------------ */
/* Extract                                                             */
/* ------------------------------------------------------------------ */

int bw_rom_extract(const char *rom_path, const char *out_dir,
                   const BwExtractOptions *opt, BwExtractResult *result,
                   BwProgressFn progress, void *ctx,
                   char *err, size_t err_len)
{
    static const BwExtractOptions defaults = { NULL, 0 };
    Rom rom;
    Plan plan = { NULL, 0, 0 };
    BwExtractResult res;
    u8 *fnt = NULL;
    u32 *fat = NULL;
    u8 *fid_seen = NULL;
    u8 *table = NULL;
    u8 *buf = NULL;
    u8 *arm9 = NULL;
    u8 *arm9_dec = NULL;
    u32 fat_count = 0;
    u32 header_size, unit_code;
    int is_twl;
    int rc = -1;
    u32 i;
    u64 total = 0;
    u64 done = 0;
    u32 arm9_region = 0;
    u32 arm9_size = 0;
    u32 banner_version = 0;
    int have_banner = 0;

    if (opt == NULL) {
        opt = &defaults;
    }
    memset(&res, 0, sizeof(res));
    if (rom_open(&rom, rom_path, err, err_len) != 0) {
        return -1;
    }

    /* --- header sanity (ndsrom.py's order) -------------------------- */
    if (opt->require_gamecode != NULL
        && memcmp(rom.hdr + 0x0C, opt->require_gamecode, 4) != 0) {
        char gc[5];
        copy_field(gc, rom.hdr + 0x0C, 4);
        say(err, err_len, "unexpected game code '%s'; expected '%.4s'",
            gc, opt->require_gamecode);
        goto done;
    }
    unit_code = rom.hdr[0x12];
    is_twl = (unit_code & 0x02u) != 0;
    header_size = H32(&rom, 0x84);
    if (header_size < HEADER_SNAPSHOT) {
        say(err, err_len, "header size 0x%X is implausibly small", (unsigned)header_size);
        goto done;
    }

    /* --- header, arm9, arm7 ------------------------------------------ */
    if (plan_add(&plan, &rom, "header", 0, HEADER_SNAPSHOT, "header.bin", err, err_len))
        goto done;
    if (header_size > HEADER_SNAPSHOT
        && plan_add(&plan, &rom, "header_ext", HEADER_SNAPSHOT,
                    header_size - HEADER_SNAPSHOT, "header_ext.bin", err, err_len))
        goto done;
    arm9_region = plan.count;
    arm9_size = H32(&rom, 0x2C);
    if (plan_add(&plan, &rom, "arm9", H32(&rom, 0x20), arm9_size, "arm9.bin", err, err_len))
        goto done;
    if (plan_add(&plan, &rom, "arm7", H32(&rom, 0x30), H32(&rom, 0x3C), "arm7.bin", err, err_len))
        goto done;

    /* --- FNT / FAT --------------------------------------------------- */
    {
        u32 fnt_off = H32(&rom, 0x40), fnt_size = H32(&rom, 0x44);
        u32 fat_off = H32(&rom, 0x48), fat_size = H32(&rom, 0x4C);

        if (plan_add(&plan, &rom, "fnt", fnt_off, fnt_size, "fnt.bin", err, err_len)
            || plan_add(&plan, &rom, "fat", fat_off, fat_size, "fat.bin", err, err_len))
            goto done;
        if (fat_size % FAT_ENTRY_SIZE) {
            say(err, err_len, "FAT size 0x%X is not a multiple of 8 bytes", (unsigned)fat_size);
            goto done;
        }
        fat_count = fat_size / FAT_ENTRY_SIZE;
        fat = (u32 *)malloc((size_t)fat_count * 8u + 8u);
        fid_seen = (u8 *)calloc((size_t)fat_count + 1u, 1);
        table = (u8 *)malloc((size_t)fat_size + 1u);
        if (fat == NULL || fid_seen == NULL || table == NULL) {
            say(err, err_len, "out of memory");
            goto done;
        }
        if (rom_read(&rom, fat_off, table, fat_size) != 0) {
            say(err, err_len, "cannot read the FAT");
            goto done;
        }
        for (i = 0; i < fat_count; i++) {
            u32 s = rd32(table + 8 * i), e = rd32(table + 8 * i + 4);
            if (e < s) {
                say(err, err_len, "FAT entry %u: end 0x%X precedes start 0x%X",
                    (unsigned)i, (unsigned)e, (unsigned)s);
                goto done;
            }
            if (e > rom.size) {
                say(err, err_len, "FAT entry %u: end 0x%X runs past the end of the ROM",
                    (unsigned)i, (unsigned)e);
                goto done;
            }
            fat[2 * i] = s;
            fat[2 * i + 1] = e;
        }
        free(table);
        table = NULL;

        /* --- overlay tables and overlay files ----------------------- */
        {
            static const struct { const char *tag; u32 off, size; } T[2] = {
                { "y9", 0x50, 0x54 }, { "y7", 0x58, 0x5C },
            };
            u32 t;

            for (t = 0; t < 2; t++) {
                u32 toff = H32(&rom, T[t].off), tsize = H32(&rom, T[t].size);
                char rel[64];
                u32 count;

                if (tsize == 0) {
                    continue;
                }
                if (tsize % OVERLAY_ENTRY_SIZE) {
                    say(err, err_len, "%s table size 0x%X is not a multiple of 32",
                        T[t].tag, (unsigned)tsize);
                    goto done;
                }
                snprintf(rel, sizeof(rel), "%s.bin", T[t].tag);
                if (plan_add(&plan, &rom, T[t].tag, toff, tsize, rel, err, err_len))
                    goto done;
                table = (u8 *)malloc(tsize);
                if (table == NULL || rom_read(&rom, toff, table, tsize) != 0) {
                    say(err, err_len, "cannot read the %s table", T[t].tag);
                    goto done;
                }
                count = tsize / OVERLAY_ENTRY_SIZE;
                for (i = 0; i < count; i++) {
                    const u8 *e = table + i * OVERLAY_ENTRY_SIZE;
                    u32 ov_id = rd32(e + 0x00), fid = rd32(e + 0x18);
                    if (fid >= fat_count) {
                        say(err, err_len, "%s overlay %u: file id %u is outside the FAT (%u entries)",
                            T[t].tag, (unsigned)i, (unsigned)fid, (unsigned)fat_count);
                        goto done;
                    }
                    snprintf(rel, sizeof(rel), "overlays/%s_%04u.bin", T[t].tag, (unsigned)ov_id);
                    if (plan_add(&plan, &rom, rel, fat[2 * fid],
                                 fat[2 * fid + 1] - fat[2 * fid], rel, err, err_len))
                        goto done;
                    fid_seen[fid] = 2;
                    res.overlays++;
                }
                free(table);
                table = NULL;
            }
        }

        /* --- NitroFS files ------------------------------------------ */
        {
            FntCtx c;
            u32 dir_count;

            if (fnt_size < FNT_MAIN_ENTRY_SIZE) {
                say(err, err_len, "FNT size 0x%X is too small to hold a root", (unsigned)fnt_size);
                goto done;
            }
            fnt = (u8 *)malloc(fnt_size);
            if (fnt == NULL || rom_read(&rom, fnt_off, fnt, fnt_size) != 0) {
                say(err, err_len, "cannot read the FNT");
                goto done;
            }
            dir_count = rd16(fnt + 6);
            if (dir_count == 0 || dir_count * FNT_MAIN_ENTRY_SIZE > fnt_size) {
                say(err, err_len, "FNT reports %u directories, which does not fit in 0x%X bytes",
                    (unsigned)dir_count, (unsigned)fnt_size);
                goto done;
            }
            c.fnt = fnt;
            c.size = fnt_size;
            c.dir_count = dir_count;
            c.fat = fat;
            c.fat_count = fat_count;
            c.fid_seen = fid_seen;
            c.plan = &plan;
            c.rom = &rom;
            c.fs_files = 0;
            c.err = err;
            c.err_len = err_len;
            if (fnt_walk(&c, 0, "", 0) != 0) {
                goto done;
            }
            res.fs_files = c.fs_files;
        }

        /* --- FAT slots nothing named -------------------------------- */
        for (i = 0; i < fat_count; i++) {
            char rel[32];
            if (fid_seen[i] != 0 || fat[2 * i + 1] <= fat[2 * i]) {
                continue;
            }
            snprintf(rel, sizeof(rel), "orphans/%04u.bin", (unsigned)i);
            if (plan_add(&plan, &rom, rel, fat[2 * i], fat[2 * i + 1] - fat[2 * i],
                         rel, err, err_len))
                goto done;
            res.orphans++;
        }
    }

    /* --- banner ------------------------------------------------------ */
    {
        u32 boff = H32(&rom, 0x68);
        if (boff != 0) {
            u8 v[2];
            u32 bsize;
            u32 hdr_bsize = is_twl ? H32(&rom, 0x208) : 0;

            if (rom_read(&rom, boff, v, 2) != 0) {
                say(err, err_len, "banner offset 0x%X is outside the ROM", (unsigned)boff);
                goto done;
            }
            banner_version = rd16(v);
            switch (banner_version) {
            case 0x0001: bsize = 0x0840; break;
            case 0x0002: bsize = 0x0940; break;
            case 0x0003: bsize = 0x0A40; break;
            case 0x0103: bsize = 0x23C0; break;
            default:     bsize = 0;      break;
            }
            if (bsize == 0) {
                bsize = hdr_bsize ? hdr_bsize : 0x23C0;
            } else if (hdr_bsize && hdr_bsize != bsize) {
                bsize = hdr_bsize;          /* the header is what hardware reads */
            }
            if (plan_add(&plan, &rom, "banner", boff, bsize, "banner.bin", err, err_len))
                goto done;
            have_banner = 1;
        }
    }

    /* --- DSi regions ------------------------------------------------- */
    if (is_twl) {
        if (H32(&rom, 0x1CC) && plan_add(&plan, &rom, "arm9i", H32(&rom, 0x1C0),
                                         H32(&rom, 0x1CC), "arm9i.bin", err, err_len))
            goto done;
        if (H32(&rom, 0x1DC) && plan_add(&plan, &rom, "arm7i", H32(&rom, 0x1D0),
                                         H32(&rom, 0x1DC), "arm7i.bin", err, err_len))
            goto done;
        if (H32(&rom, 0x1F4) && plan_add(&plan, &rom, "digest_sector", H32(&rom, 0x1F0),
                                         H32(&rom, 0x1F4), "digest_sector.bin", err, err_len))
            goto done;
        if (H32(&rom, 0x1FC) && plan_add(&plan, &rom, "digest_block", H32(&rom, 0x1F8),
                                         H32(&rom, 0x1FC), "digest_block.bin", err, err_len))
            goto done;
    }

    if (plan_check_overlaps(&plan, err, err_len) != 0) {
        goto done;
    }

    /* --- arm9_dec.bin: decompress before writing anything ------------ */
    if (!opt->no_arm9_decompress && arm9_size != 0) {
        u32 dec_size;

        arm9 = (u8 *)malloc(arm9_size);
        if (arm9 == NULL
            || rom_read(&rom, plan.r[arm9_region].offset, arm9, arm9_size) != 0) {
            say(err, err_len, "cannot read the ARM9 binary");
            goto done;
        }
        dec_size = bw_blz_size(arm9, arm9_size, err, err_len);
        if (dec_size == 0) {
            goto done;
        }
        /* Four times the container is far past any real ratio; a footer
         * that claims more is a corrupt one, not a big image. */
        if (dec_size > 16u * 1024u * 1024u) {
            say(err, err_len, "ARM9 footer claims a 0x%X-byte image", (unsigned)dec_size);
            goto done;
        }
        arm9_dec = (u8 *)malloc(dec_size);
        if (arm9_dec == NULL) {
            say(err, err_len, "out of memory");
            goto done;
        }
        if (bw_blz_decompress(arm9, arm9_size, arm9_dec, dec_size, err, err_len) != 0) {
            goto done;
        }
        res.arm9_dec_size = dec_size;
    }

    /* --- write ------------------------------------------------------- */
    for (i = 0; i < plan.count; i++) {
        total += plan.r[i].size;
    }
    total += res.arm9_dec_size;
    if (bw_mkdirs_utf8(out_dir) != 0) {
        say(err, err_len, "cannot create %s", out_dir);
        goto done;
    }
    buf = (u8 *)malloc(COPY_BLOCK);
    if (buf == NULL) {
        say(err, err_len, "out of memory");
        goto done;
    }
    for (i = 0; i < plan.count; i++) {
        if (plan.r[i].size == 0) {
            continue;
        }
        if (copy_region(&rom, out_dir, &plan.r[i], buf, err, err_len) != 0) {
            goto done;
        }
        done += plan.r[i].size;
        res.regions++;
        if (progress != NULL) {
            progress(ctx, (u32)(done >> 10), (u32)(total >> 10), plan.r[i].path);
        }
    }
    if (arm9_dec != NULL) {
        if (write_bytes(out_dir, "arm9_dec.bin", arm9_dec, res.arm9_dec_size, err, err_len) != 0) {
            goto done;
        }
        done += res.arm9_dec_size;
        res.regions++;
        if (progress != NULL) {
            progress(ctx, (u32)(done >> 10), (u32)(total >> 10), "arm9_dec.bin");
        }
    }
    res.bytes = done;

    /* --- eidos_manifest.json ----------------------------------------- */
    {
        char full[PATH_MAX_LEN];
        char title[13], gamecode[5];
        FILE *fp;

        copy_field(title, rom.hdr, 12);
        copy_field(gamecode, rom.hdr + 0x0C, 4);
        if (join(full, sizeof(full), out_dir, "eidos_manifest.json") != 0
            || (fp = bw_fopen_utf8(full, "w")) == NULL) {
            say(err, err_len, "cannot write eidos_manifest.json");
            goto done;
        }
        fprintf(fp, "{\n \"tool\": \"eidos install\",\n \"version\": %s,\n", BW_INSTALL_VERSION);
        fprintf(fp, " \"title\": ");
        json_str(fp, title);
        fprintf(fp, ",\n \"gamecode\": ");
        json_str(fp, gamecode);
        fprintf(fp, ",\n \"unit_code\": %u,\n \"rom_size\": %u,\n",
                (unsigned)unit_code, (unsigned)rom.size);
        fprintf(fp, " \"header_size\": %u,\n", (unsigned)header_size);
        if (have_banner) {
            fprintf(fp, " \"banner_version\": %u,\n", (unsigned)banner_version);
        }
        fprintf(fp, " \"arm9_dec_size\": %u,\n", (unsigned)res.arm9_dec_size);
        fprintf(fp, " \"counts\": {\"fs_files\": %u, \"overlays\": %u, \"orphans\": %u, \"files\": %u}\n}\n",
                (unsigned)res.fs_files, (unsigned)res.overlays,
                (unsigned)res.orphans, (unsigned)res.regions);
        if (fclose(fp) != 0) {
            say(err, err_len, "write error on eidos_manifest.json");
            goto done;
        }
    }
    rc = 0;

done:
    if (result != NULL) {
        *result = res;
    }
    free(plan.r);
    free(fnt);
    free(fat);
    free(fid_seen);
    free(table);
    free(buf);
    free(arm9);
    free(arm9_dec);
    rom_close(&rom);
    return rc;
}
