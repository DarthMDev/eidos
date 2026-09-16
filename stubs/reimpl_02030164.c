/*
 * sub_02030164 -- the raw free, the floor of the game's heap layer.
 * REIMPLEMENTATION, not a match.
 *
 * src/matched/sub_020307B0.c (EXACT, the release entry point with 904 branch
 * references image-wide) is two calls:
 *
 *     void sub_020307B0(void *p)
 *     {
 *         sub_02030A2C(p);     the watched-allocation bookkeeping
 *         sub_02030164(p);     and the release itself
 *     }
 *
 * and its own header records what the second one is: "the raw free at
 * 0x02030164, which walks back over the 0x1c-byte header".  That is the
 * mirror of src/matched/sub_02030098.c, which is matched and describes the
 * block completely:
 *
 *   src/matched/sub_02030098.c   reserves a 0x1c-byte header in front of
 *                                every payload, writes the public heap id
 *                                into its first halfword and the constant
 *                                0x194E into the second, advances the
 *                                pointer past the header, and increments the
 *                                live-allocation count of the heap's record
 *                                (g_EnvModule.entries[ids[id]].field0c).
 *   src/matched/sub_020304B8.c   reads that heap id back, as a plain
 *                                halfword load at payload - 0x1c.
 *   src/matched/sub_02030880.c   stamps the provenance pair into header + 4.
 *
 * So the release is: recover the header, check the signature, undo the
 * count, and hand the block back to the allocator it came from.  On the DS
 * that last step is the expanded heap's own free, which puts the region back
 * on the arena's free list; here there is no arena (the heap environment
 * constructor at 0x0202FC48 is still open), so the block goes back through
 * sdk/os_alloc.c's bw_block_free, which is where the two halves of
 * src/matched/func_02060CA0.c took it from.  Everything here is attested by
 * the matched allocation path; nothing is invented except the host allocator
 * underneath, which is os_alloc.c's business.
 *
 * The signature check is the port's own and has no DS counterpart in
 * evidence: 0x194E is written on every allocation, so a pointer that does
 * not carry it was not produced by this heap, and freeing it would corrupt
 * the host allocator silently.  The port would rather say so.
 *
 * This file is deleted when 0x02030164 is decompiled, but not on its own:
 * an arena-based free needs an arena, which means the heap constructor at
 * 0x0202FC48 as well.
 */

#include "stubs.h"

#define BW_BLOCK_HEADER 0x1cu
#define BW_BLOCK_MAGIC  0x194Eu

void sub_02030164(void *payload)
{
    u8 *block;
    u32 heap_id;
    u32 magic;

    if (payload == NULL) {
        return;
    }

    block = (u8 *)payload - BW_BLOCK_HEADER;
    heap_id = bw_rd16(block);
    magic = bw_rd16(block + 2);
    if (magic != BW_BLOCK_MAGIC) {
        bw_panic("heap free of a foreign block", 0x02030164,
                 "the 0x1c-byte header sub_02030098 writes is not there");
        return;
    }

    bw_heap_note_release(heap_id);
    bw_block_free(block);
}
