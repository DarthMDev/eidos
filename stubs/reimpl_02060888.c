/*
 * func_02060888 -- the expanded heap's carve.  TRANSCRIPTION of an EXACT
 * source, not a match, and in the build only where EIDOS_HEAP_MATCHED is on.
 *
 * src/matched/func_02060888.c is EXACT on the DS and cannot be compiled for
 * a host as written, for a reason that has nothing to do with pointer width:
 * it reads one local through another.  It declares
 *
 *     int end;
 *     int start;
 *
 * calls func_02060764(&start, block), which writes *two* words (the block's
 * region start and end), and then reads the second of them as `end`; later it
 * hands `&end - 1` to func_020607E8 as a two-word pair.  Both are correct
 * exactly when the compiler places `end` in the word above `start`, which
 * mwccarm does for this function (the header records why the two scalars,
 * and not a struct, are what reproduce the bytes).  A host compiler lays the
 * frame out its own way; under GCC the carve read a stack word it never
 * wrote and gave the new free block a garbage size (tests/test_heap.c caught
 * it on the first allocation).  In C terms both accesses are outside their
 * object, so no host compiler owes the DS layout.
 *
 * The body below is the matched one with that pair made an explicit
 * two-word region, `r[0]` where the source says `start` and `r[1]` where it
 * says `end`, and nothing else changed: the same three regions, the same
 * 0x14-byte remainder rule, the same attribute built field by field.
 * Recorded for aletheia: the source would be portable, and presumably
 * still EXACT, with the two scalars declared as an array, which only a
 * match.py run can say.  Until then this file stands in, and it is deleted
 * the day the matched source compiles correctly for a host.
 */

#include "stubs.h"

struct MBlock {
    u16            signature;
    u16            attr;
    u32            size;
    struct MBlock *prev;
    struct MBlock *next;
};

struct MBlockList {
    struct MBlock *head;
    struct MBlock *tail;
};

struct ExpHeap {
    struct MBlockList freeList;
    struct MBlockList usedList;
    u16               groupId;
    u16               feature;
};

extern void           func_02060764(int *out, struct MBlock *blk);
extern struct MBlock *func_02060790(struct MBlockList *list, struct MBlock *node);
extern struct MBlock *func_020607B8(struct MBlockList *list, struct MBlock *node,
                                    struct MBlock *after);
extern struct MBlock *func_020607E8(int *pair, u16 sig);
extern void           MIi_CpuClear32(u32 data, void *dest, u32 size);

void *func_02060888(struct ExpHeap *heap, struct MBlock *block, void *ptr,
                    u32 size, u16 fromTail)
{
    int            r[2];      /* the matched source's `start`, `end` */
    int            rest[2];
    int            alloc[2];
    struct MBlock *prev;
    struct MBlock *node;
    u32            sz;

    func_02060764(r, block);
    rest[1] = r[1];
    r[1] = (int)ptr - 0x10;
    rest[0] = (int)(size + (u32)ptr);

    prev = func_02060790(&heap->freeList, block);
    if ((u32)(r[1] - r[0]) < 0x14) {
        r[1] = r[0];
    } else {
        prev = func_020607B8(&heap->freeList, func_020607E8(r, 0x4652), prev);
    }
    if ((u32)(rest[1] - rest[0]) < 0x14) {
        rest[0] = rest[1];
    } else {
        func_020607B8(&heap->freeList, func_020607E8(rest, 0x4652), prev);
    }

    sz = (u32)(rest[0] - r[1]);
    if ((u8)((u32 *)heap)[-1] & 1) {
        MIi_CpuClear32(0, (void *)(uintptr_t)(u32)r[1], sz);
    }

    alloc[0] = (int)ptr - 0x10;
    alloc[1] = rest[0];
    node = func_020607E8(alloc, 0x5544);
    node->attr = node->attr & ~0x8000;
    node->attr = node->attr | (u16)(fromTail << 15);
    sz = (u16)((u32)(uintptr_t)node - (u32)r[1]);
    node->attr = node->attr & ~0x7f00;
    node->attr = node->attr | ((sz & 0x7f) << 8);
    sz = heap->groupId & 0xff;
    node->attr = node->attr & ~0xff;
    node->attr = node->attr | sz;
    func_020607B8(&heap->usedList, node, heap->usedList.tail);
    return ptr;
}
