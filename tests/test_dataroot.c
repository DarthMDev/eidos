/*
 * tests/test_dataroot.c -- install/setup.c: where the data root is, whether it
 * is playable, the config file, the headless install, reset and move.
 *
 *     test_dataroot <synth_dir> <work_dir>
 *
 * Everything happens under <work_dir>, which is used as the eidos home
 * (bw_setup_set_home), so the player's real home folder is never read or
 * written.  The extraction checks use hand-made trees (a manifest and a
 * data/a folder are all the check looks at) and the synthetic cartridge
 * of tools/synth_rom.py (Pokemon Black's game code, not the known dump:
 * BW_VERDICT_MODIFIED), so no game data is needed.
 *
 *   1. locations: the home, the default extraction, the config path,
 *      portable mode's precedence below the home override;
 *   2. the config file round trip, with a path outside ASCII;
 *   3. bw_setup_split on both spellings of a data root;
 *   4. bw_setup_check in every state, trusted and not;
 *   5. the option parser;
 *   6. the resolution order: --data-root, BW_DATA_ROOT, config, default;
 *   7. the headless install: the synthetic cartridge refused as not the
 *      known dump with nothing written, installed with allow_unverified,
 *      cancelled from the progress callback with nothing left behind;
 *   8. free space;
 *   9. reset (and its refusal of a folder eidos did not write) and move;
 *  10. bw_setup_prepare with --no-window on missing data, and --rom with
 *      an unsupported image.
 */

#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L   /* setenv */
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port_test.h"
#include "setup.h"

static char g_work[512];
static char g_synth[512];

static void set_env(const char *name, const char *value)
{
#ifdef _WIN32
    _putenv_s(name, value);
#else
    if (value[0] == '\0') {
        unsetenv(name);
    } else {
        setenv(name, value, 1);
    }
#endif
}

static int write_text(const char *path, const char *text)
{
    FILE *fp = bw_fopen_utf8(path, "wb");

    if (fp == NULL) {
        return -1;
    }
    fputs(text, fp);
    fclose(fp);
    return 0;
}

static void path_of(char *out, size_t n, const char *a, const char *b)
{
    snprintf(out, n, "%s/%s", a, b);
}

/* A fake extraction: a manifest of `version` and `sha`, and, if asked,
 * data/a and arm9_dec.bin. */
static int fake_extract(const char *dir, int version, const char *sha, int files)
{
    char p[1024];
    char text[512];

    if (bw_mkdirs_utf8(dir) != 0) {
        return -1;
    }
    snprintf(text, sizeof(text), "{\n \"tool\": \"eidos install\",\n \"version\": %d,\n"
             " \"rom_sha1\": \"%s\",\n \"title\": \"POKEMON B\"\n}\n", version, sha);
    path_of(p, sizeof(p), dir, "eidos_manifest.json");
    if (write_text(p, text) != 0) {
        return -1;
    }
    if (files) {
        path_of(p, sizeof(p), dir, "data/a");
        if (bw_mkdirs_utf8(p) != 0) {
            return -1;
        }
        path_of(p, sizeof(p), dir, "arm9_dec.bin");
        if (write_text(p, "x") != 0) {
            return -1;
        }
    }
    return 0;
}

static int ends_with(const char *s, const char *tail)
{
    size_t a = strlen(s), b = strlen(tail);
    return a >= b && strcmp(s + a - b, tail) == 0;
}

/* ------------------------------------------------------------------ */

static int test_locations(void)
{
    char home[1024], p[1024];

    bw_setup_set_home(g_work);
    BW_CHECK_EQ("home is the override", bw_setup_home(home, sizeof(home)), 0);
    BW_CHECK_EQ("home text", strcmp(home, g_work), 0);
    BW_CHECK_EQ("default extract", bw_setup_default_extract(p, sizeof(p)), 0);
    BW_CHECK_EQ("default extract is <home>/extract", ends_with(p, BW_SETUP_SEP "extract"), 1);
    BW_CHECK_EQ("config path", bw_setup_config_path(p, sizeof(p)), 0);
    BW_CHECK_EQ("config is <home>/config.txt", ends_with(p, BW_SETUP_SEP "config.txt"), 1);
    /* The platform default exists on every desktop this runs on. */
    BW_CHECK_EQ("the platform has a default home", bw_os_app_home(p, sizeof(p)), 0);
    BW_CHECK_EQ("it ends in eidos", ends_with(p, "eidos"), 1);
    BW_CHECK_EQ("the executable's folder is known", bw_os_exe_dir(p, sizeof(p)), 0);
    return 0;
}

