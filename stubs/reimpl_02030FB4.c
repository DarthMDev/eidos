/*
 * sub_02030FB4 -- a node's work area, owned or embedded.  REIMPLEMENTATION;
 * aletheia has this function EXACT, at src/matched/sub_02030FB4.c, and the
 * port cannot use it.
 *
 * The matched source is five instructions:
 *
 *     ldr   r1, [r0, #0x10]
 *     cmp   r1, #0
 *     addeq r1, r0, #0x18
 *     mov   r0, r1
 *     bx    lr
 *
 * i.e. "the pointer stored at +0x10, or the address of the object embedded
 * at +0x18 when that pointer is null".  Both constants are DS node offsets:
 * +0x10 is the owned-work pointer, which on this host is the u32 at 28, and
 * +0x18 is the end of the node header, which on this host is 40
 * (sdk/tcbl.h).  Compiled as written against a host node the function would
 * return the node's `next` pointer for an embedded work area and the middle
 * of its own header for the address.
 *
 * This function is the one the matched runner calls: src/matched/sub_020310C4.c
 * (EXACT, and the frame loop's step) invokes every live node's callback as
 *
 *     n->func(n, sub_02030FB4(n));
 *
 * so the second argument every task callback receives comes through here.
 * That is why tests/test_tcbl.c can assert what the argument *is* and have
 * the assertion mean something about the game's own dispatch: the caller is
 * matched even though this callee is not.
 *
 * The owned branch is unreachable on this host, because the port never lets
 * a node own a separate work block (sdk/tcbl.h explains why: +0x10 is a u32
 * in every matched declaration of the node and cannot hold a host pointer).
 * It is kept because it is what the DS does.
 */

#include "stubs.h"
#include "../sdk/tcbl.h"

void *sub_02030FB4(void *node)
{
    EidosTcblNode *n = (EidosTcblNode *)node;

    if (n->work != 0u) {
        return (void *)(uintptr_t)n->work;
    }
    return (u8 *)n + EIDOS_TCBL_NODE_HEADER;
}
