/*
 * tests/test_mod_wasm.c -- a wasm code mod through the port's own loader
 * (sdk/mod_wasm.c, docs/mod-code-wasm.md).
 *
 * The module is assembled by hand below, so the test needs no wasm
 * toolchain.  In text form it is:
 *
 *   (module
 *     (import "eidos" "on" (func $on (param i32 i32) (result i32)))
 *     (table 2 funcref)
 *     (memory (export "memory") 1)
 *     (elem (i32.const 0) $flag $trap)
 *     (func (export "eidos_mod_start") (result i32)
 *       (drop (memory.grow (i32.const 2)))                ;; pages 2 and 3
 *       (drop (call $on (i32.const 111) (i32.const 0)))   ;; flag.set -> $flag
 *       (drop (call $on (i32.const 15) (i32.const 1)))    ;; menu.open -> $trap
 *       (i32.const 0))
 *     (func (export "eidos_mod_stop"))
 *     ;; refuse when the flag is 0x123 (BwFlagSet.flag is at offset 8) and
 *     ;; the copy is in page 1, the port's page: above the page the module
 *     ;; declared and below the pages it grew
 *     (func $flag (param $e i32) (result i32)
 *       (i32.and
 *         (i32.eq (i32.load16_u offset=8 align=1 (local.get $e)) (i32.const 0x123))
 *         (i32.eq (i32.shr_u (local.get $e) (i32.const 16)) (i32.const 1))))
 *     (func $trap (param i32) (result i32) (unreachable)))
 *
 * k_bad_import is the same module with one more import,
 * (import "eidos" "nonexistent" (func)), which the port does not provide.
 *
 * What the test checks:
 *
 *   - the loader tells the module from a native library and starts it;
 *   - the module's `on` imports bind handlers through the loader;
 *   - the payload reaches the handler as a copy in the port's own page, not
 *     in memory the module declared or grew, and the handler's answer
 *     refuses or lets the action go on;
 *   - a handler that traps lets the action go on and the port runs on;
 *   - a file with the wasm magic that is not a valid module is refused;
 *   - a module with an import the port does not provide is refused at load;
 *   - a module with no memory.grow opcode loads and gets the same page;
 *   - an event fired inside a handler gets its own copy above the
 *     first, and the first copy is intact when the inner call returns;
 *   - after the stop no handler of the module runs.
 *
 * A build without the runtime refuses the module with a message; the test
 * checks that and nothing else.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mod_api.h"
#include "mod_event.h"
#include "mod_loader.h"
#include "mod_wasm.h"
#include "save.h"
#include "types.h"
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

#define T_ROOT "mod_wasm_tmp"
#define T_GOOD T_ROOT "/good"
#define T_BAD  T_ROOT "/bad"
#define T_IMPORT T_ROOT "/import"
#define T_NOGROW T_ROOT "/nogrow"
#define T_NESTED T_ROOT "/nested"
#define T_FLAG 0x123u

static const unsigned char k_module[] = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x01, 0x13, 0x04, 0x60,
    0x02, 0x7f, 0x7f, 0x01, 0x7f, 0x60, 0x01, 0x7f, 0x01, 0x7f, 0x60, 0x00,
    0x01, 0x7f, 0x60, 0x00, 0x00, 0x02, 0x0c, 0x01, 0x05, 0x65, 0x69, 0x64,
    0x6f, 0x73, 0x02, 0x6f, 0x6e, 0x00, 0x00, 0x03, 0x05, 0x04, 0x02, 0x03,
    0x01, 0x01, 0x04, 0x04, 0x01, 0x70, 0x00, 0x02, 0x05, 0x03, 0x01, 0x00,
    0x01, 0x07, 0x2d, 0x03, 0x06, 0x6d, 0x65, 0x6d, 0x6f, 0x72, 0x79, 0x02,
    0x00, 0x0f, 0x65, 0x69, 0x64, 0x6f, 0x73, 0x5f, 0x6d, 0x6f, 0x64, 0x5f,
    0x73, 0x74, 0x61, 0x72, 0x74, 0x00, 0x01, 0x0e, 0x65, 0x69, 0x64, 0x6f,
    0x73, 0x5f, 0x6d, 0x6f, 0x64, 0x5f, 0x73, 0x74, 0x6f, 0x70, 0x00, 0x02,
    0x09, 0x08, 0x01, 0x00, 0x41, 0x00, 0x0b, 0x02, 0x03, 0x04, 0x0a, 0x36,
    0x04, 0x18, 0x00, 0x41, 0x02, 0x40, 0x00, 0x1a, 0x41, 0xef, 0x00, 0x41,
    0x00, 0x10, 0x00, 0x1a, 0x41, 0x0f, 0x41, 0x01, 0x10, 0x00, 0x1a, 0x41,
    0x00, 0x0b, 0x02, 0x00, 0x0b, 0x14, 0x00, 0x20, 0x00, 0x2f, 0x00, 0x08,
    0x41, 0xa3, 0x02, 0x46, 0x20, 0x00, 0x41, 0x10, 0x76, 0x41, 0x01, 0x46,
    0x71, 0x0b, 0x03, 0x00, 0x00, 0x0b
};

static const unsigned char k_bad_import[] = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x01, 0x13, 0x04, 0x60,
    0x02, 0x7f, 0x7f, 0x01, 0x7f, 0x60, 0x01, 0x7f, 0x01, 0x7f, 0x60, 0x00,
    0x01, 0x7f, 0x60, 0x00, 0x00, 0x02, 0x20, 0x02, 0x05, 0x65, 0x69, 0x64,
    0x6f, 0x73, 0x02, 0x6f, 0x6e, 0x00, 0x00, 0x05, 0x65, 0x69, 0x64, 0x6f,
    0x73, 0x0b, 0x6e, 0x6f, 0x6e, 0x65, 0x78, 0x69, 0x73, 0x74, 0x65, 0x6e,
    0x74, 0x00, 0x03, 0x03, 0x05, 0x04, 0x02, 0x03, 0x01, 0x01, 0x04, 0x04,
    0x01, 0x70, 0x00, 0x02, 0x05, 0x03, 0x01, 0x00, 0x01, 0x07, 0x2d, 0x03,
    0x06, 0x6d, 0x65, 0x6d, 0x6f, 0x72, 0x79, 0x02, 0x00, 0x0f, 0x65, 0x69,
    0x64, 0x6f, 0x73, 0x5f, 0x6d, 0x6f, 0x64, 0x5f, 0x73, 0x74, 0x61, 0x72,
    0x74, 0x00, 0x02, 0x0e, 0x65, 0x69, 0x64, 0x6f, 0x73, 0x5f, 0x6d, 0x6f,
    0x64, 0x5f, 0x73, 0x74, 0x6f, 0x70, 0x00, 0x03, 0x09, 0x08, 0x01, 0x00,
    0x41, 0x00, 0x0b, 0x02, 0x04, 0x05, 0x0a, 0x36, 0x04, 0x18, 0x00, 0x41,
    0x02, 0x40, 0x00, 0x1a, 0x41, 0xef, 0x00, 0x41, 0x00, 0x10, 0x00, 0x1a,
    0x41, 0x0f, 0x41, 0x01, 0x10, 0x00, 0x1a, 0x41, 0x00, 0x0b, 0x02, 0x00,
    0x0b, 0x14, 0x00, 0x20, 0x00, 0x2f, 0x00, 0x08, 0x41, 0xa3, 0x02, 0x46,
    0x20, 0x00, 0x41, 0x10, 0x76, 0x41, 0x01, 0x46, 0x71, 0x0b, 0x03, 0x00,
    0x00, 0x0b
};

/* The module without its memory.grow: no memory.size or memory.grow opcode
 * at all.  WAMR's loader fixes the memory of such a module at its data
 * unless tools/fetch_wamr.py's change is in; the port's page must still be
 * added, and it is page 1 here too. */
