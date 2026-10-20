/*
 * tests/test_mod_loader.c -- the loader: the library, the host table, the
 * handler and the way down.
 *
 * The loader is sdk/mod_loader.c; the contract it keeps is the one of
 * docs/mod-implementation.md, section 3, and of docs/mod-abi.md, sections 1
 * and 3:
 *
 *   - the library opens, and the port finds eidos_mod_start and
 *     eidos_mod_stop in it;
 *   - the host table the mod is handed carries the port's own interface
 *     version, and the mod binds its handlers through it;
 *   - the handler the mod bound runs when the event fires, and it receives
 *     the state pointer the mod's own start returned;
 *   - a mod built against another api is refused and never loaded;
 *   - the way down is the order docs/mod-state.md, section 2, constraint 2,
 *     fixes: the slots the mod installed go back to their originals, the
 *     mod's stop runs, and the library is unloaded last.
 *
 * The mod the loader loads is a library of its own: the same file, built
 * with BW_MOD_TEST_LIBRARY defined, is a tiny shared library that binds one
 * handler to one event and records what it was handed (the host table's api,
 * the state pointer, the event id).  It writes the record into a log file
 * in the test's own working directory, because the library is unloaded
 * before the test can ask it anything.
 *
 * Nothing here needs game data: the mod's tree is a directory in the test's
 * own working directory, the library is a copy of the one the build makes,
 * and the event is fired through a fake binding the test registers itself.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mod_api.h"
#include "mod_state.h"

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
#define T_LOG "mod_loader_log.txt"

/* The mod's own state: the pointer the start entry point returns and every
 * handler of the mod receives. */
