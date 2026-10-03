/*
 * engine/platform/geometry.c -- where the two DS screens go in a window.
 *
 * Pure arithmetic, no SDL: the same code places the screens for drawing,
 * maps a mouse or finger position back to the bottom screen, and sizes the
 * screenshot, so those three can never disagree.  The placement is data (a
 * rectangle per screen in DS pixels), which is what a touch layout on a
 * phone will replace with its own table.
 */

#include "platform.h"

void eidos_layout_place(int layout, int swap, int gap,
                        EidosScreenPlacement *out)
{
    const int w = (int)EIDOS_DS_SCREEN_WIDTH;
    const int h = (int)EIDOS_DS_SCREEN_HEIGHT;
    int first = swap ? 1 : 0;      /* the screen that goes first (top/left) */
    int second = 1 - first;

    if (gap < 0) {
        gap = 0;
    }
    if (layout == EIDOS_LAYOUT_SIDE_BY_SIDE) {
        out->screen[first].x = 0;
        out->screen[first].y = 0;
        out->screen[second].x = w + gap;
        out->screen[second].y = 0;
        out->width = 2 * w + gap;
        out->height = h;
    } else {
        out->screen[first].x = 0;
        out->screen[first].y = 0;
        out->screen[second].x = 0;
        out->screen[second].y = h + gap;
        out->width = w;
        out->height = 2 * h + gap;
    }
    out->screen[0].w = out->screen[1].w = w;
    out->screen[0].h = out->screen[1].h = h;
}

static int mind(int a, int b)
{
    return a < b ? a : b;
}

void eidos_display_viewport(const EidosScreenPlacement *pl, int out_w,
                            int out_h, int fit, EidosViewport *vp)
{
    int lw = pl->width, lh = pl->height;
    int cw, ch, s, i;

    vp->content.x = vp->content.y = vp->content.w = vp->content.h = 0;
    vp->screen[0] = vp->screen[1] = vp->content;
    vp->scale = 0;
    if (out_w <= 0 || out_h <= 0 || lw <= 0 || lh <= 0) {
        return;
    }
    s = mind(out_w / lw, out_h / lh);
    if (!fit && s >= 1) {
        /* The largest whole multiple that fits. */
        cw = lw * s;
        ch = lh * s;
        vp->scale = s;
    } else {
        /* Aspect-preserving fractional fit (also the fallback when the
         * output is smaller than the layout at 1x). */
        if ((s64)out_w * lh <= (s64)out_h * lw) {
            cw = out_w;
            ch = (int)((s64)lh * out_w / lw);
        } else {
            ch = out_h;
            cw = (int)((s64)lw * out_h / lh);
        }
        if (cw < 1) {
            cw = 1;
        }
        if (ch < 1) {
            ch = 1;
        }
    }
    vp->content.w = cw;
    vp->content.h = ch;
    vp->content.x = (out_w - cw) / 2;
    vp->content.y = (out_h - ch) / 2;
    for (i = 0; i < 2; i++) {
        int x0 = pl->screen[i].x, x1 = x0 + pl->screen[i].w;
        int y0 = pl->screen[i].y, y1 = y0 + pl->screen[i].h;

        vp->screen[i].x = vp->content.x + (int)((s64)x0 * cw / lw);
        vp->screen[i].y = vp->content.y + (int)((s64)y0 * ch / lh);
        vp->screen[i].w = vp->content.x + (int)((s64)x1 * cw / lw)
                          - vp->screen[i].x;
        vp->screen[i].h = vp->content.y + (int)((s64)y1 * ch / lh)
                          - vp->screen[i].y;
    }
}

static s64 floor_div(s64 a, s64 b)
{
    s64 q = a / b;

    return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}

int eidos_viewport_to_touch(const EidosScreenPlacement *pl,
                            const EidosViewport *vp, int wx, int wy,
                            int *x, int *y)
{
    int px, py, inside;

    if (vp->content.w <= 0 || vp->content.h <= 0) {
        return 0;
    }
    px = (int)floor_div((s64)(wx - vp->content.x) * pl->width, vp->content.w)
         - pl->screen[1].x;
    py = (int)floor_div((s64)(wy - vp->content.y) * pl->height, vp->content.h)
         - pl->screen[1].y;
    inside = px >= 0 && px < (int)EIDOS_DS_SCREEN_WIDTH && py >= 0 &&
             py < (int)EIDOS_DS_SCREEN_HEIGHT;
    if (x != NULL) {
        *x = px < 0 ? 0 : px >= (int)EIDOS_DS_SCREEN_WIDTH
                 ? (int)EIDOS_DS_SCREEN_WIDTH - 1 : px;
    }
    if (y != NULL) {
        *y = py < 0 ? 0 : py >= (int)EIDOS_DS_SCREEN_HEIGHT
                 ? (int)EIDOS_DS_SCREEN_HEIGHT - 1 : py;
    }
    return inside;
}
