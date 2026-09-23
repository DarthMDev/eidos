/*
 * install/install.h -- the first-run extractor: the player's ROM in, the
 * data root the port reads out.
 *
 * This is the core of the installer eidos is building towards: a wizard
 * that takes the player's own dump of Pokemon Black (and eventually White)
 * and leaves a playable game behind, on Windows, macOS, Linux, Android and
 * iOS.  Everything platform-specific (file pickers, the progress screen,
 * where the data root lives) belongs to the front end; this library is
 * plain C99 and the standard library, and does the part that is the same
 * everywhere:
 *
 *   1. identify: read the cartridge header, hash the whole image, and say
 *      which game and which dump it is (bw_rom_identify);
 *   2. extract: write the tree aletheia's tools/ndsrom.py extract writes
 *      (the NitroFS files under data/, the ARM9/ARM7 binaries, the overlay
 *      files and tables, the banner and the DSi regions), plus
 *      arm9_dec.bin, the BLZ-decompressed ARM9 image tools/delink.py
 *      writes, which is what the port reads its ARM9 constant tables out of
 *      (bw_rom_extract).
 *
 * The layout is ndsrom.py's on purpose: aletheia's asset tools, eidos's
 * oracles and eidos itself already read it, and tests/test_extract.c checks
 * the two extractors against each other file for file.  What this does not
 * write is ndsrom.py's rebuild bookkeeping (residue/ and its manifest.json),
 * which only matters for turning the tree back into a ROM; it writes
 * eidos_manifest.json instead, recording what was extracted from what.
 *
 * The ROM is treated as untrusted input: every offset and size is checked
 * against the image before it is used, file names are sanitised exactly as
 * ndsrom.py sanitises them (so no name can leave the output directory or be
 * a reserved Windows name), and a malformed image is an error with a
 * message, never a crash.  Paths are UTF-8 on every platform (on Windows
 * they are converted for the wide-character API), since the one path an
 * installer cannot choose is the one the player's ROM is at.
 *
 * The image is streamed: the ROM is read in blocks for the hash and the
 * copies rather than loaded whole, which matters on a phone.  Only the
 * header, FNT, FAT, overlay tables and ARM9 binary are held in memory.
 */

#ifndef EIDOS_INSTALL_H
#define EIDOS_INSTALL_H

#include <stddef.h>

#include "types.h"

#define BW_INSTALL_VERSION "1"

/* ------------------------------------------------------------------ */
/* SHA-1                                                               */
/* ------------------------------------------------------------------ */

typedef struct BwSha1 {
    u32 h[5];
    u64 length;        /* bytes hashed so far */
    u8  block[64];
    u32 used;          /* bytes in `block` */
} BwSha1;

void bw_sha1_init(BwSha1 *s);
void bw_sha1_update(BwSha1 *s, const void *data, size_t len);
void bw_sha1_final(BwSha1 *s, u8 digest[20]);
/* Lower-case hex, 41 bytes with the terminator. */
void bw_sha1_hex(const u8 digest[20], char out[41]);

/* ------------------------------------------------------------------ */
/* BLZ (the SDK's backwards LZ, which the ARM9 binary is packed with)  */
/* ------------------------------------------------------------------ */

/*
 * The decompressed size of the BLZ container `in` (the whole container, its
 * footer at the end), or 0 with `err` filled if the footer is not a
 * consistent one.  A container whose footer says "not compressed" (its last
 * word is 0) decompresses to itself.
 */
u32 bw_blz_size(const u8 *in, u32 in_len, char *err, size_t err_len);

/*
 * Decompress `in` into `out`, which must hold exactly bw_blz_size() bytes.
 * 0 on success, -1 with `err` filled on a malformed stream.  The checks are
 * aletheia's tools/blz.py's, in the same order, so the two refuse the same
 * inputs.
 */
int bw_blz_decompress(const u8 *in, u32 in_len, u8 *out, u32 out_len,
                      char *err, size_t err_len);

