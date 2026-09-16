/*
 * sdk/game_globals.c -- the ARM9 global objects the matched code reaches
 * by name, defined for the host.
 *
 * These are not SDK functions.  They are the *data* half of the shim: objects
 * that live at fixed addresses in the DS image and that some not-yet-matched
 * start-up code initialises there.  Until that start-up code is decompiled,
 * the port creates each object itself and documents what fills it.
 *
 * `python tools/closure.py --seed ...` lists these under the `data`
 * class; every name in that list must appear here or the link fails, which is
 * exactly the agreement between the closure script and the linker the port is
 * built around.
 *
 * Two of the definitions below deliberately differ from the matched file's
 * own declaration, and both differences are findings, not conveniences:
 *
 *  1. `g_02146270` (src/matched/unit_02019C54.c) is declared there as
 *
 *         struct Memo { u16 b; u16 a; u8 obj[0x10]; };
 *
 *     but the write into `obj` is 0x3C bytes wide: src/matched/unit_0201ADA4.c
 *     calls func_020495F4(handle, 0x3C, dest) with that same buffer, and 0x3C
 *     is the personal record's size.  On the DS the under-declaration is
 *     harmless, because the real object at 0x02146274 is a plain global with
 *     the rest of the record after it and nothing in unit_02019C54.c indexes
 *     past +0x10.  On the host the object has to be big enough or the write
 *     runs off it, so it is defined here at the true size.  The matched file
 *     is left exactly as it is; it still produces the right DS bytes.
 *
 *  2. `func_02019B04_Table` (src/matched/unit_02019B04.c) is declared there
 *     `const s8 [][5]`.  The port cannot define it const, because its
 *     contents are game data that has to be copied in at run time out of the
 *     player's own extracted ARM9 image.  A definition whose qualifiers
 *     differ from another translation unit's declaration is outside what C99
 *     guarantees; it links and behaves correctly on every toolchain this port
 *     targets, and the alternative (editing the matched file) is forbidden.
 *     Recorded in README.md under "Known type mismatches".
 */

#include <string.h>

#include "port.h"

/* ------------------------------------------------------------------ */
/* g_Sink -- the assertion sink src/matched/sub_02034734.c dispatches to */
/* ------------------------------------------------------------------ */

typedef void (*BwAssertHandler)(void);

struct BwSink {
    u32 unk0;
    u32 unk4;
    u32 unk8;
    BwAssertHandler handler;
};

static void sink_handler(void)
{
    bw_panic("game assertion", 0x02034734, "sub_02034734 assert sink fired");
}

struct BwSink g_Sink = { 0, 0, 0, sink_handler };

/* ------------------------------------------------------------------ */
/* The __FILE__ / message strings the archive readers pass to the sink  */
/* ------------------------------------------------------------------ */

/*
 * On the DS these are pointers into the packed string area (0x020A7EE0 and
 * 0x020A7F18 for func_02049578, 0x020A78xx for func_0204944C), and the
 * matched files declare them as externs pinned by address because a
 * `__FILE__` literal has no attestable witness of its own
 * (docs/idioms/file-string-is-an-abs32-into-the-string-area.md).  Their
 * contents never affect behaviour: they are arguments to an assert that only
 * fires on an out-of-range member.  The port supplies its own text, which is
 * why no ROM string is reproduced here.
 */
const char func_02049578_File[] = "arc_tool.c";
const char func_02049578_Msg[]  = "member index out of range";
const char func_0204944C_File[] = "arc_tool.c";
const char func_0204944C_Msg[]  = "member index out of range";

/* ------------------------------------------------------------------ */
/* g_02146270 -- the one-entry personal-record memo                     */
/* ------------------------------------------------------------------ */

#define BW_PERSONAL_RECORD_SIZE 0x3C

struct BwMemo {
    u16 b;
    u16 a;
    u8  obj[BW_PERSONAL_RECORD_SIZE + 4];   /* see note 1 above */
};

struct BwMemo g_02146270;

/* ------------------------------------------------------------------ */
/* g_0214652C -- the personal-data archive handle                       */
/* ------------------------------------------------------------------ */

static BwArcFile g_personal_arc;

void *g_0214652C;