static const unsigned char k_no_grow[] = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x01, 0x13, 0x04, 0x60,
    0x02, 0x7f, 0x7f, 0x01, 0x7f, 0x60, 0x01, 0x7f, 0x01, 0x7f, 0x60, 0x00,
    0x01, 0x7f, 0x60, 0x00, 0x00, 0x02, 0x0c, 0x01, 0x05, 0x65, 0x69, 0x64,
    0x6f, 0x73, 0x02, 0x6f, 0x6e, 0x00, 0x00, 0x03, 0x05, 0x04, 0x02, 0x03,
    0x01, 0x01, 0x04, 0x04, 0x01, 0x70, 0x00, 0x02, 0x05, 0x03, 0x01, 0x00,
    0x01, 0x07, 0x2d, 0x03, 0x06, 0x6d, 0x65, 0x6d, 0x6f, 0x72, 0x79, 0x02,
    0x00, 0x0f, 0x65, 0x69, 0x64, 0x6f, 0x73, 0x5f, 0x6d, 0x6f, 0x64, 0x5f,
    0x73, 0x74, 0x61, 0x72, 0x74, 0x00, 0x01, 0x0e, 0x65, 0x69, 0x64, 0x6f,
    0x73, 0x5f, 0x6d, 0x6f, 0x64, 0x5f, 0x73, 0x74, 0x6f, 0x70, 0x00, 0x02,
    0x09, 0x08, 0x01, 0x00, 0x41, 0x00, 0x0b, 0x02, 0x03, 0x04, 0x0a, 0x31,
    0x04, 0x13, 0x00, 0x41, 0xef, 0x00, 0x41, 0x00, 0x10, 0x00, 0x1a, 0x41,
    0x0f, 0x41, 0x01, 0x10, 0x00, 0x1a, 0x41, 0x00, 0x0b, 0x02, 0x00, 0x0b,
    0x14, 0x00, 0x20, 0x00, 0x2f, 0x00, 0x08, 0x41, 0xa3, 0x02, 0x46, 0x20,
    0x00, 0x41, 0x10, 0x76, 0x41, 0x01, 0x46, 0x71, 0x0b, 0x03, 0x00, 0x00,
    0x0b
};

