/*
 * sdk/tcbl.c -- the port's thin helper over the game's task-callback list.
 *
 * Nothing here is game logic.  Every one of these functions is a call into a
 * matched source, plus the one piece of arithmetic the host needs and the DS
 * did not: the work-size bias that makes the matched constructor's `+ 0x18`
 * row stride come out right when a node header is 40 bytes rather than 24.
 * sdk/tcbl.h documents both.
 *
 * Under ILP32, the reference configuration, every callee below and every
 * accessor underneath them is aletheia's own C: wave 26 closed the last of
 * tcbl.c.  On an LP64 host nine of the twelve are stubs/reimpl_ files
 * instead, because the family's own declarations of the node disagree about
 * its size there.  sdk/tcbl.h has the whole of that argument.
 */

#include <stdlib.h>

#include "tcbl.h"
#include "port.h"

/*
 * The callees' current names.  aletheia's cleanup renamed the matched
 * definitions to descriptive names (sub_0203101C -> TCBL_Create, ...), so the
 * address spellings this file used to call are gone.  The constructor and the
 * runner (TCBL_Create, TCBL_Main) and the release entry point (GFL_HEAP_Free)
 * are always the matched files; the other four are matched only under
 * EIDOS_TCBL_MATCHED and are the stubs/reimpl_ stand-ins otherwise, which
 * keep their address spellings.  The symbol is the same shape either way.
 */
extern EidosTcblList *TCBL_Create(u32 heap, u16 home_heap, u32 count,
                                  u32 work_size);
extern void  TCBL_Main(EidosTcblList *list);
extern void  GFL_HEAP_Free(void *block);
#if EIDOS_TCBL_MATCHED
extern void *TCBL_AddTask(void *list, void (*func)(EidosTcblNode *, void *),
                          u32 work_size, u32 key);
extern void  TCBL_DeleteTask(void *node);
extern void  TCBL_DeleteAllTasks(void *list);
extern void *TCBL_GetTaskWork(void *node);
#else
extern void *sub_020311B8(void *list, void (*func)(EidosTcblNode *, void *),
                          u32 work_size, u32 key);
extern void  sub_02031194(void *node);
extern void  sub_0203115C(void *list);
extern void *sub_02030FB4(void *node);
#endif

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
    return TCBL_Create(heap, home_heap, count, ws + EIDOS_TCBL_HEADER_BIAS);
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
         * The DS allocates a separate block here and stores the pointer in
         * the node's +0x10 word, which is what src/matched/sub_020311B8.c
         * does under ILP32.  On an LP64 host that word is the u32 the
         * constructor and the runner declare, so the pointer would be
         * truncated.  Refusing is the only honest answer there; see
         * sdk/tcbl.h.  The guard is unconditional so that the port has one
         * behaviour to test rather than two.
         */
        bw_panic("tcbl work area larger than the node", 0x020311B8,
                 "a node that owns its work block cannot be represented on "
                 "an LP64 host: +0x10 is declared u32 by the constructor");
        return NULL;
    }
#if EIDOS_TCBL_MATCHED
    return (EidosTcblNode *)TCBL_AddTask(list, func, work_size, key);
#else
    return (EidosTcblNode *)sub_020311B8(list, func, work_size, key);
#endif
}

void *bw_tcbl_work(EidosTcblNode *node)
{
#if EIDOS_TCBL_MATCHED
    return node == NULL ? NULL : TCBL_GetTaskWork(node);
#else
    return node == NULL ? NULL : sub_02030FB4(node);
#endif
}

void bw_tcbl_run(EidosTcblList *list)
{
    if (list != NULL) {
        TCBL_Main(list);
    }
}

void bw_tcbl_destroy(EidosTcblNode *node)
{
    if (node != NULL) {
#if EIDOS_TCBL_MATCHED
        TCBL_DeleteTask(node);
#else
        sub_02031194(node);
#endif
    }
}

void bw_tcbl_destroy_all(EidosTcblList *list)
{
    if (list != NULL) {
#if EIDOS_TCBL_MATCHED
        TCBL_DeleteAllTasks(list);
#else
        sub_0203115C(list);
#endif
    }
}

void bw_tcbl_free(EidosTcblList *list)
{
    if (list == NULL) {
        return;
    }
    bw_tcbl_destroy_all(list);
    GFL_HEAP_Free(list->table);
    GFL_HEAP_Free(list);
}