static int test_config(void)
{
    BwSetupConfig c, back;

    memset(&c, 0, sizeof(c));
    BW_CHECK_EQ("no config yet", bw_setup_config_load(&back), 1);
    strcpy(c.data_root, "/somewhere/extract \xc3\xa9\xe2\x82\xac");
    strcpy(c.rom, "/roms/Pok\xc3\xa9mon Black.nds");
    BW_CHECK_EQ("save", bw_setup_config_save(&c), 0);
    BW_CHECK_EQ("load", bw_setup_config_load(&back), 0);
    BW_CHECK_EQ("data_root round trip", strcmp(back.data_root, c.data_root), 0);
    BW_CHECK_EQ("rom round trip", strcmp(back.rom, c.rom), 0);
    c.rom[0] = '\0';
    BW_CHECK_EQ("save again", bw_setup_config_save(&c), 0);
    BW_CHECK_EQ("load again", bw_setup_config_load(&back), 0);
    BW_CHECK_EQ("rom cleared", back.rom[0], 0);
    memset(&c, 0, sizeof(c));
    BW_CHECK_EQ("clear", bw_setup_config_save(&c), 0);
    return 0;
}

static int test_split_and_check(void)
{
    char ex[1100], data[1100], p[1100], why[512];
    char a[512], b[1100];

    /* An extraction folder and its data folder name the same pair. */
    path_of(a, sizeof(a), g_work, "x1");
    BW_REQUIRE("fake extraction", fake_extract(a, 2, BW_BLACK_SHA1, 1) == 0);
    bw_setup_split(a, ex, sizeof(ex), data, sizeof(data));
    BW_CHECK_EQ("split: extraction folder", strcmp(ex, a), 0);
    path_of(p, sizeof(p), a, "data");
    bw_setup_split(p, ex, sizeof(ex), b, sizeof(b));
    BW_CHECK_EQ("split: data folder gives its parent", strcmp(ex, a), 0);
    BW_CHECK_EQ("split: and itself", strcmp(b, p), 0);
    snprintf(p, sizeof(p), "%s/", a);
    bw_setup_split(p, ex, sizeof(ex), data, sizeof(data));
    BW_CHECK_EQ("split: a trailing separator is dropped", strcmp(ex, a), 0);

    BW_CHECK_EQ("check: complete", bw_setup_check(a, 0, why, sizeof(why)), BW_ROOT_OK);
    path_of(p, sizeof(p), a, "data");
    BW_CHECK_EQ("check: by its data folder", bw_setup_check(p, 0, why, sizeof(why)), BW_ROOT_OK);

    path_of(a, sizeof(a), g_work, "x_missing");
    BW_CHECK_EQ("check: missing", bw_setup_check(a, 0, why, sizeof(why)), BW_ROOT_MISSING);
    BW_CHECK_EQ("check: missing, trusted", bw_setup_check(a, 1, why, sizeof(why)), BW_ROOT_MISSING);
    BW_REQUIRE("empty folder", bw_mkdirs_utf8(a) == 0);
    BW_CHECK_EQ("check: an empty folder is missing", bw_setup_check(a, 0, why, sizeof(why)), BW_ROOT_MISSING);

    path_of(a, sizeof(a), g_work, "x_junk");
    BW_REQUIRE("junk folder", bw_mkdirs_utf8(a) == 0);
    path_of(p, sizeof(p), a, "notes.txt");
    write_text(p, "mine");
    BW_CHECK_EQ("check: someone else's folder", bw_setup_check(a, 0, why, sizeof(why)),
                BW_ROOT_INCOMPLETE);

    path_of(a, sizeof(a), g_work, "x_old");
    BW_REQUIRE("old", fake_extract(a, 1, BW_BLACK_SHA1, 1) == 0);
    BW_CHECK_EQ("check: older installer", bw_setup_check(a, 0, why, sizeof(why)), BW_ROOT_OLD);
    BW_CHECK_EQ("check: older installer, named explicitly, still runs",
                bw_setup_check(a, 1, why, sizeof(why)), BW_ROOT_OK);

    path_of(a, sizeof(a), g_work, "x_hack");
    BW_REQUIRE("hack", fake_extract(a, 2, "0123456789012345678901234567890123456789", 1) == 0);
    BW_CHECK_EQ("check: another image", bw_setup_check(a, 0, why, sizeof(why)), BW_ROOT_OTHER_IMAGE);
    BW_CHECK_EQ("the message names its hash", strstr(why, "0123456789") != NULL, 1);
    BW_CHECK_EQ("check: another image, named explicitly",
                bw_setup_check(a, 1, why, sizeof(why)), BW_ROOT_OK);

    path_of(a, sizeof(a), g_work, "x_damaged");
    BW_REQUIRE("damaged", fake_extract(a, 2, BW_BLACK_SHA1, 0) == 0);
    BW_CHECK_EQ("check: files missing", bw_setup_check(a, 0, why, sizeof(why)), BW_ROOT_DAMAGED);

    /* aletheia's extract/ has no eidos manifest: fine when named. */
    path_of(a, sizeof(a), g_work, "x_aletheia/data/a");
    BW_REQUIRE("aletheia-like", bw_mkdirs_utf8(a) == 0);
    path_of(a, sizeof(a), g_work, "x_aletheia/data");
    BW_CHECK_EQ("check: aletheia's tree, named", bw_setup_check(a, 1, why, sizeof(why)), BW_ROOT_OK);
    BW_CHECK_EQ("check: aletheia's tree, not named", bw_setup_check(a, 0, why, sizeof(why)),
                BW_ROOT_INCOMPLETE);
    return 0;
}

