/*
 * sdk/dsmem_ext.h -- what sdk/dsmem.c provides beyond the fixed contract in
 * sdk/dsmem.h.
 *
 * sdk/dsmem.h is shared by several lanes and is not edited by any one of
 * them, so the additions the implementation needed live here until they
 * are folded into it (the access hooks further down are the third):
 *
 *  - the VRAM spaces the CPU cannot see (texture image and texture palette
 *    slots, the four extended-palette areas, the ARM7's two slots).  VRAMCNT
 *    maps banks into them exactly as it maps banks into the 0x06xxxxxx
 *    window, and the renderer has to read them through that mapping;
 *  - the hook sdk/dsmem.c calls from bw_ds_reset to register the ARM9
 *    globals sdk/game_globals.c defines.
 */

#ifndef BW_PORT_DSMEM_EXT_H
#define BW_PORT_DSMEM_EXT_H

#include "dsmem.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The non-CPU VRAM spaces, by GBATEK's names.  Sizes are the whole space;
 * every space is resolved in 8 KiB pieces, the smallest unit VRAMCNT maps
 * into any of them (the OBJ extended palette).
 */
enum {
    BW_VSLOT_TEX,          /* texture image, slots 0-3, 512 KiB           */
    BW_VSLOT_TEXPAL,       /* texture palette, slots 0-5, 96 KiB          */
    BW_VSLOT_BGEXT_MAIN,   /* main BG extended palette, slots 0-3, 32 KiB */
    BW_VSLOT_OBJEXT_MAIN,  /* main OBJ extended palette, 8 KiB            */
    BW_VSLOT_BGEXT_SUB,    /* sub BG extended palette, slots 0-3, 32 KiB  */
    BW_VSLOT_OBJEXT_SUB,   /* sub OBJ extended palette, 8 KiB             */
    BW_VSLOT_ARM7,         /* the ARM7's VRAM, two 128 KiB slots          */
    BW_VSLOT_KINDS
};

/*
 * The host byte at `offset` into space `kind`, or NULL if no bank is mapped
 * there.  `contig_out` (may be NULL) receives how many bytes from there on
 * are the same bank, i.e. how far the caller may index from the pointer.
 */
u8 *bw_vram_slot(int kind, u32 offset, u32 *contig_out);

/*
 * Access hooks: a notification on every *translation* of an I/O address.
 *
 * A write hook (dsmem.h) sees a register's value change at the next sync,
 * which is enough for a latch like VRAMCNT and useless for a port: the
 * geometry FIFO at 0x04000400 and the direct command ports behind it take a
 * stream of stores to one address, and plain memory keeps only the last.
 * What the rewrite does give is one bw_ds_ptr call per access, because
 * tools/dsaddr.py turns each literal cast into its own call, so the call is
 * where a port can see each store coming.  `fn(addr, 0)` runs inside
 * bw_ds_ptr / bw_ds_try_ptr for every address in [lo, hi), before the
 * pointer is returned (so before the matched code's store through it lands);
 * `fn(0, 1)` runs at the top of every bw_io_sync, so whatever the last
 * access left pending is drained at the synchronisation points.  At most
 * four ranges, never overlapping; bw_ds_reset clears them like the write
 * hooks.  engine/g3d/hw3d.c is the user and documents the protocol.
 */
typedef void (*BwIoAccessHook)(u32 addr, int sync);

void bw_io_access_hook(u32 lo, u32 hi, BwIoAccessHook fn);

/*
 * Defined by sdk/game_globals.c: bw_ds_register every ARM9 global the port
 * defines.  bw_ds_reset clears the registry and calls this, so the bindings
 * are part of power-on state; any other registration has to be repeated
 * after a reset.
 */
void bw_game_globals_register(void);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_DSMEM_EXT_H */
