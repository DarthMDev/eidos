/*
 * engine/g2d/window.c -- the windows and the colour special effects.
 *
 * Two independent mechanisms that meet in one place.  The windows decide,
 * per pixel, which layers may contribute and whether the colour effect
 * runs there; the colour effect decides what to do with the two pixels
 * that survived.  The compositor in bg.c calls both.
 *
 * **The windows.**  WIN0 and WIN1 are rectangles; the OBJ window is the
 * union of the pixels of the OBJs whose OAM mode selects it, which is the
 * OBJ engine's business and reaches this file as a mask the caller
 * supplies.  Priority runs WIN0, WIN1, the OBJ window, then outside every
 * window, and the first one that contains the pixel decides it, with its
 * own half of WININ (or, for the last two, WINOUT's two halves).  With no
 * window enabled the mask is every layer plus the effect, which is what
 * the hardware does when DISPCNT's window bits are clear.
 *
 * A right or bottom edge that is past the screen, or before its own left
 * or top edge, reads as the screen edge.  That is the documented
 * behaviour of the real registers, where X2 is eight bits and cannot
 * express 256 at all, and it is why a full-screen window is written with
 * X2 = 0 as often as with X2 = 255.
 *
 * **The colour effects.**  BLDCNT names a first target set, a second
 * target set and one of three effects; BLDALPHA carries EVA and EVB and
 * BLDY carries EVY, all in sixteenths, all saturating at 16.  The rule
 * the compositor enforces is the DS one: the effect runs only where the
 * topmost pixel belongs to the first target set, and alpha blending
 * additionally only where the pixel *directly beneath* that one belongs
 * to the second target set.  A second-target pixel two layers down is not
 * blended with, and neither is one that the window system hid.
 *
 * The arithmetic is in the 5 bits per channel the DS has.  The palette is
 * stored here expanded to 8 bits per channel (`c * 255 / 31`), so a
 * colour is folded back to 5 bits, blended, clamped to 31 and expanded
 * again.  The fold is exact for every colour that came out of a DS
 * palette: `(c8 * 31 + 127) / 255` recovers the original 5-bit value for
 * all 32 of them.  Blending in the 8-bit expansion instead would round
 * differently from the hardware on essentially every blended pixel.
 *
 * Nothing here is derived from any other decompilation, port or emulator:
 * the register layouts and the blend formulae are public NDS hardware
 * documentation.
 */

#include "g2d.h"

/* ------------------------------------------------------------------ */
/* Windows                                                             */
/* ------------------------------------------------------------------ */

static int window_contains(const G2dWindow *w, s32 x, s32 y)
{
    u32 x2 = w->x2;
    u32 y2 = w->y2;

    if (x2 > G2D_SCREEN_WIDTH || x2 < w->x1) {
        x2 = G2D_SCREEN_WIDTH;
    }
    if (y2 > G2D_SCREEN_HEIGHT || y2 < w->y1) {
        y2 = G2D_SCREEN_HEIGHT;
    }
    if (x < 0 || y < 0) {
        return 0;
    }
    return (u32)x >= w->x1 && (u32)x < x2
        && (u32)y >= w->y1 && (u32)y < y2;
}

u32 g2d_window_mask(const G2dWindows *windows, s32 x, s32 y)
{
    if (windows == NULL) {
        return G2D_LAYER_ALL;
    }
    if (!windows->win0.enabled && !windows->win1.enabled
        && !windows->obj_enabled) {
        return G2D_LAYER_ALL;
    }
    if (windows->win0.enabled && window_contains(&windows->win0, x, y)) {
        return windows->win0.layers;
    }
    if (windows->win1.enabled && window_contains(&windows->win1, x, y)) {
        return windows->win1.layers;
    }
    if (windows->obj_enabled && windows->obj_mask != NULL
        && x >= 0 && y >= 0
        && (u32)x < windows->mask_width && (u32)y < windows->mask_height
        && windows->obj_mask[(size_t)y * windows->mask_width + (size_t)x]) {
        return windows->obj_layers;
    }
    return windows->outside_layers;
}

/* ------------------------------------------------------------------ */
/* The colour special effects                                          */
/* ------------------------------------------------------------------ */

/* The 8-bit expansion of a DS palette channel, folded back to 5 bits. */
static u32 to5(u8 c)
{
    return ((u32)c * 31u + 127u) / 255u;
}

static u8 to8(u32 c5)
{
    if (c5 > 31u) {
        c5 = 31u;
    }
    return (u8)(c5 * 255u / 31u);
}

static u32 clamp16(u32 v)
{
    return v > 16u ? 16u : v;
}

void g2d_blend_alpha(const u8 first[3], const u8 second[3],
                     u32 eva, u32 evb, u8 out[3])
{
    u32 i;

    eva = clamp16(eva);
    evb = clamp16(evb);
    for (i = 0; i < 3u; i++) {
        u32 v = (to5(first[i]) * eva + to5(second[i]) * evb) >> 4;

        out[i] = to8(v > 31u ? 31u : v);
    }
}

void g2d_blend_brightness(const u8 first[3], u32 evy, int up, u8 out[3])
{
    u32 i;

    evy = clamp16(evy);
    for (i = 0; i < 3u; i++) {
        u32 a = to5(first[i]);
        u32 v;

        if (up) {
            v = a + (((31u - a) * evy) >> 4);
        } else {
            v = a - ((a * evy) >> 4);
        }
        out[i] = to8(v > 31u ? 31u : v);
    }
}