#if defined(EIDOS_HAVE_WAMR)
/*
 * The nested-event module.  In the game a state call can run a game
 * function that fires an event while a handler of the same mod runs; the
 * test gives the module an import of its own that does the same, so the
 * nesting does not need the game:
 *
 *   (import "eidostest" "fire" (func $fire (param i32) (result i32)))
 *   ;; menu.open -> $outer, flag.set -> $inner
 *   ;; $outer: its copy is at 65536 (the port's page), it fires flag.set
 *   ;;   for flag 0x200 from inside, and it refuses when the inner handler
 *   ;;   refused and its own copy still holds kind 15 after the inner call
 *   (func $outer (param $e i32) (result i32)
 *     (i32.and (i32.eq (call $fire (i32.const 0)) (i32.const 1))
 *       (i32.and (i32.eq (local.get $e) (i32.const 65536))
 *                (i32.eq (i32.load offset=4 (local.get $e)) (i32.const 15)))))
 *   ;; $inner: refuses when it sees flag 0x200 in a copy above the outer one
 *   (func $inner (param $e i32) (result i32)
 *     (i32.and (i32.eq (i32.load16_u offset=8 (local.get $e)) (i32.const 0x200))
 *              (i32.eq (local.get $e) (i32.const 65544))))
 */
static const unsigned char k_nested[] = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x01, 0x13, 0x04, 0x60,
    0x02, 0x7f, 0x7f, 0x01, 0x7f, 0x60, 0x01, 0x7f, 0x01, 0x7f, 0x60, 0x00,
    0x01, 0x7f, 0x60, 0x00, 0x00, 0x02, 0x1d, 0x02, 0x05, 0x65, 0x69, 0x64,
    0x6f, 0x73, 0x02, 0x6f, 0x6e, 0x00, 0x00, 0x09, 0x65, 0x69, 0x64, 0x6f,
    0x73, 0x74, 0x65, 0x73, 0x74, 0x04, 0x66, 0x69, 0x72, 0x65, 0x00, 0x01,
    0x03, 0x05, 0x04, 0x02, 0x03, 0x01, 0x01, 0x04, 0x04, 0x01, 0x70, 0x00,
    0x02, 0x05, 0x03, 0x01, 0x00, 0x01, 0x07, 0x2d, 0x03, 0x06, 0x6d, 0x65,
    0x6d, 0x6f, 0x72, 0x79, 0x02, 0x00, 0x0f, 0x65, 0x69, 0x64, 0x6f, 0x73,
    0x5f, 0x6d, 0x6f, 0x64, 0x5f, 0x73, 0x74, 0x61, 0x72, 0x74, 0x00, 0x02,
    0x0e, 0x65, 0x69, 0x64, 0x6f, 0x73, 0x5f, 0x6d, 0x6f, 0x64, 0x5f, 0x73,
    0x74, 0x6f, 0x70, 0x00, 0x03, 0x09, 0x08, 0x01, 0x00, 0x41, 0x00, 0x0b,
    0x02, 0x04, 0x05, 0x0a, 0x47, 0x04, 0x13, 0x00, 0x41, 0xef, 0x00, 0x41,
    0x00, 0x10, 0x00, 0x1a, 0x41, 0x0f, 0x41, 0x01, 0x10, 0x00, 0x1a, 0x41,
    0x00, 0x0b, 0x02, 0x00, 0x0b, 0x13, 0x00, 0x20, 0x00, 0x2f, 0x00, 0x08,
    0x41, 0x80, 0x04, 0x46, 0x20, 0x00, 0x41, 0x88, 0x80, 0x04, 0x46, 0x71,
    0x0b, 0x1a, 0x00, 0x41, 0x00, 0x10, 0x01, 0x41, 0x01, 0x46, 0x20, 0x00,
    0x41, 0x80, 0x80, 0x04, 0x46, 0x20, 0x00, 0x28, 0x02, 0x04, 0x41, 0x0f,
    0x46, 0x71, 0x71, 0x0b
};

