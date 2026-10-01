/*
 * sdk/dsmem_ext.h -- what sdk/dsmem.c provides beyond the fixed contract in
 * sdk/dsmem.h.
 *
 * sdk/dsmem.h is shared by several lanes and is not edited by any one of
 * them, so the two additions the implementation needed live here until they
 * are folded into it:
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
