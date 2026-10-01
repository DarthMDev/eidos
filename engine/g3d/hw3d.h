/*
 * engine/g3d/hw3d.h -- the DS 3D hardware interface, driven from the DS
 * address space.
 *
 * engine/g3d's display-list interpreter and rasteriser draw what a parsed
 * NSBMD says.  Once the game's own code runs, the picture is what that code
 * writes into the 3D hardware: command words into the geometry FIFO at
 * 0x04000400 or the direct command ports 0x04000440..0x040005FF, the
 * control registers (DISP3DCNT, the clear colour and depth, the alpha-test
 * reference, the toon table), and texture images and palettes in the VRAM
 * banks VRAMCNT gives the texture slots.  engine/g3d/hw3d.c is that
 * hardware: it runs every command through the same interpreter, and at
 * SWAP_BUFFERS rasterises the frame into the RGBA buffer
 * engine/g2d/hwcompose.c takes as the 3D layer
 * (g2d_hw_render_dsmem(g3d_hw_frame(), ...)).  The file header of hw3d.c
 * says precisely which of the game's write paths reach it and which do not.
 */

#ifndef EIDOS_ENGINE_G3D_HW3D_H
#define EIDOS_ENGINE_G3D_HW3D_H

#include "g3d.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The 3D hardware's register addresses this module owns or reads. */
#define G3D_HW_DISP3DCNT      0x04000060u
#define G3D_HW_EDGE_COLOR     0x04000330u   /* 8 x u16                     */
#define G3D_HW_ALPHA_TEST_REF 0x04000340u
#define G3D_HW_CLEAR_COLOR    0x04000350u
#define G3D_HW_CLEAR_DEPTH    0x04000354u
#define G3D_HW_CLRIMAGE_OFS   0x04000356u
#define G3D_HW_FOG_COLOR      0x04000358u
#define G3D_HW_FOG_OFFSET     0x0400035Cu
#define G3D_HW_FOG_TABLE      0x04000360u   /* 32 x u8                     */
#define G3D_HW_TOON_TABLE     0x04000380u   /* 32 x u16                    */
#define G3D_HW_GXFIFO         0x04000400u   /* ..0x0400043F, all mirrors   */
#define G3D_HW_PORTS          0x04000440u   /* direct ports, op * 4 + 0x400 */
#define G3D_HW_SWAP_BUFFERS   0x04000540u
#define G3D_HW_VIEWPORT       0x04000580u
#define G3D_HW_GXSTAT         0x04000600u
#define G3D_HW_RAM_COUNT      0x04000604u
#define G3D_HW_DISP_1DOT      0x04000610u
#define G3D_HW_POS_RESULT     0x04000620u   /* 4 x s32                     */
#define G3D_HW_VEC_RESULT     0x04000630u   /* 3 x s16                     */
#define G3D_HW_CLIPMTX_RESULT 0x04000640u   /* 16 x s32                    */
#define G3D_HW_VECMTX_RESULT  0x04000680u   /* 9 x s32                     */
#define G3D_HW_WINDOW_END     0x040006A4u   /* end of the hooked window    */

/* Register features the rasteriser does not implement, reported per frame
 * by g3d_hw_unsupported() when the game enables them. */
#define G3D_HW_UNSUP_FOG        0x01u
#define G3D_HW_UNSUP_EDGE       0x02u
#define G3D_HW_UNSUP_ANTIALIAS  0x04u
#define G3D_HW_UNSUP_REARPLANE  0x08u   /* clear image from VRAM C/D    */
#define G3D_HW_UNSUP_WBUFFER    0x10u   /* SWAP_BUFFERS bit 1           */
#define G3D_HW_UNSUP_SHADOW     0x20u   /* a shadow polygon was drawn   */
#define G3D_HW_UNSUP_LIGHTING   0x40u   /* NORMAL with a light enabled  */

/*
 * Power-on state, and the access hook over 0x04000400..0x040006A3.  Call it
 * after every bw_ds_reset (which clears access hooks); the boot does
 * bw_ds_reset, bw_os_reset, g3d_hw_reset, then the game.
 */
void g3d_hw_reset(void);

/* Commit the store the last access left pending.  bw_io_sync does this
 * through the access hook; port code that reads the 3D state between
 * synchronisation points calls it directly. */
void g3d_hw_flush(void);

/*
 * The paths into the FIFO that do not go through a translated pointer:
 * port code, the copy and DMA shims (sdk/g3_fifo.c, sdk/mi_gx.c), and a
 * future DMA engine whose destination is 0x04000400.  `fifo_send` reads
 * `bytes / 4` little-endian words from host memory.
 */
void g3d_hw_fifo_write(u32 word);
void g3d_hw_fifo_send(const void *src, u32 bytes);
/* One 32-bit store to `addr` in 0x04000400..0x040005FF. */
void g3d_hw_port_write(u32 addr, u32 value);

/*
 * The shims' test for "this destination is the geometry FIFO": a host
 * pointer into the register file at 0x04000400..0x0400043F (what a
 * rewritten `&reg_G3X_GXFIFO` is), or the raw DS value itself, which is
 * what a matched call site passes when its address is a macro or an
 * integer rather than a literal cast (README: dsaddr's residue).  A shim
 * that answers yes calls g3d_hw_claim(dst) before sending, so the access
 * that produced the pointer is not mistaken for a store.
 */
int  g3d_hw_is_fifo(const volatile void *dst);
void g3d_hw_claim(const volatile void *dst);
/* As g3d_hw_is_fifo, for the read-only result window: returns the DS
 * address `src` names in 0x04000600..0x040006A3, or 0. */
u32  g3d_hw_result_addr(const volatile void *src);

/* Write every readable register (GXSTAT, RAM_COUNT, the test results, the
 * clip and vector matrices) into the register file. */
void g3d_hw_refresh(void);

/*
 * The last frame SWAP_BUFFERS produced: 256x192 RGBA in the byte order
 * engine/g2d composes, alpha 0 where nothing was drawn and the clear alpha
 * was 0.  NULL before the first swap.
 */
const u8 *g3d_hw_frame(void);
u32       g3d_hw_frame_count(void);
u32       g3d_hw_unsupported(void);

/* GXSTAT as the hardware holds it, bit 15 (the matrix stack error) included;
 * the register file's copy reads that bit as 0 (see hw3d.c). */
u32 g3d_hw_gxstat(void);

/* The interpreter state, for tests and tools. */
const G3dGx *g3d_hw_gx(void);

#ifdef __cplusplus
}
#endif

#endif /* EIDOS_ENGINE_G3D_HW3D_H */
