/*
 * NitroMain (0x02005124, Thumb, 180 bytes) -- the game's main function: the
 * boot sequence and the frame loop.  REIMPLEMENTATION, not a match.
 *
 * Why this is a port-side file
 *
 *   crt0 reaches NitroMain through a register, so aletheia's disassembler
 *   never traced it: asm/arm9/main.s carries 0x02005124..0x020051D7 as
 *   `.word` data (the run that starts `0xF000B5F8` right after the
 *   "[SDK+Nintendo:libVCT 1.4]" string), and there is no src/matched file
 *   for it.  The C below was written by hand from those words, instruction
 *   for instruction, and does exactly what they do.  When aletheia matches
 *   0x02005124 this file is deleted and the matched source takes its place
 *   (stubs/stubs.h, "a reimplementation").
 *
 * The decoded instructions
 *
 *   Literal pool 0x020051C4..0x020051D7:
 *     0x020051C4  0x02005205   func_02005204 | 1 (Thumb)
 *     0x020051C8  0x0200520D   func_0200520C | 1 (Thumb)
 *     0x020051CC  0x04000208   REG_IME
 *     0x020051D0  0x020AA354   the game's second OSThread (g_020AA354)
 *     0x020051D4  0xFFFF7FFF   ~0x8000
 *
 *   02005124  B5F8       push  {r3-r7, lr}
 *   02005126  F000 F8FF  bl    func_02005328      system init
 *   0200512A  F000 F997  bl    sub_0200545C       gfl_use.c: heaps, system record
 *   0200512E  4925       ldr   r1, =func_02005204
 *   02005130  2002       movs  r0, #2             OS_IE_H_BLANK
 *   02005132  F07F EB20  blx   0x02084774         OS_SetIrqFunction
 *   02005136  4924       ldr   r1, =func_0200520C
 *   02005138  2001       movs  r0, #1             OS_IE_V_BLANK
 *   0200513A  2401       movs  r4, #1
 *   0200513C  F07F EB1A  blx   0x02084774         OS_SetIrqFunction
 *   02005140  2003       movs  r0, #3             V_BLANK | H_BLANK
 *   02005142  F07F EC12  blx   0x02084968         OS_EnableIrqMask
 *   02005146  4E21       ldr   r6, =0x04000208
 *   02005148  8830       ldrh  r0, [r6]           OS_EnableIrq(), inlined:
 *   0200514A  2001       movs  r0, #1               the old IME is read and
 *   0200514C  8034       strh  r4, [r6]             dropped, IME = 1
 *   0200514E  F078 E92E  blx   0x0207D3AC         GX_HBlankIntr(TRUE)
 *   02005152  2001       movs  r0, #1
 *   02005154  F078 E946  blx   0x0207D3E4         GX_VBlankIntr(TRUE)
 *   02005158  F082 EC0C  blx   0x02087974         OS_EnableInterrupts
 *   0200515C  F000 F86E  bl    func_0200523C      boot sequence, overlays
 *   02005160  4C1B       ldr   r4, =g_020AA354
 *   02005162  1F35       subs  r5, r6, #4         r5 = 0x04000204 (EXMEMCNT)
 *   02005164  0AF7       lsrs  r7, r6, #11        r7 = 0x8000
 * loop:
 *   02005166  F000 F941  bl    func_020053EC
 *   0200516A  F000 FA45  bl    func_020055F8
 *   0200516E  F000 F8BD  bl    func_020052EC
 *   02005172  F000 FA4B  bl    sub_0200560C       empty hook (bx lr)
 *   02005176  F042 EC0C  blx   0x02047990         g3d: deferred swap buffers
 *   0200517A  F000 F82D  bl    func_020051D8      readiness test
 *   0200517E  1C06       adds  r6, r0, #0
 *   02005180  D003       beq   0x0200518A
 *   02005182  8829       ldrh  r1, [r5]
 *   02005184  4813       ldr   r0, =0xFFFF7FFF
 *   02005186  4008       ands  r0, r1
 *   02005188  8028       strh  r0, [r5]           EXMEMCNT &= ~0x8000
 *   0200518A  1C20       adds  r0, r4, #0
 *   0200518C  F080 EADE  blx   0x0208574C         OS_IsThreadTerminated
 *   02005190  2800       cmp   r0, #0
 *   02005192  D103       bne   0x0200519C
 *   02005194  1C20       adds  r0, r4, #0
 *   02005196  2100       movs  r1, #0
 *   02005198  F080 EAE2  blx   0x02085760         OS_SleepThreadDirect(t, NULL)
 *   0200519C  2001       movs  r0, #1
 *   0200519E  2101       movs  r1, #1
 *   020051A0  F07F E9FC  blx   0x0208459C         OS_WaitIrq(TRUE, V_BLANK)
 *   020051A4  F000 F844  bl    func_02005230
 *   020051A8  1C20       adds  r0, r4, #0
 *   020051AA  F080 EAD0  blx   0x0208574C         OS_IsThreadTerminated
 *   020051AE  2800       cmp   r0, #0
 *   020051B0  D102       bne   0x020051B8
 *   020051B2  1C20       adds  r0, r4, #0
 *   020051B4  F080 EB58  blx   0x02085868         OS_WakeupThreadDirect(t)
 *   020051B8  2E00       cmp   r6, #0
 *   020051BA  D0D4       beq   loop
 *   020051BC  8828       ldrh  r0, [r5]
 *   020051BE  4338       orrs  r0, r7
 *   020051C0  8028       strh  r0, [r5]           EXMEMCNT |= 0x8000
 *   020051C2  E7D0       b     loop
 *
 *   Names: the SDK ones are layout/symbols_sdk.txt's for those addresses;
 *   the C below calls each callee by the symbol its matched source defines
 *   (func_0207D3AC is GX_HBlankIntr, func_0208459C is OS_WaitIrq, and so
 *   on), so the host linker resolves them without forwarders.  0x02047990 is
 *   the one callee aletheia has not matched (it too is untraced `.word`
 *   data); stubs/reimpl_02047990.c supplies it.
 *
 *   r4, r5, r6, r7 are the only state carried across iterations: the thread,
 *   EXMEMCNT's address, the readiness flag and the constant 0x8000.  r6 is
 *   written before it is read in every iteration, so nothing is carried
 *   from one frame to the next except what the callees keep themselves.
 *
 * How the port runs it
 *
 *   On the DS the function never returns: the loop body ends in
 *   OS_WaitIrq(TRUE, OS_IE_V_BLANK), which clears the vblank bit of the IRQ
 *   check word (DTCM + 0x3FF8, i.e. 0x02FE3FF8) and sleeps until the vblank
 *   handler (func_0200520C, installed above) sets it again.  The body splits
 *   at that wait into a head (0x02005166..0x020051A0) and a tail
 *   (0x020051A4..0x020051C2), and the DS's order in time is
 *
 *       init, head(0), wait, [vblank IRQ], tail(0), head(1), wait, ...
 *
 *   Two entry points cover the two ways the port can drive it:
 *
 *   - bw_nitromain_init() then bw_nitromain_step() once per frame, from the
 *     frame driver's step hook (engine/frame/frame.h).  A step is
 *     tail(n - 1) + head(n): it ends where the original blocks, the driver's
 *     vblank hook delivers the interrupt (running func_0200520C, which sets
 *     the check bit), and the next step resumes after the wait.  The wait is
 *     kept honest: the step tests the same check bit OS_WaitIrq's loop tests,
 *     and if vblank has not been delivered it returns 0 without running
 *     anything, which is what OS_WaitIrq would do (keep sleeping).
 *
 *   - NitroMain(), the literal form: init, then the infinite loop with the
 *     real OS_WaitIrq call.  This is the entry point for the threaded model
 *     docs/boot.md describes, where the game's main OSThread runs on a host
 *     thread and OS_WaitIrq's OS_SleepThread hands the run token back to the
 *     frame driver.  It never returns.
 *
 *   Both run the same head and tail functions, so there is one copy of the
 *   body.  Only one of the two may be used in a process.
 *
 * Port-side accesses
 *
 *   The original stores to REG_IME (0x04000208) and EXMEMCNT (0x04000204)
 *   directly; port code reaches the register file through sdk/dsmem.h's
 *   bw_io_read16/bw_io_write16.  The IRQ check word is main RAM (DTCM), and
 *   is read through bw_ds_ptr(0x02FE3FF8u): the matched OS_WaitIrq reaches
 *   the same word as SDK_AUTOLOAD_DTCM_START + 0x3FF8, and func_0200520C as
 *   SDK_AUTOLOAD_MAIN_TWL_START + 0x3FF8, so whoever defines those two
 *   symbols must register them at 0x02FE0000 (bw_ds_register) for the three
 *   to agree.  The word is host memory holding a native u32 written by
 *   matched code, so it is read and written as one, through memcpy.
 */

