/*
 * sdk/asm/CTRDGi_IsTaskAvailable_02010054.c -- 0x02010054.
 *
 * Replaces src/matched/arm9_sdk/CTRDGi_IsTaskAvailable_02010054.c (a Thumb
 * asm function; TwlSDK libctrdg ctrdg_task.o CTRDGi_IsTaskAvailable):
 *
 *     ldr r0, =0x0214617C; ldr r0, [r0]; return r0 != 0 ? 1 : 0
 *
 * The address is a literal in the asm (not a relocation), so it is reached
 * through the DS address space: the registry resolves it to the game
 * object standing there when one is registered, main RAM otherwise.  The
 * matched callers use the spellings func_02010054 and sub_02010054.
 */

#include "asmsdk.h"
#include "../dsmem.h"

u32 CTRDGi_IsTaskAvailable_02010054(void)
{
    return *(const volatile u32 *)bw_ds_ptr(0x0214617Cu) != 0 ? 1u : 0u;
}

u32 func_02010054(void)
{
    return CTRDGi_IsTaskAvailable_02010054();
}

u32 sub_02010054(void)
{
    return CTRDGi_IsTaskAvailable_02010054();
}
