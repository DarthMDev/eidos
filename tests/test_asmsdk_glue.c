/*
 * tests/test_asmsdk_glue.c -- the BW_ASMSDK_BOOT_SOURCES half of sdk/asm/:
 * the asm forwarders that call matched game code, the arena bounds, and the
 * TWL-only stand-ins.
 *
 * Those files exist to link with the boot closure, which this test does not
 * build.  It links them alone, with recording doubles for every callee and
 * global they name, and checks what each passes on and returns: the
 * argument order, the constant the asm loads into a free register, the
 * value it hands back.  It does not link the eidos library, so the two
 * address-space calls the arena files make and bw_panic are doubled here as
 * well: a flat main RAM block, and a panic that returns to the test through
 * longjmp so each TWL stand-in can be shown to panic with its target.
 */

#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port.h"
#include "dsmem.h"
#include "asm/asmsdk.h"
#include "port_test.h"

/* ------------------------------------------------------------------ */
/* Doubles                                                             */
/* ------------------------------------------------------------------ */

static u32 g_calls;
static const char *g_last;
static uintptr_t g_arg[3];

static void record(const char *name, uintptr_t a, uintptr_t b, uintptr_t c)
{
    g_calls++;
    g_last = name;
    g_arg[0] = a;
    g_arg[1] = b;
    g_arg[2] = c;
}

s32 func_02084D78(u16 lockID);
s32 func_02084D78(u16 lockID)
{
    record("func_02084D78", lockID, 0, 0);
    return -77;
}

static u32 g_f90_a;
static u32 g_f90_b;

void func_02005F90(u32 a, u32 b);
void func_02005F90(u32 a, u32 b)
{
    g_f90_a = a;
    g_f90_b = b;
}

static u8 g_d34_result[4];

void *func_02005D34(u32 a0, u32 a1);
void *func_02005D34(u32 a0, u32 a1)
{
    record("func_02005D34", a0, a1, 0);
    return g_d34_result;
}

BOOL func_02078B44(void *file, s32 offset, u32 origin);
BOOL func_02078B44(void *file, s32 offset, u32 origin)
{
    record("func_02078B44", (uintptr_t)file, (u32)offset, origin);
    return 1;
}

static BOOL g_reserve_result;

BOOL sub_02030534(u32 size);
BOOL sub_02030534(u32 size)
{
    record("sub_02030534", size, 0, 0);
    return g_reserve_result;
}

static u32 g_terminated;

void OS_Terminate(void);
void OS_Terminate(void)
{
    g_terminated++;
}

static u8 g_alloc_result[4];

void *func_02031354(int a0);
void *func_02031354(int a0)
{
    record("func_02031354", (u32)a0, 0, 0);
    return g_alloc_result;
}

void *g_02146990;
void *g_021469BC;
u8 g_0215301C[4];
u8 g_02152F40[12 * 8];

void sub_0203515C(void *p, u8 mask);
void sub_0203515C(void *p, u8 mask)
{
    record("sub_0203515C", (uintptr_t)p, mask, 0);
}

u32 sub_020351BC(void *p, u32 mask);
u32 sub_020351BC(void *p, u32 mask)
{
    record("sub_020351BC", (uintptr_t)p, mask, 0);
    return 1;
}

static int g_order;
static int g_close_order;
static int g_free_order;

BOOL FS_CloseFile(void *file);
BOOL FS_CloseFile(void *file)
{
    record("FS_CloseFile", (uintptr_t)file, 0, 0);
    g_close_order = ++g_order;
    return 1;
}

void sub_020307B0(void *p);
void sub_020307B0(void *p)
{
    record("sub_020307B0", (uintptr_t)p, 0, 0);
    g_free_order = ++g_order;
}

void func_0208C788(void *listp, void *info);
void func_0208C788(void *listp, void *info)
{
    record("func_0208C788", (uintptr_t)listp, (uintptr_t)info, 0);
}

static int g_twl;
static u32 g_console;