#include <string.h>

#include "stubs.h"
#include "dsmem.h"

/* ------------------------------------------------------------------ */
/* Callees, by the symbol each matched source defines                  */
/* ------------------------------------------------------------------ */

/* Game code (src/matched/arm9/). */
extern void func_02005328(void);       /* func_02005328.c                  */
extern void sub_0200545C(void);        /* sub_0200545C.c                   */
extern void func_02005204(void);       /* unit_020051D8.c, hblank handler  */
extern void func_0200520C(void);       /* func_0200520C.c, vblank handler  */
extern void func_0200523C(void);       /* func_0200523C.c                  */
extern void func_020053EC(void);       /* func_020053EC.c                  */
extern void func_020055F8(void);       /* func_020055F8.c                  */
extern void func_020052EC(void);       /* func_020052EC.c                  */
extern void sub_0200560C(void);        /* unit_0200560C.c                  */
extern int  func_020051D8(void);       /* unit_020051D8.c                  */
extern void func_02005230(void);       /* func_02005230.c                  */
extern void func_02047990(void);       /* stubs/reimpl_02047990.c          */

/* SDK (src/matched/arm9_sdk/, names from layout/symbols_sdk.txt). */
extern void OS_SetIrqFunction(u32 intrBit, void (*function)(void));
extern u32  OS_EnableIrqMask(u32 intr);
extern u32  OS_EnableInterrupts(void);
extern u32  OS_DisableInterrupts(void);
extern u32  OS_RestoreInterrupts(u32 state);
extern long func_0207D3AC(int enable);                   /* GX_HBlankIntr  */
extern long func_0207D3E4(int enable);                   /* GX_VBlankIntr  */
extern int  func_0208574C(const void *thread);           /* OS_IsThreadTerminated */
extern void func_02085760(void *thread, void *queue);    /* OS_SleepThreadDirect  */
extern void func_02085868(void *thread);                 /* OS_WakeupThreadDirect */
extern void func_0208459C(int clear, u32 irqFlags);      /* OS_WaitIrq     */

