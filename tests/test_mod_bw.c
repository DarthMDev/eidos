/*
 * tests/test_mod_bw.c -- the .bw package reader (sdk/mod_bw.c, sdk/inflate.c).
 *
 * Every package here is built in memory by a tiny zip writer in this file.
 * Members are either stored, or deflated by a test-only fixed-Huffman
 * encoder with a naive match finder (so literals, length codes and distance
 * codes are all exercised).  Dynamic-Huffman blocks, which the encoder does
 * not write, are covered by one package made with Python's zipfile module
 * when Python is on the path (the test prints SKIP for that part otherwise).
 *
 * Covered: a valid package (deflate, stored, empty and nested members, no
 * leftover temporary folder), the version comment rules, the manifest-first
 * rule, every path refusal, the size and ratio limits, a corrupt deflate
 * stream, a truncated one, a CRC mismatch, and that every refusal and every
 * late failure leaves neither the cache folder nor its ".part" sibling.
 * Fixtures are written under "bwtest_work" in the current directory.
 */

#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define BW_RMDIR_OK(p) _rmdir(p)
#else
#include <unistd.h>
#define BW_RMDIR_OK(p) rmdir(p)
#endif

#include "port_test.h"
#include "inflate.h"
#include "mod_bw.h"
#include "plat_dir.h"
#include "plat_save.h"

#define WORK "bwtest_work"

/* ---- growable byte buffer ------------------------------------------ */

typedef struct {
    u8 *p;
    size_t n, cap;
} Buf;

static void buf_put(Buf *b, const void *d, size_t n)
{
    if (b->n + n > b->cap) {
        b->cap = (b->n + n) * 2 + 64;
        b->p = (u8 *)realloc(b->p, b->cap);
        if (b->p == NULL) {
            printf("out of memory\n");
            exit(2);
        }
    }
    if (n > 0)
        memcpy(b->p + b->n, d, n);
    b->n += n;
}

static void buf_u16(Buf *b, u32 v)
{
    u8 t[2];

    t[0] = (u8)v;
    t[1] = (u8)(v >> 8);
    buf_put(b, t, 2);
}

static void buf_u32(Buf *b, u32 v)
{
    buf_u16(b, v & 0xFFFFu);
    buf_u16(b, v >> 16);
}

/* ---- test-only fixed-Huffman deflate writer ------------------------ */

typedef struct {
    Buf out;
    u32 acc;
    int cnt;
} BitW;

static void put_bits(BitW *w, u32 v, int n)       /* LSB first */
{
    int i;

    for (i = 0; i < n; i++) {
        w->acc |= ((v >> i) & 1u) << w->cnt;
        if (++w->cnt == 8) {
            u8 c = (u8)w->acc;

            buf_put(&w->out, &c, 1);
            w->acc = 0;
            w->cnt = 0;
        }
    }
}

static void put_code(BitW *w, u32 code, int n)    /* Huffman: MSB first */
{
    int i;

    for (i = n - 1; i >= 0; i--)
        put_bits(w, (code >> i) & 1u, 1);
}

static void put_sym(BitW *w, int s)
{
    if (s < 144)
        put_code(w, 0x30u + (u32)s, 8);
    else if (s < 256)
        put_code(w, 0x190u + (u32)(s - 144), 9);
    else if (s < 280)
        put_code(w, (u32)(s - 256), 7);
    else
        put_code(w, 0xC0u + (u32)(s - 280), 8);
}

static const u16 t_lbase[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};
static const u8 t_lext[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};
static const u16 t_dbase[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
    8193, 12289, 16385, 24577
};
static const u8 t_dext[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};

static void put_match(BitW *w, int len, int dist)
{
    int ls = 28, ds = 29;

    while (t_lbase[ls] > len)
        ls--;
    put_sym(w, 257 + ls);
    put_bits(w, (u32)(len - t_lbase[ls]), t_lext[ls]);
    while (t_dbase[ds] > dist)
        ds--;
    put_code(w, (u32)ds, 5);
    put_bits(w, (u32)(dist - t_dbase[ds]), t_dext[ds]);
}

