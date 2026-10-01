/*
 * tests/pxi_doubles.c -- TEST-ONLY doubles for tests/test_pxi.c.
 *
 * Nothing in this file is port code and none of it is linked into the eidos
 * library.  test_pxi is built from its own source list (CMakeLists.txt,
 * "PXI and the ARM7") so these definitions never meet the real ones.
 *
 *  1. A minimal sdk/dsmem.h: main RAM and the I/O register file with write
 *     hooks, which is all sdk/pxi.c and sdk/arm7/ use.  sdk/dsmem.c (the
 *     dsmem lane) replaces it; test_pxi keeps the double so it tests the
 *     PXI protocol and not the address space.
 *
 *  2. The CPU stand-in's half of the contract in sdk/pxi.h, as the os lane
 *     is asked to provide it: OS_RestoreInterrupts and OS_EnableInterrupts
 *     step the ARM7 and, when interrupts end up enabled, deliver pending
 *     ones through the IRQ table the matched OS_SetIrqFunction fills;
 *     SVC_WaitByLoop steps the ARM7.
 *
 *  3. The ARM9 data objects the matched files under test name, and the
 *     spelling aliases and asm veneers their callers need (sdk/aliases.c
 *     and the shims will carry these at integration).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arm7/arm7.h"
#include "dsmem.h"
#include "pxi.h"

/* ------------------------------------------------------------------ */
/* 1. dsmem double                                                     */
/* ------------------------------------------------------------------ */

static u8 *g_main;
static u8 g_io[BW_DS_IO_SIZE];
static u8 g_io_synced[BW_DS_IO_SIZE];

#define MAX_HOOKS 32
static struct { u32 addr, width; BwIoWriteHook hook; } g_hooks[MAX_HOOKS];
static int g_nhooks;

void *bw_ds_try_ptr(u32 addr)
{
    if (addr >= BW_DS_MAIN_BASE && addr - BW_DS_MAIN_BASE < BW_DS_MAIN_SIZE) {
        if (!g_main)
            g_main = (u8 *)calloc(1, BW_DS_MAIN_SIZE);
        return g_main ? g_main + (addr - BW_DS_MAIN_BASE) : NULL;
    }
    if (addr >= BW_DS_IO_BASE && addr - BW_DS_IO_BASE < BW_DS_IO_SIZE)
        return g_io + (addr - BW_DS_IO_BASE);
    return NULL;
}

void *bw_ds_ptr(u32 addr)
{
    void *p = bw_ds_try_ptr(addr);

    if (!p) {
        fprintf(stderr, "test dsmem double: no backing for 0x%08lx\n",
                (unsigned long)addr);
        exit(2);
    }
    return p;
}

volatile u8 *bw_io(u32 addr)
{
    return (volatile u8 *)bw_ds_ptr(addr);
}

static u32 io_value(const u8 *base, u32 addr, u32 width)
{
    const u8 *p = base + (addr - BW_DS_IO_BASE);
    u32 v = 0, i;

    for (i = 0; i < width; i++)
        v |= (u32)p[i] << (8 * i);
    return v;
}

u16 bw_io_read16(u32 addr) { return (u16)io_value(g_io, addr, 2); }
u32 bw_io_read32(u32 addr) { return io_value(g_io, addr, 4); }

static void io_store(u32 addr, u32 v, u32 width)
{
    u32 i, old = io_value(g_io, addr, width);
    int h;

    for (i = 0; i < width; i++)
        g_io[addr - BW_DS_IO_BASE + i] = (u8)(v >> (8 * i));
    for (h = 0; h < g_nhooks; h++)
        if (g_hooks[h].addr == addr)
            g_hooks[h].hook(addr, old, v);
    memcpy(g_io_synced + (addr - BW_DS_IO_BASE), g_io + (addr - BW_DS_IO_BASE),
           width);
}