/* The game's second thread, 0x020AA354 (src/matched/arm9/func_02006368.c). */
extern u8 g_020AA354[];

/* Port entry points (demo/eidos_boot.c declares the same three). */
void NitroMain(void);
void bw_nitromain_init(void);
int  bw_nitromain_step(void);

/* ------------------------------------------------------------------ */
/* Constants, as the pool and the register arithmetic produce them    */
/* ------------------------------------------------------------------ */

#define NM_REG_IME        0x04000208u   /* pool word 0x020051CC          */
#define NM_REG_EXMEMCNT   0x04000204u   /* subs r5, r6, #4               */
#define NM_EXMEM_BIT      0x8000u       /* lsrs r7, r6, #11              */
#define NM_EXMEM_MASK     0x7FFFu       /* pool word 0xFFFF7FFF, as a u16 */
#define NM_IE_V_BLANK     0x1u
#define NM_IE_H_BLANK     0x2u
#define NM_IRQ_CHECK_WORD 0x02FE3FF8u   /* DTCM + 0x3FF8                  */

/* ------------------------------------------------------------------ */
/* State the original keeps in r6 between the head and the tail        */
/* ------------------------------------------------------------------ */

static int s_ready;        /* r6: func_020051D8's answer this iteration     */
static int s_waiting;      /* the step model: head ran, tail has not        */
static int s_initialised;

/* ------------------------------------------------------------------ */
/* The three parts                                                     */
/* ------------------------------------------------------------------ */