#include "wasm_export.h"

/* eidostest.fire: flag.set for flag 0x200, from inside a handler. */
static int32_t test_fire(wasm_exec_env_t env, int32_t unused)
{
    BwEventPayload p;

    (void)env;
    (void)unused;
    memset(&p, 0, sizeof p);
    p.flag_set.flag = 0x200u;
    p.flag_set.value = 1u;
    return bw_mod_event_fire(BW_EVENT_FLAG_SET, &p);
}

static NativeSymbol k_test_natives[] = {
    { "fire", (void *)test_fire, "(i)i", NULL },
};
#endif

/* The wasm magic and version, then bytes that are no section. */
static const unsigned char k_broken[] = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x7e, 0x7e, 0x7e
};

extern const BwEventBinding *bw_event_bindings_field(u32 *count);

/*
 * flag.set cannot be refused at the port's own row, so the test binds a row
 * of its own for it, the way tests/test_mod_endtoend.c does: a pair the game
 * does not use, a builder that fills the flag from the call, and a refusal
 * function that marks the refused outcome.
 */
#define T_REGION 0x0042u
#define T_ADDR   0x0210ABCDu

static void synthetic_build(const BwEventCall *call, BwEventPayload *out)
{
    memset(&out->flag_set, 0, sizeof(out->flag_set));
    out->flag_set.flag = (u16)call->a[1];
    out->flag_set.value = 1u;
}

static void synthetic_refuse(BwEventCall *call)
{
    call->ret = -7;
}

/* The port's own row for an event in one area table, or NULL. */
static const BwEventBinding *port_row(const BwEventBinding *(*table)(u32 *),
                                      u32 kind)
{
    u32 n = 0u;
    const BwEventBinding *rows = table(&n);
    u32 k;

    for (k = 0; rows != NULL && k < n; k++) {
        if (rows[k].kind == kind) {
            return &rows[k];
        }
    }
    return NULL;
}

static int write_file(const char *dir, const char *name, const void *data,
                      size_t size)
{
    char path[512];
    FILE *fp;

    snprintf(path, sizeof path, "%s/%s", dir, name);
    fp = fopen(path, "wb");
    if (fp == NULL) {
        return -1;
    }
    if (fwrite(data, 1, size, fp) != size) {
        fclose(fp);
        return -1;
    }
    fclose(fp);
    return 0;
}

static int write_mod(const char *dir, const char *title, const char *ns,
                     const void *code, size_t size)
{
    char text[512];

    snprintf(text, sizeof text,
             "name = %s\n"
             "api = %d\n"
             "code = mod.wasm\n"
             "state = %s\n",
             title, (int)BW_MOD_API_VERSION, ns);
    if (write_file(dir, "eidos_mod.txt", text, strlen(text)) != 0) {
        return -1;
    }
    return write_file(dir, "mod.wasm", code, size);
}