static int test_args(void)
{
    char *argv[] = { "prog", "--data-root", "D", "--rom", "R", "--no-window",
                     "--reset-data", "--move-data", "M", "--other", "--rom" };
    int argc = (int)(sizeof(argv) / sizeof(argv[0]));
    BwSetupRequest r;
    int i;

    memset(&r, 0, sizeof(r));
    i = 1;
    BW_CHECK_EQ("--data-root", bw_setup_parse_arg(&r, argc, argv, &i), 1);
    BW_CHECK_EQ("consumed its value", i, 2);
    i = 3;
    BW_CHECK_EQ("--rom", bw_setup_parse_arg(&r, argc, argv, &i), 1);
    i = 5;
    BW_CHECK_EQ("--no-window", bw_setup_parse_arg(&r, argc, argv, &i), 1);
    i = 6;
    BW_CHECK_EQ("--reset-data", bw_setup_parse_arg(&r, argc, argv, &i), 1);
    i = 7;
    BW_CHECK_EQ("--move-data", bw_setup_parse_arg(&r, argc, argv, &i), 1);
    i = 9;
    BW_CHECK_EQ("not ours", bw_setup_parse_arg(&r, argc, argv, &i), 0);
    i = 10;
    BW_CHECK_EQ("missing value", bw_setup_parse_arg(&r, argc, argv, &i), -1);
    BW_CHECK_EQ("values", strcmp(r.data_root, "D") == 0 && strcmp(r.rom, "R") == 0
                && strcmp(r.move_to, "M") == 0 && r.no_window && r.reset, 1);
    BW_CHECK_EQ("usage text", strstr(bw_setup_usage(), "--rom") != NULL, 1);
    return 0;
}

static int test_resolve(void)
{
    BwSetupRequest r;
    BwSetupResolved out;
    char a[1024], env[1024], p[1024];

    memset(&r, 0, sizeof(r));
    /* Nothing anywhere: the default, missing. */
    BW_CHECK_EQ("resolve", bw_setup_resolve(&r, &out), 0);
    BW_CHECK_EQ("source default", out.source, BW_SOURCE_DEFAULT);
    BW_CHECK_EQ("missing", out.state, BW_ROOT_MISSING);
    BW_CHECK_EQ("at <home>/extract", ends_with(out.extract_dir, "extract"), 1);

    /* The config file. */
    path_of(a, sizeof(a), g_work, "x1");
    BW_REQUIRE("remember", bw_setup_remember(a, NULL) == 0);
    bw_setup_resolve(&r, &out);
    BW_CHECK_EQ("source config", out.source, BW_SOURCE_CONFIG);
    BW_CHECK_EQ("config: ok", out.state, BW_ROOT_OK);

    /* The environment outranks it. */
    path_of(env, sizeof(env), g_work, "x_aletheia/data");
    set_env("BW_DATA_ROOT", env);
    bw_setup_resolve(&r, &out);
    BW_CHECK_EQ("source env", out.source, BW_SOURCE_ENV);
    BW_CHECK_EQ("env: ok (trusted)", out.state, BW_ROOT_OK);
    BW_CHECK_EQ("env: the data folder as given", strcmp(out.data_dir, env), 0);

    /* The flag outranks both. */
    path_of(p, sizeof(p), g_work, "x_old");
    r.data_root = p;
    bw_setup_resolve(&r, &out);
    BW_CHECK_EQ("source flag", out.source, BW_SOURCE_FLAG);
    BW_CHECK_EQ("flag: ok (trusted)", out.state, BW_ROOT_OK);
    set_env("BW_DATA_ROOT", "");

    /* An untrusted old extraction in the config. */
    BW_REQUIRE("remember old", bw_setup_remember(p, "/no/such/rom.nds") == 0);
    r.data_root = NULL;
    bw_setup_resolve(&r, &out);
    BW_CHECK_EQ("config: old", out.state, BW_ROOT_OLD);
    BW_CHECK_EQ("the remembered rom is reported", ends_with(out.rom, "rom.nds"), 1);
    {
        BwSetupConfig c;
        memset(&c, 0, sizeof(c));
        bw_setup_config_save(&c);
    }
    return 0;
}

