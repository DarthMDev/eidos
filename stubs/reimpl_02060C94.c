/*
 * func_02060C94 -- destroy an expanded heap.  REIMPLEMENTATION of aletheia's
 * EXACT src/matched/arm9/func_02060C94.c, for the reason
 * stubs/reimpl_0203064C.c gives: a register-forwarded argument.
 *
 * The matched source is
 *
 *     void func_02060C94(void) { func_0206074C(); }
 *
 * a tail call that hands its own r0, the heap, to func_0206074C
 * (src/matched/arm9_sdk/nhttp_cleanupasync.c: `void ...(void *p)`, which
 * unlinks the heap from the list that holds it).  Its callers pass the heap
 * (src/matched/arm9/sub_0202FF98.c; ov009's func_021570B8, the network
 * library's teardown the boot runs through ov009).  On the host the
 * argument is said out loud; an indeterminate one crashed the boot there.
 * cmake/boot.cmake lists the matched file as replaced.
 */

#include "stubs.h"

void func_0206074C(void *heap);
void func_02060C94(void *heap);

void func_02060C94(void *heap)
{
    func_0206074C(heap);
}
