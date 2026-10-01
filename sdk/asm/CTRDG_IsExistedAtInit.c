/*
 * sdk/asm/CTRDG_IsExistedAtInit.c -- CTRDG_IsExistedAtInit at 0x02090EF4.
 *
 * Replaces src/matched/arm9_sdk/CTRDG_IsExistedAtInit.c (an asm function).
 * BOOL CTRDG_IsExistedAtInit(void):
 *
 *     ldrh r1, [0x02FFFC30]; cmp r1, #0xFFFF; movne r0, #1; moveq r0, #0
 *
 * TRUE unless the module id the ARM7 left in the shared cartridge block is
 * 0xFFFF ("no cartridge"); sdk/arm7/ctrdg.c writes that block at power-on.
 */

#include "asmsdk.h"
#include "../dsmem.h"

BOOL CTRDG_IsExistedAtInit(void)
{
    return *(const volatile u16 *)bw_ds_ptr(0x02FFFC30u) != 0xFFFFu;
}