static int cancel_at_half(void *ctx, u32 done, u32 total, const char *what)
{
    int *calls = (int *)ctx;
    (*calls)++;
    return strcmp(what, "hashing") != 0 && done * 2 >= total;
}

static int count_calls(void *ctx, u32 done, u32 total, const char *what)
{
    (void)done;
    (void)total;
    (void)what;
    (*(int *)ctx)++;
    return 0;
}

static int test_install(void)
{
    char rom[600], out[600], p[1100], msg[1024];
    BwSetupOutcome oc;
    BwSetupResolved res;
    int calls = 0;
    int rc;

    path_of(rom, sizeof(rom), g_synth, "synth.nds");
    path_of(out, sizeof(out), g_work, "inst \xc3\xa9");
    bw_remove_tree_utf8(out);

    /* Not the known dump: refused, nothing written. */
    rc = bw_setup_install(rom, out, 0, 0, NULL, NULL, &oc, msg, sizeof(msg));
    BW_CHECK_EQ("the synthetic image is refused", rc, -1);
    BW_CHECK_EQ("at the verdict", oc.stage, BW_STAGE_VERDICT);
    BW_CHECK_EQ("as modified", oc.verdict, BW_VERDICT_MODIFIED);
    BW_CHECK_EQ("nothing written", bw_path_kind_utf8(out), BW_PATH_NONE);

    /* A file that is not a ROM. */
    path_of(p, sizeof(p), g_work, "rom.zip");
    write_text(p, "PK\x03\x04 not really a zip, but it starts like one........"
                  "................................................................"
                  "................................................................"
                  "................................................................"
                  "................................................................");
    rc = bw_setup_install(p, out, 0, 0, NULL, NULL, &oc, msg, sizeof(msg));
    BW_CHECK_EQ("an archive is refused", rc, -1);
    BW_CHECK_EQ("at identify", oc.stage, BW_STAGE_IDENTIFY);
    BW_CHECK_EQ("the message says to unpack it", strstr(msg, "unpack") != NULL, 1);

    /* Cancelled half way through writing: nothing left. */
    rc = bw_setup_install(rom, out, 1, 0, cancel_at_half, &calls, &oc, msg, sizeof(msg));
    BW_CHECK_EQ("cancelled", rc, BW_CANCELLED);
    BW_CHECK_EQ("while extracting", oc.stage, BW_STAGE_EXTRACT);
    BW_CHECK_EQ("no extraction", bw_path_kind_utf8(out), BW_PATH_NONE);
    snprintf(p, sizeof(p), "%s.partial", out);
    BW_CHECK_EQ("no staging folder", bw_path_kind_utf8(p), BW_PATH_NONE);

    /* Installed (the test override), and remembered. */
    calls = 0;
    rc = bw_setup_install(rom, out, 1, 1, count_calls, &calls, &oc, msg, sizeof(msg));
    if (rc != 0) {
        printf("    install: %s\n", msg);
    }
    BW_CHECK_EQ("installed", rc, 0);
    BW_CHECK_EQ("done", oc.stage, BW_STAGE_DONE);
    BW_CHECK_EQ("progress was reported", calls > 2, 1);
    path_of(p, sizeof(p), out, "eidos_manifest.json");
    BW_CHECK_EQ("manifest written", bw_path_kind_utf8(p), BW_PATH_FILE);
    bw_setup_resolve(NULL, &res);
    BW_CHECK_EQ("the config names it", res.source, BW_SOURCE_CONFIG);
    BW_CHECK_EQ("the remembered ROM", ends_with(res.rom, "synth.nds"), 1);
    BW_CHECK_EQ("but an unverified image is not playable unless named",
                res.state, BW_ROOT_OTHER_IMAGE);
    BW_CHECK_EQ("named, it is", bw_setup_check(out, 1, msg, sizeof(msg)), BW_ROOT_OK);

    /* Again: already there. */
    rc = bw_setup_install(rom, out, 1, 0, NULL, NULL, &oc, msg, sizeof(msg));
    BW_CHECK_EQ("again", rc, 0);
    BW_CHECK_EQ("already", oc.already, 1);
    return 0;
}

