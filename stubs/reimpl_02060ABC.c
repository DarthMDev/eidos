/*
 * func_02060ABC -- the tail-end half of the expanded-heap allocator.
 * REIMPLEMENTATION, not a match.
 *
 * The mirror of stubs/reimpl_02060A08.c and out of the build for exactly the
 * same two reasons: aletheia has src/matched/func_02060ABC.c EXACT, and it
 * walks the free list from the *tail* through each block's `prev` link,
 * placing the candidate at the end of the block minus the request rounded
 * *down* to the alignment, then hands it to the open func_02060888 with the
 * direction flag set.  There is no arena here to walk and no carve to hand
 * it to (the heap environment constructor at 0x0202FC48 is open), so
 * src/matched/func_02060CA0.c dispatches to this instead.
 *
 * **The head/tail distinction has no effect on the address the port hands
 * out, and that is the one observable difference from the DS.**  The sign of
 * the alignment argument is how src/matched/sub_02030098.c spells "take this
 * block from the high end of the heap", which is bit 15 of the public heap
 * id; the front door passes the magnitude on.  On the DS that keeps
 * long-lived allocations from fragmenting the low end of a fixed arena.  A
 * host heap has neither a fixed arena nor that failure mode, so both halves
 * go to the same place: sdk/os_alloc.c's bw_block_alloc.  A matched caller
 * that compared two pointers' order would notice; none does.
 */

#include "stubs.h"

void *func_02060ABC(void *heap, u32 size, s32 align)
{
    (void)heap;
    return bw_block_alloc(size, (u32)(align < 0 ? -align : align));
}
