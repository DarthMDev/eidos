/*
 * tests/test_mod_endtoend.c -- the mod layer end to end: a real library, a
 * real manifest, the port's own loader and a real event.
 *
 * The port's own tests drive the layers one at a time, so a defect in the
 * way an event reaches a mod at run time would not show: the manifest, the
 * library, the host table, the registration, the dispatch and the mod's own
 * data are never on one path.  This test puts them on one path:
 *
 *   - a mod library, built from this file with BW_MOD_TEST_LIBRARY defined,
 *     that binds a handler to an event and records what it was handed;
 *   - a manifest that names the library, states the port's api and gives
 *     the mod its own state namespace;
 *   - the port's own loader (sdk/mod_loader.c), which reads the manifest,
 *     checks the versions, opens the library, calls eidos_mod_start with
 *     the host table, registers the handlers the mod asked for and hands
 *     the mod the services table;
 *   - the port's own event dispatch (sdk/mod_event.c), fired the way a slot
 *     fires it: bw_mod_event_enter is the call the generated slot makes
 *     around the game's function (tools/ovlink.py).  The rows are the port's
 *     own: the two of the area tables (sdk/mod_event_session.c and its
 *     siblings) whose builders need nothing but the call, and one row the
 *     test adds itself with the port's own call (bw_mod_event_bind), the
 *     way the area files add theirs.
 *
 * What the test checks:
 *
 *   - the handler runs when the event fires, with the payload the port
 *     built and with the mod's own state (the pointer the start returned);
 *   - a handler that refuses stops the action: the port's own refusal
 *     function produces the refused outcome, and the slot does not call the
 *     game function;
 *   - a mod built against another interface version is refused, reported
 *     and never loaded;
 *   - a handler for an event the port does not have is refused, reported
 *     and never registered;
 *   - the mod's own saved data is the mod's own: the namespace is in force
 *     while its handler runs, so the file it writes is its own and not
 *     another mod's.
 *
 * The frozen surface does not change here: the library is compiled against
 * sdk/mod_api.h and sdk/mod_state.h alone, and it links against nothing.
 *
 * Nothing here needs game data: the rows are the port's own, and the two
 * events it fires on them (flag.set and menu.open) are the ones whose
 * builders need nothing but the call.  The mod's tree is a directory in the
 * test's own working directory, the library is a copy of the one the build
 * makes, and the library writes what it was handed into a log file, because
 * it is unloaded before the test can ask it anything.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mod_api.h"
#include "mod_state.h"
#include "types.h"

/* ================================================================== */
/* The test mod library                                                */
/* ================================================================== */

#ifdef BW_MOD_TEST_LIBRARY

#include <stdarg.h>

#if defined(_WIN32)
#include <windows.h>
#define BW_MOD_EXPORT __declspec(dllexport)
#else
#define BW_MOD_EXPORT __attribute__((visibility("default")))
#endif

/* The log the library writes what it was handed into; the test reads it. */
#define T_LOG "mod_endtoend_log.txt"

/*
 * An event the port does not have.  The ids of sdk/mod_api.h go up to
 * BW_EVENT_DEX_REGISTER, and the port's own table holds no other: a mod
 * that binds a handler to one of these is refused and reported.
 */
#define T_UNKNOWN_KIND 250u

/*
 * The mod's own state: the pointer the start entry point returns and every
 * handler of the mod receives.
 */
typedef struct TestModState {
    u32  api;                      /* the host table's interface version */
    u32  calls;                    /* how many times the handlers ran */
    const BwModServices *services;  /* the port's own services table */
} TestModState;

/* Append one line to the log. */
static void record(const char *fmt, ...)
{
    va_list ap;
    FILE *fp = fopen(T_LOG, "ab");

    if (fp == NULL) {
        return;
    }
    va_start(ap, fmt);
    vfprintf(fp, fmt, ap);
    va_end(ap);
    fputc('\n', fp);
    fclose(fp);
}

/*
 * The handler of flag.set: it records the event it was handed, the payload
 * the port built and the state it was given, and it keeps its own data in
 * its own file (docs/mod-events.md, section 2).
 */
