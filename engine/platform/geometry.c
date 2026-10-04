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

void eidos_display_viewport(const EidosScreenPlacement *pl, int out_w,
                            int out_h, EidosViewport *vp)
{
    int lw = pl->width, lh = pl->height;
    int cw, ch, i;

    vp->content.x = vp->content.y = vp->content.w = vp->content.h = 0;
    vp->screen[0] = vp->screen[1] = vp->content;
    if (out_w <= 0 || out_h <= 0 || lw <= 0 || lh <= 0) {
        return;
    }
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

int eidos_resolution_fit(const EidosViewport *vp, int max)
{
    int n = 1, i;
    const int w = (int)EIDOS_DS_SCREEN_WIDTH, h = (int)EIDOS_DS_SCREEN_HEIGHT;

    for (i = 0; i < 2; i++) {
        int nw = (vp->screen[i].w + w - 1) / w;
        int nh = (vp->screen[i].h + h - 1) / h;

        n = n < nw ? nw : n;
        n = n < nh ? nh : n;
    }
    if (max < 1) {
        max = 1;
    }
    return n > max ? max : n;
}

/* Mapped through the bottom screen's own destination rectangle, so its first
 * and last pixel row/column are exact whatever the scale (edge rounding in
 * the viewport can otherwise put its first pixel outside).  The sample is the
 * centre of the output pixel. */
int eidos_viewport_to_touch(const EidosScreenPlacement *pl,
                            const EidosViewport *vp, int wx, int wy,
                            int *x, int *y)
{
    const EidosRect *r = &vp->screen[1];
    int cx, cy, inside;

    (void)pl;
    if (r->w <= 0 || r->h <= 0) {
        return 0;
    }
    cx = wx < r->x ? r->x : wx >= r->x + r->w ? r->x + r->w - 1 : wx;
    cy = wy < r->y ? r->y : wy >= r->y + r->h ? r->y + r->h - 1 : wy;
    inside = cx == wx && cy == wy;
    if (x != NULL) {
        *x = (int)((s64)(2 * (cx - r->x) + 1) * (s64)EIDOS_DS_SCREEN_WIDTH /
                   (2 * (s64)r->w));
    }
    if (y != NULL) {
        *y = (int)((s64)(2 * (cy - r->y) + 1) * (s64)EIDOS_DS_SCREEN_HEIGHT /
                   (2 * (s64)r->h));
    }
    return inside;
}
