/*
 * tests/test_extract.c -- the first-run extractor (install/).
 *
 * No game data.  Four parts:
 *
 *   1. SHA-1 against the FIPS 180 test vectors, including the million-'a'
 *      message fed in awkward chunk sizes.
 *   2. The BLZ decoder on containers built by hand, every byte worked out
 *      on paper: stored, literals only, a match, a match that overlaps its
 *      own output, and each of the footer and stream refusals blz.py makes.
 *   3. The name rules against ndsrom.py's sanitize_name, case by case.
 *   4. A whole synthetic cartridge (tools/synth_rom.py, argv[1]): identified,
 *      extracted, and compared file for file, by SHA-1, with what aletheia's
 *      tools/ndsrom.py extract and tools/blz.py produce for the same image.
 *      Then a dozen deliberately broken copies of it, each of which must be
 *      refused with a message and without writing anything, and a few
 *      hundred randomly damaged copies, each of which must be either
 *      extracted or refused and never crash (run it under ASan: tools/check.py
 *      does).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "install.h"
#include "port_test.h"

/* ------------------------------------------------------------------ */
/* 1. SHA-1                                                            */
/* ------------------------------------------------------------------ */

static void sha1_of(const void *data, size_t len, char hex[41])
{
    BwSha1 s;
    u8 d[20];

    bw_sha1_init(&s);
    bw_sha1_update(&s, data, len);
    bw_sha1_final(&s, d);
    bw_sha1_hex(d, hex);
}

static int test_sha1(void)
{
    char hex[41];
    static const char *const two_blocks =
        "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    BwSha1 s;
    u8 d[20];
    static u8 a[1000];
    u32 i;

    sha1_of("", 0, hex);
    BW_CHECK_EQ("SHA-1 of the empty string",
                strcmp(hex, "da39a3ee5e6b4b0d3255bfef95601890afd80709"), 0);
    sha1_of("abc", 3, hex);
    BW_CHECK_EQ("SHA-1 of abc",
                strcmp(hex, "a9993e364706816aba3e25717850c26c9cd0d89d"), 0);
    sha1_of(two_blocks, strlen(two_blocks), hex);
    BW_CHECK_EQ("SHA-1 of the 448-bit message (padding into a second block)",
                strcmp(hex, "84983e441c3bd26ebaae4aa1f95129e5e54670f1"), 0);

    /* A million 'a's, in chunks of 1, 63, 64, 65 and 999 bytes: the
     * buffering across block boundaries is what is under test. */
    memset(a, 'a', sizeof(a));
    bw_sha1_init(&s);
    {
        static const u32 chunks[] = { 1, 63, 64, 65, 999 };
        u32 fed = 0;
        i = 0;
        while (fed < 1000000u) {
            u32 n = chunks[i++ % 5];
            if (n > 1000000u - fed) {
                n = 1000000u - fed;
            }
            bw_sha1_update(&s, a, n);
            fed += n;
        }
    }
    bw_sha1_final(&s, d);
    bw_sha1_hex(d, hex);
    BW_CHECK_EQ("SHA-1 of a million a, fed in odd chunks",
                strcmp(hex, "34aa973cd4c4daa4f61eeb2bdbad27316534016f"), 0);
    return 0;
}

/* ------------------------------------------------------------------ */
/* 2. BLZ                                                              */
/* ------------------------------------------------------------------ */

static void put32(u8 *p, u32 v)
{
    p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24);
}

/* A container: `prefix` verbatim, `stream` (already in container order,
 * i.e. reversed), `pad` bytes of 0xFF, then the 8-byte footer. */
static u32 blz_make(u8 *out, const u8 *prefix, u32 np, const u8 *stream,
                    u32 ns, u32 pad, u32 inc)
{
    u32 hdr = 8 + pad;
    u32 enc = ns + hdr;

    memcpy(out, prefix, np);
    memcpy(out + np, stream, ns);
    memset(out + np + ns, 0xFF, pad);
    put32(out + np + ns + pad, (hdr << 24) | enc);
    put32(out + np + ns + pad + 4, inc);
    return np + ns + pad + 8;
}

