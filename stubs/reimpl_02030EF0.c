/*
 * sub_02030EF0 -- the address of one row of the task-callback pool.
 * REIMPLEMENTATION, and an unusual one: aletheia has this function EXACT,
 * at src/matched/sub_02030EF0.c, and the port cannot use it.
 *
 * The matched source is
 *
 *     u32 sub_02030EF0(struct S02030EF0 *p, u32 idx)
 *     {
 *         return idx * (p->field4 + 0x18) + p->field14;
 *     }
 *
 * with `struct S02030EF0 { char pad0[4]; u32 field4; char pad1[0xc]; u32
 * field14; }`.  Two things in that make it a DS-only function, and both are
 * inherent rather than incidental:
 *
 *  1. **It returns an address in a u32.**  On the DS a pointer is a u32 and
 *     the arithmetic is exact.  On a 64-bit host the row address does not
 *     fit, and the caller (src/matched/sub_02030FC8.c) declares the callee
 *     as returning `void *`, so what would actually happen is a silent
 *     truncation to the low 32 bits of a malloc'd address.
 *  2. **field14 is the pool table pointer at DS offset 0x14**, reached
 *     through explicit padding.  On an LP64 host the manager's table pointer
 *     is at offset 24, because two of the fields in front of it are a
 *     pointer and its alignment (sdk/tcbl.h has the table).  The padding is
 *     right for the DS and wrong here whatever the return type.
 *
 * So the matched file stays out of CMakeLists.txt and this file stands in.
 * The arithmetic below is the matched arithmetic, with the two DS-specific
 * pieces replaced: `p->work_size + 0x18` is the same stride, computed from
 * the same field, with the same un-rounded `work_size` the constructor
 * stored, and the base is the same table pointer.  EIDOS_TCBL_HEADER_BIAS
 * (sdk/tcbl.h) is what keeps that stride large enough for a host node.
 *
 * The one caller is src/matched/sub_02030FC8.c, EXACT and in the build: it
 * walks `i` from 0 to p->count and hands each row to sub_02030F50 and then
 * to sub_02030F34, which is how the free list is built.
 */

#include "stubs.h"
#include "../sdk/tcbl.h"

void *sub_02030EF0(void *list, u32 idx)
{
    EidosTcblList *p = (EidosTcblList *)list;

    return (u8 *)p->table
           + (size_t)idx * (size_t)(p->work_size + EIDOS_TCBL_DS_NODE_HEADER);
}
