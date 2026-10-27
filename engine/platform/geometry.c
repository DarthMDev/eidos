/*
 * engine/platform/geometry.c -- where the two DS screens go in a window.
 *
 * Pure arithmetic, no SDL: the same code places the screens for drawing,
 * maps a mouse or finger position back to the bottom screen, and sizes the
 * screenshot, so those three can never disagree.  The placement is data (a
 * rectangle per screen in DS pixels), which is what a touch layout on a
 * phone will replace with its own table.
 *
 * The arrangement is the two screens and the run-status panel together:
 * the panel is drawn beside the screens (engine/platform/mod_panel_draw.h),
 * so the geometry places the screens and then grows the arrangement by the
 * panel's own space.  The screens keep their place in it either way.
 */

#include "mod_panel_draw.h"
#include "platform.h"

/*
 * The model's call, as sdk/mod_panel.h declares it: whether the panel the
 * port draws has a line in it.  The header is not included here (the
 * include path carries both directories and the two files have the same
 * name), the way engine/platform/mod_panel_draw.c declares the ones it
 * needs.
 */
extern u32 bw_mod_panel_shown_used(void);

/*
 * The one-screen layout's fallback: the two screens side by side, no gap,
 * in a 512 x 192 box.  The same arithmetic as the side-by-side layout, but
 * without the run-status panel (the one-screen layout shows none).
 */
void eidos_one_fallback_place(int swap, EidosScreenPlacement *out)
{
    const int w = (int)EIDOS_DS_SCREEN_WIDTH;
    const int h = (int)EIDOS_DS_SCREEN_HEIGHT;
    int first = swap ? 1 : 0;

    out->screen[first].x = 0;
    out->screen[first].y = 0;
    out->screen[1 - first].x = w;
    out->screen[1 - first].y = 0;
    out->screen[0].w = out->screen[1].w = w;
    out->screen[0].h = out->screen[1].h = h;
    out->width = 2 * w;
    out->height = h;
}

void eidos_one_fallback_viewport(int swap, int pixel_scale, int out_w,
                                 int out_h, EidosViewport *vp)
{
    EidosScreenPlacement pl;

    eidos_one_fallback_place(swap, &pl);
    eidos_display_viewport_n(&pl, out_w, out_h, pixel_scale, vp);
}

void eidos_world_geometry(int win_w, int win_h, float zoom,
                          EidosWorldGeometry *out)
{
    double a, kx, ky;
    s32 fx, fy;

    out->kx = out->ky = 4096;
    out->ds.x = out->ds.y = 0;
    out->ds.w = win_w > 0 ? win_w : 0;
    out->ds.h = win_h > 0 ? win_h : 0;
    if (win_w <= 0 || win_h <= 0) {
        return;
    }
    if (!(zoom > 0.0f)) {                       /* unset, or NaN */
        zoom = 1.0f;
    }
    if (zoom < EIDOS_WORLD_ZOOM_MIN) {
        zoom = EIDOS_WORLD_ZOOM_MIN;
    }
    if (zoom > EIDOS_WORLD_ZOOM_MAX) {
        zoom = EIDOS_WORLD_ZOOM_MAX;
    }
    a = (double)win_w / (double)win_h;
    if (a >= 4.0 / 3.0) {
        kx = (4.0 / 3.0) / a;
        ky = 1.0;
    } else {
        kx = 1.0;
        ky = a / (4.0 / 3.0);
    }
    kx *= (double)zoom;
    ky *= (double)zoom;
    fx = (s32)(kx * 4096.0 + 0.5);
    fy = (s32)(ky * 4096.0 + 0.5);
    if (fx < 1) {
        fx = 1;
    }
    if (fy < 1) {
        fy = 1;
    }
    out->kx = fx;
    out->ky = fy;
    out->ds.w = (int)((double)win_w * (double)fx / 4096.0 + 0.5);
    out->ds.h = (int)((double)win_h * (double)fy / 4096.0 + 0.5);
    out->ds.x = (win_w - out->ds.w) / 2;
    out->ds.y = (win_h - out->ds.h) / 2;
}

