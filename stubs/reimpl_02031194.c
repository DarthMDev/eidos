/*
 * sub_02031194 -- ask for one task-callback node to be destroyed.
 * REIMPLEMENTATION, not a match.
 *
 * **This file is the LP64 stand-in only.**  aletheia matched 0x02031194 in
 * wave 26 (src/matched/sub_02031194.c, EXACT), and under ILP32 that source is
 * what the build compiles: this file is left out of the library
 * entirely.  It survives because the matched source declares the node's
 * +0x00 and +0x10 as pointers while src/matched/sub_0203101C.c and
 * src/matched/sub_020310C4.c declare them as u32, which is one struct on
 * the DS and two different ones on a 64-bit host (sdk/tcbl.h).
 *
 * Thirty-six bytes (aletheia asm/arm9/src/013_02030F70.s, 0x02031194):
 *
 *     push  {r3, lr}
 *     ldr   r2, [r0]            the node's owner, at +0
 *     ldr   r1, [r2, #0x30]     the list's walk cursor
 *     cmp   r1, r0
 *     moveq r0, #1
 *     streq r0, [r2, #0x10]     it is the node being run: set the flag
 *     popeq {r3, pc}
 *     bl    func_02030F70       otherwise unlink it right now
 *     pop   {r3, pc}
 *
 * This is the other half of the runner's contract and it is attested from
 * the runner's side too.  src/matched/sub_020310C4.c (EXACT) publishes the
 * node it is about to call in the manager's cursor at +0x30, clears the flag
 * at +0x10 before each call, and after the call either advances or, if the
 * flag came back set, reads the successor out first and unlinks through
 * sub_02030F70.  Its own header names the field "the manager's 'current node
 * was destroyed' flag" and says the cursor lives in the manager rather than
 * in a local precisely "so a callback that destroys a *different* node can
 * fix it up".
 *
 * That is the whole shape: a node destroying itself from inside its own
 * callback cannot be unlinked on the spot, because the runner still has to
 * read its successor, so it raises the flag and the runner does it; a node
 * destroyed from anywhere else -- another node's callback, or outside a run
 * entirely, where the cursor is null because the runner clears it on both
 * exits -- is unlinked immediately.
 *
 * Note which node the cursor is compared against: the *node*, not the list.
 * Destroying a different node while the runner is inside some callback takes
 * the immediate arm, which is safe for every node except the one the runner
 * is holding, and that one is exactly the case the flag covers.
 */

#include "stubs.h"
#include "../sdk/tcbl.h"

extern void sub_02030F70(void *node);

void sub_02031194(void *node)
{
    EidosTcblNode *n = (EidosTcblNode *)node;
    EidosTcblList *p = n->owner;

    if (p->cursor == n) {
        p->destroyed = 1;
        return;
    }
    sub_02030F70(n);
}
