/*
 * func_020319A4 -- the address of one save block's data.
 * REIMPLEMENTATION of aletheia's EXACT src/matched/arm9/func_020319A4.c,
 * for an argument the matched C forwards in r1.
 *
 * The matched source is
 *
 *     u32 func_020319A4(u8 *p)
 *     {
 *         return func_02033B68(*(u32 *)(p + 0x2c)) + *(u32 *)(p + 0x34);
 *     }
 *
 * and func_02033B68 (src/matched/arm9/func_02033B68.c) is `u32
 * func_02033B68(u8 *p, u32 i)`: the block's offset in the layout table.
 * The block id is the caller's second argument, in r1, which nothing writes
 * before the call (aletheia's asm/arm9/main.s, 0x020319A4-0x020319B0); the
 * one caller, sub_020071CC (src/matched/arm9/unit_020071CC.c), passes it
 * as `func_020319A4(ctrl->unk10, id)`.  A host passes one argument and the
 * table was indexed by an indeterminate id.  This file names the id.
 * cmake/boot.cmake lists the matched file as replaced.
 */

#include "stubs.h"

u32 func_02033B68(u32 table, u32 i);
u32 func_020319A4(u8 *p, u32 i);

u32 func_020319A4(u8 *p, u32 i)
{
    return func_02033B68(*(u32 *)(p + 0x2c), i) + *(u32 *)(p + 0x34);
}