typedef struct TestModState {
    u32 api;        /* the host table's interface version */
    u32 calls;      /* how many times the handler ran */
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
 * The one handler: it records the event it was handed and the state it was
 * given, and it goes on (docs/mod-events.md, section 2).
 */
static int test_mod_handler(void *mod_state, BwEvent *event)
{
    TestModState *s = (TestModState *)mod_state;

    record("handler_kind=%u", event != NULL ? (unsigned int)event->kind : 0u);
    record("handler_state=%p", mod_state);
    if (s != NULL) {
        s->calls++;
    }
    record("handler_calls=%u", s != NULL ? (unsigned int)s->calls : 0u);
    return BW_EVENT_CONTINUE;
}

/*
 * The library's entry point (docs/mod-abi.md, section 1): it records what
 * the host table holds, binds one handler to one event and gives the port
 * the mod's own state.
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
    record("start_api=%u", (unsigned int)host->api);
    record("services=%d",
           (host->services != NULL && host->services->state_store != NULL)
               ? 1 : 0);
    if (host->on(BW_EVENT_MONEY_CHANGE, test_mod_handler) != 0) {
        record("bind_failed=1");
        free(s);
        return -1;
    }
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
 * The symbol the manifest's override names: the mod's own hook, the one the
 * loader installs in the slot registry.
 */
int BW_MOD_EXPORT test_mod_hook(void)
{
    return 1;
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

#include "mod_event.h"
#include "mod_slot.h"
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
#define BW_RMDIR(p) rmdir((p))
#endif

/* ================================================================== */
/* The test                                                            */
/* ================================================================== */

/*
 * The loader's own calls (sdk/mod_loader.c).  That file holds its
 * prototypes until the header which will carry them for the rest of the
 * port is written, so the test declares the ones it uses.
 */
int         bw_mod_loader_load_all(void);
void        bw_mod_loader_stop_all(void);
int         bw_mod_loader_stop(u32 index);
u32         bw_mod_loader_count(void);
const char *bw_mod_loader_name_at(u32 index);
const char *bw_mod_loader_namespace_at(u32 index);

/* The tree the test builds: one mod with the port's api, one with another. */
#define T_ROOT "mod_loader_tmp"
#define T_BAD  T_ROOT "/badapi"
#define T_GOOD T_ROOT "/good"

/* The log the library writes into. */
#define T_LOG "mod_loader_log.txt"

/* The pair the fake binding is registered for: a region and an address the
 * game does not use. */
#define T_REGION 0x0042u
#define T_ADDR   0x0210ABCDu

/* The pair the mod's override names, and the one the test registers in the
 * declared set, so the manifest's check passes it. */
#define T_OVL_REGION BW_MOD_REGION_OVL(93)
#define T_OVL_ADDR   0x021CBA40u

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
/* The tree                                                            */
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
    BW_RMDIR(T_ROOT);
}

/* ------------------------------------------------------------------ */
/* The test                                                            */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    BwEventBinding binding;
    BwEventCall call;
    BwEventPayload payload;
    BwModDeclared declared[1];
    char text[1024];
    char value[64];
    char other[64];
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
             "name = Test Mod\n"
             "order = 20\n"
             "api = %d\n"
             "code = %s\n"
             "state = testmod\n"
             "override = ov%03u:0x%08X replace test_mod_hook\n",
             (int)BW_MOD_API_VERSION, base_name(library),
             (unsigned int)T_OVL_REGION, (unsigned int)T_OVL_ADDR);
    BW_REQUIRE("the good mod's manifest is written",
               write_manifest(T_GOOD, text) == 0);
    snprintf(text, sizeof text,
             "name = Wrong Api\n"
             "order = 10\n"
             "api = %d\n"
             "code = %s\n"
             "state = wrongapi\n",
             (int)BW_MOD_API_VERSION + 1, base_name(library));
    BW_REQUIRE("the bad mod's manifest is written",
               write_manifest(T_BAD, text) == 0);

    /*
     * The declared set: the test registers the one pair the mod's override
     * names, so the manifest's check passes it (docs/mod-state.md,
     * section 4).
     */
    declared[0].region = T_OVL_REGION;
    declared[0].addr = T_OVL_ADDR;
    bw_mod_declare_table(declared, 1);

    /*
     * The event the mod binds: a fake binding for a pair the game does not
     * use, so the test can fire the event itself (the way
     * tests/test_mod_event.c does).
     */
    bw_mod_event_reset();
    memset(&binding, 0, sizeof(binding));
    binding.region = T_REGION;
    binding.addr = T_ADDR;
    binding.kind = BW_EVENT_MONEY_CHANGE;
    binding.point = BW_EVENT_BEFORE;
    binding.scope = BW_SCOPE_ANY;
    BW_REQUIRE("the fake binding is registered",
               bw_mod_event_bind(&binding) == 0);

    /* The mod list: the mod with the wrong api first, then the good one. */
    bw_clear_mods();
    BW_REQUIRE("the first mod is added", bw_add_mod_root(T_BAD) == 0);
    BW_REQUIRE("the second mod is added", bw_add_mod_root(T_GOOD) == 0);

    printf("the load\n");
    loaded = bw_mod_loader_load_all();
    BW_CHECK_EQ("one mod is loaded", (long)loaded, 1);
    BW_CHECK_EQ("the loader holds one mod", (long)bw_mod_loader_count(), 1);
    name = bw_mod_loader_name_at(0);
    BW_REQUIRE("the loaded mod has a name", name != NULL);
    BW_CHECK_EQ("it is the mod with the port's api", strcmp(name, "Test Mod"), 0);
    BW_CHECK_EQ("the mod built against another api was refused",
                (long)bw_mod_loader_count(), 1);

    log_read();
    BW_CHECK_EQ("and it never started", (long)log_count("start_api="), 1);
    BW_REQUIRE("the mod recorded what it was handed",
               log_value("start_api=", value, sizeof value) == 0);
    BW_CHECK_EQ("the host table's api is the port's",
                strtol(value, NULL, 10), (long)BW_MOD_API_VERSION);
    BW_REQUIRE("the services table is filled",
               log_value("services=", other, sizeof other) == 0);
    BW_CHECK_EQ("the mod was given the services table", strcmp(other, "1"), 0);
    BW_CHECK_EQ("the handler is bound",
                (long)bw_mod_event_handler_count(BW_EVENT_MONEY_CHANGE), 1);
    BW_CHECK_EQ("the override is installed",
                bw_mod_slot_lookup(T_OVL_REGION, T_OVL_ADDR) != NULL, 1);
    BW_CHECK_EQ("the mod's namespace is the manifest's",
                strcmp(bw_mod_loader_namespace_at(0), "testmod"), 0);

    printf("the event\n");
    memset(&call, 0, sizeof(call));
    memset(&payload, 0, sizeof(payload));
    call.nargs = 1;
    call.a[0] = 10;
    BW_CHECK_EQ("the event goes on",
                (long)bw_mod_event_enter(T_REGION, T_ADDR, &call, &payload), 0);
    log_read();
    BW_REQUIRE("the handler ran",
               log_value("handler_kind=", value, sizeof value) == 0);
    BW_CHECK_EQ("the handler was handed the event it bound",
                strtol(value, NULL, 10), (long)BW_EVENT_MONEY_CHANGE);
    BW_REQUIRE("the handler's state is known",
               log_value("handler_state=", other, sizeof other) == 0);
    BW_REQUIRE("the mod's state is known",
               log_value("start_state=", value, sizeof value) == 0);
    BW_CHECK_EQ("the mod's state pointer reached the handler",
                strcmp(value, other), 0);
    BW_REQUIRE("the handler ran once",
               log_value("handler_calls=", value, sizeof value) == 0);
    BW_CHECK_EQ("and it ran once", strtol(value, NULL, 10), 1);

    printf("the stop\n");
    bw_mod_loader_stop_all();
    BW_CHECK_EQ("no mod is loaded", (long)bw_mod_loader_count(), 0);
    BW_CHECK_EQ("the slot is back to the original",
                bw_mod_slot_lookup(T_OVL_REGION, T_OVL_ADDR) == NULL, 1);
    log_read();
    BW_CHECK_EQ("the mod's stop ran", (long)log_has("stop_calls="), 1);
    BW_CHECK_EQ("and the library was unloaded", (long)log_has("unload"), 1);

    printf("the event of a stopped mod\n");
    memset(&call, 0, sizeof(call));
    memset(&payload, 0, sizeof(payload));
    bw_mod_event_enter(T_REGION, T_ADDR, &call, &payload);
    log_read();
    BW_CHECK_EQ("the handler does not run again",
                (long)log_count("handler_kind="), 1);

    remove(T_LOG);
    clean_tree(library);
    BW_REPORT("test_mod_loader");
}

#endif /* BW_MOD_TEST_LIBRARY */