static int test_mod_observer(void *mod_state, BwEvent *event)
{
    TestModState *s = (TestModState *)mod_state;
    const BwFlagSet *p = (const BwFlagSet *)event;
    char dir[512];
    u32 stored = 0u;
    u32 loaded = 0u;
    s32 got;

    record("observer_kind=%u", event != NULL ? (unsigned int)event->kind : 0u);
    record("observer_size=%u", event != NULL ? (unsigned int)event->size : 0u);
    record("observer_flag=0x%04X",
           event != NULL ? (unsigned int)p->flag : 0u);
    record("observer_value=%u", event != NULL ? (unsigned int)p->value : 0u);
    record("observer_state=%p", mod_state);
    if (s != NULL) {
        s->calls++;
    }
    record("observer_calls=%u", s != NULL ? (unsigned int)s->calls : 0u);

    /*
     * The mod's own data: the namespace is in force while the handler runs
     * (docs/mod-state.md, section 2), so the folder the port names here is
     * the mod's own and the file it writes is the mod's own.
     */
    if (s != NULL && s->services != NULL) {
        if (s->services->state_dir != NULL &&
            s->services->state_dir(dir, (u32)sizeof(dir)) == 0) {
            record("observer_dir=%s", dir);
        } else {
            record("observer_dir=none");
        }
        stored = s->calls;
        if (s->services->state_store != NULL &&
            s->services->state_store(&stored, (u32)sizeof(stored)) == 0) {
            record("observer_store=ok");
        } else {
            record("observer_store=failed");
        }
        got = -1;
        if (s->services->state_load != NULL) {
            got = s->services->state_load(&loaded, (u32)sizeof(loaded));
        }
        record("observer_load=%d", (int)got);
        record("observer_loaded=%u", (unsigned int)loaded);
    }
    return BW_EVENT_CONTINUE;
}

/*
 * The handler of menu.open: it refuses, so the port produces the event's
 * refused outcome (docs/mod-events.md, section 3).
 */
static int test_mod_refuser(void *mod_state, BwEvent *event)
{
    TestModState *s = (TestModState *)mod_state;

    record("refuser_kind=%u", event != NULL ? (unsigned int)event->kind : 0u);
    record("refuser_state=%p", mod_state);
    if (s != NULL) {
        s->calls++;
    }
    record("refuser_calls=%u", s != NULL ? (unsigned int)s->calls : 0u);
    return BW_EVENT_REFUSE;
}

/*
 * The handler of an event the port does not have: the port must refuse it,
 * so it never runs.
 */
static int test_mod_unknown(void *mod_state, BwEvent *event)
{
    (void)mod_state;
    (void)event;
    record("unknown_ran=1");
    return BW_EVENT_CONTINUE;
}

/*
 * The library's entry point (docs/mod-abi.md, section 1): it records what
 * the host table holds, binds its handlers and gives the port the mod's own
 * state.
 */
int BW_MOD_EXPORT eidos_mod_start(const BwModHost *host, void **mod_state)
{
    TestModState *s;

    if (host == NULL || mod_state == NULL) {
        return -1;
    }
    s = (TestModState *)calloc(1, sizeof(*s));
    if (s == NULL) {
        return -1;
    }
    s->services = host->services;
    record("start_api=%u", (unsigned int)host->api);
    record("start_state_table=%d", host->state != NULL ? 1 : 0);
    record("start_services=%d",
           (host->services != NULL && host->services->state_store != NULL)
               ? 1 : 0);
    if (host->on(BW_EVENT_FLAG_SET, test_mod_observer) != 0) {
        record("bind_flag=failed");
        free(s);
        return -1;
    }
    if (host->on(BW_EVENT_MENU_OPEN, test_mod_refuser) != 0) {
        record("bind_menu=failed");
        free(s);
        return -1;
    }
    /* The event the port does not have: the port refuses it and reports. */
    record("bind_unknown=%d", host->on(T_UNKNOWN_KIND, test_mod_unknown));
    *mod_state = s;
    record("start_state=%p", (void *)s);
    return 0;
}

