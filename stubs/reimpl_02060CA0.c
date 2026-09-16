/*
 * sub_02060CA0 -- the expanded heap's allocation front.  REIMPLEMENTATION,
 * not a match.
 *
 * This is the bottom of the game's heap layer: src/matched/sub_02030098.c
 * (status EXACT, the only function in the image that actually takes memory
 * from a heap) calls it as
 *
 *     p = sub_02060CA0(arena, size + 0x1c, align);
 *
 * with `align` either +4 or -4 depending on bit 15 of the public heap id,
 * and treats a null result as "heap exhausted" (result code 3).
 *
 * The 48 bytes at 0x02060CA0 are short enough to read off whole, and they
 * settle the contract exactly (aletheia asm/arm9/src/021_0205F7C8.s):
 *
 *     push  {r3, lr}
 *     cmp   r1, #0
 *     moveq r1, #1              size 0 is treated as size 1
 *     add   r1, r1, #3
 *     cmp   r2, #0
 *     bic   r1, r1, #3          size rounded up to a multiple of four
 *     blt   .tail
 *     bl    func_02060A08       align >= 0: allocate from the head
 *     pop   {r3, pc}
 *   .tail:
 *     rsb   r2, r2, #0          align < 0: the magnitude is the alignment
 *     bl    func_02060ABC       and the block comes off the tail
 *     pop   {r3, pc}
 *
 * so: round the request up to a word (a zero request still costs one word),
 * take the block from the head of the arena when the alignment argument is
 * non-negative and from the tail when it is negative, align it to the
 * magnitude of that argument, and answer null when it cannot be done.  The
 * two callees are the real expanded-heap allocators (the NNS FndExpHeap
 * family, several hundred bytes each of free-list walking); what they do
 * *inside* the arena is not reproduced here and does not need to be, because
 * nothing above this line can observe it.  The game sees one thing: a
 * pointer with `size` usable bytes behind it, at the requested alignment.
 *
 * What the port does instead of an arena.  There is no arena: the block
 * comes from malloc, with the malloc base stored in the word immediately
 * before the block so that stubs/reimpl_020307B0.c can give it back.  The
 * head/tail distinction therefore has no effect on the address handed out,
 * and that is the one observable difference from the DS.  It is not a
 * behavioural difference for the game: the direction flag exists so that
 * long-lived allocations can be kept from fragmenting the low end of a fixed
 * arena, and a host heap has neither a fixed arena nor that failure mode.  A
 * matched caller that compared two pointers' order would notice; none does,
 * and src/matched/sub_02030098.c, the only caller, does not look at the
 * value beyond testing it against null.
 *
 * **What is aligned is the payload, not the block.**  The caller adds a
 * 0x1c-byte header to the request and hands out `block + 0x1c`, so on the
 * DS, where 0x1c is a multiple of four and four is all anything needs, the
 * two are the same question.  On a 64-bit host they are not: 0x1c is not a
 * multiple of eight, and the game's own structures store host pointers now
 * (sdk/tcbl.h), so an eight-aligned block would hand out a payload that is
 * only four-aligned and every pointer in a task-callback node would be
 * misaligned.  This function therefore aligns `block + 0x1c` and lets the
 * block itself land where it must.  The coupling to the header size is
 * deliberate and is shared with exactly one other file, the release side,
 * which undoes it.
 *
 * When the real 0x02060CA0 and the allocators under it are decompiled this
 * file is deleted, but not on its own: an arena-based allocator needs an
 * arena, which means the heap constructor at 0x0202FC48 as well.
 */

#include <stdlib.h>
#include <string.h>

#include "stubs.h"

/* The block header src/matched/sub_02030098.c reserves in front of every
 * payload, and the alignment the payload needs on this host. */
#define BW_BLOCK_HEADER 0x1cu
#define BW_PAYLOAD_ALIGN ((u32)sizeof(void *) > 4u ? (u32)sizeof(void *) : 4u)

/* Room in front of the block for the stashed malloc base and for the
 * alignment slack.  One cache line is plenty and keeps the arithmetic
 * obviously non-negative. */
#define BW_GUARD 64u

void *sub_02060CA0(void *arena, u32 size, s32 align)
{
    u32 want;
    u32 a;
    unsigned char *base;
    unsigned char *payload;
    unsigned char *block;
    size_t misalign;

    (void)arena;

    if (size == 0u) {
        size = 1u;
    }
    want = (size + 3u) & ~3u;

    a = (u32)(align < 0 ? -align : align);
    if (a < BW_PAYLOAD_ALIGN) {
        a = BW_PAYLOAD_ALIGN;
    }
    if (a > 256u) {
        a = 256u;
    }

    base = (unsigned char *)malloc((size_t)BW_GUARD + (size_t)a
                                   + (size_t)BW_BLOCK_HEADER + (size_t)want);
    if (base == NULL) {
        return NULL;
    }

    payload = base + BW_GUARD + BW_BLOCK_HEADER;
    misalign = (size_t)(payload - (unsigned char *)0) % (size_t)a;
    if (misalign != 0) {
        payload += (size_t)a - misalign;
    }
    block = payload - BW_BLOCK_HEADER;

    memcpy(block - sizeof(base), &base, sizeof(base));
    return block;
}