static void clean_tree(void)
{
    static const char *const dirs[] = { T_GOOD, T_BAD, T_IMPORT, T_NOGROW, T_NESTED };
    char path[512];
    size_t i;

    for (i = 0; i < sizeof dirs / sizeof dirs[0]; i++) {
        snprintf(path, sizeof path, "%s/mod.wasm", dirs[i]);
        remove(path);
        snprintf(path, sizeof path, "%s/eidos_mod.txt", dirs[i]);
        remove(path);
        BW_RMDIR(dirs[i]);
    }
    BW_RMDIR(T_ROOT "/save/mods/wasmgood");
    BW_RMDIR(T_ROOT "/save/mods/wasmbad");
    BW_RMDIR(T_ROOT "/save/mods/wasmimport");
    BW_RMDIR(T_ROOT "/save/mods/wasmnogrow");
    BW_RMDIR(T_ROOT "/save/mods/wasmnested");
    BW_RMDIR(T_ROOT "/save/mods");
    BW_RMDIR(T_ROOT "/save");
    BW_RMDIR(T_ROOT);
}

/* Fire flag.set for `flag` on the test's row; the dispatch's answer. */
static int fire_flag(u32 flag)
{
    BwEventCall call;
    BwEventPayload payload;
    int rc;

    memset(&call, 0, sizeof call);
    memset(&payload, 0, sizeof payload);
    call.nargs = 2;
    call.a[1] = flag;
    rc = bw_mod_event_enter(T_REGION, T_ADDR, &call, &payload);
    /* A refusal runs the row's refusal function. */
    return rc == 1 && call.ret == -7 ? 1 : (rc == 0 ? 0 : -1);
}