/* The library's second entry point: the mod's own way down. */
void BW_MOD_EXPORT eidos_mod_stop(void *mod_state)
{
    TestModState *s = (TestModState *)mod_state;

    record("stop_calls=%u", s != NULL ? (unsigned int)s->calls : 0u);
    free(s);
}

/*
 * The library is unloaded: the port frees it, and the record says so.  The
 * Windows loader calls the entry point below when the module goes; the
 * other hosts run the library's destructors.
 */
#if defined(_WIN32)
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)instance;
    (void)reserved;
    if (reason == DLL_PROCESS_DETACH) {
        HANDLE f = CreateFileA(T_LOG, FILE_APPEND_DATA,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                               OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (f != INVALID_HANDLE_VALUE) {
            const char line[] = "unload\n";
            DWORD written = 0;

            WriteFile(f, line, (DWORD)(sizeof(line) - 1), &written, NULL);
            CloseHandle(f);
        }
    }
    return TRUE;
}
#else
__attribute__((destructor)) static void test_mod_unloaded(void)
{
    record("unload");
}
#endif

#else /* the test */

/* ================================================================== */
/* The test                                                            */
/* ================================================================== */

#include "mod_event.h"
#include "mod_loader.h"
#include "save.h"
#include "vfs.h"

#include "port_test.h"

#ifdef _WIN32
#include <direct.h>
#define BW_MKDIR(p) _mkdir(p)
#define BW_RMDIR(p) _rmdir(p)
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#define BW_MKDIR(p) mkdir((p), 0777)
#define BW_RMDIR(p) rmdir(p)
#endif

/* ------------------------------------------------------------------ */
/* The port's stderr, taken over for the moment                        */
/* ------------------------------------------------------------------ */

/*
 * The port reports a refused mod and a handler it did not bind on its
 * stderr, so the test takes the stream over, lets the port write into a
 * file, and reads the file back.  The stream is put back where it was, so
 * the report of the test itself (stdout) and every later message are as
 * they were.
 */

#ifdef _WIN32
#include <io.h>
#define T_DUP(fd)    _dup(fd)
#define T_DUP2(a, b) _dup2(a, b)
#define T_CLOSE(fd)  _close(fd)
#define T_FILENO(f)  _fileno(f)
#else
#include <unistd.h>
#define T_DUP(fd)    dup(fd)
#define T_DUP2(a, b) dup2(a, b)
#define T_CLOSE(fd)  close(fd)
#define T_FILENO(f)  fileno(f)
#endif

/* The file the reports are taken into, in the test's own directory. */
#define T_ERR_FILE "mod_endtoend_stderr.txt"

/* The stderr file descriptor. */
#define T_STDERR 2

static int g_err_saved = -1;
static FILE *g_err_file;
static char g_err_text[1024];

/* Take the port's stderr over, so what is written into it can be read. */
static void err_begin(void)
{
    fflush(stderr);
    g_err_file = fopen(T_ERR_FILE, "w+b");
    if (g_err_file == NULL) {
        return;
    }
    g_err_saved = T_DUP(T_STDERR);
    if (g_err_saved >= 0) {
        T_DUP2(T_FILENO(g_err_file), T_STDERR);
    }
}

/* Give the port's stderr back and keep what was written into it. */
static const char *err_end(void)
{
    size_t n = 0;

    fflush(stderr);
    if (g_err_saved >= 0) {
        T_DUP2(g_err_saved, T_STDERR);
        T_CLOSE(g_err_saved);
        g_err_saved = -1;
    }
    g_err_text[0] = '\0';
    if (g_err_file != NULL) {
        fseek(g_err_file, 0, SEEK_SET);
        n = fread(g_err_text, 1, sizeof g_err_text - 1, g_err_file);
        fclose(g_err_file);
        g_err_file = NULL;
    }
    g_err_text[n] = '\0';
    return g_err_text;
}

/* Whether the report the port wrote says `what`. */
static int reported(const char *what)
{
    return strstr(g_err_text, what) != NULL;
}

/* ------------------------------------------------------------------ */
/* The mod tree                                                        */
/* ------------------------------------------------------------------ */

/* The tree the test builds: one mod with the port's api, one with another. */
#define T_ROOT "mod_endtoend_tmp"
#define T_BAD  T_ROOT "/badapi"
#define T_GOOD T_ROOT "/good"

