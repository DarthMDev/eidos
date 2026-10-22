/*
 * patches: src/game/ov119/pokemontrade_3d.c POKETRADE_3D_EmitCubeVertex
 *
 * The trade screen's cube: its 24 vertex numbers (8 corners, x y z) are
 * the overlay's own data at 0x021E8190.  The tree's file takes them from a
 * file the build writes out of the player's ROM (build/gen/
 * h16_ov119_021E8190.inc), so a build made without a ROM (a release,
 * docs/release.md) has zeros there and draws no cube.  The port has the
 * overlay's data in main RAM once the overlay is loaded, read from the
 * player's own files (sdk/overlay_image.c), so the vertices are read from
 * there: every build draws the cube, and no build holds ROM bytes.
 */

typedef unsigned int u32;
typedef unsigned short u16;
typedef signed short s16;

extern void *bw_ds_ptr(u32 addr);
extern void bw_io_write32(u32 addr, u32 value);

#define CUBE_VTX_ADDR   0x021E8190u     /* sCubeVtx: s16[24] */
#define REG_VTX_16_ADDR 0x0400048Cu

void POKETRADE_3D_EmitCubeVertex(int n)
{
    const s16 *v = (const s16 *)bw_ds_ptr(CUBE_VTX_ADDR);
    s16 x, y, z;

    if (v == 0 || n < 0 || n >= 8) {
        return;
    }
    x = v[n * 3];
    y = v[n * 3 + 1];
    z = v[n * 3 + 2];
    bw_io_write32(REG_VTX_16_ADDR, (u32)(u16)x | ((u32)(u16)y << 16));
    bw_io_write32(REG_VTX_16_ADDR, (u32)(u16)z);
}
