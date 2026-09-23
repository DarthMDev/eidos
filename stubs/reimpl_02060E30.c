/*
 * func_02060E30 -- the expanded heap's free.  TRANSCRIPTION of an EXACT
 * source, not a match, and in the build only where EIDOS_HEAP_MATCHED is on.
 *
 * src/matched/func_02060E30.c has the same frame-layout dependence as
 * func_02060888.c (see stubs/reimpl_02060888.c): it declares `int end; int
 * start;`, lets func_02060764(&start, hdr) write the block's region start
 * and end into two words starting at `start`, and hands `&end - 1` to
 * func_02060B6C as that region.  Correct under mwccarm's frame layout, out
 * of bounds in C, and wrong under GCC.  The body below is the matched one
 * with the pair declared as the two-word region it is.
 */

#include "stubs.h"

struct MBlock;

struct MBlockList {
    struct MBlock *head;
    struct MBlock *tail;
};

struct ExpHeap {
    u8                pad_00[0x24];
    struct MBlockList freeList;
    struct MBlockList usedList;
    u16               groupId;
    u16               feature;
};

extern void           func_02060764(int *out, struct MBlock *blk);
extern struct MBlock *func_02060790(struct MBlockList *list, struct MBlock *node);
extern int            func_02060B6C(struct MBlockList *list, int *info);

void func_02060E30(struct ExpHeap *heap, void *block)
{
    struct MBlock *hdr = (struct MBlock *)((u8 *)block - 0x10);
    int            region[2];

    func_02060764(region, hdr);
    func_02060790(&heap->usedList, hdr);
    func_02060B6C(&heap->freeList, region);
}