int OS_IsRunOnTwl(void);
int OS_IsRunOnTwl(void)
{
    return g_twl;
}

u32 OS_GetConsoleType(void);
u32 OS_GetConsoleType(void)
{
    return g_console;
}

/* Main RAM only: the arena files read one flag there and translate their
 * results; everything else is outside what this double backs. */
static u8 g_main[0x01000000];

void *bw_ds_try_ptr(u32 addr)
{
    if (addr >= BW_DS_MAIN_BASE && addr - BW_DS_MAIN_BASE < BW_DS_MAIN_SIZE) {
        return g_main + (addr - BW_DS_MAIN_BASE);
    }
    return NULL;
}

void *bw_ds_ptr(u32 addr)
{
    void *p = bw_ds_try_ptr(addr);

    if (p == NULL) {
        bw_panic("bw_ds_ptr", addr, "outside the test's main RAM");
    }
    return p;
}

static jmp_buf g_panic_jmp;
static int g_panic_armed;
static u32 g_panic_addr;

void bw_panic(const char *what, u32 address, const char *detail)
{
    g_panic_addr = address;
    if (g_panic_armed) {
        g_panic_armed = 0;
        longjmp(g_panic_jmp, 1);
    }
    printf("unexpected panic: %s at 0x%08X: %s\n", what, (unsigned)address, detail);
    exit(1);
}

/* ------------------------------------------------------------------ */
/* Checks                                                              */
/* ------------------------------------------------------------------ */

static void test_forwarders(void)
{
    u8 handle[8];
    u8 obj[0x40];

    g_calls = 0;
    BW_CHECK_EQ("OS_UnLockCartridge returns the callee's value",
                OS_UnLockCartridge(0x55), -77);
    BW_CHECK_EQ("... with the id", g_arg[0], 0x55);
    BW_CHECK_EQ("func_02084D98 spelling", func_02084D98(0x60), -77);

    BW_CHECK_EQ("ArrayDeleteAt returns func_02005D34's result",
                ArrayDeleteAt(0x1234, 7) == (void *)g_d34_result, 1);
    BW_CHECK_EQ("... first call a", g_f90_a, 0x1234);
    BW_CHECK_EQ("... first call b", g_f90_b, 7);
    BW_CHECK_EQ("... second call a", g_arg[0], 0x1234);
    BW_CHECK_EQ("... second call b", g_arg[1], 7);
    BW_CHECK_EQ("sub_02005D90 spelling",
                sub_02005D90(1, 2) == (void *)g_d34_result, 1);

    BW_CHECK_EQ("NHTTPi_GetConnection returns the seek's result",
                NHTTPi_GetConnection(handle, 0x400), 1);
    BW_CHECK_EQ("... the file", g_arg[0] == (uintptr_t)handle, 1);
    BW_CHECK_EQ("... the position", g_arg[1], 0x400);
    BW_CHECK_EQ("... origin FS_SEEK_SET", g_arg[2], 0);
    sub_0204960C(handle, 8);
    BW_CHECK_EQ("sub_0204960C spelling", g_arg[1], 8);

    g_reserve_result = 1;
    g_terminated = 0;
    BW_CHECK_EQ("func_02030BA4 returns the reserve's result", func_02030BA4(0x800), 1);
    BW_CHECK_EQ("... with the size", g_arg[0], 0x800);
    BW_CHECK_EQ("... and does not terminate", g_terminated, 0);
    g_reserve_result = 0;
    func_02030BA4(0x900);
    BW_CHECK_EQ("a failed reserve terminates", g_terminated, 1);

    g_02146990 = NULL;
    func_0203129C(3);
    BW_CHECK_EQ("func_0203129C passes its argument", g_arg[0], 3);
    BW_CHECK_EQ("... and stores the result",
                g_02146990 == (void *)g_alloc_result, 1);

    memset(obj, 0, sizeof obj);
    g_021469BC = obj;
    func_02034F84(0x12);
    BW_CHECK_EQ("func_02034F84 ORs into the global's object", obj[0x38], 0x12);
    func_0203516C(0x104);
    BW_CHECK_EQ("func_0203516C passes the object", g_arg[0] == (uintptr_t)obj, 1);
    BW_CHECK_EQ("... and the mask's low byte", g_arg[1], 0x04);
    BW_CHECK_EQ("func_020351D0 returns the test", func_020351D0(0x20), 1);
    BW_CHECK_EQ("... on the object", g_arg[0] == (uintptr_t)obj, 1);
    BW_CHECK_EQ("... with the mask", g_arg[1], 0x20);

    g_order = 0;
    func_02049238(handle);
    BW_CHECK_EQ("func_02049238 closes first", g_close_order, 1);
    BW_CHECK_EQ("... then frees", g_free_order, 2);
    BW_CHECK_EQ("... the same handle", g_arg[0] == (uintptr_t)handle, 1);

    func_0208C86C(handle);
    BW_CHECK_EQ("func_0208C86C passes the list head",
                g_arg[0] == (uintptr_t)g_0215301C, 1);
    BW_CHECK_EQ("... and the record", g_arg[1] == (uintptr_t)handle, 1);
}

