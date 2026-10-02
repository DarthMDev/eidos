/*
 * sdk/asm/DWCi_TSKlForm.c -- DWCi_TSKlForm at 0x02061EA4.
 *
 * Replaces src/matched/arm9_sdk/DWCi_TSKlForm.c (an asm function):
 *
 *     stmfd sp!, {r3, lr}
 *     mov   ip, #0
 *     str   ip, [sp]          ; fifth argument: 0
 *     bl    func_02061EB8     ; r0-r3 passed through
 *     ldmfd sp!, {r3, pc}
 *
 * func_02061EB8(list, pool, pRet, size, 0): the block allocator with
 * alignment 0 (`BOOL func_02061EB8(Blk **list, Blk **pool, u32 *pRet, u32
 * size, u32 alignment)`, matched C).  The callers (func_0206216C) declare
 * it as `u32 func_02061EA4(u32 a, u32 b, u32 *out, u32 size)`.
 */

#include "asmsdk.h"

u32 func_02061EB8(void *list, void *pool, u32 *pRet, u32 size, u32 alignment);
u32 DWCi_TSKlForm(void *list, void *pool, u32 *pRet, u32 size);

u32 DWCi_TSKlForm(void *list, void *pool, u32 *pRet, u32 size)
{
    return func_02061EB8(list, pool, pRet, size, 0);
}

/* The callers' address spelling. */
u32 func_02061EA4(void *list, void *pool, u32 *pRet, u32 size);

u32 func_02061EA4(void *list, void *pool, u32 *pRet, u32 size)
{
    return DWCi_TSKlForm(list, pool, pRet, size);
}