/* ------------------------------------------------------------------ */
/* Identifying a ROM                                                   */
/* ------------------------------------------------------------------ */

typedef enum BwGame {
    BW_GAME_UNKNOWN = 0,
    BW_GAME_BLACK,          /* Pokemon Black, USA/Europe, NDSi enhanced */
    BW_GAME_WHITE           /* not yet supported; recognised by game code */
} BwGame;

typedef struct BwRomInfo {
    char   title[13];       /* header 0x000, NUL-terminated */
    char   gamecode[5];     /* header 0x00C */
    char   maker[3];        /* header 0x010 */
    u8     unit_code;       /* header 0x012; bit 1 = DSi (TWL) aware */
    u32    rom_size;        /* bytes in the file */
    u8     sha1[20];
    char   sha1_hex[41];
    BwGame game;            /* from the game code */
    int    verified;        /* 1 if the SHA-1 is a known good dump */
} BwRomInfo;

/* The one dump aletheia is built against (README.md there). */
#define BW_BLACK_SHA1 "26ad0b9967aa279c4a266ee69f52b9b2332399a5"
#define BW_BLACK_SIZE 268435456u

typedef void (*BwProgressFn)(void *ctx, u32 done, u32 total, const char *what);

/*
 * Read the header and hash the whole image.  0 if the file is readable and
 * looks like a DS cartridge image at all (a header that fits, offsets
 * inside the file); -1 with `err` filled otherwise.  Whether it is a game
 * the port supports is `info->game` and `info->verified`, which the caller
 * decides what to do with.  `progress` (may be NULL) is told how far the
 * hash has got.
 */
int bw_rom_identify(const char *rom_path, BwRomInfo *info,
                    BwProgressFn progress, void *ctx,
                    char *err, size_t err_len);

/* ------------------------------------------------------------------ */
/* Extracting                                                          */
/* ------------------------------------------------------------------ */

typedef struct BwExtractOptions {
    /* The game code the image must carry.  NULL accepts any; the default
     * front end passes "IRBO", which is what ndsrom.py insists on. */
    const char *require_gamecode;
    /* Skip arm9_dec.bin (the only step that decompresses anything). */
    int no_arm9_decompress;
} BwExtractOptions;

typedef struct BwExtractResult {
    u32 fs_files;           /* NitroFS files written under data/ */
    u32 overlays;           /* overlay files written under overlays/ */
    u32 orphans;            /* FAT entries nothing named */
    u32 regions;            /* every non-empty file written */
    u64 bytes;              /* bytes written, all files */
    u32 arm9_dec_size;      /* 0 if not written */
} BwExtractResult;

/*
 * Extract `rom_path` into `out_dir` (created if missing).  0 on success, -1
 * with `err` filled on the first problem; a failed extraction may leave a
 * partial tree behind, which the caller should discard.  `progress` (may be
 * NULL) is called as files are written, with bytes done and bytes total.
 */
int bw_rom_extract(const char *rom_path, const char *out_dir,
                   const BwExtractOptions *opt, BwExtractResult *result,
                   BwProgressFn progress, void *ctx,
                   char *err, size_t err_len);

/*
 * The same name mangling ndsrom.py applies to an FNT name, exposed for the
 * tests: `raw` is `len` bytes straight from the FNT, `out` receives a
 * NUL-terminated name safe on every filesystem the port targets.
 */
void bw_fnt_sanitize(const u8 *raw, u32 len, char *out, size_t out_size);

/* ------------------------------------------------------------------ */
/* Files, with UTF-8 paths everywhere                                  */
/* ------------------------------------------------------------------ */

#include <stdio.h>

FILE *bw_fopen_utf8(const char *path, const char *mode);
/* mkdir -p.  0 on success (or if it already exists). */
int   bw_mkdirs_utf8(const char *path);

#endif /* EIDOS_INSTALL_H */