/* One final fixed-Huffman block holding `d`.  Caller frees out->p. */
static void deflate_fixed(const u8 *d, size_t n, Buf *out)
{
    BitW w;
    size_t i = 0;

    memset(&w, 0, sizeof w);
    put_bits(&w, 1, 1);            /* final   */
    put_bits(&w, 1, 2);            /* fixed   */
    while (i < n) {
        size_t best = 0, bestd = 0, j;
        size_t from = i > 4096 ? i - 4096 : 0;

        for (j = from; j < i; j++) {
            size_t l = 0;

            while (i + l < n && l < 258 && d[j + l] == d[i + l])
                l++;
            if (l > best) {
                best = l;
                bestd = i - j;
            }
        }
        if (best >= 3) {
            put_match(&w, (int)best, (int)bestd);
            i += best;
        } else {
            put_sym(&w, d[i++]);
        }
    }
    put_sym(&w, 256);
    if (w.cnt > 0)
        put_bits(&w, 0, 8 - w.cnt);
    *out = w.out;
}

/* ---- in-memory zip writer ------------------------------------------ */

typedef struct {
    const char *name;
    const u8 *data;
    size_t len;
    int method;                    /* 0 stored, 8 deflate            */
    const u8 *rawc;                /* override compressed bytes      */
    size_t rawc_len;
    long usize_override;           /* -1: use len                    */
    int bad_crc;
    int is_dir;
} Member;

static void make_member(Member *m, const char *name, const void *data,
                        size_t len, int method)
{
    memset(m, 0, sizeof *m);
    m->name = name;
    m->data = (const u8 *)data;
    m->len = len;
    m->method = method;
    m->usize_override = -1;
}

static void build_zip(Buf *z, const Member *m, int count, const char *comment)
{
    Buf cd;
    int i;
    size_t cd_off;

    memset(z, 0, sizeof *z);
    memset(&cd, 0, sizeof cd);
    for (i = 0; i < count; i++) {
        Buf c;
        u32 crc = bw_crc32(0, m[i].data, m[i].len);
        u32 usize = m[i].usize_override >= 0 ? (u32)m[i].usize_override
                                             : (u32)m[i].len;
        size_t off = z->n, nlen = strlen(m[i].name);
        const u8 *cdata;
        size_t clen;

        if (m[i].bad_crc)
            crc ^= 0x1234u;
        memset(&c, 0, sizeof c);
        if (m[i].rawc != NULL) {
            cdata = m[i].rawc;
            clen = m[i].rawc_len;
        } else if (m[i].method == 8) {
            deflate_fixed(m[i].data, m[i].len, &c);
            cdata = c.p;
            clen = c.n;
        } else {
            cdata = m[i].data;
            clen = m[i].len;
        }
        buf_u32(z, 0x04034B50u);
        buf_u16(z, 20);
        buf_u16(z, 0);
        buf_u16(z, (u32)m[i].method);
        buf_u16(z, 0);
        buf_u16(z, 0x21);
        buf_u32(z, crc);
        buf_u32(z, (u32)clen);
        buf_u32(z, usize);
        buf_u16(z, (u32)nlen);
        buf_u16(z, 0);
        buf_put(z, m[i].name, nlen);
        buf_put(z, cdata, clen);

        buf_u32(&cd, 0x02014B50u);
        buf_u16(&cd, 0x031E);
        buf_u16(&cd, 20);
        buf_u16(&cd, 0);
        buf_u16(&cd, (u32)m[i].method);
        buf_u16(&cd, 0);
        buf_u16(&cd, 0x21);
        buf_u32(&cd, crc);
        buf_u32(&cd, (u32)clen);
        buf_u32(&cd, usize);
        buf_u16(&cd, (u32)nlen);
        buf_u16(&cd, 0);
        buf_u16(&cd, 0);
        buf_u16(&cd, 0);
        buf_u16(&cd, 0);
        buf_u32(&cd, 0x81A40000u);
        buf_u32(&cd, (u32)off);
        buf_put(&cd, m[i].name, nlen);
        free(c.p);
    }
    cd_off = z->n;
    buf_put(z, cd.p, cd.n);
    buf_u32(z, 0x06054B50u);
    buf_u16(z, 0);
    buf_u16(z, 0);
    buf_u16(z, (u32)count);
    buf_u16(z, (u32)count);
    buf_u32(z, (u32)cd.n);
    buf_u32(z, (u32)cd_off);
    buf_u16(z, comment != NULL ? (u32)strlen(comment) : 0);
    if (comment != NULL)
        buf_put(z, comment, strlen(comment));
    free(cd.p);
}

