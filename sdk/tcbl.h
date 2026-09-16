/*
 * sdk/tcbl.h -- the task-callback list (aletheia's tcbl.c), as the port has
 * to hold it on a 64-bit host, and the port's thin helper over it.
 *
 * This is the game's own scheduler.  One manager owns a fixed pool of nodes;
 * a caller registers a callback with a priority and gets a node back; one
 * pass of the runner walks the live nodes in priority order and calls each
 * callback with the node and its work area.  engine/frame/ runs that pass as
 * the frame loop's step, which is what puts matched game code inside the
 * frame.
 *
 * Which functions are matched, and which the port supplies, is the whole
 * subject of this header:
 *
 *   sub_0203101C   construct        MATCHED  src/matched/sub_0203101C.c
 *   sub_02030FC8   build free list  MATCHED  src/matched/sub_02030FC8.c
 *   sub_020310C4   one pass         MATCHED  src/matched/sub_020310C4.c
 *   sub_02030F50   reset a node     port     stubs/reimpl_02030F50.c
 *   sub_02030F70   unlink a node    port     stubs/reimpl_02030F70.c
 *   sub_02030EF0   row address      port     stubs/reimpl_02030EF0.c
 *   sub_02030F34   free-list push   port     stubs/reimpl_02030F34.c
 *   func_02030F04  free-list pop    port     stubs/reimpl_02030F04.c
 *   sub_02030FB4   work accessor    port     stubs/reimpl_02030FB4.c
 *   sub_020311B8   register         port     stubs/reimpl_020311B8.c
 *   sub_02031194   destroy          port     stubs/reimpl_02031194.c
 *   sub_0203115C   destroy all      port     stubs/reimpl_0203115C.c
 *
 * The four in the middle are the port's own even though aletheia has EXACT
 * matched sources for them, and that is a finding rather than a preference.
 * See "Why four matched files are out of the build" in README.md; the short
 * form is below, because the struct in this header is the direct consequence.
 *
 * ---------------------------------------------------------------------
 * The layout, and the two places the DS's and the host's disagree
 * ---------------------------------------------------------------------
 *
 * Every offset below is the one the matched declarations produce when a
 * host compiler lays them out, which is what the matched code in this build
 * actually uses.  The DS offsets are in the right-hand column.
 *
 *   node    host  DS
 *   owner      0  0x00   a pointer.  The matched files declare +0 as `u32
 *                        unk_00` and never read it; on an LP64 host that
 *                        u32 is followed by four bytes of padding, and the
 *                        port widens the pair into one 8-byte pointer.  The
 *                        only writer is sub_02030F50 and the only readers
 *                        are sub_02030F70 and sub_02031194, all three of
 *                        them port files, so nothing matched can see the
 *                        difference.
 *   prev       8  0x04
 *   next      16  0x08
 *   key       24  0x0c   the priority the list is sorted ascending on
 *   work      28  0x10   **stays a u32.**  On the DS this is a pointer to a
 *                        separately allocated work area, null when the work
 *                        fits inside the node.  The matched declarations
 *                        spell it `u32`, which is exact on the DS and cannot
 *                        hold a host pointer, so the port never lets a node
 *                        own one: bw_tcbl_register refuses a request larger
 *                        than the list's per-node size rather than storing a
 *                        truncated pointer, and the field is always 0.
 *   func      32  0x14
 *   (work area follows)  40  0x18
 *
 *   manager host  DS
 *   count      0  0x00
 *   work_size  4  0x04
 *   heap_id    8  0x08   the home heap, kept at construction; sub_020311B8
 *                        allocates an oversize work block from it
 *   unk_0c    12  0x0c
 *   destroyed 16  0x10   the runner's "the node being run asked to die"
 *   table     24  0x14
 *   sentinel  32  0x18   a whole node, embedded; the empty list is it
 *                        pointing at itself
 *   cursor    72  0x30   the node the runner is inside, published so a
 *                        callback can fix it up
 *   free_head 80  0x34
 *   sizeof    88  0x38   src/matched/sub_0203101C.c allocates sizeof(struct
 *                        Tcbl), so the matched constructor asks for 88 here
 *                        and 0x38 on the DS, each correct for its target
 *
 * **The node header grew from 0x18 to 40 bytes, and the matched constructor
 * does not know that.**  src/matched/sub_0203101C.c sizes the pool as
 * `(((workSize + 3) & ~3) + 0x18) * count` with 0x18 written out, and
 * sub_02030EF0's row stride is `workSize + 0x18` to match.  Both constants
 * are the DS node header.  Rather than keep the constructor out of the build
 * over one literal, the port pays the difference at the only place it can:
 * bw_tcbl_create adds EIDOS_TCBL_HEADER_BIAS to the caller's work size
 * before handing it to the matched constructor, so the rows the matched
 * arithmetic produces are exactly `40 + work` bytes apart and the last one
 * ends exactly at the end of the table.  The port's own row-address
 * function uses the same `work_size + 0x18` formula the DS one does, which
 * is why the two stay in step.
 */

