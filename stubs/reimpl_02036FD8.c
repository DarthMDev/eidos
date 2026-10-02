/*
 * func_02036FD8 -- clear both screens' OAM from the empty sprite table.
 * REIMPLEMENTATION of aletheia's EXACT src/matched/arm9/func_02036FD8.c,
 * for a game data address passed as an integer.
 *
 * The matched source is
 *
 *     int func_02036FD8(int a0, int a1, int a2, int a3)
 *     {
 *         int t0 = func_0207E684((int)0x20A0810, (int)0, (int)0x20);
 *         int t1 = func_0207E708((int)0x20A0810, (int)0, (int)0x20);
 *         return t1;
 *     }
 *
 * GX_LoadOAM and GXS_LoadOAM (0x0207E684, 0x0207E708) copying 0x20 bytes
 * of the ARM9 table at 0x020A0810 to the main and sub OAM.  The address is
 * an integer argument, which tools/dsaddr.py leaves alone
 * (docs/dsaddr-residue.md lists both sites as needing a port answer), so the
 * callee read the DS address as a host pointer: the boot crashed there,
 * in the net initialisation's display reset (func_020370C8, reached from
 * func_0203CB34 after ov009's set-up).  The port answer is the DS address
 * translated where it is passed.  cmake/boot.cmake lists the matched file as
 * replaced.
 */

#include "stubs.h"
#include "../sdk/dsmem.h"

void GX_LoadOAM(const void *src, u32 offset, u32 size);
void GXS_LoadOAM(const void *src, u32 offset, u32 size);
void func_02036FD8(void);

void func_02036FD8(void)
{
    GX_LoadOAM(bw_ds_ptr(0x020A0810u), 0, 0x20);
    GXS_LoadOAM(bw_ds_ptr(0x020A0810u), 0, 0x20);
}