void bw_io_write16(u32 addr, u16 v) { io_store(addr, v, 2); }
void bw_io_write32(u32 addr, u32 v) { io_store(addr, v, 4); }

void bw_io_hook(u32 addr, u32 width, BwIoWriteHook hook)
{
    int h;

    for (h = 0; h < g_nhooks; h++) {
        if (g_hooks[h].addr == addr) {
            g_hooks[h].width = width;
            g_hooks[h].hook = hook;
            return;
        }
    }
    if (g_nhooks < MAX_HOOKS) {
        g_hooks[g_nhooks].addr = addr;
        g_hooks[g_nhooks].width = width;
        g_hooks[g_nhooks].hook = hook;
        g_nhooks++;
    }
}

void bw_io_sync(void)
{
    int h;

    for (h = 0; h < g_nhooks; h++) {
        u32 a = g_hooks[h].addr, w = g_hooks[h].width;
        u32 old = io_value(g_io_synced, a, w), now = io_value(g_io, a, w);

        if (old != now) {
            memcpy(g_io_synced + (a - BW_DS_IO_BASE), g_io + (a - BW_DS_IO_BASE),
                   w);
            g_hooks[h].hook(a, old, now);
        }
    }
    memcpy(g_io_synced, g_io, sizeof g_io);
}

void bw_ds_reset(void)
{
    if (g_main)
        memset(g_main, 0, BW_DS_MAIN_SIZE);
    memset(g_io, 0, sizeof g_io);
    memset(g_io_synced, 0, sizeof g_io_synced);
    g_nhooks = 0;
}

/* ------------------------------------------------------------------ */
/* 2. CPU stand-in double                                              */
/* ------------------------------------------------------------------ */

/* OS_IRQTable (DTCM 0x02FE0020) and OSi_IrqCallbackInfo, which the matched
 * OS_SetIrqFunction fills.                                                 */
void (*g_02FE0020[32])(void);
struct { void (*func)(void *); unsigned long enable; void *arg; } g_02150F38[12];

static u32 g_cpu_ime = 1;              /* the CPSR I bit, as os_irq.c keeps it */
static u32 g_irq_pending;
static int g_delivering;
int test_irq_delivered;

static void irq_sink(u32 mask)
{
    g_irq_pending |= mask;
}

void test_cpu_reset(void)
{
    g_cpu_ime = 1;
    g_irq_pending = 0;
    g_delivering = 0;
    test_irq_delivered = 0;
    bw_pxi_set_irq_sink(irq_sink);
}

u32 test_irq_pending(void)
{
    return g_irq_pending;
}

static void deliver(void)
{
    int bit;

    if (g_delivering || !g_cpu_ime || !(bw_io_read16(0x04000208u) & 1u))
        return;
    g_delivering = 1;
    g_cpu_ime = 0;                      /* the IRQ handler runs with I set  */
    for (bit = 0; bit < 32; bit++) {
        u32 m = 1u << bit;

        if (!(g_irq_pending & bw_io_read32(0x04000210u) & m))
            continue;
        g_irq_pending &= ~m;
        if (g_02FE0020[bit]) {
            test_irq_delivered++;
            g_02FE0020[bit]();
        }
    }
    g_cpu_ime = 1;
    g_delivering = 0;
}

u32 OS_DisableInterrupts(void)
{
    u32 was = g_cpu_ime;

    g_cpu_ime = 0;
    return was;
}

u32 OS_RestoreInterrupts(u32 state)
{
    u32 was = g_cpu_ime;

    g_cpu_ime = state;
    bw_arm7_step();
    deliver();
    return was;
}

u32 OS_EnableInterrupts(void)
{
    return OS_RestoreInterrupts(1);
}

void OS_Terminate(void)
{
    fprintf(stderr, "test: OS_Terminate reached\n");
    exit(3);
}

/* SVC_WaitByLoop, an ARM9 BIOS call (asm in aletheia).                     */
void func_02004490(int count)
{
    (void)count;
    bw_arm7_step();
}

