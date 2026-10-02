/*
 * func_02061124 -- allocate from a frame heap (head or tail).
 * REIMPLEMENTATION of aletheia's EXACT src/matched/arm9/func_02061124.c,
 * for an argument and a result the matched C passes in registers.
 *
 * The matched source is
 *
 *     extern void func_02060FEC(char *p, int n);
 *     void func_02061124(char *p, int n, int v)
 *     {
 *         if (n == 0) n = 1;
 *         n = (n + 3) & ~3;
 *         p += 0x24;
 *         if (v >= 0) func_02060FEC(p, n);
 *         else        func_02061048((unsigned int *)p, (unsigned int)n,
 *                                   (unsigned int)-v);
 *     }
 *
 * Two things travel in registers on the DS (aletheia's asm/arm9/main.s,
 * 0x02061124-0x02061150):
 *
 *   - the head allocation's alignment.  func_02060FEC (src/matched/arm9/
 *     func_02060FEC.c) is `u32 func_02060FEC(struct Heap *h, u32 size,
 *     u32 align)`, and r2 still holds `v` at the `bl`: nothing between the
 *     entry and the call writes r2.  A host passes two arguments and the
 *     callee aligned by an indeterminate value, so the allocation failed or
 *     not from one run to the next;
 *   - the block.  Both callees return it in r0 and the function ends with
 *     their call, so it is this function's result, which every caller reads
 *     (func_020749F8: `void *node = func_02061124(t->heap, 0x14, 4)`).
 *
 * This file says both out loud.  cmake/boot.cmake lists the matched file as
 * replaced.
 */

#include "stubs.h"

u32   func_02060FEC(void *h, u32 size, u32 align);
u32   func_02061048(unsigned int *p, unsigned int size, unsigned int align);
void *func_02061124(char *p, int n, int v);

void *func_02061124(char *p, int n, int v)
{
    if (n == 0)
        n = 1;
    n = (n + 3) & ~3;
    p += 0x24;
    if (v >= 0)
        return (void *)(uintptr_t)func_02060FEC(p, (u32)n, (u32)v);
    return (void *)(uintptr_t)func_02061048((unsigned int *)p, (unsigned int)n,
                                            (unsigned int)-v);
}
