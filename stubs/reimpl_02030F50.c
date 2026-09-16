/*
 * sub_02030F50 -- reset one task-callback node and point it at its list.
 * REIMPLEMENTATION, not a match.
 *
 * Thirty-two bytes, no relocations, no branches.  The whole function is its
 * own evidence (aletheia asm/arm9/src/013_02030BC8.s, 0x02030F50):
 *
 *     add  r3, r0, #0x18        r3 = &p->sentinel
 *     stm  r1, {r0, r3}         node->owner = p;  node->prev = &p->sentinel
 *     mov  r2, #0
 *     str  r3, [r1, #8]         node->next = &p->sentinel
 *     str  r2, [r1, #0xc]       node->key  = 0
 *     str  r2, [r1, #0x10]      node->work = 0
 *     str  r2, [r1, #0x14]      node->func = 0
 *     bx   lr
 *
 * Every offset in it is corroborated by matched code in the same family:
 *
 *   +0x00 owner   src/matched/sub_02030EAC.c reads a node's owner at +0 and
 *                 reaches its list head through it.
 *   +0x04 prev    src/matched/sub_02030F34.c writes prev at +4 and next at
 *   +0x08 next    +8 (`stmib r1, {r2, r3}`).
 *   +0x0c key     the key func_020311B8 orders the list on.
 *   +0x10 work    src/matched/sub_02030FB4.c: the owned work pointer, and
 *                 the address of the embedded area at +0x18 when it is null.
 *   +0x14 func    src/matched/sub_020310C4.c calls the node's callback from
 *                 +0x14.
 *
 * And the manager's sentinel really is at +0x18: src/matched/sub_0203101C.c
 * lays the 0x38-byte manager out with a whole node embedded there, calls
 * this function on `&p->sentinel` itself -- the self-link that makes an
 * empty list `next == prev == &p->sentinel` -- and
 * src/matched/sub_020310C4.c walks until it reaches that same address.
 *
 * Two callers, both matched: sub_0203101C on the sentinel, and
 * src/matched/sub_02030FC8.c on every row of the pool before pushing it onto
 * the free list.  A node that has never been inserted therefore links to the
 * sentinel from both sides, which is what lets sub_02030F70 unlink it
 * unconditionally with no "was it linked" test.
 *
 * The host's offsets are not the DS's; sdk/tcbl.h has the table and the
 * reason.  This file is written against the struct, not against the numbers.
 */

#include "stubs.h"
#include "../sdk/tcbl.h"

void sub_02030F50(void *list, void *node)
{
    EidosTcblList *p = (EidosTcblList *)list;
    EidosTcblNode *n = (EidosTcblNode *)node;

    n->owner = p;
    n->prev = &p->sentinel;
    n->next = &p->sentinel;
    n->key = 0;
    n->work = 0;
    n->func = NULL;
}