int main(void)
{
    const BwEventBinding *menu_row;
    BwEventBinding binding;
    BwEventCall call;
    BwEventPayload payload;
    char path[512];
    int loaded;

    clean_tree();
    BW_MKDIR(T_ROOT);
    BW_MKDIR(T_GOOD);
    BW_MKDIR(T_BAD);
    BW_MKDIR(T_IMPORT);
    BW_MKDIR(T_NOGROW);
    bw_set_save_dir(T_ROOT "/save");
    BW_REQUIRE("the module is written",
               write_mod(T_GOOD, "Wasm Mod", "wasmgood", k_module,
                         sizeof k_module) == 0);
    BW_REQUIRE("the broken module is written",
               write_mod(T_BAD, "Broken Wasm", "wasmbad", k_broken,
                         sizeof k_broken) == 0);
    BW_REQUIRE("the module with a missing import is written",
               write_mod(T_IMPORT, "Missing Import", "wasmimport",
                         k_bad_import, sizeof k_bad_import) == 0);
    BW_REQUIRE("the module that never grows its memory is written",
               write_mod(T_NOGROW, "No Grow", "wasmnogrow", k_no_grow,
                         sizeof k_no_grow) == 0);
    snprintf(path, sizeof path, "%s/mod.wasm", T_GOOD);
    BW_CHECK_EQ("the module is told from a library",
                (long)bw_mod_wasm_is_module(path), 1);

    bw_mod_event_reset();
    bw_mod_event_bind_all();
    menu_row = port_row(bw_event_bindings_field, BW_EVENT_MENU_OPEN);
    memset(&binding, 0, sizeof binding);
    binding.region = T_REGION;
    binding.addr = T_ADDR;
    binding.kind = BW_EVENT_FLAG_SET;
    binding.point = BW_EVENT_BEFORE;
    binding.scope = BW_SCOPE_ANY;
    binding.build = synthetic_build;
    binding.refuse = synthetic_refuse;
    BW_REQUIRE("the test's flag.set row is bound",
               bw_mod_event_bind(&binding) == 0);
    BW_REQUIRE("the port has a row for menu.open", menu_row != NULL);

    bw_clear_mods();
    BW_REQUIRE("the broken mod is added", bw_add_mod_root(T_BAD) == 0);
    BW_REQUIRE("the good mod is added", bw_add_mod_root(T_GOOD) == 0);
    BW_REQUIRE("the mod with a missing import is added",
               bw_add_mod_root(T_IMPORT) == 0);

    printf("the load\n");
    loaded = bw_mod_loader_load_all();
    if (!bw_mod_wasm_available()) {
        printf("this build has no wasm runtime: every module is refused\n");
        BW_CHECK_EQ("no module is loaded", (long)loaded, 0);
        clean_tree();
        BW_REPORT("test_mod_wasm");
    }
    BW_CHECK_EQ("only the good module is loaded",
                (long)loaded, 1);
    BW_CHECK_EQ("the loader holds one mod", (long)bw_mod_loader_count(), 1);
    BW_CHECK_EQ("it is the good one",
                strcmp(bw_mod_loader_name_at(0), "Wasm Mod"), 0);
    BW_CHECK_EQ("the flag.set handler is bound",
                (long)bw_mod_event_handler_count(BW_EVENT_FLAG_SET), 1);
    BW_CHECK_EQ("the menu.open handler is bound",
                (long)bw_mod_event_handler_count(BW_EVENT_MENU_OPEN), 1);

    printf("the events\n");
    BW_CHECK_EQ("the handler refuses the flag it looks for",
                (long)fire_flag(T_FLAG), 1);
    BW_CHECK_EQ("and lets another flag go on",
                (long)fire_flag(T_FLAG + 1u), 0);
    BW_CHECK_EQ("it answers the same the second time",
                (long)fire_flag(T_FLAG), 1);

    memset(&call, 0, sizeof call);
    memset(&payload, 0, sizeof payload);
    BW_CHECK_EQ("a handler that traps lets the action go on",
                (long)bw_mod_event_enter(menu_row->region, menu_row->addr,
                                         &call, &payload), 0);
    BW_CHECK_EQ("and the module still answers after the trap",
                (long)fire_flag(T_FLAG), 1);

    printf("the stop\n");
    bw_mod_loader_stop_all();
    BW_CHECK_EQ("no mod is loaded", (long)bw_mod_loader_count(), 0);
    BW_CHECK_EQ("the stopped module's handler does not run",
                (long)fire_flag(T_FLAG), 0);

    printf("the load again\n");
    BW_CHECK_EQ("the module loads a second time",
                (long)bw_mod_loader_load_all(), 1);
    BW_CHECK_EQ("and refuses again",
                (long)fire_flag(T_FLAG), 1);
    bw_mod_loader_stop_all();

    printf("a module that never grows its memory\n");
    bw_clear_mods();
    BW_REQUIRE("the module is added", bw_add_mod_root(T_NOGROW) == 0);
    BW_CHECK_EQ("it loads: the port's page is added all the same",
                (long)bw_mod_loader_load_all(), 1);
    BW_CHECK_EQ("and its handler gets the copy in that page",
                (long)fire_flag(T_FLAG), 1);
    bw_mod_loader_stop_all();

#if defined(EIDOS_HAVE_WAMR)
    printf("an event inside a handler\n");
    BW_MKDIR(T_NESTED);
    BW_REQUIRE("the nested module is written",
               write_mod(T_NESTED, "Nested", "wasmnested", k_nested,
                         sizeof k_nested) == 0);
    BW_REQUIRE("the test's import is registered",
               wasm_runtime_register_natives("eidostest", k_test_natives, 1));
    bw_clear_mods();
    BW_REQUIRE("the nested module is added", bw_add_mod_root(T_NESTED) == 0);
    BW_CHECK_EQ("it loads", (long)bw_mod_loader_load_all(), 1);
    memset(&call, 0, sizeof call);
    memset(&payload, 0, sizeof payload);
    BW_CHECK_EQ("the inner copy is above the outer one and the outer copy "
                "is intact after the inner call",
                (long)bw_mod_event_enter(menu_row->region, menu_row->addr,
                                         &call, &payload), 1);
    BW_CHECK_EQ("the page is given back: the same again",
                (long)bw_mod_event_enter(menu_row->region, menu_row->addr,
                                         &call, &payload), 1);
    bw_mod_loader_stop_all();
#endif

    clean_tree();
    BW_REPORT("test_mod_wasm");
}