static void test_alarm_id(void)
{
    memset(g_02152F40, 0, sizeof g_02152F40);
    g_02152F40[2 * 12 + 8] = 0x10;
    BW_CHECK_EQ("SNDi_IncAlarmId returns the new id", SNDi_IncAlarmId(2), 0x11);
    BW_CHECK_EQ("... and stores it at +8 of entry 2", g_02152F40[2 * 12 + 8], 0x11);
    BW_CHECK_EQ("... nothing else", g_02152F40[2 * 12 + 7] | g_02152F40[2 * 12 + 9], 0);
    g_02152F40[5 * 12 + 8] = 0xFF;
    BW_CHECK_EQ("0xFF increments to 0x100 in r0", SNDi_IncAlarmId(5), 0x100);
    BW_CHECK_EQ("... and wraps the byte", g_02152F40[5 * 12 + 8], 0);
}

static void set_mainex(u32 v)
{
    memcpy(bw_ds_ptr(0x02151268u), &v, 4);
}

static void test_arena(void)
{
    g_twl = 0;
    g_console = 0x80000001u;   /* NITRO, retail size type 1 */
    set_mainex(0);

    BW_CHECK_EQ("lo MAIN, NITRO", bw_os_init_arena_lo_addr(0), 0x02213D20u);
    BW_CHECK_EQ("hi MAIN, NITRO", bw_os_init_arena_hi_addr(0), 0x023E0000u);
    BW_CHECK_EQ("lo MAINEX_PRIV", bw_os_init_arena_lo_addr(1), 0);
    BW_CHECK_EQ("hi MAINEX_PRIV", bw_os_init_arena_hi_addr(1), 0);
    BW_CHECK_EQ("lo MAINEX, disabled", bw_os_init_arena_lo_addr(2), 0);
    BW_CHECK_EQ("hi MAINEX, disabled", bw_os_init_arena_hi_addr(2), 0);
    BW_CHECK_EQ("lo ITCM", bw_os_init_arena_lo_addr(3), 0x01FF8820u);
    BW_CHECK_EQ("hi ITCM", bw_os_init_arena_hi_addr(3), 0x02000000u);
    BW_CHECK_EQ("lo DTCM", bw_os_init_arena_lo_addr(4), 0x02FE00C0u);
    BW_CHECK_EQ("hi DTCM (system stack floor)", bw_os_init_arena_hi_addr(4), 0x02FE1F80u);
    BW_CHECK_EQ("lo SHARED", bw_os_init_arena_lo_addr(5), 0x02FFF000u);
    BW_CHECK_EQ("hi SHARED", bw_os_init_arena_hi_addr(5), 0x02FFF680u);
    BW_CHECK_EQ("lo WRAM", bw_os_init_arena_lo_addr(6), 0x03040000u);
    BW_CHECK_EQ("hi WRAM", bw_os_init_arena_hi_addr(6), 0x03040000u);
    BW_CHECK_EQ("lo id 7", bw_os_init_arena_lo_addr(7), 0);
    BW_CHECK_EQ("hi id -1 (unsigned compare)", bw_os_init_arena_hi_addr(0xFFFFFFFFu), 0);

    set_mainex(1);
    BW_CHECK_EQ("lo MAINEX, NITRO size 1", bw_os_init_arena_lo_addr(2), 0);
    g_console = 0x80000002u;
    BW_CHECK_EQ("lo MAINEX, NITRO 8 MB", bw_os_init_arena_lo_addr(2), 0x02400060u);
    BW_CHECK_EQ("hi MAINEX, NITRO 8 MB", bw_os_init_arena_hi_addr(2), 0x02700000u);
    g_twl = 1;
    g_console = 0x01800004u;
    BW_CHECK_EQ("lo MAINEX, TWL type 4", bw_os_init_arena_lo_addr(2), 0);
    g_console = 0x01800002u;
    BW_CHECK_EQ("lo MAINEX, TWL", bw_os_init_arena_lo_addr(2), 0x0D000000u);
    BW_CHECK_EQ("hi MAINEX, TWL", bw_os_init_arena_hi_addr(2), 0x0DF00000u);
    BW_CHECK_EQ("lo MAIN, TWL", bw_os_init_arena_lo_addr(0), 0x02735B40u);
    BW_CHECK_EQ("hi MAIN, TWL", bw_os_init_arena_hi_addr(0), 0x02F80000u);

    /* The SDK names hand back host addresses through the address space. */
    g_twl = 0;
    set_mainex(0);
    BW_CHECK_EQ("OS_GetInitArenaLo MAIN translated",
                OS_GetInitArenaLo(0) == (void *)(g_main + 0x213D20), 1);
    BW_CHECK_EQ("OS_GetInitArenaHi SHARED translated",
                func_02086594(5) == (void *)(g_main + 0xFFF680), 1);
    BW_CHECK_EQ("ITCM is not backed: NULL", func_02086694(3) == NULL, 1);
    BW_CHECK_EQ("a 0 bound is NULL", OS_GetInitArenaHi(1) == NULL, 1);
}

