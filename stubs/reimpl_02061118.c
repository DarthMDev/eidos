/*
 * func_02061118 -- destroy a heap of the second kind.  REIMPLEMENTATION of
 * aletheia's EXACT src/matched/arm9/func_02061118.c, for the reason
 * stubs/reimpl_0203064C.c gives: a register-forwarded argument.
 *
 * The matched source is
 *
 *     void func_02061118(void) { func_0206074C(); }
 *
 * the same tail call as func_02060C94 (stubs/reimpl_02060C94.c): its r0,
 * the heap, goes on to func_0206074C, which unlinks it.  Its callers pass
 * the heap (src/matched/arm9/func_020746E8.c, func_02074758.c).
 * cmake/boot.cmake lists the matched file as replaced.
 */

#include "stubs.h"

void func_0206074C(void *heap);
void func_02061118(void *heap);

void func_02061118(void *heap)
{
    func_0206074C(heap);
}
