/*
 * func_020307B0 -- the release counterpart of the game's allocation entry
 * point.  REIMPLEMENTATION, not a match.
 *
 * It takes the payload pointer sub_02030734 handed out and gives the block
 * back.  The port does not have to guess what that means, because the
 * allocation side is matched and fully describes the block:
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
 * So the release is the mirror: recover the header, check the signature,
 * undo the count, and hand the block back to the allocator it came from,
 * stubs/reimpl_02060CA0.c.  Everything here is attested by the matched
 * allocation path; nothing is invented except the host allocator underneath,
 * which is that file's business.
 *
 * The signature check is the port's own and has no DS counterpart in
 * evidence: 0x194E is written on every allocation, so a pointer that does
 * not carry it was not produced by this heap, and freeing it would corrupt
 * the host allocator silently.  The port would rather say so.
 *
 * aletheia's layout/symbols_sdk.json offers ENV_SetResourceSet for
 * 0x020307B0.  src/matched/sub_02030734.c's header records that offer as a
 * signature-matcher guess with eight siblings that does not fit the role,
 * and src/matched/sub_02031140.c does call 0x020307B0 in a shape that looks
 * like a resource-set setter.  That file is not in this closure; if it ever
 * is, the two readings of the address have to be settled in aletheia before
 * the port can hold both.
 */

#include <stdlib.h>
#include <string.h>

#include "stubs.h"

#define BW_BLOCK_HEADER 0x1cu
#define BW_BLOCK_MAGIC  0x194Eu

void func_020307B0(void *payload)
{
    u8 *base;
    unsigned char *malloc_base;
    u32 heap_id;
    u32 magic;

    if (payload == NULL) {
        return;
    }

    base = (u8 *)payload - BW_BLOCK_HEADER;
    heap_id = bw_rd16(base);
    magic = bw_rd16(base + 2);
    if (magic != BW_BLOCK_MAGIC) {
        bw_panic("heap free of a foreign block", 0x020307B0,
                 "the 0x1c-byte header sub_02030098 writes is not there");
        return;
    }

    bw_heap_note_release(heap_id);

    /* stubs/reimpl_02060CA0.c stashed the malloc base in the word before the
     * block it handed back, which is what `base` is. */
    memcpy(&malloc_base, base - sizeof(malloc_base), sizeof(malloc_base));
    free(malloc_base);
}
