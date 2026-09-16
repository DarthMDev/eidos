/*
 * sub_0203115C -- destroy every node on a task-callback list.
 * REIMPLEMENTATION, not a match.
 *
 * Fifty-six bytes (aletheia asm/arm9/src/013_02030F70.s, 0x0203115C):
 *
 *     push  {r4, r5, r6, lr}
 *     mov   r6, r0
 *     add   r4, r6, #0x18        r4 = &p->sentinel
 *     ldr   r0, [r4, #8]         r0 = sentinel->next
 *     str   r0, [r6, #0x30]      publish it as the walk cursor
 *     cmp   r0, r4
 *     popeq {r4, r5, r6, pc}     an empty list is already done
 *   .loop:
 *     ldr   r5, [r0, #8]         read the successor out first
 *     bl    func_02030F70        unlink and free this node
 *     mov   r0, r5
 *     str   r5, [r6, #0x30]      keep the cursor pointing at it
 *     cmp   r5, r4
 *     bne   .loop
 *     pop   {r4, r5, r6, pc}
 *
 * The same two habits as the matched runner src/matched/sub_020310C4.c, and
 * for the same reasons its header gives: the successor is read before the
 * node is unlinked, because unlinking makes the node's own links useless,
 * and the walk position is published in the manager's cursor at +0x30
 * rather than kept in a local, so that anything the teardown reaches can see
 * where it is.  The one difference from the runner is that this function
 * does not clear the cursor on the way out: it leaves it on the sentinel.
 *
 * The port uses it for teardown (sdk/tcbl.c's bw_tcbl_destroy_all and
 * bw_tcbl_free), which is also what makes tests/test_tcbl.c able to return
 * every node to the free list and check the heap balanced.
 */

#include "stubs.h"
#include "../sdk/tcbl.h"

extern void sub_02030F70(void *node);

void sub_0203115C(void *list)
{
    EidosTcblList *p = (EidosTcblList *)list;
    EidosTcblNode *s = &p->sentinel;
    EidosTcblNode *cur = s->next;

    p->cursor = cur;
    if (cur == s) {
        return;
    }
    do {
        EidosTcblNode *next = cur->next;
        sub_02030F70(cur);
        cur = next;
        p->cursor = cur;
    } while (cur != s);
}