int bw_open_personal_archive(void)
{
    if (g_0214652C != NULL) {
        return 0;
    }
    if (bw_arc_open(&g_personal_arc, "a/0/1/6") != 0) {
        return -1;
    }
    g_0214652C = &g_personal_arc;

    /*
     * The memo caches by (a, b) and starts zeroed, which would read as
     * "species 0, form 0 already loaded".  Poison the key so the first real
     * call always misses and goes to the archive.
     */
    g_02146270.a = 0xFFFF;
    g_02146270.b = 0xFFFF;
    memset(g_02146270.obj, 0, sizeof(g_02146270.obj));
    return 0;
}

void bw_close_personal_archive(void)
{
    if (g_0214652C == NULL) {
        return;
    }
    bw_arc_close(&g_personal_arc);
    g_0214652C = NULL;
}

/* ------------------------------------------------------------------ */
/* func_02019B04_Table -- the nature multiplier table                   */
/* ------------------------------------------------------------------ */

/*
 * 25 natures by 5 stats of { -1, 0, +1 }, at ARM9 0x0209E324.  The index is
 * [nature][n - 1] with n as src/matched/sub_02018A7C.c passes it: 1 attack,
 * 2 defence, 3 sp. attack, 4 sp. defence, 5 speed.  Loaded from the player's
 * own extract/arm9_dec.bin; never stored in this repository.
 */
#define BW_NATURE_TABLE_ADDRESS 0x0209E324u
#define BW_NATURE_COUNT         25
#define BW_NATURE_STATS         5

s8 func_02019B04_Table[BW_NATURE_COUNT][BW_NATURE_STATS];

int bw_load_nature_table(void)
{
    const void *src = bw_arm9_rodata(BW_NATURE_TABLE_ADDRESS,
                                     BW_NATURE_COUNT * BW_NATURE_STATS);

    if (src == NULL) {
        return -1;
    }
    memcpy(func_02019B04_Table, src, BW_NATURE_COUNT * BW_NATURE_STATS);
    return 0;
}

/* ------------------------------------------------------------------ */
/* g_EnvModule -- the heap manager at 0x02146954                        */
/* ------------------------------------------------------------------ */

/*
 * The struct declarations below are copied field for field from
 * src/matched/sub_02030098.c, which is the widest view of the object any
 * matched file in the closure takes (unit_0202FBAC.c declares the same
 * object with only the first two members, and the two declarations agree on
 * every offset they share).  Copying the shape rather than inventing one is
 * what keeps the port and the matched code agreeing about where `count`,
 * `error` and the per-record live-allocation counter are on an LP64 host,
 * where the two leading pointers are eight bytes each and the DS's offsets
 * do not apply to either side.
 *
 * The DS constructor at 0x0202FC48 is not decompiled ("not in this packet's
 * cheap yield", unit_0202FBAC.c's header), so nothing matched can build this
 * object and the port builds it: the fields the matched code reads, filled
 * with values that make every heap id valid.
 *
 * The arena handle is the one thing the port does not reproduce.  On the DS
 * it is an NNS expanded-heap handle and the raw allocator hands it to
 * sub_02060CA0, which walks the heap's own free blocks.  Here it is a
 * distinct non-null token per heap: stubs/reimpl_02060A08.c and
 * stubs/reimpl_02060ABC.c, the two walks src/matched/func_02060CA0.c
 * dispatches to, take memory from the host instead of from an arena, and the
 * handle only needs to be the "this heap exists" answer that sub_0202FBAC
 * returns and sub_02030098 tests.  Building a real one needs the heap
 * environment constructor at 0x0202FC48, which is still open.
 */

struct BwEnvEntry {
    void *ptr00;
    u32   field04;
    u32   field08;
    u16   field0c;   /* live allocations; sub_02030098 increments it */
    u8    field0e;
};

struct BwEnvModule {
    struct BwEnvEntry *entries;
    u8                *ids;
    u16                count;
    u16                field0a;
    u16                field0c;
    u16                error;
};

struct BwEnvModule g_EnvModule;

static struct BwEnvEntry g_heap_entries[BW_HEAP_COUNT];
static u8                g_heap_ids[BW_HEAP_COUNT];
static u32               g_heap_arena[BW_HEAP_COUNT];