/* The log the library writes into. */
#define T_LOG "mod_endtoend_log.txt"

/* The state namespace the good mod's manifest gives it. */
#define T_NS "e2emod"

/* The namespace the refused mod's manifest gives it. */
#define T_BAD_NS "wrongapi"

/* An event the port does not have (the library binds a handler to it). */
#define T_UNKNOWN_KIND 250u

/* The flag the call carries when the port's flag.set row fires. */
#define T_FLAG 0x1234u

/*
 * The pair the synthetic row is bound to: a region and an address the game
 * does not use, the pair the port's own tests use for their fake rows.
 */
#define T_REGION 0x0042u
#define T_ADDR   0x0210ABCDu

/* ------------------------------------------------------------------ */
/* The log                                                             */
/* ------------------------------------------------------------------ */

/* The whole log, in a buffer the test owns. */
static char g_log[4096];

/* Read the log the library wrote. */
static void log_read(void)
{
    FILE *fp = fopen(T_LOG, "rb");
    size_t n;

    g_log[0] = '\0';
    if (fp == NULL) {
        return;
    }
    n = fread(g_log, 1, sizeof(g_log) - 1, fp);
    g_log[n] = '\0';
    fclose(fp);
}

/* Whether the log holds the text. */
static int log_has(const char *text)
{
    return strstr(g_log, text) != NULL;
}

/* How many times the log holds the text. */
static int log_count(const char *text)
{
    const char *p = g_log;
    int n = 0;

    while ((p = strstr(p, text)) != NULL) {
        n++;
        p++;
    }
    return n;
}

/* The value of a line: the text after the key, up to the end of the line. */
static int log_value(const char *key, char *out, size_t size)
{
    const char *p;
    size_t n = 0;

    if (out == NULL || size == 0) {
        return -1;
    }
    out[0] = '\0';
    p = strstr(g_log, key);
    if (p == NULL) {
        return -1;
    }
    p += strlen(key);
    while (*p != '\0' && *p != '\n' && *p != '\r' && n + 1 < size) {
        out[n++] = *p++;
    }
    out[n] = '\0';
    return 0;
}

/* ------------------------------------------------------------------ */
/* The tree, built                                                     */
/* ------------------------------------------------------------------ */

/* The last component of a path. */
static const char *base_name(const char *path)
{
    const char *a = strrchr(path, '/');
    const char *b = strrchr(path, '\\');

    if (b != NULL && (a == NULL || b > a)) {
        a = b;
    }
    return a != NULL ? a + 1 : path;
}

/* Copy a file: the test's tree holds its own copy of the library, because
 * the loader opens the library inside the mod's own folder. */
static int copy_file(const char *from, const char *to)
{
    FILE *in;
    FILE *out;
    char buf[4096];
    size_t n;

    in = fopen(from, "rb");
    if (in == NULL) {
        return -1;
    }
    out = fopen(to, "wb");
    if (out == NULL) {
        fclose(in);
        return -1;
    }
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            fclose(in);
            fclose(out);
            return -1;
        }
    }
    fclose(in);
    fclose(out);
    return 0;
}

/* Write a mod's manifest. */
static int write_manifest(const char *dir, const char *text)
{
    char path[512];
    FILE *fp;
    int n = snprintf(path, sizeof path, "%s/eidos_mod.txt", dir);

    if (n < 0 || (size_t)n >= sizeof path) {
        return -1;
    }
    fp = fopen(path, "wb");
    if (fp == NULL) {
        return -1;
    }
    fputs(text, fp);
    fclose(fp);
    return 0;
}

/* Put the library into a mod's own folder. */
static int put_library(const char *dir, const char *library)
{
    char path[512];
    int n = snprintf(path, sizeof path, "%s/%s", dir, base_name(library));

    if (n < 0 || (size_t)n >= sizeof path) {
        return -1;
    }
    return copy_file(library, path);
}

/* ------------------------------------------------------------------ */
/* The port's own rows                                                 */
/* ------------------------------------------------------------------ */

