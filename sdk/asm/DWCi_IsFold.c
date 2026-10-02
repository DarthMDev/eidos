/*
 * sdk/asm/DWCi_IsFold.c -- DWCi_IsFold at 0x02035390.
 *
 * Replaces src/matched/arm9_sdk/DWCi_IsFold.c (an asm function):
 *
 *     ldr   r0, =0x02FFFFA8      ; HW_BUTTON_XY_BUF
 *     ldrh  r0, [r0]
 *     and   r0, r0, #0x8000
 *     movs  r0, r0, asr #15
 *     movne r0, #1
 *     moveq r0, #0
 *     bx    lr
 *
 * 1 while the lid is closed (bit 15 of the ARM7's XY halfword, sdk/input.h).
 */

#include "asmsdk.h"
#include "../dsmem.h"

int DWCi_IsFold(void);

int DWCi_IsFold(void)
{
    return (*(const u16 *)bw_ds_ptr(0x02FFFFA8u) & 0x8000u) != 0;
}

/* The callers' address spelling. */
int func_02035390(void);

int func_02035390(void)
{
    return DWCi_IsFold();
}