int eidos_world_to_ds(const EidosWorldGeometry *g, int wx, int wy, int *x,
                      int *y)
{
    const EidosRect *r = &g->ds;
    int cx, cy, inside;

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

void eidos_layout_place(int layout, int swap, int gap,
                        EidosScreenPlacement *out)
{
    const int w = (int)EIDOS_DS_SCREEN_WIDTH;
    const int h = (int)EIDOS_DS_SCREEN_HEIGHT;
    int first = swap ? 1 : 0;      /* the screen that goes first (top/left) */
    int second = 1 - first;

    if (layout == EIDOS_LAYOUT_ONE) {
        /* The window's shape is 16:9, the world view's; the screens are
         * the fallback's, centred in it.  No panel, no gap. */
        eidos_one_fallback_place(swap, out);
        out->screen[0].y = out->screen[1].y = 48;
        out->width = 2 * w;
        out->height = 288;
        return;
    }
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

    /*
     * The panel's space: the panel is drawn beside the screens, so the
     * arrangement has to make room for it.  The room is the panel's own
     * (eidos_mod_panel_space): its rectangle in the arrangement and how
     * much the arrangement grows for it.
     *
     * A panel that is not shown takes no space at all: the player hid it,
     * or no mod has filled a line, and then the arrangement is the two
     * screens alone and the window keeps its own shape.  That is the same
     * question the drawing asks (eidos_mod_panel_view), so the room and
     * the picture agree: the panel is never drawn outside the room
     * reserved for it, and the room is never reserved for a panel that
     * cannot be seen.
     */
    {
        EidosModPanelSpace space;

        eidos_mod_panel_space(out, &space);
        if ((space.grow_w > 0 || space.grow_h > 0) &&
            bw_mod_panel_shown_used() > 0) {
            out->width += space.grow_w;
            out->height += space.grow_h;
        }
    }
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

void eidos_display_viewport_n(const EidosScreenPlacement *pl, int out_w,
                              int out_h, int scale, EidosViewport *vp)
{
    int lw = pl->width, lh = pl->height;
    int k, i;

    if (scale <= 0 || lw <= 0 || lh <= 0 || out_w < lw || out_h < lh) {
        eidos_display_viewport(pl, out_w, out_h, vp);
        return;
    }
    k = scale;
    if (k > out_w / lw) {
        k = out_w / lw;
    }
    if (k > out_h / lh) {
        k = out_h / lh;
    }
    vp->content.w = lw * k;
    vp->content.h = lh * k;
    vp->content.x = (out_w - vp->content.w) / 2;
    vp->content.y = (out_h - vp->content.h) / 2;
    for (i = 0; i < 2; i++) {
        vp->screen[i].x = vp->content.x + pl->screen[i].x * k;
        vp->screen[i].y = vp->content.y + pl->screen[i].y * k;
        vp->screen[i].w = pl->screen[i].w * k;
        vp->screen[i].h = pl->screen[i].h * k;
    }
}

void eidos_first_window_size(const EidosScreenPlacement *pl, int work_w,
                             int work_h, int percent, int *out_w, int *out_h)
{
    int lw = pl->width > 0 ? pl->width : 1;
    int lh = pl->height > 0 ? pl->height : 1;
    int k = 1;

    if (work_w > 0 && work_h > 0) {
        int kh = (int)((s64)work_h * percent / 100) / lh;
        int kw = work_w / lw;

        k = kh < kw ? kh : kw;
        if (k < 1) {
            k = 1;
        }
    }
    *out_w = lw * k;
    *out_h = lh * k;
}

/* Newton's square root: this file stays free of libm. */
static double root(double v)
{
    double x = v > 1.0 ? v : 1.0;
    int i;

    for (i = 0; i < 64; i++) {
        x = 0.5 * (x + v / x);
    }
    return x;
}

void eidos_window_reshape(int cur_w, int cur_h, const EidosScreenPlacement *to,
                          int max_w, int max_h, int *out_w, int *out_h)
{
    double area, aspect, w, h;

    if (cur_w < 1) {
        cur_w = 1;
    }
    if (cur_h < 1) {
        cur_h = 1;
    }
    aspect = (double)to->width / (double)(to->height > 0 ? to->height : 1);
    area = (double)cur_w * (double)cur_h;
    w = root(area * aspect);
    h = w / aspect;
    if (max_w > 0 && w > (double)max_w) {
        h *= (double)max_w / w;
        w = (double)max_w;
    }
    if (max_h > 0 && h > (double)max_h) {
        w *= (double)max_h / h;
        h = (double)max_h;
    }
    *out_w = (int)(w + 0.5) < 1 ? 1 : (int)(w + 0.5);
    *out_h = (int)(h + 0.5) < 1 ? 1 : (int)(h + 0.5);
}

int eidos_resolution_fit(const EidosViewport *vp, int max)
{
    return eidos_resolution_fit_cap(vp, max, EIDOS_RESOLUTION_FIT_CAP);
}

int eidos_resolution_fit_cap(const EidosViewport *vp, int max, int cap)
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
    if (max > cap) {
        max = cap;                          /* an explicit N may go higher */
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