/* ---- helpers ------------------------------------------------------- */

static int write_file(const char *path, const u8 *d, size_t n)
{
    FILE *f = fopen(path, "wb");

    if (f == NULL)
        return -1;
    if (n > 0 && fwrite(d, 1, n, f) != n) {
        fclose(f);
        return -1;
    }
    return fclose(f);
}

static int read_back(const char *path, Buf *out)
{
    FILE *f = fopen(path, "rb");
    u8 tmp[4096];
    size_t n;

    memset(out, 0, sizeof *out);
    if (f == NULL)
        return -1;
    while ((n = fread(tmp, 1, sizeof tmp, f)) > 0)
        buf_put(out, tmp, n);
    fclose(f);
    return 0;
}

static int same_file(const char *path, const u8 *d, size_t n)
{
    Buf b;
    int ok;

    if (read_back(path, &b) != 0)
        return 0;
    ok = b.n == n && (n == 0 || memcmp(b.p, d, n) == 0);
    free(b.p);
    return ok;
}

static int exists(const char *p)
{
    return bw_plat_path_kind(p) != BW_PATH_NONE;
}

/* Build `m` into WORK/pkg.bw, unpack into WORK/cache, report the result. */
static char g_err[256];

static int try_unpack(const Member *m, int count, const char *comment)
{
    Buf z;
    int rc;
    char part[128];

    bw_plat_mkdirs(WORK);
    build_zip(&z, m, count, comment);
    write_file(WORK "/pkg.bw", z.p, z.n);
    free(z.p);
    rc = bw_mod_bw_unpack(WORK "/pkg.bw", WORK "/cache", g_err, sizeof g_err);
    sprintf(part, "%s.part", WORK "/cache");
    if (rc != 0 && (exists(WORK "/cache") || exists(part)))
        printf("  note: a failed unpack left a folder behind\n");
    return rc;
}

/* Forget the cache folder between cases (tests know what they wrote). */
static void clear_cache(const char *const *files, int nfiles)
{
    char p[256];
    int i;

    for (i = 0; i < nfiles; i++) {
        sprintf(p, "%s/cache/%s", WORK, files[i]);
        remove(p);
    }
}

static void gen(u8 *out, size_t n, u32 seed)
{
    size_t i;
    u32 s = seed * 2654435761u + 1;

    for (i = 0; i < n; i++) {
        s = s * 1664525u + 1013904223u;
        /* biased bytes: compressible, with several distinct code lengths */
        out[i] = (u8)(((s >> 24) & 7) < 5 ? 'a' + ((s >> 20) & 3) : (s >> 16));
    }
}

/* Remove cache directories written by earlier cases (best effort). */
static void rm_dirs(void)
{
    remove(WORK "/cache/a/b/deep.bin");
    BW_RMDIR_OK(WORK "/cache/a/b");
    BW_RMDIR_OK(WORK "/cache/a");
}

/* ---- cases --------------------------------------------------------- */