static int test_space(void)
{
    char p[1024];
    u64 bytes = 0;

    path_of(p, sizeof(p), g_work, "not/yet/there");
    BW_CHECK_EQ("free space of a folder to be", bw_free_space_utf8(p, &bytes), 0);
    BW_CHECK_EQ("is some", bytes > 0, 1);
    BW_CHECK_EQ("free space of a bare name", bw_free_space_utf8("nothing_here", &bytes), 0);
    BW_CHECK_EQ("the need is above the ROM size",
                bw_setup_space_needed(1000u) > 1000u, 1);
    return 0;
}

static int test_reset_move(void)
{
    char from[1024], to[1024], p[1024], msg[1024];
    BwSetupResolved res;

    path_of(from, sizeof(from), g_work, "inst \xc3\xa9");
    path_of(to, sizeof(to), g_work, "moved/here");
    bw_remove_tree_utf8(to);

    BW_CHECK_EQ("move", bw_setup_move(from, to, msg, sizeof(msg)), 0);
    BW_CHECK_EQ("the old place is gone", bw_path_kind_utf8(from), BW_PATH_NONE);
    path_of(p, sizeof(p), to, "eidos_manifest.json");
    BW_CHECK_EQ("the new place has it", bw_path_kind_utf8(p), BW_PATH_FILE);
    bw_setup_resolve(NULL, &res);
    BW_CHECK_EQ("the config follows", ends_with(res.extract_dir, "here"), 1);

    BW_CHECK_EQ("moving nothing is refused", bw_setup_move(from, to, msg, sizeof(msg)), -1);
    path_of(p, sizeof(p), g_work, "x_junk");
    BW_CHECK_EQ("moving onto a full folder is refused", bw_setup_move(to, p, msg, sizeof(msg)), -1);

    BW_CHECK_EQ("reset someone else's folder is refused",
                bw_setup_reset(p, msg, sizeof(msg)), -1);
    BW_CHECK_EQ("and it is still there", bw_path_kind_utf8(p), BW_PATH_DIR);
    BW_CHECK_EQ("reset", bw_setup_reset(to, msg, sizeof(msg)), 0);
    BW_CHECK_EQ("deleted", bw_path_kind_utf8(to), BW_PATH_NONE);
    bw_setup_resolve(NULL, &res);
    BW_CHECK_EQ("the config is cleared", res.source, BW_SOURCE_DEFAULT);
    BW_CHECK_EQ("reset of nothing is fine", bw_setup_reset(to, msg, sizeof(msg)), 0);
    return 0;
}

static int test_prepare(void)
{
    BwSetupRequest r;
    BwSetupResolved out;
    char rom[1024];

    memset(&r, 0, sizeof(r));
    r.no_window = 1;
    printf("  (the next lines are bw_setup_prepare's own messages)\n");
    BW_CHECK_EQ("no data, no window: failed", bw_setup_prepare(&r, &out), BW_PREPARE_FAILED);
    r.no_window = 0;
    BW_CHECK_EQ("no data, window: the wizard", bw_setup_prepare(&r, &out), BW_PREPARE_WIZARD);
    path_of(rom, sizeof(rom), g_synth, "synth.nds");
    r.rom = rom;
    BW_CHECK_EQ("--rom with an unsupported image: failed",
                bw_setup_prepare(&r, &out), BW_PREPARE_FAILED);
    BW_CHECK_EQ("nothing written", bw_path_kind_utf8(out.extract_dir), BW_PATH_NONE);
    r.rom = NULL;
    r.reset = 1;
    BW_CHECK_EQ("--reset-data on nothing", bw_setup_prepare(&r, &out), BW_PREPARE_EXIT_OK);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        printf("usage: test_dataroot <synth_dir> <work_dir>\n");
        return 2;
    }
    snprintf(g_synth, sizeof(g_synth), "%s", argv[1]);
    snprintf(g_work, sizeof(g_work), "%s", argv[2]);
    bw_remove_tree_utf8(g_work);
    if (bw_mkdirs_utf8(g_work) != 0) {
        printf("cannot create %s\n", g_work);
        return 1;
    }
    set_env("BW_DATA_ROOT", "");

    if (test_locations() || test_config() || test_split_and_check() || test_args()
        || test_resolve() || test_install() || test_space() || test_reset_move()
        || test_prepare()) {
        BW_REPORT("test_dataroot");
    }
    if (bw_failures == 0) {
        bw_remove_tree_utf8(g_work);
    }
    BW_REPORT("test_dataroot");
}
