/*
 * stubs/unit_members.c -- address spellings of game-unit members that the
 * manifests of the pinned aletheia do not list.
 *
 * A matched unit can define several functions while its manifest entry
 * lists only the first, so a caller in another file that names a later
 * member by another spelling (an overlay's name for the address) cannot be
 * resolved from the manifest, and tools/ovlink.py stubs it.  aletheia's
 * docs/restructure/unit_members_unlisted.tsv lists the units; once their
 * manifests carry every member, the generators resolve these by address and
 * the entries here go.
 *
 *   BMP_SetDisplaySwap  0x0204363C  ov021's name for BG_SYS_SetLcdSwap, the
 *                       second member of src/matched/arm9/unit_0204361C.c
 *                       (the LCD-swap bit of POWCNT1); the field calls it.
 */

typedef unsigned int u32;

extern void BG_SYS_SetLcdSwap(u32 swap);

void BMP_SetDisplaySwap(u32 swap)
{
    BG_SYS_SetLcdSwap(swap);
}
