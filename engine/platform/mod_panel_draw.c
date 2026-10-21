/*
 * engine/platform/mod_panel.c -- the run-status panel: the port's own text
 * panel, drawn beside the DS screens (docs/mod-services.md, "Run-status
 * panel"; sdk/mod_panel.h).
 *
 * What this file does
 * -------------------
 *
 * It draws the panel's picture and says where the picture goes:
 *
 *   - the picture: the model's sixteen lines, drawn with the port's own
 *     bitmap font (engine/platform/font.h) into a buffer of the port's own
 *     resolution, which the platform's presenter shows beside the two
 *     screens;
 *   - the place: beside the screens when they are stacked, below them when
 *     they are side by side, so the panel never covers the game.
 *
 * The picture is drawn on the CPU and only when the model's lines change:
 * the model counts its changes, and the view carries that number, so the
 * platform does not upload the same picture every frame.
 *
 * The colours are the first-run window's (engine/platform/wizard.c): a dark
 * ground, a blue edge and a light text.
 *
 * The model's calls are declared here rather than taken from
 * sdk/mod_panel.h: the two files have the same name and the include path
 * carries both directories (sdk/mod_panel.h and engine/platform/mod_panel.h),
 * and the drawing is the port's own code, not a mod's.
 */

#include <string.h>

#include "font.h"
#include "mod_panel_draw.h"
#include "platform.h"

/* ------------------------------------------------------------------ */
/* The model (sdk/mod_panel.c)                                         */
/* ------------------------------------------------------------------ */

/*
 * The model's calls, as sdk/mod_panel.h declares them.  The header is not
 * included here: it has the same name as this file's own header and the
 * include path carries both directories.
 */
extern const char *bw_mod_panel_shown_line(u32 line);
extern u32         bw_mod_panel_shown_used(void);
extern u32         bw_mod_panel_generation(void);
extern u32         bw_mod_panel_line_count(void);

/* ------------------------------------------------------------------ */
/* The colours                                                         */
/* ------------------------------------------------------------------ */

/* 0xAARRGGBB, the way the first-run window writes its pixels. */
#define C_BG   0xFF1E2230u    /* the ground */
#define C_EDGE 0xFF4F6FB0u    /* the border */
#define C_TEXT 0xFFE8EAF0u    /* the text */

/* ------------------------------------------------------------------ */
/* The picture                                                         */
/* ------------------------------------------------------------------ */

/* The panel's own buffer: the picture the presenter shows. */
static u8 g_picture[EIDOS_MOD_PANEL_W * EIDOS_MOD_PANEL_H * 4];

/* The model's generation the picture was drawn from, and whether it was
 * drawn at all: the model's counter starts at zero, so the picture cannot
 * be "up to date" before the first draw. */
static u32 g_drawn;
static int g_drawn_ok;

/* The margin between the border and the text, in pixels. */
#define MARGIN 3

/* How many characters a line holds: the picture is 128 pixels wide and a
 * character is six. */
#define CHARS ((EIDOS_MOD_PANEL_W - 2 * MARGIN) / EIDOS_FONT_ADVANCE)

/* Fill a rectangle of the picture with a colour. */
static void fill(u8 *pic, int x, int y, int w, int h, u32 c)
{
    int i, j;

    for (j = y; j < y + h; j++) {
        if (j < 0 || j >= EIDOS_MOD_PANEL_H) {
            continue;
        }
        for (i = x; i < x + w; i++) {
            if (i < 0 || i >= EIDOS_MOD_PANEL_W) {
                continue;
            }
            {
                u8 *p = pic + ((size_t)j * (size_t)EIDOS_MOD_PANEL_W + (size_t)i) * 4u;

                p[0] = (u8)((c >> 16) & 0xFFu);   /* R */
                p[1] = (u8)((c >> 8) & 0xFFu);    /* G */
                p[2] = (u8)(c & 0xFFu);           /* B */
                p[3] = (u8)((c >> 24) & 0xFFu);   /* A */
            }
        }
    }
}

/* One character, at the cell's top left corner. */
static void glyph(u8 *pic, int x, int y, u32 ch)
{
    const u8 *rows = eidos_font_glyph(ch);
    int r, col;

    for (r = 0; r < EIDOS_FONT_ROWS; r++) {
        for (col = 0; col < EIDOS_FONT_COLS; col++) {
            if (rows[r] & (0x10u >> col)) {
                fill(pic, x + col, y + r, 1, 1, C_TEXT);
            }
        }
    }
}

/* One line of text, cut at the picture's width.  The text is UTF-8, and a
 * character the font lacks shows as a box (the font's own answer). */
static void text(u8 *pic, int x, int y, const char *s)
{
    u32 ch;
    int n = 0;

    while (n < CHARS) {
        ch = eidos_utf8_next(&s);
        if (ch == 0) {
            break;
        }
        glyph(pic, x + n * EIDOS_FONT_ADVANCE, y, ch);
        n++;
    }
}

/*
 * The picture: the ground, the border and the model's lines, one per line
 * of the panel.  A line that is empty is left blank: the panel keeps the
 * place of every line, so a gap is a gap.
 */
