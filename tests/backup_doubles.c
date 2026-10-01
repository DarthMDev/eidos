/*
 * tests/backup_doubles.c -- what tests/test_backup.c's matched files name
 * that the eidos library does not define yet.
 *
 * test_backup runs on the port's real address space, OS layer and PXI
 * (bw_port_reset, os_test_boot).  What is here is not port code: the ARM9
 * data objects the matched CARD, NVRAM, MIC and WM files under test name,
 * the cache and DMA no-ops they call (the host has no cache), the two
 * asm-bodied getters the files reach (CARD_GetBackupSectorSize,
 * NVRAMi_IsInitialized), the BIOS-style waits (OS_SpinWaitSysCycles,
 * SVC_WaitByLoop: the ARM9 waiting for the ARM7, so the ARM7 gets its turn,
 * sdk/pxi.h), and stubs for what WM_Init would reach only after the ARM7
 * had answered on the wireless tag, which it never does (each one fails the
 * test if called).  The boot's own closure brings the real definitions;
 * when the library has one, the copy here goes.
 *
 * The objects are host objects, not the DS's: the test binds the ARM7 to
 * them with bw_arm7_fs_bind and bw_arm7_nvram_bind, as a port would bind
 * objects that are not at their DS addresses.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arm7/arm7.h"
#include "types.h"

/* Storage for game objects; sized for either pointer width.                */
typedef union { u8 b[2048]; void *p; u32 w; u64 d; } TestObj;

/* ------------------------------------------------------------------ */
/* The ARM9 waiting for the ARM7                                       */
/* ------------------------------------------------------------------ */

void OS_SpinWaitSysCycles(u32 cycles)
{
    (void)cycles;
    bw_arm7_step();
}

void SVC_WaitByLoop(int count)
{
    (void)count;
    bw_arm7_step();
}

void func_02004490(int count)
{
    SVC_WaitByLoop(count);
}

/* ------------------------------------------------------------------ */
/* Cache and DMA: no cache on the host                                 */
/* ------------------------------------------------------------------ */

void DC_FlushRange(const void *start, u32 size) { (void)start; (void)size; }
void DC_InvalidateRange(void *start, u32 size) { (void)start; (void)size; }
void DC_StoreRange(void *start, u32 size) { (void)start; (void)size; }
void DC_WaitWriteBufferEmpty(void) { }

void MIi_CpuClearFast(u32 data, void *dest, u32 size)
{
    u32 i;

    for (i = 0; i + 4 <= size; i += 4)
        memcpy((u8 *)dest + i, &data, 4);
}

/* ------------------------------------------------------------------ */
/* Console and module state                                            */
/* ------------------------------------------------------------------ */

int OS_IsRunOnTwl(void) { return 0; }
void OSi_ReferSymbol(const void *symbol) { (void)symbol; }
const char cardi_backup_assert[22] = "CARD-backup";
void CARD_CheckEnabled(void) { }
u32 CARDi_GetAccessLevel(void) { return 3; }      /* read and write       */

/* libcard's state: cardi_common (CARDiCommon, 0x520 bytes at 32-bit
 * pointers), the command block cardi_arg and the bounce buffer.            */
TestObj cardi_common;
union { u8 b[0x60]; u64 align; } cardi_arg;
u8 CARDi_backup_cache_page_buf[256];

/* CARD_GetBackupSectorSize is asm in aletheia: cardi_common.cmd->spec.
 * sect_size, the word at +0x1C of the command block.                       */
u32 CARD_GetBackupSectorSize(void)
{
    const u8 *cmd = *(u8 *const *)cardi_common.b;
    u32 v;

    memcpy(&v, cmd + 0x1C, 4);
    return v;
}

/* nvram.o's .bss: nvramInitialized at 0x02150EA0, nvramTemp at +0x20,
 * nvramw at +0x40.  NVRAMi_WriteCallback also reaches nvramTemp as a member
 * of the section base (`g_02150EA0.nvramTemp`); its CMake entry spells that
 * name as `(*bw_test_nvram_base)`, which test_backup points 0x20 bytes
 * before nvramTemp, the DS layout, before any NVRAM code runs.  Nothing
 * else in the section is reached through the base.                         */
u32 nvramInitialized;
u32 nvramTemp[8];
TestObj nvramw;
void *bw_test_nvram_base;

void bw_test_doubles_init(void)
{
    bw_test_nvram_base = (void *)((uintptr_t)nvramTemp - 0x20u);
}

/* NVRAMi_IsInitialized is asm in aletheia: nvramInitialized == 2.          */
int NVRAMi_IsInitialized(void)
{
    return nvramInitialized == 2;
}

/* mic.o's .bss (micInitialized, then micWork), wm_system.o's.              */
TestObj g_02152FE0;
TestObj wmSystemBss;
TestObj bufMsgQ;
TestObj bufMsg;
TestObj fifoBuf;

/* ------------------------------------------------------------------ */
/* What WM_Init reaches only past the ARM7 check                       */
/* ------------------------------------------------------------------ */

static void unreachable(const char *what)
{
    printf("  FAIL WM_Init reached %s past the ARM7 check\n", what);
    exit(1);
}

int  NWM_CheckInitialized(void) { unreachable("NWM_CheckInitialized"); return 0; }
int  OS_IsOnVram(void *p) { (void)p; return 0; }
int  OS_IsOnExtendedMainMemory(void *p) { (void)p; return 0; }
void MIi_DmaFill32(u32 dma, void *dst, u32 data, u32 size, int enable)
{
    (void)dma; (void)dst; (void)data; (void)size; (void)enable;
    unreachable("MIi_DmaFill32");
}
void WmClearFifoRecvFlag(void) { unreachable("WmClearFifoRecvFlag"); }
void OS_InitMessageQueue(void *mq, void **msgs, s32 count)
{
    (void)mq; (void)msgs; (void)count;
    unreachable("OS_InitMessageQueue");
}
int OS_SendMessage(void *mq, void *msg, s32 flags)
{
    (void)mq; (void)msg; (void)flags;
    unreachable("OS_SendMessage");
    return 0;
}
void WmReceiveFifo(int tag, u32 data, int err)
{
    (void)tag; (void)data; (void)err;
    unreachable("WmReceiveFifo");
}
