/*
 * install/extract_main.c -- eidos_extract, the extractor on the command line.
 *
 *     eidos_extract <rom.nds>                  identify only
 *     eidos_extract <rom.nds> <out_dir>        identify, then extract
 *     eidos_extract --any <rom.nds> <out_dir>  accept any game code
 *
 * The installer's front ends (the wizard on each platform) call the same
 * two functions this does; this is the version with no user interface
 * beyond a line of progress, for scripting and for checking a dump.  Exit
 * status: 0 extracted (or identified a supported dump), 1 refused or
 * failed, 2 usage.
 */

#include <stdio.h>
#include <string.h>

#include "install.h"

#ifdef _WIN32
#include <stdlib.h>
#include <windows.h>
#include <shellapi.h>

/*
 * On Windows `argv` is in the ANSI code page (1252 on a Western install),
 * not UTF-8, so a ROM in "C:\\Users\\Andre\\Téléchargements" would arrive
 * as bytes install/ cannot open.  Take the command line as UTF-16 and hand
 * the library UTF-8, which is what every bw_ path function expects.
 */
static char **utf8_argv(int *argc)
{
    LPWSTR *w = CommandLineToArgvW(GetCommandLineW(), argc);
    char **out;
    int i;

    if (w == NULL) {
        return NULL;
    }
    out = (char **)calloc((size_t)*argc + 1, sizeof(char *));
    for (i = 0; out != NULL && i < *argc; i++) {
        int n = WideCharToMultiByte(CP_UTF8, 0, w[i], -1, NULL, 0, NULL, NULL);
        out[i] = (char *)malloc((size_t)n);
        if (out[i] == NULL) {
            return NULL;
        }
        WideCharToMultiByte(CP_UTF8, 0, w[i], -1, out[i], n, NULL, NULL);
    }
    LocalFree(w);
    return out;
}
#endif

static void progress(void *ctx, u32 done, u32 total, const char *what)
{
    static int last = -1;
    int pct = total ? (int)((u64)done * 100u / total) : 100;

    (void)what;
    if (pct != last) {
        fprintf(stderr, "\r  %s %3d%%", (const char *)ctx, pct);
        if (pct == 100) {
            fputc('\n', stderr);
        }
        last = pct == 100 ? -1 : pct;
    }
}

int main(int argc, char **argv)
{
    BwRomInfo info;
    BwExtractOptions opt;
    BwExtractResult res;
    char err[512];
    int any = 0;
    int a = 1;

#ifdef _WIN32
    {
        char **u = utf8_argv(&argc);
        if (u != NULL) {
            argv = u;
        }
    }
#endif

    if (a < argc && strcmp(argv[a], "--any") == 0) {
        any = 1;
        a++;
    }
    if (argc - a < 1 || argc - a > 2) {
        fprintf(stderr, "usage: %s [--any] <rom.nds> [out_dir]\n", argv[0]);
        return 2;
    }

    if (bw_rom_identify(argv[a], &info, progress, (void *)"hashing  ",
                        err, sizeof(err)) != 0) {
        fprintf(stderr, "eidos_extract: %s\n", err);
        return 1;
    }
    printf("title     %s\n", info.title);
    printf("gamecode  %s (maker %s, unit code 0x%02X%s)\n", info.gamecode,
           info.maker, info.unit_code, (info.unit_code & 2) ? ", DSi enhanced" : "");
    printf("size      %u bytes\n", (unsigned)info.rom_size);
    printf("sha1      %s\n", info.sha1_hex);
    printf("game      %s\n",
           info.game == BW_GAME_BLACK ? "Pokemon Black" :
           info.game == BW_GAME_WHITE ? "Pokemon White (not supported yet)" :
                                        "not a game eidos knows");
    printf("dump      %s\n", info.verified
           ? "verified (the dump aletheia is built against)"
           : "not the verified dump");

    if (argc - a == 1) {
        return info.verified ? 0 : 1;
    }
    if (!any && info.game != BW_GAME_BLACK) {
        fprintf(stderr, "eidos_extract: refusing a ROM that is not Pokemon Black "
                        "(pass --any to extract it anyway)\n");
        return 1;
    }
    if (!info.verified) {
        fprintf(stderr, "eidos_extract: warning: this is not the dump aletheia is "
                        "verified against; extracting anyway\n");
    }

    memset(&opt, 0, sizeof(opt));
    opt.require_gamecode = any ? NULL : "IRBO";
    if (bw_rom_extract(argv[a], argv[a + 1], &opt, &res, progress,
                       (void *)"extracting", err, sizeof(err)) != 0) {
        fprintf(stderr, "\neidos_extract: %s\n", err);
        return 1;
    }
    printf("extracted %u files (%u NitroFS, %u overlays, %u orphans), "
           "%llu bytes; arm9_dec.bin %u bytes\n",
           (unsigned)res.regions, (unsigned)res.fs_files, (unsigned)res.overlays,
           (unsigned)res.orphans, (unsigned long long)res.bytes,
           (unsigned)res.arm9_dec_size);
    return 0;
}
