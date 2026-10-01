/*
 * sdk/asm/MI_InitWramManager.c -- MI_InitWramManager at 0x02082198.
 *
 * Replaces src/matched/arm9_sdk/MI_InitWramManager.c (TwlSDK
 * mi_sharedwram.o, an asm function):
 *
 *     stmfd   sp!, {r3, lr}
 *     bl      func_02085D3C        OS_IsRunOnTwl()
 *     cmp     r0, #0
 *     ldmeqfd sp!, {r3, pc}        NITRO: return
 *     bl      func_02730468        the TWL-only WRAM manager set-up
 *     ldmfd   sp!, {r3, pc}
 *
 * void MI_InitWramManager(void).  OS_IsRunOnTwl is matched C (int (void));
 * func_02730468 is an empty TWL "ltdmain" placeholder, void (void), that the
 * NITRO boot never reaches.  OS_InitArena's caller (func_02086374.c) uses
 * the address spelling, which forwards.
 */

#include "asmsdk.h"

extern int OS_IsRunOnTwl(void);
extern void func_02730468(void);

void MI_InitWramManager(void)
{
    if (OS_IsRunOnTwl() == 0) {
        return;
    }
    func_02730468();
}

void func_02082198(void)
{
    MI_InitWramManager();
}
