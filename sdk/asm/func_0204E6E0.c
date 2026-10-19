/*
 * sdk/asm/func_0204E6E0.c -- 0x0204E6E0 FADE_GetSpeed.
 *
 * Replaces src/matched/arm9_sdk/func_0204E6E0.c (asm): the word at +0x1C of
 * the fade work that g_02146A7C points to (func_0204E5E8 allocates it, 0x20
 * bytes).
 */

#include "asmsdk.h"

extern u32 *g_02146A7C;

u32 FADE_GetSpeed(void)
{
    return g_02146A7C[7];
}