int bw_heap_init(u32 heaps)
{
    u32 i;

    if (heaps == 0u || heaps > BW_HEAP_COUNT) {
        heaps = BW_HEAP_COUNT;
    }
    memset(g_heap_entries, 0, sizeof(g_heap_entries));
    for (i = 0; i < BW_HEAP_COUNT; i++) {
        /*
         * 0xff is the manager's "no such resource" marker: sub_0202FBAC
         * returns 0 for it, which is the raw allocator's result code 2.
         * Ids past `heaps` keep it so a test can see that arm.
         */
        g_heap_ids[i] = i < heaps ? (u8)i : (u8)0xff;
        g_heap_arena[i] = 0x48454150u + i;          /* 'HEAP' + index */
        g_heap_entries[i].ptr00 = &g_heap_arena[i];
    }
    g_EnvModule.entries = g_heap_entries;
    g_EnvModule.ids = g_heap_ids;
    g_EnvModule.count = (u16)heaps;
    g_EnvModule.field0a = 0;
    g_EnvModule.field0c = 0;
    g_EnvModule.error = 0;
    return 0;
}

long bw_heap_live_blocks(u32 heap_id)
{
    heap_id &= 0x7FFFu;
    if (g_EnvModule.ids == NULL || heap_id >= (u32)g_EnvModule.count) {
        return -1;
    }
    return (long)g_heap_entries[g_EnvModule.ids[heap_id]].field0c;
}

void bw_heap_note_release(u32 heap_id)
{
    u8 idx;

    heap_id &= 0x7FFFu;
    if (g_EnvModule.ids == NULL || heap_id >= (u32)g_EnvModule.count) {
        return;
    }
    idx = g_EnvModule.ids[heap_id];
    if (idx != 0xffu && g_heap_entries[idx].field0c != 0) {
        g_heap_entries[idx].field0c--;
    }
}

/* ------------------------------------------------------------------ */
/* g_020A7820 -- the watched-heap id                                    */
/* ------------------------------------------------------------------ */

/*
 * src/matched/sub_02030A00.c gates its whole body on this: a signed watch id
 * where any negative value means "watch nothing", and a non-negative one
 * names the single heap whose allocations are handed to the provenance audit
 * sub_02030984.
 *
 * It is a data word inside the ARM9 image (0x020A7820 is below the image end
 * at 0x020A9E80, so it is in .data and not in BSS), which is why it is
 * loaded out of the player's own extract/arm9_dec.bin at run time rather
 * than spelled out here, exactly like the nature table above.  The value the
 * retail image carries is the "watch nothing" one; a port that failed to
 * load the image would otherwise default to heap 0 and run the audit on
 * every allocation, so the definition starts at -1 and the load only ever
 * replaces it.
 */
#define BW_WATCH_ID_ADDRESS 0x020A7820u

s32 g_020A7820 = -1;

int bw_load_watch_id(void)
{
    const void *src = bw_arm9_rodata(BW_WATCH_ID_ADDRESS, 4);

    if (src == NULL) {
        return -1;
    }
    g_020A7820 = (s32)bw_rd32((const u8 *)src);
    return 0;
}

/* ------------------------------------------------------------------ */
/* sub_0203101C_File -- tcbl.c's allocation-site anchor                 */
/* ------------------------------------------------------------------ */

/*
 * src/matched/sub_0203101C.c passes this as the fourth argument of every
 * allocation it makes, and sub_02030880 stamps the first 0x12 bytes of it
 * into the block header's provenance field.  It is not a __FILE__ literal:
 * the addresses these sites pass are ARM9 data words, and aletheia's
 * layout/file_anchors.txt names 0x020A7834 as the anchor of tcbl.c (the two
 * rows for 0x020310C0 and 0x02031284).  So the text is a file name that
 * aletheia's own metadata attests, and the port supplies it as text rather
 * than copying bytes out of the image: the value reaches nothing but a
 * provenance string that the release build never reads back.
 */
const char sub_0203101C_File[] = "tcbl.c";

/*
 * src/matched/sub_020311B8.c names the same word 0x020A7834 under its own
 * file-local spelling, which is what a matched source does with an anchor it
 * loads out of its own pool: the two rows layout/file_anchors.txt records for
 * tcbl.c are 0x020310C0 (the constructor's) and 0x02031284 (the registrar's),
 * one address.  The DS build resolves both names to it; a host linker sees
 * two, so the registrar gets its own definition of the same string rather
 * than a forwarder, because a `const char []` cannot be aliased the way
 * sdk/aliases.c aliases a function.
 */
const char sub_020311B8_File[] = "tcbl.c";