/* 0x02005124..0x02005164: everything before the loop. */
static void nitromain_init_part(void)
{
    func_02005328();
    sub_0200545C();

    OS_SetIrqFunction(NM_IE_H_BLANK, func_02005204);
    OS_SetIrqFunction(NM_IE_V_BLANK, func_0200520C);
    (void)OS_EnableIrqMask(NM_IE_V_BLANK | NM_IE_H_BLANK);

    /* OS_EnableIrq(), inlined: read the old IME, drop it, store 1. */
    (void)bw_io_read16(NM_REG_IME);
    bw_io_write16(NM_REG_IME, 1);

    (void)func_0207D3AC(1);
    (void)func_0207D3E4(1);
    (void)OS_EnableInterrupts();

    func_0200523C();
}

/* 0x02005166..0x0200519A: the frame's work, up to the vblank wait. */
static void nitromain_head(void)
{
    func_020053EC();
    func_020055F8();
    func_020052EC();
    sub_0200560C();
    func_02047990();

    s_ready = func_020051D8();
    if (s_ready != 0) {
        bw_io_write16(NM_REG_EXMEMCNT,
                      (u16)(bw_io_read16(NM_REG_EXMEMCNT) & NM_EXMEM_MASK));
    }

    if (func_0208574C(g_020AA354) == 0) {
        func_02085760(g_020AA354, NULL);
    }
}

/* 0x020051A4..0x020051C2: after the wait returns. */
static void nitromain_tail(void)
{
    func_02005230();

    if (func_0208574C(g_020AA354) == 0) {
        func_02085868(g_020AA354);
    }

    if (s_ready != 0) {
        bw_io_write16(NM_REG_EXMEMCNT,
                      (u16)(bw_io_read16(NM_REG_EXMEMCNT) | NM_EXMEM_BIT));
    }
}

/* ------------------------------------------------------------------ */
/* The IRQ check word, for the step model's wait                       */
/* ------------------------------------------------------------------ */

static u32 irq_check_read(void)
{
    u32 v;
    memcpy(&v, bw_ds_ptr(NM_IRQ_CHECK_WORD), sizeof v);
    return v;
}

static void irq_check_write(u32 v)
{
    memcpy(bw_ds_ptr(NM_IRQ_CHECK_WORD), &v, sizeof v);
}

/* ------------------------------------------------------------------ */
/* Entry points                                                        */
/* ------------------------------------------------------------------ */

void bw_nitromain_init(void)
{
    if (s_initialised) {
        bw_panic("NitroMain initialised twice", 0x02005124u,
                 "bw_nitromain_init");
    }
    s_initialised = 1;
    s_waiting = 0;
    nitromain_init_part();
}

/*
 * One frame of the loop: the tail of the previous iteration (once vblank
 * has been delivered) and the head of this one, ending at the point the
 * original enters OS_WaitIrq(TRUE, OS_IE_V_BLANK).  Returns 1 when it ran a
 * frame, 0 when the previous wait is still unsatisfied.
 */
int bw_nitromain_step(void)
{
    if (!s_initialised) {
        bw_panic("NitroMain stepped before init", 0x02005124u,
                 "bw_nitromain_step");
    }

    if (s_waiting) {
        /* OS_WaitIrq's loop condition: while (!(irqFlags & check)) sleep. */
        if ((irq_check_read() & NM_IE_V_BLANK) == 0) {
            return 0;
        }
        s_waiting = 0;
        nitromain_tail();
    }

    nitromain_head();

    /*
     * OS_WaitIrq(TRUE, ...)'s first half: with interrupts disabled, clear
     * the bits it is about to wait for.  The second half (the sleep) is the
     * return to the frame driver.
     */
    {
        u32 enabled = OS_DisableInterrupts();
        irq_check_write(irq_check_read() & ~NM_IE_V_BLANK);
        (void)OS_RestoreInterrupts(enabled);
    }
    s_waiting = 1;
    return 1;
}

/* The literal form, for the threaded model.  Never returns. */
void NitroMain(void)
{
    bw_nitromain_init();
    for (;;) {
        nitromain_head();
        func_0208459C(1, NM_IE_V_BLANK);
        nitromain_tail();
    }
}
