/*
 * engine/g3d/light.c -- the DS geometry engine's lighting.
 *
 * The display-list interpreter (gx.c) consumes the lighting commands
 * without interpreting them, because an asset-path render of a map model
 * must not depend on lights nobody has set.  The hardware path does set
 * them: engine/g3d/hw3d.c routes DIF_AMB, SPE_EMI, LIGHT_VECTOR,
 * LIGHT_COLOR, SHININESS and NORMAL here, and a NORMAL under a POLYGON_ATTR
 * that enables a light replaces the current vertex colour with the result.
 *
 * The lighting equation is the public one (GBATEK, "DS 3D Polygon Light
 * Parameters"), per light i enabled in POLYGON_ATTR bits 0-3:
 *
 *   N            = NORMAL * vector matrix
 *   L[i]         = LIGHT_VECTOR * vector matrix, taken when the command runs
 *   H[i]         = (L[i] + (0, 0, -1)) / 2, the half vector
 *   diffuse      = max(0, -(L[i] . N))
 *   shine        = max(0, -(H[i] . N)) ^ 2, through the shininess table
 *                  when SPE_EMI bit 15 enables it
 *   colour       = emission
 *                + sum over i of  specular * light[i] * shine
 *                               + diffuse  * light[i] * diffuse level
 *                               + ambient  * light[i]
 *
 * per 5-bit channel, clamped to 31.  The fixed point, which public
 * documentation gives only in outline, is this file's:
 *
 *   [AP] the 1.9 vectors are widened to the 20.12 the matrices use (<< 3)
 *        and multiplied in 64 bits, results in 12 fraction bits; the dot
 *        products keep 12 fraction bits (1.0 = 4096); a colour product
 *        c5 * l5 * level is shifted down by 5 + 12 (so 31 * 31 at level
 *        1.0 is 30, the same /32 the hardware's 5-bit multipliers make)
 *        and the ambient product by 5; the levels are not clamped above
 *        1.0, so an unnormalised normal overdrives, as on hardware.
 *   [AP] the shininess table index is the shine level's top 7 fraction
 *        bits (level * 128, clamped to 127), and an entry is an 8-bit
 *        fraction (255 is just under 1.0).
 *
 * Texture coordinate transform mode 2 (texture coordinates from the
 * normal) is not done here, and neither is it in gx.c.
 *
 * The register layouts and the lighting equation are public NDS hardware
 * documentation.  Nothing here is derived from any other decompilation,
 * port or emulator.
 */

#include <string.h>

#include "g3d.h"

static s32 sx10(u32 v)
{
    v &= 0x3FFu;
    return (v & 0x200u) ? (s32)v - 0x400 : (s32)v;
}

/* A packed 1.9 vector times the 3x3 of `m` (row vector), 12 fraction bits. */
static void xform10(const G3dMtx *m, u32 packed, s32 out[3])
{
    s64 v[3];
    u32 i;

    v[0] = (s64)sx10(packed) << 3;
    v[1] = (s64)sx10(packed >> 10) << 3;
    v[2] = (s64)sx10(packed >> 20) << 3;
    for (i = 0; i < 3u; i++) {
        out[i] = (s32)((v[0] * m->m[0 * 4 + i] + v[1] * m->m[1 * 4 + i]
                        + v[2] * m->m[2 * 4 + i]) >> 12);
    }
}

static s64 dot12(const s32 a[3], const s32 b[3])
{
    return ((s64)a[0] * b[0] + (s64)a[1] * b[1] + (s64)a[2] * b[2]) >> 12;
}

void g3d_light_init(G3dLight *l)
{
    memset(l, 0, sizeof(*l));
}

void g3d_light_vector(G3dLight *l, const G3dMtx *vec, u32 param)
{
    u32 i = (param >> 30) & 3u;
    s32 *v = l->dir[i];

    xform10(vec, param, v);
    l->half[i][0] = v[0] / 2;
    l->half[i][1] = v[1] / 2;
    l->half[i][2] = (v[2] - 4096) / 2;
}

void g3d_light_color(G3dLight *l, u32 param)
{
    l->color[(param >> 30) & 3u] = (u16)(param & 0x7FFFu);
}

int g3d_light_dif_amb(G3dLight *l, u32 param)
{
    l->diffuse = (u16)(param & 0x7FFFu);
    l->ambient = (u16)((param >> 16) & 0x7FFFu);
    return (param & 0x8000u) != 0;
}

void g3d_light_spe_emi(G3dLight *l, u32 param)
{
    l->specular = (u16)(param & 0x7FFFu);
    l->emission = (u16)((param >> 16) & 0x7FFFu);
    l->use_table = (param & 0x8000u) != 0;
}

void g3d_light_shininess(G3dLight *l, const u32 par[32])
{
    u32 i;

    for (i = 0; i < 32u; i++) {
        l->shine[i * 4u + 0u] = (u8)(par[i] & 0xFFu);
        l->shine[i * 4u + 1u] = (u8)((par[i] >> 8) & 0xFFu);
        l->shine[i * 4u + 2u] = (u8)((par[i] >> 16) & 0xFFu);
        l->shine[i * 4u + 3u] = (u8)((par[i] >> 24) & 0xFFu);
    }
}

static u32 ch(u16 c, u32 k)
{
    return ((u32)c >> (k * 5u)) & 0x1Fu;
}

u16 g3d_light_normal(const G3dLight *l, const G3dMtx *vec, u32 param,
                     u32 light_mask)
{
    s32 n[3];
    s64 acc[3];
    u32 i, k;
    u16 out = 0;

    xform10(vec, param, n);
    for (k = 0; k < 3u; k++) {
        acc[k] = (s64)ch(l->emission, k) << 17;
    }
    for (i = 0; i < 4u; i++) {
        s64 dif, sh;

        if ((light_mask & (1u << i)) == 0) {
            continue;
        }
        dif = -dot12(l->dir[i], n);
        if (dif < 0) {
            dif = 0;
        }
        sh = -dot12(l->half[i], n);
        if (sh < 0) {
            sh = 0;
        }
        sh = (sh * sh) >> 12;
        if (l->use_table) {
            s64 idx = sh >> 5;

            if (idx > 127) {
                idx = 127;
            }
            sh = (s64)l->shine[idx] << 4;
        }
        for (k = 0; k < 3u; k++) {
            s64 lc = ch(l->color[i], k);

            acc[k] += (s64)ch(l->specular, k) * lc * sh;
            acc[k] += (s64)ch(l->diffuse, k) * lc * dif;
            acc[k] += ((s64)ch(l->ambient, k) * lc) << 12;
        }
    }
    for (k = 0; k < 3u; k++) {
        s64 c = acc[k] >> 17;

        if (c > 31) {
            c = 31;
        }
        out |= (u16)((u32)c << (k * 5u));
    }
    return out;
}
