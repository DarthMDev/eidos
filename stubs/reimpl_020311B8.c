/*
 * sub_020311B8 -- register a callback on a task-callback list.
 * REIMPLEMENTATION, not a match.
 *
 * This is the insert half of tcbl.c and the counterpart of the matched
 * runner src/matched/sub_020310C4.c.  It is 200 bytes, it is `open` in
 * aletheia's layout/worklist.json (packet pkt_0203115C), and the port needs
 * it because a list that cannot be registered on cannot be run.  Every line
 * below is the listing at aletheia asm/arm9/src/013_02030F70.s, 0x020311B8,
 * read straight through; the register names are the listing's.
 *
 *     push  {r3, r4, r5, r6, r7, r8, sb, lr}
 *     mov   r6, r0              r6 = the list
 *     mov   sb, r1              sb = the callback
 *     mov   r8, r2              r8 = the work size the caller wants
 *     mov   r7, r3              r7 = the sort key
 *     add   r5, r6, #0x18       r5 = &p->sentinel
 *     bl    func_02030F04       pop one node off the free list
 *     movs  r4, r0
 *     moveq r0, #0
 *     popeq {...}               the pool is empty: answer null
 *     str   r7, [r4, #0xc]      node->key  = key
 *     str   sb, [r4, #0x14]     node->func = callback
 *     ldr   r0, [r6, #4]        p->workSize
 *     cmp   r8, r0
 *     movls r0, #0              it fits in the node: no separate block
 *     bls   .store
 *     ldr   r0, =0x145          the allocation tag, 325
 *     ldr   r3, =0x020A7834     sub_0203101C_File, the tcbl.c anchor
 *     str   r0, [sp]
 *     ldrh  r0, [r6, #8]        the heap id the constructor kept at +8
 *     mov   r1, r8
 *     mov   r2, #0              clear = 0
 *     bl    func_02030734
 *   .store:
 *     str   r0, [r4, #0x10]     node->work = the block, or null
 *     ... the ordered insert, then the tail append ...
 *
 * Four things this pins that nothing else in the port did:
 *
 *  1. **The second constructor argument is a heap id, not a tag.**
 *     src/matched/sub_0203101C.c stores its `tag` parameter into the u16 at
 *     +8 and never uses it; this function loads that halfword and passes it
 *     to func_02030734 as the heap id of the oversize work block.  So the
 *     field is the list's *home heap*, remembered at construction for later
 *     allocations.
 *  2. **The work size comparison is `>`, not `>=`.**  `cmp r8, r0` / `bls`
 *     takes the embedded path when the request is less than *or equal to*
 *     the list's per-node work size, which is exactly the size the
 *     constructor reserved per row: `(((workSize + 3) & ~3) + 0x18) * count`.
 *     A request equal to the reserved size fits.
 *  3. **The list is sorted ascending on the key at +0x0c, and ties keep
 *     insertion order.**  The walk inserts before the first node whose key
 *     is strictly greater (`cmp r0, r1` / `bls` continues), so a node lands
 *     after every node that already has its key.  That is what makes the
 *     key a priority and the matched runner's forward walk a priority walk.
 *  4. **The tail append goes through the sentinel's own prev at +0x1c**,
 *     which is the second word of the embedded node -- the circular list's
 *     last element -- and it re-reads that field after storing into the
 *     node, which is how the original is written and what a same-object
 *     alias forces.
 *
 * Not reproduced, and worth stating: this is the port's own C, not matched
 * bytes.  What the port's tests can rest on is the *matched* side of the
 * pair -- func_02030F04 handing out the row, sub_02030FB4 answering with the
 * work area, sub_020310C4 walking sentinel-forward and dispatching -- and
 * the ordering assertion in tests/test_tcbl.c rests on this file's walk.
 * When 0x020311B8 is decompiled this file is deleted and that assertion
 * becomes a statement about matched code.
 */

#include "stubs.h"
#include "../sdk/tcbl.h"

/* The allocation tag at the oversize-work call site, pool word 0x145. */
#define TCBL_WORK_TAG 325u

extern const char sub_0203101C_File[];

extern void *func_02030F04(void *list);
extern void *sub_02030734(u32 heap, u32 size, u32 clear, const char *site,
                          u16 tag);

void *sub_020311B8(void *list, void (*func)(EidosTcblNode *, void *),
                   u32 work_size, u32 key)
{
    EidosTcblList *p = (EidosTcblList *)list;
    EidosTcblNode *s = &p->sentinel;
    EidosTcblNode *n;
    EidosTcblNode *cur;

    n = (EidosTcblNode *)func_02030F04(p);
    if (n == NULL) {
        return NULL;
    }

    n->key = key;
    n->func = func;

    if (work_size > p->work_size) {
        /*
         * The DS allocates the oversize work block here and stores the
         * pointer in the node's +0x10 word.  That word is a u32 in every
         * matched declaration of the node, so the store would truncate on
         * this host; sdk/tcbl.c refuses the request before it gets here and
         * this arm is unreachable.  It is kept because it is what the DS
         * does, and it is what has to come back when the field can hold a
         * pointer.
         */
        bw_panic("tcbl oversize work block", 0x020311B8,
                 "node +0x10 is a u32 in the matched declarations and cannot "
                 "hold a host pointer");
        return NULL;
    }
    n->work = 0u;

    for (cur = s->next; cur != s; cur = cur->next) {
        if (cur->key > n->key) {
            n->prev = cur->prev;
            n->next = cur;
            cur->prev->next = n;
            cur->prev = n;
            return n;
        }
    }

    n->prev = s->prev;
    n->next = s;
    s->prev->next = n;
    s->prev = n;
    return n;
}
