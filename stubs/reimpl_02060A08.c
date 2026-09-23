/*
 * func_02060A08 -- the head-end half of the expanded-heap allocator.
 * REIMPLEMENTATION, not a match.
 *
 * LP64 ONLY now.  aletheia has matched the heap environment constructor at
 * 0x0202FC48 and the expanded heap under it, and where EIDOS_HEAP_MATCHED is
 * on (32-bit pointers, the reference configuration) the matched source is
 * compiled in this file's place.  On an LP64 host the expanded heap cannot
 * run (README.md, "The heap environment"), so this stays that host's heap
 * floor.  The text below is from before the constructor was matched and
 * records why the stand-in was written the way it was.
 *
 * aletheia has this one EXACT (src/matched/func_02060A08.c) and the port
 * still cannot use it, for a reason that is about data rather than about
 * code: it walks an arena.  The matched source declares the heap handle as
 * 0x24 bytes of common header followed by a free-list head and tail, a
 * used-list head and tail, a group id and a feature halfword, walks the free
 * list from the head keeping the smallest block that can still hold the
 * request once its data start is rounded up to the alignment, and hands the
 * winner to func_02060888, which carves the region out of the block and
 * moves it to the used list.
 *
 * Two things have to exist before any of that can run here:
 *
 *   1. **an arena.**  The heap environment constructor at 0x0202FC48 is the
 *      function that builds one and fills g_EnvModule's records in with it;
 *      it is still open, so sdk/game_globals.c publishes a distinct non-null
 *      token per heap rather than a handle, and there is no free list for
 *      the matched walk to walk.
 *   2. **func_02060888**, the carve, which is what actually produces the
 *      pointer.  It is open too, and it is where the block header the walk
 *      reads is written.
 *
 * Until both land, this file is what src/matched/func_02060CA0.c dispatches
 * to for a non-negative alignment: sdk/os_alloc.c's bw_block_alloc, which
 * takes the block from malloc and aligns the *payload* rather than the
 * block.  The size is already rounded up to a word by the matched front
 * door, and a zero request has already been turned into one word there, so
 * nothing of that arithmetic is repeated here.
 *
 * The free-list walk's smallest-block-wins rule, its two early exits and the
 * allocation-mode bit are not reproduced and do not need to be: they decide
 * *which* region of the arena a request lands in, and the game observes only
 * that the pointer is non-null and correctly aligned.
 * src/matched/sub_02030098.c, the only path down to here, tests the result
 * against null and nothing else.
 */

#include "stubs.h"

void *func_02060A08(void *heap, u32 size, s32 align)
{
    (void)heap;
    return bw_block_alloc(size, (u32)(align < 0 ? -align : align));
}