#ifndef EIDOS_SDK_TCBL_H
#define EIDOS_SDK_TCBL_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct EidosTcblList;

typedef struct EidosTcblNode {
    struct EidosTcblList *owner;
    struct EidosTcblNode *prev;
    struct EidosTcblNode *next;
    u32                   key;
    u32                   work;   /* always 0 on the host; see above */
    void                (*func)(struct EidosTcblNode *node, void *work);
} EidosTcblNode;

typedef struct EidosTcblList {
    u32            count;
    u32            work_size;
    u16            heap_id;
    u16            pad_0a;
    u32            unk_0c;
    u32            destroyed;
    void          *table;
    EidosTcblNode  sentinel;
    EidosTcblNode *cursor;
    EidosTcblNode *free_head;
} EidosTcblList;

/* The DS node header, written out as a literal in the matched constructor
 * and in the matched row-address function. */
#define EIDOS_TCBL_DS_NODE_HEADER 0x18u

/* What the same header costs on this host, and the difference the port adds
 * to a caller's work size so that the matched arithmetic still fits. */
#define EIDOS_TCBL_NODE_HEADER    ((u32)sizeof(EidosTcblNode))
#define EIDOS_TCBL_HEADER_BIAS \
    (EIDOS_TCBL_NODE_HEADER - EIDOS_TCBL_DS_NODE_HEADER)

/* ------------------------------------------------------------------ */
/* The port's helper                                                   */
/* ------------------------------------------------------------------ */

/*
 * Construct a list of `count` nodes with `work_size` usable bytes each, on
 * heap `heap`, remembering `home_heap` for later allocations (which is what
 * the matched constructor's second parameter turns out to be; see
 * stubs/reimpl_020311B8.c).  Goes straight through the matched constructor
 * src/matched/sub_0203101C.c with the bias above applied.  NULL if the heap
 * has not been initialised.
 */
EidosTcblList *bw_tcbl_create(u32 heap, u16 home_heap, u32 count,
                              u32 work_size);

/* The usable bytes in one node's work area, i.e. what was asked for,
 * rounded up. */
u32 bw_tcbl_work_size(const EidosTcblList *list);

/*
 * Register `func` at priority `key`, with `work_size` bytes of work area.
 * The list runs ascending on `key`, so a smaller key runs first and equal
 * keys keep registration order.  Returns the node, or NULL when the pool is
 * empty.  Panics if `work_size` exceeds the list's per-node size, because
 * the host cannot represent a node that owns a separate work block.
 */
EidosTcblNode *bw_tcbl_register(EidosTcblList *list,
                                void (*func)(EidosTcblNode *, void *),
                                u32 work_size, u32 key);

/* The node's work area, through the same accessor the matched runner uses. */
void *bw_tcbl_work(EidosTcblNode *node);

/* One pass: the matched runner src/matched/sub_020310C4.c. */
void bw_tcbl_run(EidosTcblList *list);

/* Destroy one node, and destroy every node. */
void bw_tcbl_destroy(EidosTcblNode *node);
void bw_tcbl_destroy_all(EidosTcblList *list);

/* Release the manager and its pool. */
void bw_tcbl_free(EidosTcblList *list);

#ifdef __cplusplus
}
#endif

#endif /* EIDOS_SDK_TCBL_H */