static int run_cases(void)
{
    static const char *const valid_files[] = {
        "eidos_mod.txt", "a/b/deep.bin", "stored.bin", "empty.bin", "z.dat"
    };
    u8 manifest[] = "name = test\n";
    u8 big[20000], big2[3000], zeros[5000];
    Member m[8];
    Buf z;
    int rc;

    gen(big, sizeof big, 7);
    gen(big2, sizeof big2, 9);
    memset(zeros, 0, sizeof zeros);

    /* 1. a valid package ------------------------------------------- */
    make_member(&m[0], "eidos_mod.txt", manifest, sizeof manifest - 1, 8);
    make_member(&m[1], "a/b/deep.bin", big, sizeof big, 8);
    make_member(&m[2], "stored.bin", big2, sizeof big2, 0);
    make_member(&m[3], "empty.bin", "", 0, 8);
    make_member(&m[4], "z.dat", zeros, sizeof zeros, 8);
    rc = try_unpack(m, 5, "eidos-mod bw 1");
    BW_CHECK_EQ("valid: rc", rc, 0);
    if (rc != 0)
        printf("  err: %s\n", g_err);
    BW_CHECK_EQ("valid: manifest", same_file(WORK "/cache/eidos_mod.txt", manifest, sizeof manifest - 1), 1);
    BW_CHECK_EQ("valid: nested deflate", same_file(WORK "/cache/a/b/deep.bin", big, sizeof big), 1);
    BW_CHECK_EQ("valid: stored", same_file(WORK "/cache/stored.bin", big2, sizeof big2), 1);
    BW_CHECK_EQ("valid: empty", same_file(WORK "/cache/empty.bin", (const u8 *)"", 0), 1);
    BW_CHECK_EQ("valid: zeros (long matches)", same_file(WORK "/cache/z.dat", zeros, sizeof zeros), 1);
    BW_CHECK_EQ("valid: no .part left", exists(WORK "/cache.part"), 0);

    /* an existing cache folder is kept, and a bad package is still refused */
    {
        u8 marker = 'x';

        write_file(WORK "/cache/marker", &marker, 1);
        rc = bw_mod_bw_unpack(WORK "/pkg.bw", WORK "/cache", g_err, sizeof g_err);
        BW_CHECK_EQ("existing cache kept: rc", rc, 0);
        BW_CHECK_EQ("existing cache kept: marker", exists(WORK "/cache/marker"), 1);
        remove(WORK "/cache/marker");
    }
    clear_cache(valid_files, 5);
    rm_dirs();
    BW_REQUIRE("cache folder removable", BW_RMDIR_OK(WORK "/cache") == 0);

    /* 2. comment / version rules ------------------------------------ */
    make_member(&m[0], "eidos_mod.txt", manifest, sizeof manifest - 1, 8);
    rc = try_unpack(m, 1, NULL);
    BW_CHECK_EQ("no comment is version 1", rc, 0);
    remove(WORK "/cache/eidos_mod.txt");
    BW_RMDIR_OK(WORK "/cache");
    rc = try_unpack(m, 1, "eidos-mod bw 1.4");
    BW_CHECK_EQ("minor version ok", rc, 0);
    remove(WORK "/cache/eidos_mod.txt");
    BW_RMDIR_OK(WORK "/cache");
    rc = try_unpack(m, 1, "eidos-mod bw 2");
    BW_CHECK_EQ("major 2 refused", rc, -1);
    BW_CHECK_EQ("major 2 message names version", strstr(g_err, "version") != NULL, 1);
    BW_CHECK_EQ("major 2: no cache", exists(WORK "/cache"), 0);
    rc = try_unpack(m, 1, "eidos-mod bw 10");
    BW_CHECK_EQ("major 10 refused", rc, -1);
    rc = try_unpack(m, 1, "something else");
    BW_CHECK_EQ("foreign comment refused", rc, -1);
    rc = try_unpack(m, 1, "eidos-mod bw x");
    BW_CHECK_EQ("non-numeric version refused", rc, -1);

    /* 3. manifest rules --------------------------------------------- */
    make_member(&m[0], "other.txt", manifest, sizeof manifest - 1, 8);
    make_member(&m[1], "eidos_mod.txt", manifest, sizeof manifest - 1, 8);
    rc = try_unpack(m, 2, "eidos-mod bw 1");
    BW_CHECK_EQ("manifest not first refused", rc, -1);
    rc = try_unpack(m, 1, "eidos-mod bw 1");
    BW_CHECK_EQ("no manifest refused", rc, -1);
    BW_CHECK_EQ("no manifest: no cache", exists(WORK "/cache"), 0);

    /* 4. path rules: each bad path as the LAST member --------------- */
    {
        static const char *const bad[] = {
            "../x", "a/../b", "/abs", "a//b", "a\\b", "C:/x", "c:x", "a/./b",
            "con.txt", "NUL", "a/COM1.dat", "lpt9", "Aux.tar.gz", "x.", "x ",
            "dir/y ", "d/", "a:b", "a*b", "a?b", "a|b", "a\"b", "a<b", "a>b",
            "tab\there"
        };
        char label[64];
        size_t i;

        for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
            make_member(&m[0], "eidos_mod.txt", manifest, sizeof manifest - 1, 8);
            make_member(&m[1], "good.bin", big2, sizeof big2, 8);
            make_member(&m[2], bad[i], "data", 4, 8);
            rc = try_unpack(m, 3, "eidos-mod bw 1");
            sprintf(label, "bad path #%u refused", (unsigned)i);
            BW_CHECK_EQ(label, rc, -1);
            sprintf(label, "bad path #%u: all or nothing", (unsigned)i);
            BW_CHECK_EQ(label, exists(WORK "/cache") || exists(WORK "/cache.part")
                               || exists(WORK "/cache.part/good.bin"), 0);
        }
        /* near misses that must be accepted */
        {
            static const char *const good[] = {
                "COM10.txt", "console", "lpt", "a.b.c", "A-b_c d/e", ".hidden",
                "12", "12.dat"
            };
            for (i = 0; i < sizeof good / sizeof good[0]; i++) {
                make_member(&m[0], "eidos_mod.txt", manifest, sizeof manifest - 1, 8);
                make_member(&m[1], good[i], "data", 4, 8);
                rc = try_unpack(m, 2, "eidos-mod bw 1");
                sprintf(label, "good path #%u accepted", (unsigned)i);
                BW_CHECK_EQ(label, rc, 0);
                if (rc == 0) {
                    char p[256];

                    sprintf(p, "%s/cache/%s", WORK, good[i]);
                    remove(p);
                    remove(WORK "/cache/eidos_mod.txt");
                    if (strchr(good[i], '/') != NULL)
                        BW_RMDIR_OK(WORK "/cache/A-b_c d");
                    BW_RMDIR_OK(WORK "/cache");
                } else {
                    printf("  err: %s\n", g_err);
                }
            }
        }
    }

    /* case collisions ------------------------------------------------ */
    make_member(&m[0], "eidos_mod.txt", manifest, sizeof manifest - 1, 8);
    make_member(&m[1], "A.txt", "1", 1, 8);
    make_member(&m[2], "a.TXT", "2", 1, 8);
    rc = try_unpack(m, 3, "eidos-mod bw 1");
    BW_CHECK_EQ("case-only duplicate refused", rc, -1);
    make_member(&m[1], "same", "1", 1, 8);
    make_member(&m[2], "same", "2", 1, 8);
    rc = try_unpack(m, 3, "eidos-mod bw 1");
    BW_CHECK_EQ("exact duplicate refused", rc, -1);
    make_member(&m[1], "x", "1", 1, 8);
    make_member(&m[2], "X/y", "2", 1, 8);
    rc = try_unpack(m, 3, "eidos-mod bw 1");
    BW_CHECK_EQ("file and folder same name refused", rc, -1);
    make_member(&m[1], "x", "1", 1, 8);
    make_member(&m[2], "x-y/z", "2", 1, 8);
    make_member(&m[3], "x/w", "2", 1, 8);
    rc = try_unpack(m, 4, "eidos-mod bw 1");
    BW_CHECK_EQ("file and folder with a sibling between refused", rc, -1);

    /* 5. limits ------------------------------------------------------ */
    make_member(&m[0], "eidos_mod.txt", manifest, sizeof manifest - 1, 8);
    make_member(&m[1], "bomb.bin", "abc", 3, 8);
    m[1].usize_override = 100000;
    rc = try_unpack(m, 2, "eidos-mod bw 1");
    BW_CHECK_EQ("ratio over 1000 refused", rc, -1);
    BW_CHECK_EQ("ratio message", strstr(g_err, "expands") != NULL, 1);
    make_member(&m[1], "huge.bin", "abc", 3, 8);
    m[1].usize_override = 0x40000001L;
    rc = try_unpack(m, 2, "eidos-mod bw 1");
    BW_CHECK_EQ("member over 1 GiB refused", rc, -1);
    BW_CHECK_EQ("1 GiB message", strstr(g_err, "1 GiB") != NULL, 1);
    {
        static u8 pad[1100000];
        int i;

        make_member(&m[0], "eidos_mod.txt", manifest, sizeof manifest - 1, 8);
        for (i = 1; i <= 5; i++) {
            static const char *const names[] = { "", "t1", "t2", "t3", "t4", "t5" };

            make_member(&m[i], names[i], "", 0, 8);
            m[i].rawc = pad;
            m[i].rawc_len = sizeof pad;
            m[i].usize_override = 1L << 30;
        }
        rc = try_unpack(m, 6, "eidos-mod bw 1");
        BW_CHECK_EQ("total over 4 GiB refused", rc, -1);
        BW_CHECK_EQ("4 GiB message", strstr(g_err, "4 GiB") != NULL, 1);
        BW_CHECK_EQ("total: no cache", exists(WORK "/cache") || exists(WORK "/cache.part"), 0);
    }

    /* 6. damaged data: each as the LAST member ------------------------ */
    {
        u8 bad_block[] = { 0x07, 0x00 };           /* block type 3 */
        Buf d;
        size_t i;
        static const char *const names[] = { "a.bin", "b.bin" };

        deflate_fixed(big, 4000, &d);

        make_member(&m[0], "eidos_mod.txt", manifest, sizeof manifest - 1, 8);
        make_member(&m[1], "first.bin", big2, sizeof big2, 8);
        make_member(&m[2], "bad.bin", big, 4000, 8);
        m[2].rawc = bad_block;
        m[2].rawc_len = sizeof bad_block;
        rc = try_unpack(m, 3, "eidos-mod bw 1");
        BW_CHECK_EQ("corrupt deflate refused", rc, -1);
        BW_CHECK_EQ("corrupt deflate: nothing left", exists(WORK "/cache") || exists(WORK "/cache.part/first.bin"), 0);

        m[2].rawc = d.p;
        m[2].rawc_len = d.n / 2;                   /* truncated stream */
        rc = try_unpack(m, 3, "eidos-mod bw 1");
        BW_CHECK_EQ("truncated deflate refused", rc, -1);
        BW_CHECK_EQ("truncated: nothing left", exists(WORK "/cache") || exists(WORK "/cache.part/first.bin"), 0);

        m[2].rawc = NULL;
        m[2].bad_crc = 1;
        rc = try_unpack(m, 3, "eidos-mod bw 1");
        BW_CHECK_EQ("crc mismatch (deflate) refused", rc, -1);
        BW_CHECK_EQ("crc message", strstr(g_err, "CRC") != NULL, 1);
        BW_CHECK_EQ("crc: nothing left", exists(WORK "/cache") || exists(WORK "/cache.part/first.bin"), 0);

        make_member(&m[2], "bad.bin", big2, sizeof big2, 0);
        m[2].bad_crc = 1;
        rc = try_unpack(m, 3, "eidos-mod bw 1");
        BW_CHECK_EQ("crc mismatch (stored) refused", rc, -1);
        BW_CHECK_EQ("crc stored: no .part", exists(WORK "/cache.part"), 0);

        /* a stream that decodes to a different size than the header says */
        make_member(&m[2], "short.bin", big, 4000, 8);
        m[2].usize_override = 5000;
        rc = try_unpack(m, 3, "eidos-mod bw 1");
        BW_CHECK_EQ("size mismatch refused", rc, -1);
        m[2].usize_override = 3000;
        rc = try_unpack(m, 3, "eidos-mod bw 1");
        BW_CHECK_EQ("stream longer than stated refused", rc, -1);
        free(d.p);

        /* inflate alone: never reads or writes outside its buffers --- */
        deflate_fixed(big, 4000, &d);
        for (i = 0; i < d.n; i++) {
            u8 out[4000];
            /* every truncation fails cleanly */
            if (bw_inflate(d.p, i, out, sizeof out, NULL) == BW_INFLATE_OK)
                break;
        }
        BW_CHECK_EQ("every truncation fails", i, d.n);
        {
            u8 *out = (u8 *)malloc(4000);
            size_t used = 0;
            int r = bw_inflate(d.p, d.n, out, 4000, &used);

            BW_CHECK_EQ("whole stream ok", r, 0);
            BW_CHECK_EQ("src_used is the stream length", used, d.n);
            BW_CHECK_EQ("round trip", memcmp(out, big, 4000), 0);
            BW_CHECK_EQ("dst too small", bw_inflate(d.p, d.n, out, 3999, NULL), BW_INFLATE_OVERFLOW);
            free(out);
        }
        /* flip bits everywhere: must return, never crash */
        {
            u8 *mut = (u8 *)malloc(d.n);
            u8 out[4000];
            int fails = 0;

            for (i = 0; i < d.n * 8; i++) {
                memcpy(mut, d.p, d.n);
                mut[i / 8] ^= (u8)(1u << (i % 8));
                if (bw_inflate(mut, d.n, out, sizeof out, NULL) != 0)
                    fails++;
            }
            BW_CHECK_EQ("bit flips handled (most fail)", fails > 100, 1);
            free(mut);
        }
        free(d.p);
        (void)names;
    }

    /* inflate: a stored block and a hand-made dynamic block ----------- */
    {
        u8 stored[] = { 0x01, 0x03, 0x00, 0xFC, 0xFF, 'a', 'b', 'c' };
        u8 out[3];
        u8 badlen[] = { 0x01, 0x03, 0x00, 0x00, 0x00, 'a', 'b', 'c' };

        BW_CHECK_EQ("stored block", bw_inflate(stored, sizeof stored, out, 3, NULL), 0);
        BW_CHECK_EQ("stored content", memcmp(out, "abc", 3), 0);
        BW_CHECK_EQ("stored bad length check", bw_inflate(badlen, sizeof badlen, out, 3, NULL) != 0, 1);
        BW_CHECK_EQ("crc32 of 123456789", bw_crc32(0, (const u8 *)"123456789", 9), (long)0xCBF43926u);
        BW_CHECK_EQ("crc32 in pieces", bw_crc32(bw_crc32(0, (const u8 *)"1234", 4), (const u8 *)"56789", 5), (long)0xCBF43926u);
    }

    /* 8. cache path ---------------------------------------------------- */
    {
        char p1[256], p2[256], p3[256];
        Buf z2;

        make_member(&m[0], "eidos_mod.txt", manifest, sizeof manifest - 1, 8);
        build_zip(&z, m, 1, "eidos-mod bw 1");
        write_file(WORK "/My Mod!.bw", z.p, z.n);
        BW_CHECK_EQ("cache path ok", bw_mod_bw_cache_path(WORK "/My Mod!.bw", p1, sizeof p1), 0);
        BW_CHECK_EQ("cache path again same", bw_mod_bw_cache_path(WORK "/My Mod!.bw", p2, sizeof p2), 0);
        BW_CHECK_EQ("cache path stable", strcmp(p1, p2), 0);
        BW_CHECK_EQ("cache path has stem", strstr(p1, "/bw-cache/My_Mod_-") != NULL, 1);
        BW_CHECK_EQ("cache path has no .bw", strstr(p1, ".bw") == NULL, 1);
        z.p[z.n - 20] ^= 1;
        write_file(WORK "/My Mod!.bw", z.p, z.n);
        BW_CHECK_EQ("cache path changed file", bw_mod_bw_cache_path(WORK "/My Mod!.bw", p3, sizeof p3), 0);
        BW_CHECK_EQ("cache path differs after change", strcmp(p1, p3) != 0, 1);
        BW_CHECK_EQ("cache path too small", bw_mod_bw_cache_path(WORK "/My Mod!.bw", p3, 10), -1);
        BW_CHECK_EQ("cache path missing file", bw_mod_bw_cache_path(WORK "/nope.bw", p3, sizeof p3), -1);
        (void)z2;
        free(z.p);

        /* unpack to the named cache folder, nested parents created */
        build_zip(&z, m, 1, "eidos-mod bw 1");
        write_file(WORK "/real.bw", z.p, z.n);
        free(z.p);
        BW_CHECK_EQ("cache path real", bw_mod_bw_cache_path(WORK "/real.bw", p1, sizeof p1), 0);
        BW_CHECK_EQ("unpack into named cache", bw_mod_bw_unpack(WORK "/real.bw", p1, g_err, sizeof g_err), 0);
        {
            char f[300];

            sprintf(f, "%s/eidos_mod.txt", p1);
            BW_CHECK_EQ("named cache has manifest", same_file(f, manifest, sizeof manifest - 1), 1);
            remove(f);
            BW_RMDIR_OK(p1);
            BW_RMDIR_OK(WORK "/bw-cache");
        }
    }

    /* bad inputs ------------------------------------------------------- */
    BW_CHECK_EQ("missing package", bw_mod_bw_unpack(WORK "/nope.bw", WORK "/cache2", g_err, sizeof g_err), -1);
    {
        u8 junk[100];

        memset(junk, 7, sizeof junk);
        write_file(WORK "/junk.bw", junk, sizeof junk);
        BW_CHECK_EQ("not a zip", bw_mod_bw_unpack(WORK "/junk.bw", WORK "/cache2", g_err, sizeof g_err), -1);
        write_file(WORK "/tiny.bw", junk, 5);
        BW_CHECK_EQ("tiny file", bw_mod_bw_unpack(WORK "/tiny.bw", WORK "/cache2", g_err, sizeof g_err), -1);
        BW_CHECK_EQ("NULL err ok", bw_mod_bw_unpack(WORK "/junk.bw", WORK "/cache2", NULL, 0), -1);
        BW_CHECK_EQ("no cache2", exists(WORK "/cache2"), 0);
    }
    /* a damaged central directory */
    {
        make_member(&m[0], "eidos_mod.txt", manifest, sizeof manifest - 1, 8);
        build_zip(&z, m, 1, "eidos-mod bw 1");
        {
            size_t eocd = z.n - 22 - 14;
            u32 cdoff = (u32)z.p[eocd + 16] | ((u32)z.p[eocd + 17] << 8);

            z.p[cdoff] ^= 0xFF;                      /* break the signature */
        }
        write_file(WORK "/dmg.bw", z.p, z.n);
        BW_CHECK_EQ("damaged central directory", bw_mod_bw_unpack(WORK "/dmg.bw", WORK "/cache2", g_err, sizeof g_err), -1);
        free(z.p);
    }
    return 0;
}