static int test_blz(void)
{
    u8 c[64];
    u8 o[64];
    char err[200];
    u32 n;

    /* Stored: the last word zero.  The container is the image. */
    memcpy(c, "stored!!\0\0\0\0\0\0\0\0", 16);
    BW_CHECK_EQ("stored: size is the container's", bw_blz_size(c, 16, err, sizeof(err)), 16);
    BW_CHECK_EQ("stored: decompresses", bw_blz_decompress(c, 16, o, 16, err, sizeof(err)), 0);
    BW_CHECK_EQ("stored: to itself", memcmp(o, c, 16), 0);

    /* Literals only: flags 0x00 then 'D','C','B','A' forwards, i.e.
     * A B C D 00 in the container, after the prefix "pre".  A literal run
     * never pays for its flag bytes, so an honest footer cannot describe
     * it as compressed; claim one byte more than the stream makes and the
     * decoder must refuse rather than read past the stream. */
    {
        static const u8 prefix[3] = { 'p', 'r', 'e' };
        /* forwards: flags=0x00, 'D','C','B','A' -> container: A B C D 00 */
        static const u8 stream[5] = { 'A', 'B', 'C', 'D', 0x00 };
        /* image = prefix (3) + 4 = 7; container = 3 + 5 + 8 = 16;
         * raw_len = dec + enc + inc = 3 + 13 + inc = 7 needs inc < 0, which
         * the format cannot say: a literal-only stream never pays for its
         * own flag bytes, so the SDK stores such data instead.  What the
         * decoder must do with an honest footer is still defined: build
         * one that describes the image as 3 + 13 + inc with inc = 1 and
         * let the stream fall one byte short. */
        n = blz_make(c, prefix, 3, stream, 5, 0, 1);
        BW_CHECK_EQ("literals: size", bw_blz_size(c, n, err, sizeof(err)), 17);
        BW_CHECK_EQ("literals: a stream that runs out is refused",
                    bw_blz_decompress(c, n, o, 17, err, sizeof(err)), -1);
        BW_REQUIRE("... saying so", strstr(err, "ran out") != NULL);
    }

    /* A match.  Image = "xy" + "ABCABCABCABC" (14 bytes).  Decoding runs
     * from the image's end: the last three bytes 'C','B','A' are literals
     * (produced[] = C B A), then the remaining 9 bytes are one match of
     * length 9 at displacement 3 (it overlaps its own output, which is
     * the case a naive memcpy gets wrong).  Token: len-3 = 6 in the top
     * nibble, disp-3 = 0 below: 0x6000.  Flags: three literals, then a
     * match: 0b00010000 = 0x10.  Forwards stream: 10 'C' 'B' 'A' 60 00,
     * reversed into the container: 00 60 'A' 'B' 'C' 10.  Prefix "xy".
     * Container = 2 + 6 + 8 = 16, image 14, so the footer's inc is
     * 14 - 16 < 0 again -- pad the image instead: prefix "xy" and a
     * 30-byte run, "ABC" x 10. */
    {
        static const u8 prefix[2] = { 'x', 'y' };
        static const u8 stream[6] = { 0x00, 0xF0, 'A', 'B', 'C', 0x10 };
        /* forwards: 10 'C' 'B' 'A' F0 00 -> literals C,B,A then a match of
         * len (0xF)+3 = 18 at disp 0+3 = 3: 21 bytes, "ABC" x 7. */
        static const char *want = "xyABCABCABCABCABCABCABC";
        u32 raw;

        n = blz_make(c, prefix, 2, stream, 6, 0, 23 - 16);
        raw = bw_blz_size(c, n, err, sizeof(err));
        BW_CHECK_EQ("match: size", raw, 23);
        BW_CHECK_EQ("match: decompresses", bw_blz_decompress(c, n, o, raw, err, sizeof(err)), 0);
        BW_CHECK_EQ("match: an overlapping copy, byte for byte", memcmp(o, want, 23), 0);

        /* Two bytes of padding before the footer (hdr_len 0x0A) are not
         * stream.  With the footer claiming a 25-byte image the stream (21
         * bytes of output) falls two short, and that is refused. */
        n = blz_make(c, prefix, 2, stream, 6, 2, 7);
        raw = bw_blz_size(c, n, err, sizeof(err));
        BW_CHECK_EQ("padded: size", raw, 25);
        BW_CHECK_EQ("padded: a short stream is refused",
                    bw_blz_decompress(c, n, o, raw, err, sizeof(err)), -1);
    }

    /* Refusals, blz.py's, in its order. */
    BW_CHECK_EQ("a container under 8 bytes", bw_blz_size(c, 7, err, sizeof(err)), 0);
    memset(c, 0, 16);
    put32(c + 8, (0x20u << 24) | 8u);
    put32(c + 12, 4);
    BW_CHECK_EQ("a header length outside 8..0x0B", bw_blz_size(c, 16, err, sizeof(err)), 0);
    BW_REQUIRE("... saying so", strstr(err, "header length") != NULL);
    put32(c + 8, (0x08u << 24) | 0x100u);
    BW_CHECK_EQ("an encoded length past the container", bw_blz_size(c, 16, err, sizeof(err)), 0);
    put32(c + 8, (0x0Bu << 24) | 0x09u);
    BW_CHECK_EQ("an encoded length under the header length", bw_blz_size(c, 16, err, sizeof(err)), 0);
    put32(c + 8, (0x08u << 24) | 0x0Bu);    /* enc 11: 3 stream bytes */
    put32(c + 12, 8);
    c[7] = 0x80;                             /* flags (read first, from the end): a match */
    c[6] = 0x00;
    c[5] = 0x00;                             /* token 0x0000 */
    BW_CHECK_EQ("a match before anything was produced",
                bw_blz_decompress(c, 16, o, bw_blz_size(c, 16, err, sizeof(err)),
                                  err, sizeof(err)), -1);
    BW_REQUIRE("... reaches back past the decoded region", strstr(err, "back") != NULL);
    BW_CHECK_EQ("a wrong output size is refused",
                bw_blz_decompress(c, 16, o, 3, err, sizeof(err)), -1);
    return 0;
}