static void render(u8 *pic)
{
    u32 line, lines = bw_mod_panel_line_count();

    if (lines > 16u) {
        lines = 16u;                 /* the picture holds sixteen lines */
    }
    fill(pic, 0, 0, EIDOS_MOD_PANEL_W, EIDOS_MOD_PANEL_H, C_BG);
    fill(pic, 0, 0, EIDOS_MOD_PANEL_W, 1, C_EDGE);
    fill(pic, 0, EIDOS_MOD_PANEL_H - 1, EIDOS_MOD_PANEL_W, 1, C_EDGE);
    fill(pic, 0, 0, 1, EIDOS_MOD_PANEL_H, C_EDGE);
    fill(pic, EIDOS_MOD_PANEL_W - 1, 0, 1, EIDOS_MOD_PANEL_H, C_EDGE);
    for (line = 0; line < lines; line++) {
        const char *t = bw_mod_panel_shown_line(line);

        if (t == NULL || t[0] == '\0') {
            continue;
        }
        text(pic, MARGIN, MARGIN + (int)line * EIDOS_FONT_LINE, t);
    }
}

/* ------------------------------------------------------------------ */
/* The panel's place in the arrangement                                */
/* ------------------------------------------------------------------ */

/*
 * The panel's rectangle in the arrangement, in DS pixels.
 *
 * The arrangement is the two screens and the panel together, so the panel
 * is placed against the screens and the arrangement grows by the panel's
 * size: the picture is at the arrangement's own scale, which is the scale
 * the game's picture is at.
 *
 *   - the screens are stacked (the second is below the first): the panel
 *     is on their right;
 *   - the screens are side by side: the panel is below them, in the
 *     middle.
 *
 * A panel the player has hidden takes no space at all: the arrangement is
 * then the two screens alone, and the window keeps its own shape.  The
 * player's choice changes the window's shape when it changes (the
 * settings' apply calls eidos_platform_set_layout, as it does for the
 * layout options).
 */
void eidos_mod_panel_space(const EidosScreenPlacement *pl,
                           EidosModPanelSpace *out)
{
    int left, right, bottom, stacked;

    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (pl == NULL || !eidos_mod_panel_visible()) {
        return;
    }

    left = pl->screen[0].x < pl->screen[1].x ? pl->screen[0].x : pl->screen[1].x;
    right = pl->screen[0].x + pl->screen[0].w;
    if (pl->screen[1].x + pl->screen[1].w > right) {
        right = pl->screen[1].x + pl->screen[1].w;
    }
    bottom = pl->screen[0].y + pl->screen[0].h;
    if (pl->screen[1].y + pl->screen[1].h > bottom) {
        bottom = pl->screen[1].y + pl->screen[1].h;
    }
    /* The screens are stacked when they are in the same column, whichever
     * of them is the first. */
    stacked = pl->screen[0].x == pl->screen[1].x;

    if (stacked) {
        out->rect.x = right;
        out->rect.y = pl->screen[0].y;
        out->rect.w = EIDOS_MOD_PANEL_W;
        out->rect.h = EIDOS_MOD_PANEL_H;
        out->grow_w = EIDOS_MOD_PANEL_W;
    } else {
        out->rect.x = left + (right - left - EIDOS_MOD_PANEL_W) / 2;
        out->rect.y = bottom;
        out->rect.w = EIDOS_MOD_PANEL_W;
        out->rect.h = EIDOS_MOD_PANEL_H;
        out->grow_h = EIDOS_MOD_PANEL_H;
    }
}

/* ------------------------------------------------------------------ */
/* The panel's picture and where it goes                               */
/* ------------------------------------------------------------------ */

/*
 * The panel's picture and the rectangle it goes to in the output.
 *
 * The rectangle is the panel's rectangle in the arrangement, mapped
 * through the viewport the way the screens' rectangles are (the same
 * arithmetic as eidos_display_viewport): the arrangement's area in the
 * output is the viewport's content rectangle.
 */
int eidos_mod_panel_view(EidosModPanelView *view, const EidosViewport *vp,
                         const EidosModPanelSpace *space,
                         int arr_w, int arr_h)
{
    u32 gen;

    if (view == NULL || vp == NULL || space == NULL) {
        return -1;
    }
    if (!eidos_mod_panel_visible()) {
        return -1;                 /* the player hid the panel */
    }
    if (bw_mod_panel_shown_used() == 0) {
        return -1;                 /* no mod has filled a line */
    }
    if (arr_w <= 0 || arr_h <= 0 || vp->content.w <= 0 || vp->content.h <= 0) {
        return -1;
    }

    gen = bw_mod_panel_generation();
    if (!g_drawn_ok || gen != g_drawn) {
        render(g_picture);
        g_drawn = gen;
        g_drawn_ok = 1;
    }

    view->rgba = g_picture;
    view->w = EIDOS_MOD_PANEL_W;
    view->h = EIDOS_MOD_PANEL_H;
    view->generation = g_drawn;
    view->dst.x = vp->content.x
                  + (int)((s64)space->rect.x * (s64)vp->content.w / (s64)arr_w);
    view->dst.y = vp->content.y
                  + (int)((s64)space->rect.y * (s64)vp->content.h / (s64)arr_h);
    view->dst.w = (int)((s64)space->rect.w * (s64)vp->content.w / (s64)arr_w);
    view->dst.h = (int)((s64)space->rect.h * (s64)vp->content.h / (s64)arr_h);
    return 0;
}

/* ------------------------------------------------------------------ */
/* The player's choice                                                 */
/* ------------------------------------------------------------------ */

static int g_visible = 1;

int eidos_mod_panel_visible(void)
{
    return g_visible;
}

void eidos_mod_panel_set_visible(int visible)
{
    g_visible = (visible != 0) ? 1 : 0;
}