/* ---- Python-made package (dynamic Huffman blocks) -------------------- */

static int run_python(void)
{
    static const char script[] =
        "import sys, zipfile\n"
        "root, out = sys.argv[1], sys.argv[2]\n"
        "names = sys.argv[3:]\n"
        "z = zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED)\n"
        "z.comment = b'eidos-mod bw 1'\n"
        "for n in names:\n"
        "    z.write(root + '/' + n, n)\n"
        "z.close()\n";
    static const char *const names[] = {
        "eidos_mod.txt", "text/one.txt", "text/two.txt", "data/blob.bin"
    };
    char cmd[1024];
    u8 *d[4];
    size_t len[4] = { 40, 30000, 70000, 50000 };
    int i, rc;

    if (system("python -c \"import zipfile\" >nul 2>&1") != 0
        && system("python -c \"import zipfile\" >/dev/null 2>&1") != 0) {
        printf("SKIP python package (python not found)\n");
        return 0;
    }
    bw_plat_mkdirs(WORK "/pyfix/text");
    bw_plat_mkdirs(WORK "/pyfix/data");
    write_file(WORK "/mk.py", (const u8 *)script, sizeof script - 1);
    for (i = 0; i < 4; i++) {
        char p[128];

        d[i] = (u8 *)malloc(len[i]);
        gen(d[i], len[i], (u32)(30 + i));
        if (i == 3) {                       /* mix random and repeated parts */
            size_t k;
            u32 s = 12345;

            for (k = 0; k < len[i] / 2; k++) {
                s = s * 1103515245u + 12345u;
                d[i][k] = (u8)(s >> 16);
            }
        }
        sprintf(p, "%s/pyfix/%s", WORK, names[i]);
        write_file(p, d[i], len[i]);
    }
    sprintf(cmd, "python %s/mk.py %s/pyfix %s/py.bw %s %s %s %s", WORK, WORK, WORK,
            names[0], names[1], names[2], names[3]);
    if (system(cmd) != 0) {
        printf("SKIP python package (script failed)\n");
        return 0;
    }
    rc = bw_mod_bw_unpack(WORK "/py.bw", WORK "/pycache", g_err, sizeof g_err);
    BW_CHECK_EQ("python package: rc", rc, 0);
    if (rc != 0)
        printf("  err: %s\n", g_err);
    for (i = 0; i < 4; i++) {
        char p[128], label[64];

        sprintf(p, "%s/pycache/%s", WORK, names[i]);
        sprintf(label, "python package: %s", names[i]);
        BW_CHECK_EQ(label, same_file(p, d[i], len[i]), 1);
        remove(p);
        free(d[i]);
    }
    BW_RMDIR_OK(WORK "/pycache/text");
    BW_RMDIR_OK(WORK "/pycache/data");
    BW_RMDIR_OK(WORK "/pycache");
    return 0;
}

int main(void)
{
    printf("test_mod_bw\n");
    run_cases();
    run_python();
    BW_REPORT("test_mod_bw");
}