#define EXPECT_PANIC(label, call, addr)                                     \
    do {                                                                    \
        g_panic_addr = 0;                                                   \
        g_panic_armed = 1;                                                  \
        if (setjmp(g_panic_jmp) == 0) {                                     \
            call;                                                           \
            g_panic_armed = 0;                                              \
        }                                                                   \
        BW_CHECK_EQ(label, g_panic_addr, (addr));                           \
    } while (0)

static void test_twl_stand_ins(void)
{
    EXPECT_PANIC("func_020053E4 panics with its target", func_020053E4(NULL), 0x0273154Cu);
    EXPECT_PANIC("func_02005420", (void)func_02005420(), 0x027315D0u);
    EXPECT_PANIC("func_02005428", (void)func_02005428(), 0x027314F0u);
    EXPECT_PANIC("func_02005454", func_02005454(0), 0x027315C0u);
    EXPECT_PANIC("func_02005B6C", func_02005B6C(), 0x02704364u);
    EXPECT_PANIC("OSi_InitArenaEx_ltdmain", OSi_InitArenaEx_ltdmain(), 0x027312B4u);
    EXPECT_PANIC("OSi_SearchTitleIdFromList",
                 (void)OSi_SearchTitleIdFromList(1, 2), 0x0273131Cu);
    EXPECT_PANIC("PMi_PreSleepForNdma", PMi_PreSleepForNdma(0), 0x027313E8u);
}

int main(void)
{
    test_forwarders();
    test_alarm_id();
    test_arena();
    test_twl_stand_ins();
    BW_REPORT("test_asmsdk_glue");
}
