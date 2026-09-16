/*
 * func_02030F04 -- pop one node off the free list.  REIMPLEMENTATION;
 * aletheia has this function EXACT, at src/matched/func_02030F04.c, and the
 * port cannot use it for the same reason as stubs/reimpl_02030F34.c.
 *
 * The matched source is the mirror of the push:
 *
 *     struct Node *func_02030F04(struct List *list)
 *     {
 *         struct Node *head = list->head;
 *         if (head == 0) { return 0; }
 *         list->head = head->next;
 *         if (head->next != 0) { head->next->prev = 0; }
 *         return head;
 *     }
 *
 * with `struct List { char pad[0x34]; struct Node *head; }`.  0x34 is the DS
 * offset of the manager's free-list head; on this host it is 80 (sdk/tcbl.h).
 * The body is target-independent and is reproduced field for field below.
 *
 * Worth keeping from the matched version: `head->next` is re-read after the
 * store into `list->head` rather than cached, which is what a plain field
 * read compiles to and what the original's two loads show.  It makes no
 * difference to the result here and is kept so the two read the same.
 *
 * The caller is the register path, stubs/reimpl_020311B8.c, which treats a
 * null answer as "the pool is full" and returns null to its own caller.
 */

#include "stubs.h"
#include "../sdk/tcbl.h"

void *func_02030F04(void *list)
{
    EidosTcblList *p = (EidosTcblList *)list;
    EidosTcblNode *head = p->free_head;

    if (head == NULL) {
        return NULL;
    }
    p->free_head = head->next;
    if (head->next != NULL) {
        head->next->prev = NULL;
    }
    return head;
}
