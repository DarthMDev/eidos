/*
 * sub_02030F34 -- push one node onto the free list.  REIMPLEMENTATION, and
 * like stubs/reimpl_02030EF0.c an unusual one: aletheia has this function
 * EXACT, at src/matched/sub_02030F34.c, and the port cannot use it.
 *
 * The matched source is a plain doubly linked push-front:
 *
 *     void sub_02030F34(struct S02030F34 *p, struct Node02030F34 *node)
 *     {
 *         struct Node02030F34 *old = p->head;
 *         node->prev = 0;
 *         node->next = old;
 *         if (old != 0) { old->prev = node; }
 *         p->head = node;
 *     }
 *
 * and the body is correct on any target.  What is not is the declaration it
 * reaches the head through: `struct S02030F34 { char pad[0x34]; struct
 * Node02030F34 *head; }`.  0x34 is the DS offset of the manager's free-list
 * head.  On an LP64 host that field is at offset 80, because the two leading
 * pointers, the embedded sentinel node and the cursor are all wider
 * (sdk/tcbl.h).  Compiled as written the function would write the free-list
 * head into the middle of the sentinel.
 *
 * This is the same class of problem as reimpl_02030EF0.c and is the reason
 * four otherwise EXACT files are out of the build: a matched function that
 * reaches a field of a *pointer-bearing* game struct through a DS byte
 * offset has no host reading.  docs/runtime.md, "Why nine matched files are out of
 * the build", records it for the aletheia side; nothing about the DS bytes
 * is wrong, and nothing in this port may change them.
 *
 * The body below is the matched body, field for field.  Note that a node on
 * the free list is singly threaded through `next` with `prev` null on the
 * head, which is *not* the sentinel-linked shape the live list uses: a node
 * is reset by sub_02030F50 before it is pushed, and re-linked by
 * sub_020311B8 when it is popped.
 *
 * Callers: src/matched/sub_02030FC8.c (EXACT, the free-list build) and
 * stubs/reimpl_02030F70.c (the release side).
 */

#include "stubs.h"
#include "../sdk/tcbl.h"

void sub_02030F34(void *list, void *node)
{
    EidosTcblList *p = (EidosTcblList *)list;
    EidosTcblNode *n = (EidosTcblNode *)node;
    EidosTcblNode *old = p->free_head;

    n->prev = NULL;
    n->next = old;
    if (old != NULL) {
        old->prev = n;
    }
    p->free_head = n;
}