/* ------------------------------------------------------------------ */
/* 3. Names                                                            */
/* ------------------------------------------------------------------ */

static int name_is(const char *raw, u32 len, const char *want)
{
    char out[300];

    bw_fnt_sanitize((const u8 *)raw, len, out, sizeof(out));
    if (strcmp(out, want) != 0) {
        printf("    sanitize(%.*s) = %s, want %s\n", (int)len, raw, out, want);
        return 0;
    }
    return 1;
}

static int test_names(void)
{
    BW_CHECK_EQ("a plain name", name_is("a", 1, "a"), 1);
    BW_CHECK_EQ("a plain file name", name_is("title.narc", 10, "title.narc"), 1);
    BW_CHECK_EQ("a slash", name_is("a/b", 3, "a_b"), 1);
    BW_CHECK_EQ("every refused character", name_is("<>:\"\\|?*", 8, "________"), 1);
    BW_CHECK_EQ("control bytes", name_is("a\x01\x1f" "b", 4, "a__b"), 1);
    BW_CHECK_EQ("DEL is ASCII and kept", name_is("a\x7f", 2, "a\x7f"), 1);
    BW_CHECK_EQ("a non-ASCII byte", name_is("caf\xe9", 4, "caf_"), 1);
    BW_CHECK_EQ("trailing dot and space", name_is("trail. ", 7, "trail"), 1);
    BW_CHECK_EQ("dot-dot cannot climb", name_is("..", 2, "_"), 1);
    BW_CHECK_EQ("all spaces", name_is("   ", 3, "_"), 1);
    BW_CHECK_EQ("CON", name_is("CON", 3, "_CON"), 1);
    BW_CHECK_EQ("con, any case", name_is("con", 3, "_con"), 1);
    BW_CHECK_EQ("CON.txt", name_is("CON.txt", 7, "_CON.txt"), 1);
    BW_CHECK_EQ("LPT1.x.y", name_is("LPT1.x.y", 8, "_LPT1.x.y"), 1);
    BW_CHECK_EQ("COM10 is not reserved", name_is("COM10", 5, "COM10"), 1);
    BW_CHECK_EQ("CONSOLE is not reserved", name_is("CONSOLE", 7, "CONSOLE"), 1);
    BW_CHECK_EQ("nul. is NUL after the strip", name_is("nul.", 4, "_nul"), 1);
    return 0;
}

