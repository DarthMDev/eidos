/*
 * sdk/asm/OS_GetMacAddress.c -- OS_GetMacAddress at 0x02087C6C.
 *
 * Replaces src/matched/arm9_sdk/OS_GetMacAddress.c (an asm tail call).
 * void OS_GetMacAddress(u8 *macAddress):
 *
 *     mov r1, r0; ldr r0, =0x02FFFCF4; mov r2, #6; b MI_CpuCopy8
 *
 * Copies the six bytes the firmware leaves at 0x02FFFCF4 in the shared
 * area.  The callers also use the address spelling func_02087C6C.
 */

#include "asmsdk.h"
#include "../dsmem.h"

void OS_GetMacAddress(u8 *macAddress)
{
    MI_CpuCopy8(bw_ds_ptr(0x02FFFCF4u), macAddress, 6);
}

void func_02087C6C(u8 *macAddress)
{
    OS_GetMacAddress(macAddress);
}