void SVC_WaitByLoop(int count)
{
    func_02004490(count);
}

/* CPSR readers (asm in aletheia); PMi_WaitBusy asks only when its policy
 * word g_020A8724 says so, which it does not here.                         */
int OS_GetProcMode(void) { return 0x1F; }
int OS_GetCpsrIrq(void) { return 0; }

/* ------------------------------------------------------------------ */
/* 3. Data objects, aliases and veneers                                */
/* ------------------------------------------------------------------ */

/* Storage for game objects; sized for either pointer width.                */
typedef union { u8 b[256]; void *p; u32 w; } TestObj;

TestObj g_02150FC8;                    /* os_pxi.o: reset/terminate flags  */
u16 OSi_IsInitReset;
char SDK_AUTOLOAD_DTCM_START[0x4000];
TestObj g_02153000;                    /* pm.o .bss                        */
int g_020A8724;                        /* PMi_WaitBusy's policy            */
TestObj PMi_BatteryLowCallbackInfo;
TestObj g_02151428;                    /* external.o (RTC) .bss            */
TestObj cardi_common[8];               /* CARDiCommon, 0x560+ bytes        */

void PMi_ProceedToExit(int factor) { (void)factor; }
unsigned long RTCEXi_CommonCallback(void) { return 4; }
int OS_IsRunOnTwl(void) { return 0; }
void CTRDGi_InitCommon(void) { }

void *test_wakeup_thread;
int test_wakeups;
void OS_WakeupThreadDirect(void *thread)
{
    test_wakeup_thread = thread;
    test_wakeups++;
}

extern void func_02088498(void);
void PXI_Init(void) { func_02088498(); }

extern BOOL func_020885EC(int fifotag, int proc);
BOOL PXI_IsCallbackReady(int fifotag, int proc)
{
    return func_020885EC(fifotag, proc);
}

extern int PXI_SendWordByFifo(int fifotag, unsigned long data, BOOL err);
int func_02088610(int fifotag, unsigned long data, BOOL err)
{
    return PXI_SendWordByFifo(fifotag, data, err);
}

extern void func_02084A38(int tag, unsigned long data, BOOL err);
void OSi_CommonCallback(int tag, unsigned long data, BOOL err)
{
    func_02084A38(tag, data, err);
}

extern void func_02084AB0(int type, volatile u8 *p);
void OSi_SyncWithOtherProc(int type, volatile u8 *p)
{
    func_02084AB0(type, p);
}

#if BW_TEST_PM_MATCHED
extern void func_0208B950(int tag, unsigned long data, BOOL err);
void PMi_CommonCallback(int tag, unsigned long data, BOOL err)
{
    func_0208B950(tag, data, err);
}
#endif

extern void func_0208B840(unsigned int result, unsigned int *arg);
void PMi_DummyCallback(unsigned int result, unsigned int *arg)
{
    func_0208B840(result, arg);
}

extern BOOL func_02088F2C(unsigned long command);
int RtcSendPxiCommand(unsigned long command)
{
    return func_02088F2C(command);
}

/* RTCi_ReadRawDateTimeAsync is a three-instruction asm veneer in aletheia:
 * `mov r0, #0x10` and a tail call of RtcSendPxiCommand.                    */
BOOL RTCi_ReadRawDateTimeAsync(void)
{
    return func_02088F2C(0x10);
}

extern void func_02091150(unsigned long data);
void CTRDGi_SendtoPxi(unsigned long data) { func_02091150(data); }

extern void func_020911D4(void);
void CTRDGi_DummyCallback(int tag, unsigned long data, BOOL err)
{
    (void)tag;
    (void)data;
    (void)err;
    func_020911D4();
}

/* Addresses of the objects above, for the test's checks.                   */
void *test_cardi_common(void) { return cardi_common; }
void *test_os_pxi_bss(void) { return &g_02150FC8; }
void *test_pm_bss(void) { return &g_02153000; }