/* ------------------------------------------------------------------ */
/* 4. A whole cartridge                                                */
/* ------------------------------------------------------------------ */

static u8 *read_file(const char *path, u32 *len)
{
    FILE *fp = bw_fopen_utf8(path, "rb");
    long n;
    u8 *buf;

    if (fp == NULL) {
        return NULL;
    }
    fseek(fp, 0, SEEK_END);
    n = ftell(fp);
    rewind(fp);
    buf = (u8 *)malloc((size_t)n + 1);
    if (buf != NULL && fread(buf, 1, (size_t)n, fp) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    fclose(fp);
    *len = (u32)n;
    return buf;
}

static int write_file(const char *path, const u8 *data, u32 len)
{
    FILE *fp = bw_fopen_utf8(path, "wb");

    if (fp == NULL) {
        return -1;
    }
    fwrite(data, 1, len, fp);
    fclose(fp);
    return 0;
}

static int exists(const char *path)
{
    FILE *fp = bw_fopen_utf8(path, "rb");

    if (fp == NULL) {
        return 0;
    }
    fclose(fp);
    return 1;
}

static long expect_value(const char *dir, const char *key)
{
    char path[512];
    char k[64];
    char v[64];
    FILE *fp;
    long found = -1;

    snprintf(path, sizeof(path), "%s/expect.txt", dir);
    fp = fopen(path, "r");
    if (fp == NULL) {
        return -1;
    }
    while (fscanf(fp, "%63s %63s", k, v) == 2) {
        if (strcmp(k, key) == 0) {
            found = strtol(v, NULL, 0);
        }
    }
    fclose(fp);
    return found;
}

static int test_cartridge(const char *dir)
{
    char rom_path[512];
    char out[512];
    char path[2600];
    char line[1200];
    char want_sha[64];
    BwRomInfo info;
    BwExtractResult res;
    BwExtractOptions opt;
    char err[512];
    FILE *fp;
    u32 files = 0;
    u32 good = 0;

    snprintf(rom_path, sizeof(rom_path), "%s/synth.nds", dir);
    snprintf(out, sizeof(out), "%s/c_out", dir);

    /* Identify. */
    BW_REQUIRE("identify the synthetic image",
               bw_rom_identify(rom_path, &info, NULL, NULL, err, sizeof(err)) == 0);
    fp = fopen((snprintf(path, sizeof(path), "%s/expect.txt", dir), path), "r");
    BW_REQUIRE("expect.txt", fp != NULL);
    BW_REQUIRE("expect.txt has the sha1", fscanf(fp, "sha1 %63s", want_sha) == 1);
    fclose(fp);
    BW_CHECK_EQ("the SHA-1 is hashlib's", strcmp(info.sha1_hex, want_sha), 0);
    BW_CHECK_EQ("title", strcmp(info.title, "POKEMON B"), 0);
    BW_CHECK_EQ("game code", strcmp(info.gamecode, "IRBO"), 0);
    BW_CHECK_EQ("maker", strcmp(info.maker, "01"), 0);
    BW_CHECK_EQ("unit code (DSi enhanced)", info.unit_code, 2);
    BW_CHECK_EQ("size", info.rom_size, 0x100000);
    BW_CHECK_EQ("IRBO is Black", info.game, BW_GAME_BLACK);
    BW_CHECK_EQ("but not the verified dump", info.verified, 0);

    /* Extract. */
    memset(&opt, 0, sizeof(opt));
    opt.require_gamecode = "IRBO";
    if (bw_rom_extract(rom_path, out, &opt, &res, NULL, NULL, err, sizeof(err)) != 0) {
        printf("    extract: %s\n", err);
        BW_REQUIRE("extract the synthetic image", 0);
    }
    BW_CHECK_EQ("NitroFS files", res.fs_files, expect_value(dir, "fs_files"));
    BW_CHECK_EQ("overlays", res.overlays, expect_value(dir, "overlays"));
    BW_CHECK_EQ("orphans", res.orphans, expect_value(dir, "orphans"));
    BW_CHECK_EQ("arm9_dec.bin size", res.arm9_dec_size, expect_value(dir, "arm9_dec_size"));
    BW_CHECK_EQ("files written = the oracle's", res.regions, expect_value(dir, "files"));

    /* Every oracle file, by SHA-1. */
    snprintf(path, sizeof(path), "%s/oracle_files.txt", dir);
    fp = fopen(path, "r");
    BW_REQUIRE("oracle_files.txt", fp != NULL);
    while (fgets(line, sizeof(line), fp) != NULL) {
        char sha[64];
        char rel[1024];
        unsigned size;
        u32 len;
        u8 *data;

        if (sscanf(line, "%63s %u %1023[^\n]", sha, &size, rel) != 3) {
            continue;
        }
        files++;
        snprintf(path, sizeof(path), "%s/%s", out, rel);
        data = read_file(path, &len);
        if (data == NULL) {
            printf("    missing: %s\n", rel);
            continue;
        }
        {
            char hex[41];
            sha1_of(data, len, hex);
            if (len == size && strcmp(hex, sha) == 0) {
                good++;
            } else {
                printf("    differs: %s (%u bytes, want %u)\n", rel, len, size);
            }
        }
        free(data);
    }
    fclose(fp);
    BW_CHECK_EQ("oracle files listed", files, expect_value(dir, "files"));
    BW_CHECK_EQ("every one identical to ndsrom.py's / blz.py's", good, files);
    snprintf(path, sizeof(path), "%s/eidos_manifest.json", out);
    BW_CHECK_EQ("eidos_manifest.json written", exists(path), 1);
    snprintf(path, sizeof(path), "%s/data/a/0/0/2", out);
    BW_CHECK_EQ("a zero-length file is not written (as ndsrom.py)", exists(path), 0);

    /* Extracting over an existing tree gives the same tree. */
    BW_CHECK_EQ("extracting again over it",
                bw_rom_extract(rom_path, out, &opt, &res, NULL, NULL, err, sizeof(err)), 0);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Broken images                                                       */
/* ------------------------------------------------------------------ */

static u8 *g_rom;
static u32 g_rom_len;

static u32 H(u32 off) { return (u32)g_rom[off] | ((u32)g_rom[off + 1] << 8)
                                | ((u32)g_rom[off + 2] << 16) | ((u32)g_rom[off + 3] << 24); }

/* Refused, with `needle` in the message, and nothing written. */
static int refused(const char *dir, const char *label, const u8 *img, u32 len,
                   const char *require, const char *needle)
{
    char rom[512];
    char out[512];
    char probe[600];
    char err[512];
    BwExtractOptions opt;
    int rc;
    int ok = 1;

    snprintf(rom, sizeof(rom), "%s/broken.nds", dir);
    snprintf(out, sizeof(out), "%s/broken_out_%s", dir, label);
    write_file(rom, img, len);
    memset(&opt, 0, sizeof(opt));
    opt.require_gamecode = require;
    err[0] = '\0';
    rc = bw_rom_extract(rom, out, &opt, NULL, NULL, NULL, err, sizeof(err));
    if (rc != -1) {
        printf("    %s: accepted\n", label);
        ok = 0;
    } else if (strstr(err, needle) == NULL) {
        printf("    %s: refused, but with \"%s\"\n", label, err);
        ok = 0;
    }
    snprintf(probe, sizeof(probe), "%s/header.bin", out);
    if (exists(probe)) {
        printf("    %s: wrote files before refusing\n", label);
        ok = 0;
    }
    return ok;
}

static void set32(u8 *img, u32 off, u32 v) { put32(img + off, v); }

static int test_broken(const char *dir)
{
    u8 *img = (u8 *)malloc(g_rom_len);
    u32 fnt, fat, y9;

    BW_REQUIRE("scratch image", img != NULL);
    fnt = H(0x40);
    fat = H(0x48);
    y9 = H(0x50);

#define FRESH() memcpy(img, g_rom, g_rom_len)

    FRESH();
    BW_CHECK_EQ("a file smaller than a header", refused(dir, "tiny", img, 0x800, NULL, "smaller than"), 1);
    FRESH();
    BW_CHECK_EQ("the wrong game code", refused(dir, "gamecode", img, g_rom_len, "IRAO", "unexpected game code"), 1);
    FRESH(); set32(img, 0x84, 0x200);
    BW_CHECK_EQ("a header size under 0x1000", refused(dir, "hdrsize", img, g_rom_len, NULL, "implausibly small"), 1);
    FRESH(); set32(img, 0x20, g_rom_len - 0x10);
    BW_CHECK_EQ("ARM9 past the end", refused(dir, "arm9end", img, g_rom_len, NULL, "runs past the end"), 1);
    FRESH(); set32(img, 0x30, H(0x20));
    BW_CHECK_EQ("ARM7 on top of ARM9", refused(dir, "overlap", img, g_rom_len, NULL, "overlapping"), 1);
    FRESH(); set32(img, 0x4C, H(0x4C) - 4);
    BW_CHECK_EQ("a FAT size not a multiple of 8", refused(dir, "fatsize", img, g_rom_len, NULL, "multiple of 8"), 1);
    FRESH(); set32(img, fat + 8 * 3 + 4, H(fat + 8 * 3) - 1);
    BW_CHECK_EQ("a FAT entry ending before it starts", refused(dir, "fatorder", img, g_rom_len, NULL, "precedes"), 1);
    FRESH(); set32(img, fat + 8 * 3 + 4, g_rom_len + 1);
    BW_CHECK_EQ("a FAT entry past the end", refused(dir, "fatend", img, g_rom_len, NULL, "past the end"), 1);
    FRESH(); set32(img, y9 + 0x18, 0x7FFF);
    BW_CHECK_EQ("an overlay's file id outside the FAT", refused(dir, "ovfid", img, g_rom_len, NULL, "outside the FAT"), 1);
    FRESH(); set32(img, 0x54, 33);
    BW_CHECK_EQ("an overlay table not a multiple of 32", refused(dir, "ovsize", img, g_rom_len, NULL, "multiple of 32"), 1);
    FRESH(); img[fnt + 6] = 0; img[fnt + 7] = 0;
    BW_CHECK_EQ("an FNT with no directories", refused(dir, "fntdirs", img, g_rom_len, NULL, "directories"), 1);
    FRESH(); set32(img, fnt, 0xFFFF);
    BW_CHECK_EQ("a subtable outside the FNT", refused(dir, "fntsub", img, g_rom_len, NULL, "outside the FNT"), 1);
    /* The root's first entry is the directory "a" (0x81 'a' id): point its
     * id back at the root, making the tree a cycle. */
    FRESH();
    {
        u32 sub = fnt + H(fnt);
        BW_REQUIRE("the root's first entry is a directory", img[sub] == 0x81);
        img[sub + 2] = 0x00;
        img[sub + 3] = 0xF0;
    }
    BW_CHECK_EQ("a cyclic directory tree", refused(dir, "cycle", img, g_rom_len, NULL, "cyclic"), 1);
    /* Point the root's first file id at an overlay's. */
    FRESH(); img[fnt + 4] = 0; img[fnt + 5] = 0;
    BW_CHECK_EQ("a file claiming an overlay's id", refused(dir, "ovclaim", img, g_rom_len, NULL, "overlay"), 1);
    /* a/0/0 (directory 3, which holds files) starting at the root's first
     * file id, so two files get the same id. */
    FRESH(); img[fnt + 24 + 4] = img[fnt + 4]; img[fnt + 24 + 5] = img[fnt + 5];
    BW_CHECK_EQ("a file id listed twice", refused(dir, "dupfid", img, g_rom_len, NULL, "more than once"), 1);
    /* The ARM9 footer's header length. */
    FRESH(); img[H(0x20) + H(0x2C) - 5] = 0x40;
    BW_CHECK_EQ("a corrupt ARM9 footer", refused(dir, "blz", img, g_rom_len, NULL, "header length"), 1);

    free(img);
    return 0;
}

/* Random damage: extracted or refused, never a crash or a hang. */
static int test_fuzz(const char *dir)
{
    u8 *img = (u8 *)malloc(g_rom_len);
    char rom[512];
    char out[512];
    char err[512];
    u32 seed = 0xB1ACC0DEu;
    u32 i;
    u32 accepted = 0;
    u32 fnt = H(0x40);
    u32 fat = H(0x48);

    BW_REQUIRE("scratch image", img != NULL);
    snprintf(rom, sizeof(rom), "%s/fuzz.nds", dir);
    snprintf(out, sizeof(out), "%s/fuzz_out", dir);
    for (i = 0; i < 300; i++) {
        u32 k;
        u32 hits = 1 + i % 6;

        memcpy(img, g_rom, g_rom_len);
        for (k = 0; k < hits; k++) {
            u32 where;
            seed = seed * 1664525u + 1013904223u;
            switch ((seed >> 28) % 3) {
            case 0:  where = (seed >> 8) % 0x220; break;               /* header */
            case 1:  where = fnt + (seed >> 8) % 0xAC; break;          /* FNT */
            default: where = fat + (seed >> 8) % 0x78; break;          /* FAT */
            }
            seed = seed * 1664525u + 1013904223u;
            img[where] = (u8)(seed >> 24);
        }
        write_file(rom, img, g_rom_len);
        if (bw_rom_extract(rom, out, NULL, NULL, NULL, NULL, err, sizeof(err)) == 0) {
            accepted++;
        }
    }
    printf("fuzz: 300 damaged images, %u extracted, %u refused\n",
           (unsigned)accepted, (unsigned)(300 - accepted));
    BW_CHECK_EQ("fuzz ran to the end", 1, 1);
    free(img);
    return 0;
}

int main(int argc, char **argv)
{
    char path[512];

    if (test_sha1() || test_blz() || test_names()) {
        BW_REPORT("test_extract");
    }
    if (argc < 2) {
        printf("no synthetic cartridge directory given; parts 1-3 only\n");
        BW_REPORT("test_extract");
    }
    if (test_cartridge(argv[1]) != 0) {
        BW_REPORT("test_extract");
    }
    snprintf(path, sizeof(path), "%s/synth.nds", argv[1]);
    g_rom = read_file(path, &g_rom_len);
    BW_REQUIRE("read the synthetic image", g_rom != NULL);
    if (test_broken(argv[1]) || test_fuzz(argv[1])) {
        BW_REPORT("test_extract");
    }
    free(g_rom);
    BW_REPORT("test_extract");
}
