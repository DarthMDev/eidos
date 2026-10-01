/*
 * sdk/asm/NVRAMi_IsInitialized.c -- NVRAMi_IsInitialized at 0x020841E8.
 *
 * Replaces src/matched/arm9_sdk/NVRAMi_IsInitialized.c (TwlSDK nvram.o, an
 * asm function):
 *
 *     ldr r0, =g_02150EA0; ldr r0, [r0]
 *     cmp r0, #2; moveq r0, #1; movne r0, #0
 *     bx  lr
 *
 * BOOL NVRAMi_IsInitialized(void): TRUE when the u32 at the start of the
 * NVRAM work area (g_02150EA0, its init state) is 2.
 */

#include "asmsdk.h"

extern u32 g_02150EA0;   /* first field of the NVRAM bss: the init state */

BOOL NVRAMi_IsInitialized(void)
{
    return g_02150EA0 == 2u ? 1 : 0;
}