/*
 * The area tables of the port's own rows (sdk/mod_event_session.c and its
 * siblings).  The dispatch core binds them at start-up and at every reset
 * (bw_mod_event_bind_all); the test reads them to find the row it fires,
 * so the row the test drives is the port's own row and not a copy of it.
 */
extern const BwEventBinding *bw_event_bindings_session(u32 *count);
extern const BwEventBinding *bw_event_bindings_items(u32 *count);
extern const BwEventBinding *bw_event_bindings_mon(u32 *count);
extern const BwEventBinding *bw_event_bindings_field(u32 *count);
extern const BwEventBinding *bw_event_bindings_battle(u32 *count);

static const BwEventBinding *(*const g_area_tables[])(u32 *) = {
    bw_event_bindings_session,
    bw_event_bindings_items,
    bw_event_bindings_mon,
    bw_event_bindings_field,
    bw_event_bindings_battle
};

/* The port's own row for an event, or NULL when the port has none. */
static const BwEventBinding *port_row(u32 kind)
{
    size_t i;

    for (i = 0; i < sizeof(g_area_tables) / sizeof(g_area_tables[0]); i++) {
        u32 n = 0u;
        const BwEventBinding *rows = g_area_tables[i](&n);
        u32 k;

        for (k = 0; rows != NULL && k < n; k++) {
            if (rows[k].kind == kind) {
                return &rows[k];
            }
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* The mod's own file                                                  */
/* ------------------------------------------------------------------ */

/*
 * The mod's own state file: the port's own header (BWMS and 24 bytes) and
 * the mod's own payload after it.  The payload is read the way the port
 * writes it, little-endian, so the check is the same at every host.
 */
static int state_file_ok(const char *path, u32 want)
{
    unsigned char buf[64];
    FILE *fp;
    size_t n;
    u32 payload;

    fp = fopen(path, "rb");
    if (fp == NULL) {
        return 0;
    }
    n = fread(buf, 1, sizeof(buf), fp);
    fclose(fp);
    if (n != (size_t)BW_MOD_STATE_HEADER_SIZE + 4u) {
        return 0;
    }
    if (memcmp(buf, "BWMS", 4) != 0) {
        return 0;
    }
    payload = (u32)buf[BW_MOD_STATE_HEADER_SIZE + 0] |
              ((u32)buf[BW_MOD_STATE_HEADER_SIZE + 1] << 8) |
              ((u32)buf[BW_MOD_STATE_HEADER_SIZE + 2] << 16) |
              ((u32)buf[BW_MOD_STATE_HEADER_SIZE + 3] << 24);
    return payload == want;
}

/* ------------------------------------------------------------------ */
/* The synthetic row                                                   */
/* ------------------------------------------------------------------ */

/*
 * A row the test adds itself, the way the area files add theirs: the
 * port's own call (bw_mod_event_bind) takes one row and binds the pair it
 * names to the event it names.  The builder fills the payload the way the
 * port's own builders do, and the refusal function produces the refused
 * outcome the caller sees.
 */
static void synthetic_build(const BwEventCall *call, BwEventPayload *out)
{
    (void)call;
    memset(&out->menu_open, 0, sizeof(out->menu_open));
}

static void synthetic_refuse(BwEventCall *call)
{
    call->ret = -7;
}

/* ------------------------------------------------------------------ */
/* The tree, taken away                                                */
/* ------------------------------------------------------------------ */

/* Take the tree the test built away. */
static void clean_tree(const char *library)
{
    static const char *const dirs[] = { T_GOOD, T_BAD };
    char path[512];
    size_t i;

    for (i = 0; i < sizeof dirs / sizeof dirs[0]; i++) {
        snprintf(path, sizeof path, "%s/%s", dirs[i], base_name(library));
        remove(path);
        snprintf(path, sizeof path, "%s/eidos_mod.txt", dirs[i]);
        remove(path);
        BW_RMDIR(dirs[i]);
    }
    /* The mod's own file, its folder and the folders above it. */
    snprintf(path, sizeof path, "%s/save/mods/%s/pokeblack.bin", T_ROOT, T_NS);
    remove(path);
    snprintf(path, sizeof path, "%s/save/mods/%s/pokeblack.bin.tmp", T_ROOT, T_NS);
    remove(path);
    snprintf(path, sizeof path, "%s/save/mods/%s", T_ROOT, T_NS);
    BW_RMDIR(path);
    snprintf(path, sizeof path, "%s/save/mods/%s", T_ROOT, T_BAD_NS);
    BW_RMDIR(path);
    snprintf(path, sizeof path, "%s/save/mods", T_ROOT);
    BW_RMDIR(path);
    snprintf(path, sizeof path, "%s/save", T_ROOT);
    BW_RMDIR(path);
    BW_RMDIR(T_ROOT);
}

/* ------------------------------------------------------------------ */
/* The test                                                            */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    BwEventCall call;
    BwEventPayload payload;
    BwEventBinding binding;
    const BwEventBinding *row;
    const BwEventBinding *menu_row;
    char text[1024];
    char value[512];
    char other[512];
    char path[512];
    const char *library;
    const char *name;
    int loaded;

    if (argc < 2 || argv[1] == NULL || argv[1][0] == '\0') {
        printf("usage: %s <the test mod library>\n", argv[0]);
        return 1;
    }
    library = argv[1];
    printf("the test mod library: %s\n", library);

    remove(T_LOG);
    remove(T_ERR_FILE);
    BW_MKDIR(T_ROOT);
    BW_MKDIR(T_BAD);
    BW_MKDIR(T_GOOD);
    /* The mod's own data would live here, in the test's own tree. */
    bw_set_save_dir(T_ROOT "/save");
    BW_REQUIRE("the good mod's tree holds the library",
               put_library(T_GOOD, library) == 0);
    BW_REQUIRE("the bad mod's tree holds the library too",
               put_library(T_BAD, library) == 0);

    snprintf(text, sizeof text,
             "name = End To End Mod\n"
             "order = 20\n"
             "api = %d\n"
             "code = %s\n"
             "state = %s\n",
             (int)BW_MOD_API_VERSION, base_name(library), T_NS);
    BW_REQUIRE("the good mod's manifest is written",
               write_manifest(T_GOOD, text) == 0);
    snprintf(text, sizeof text,
             "name = Wrong Api\n"
             "order = 10\n"
             "api = %d\n"
             "code = %s\n"
             "state = %s\n",
             (int)BW_MOD_API_VERSION + 1, base_name(library), T_BAD_NS);
    BW_REQUIRE("the bad mod's manifest is written",
               write_manifest(T_BAD, text) == 0);

    /*
     * The port's own rows: the tables of the areas, bound the way the port
     * binds them at start-up.  The two events the test fires are the ones
     * whose builders need nothing but the call, so no game data is needed.
     */
    bw_mod_event_reset();
    bw_mod_event_bind_all();
    row = port_row(BW_EVENT_FLAG_SET);
    BW_REQUIRE("the port has a row for flag.set", row != NULL);
    BW_REQUIRE("the flag.set row fires before the function",
               row->point == BW_EVENT_BEFORE);
    menu_row = port_row(BW_EVENT_MENU_OPEN);
    BW_REQUIRE("the port has a row for menu.open", menu_row != NULL);
    BW_REQUIRE("the menu.open row fires before the function",
               menu_row->point == BW_EVENT_BEFORE);

    /*
     * A row of the test's own, added the way the area files add theirs:
     * the port's own call takes one row and binds the pair it names to the
     * event it names.  It is the refusal path the test drives with it, and
     * its builder and its refusal function are the test's own.
     */
    memset(&binding, 0, sizeof(binding));
    binding.region = T_REGION;
    binding.addr = T_ADDR;
    binding.kind = BW_EVENT_MENU_OPEN;
    binding.point = BW_EVENT_BEFORE;
    binding.scope = BW_SCOPE_ANY;
    binding.build = synthetic_build;
    binding.refuse = synthetic_refuse;
    BW_REQUIRE("the synthetic row is bound", bw_mod_event_bind(&binding) == 0);

    /* The mod list: the mod with the wrong api first, then the good one. */
    bw_clear_mods();
    BW_REQUIRE("the first mod is added", bw_add_mod_root(T_BAD) == 0);
    BW_REQUIRE("the second mod is added", bw_add_mod_root(T_GOOD) == 0);

    printf("the load\n");
    err_begin();
    loaded = bw_mod_loader_load_all();
    err_end();
    BW_CHECK_EQ("one mod is loaded", (long)loaded, 1);
    BW_CHECK_EQ("the loader holds one mod", (long)bw_mod_loader_count(), 1);
    name = bw_mod_loader_name_at(0);
    BW_REQUIRE("the loaded mod has a name", name != NULL);
    BW_CHECK_EQ("it is the mod with the port's api",
                strcmp(name, "End To End Mod"), 0);
    BW_CHECK_EQ("the mod built against another api was refused",
                (long)bw_mod_loader_count(), 1);
    BW_CHECK_EQ("the refusal was reported",
                (long)reported("does not match the port"), 1);
    BW_CHECK_EQ("the handler for the event the port does not have was refused",
                (long)bw_mod_event_handler_count(T_UNKNOWN_KIND), 0);
    BW_CHECK_EQ("and it was reported", (long)reported("was not bound"), 1);
    BW_CHECK_EQ("the mod's handler is bound",
                (long)bw_mod_event_handler_count(BW_EVENT_FLAG_SET), 1);
    BW_CHECK_EQ("the mod's other handler is bound too",
                (long)bw_mod_event_handler_count(BW_EVENT_MENU_OPEN), 1);
    BW_CHECK_EQ("the mod's namespace is the manifest's",
                strcmp(bw_mod_loader_namespace_at(0), T_NS), 0);

    log_read();
    BW_REQUIRE("the mod recorded what it was handed",
               log_value("start_api=", value, sizeof value) == 0);
    BW_CHECK_EQ("the host table's api is the port's",
                strtol(value, NULL, 10), (long)BW_MOD_API_VERSION);
    BW_REQUIRE("the state table is filled",
               log_value("start_state_table=", value, sizeof value) == 0);
    BW_CHECK_EQ("the mod was given the state table", strcmp(value, "1"), 0);
    BW_REQUIRE("the services table is filled",
               log_value("start_services=", value, sizeof value) == 0);
    BW_CHECK_EQ("the mod was given the services table",
                strcmp(value, "1"), 0);
    BW_CHECK_EQ("the refused mod never started",
                (long)log_count("start_api="), 1);

    printf("the event\n");
    memset(&call, 0, sizeof(call));
    memset(&payload, 0, sizeof(payload));
    call.nargs = 2;
    call.a[1] = T_FLAG;
    BW_CHECK_EQ("the event goes on",
                (long)bw_mod_event_enter(row->region, row->addr, &call,
                                         &payload), 0);
    log_read();
    BW_REQUIRE("the handler ran",
               log_value("observer_kind=", value, sizeof value) == 0);
    BW_CHECK_EQ("the handler was handed the event it bound",
                strtol(value, NULL, 10), (long)BW_EVENT_FLAG_SET);
    BW_REQUIRE("the payload's size is known",
               log_value("observer_size=", value, sizeof value) == 0);
    BW_CHECK_EQ("the port built the whole payload",
                strtol(value, NULL, 10), (long)sizeof(BwFlagSet));
    BW_REQUIRE("the flag is known",
               log_value("observer_flag=", value, sizeof value) == 0);
    BW_CHECK_EQ("the handler saw the flag the call carried",
                strtol(value, NULL, 0), (long)T_FLAG);
    BW_REQUIRE("the value is known",
               log_value("observer_value=", value, sizeof value) == 0);
    BW_CHECK_EQ("the handler saw the value the port built",
                strtol(value, NULL, 10), 1);
    BW_REQUIRE("the handler's state is known",
               log_value("observer_state=", other, sizeof other) == 0);
    BW_REQUIRE("the mod's state is known",
               log_value("start_state=", value, sizeof value) == 0);
    BW_CHECK_EQ("the mod's state pointer reached the handler",
                strcmp(value, other), 0);
    BW_REQUIRE("the handler ran once",
               log_value("observer_calls=", value, sizeof value) == 0);
    BW_CHECK_EQ("and it ran once", strtol(value, NULL, 10), 1);

    printf("the mod's own data\n");
    BW_REQUIRE("the mod's own folder is known",
               log_value("observer_dir=", value, sizeof value) == 0);
    snprintf(path, sizeof path, "/mods/%s", T_NS);
    BW_CHECK_EQ("the namespace in force is the mod's own",
                strstr(value, path) != NULL, 1);
    BW_REQUIRE("the store is known",
               log_value("observer_store=", value, sizeof value) == 0);
    BW_CHECK_EQ("the mod's own data was stored", strcmp(value, "ok"), 0);
    BW_REQUIRE("the load is known",
               log_value("observer_load=", value, sizeof value) == 0);
    BW_CHECK_EQ("the mod read its own data back",
                strtol(value, NULL, 10), (long)sizeof(u32));
    BW_REQUIRE("the value is known",
               log_value("observer_loaded=", value, sizeof value) == 0);
    BW_CHECK_EQ("and it is the value the mod stored",
                strtol(value, NULL, 10), 1);
    snprintf(path, sizeof path, "%s/save/mods/%s/pokeblack.bin", T_ROOT, T_NS);
    BW_CHECK_EQ("the mod's own file holds the mod's own data",
                state_file_ok(path, 1u), 1);
    snprintf(path, sizeof path, "%s/save/mods/%s/pokeblack.bin", T_ROOT,
             T_BAD_NS);
    BW_CHECK_EQ("the refused mod kept no data of its own",
                state_file_ok(path, 1u), 0);

    printf("the refusal\n");
    memset(&call, 0, sizeof(call));
    memset(&payload, 0, sizeof(payload));
    call.ret = 1234;
    BW_CHECK_EQ("the action is refused",
                (long)bw_mod_event_enter(menu_row->region, menu_row->addr,
                                         &call, &payload), 1);
    BW_CHECK_EQ("the refusal produced the refused outcome",
                (long)call.ret, 0);
    log_read();
    BW_REQUIRE("the refuser ran",
               log_value("refuser_kind=", value, sizeof value) == 0);
    BW_CHECK_EQ("the refuser was handed the event it bound",
                strtol(value, NULL, 10), (long)BW_EVENT_MENU_OPEN);
    BW_REQUIRE("the refuser's state is known",
               log_value("refuser_state=", other, sizeof other) == 0);
    BW_REQUIRE("the mod's state is known",
               log_value("start_state=", value, sizeof value) == 0);
    BW_CHECK_EQ("the refuser got the mod's own state too",
                strcmp(value, other), 0);

    printf("the refusal of a synthetic row\n");
    memset(&call, 0, sizeof(call));
    memset(&payload, 0, sizeof(payload));
    call.ret = 1234;
    BW_CHECK_EQ("the action is refused",
                (long)bw_mod_event_enter(T_REGION, T_ADDR, &call,
                                         &payload), 1);
    BW_CHECK_EQ("the refusal function produced the refused outcome",
                (long)call.ret, -7);
    log_read();
    BW_CHECK_EQ("the refuser ran for the synthetic row too",
                (long)log_count("refuser_kind="), 2);
    BW_CHECK_EQ("the handler for the event the port does not have never ran",
                (long)log_count("unknown_ran="), 0);

    printf("the stop\n");
    bw_mod_loader_stop_all();
    BW_CHECK_EQ("no mod is loaded", (long)bw_mod_loader_count(), 0);
    log_read();
    BW_CHECK_EQ("the mod's stop ran", (long)log_has("stop_calls="), 1);
    BW_CHECK_EQ("and the library was unloaded", (long)log_has("unload"), 1);

    printf("the event of a stopped mod\n");
    memset(&call, 0, sizeof(call));
    memset(&payload, 0, sizeof(payload));
    call.nargs = 2;
    call.a[1] = T_FLAG;
    bw_mod_event_enter(row->region, row->addr, &call, &payload);
    log_read();
    BW_CHECK_EQ("the handler does not run again",
                (long)log_count("observer_kind="), 1);

    remove(T_LOG);
    remove(T_ERR_FILE);
    clean_tree(library);
    BW_REPORT("test_mod_endtoend");
}

#endif /* BW_MOD_TEST_LIBRARY */
