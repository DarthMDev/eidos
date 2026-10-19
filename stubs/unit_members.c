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
 *   func_02048AE8       G3D_CAMERA_GetEye and G3D_CAMERA_GetUp (the vectors
 *   func_02048B30       at +0x20 and +0x38), members of
 *                       src/matched/arm9/unit_02048AD0.c; ov203's opening
 *                       demo calls them by address.
 */

typedef unsigned int u32;

extern void BG_SYS_SetLcdSwap(u32 swap);
extern void G3D_CAMERA_GetEye(void *camera, void *out);
extern void G3D_CAMERA_GetUp(void *camera, void *out);

void BMP_SetDisplaySwap(u32 swap)
{
    BG_SYS_SetLcdSwap(swap);
}

void func_02048AE8(void *camera, void *out)
{
    G3D_CAMERA_GetEye(camera, out);
}

void func_02048B30(void *camera, void *out)
{
    G3D_CAMERA_GetUp(camera, out);
}
