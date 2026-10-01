/*
 * sdk/asm/asmsdk.h -- portable C for the boot closure's `asm` functions.
 *
 * aletheia matches a few dozen SDK and runtime functions as mwcc `asm`
 * bodies: hand-written ARM in the vendor source, or code no C spelling
 * produces (a computed jump into an unrolled kernel, `swpb`, `clz`, a long
 * branch veneer).  A host compiler cannot build those files.  Each one the
 * NitroMain closure reaches is either
 *
 *   - transliterated here, one file per matched source, when its behaviour
 *     is memory and register traffic (docs/boot.md, "Three layers": compile
 *     what only computes or touches memory); or
 *   - left to the OS shims when it depends on the CPU (CPSR, CP15 cache and
 *     wait-for-interrupt, SWI, the thread context), which this directory does
 *     not touch.
 *
 * Every file keeps the symbol the matched callers link against, and the
 * address spelling (func_/sub_) as a forwarder where a caller uses it.
 * Its header comment names the matched source it replaces and summarises
 * the asm, instruction by instruction where an edge case hangs on it.
 *
 * Two lists in CMakeLists.txt build these files.  BW_ASMSDK_SOURCES need
 * nothing beyond the eidos library itself (the address space and each
 * other) and are in it.  BW_ASMSDK_BOOT_SOURCES call matched game code or
 * name game globals, so they link only with the boot closure; their tests
 * supply those callees themselves (tests/test_asmsdk_glue.c).
 */

#ifndef BW_PORT_ASMSDK_H
#define BW_PORT_ASMSDK_H

#include "../types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* MI: CPU copies and fills (mi_memory.o)                              */
/* ------------------------------------------------------------------ */

void MI_CpuCopy8(const void *srcp, void *dstp, u32 size);       /* 0x02082D44 */
void func_02082D44(const void *srcp, void *dstp, u32 size);
void sub_02082D44(const void *srcp, void *dstp, u32 size);
void MI_CpuFill8(void *dstp, u8 data, u32 size);                 /* 0x02082BCC */
void func_02082BCC(void *dstp, u8 data, u32 size);
void MIi_CpuClear16(u16 data, void *destp, u32 size);            /* 0x02082A48 */
void MIi_CpuCopy16(const void *srcp, void *destp, u32 size);     /* 0x02082A60 */
void func_02082A60(const void *srcp, void *destp, u32 size);
void MIi_CpuClear32(u32 data, void *destp, u32 size);            /* 0x02082A7C */
void func_02082A7C(u32 data, void *destp, u32 size);
void MIi_CpuCopy32(const void *srcp, void *destp, u32 size);     /* 0x02082A90 */

/* MI_UncompressLZ8 at 0x020834C0: LZ77 (type 0x10) and the extended form. */
void MI_UncompressLZ8(const void *srcp, void *destp);
void func_020834C0(const void *srcp, void *destp);

/* ------------------------------------------------------------------ */
/* FX                                                                  */
/* ------------------------------------------------------------------ */

typedef struct BwAsmMtxFx22 {
    fx32 _00, _01;
    fx32 _10, _11;
} BwAsmMtxFx22;

void MTX_Rot22_(BwAsmMtxFx22 *dst, fx32 sinVal, fx32 cosVal);   /* 0x0207AD64 */

/* ------------------------------------------------------------------ */
/* mwccarm runtime division (MSL runtime.a)                            */
/* ------------------------------------------------------------------ */

/*
 * The ARM routines return the quotient in r0 and the remainder in r1.  A C
 * function returns one value, so the SDK-named entries return the quotient
 * (all the matched callers use) and the bw_ forms hand back both.
 */
s32 _s32_div_f(s32 a, s32 b);                                    /* 0x0209C0A4 */
s32 func_0209C0A4(s32 a, s32 b);
u32 _u32_div_f(u32 a, u32 b);                                    /* 0x0209C2B0 */
u32 _div_by_zero_check(u32 a, u32 b);                            /* same entry */
u32 _u32_div_not_0_f(u32 a, u32 b);                              /* 0x0209C2B8 */
s32 bw_rt_s32_divmod(s32 a, s32 b, s32 *rem);
u32 bw_rt_u32_divmod(u32 a, u32 b, u32 *rem);
u32 bw_rt_u32_divmod_not_0(u32 a, u32 b, u32 *rem);
u32 bw_rt_div_kernel(u32 n, u32 d, u32 *rem);      /* the unrolled kernel */

/* ------------------------------------------------------------------ */
/* OS: lock ids, MAC address, coprocessor context                       */
/* ------------------------------------------------------------------ */

s32  OS_GetLockID(void);                                         /* 0x02084EB8 */
s32  func_02084EB8(void);
void OS_ReleaseLockID(u16 lockID);                               /* 0x02084F10 */
void func_02084F10(u16 lockID);
void OS_GetMacAddress(u8 *macAddress);                           /* 0x02087C6C */
void func_02087C6C(u8 *macAddress);
void CP_SaveContext(void *context);                              /* 0x020775B8 */
void CPi_RestoreContext(const void *context);                    /* 0x020775F8 */

/* ------------------------------------------------------------------ */
/* CTRDG                                                               */
/* ------------------------------------------------------------------ */

BOOL CTRDG_IsExistedAtInit(void);                                /* 0x02090EF4 */
u32  CTRDGi_IsTaskAvailable_02010054(void);                      /* 0x02010054 */
u32  func_02010054(void);
u32  sub_02010054(void);

/* ------------------------------------------------------------------ */
/* Small game-side leaves                                              */
/* ------------------------------------------------------------------ */

void func_02034F74(void *obj, u32 mask);                         /* byte +0x38 |= mask */

/* ---- BW_ASMSDK_BOOT_SOURCES: these call matched code ------------- */

s32   func_02084D98(u16 lockID);                                 /* OS_UnLockCartridge */
s32   OS_UnLockCartridge(u16 lockID);
void *ArrayDeleteAt(u32 a, u32 b);                               /* 0x02005D90 */
void *sub_02005D90(u32 a, u32 b);
BOOL  NHTTPi_GetConnection(void *file, u32 pos);                 /* 0x0204960C */
BOOL  sub_0204960C(void *file, u32 pos);
u32   bw_os_init_arena_lo_addr(u32 id);
u32   bw_os_init_arena_hi_addr(u32 id);
void *OS_GetInitArenaLo(u32 id);                                 /* 0x02086694 */
void *func_02086694(u32 id);
void *OS_GetInitArenaHi(u32 id);                                 /* 0x02086594 */
void *func_02086594(u32 id);
u32   SNDi_IncAlarmId(int alarmNo);                              /* 0x02089FC0 */
BOOL  func_02030BA4(u32 size);
void  func_0203129C(u32 a);
void  func_02034F84(u32 mask);
void  func_0203516C(u32 mask);
u32   func_020351D0(u32 mask);
void  func_02049238(void *handle);
void  func_0208C86C(void *info);

/* TWL-only code the NITRO boot never reaches (see each file). */
void  func_020053E4(void *p);
int   func_02005420(void);
int   func_02005428(void);
void  func_02005454(int v);
void  func_02005B6C(void);
void  func_027312B4(void);
void  OSi_InitArenaEx_ltdmain(void);
u64   func_0273131C(u64 titleId, u64 mask);
u64   OSi_SearchTitleIdFromList(u64 titleId, u64 mask);
void  func_027313E8(u32 dmaNo);
void  PMi_PreSleepForNdma(u32 dmaNo);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_ASMSDK_H */
