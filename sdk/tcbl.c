/*
 * sdk/tcbl.c -- the port's thin helper over the game's task-callback list.
 *
 * Nothing here is game logic.  Every one of these functions is a call into
 * either a matched source or a stubs/reimpl_ file, plus the one piece of
 * arithmetic the host needs and the DS did not: the work-size bias that
 * makes the matched constructor's `+ 0x18` row stride come out right when a
 * node header is 40 bytes rather than 24.  sdk/tcbl.h documents both.
 */

#include <stdlib.h>

#include "tcbl.h"
#include "port.h"

extern EidosTcblList *sub_0203101C(u32 heap, u16 home_heap, u32 count,
                                   u32 work_size);
extern void  sub_020310C4(EidosTcblList *list);
extern void *sub_020311B8(void *list, void (*func)(EidosTcblNode *, void *),
                          u32 work_size, u32 key);
extern void  sub_02031194(void *node);
extern void  sub_0203115C(void *list);
extern void  func_020307B0(void *block);

EidosTcblList *bw_tcbl_create(u32 heap, u16 home_heap, u32 count,
                              u32 work_size)
{
    /*
     * Rounded to eight rather than to the matched constructor's four: the
     * row stride is `work_size + 0x18`, and the nodes in the table hold host
     * pointers, so every row has to start eight-aligned.  0x18 is already a
     * multiple of eight, so an eight-aligned work size is exactly the
     * condition.
     */
    u32 ws = (work_size + 7u) & ~7u;

    if (count == 0u) {
        return NULL;
    }
    return sub_0203101C(heap, home_heap, count, ws + EIDOS_TCBL_HEADER_BIAS);
}

u32 bw_tcbl_work_size(const EidosTcblList *list)
{
    if (list == NULL || list->work_size <= EIDOS_TCBL_HEADER_BIAS) {
        return 0u;
    }
    return list->work_size - EIDOS_TCBL_HEADER_BIAS;
}

EidosTcblNode *bw_tcbl_register(EidosTcblList *list,
                                void (*func)(EidosTcblNode *, void *),
                                u32 work_size, u32 key)
{
    if (list == NULL || func == NULL) {
        return NULL;
    }
    if (work_size > bw_tcbl_work_size(list)) {
        /*
         * The DS would allocate a separate block here and store the pointer
         * in the node's +0x10 word.  That word is a u32 in every matched
         * declaration of the node, so on this host the pointer would be
         * truncated.  Refusing is the only honest answer; see sdk/tcbl.h.
         */
        bw_panic("tcbl work area larger than the node", 0x020311B8,
                 "a node that owns its work block cannot be represented on "
                 "an LP64 host: +0x10 is declared u32");
        return NULL;
    }
    return (EidosTcblNode *)sub_020311B8(list, func, work_size, key);
}

void *bw_tcbl_work(EidosTcblNode *node)
{
    extern void *sub_02030FB4(void *node);

    return node == NULL ? NULL : sub_02030FB4(node);
}

void bw_tcbl_run(EidosTcblList *list)
{
    if (list != NULL) {
        sub_020310C4(list);
    }
}

void bw_tcbl_destroy(EidosTcblNode *node)
{
    if (node != NULL) {
        sub_02031194(node);
    }
}

void bw_tcbl_destroy_all(EidosTcblList *list)
{
    if (list != NULL) {
        sub_0203115C(list);
    }
}

void bw_tcbl_free(EidosTcblList *list)
{
    if (list == NULL) {
        return;
    }
    bw_tcbl_destroy_all(list);
    func_020307B0(list->table);
    func_020307B0(list);
}
