/*
 * tests/test_mod_panel.c -- the run-status panel: the model's sixteen lines
 * and the picture the port draws beside the two screens.
 *
 * The panel is the port's own small text panel, filled by a mod through the
 * services table (sdk/mod_panel.h): deaths, encounters by route, the level
 * cap, a timer.  The model keeps the lines (sdk/mod_panel.c) and the drawing
 * (engine/platform/mod_panel_draw.c) puts them in a picture the presenter
 * shows beside the DS screens, so the panel never covers the game.
 *
 * What is checked here:
 *
 *   - the model: one panel per mod, so two mods that both fill the panel do
 *     not share one; a line out of range is ignored; a line set empty is
 *     cleared; the clear empties every line of the calling mod's panel and
 *     leaves the other mods' panels alone; the port may name the panel it
 *     draws; the port's own line (the timer and the splits) is never drawn
 *     on a line the mod has used; and the generation grows only when a line
 *     really changed;
 *   - the picture: the model's text is on the picture, the picture is drawn
 *     again when the model changed, and the view carries the model's own
 *     generation, so the presenter uploads it only then;
 *   - the place: the panel's rectangle lies outside the screens' area and
 *     inside the arrangement, in the stacked layout and in the side-by-side
 *     one;
 *   - a panel the player has hidden takes no space in the arrangement, and
 *     there is no picture to show;
 *   - the port's own reset (bw_port_reset) empties the panel.
 *
 * Nothing here needs game data: the model is text, the picture is the port's
 * own font drawn on the CPU, and the arrangement is the port's own geometry.
 *
 * What this test does not cover: the services table the loader binds the two
 * calls into (sdk/mod_loader.c binds the very functions this test calls), and
 * the file the splits are written to at the end of a run.
 */

#include <stdio.h>
#include <string.h>

#include "font.h"
#include "mod_panel.h"
#include "mod_panel_draw.h"
#include "platform.h"
#include "port.h"
#include "types.h"

#include "port_test.h"

/*
 * The saved-data layer's own calls (sdk/mod_savedata.c has no header of its
 * own; its prototypes live at the top of that file).  The loader puts the
 * mod's namespace in force around every call it makes into a mod, and the
 * panel's model asks the layer which mod is calling, so the test does the
 * same to be a mod.
 */
extern void        bw_mod_savedata_set_namespace(const char *ns);
extern const char *bw_mod_savedata_namespace(void);

/* ------------------------------------------------------------------ */
/* The test's own names                                                */
/* ------------------------------------------------------------------ */

/* The mods the test's panels belong to. */
#define T_NS_A "mod_a"
#define T_NS_B "mod_b"
#define T_NS_C "mod_c"

/* How many lines a panel has. */
#define T_LINES BW_MOD_PANEL_LINES

/* The output the arrangement is shown in, in pixels. */
#define T_OUT_W 800
#define T_OUT_H 600

/* ------------------------------------------------------------------ */
/* The picture's own colours                                           */
/* ------------------------------------------------------------------ */

/*
 * The drawing's own colours (engine/platform/mod_panel_draw.c): a dark
 * ground, a blue edge and a light text, in R, G, B, A order.  The text's
 * colour is what says the picture's bytes are in that order: the ground and
 * the edge have their own bytes, and a picture in another order would not
 * carry the text's colour at all.
 */
#define T_EDGE_R   0x4Fu
#define T_EDGE_G   0x6Fu
#define T_EDGE_B   0xB0u
#define T_GROUND_R 0x1Eu
#define T_GROUND_G 0x22u
#define T_GROUND_B 0x30u
#define T_TEXT_R   0xE8u
#define T_TEXT_G   0xEAu
#define T_TEXT_B   0xF0u
#define T_ALPHA    0xFFu

/* ------------------------------------------------------------------ */
/* The helpers                                                         */
/* ------------------------------------------------------------------ */

/* A copy of the picture, to compare one draw with the next. */
static u8 g_picture_copy[EIDOS_MOD_PANEL_W * EIDOS_MOD_PANEL_H * 4];

/* Whether two rectangles share a pixel. */
static int rects_overlap(EidosRect a, EidosRect b)
{
    return (a.x < b.x + b.w && b.x < a.x + a.w &&
            a.y < b.y + b.h && b.y < a.y + a.h);
}

/* The picture's pixel at (x, y). */
static const u8 *pixel_at(const u8 *pic, int w, int x, int y)
{
    return pic + ((size_t)y * (size_t)w + (size_t)x) * 4u;
}

/* Whether the picture's pixel at (x, y) carries the given colour. */
static int pixel_is(const u8 *pic, int w, int h, int x, int y,
                    u8 r, u8 g, u8 b, u8 a)
{
    const u8 *p;

    if (x < 0 || y < 0 || x >= w || y >= h) {
        return 0;
    }
    p = pixel_at(pic, w, x, y);
    return (p[0] == r && p[1] == g && p[2] == b && p[3] == a);
}

/* How many of the picture's pixels carry the text's colour. */
static int picture_text_pixels(const u8 *pic, int w, int h)
{
    int x, y, n = 0;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            if (pixel_is(pic, w, h, x, y,
                         T_TEXT_R, T_TEXT_G, T_TEXT_B, T_ALPHA)) {
                n++;
            }
        }
    }
    return n;
}

/* The first of the picture's text pixels: where it is, and whether there is
 * one at all. */
static int picture_first_text(const u8 *pic, int w, int h, int *x, int *y)
{
    int cx, cy;

    for (cy = 0; cy < h; cy++) {
        for (cx = 0; cx < w; cx++) {
            if (pixel_is(pic, w, h, cx, cy,
                         T_TEXT_R, T_TEXT_G, T_TEXT_B, T_ALPHA)) {
                *x = cx;
                *y = cy;
                return 1;
            }
        }
    }
    return 0;
}

/*
 * The rows the picture's text is in: the first row that carries a text
 * pixel, and the first row of the next group of text (a row that carries a
 * text pixel and follows a row that carries none).  A panel with text on
 * its first and on its third line has two groups of rows, and the second
 * group starts two lines below the first.
 */
static void picture_text_rows(const u8 *pic, int w, int h, int *first, int *second)
{
    int x, y;
    int f = -1, s = -1;
    int prev = 0;                    /* the row before: text or no text */

    for (y = 0; y < h && s < 0; y++) {
        int any = 0;

        for (x = 0; x < w; x++) {
            if (pixel_is(pic, w, h, x, y,
                         T_TEXT_R, T_TEXT_G, T_TEXT_B, T_ALPHA)) {
                any = 1;
                break;
            }
        }
        if (any && f < 0) {
            f = y;                   /* the first row of text */
        } else if (any && !prev) {
            s = y;                   /* the first row of the next group */
        }
        prev = any;
    }
    *first = f;
    *second = s;
}

/* The number of lit pixels of one character in the port's font. */
static int glyph_pixels(u32 ch)
{
    const u8 *rows = eidos_font_glyph(ch);
    int r, n = 0;

    for (r = 0; r < EIDOS_FONT_ROWS; r++) {
        u8 bits = (u8)(rows[r] & 0x1Fu);  /* the five columns of the cell */

        while (bits != 0) {
            n += (int)(bits & 1u);
            bits = (u8)(bits >> 1);
        }
    }
    return n;
}

/* The number of lit pixels of a text in the port's font. */
static int text_pixels(const char *s)
{
    u32 ch;
    int n = 0;

    while ((ch = eidos_utf8_next(&s)) != 0) {
        n += glyph_pixels(ch);
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* The test                                                            */
/* ------------------------------------------------------------------ */

int main(void)
{
    EidosScreenPlacement pl;
    EidosViewport vp;
    EidosModPanelSpace space;
    EidosModPanelView view;
    u32 gen;
    u32 i;
    int line, line2;
    int r0, r1, tx, ty;
    char text[32];

    printf("one panel per mod\n");
    bw_mod_panel_reset();
    BW_CHECK_EQ("the panel is empty at the start",
                (long)bw_mod_panel_shown_used(), 0);
    BW_CHECK_EQ("a panel has sixteen lines",
                (long)bw_mod_panel_line_count(), (long)T_LINES);

    bw_mod_savedata_set_namespace(T_NS_A);
    BW_CHECK_EQ("the mod in force is the one the loader named",
                strcmp(bw_mod_savedata_namespace(), T_NS_A), 0);
    bw_mod_panel_set(0, "deaths 3");
    bw_mod_savedata_set_namespace(T_NS_B);
    bw_mod_panel_set(0, "route 201");
    bw_mod_savedata_set_namespace(T_NS_A);

    BW_CHECK_EQ("the first mod's line is its own",
                strcmp(bw_mod_panel_line_for(T_NS_A, 0), "deaths 3"), 0);
    BW_CHECK_EQ("the second mod's line is its own",
                strcmp(bw_mod_panel_line_for(T_NS_B, 0), "route 201"), 0);
    BW_CHECK_EQ("the first mod's panel holds one line",
                (long)bw_mod_panel_used_for(T_NS_A), 1);
    BW_CHECK_EQ("the second mod's panel holds one line",
                (long)bw_mod_panel_used_for(T_NS_B), 1);
    BW_CHECK_EQ("and the port's own panel is empty",
                (long)bw_mod_panel_used_for(BW_MOD_PANEL_PORT_NS), 0);

    printf("a line out of range\n");
    gen = bw_mod_panel_generation();
    bw_mod_panel_set(T_LINES, "too far");
    bw_mod_panel_set(0xFFFFFFFFu, "far too far");
    BW_CHECK_EQ("a line out of range is ignored",
                (long)bw_mod_panel_generation(), (long)gen);
    BW_CHECK_EQ("and the panel still holds one line",
                (long)bw_mod_panel_used_for(T_NS_A), 1);
    bw_mod_panel_set_for(T_NS_A, T_LINES, "too far");
    BW_CHECK_EQ("the port's own call ignores it too",
                (long)bw_mod_panel_used_for(T_NS_A), 1);
    BW_CHECK_EQ("and leaves the panel as it was",
                strcmp(bw_mod_panel_line_for(T_NS_A, 0), "deaths 3"), 0);

    printf("a line set empty\n");
    bw_mod_panel_set(0, "");
    BW_CHECK_EQ("a line set empty is cleared",
                (long)bw_mod_panel_used_for(T_NS_A), 0);
    BW_CHECK_EQ("and it carries no text",
                strcmp(bw_mod_panel_line_for(T_NS_A, 0), ""), 0);
    bw_mod_panel_set(0, "deaths 3");
    bw_mod_panel_set(0, NULL);
    BW_CHECK_EQ("and a line set with no text at all is cleared too",
                (long)bw_mod_panel_used_for(T_NS_A), 0);
    bw_mod_panel_set(0, "deaths 3");

    printf("the clear\n");
    bw_mod_panel_set(1, "level cap 50");
    bw_mod_panel_set(2, "encounters 12");
    BW_CHECK_EQ("the panel holds three lines",
                (long)bw_mod_panel_used_for(T_NS_A), 3);
    bw_mod_panel_clear();
    BW_CHECK_EQ("the clear empties every line",
                (long)bw_mod_panel_used_for(T_NS_A), 0);
    BW_CHECK_EQ("and the other mod's panel is untouched",
                (long)bw_mod_panel_used_for(T_NS_B), 1);

    printf("the panel the port draws\n");
    bw_mod_panel_set(0, "deaths 3");
    BW_CHECK_EQ("the port draws the first panel with a line in it",
                strcmp(bw_mod_panel_shown(), T_NS_A), 0);
    BW_CHECK_EQ("with the mod's own line on it",
                strcmp(bw_mod_panel_shown_line(0), "deaths 3"), 0);
    BW_CHECK_EQ("the port may name the panel it draws",
                (long)bw_mod_panel_select(T_NS_B), 0);
    BW_CHECK_EQ("and the named panel is the one drawn",
                strcmp(bw_mod_panel_shown(), T_NS_B), 0);
    BW_CHECK_EQ("with the other mod's own line on it",
                strcmp(bw_mod_panel_shown_line(0), "route 201"), 0);
    BW_CHECK_EQ("a name that is dropped brings the port's own choice back",
                (long)bw_mod_panel_select(NULL), 0);
    BW_CHECK_EQ("so the first panel with a line in it is drawn",
                strcmp(bw_mod_panel_shown(), T_NS_A), 0);

    printf("the port's own panel\n");
    bw_mod_panel_clear();
    bw_mod_savedata_set_namespace(T_NS_B);
    bw_mod_panel_clear();
    bw_mod_savedata_set_namespace("");
    bw_mod_panel_set(0, "port's own line");
    BW_CHECK_EQ("the port's own panel is drawn when it is the only one",
                strcmp(bw_mod_panel_shown(), BW_MOD_PANEL_PORT_NS), 0);
    BW_CHECK_EQ("and its line is drawn",
                strcmp(bw_mod_panel_shown_line(0), "port's own line"), 0);

    printf("the port's own lines\n");
    bw_mod_savedata_set_namespace(T_NS_A);
    bw_mod_panel_set(0, "deaths 3");
    bw_mod_panel_set(2, "encounters 12");
    line = bw_mod_panel_set_port(T_NS_A, 0, "Time 0:12");
    BW_CHECK_EQ("the port's own line is on a line the mod has not used",
                (long)line, 1);
    BW_CHECK_EQ("and the mod's own line is not overwritten",
                strcmp(bw_mod_panel_line_for(T_NS_A, 0), "deaths 3"), 0);
    BW_CHECK_EQ("the port's own line is on the line it was given",
                strcmp(bw_mod_panel_line_for(T_NS_A, (u32)line), "Time 0:12"), 0);
    line2 = bw_mod_panel_set_port(T_NS_A, 1, "0:12 Route 201");
    BW_CHECK_EQ("and the next one on the next line the mod has not used",
                (long)line2, 3);
    BW_CHECK_EQ("the panel holds the port's own lines",
                (long)bw_mod_panel_port_count(T_NS_A), 2);

    gen = bw_mod_panel_generation();
    line2 = bw_mod_panel_set_port(T_NS_A, 0, "Time 0:20");
    BW_CHECK_EQ("the port's own line holds its place",
                (long)line2, (long)line);
    gen = bw_mod_panel_generation();
    bw_mod_panel_set_port(T_NS_A, 0, "Time 0:20");
    BW_CHECK_EQ("and the same text again does not make the picture stale",
                (long)bw_mod_panel_generation(), (long)gen);

    bw_mod_panel_clear();
    BW_CHECK_EQ("the clear empties the mod's own lines",
                strcmp(bw_mod_panel_line_for(T_NS_A, 0), ""), 0);
    BW_CHECK_EQ("and the port's own line is left alone",
                strcmp(bw_mod_panel_line_for(T_NS_A, (u32)line), "Time 0:20"), 0);

    printf("a panel with no room\n");
    bw_mod_savedata_set_namespace(T_NS_C);
    for (i = 0; i < T_LINES; i++) {
        snprintf(text, sizeof text, "line %u", (unsigned int)i);
        bw_mod_panel_set(i, text);
    }
    BW_CHECK_EQ("the third mod's panel holds every line",
                (long)bw_mod_panel_used_for(T_NS_C), (long)T_LINES);
    BW_CHECK_EQ("the port's own line has no room there",
                (long)bw_mod_panel_set_port(T_NS_C, 0, "Time 0:00"), -1);
    BW_CHECK_EQ("and the mod's own lines are untouched",
                strcmp(bw_mod_panel_line_for(T_NS_C, 0), "line 0"), 0);

    printf("the generation\n");
    bw_mod_savedata_set_namespace(T_NS_A);
    bw_mod_panel_set(0, "deaths 3");
    gen = bw_mod_panel_generation();
    bw_mod_panel_set(0, "deaths 3");
    BW_CHECK_EQ("the same text does not make the picture stale",
                (long)bw_mod_panel_generation(), (long)gen);
    bw_mod_panel_set(0, "deaths 4");
    BW_CHECK_EQ("a changed line does",
                (long)(bw_mod_panel_generation() > gen), 1);
    gen = bw_mod_panel_generation();
    bw_mod_panel_set(0, "deaths 4");
    BW_CHECK_EQ("and the same text again does not",
                (long)bw_mod_panel_generation(), (long)gen);
    bw_mod_panel_set(T_LINES, "too far");
    BW_CHECK_EQ("and a line out of range does not either",
                (long)bw_mod_panel_generation(), (long)gen);

    printf("the picture\n");
    bw_mod_panel_reset();
    bw_mod_savedata_set_namespace(T_NS_A);
    bw_mod_panel_set(0, "AB");
    bw_mod_panel_set(2, "CD");
    BW_CHECK_EQ("the panel has two lines in it",
                (long)bw_mod_panel_shown_used(), 2);

    eidos_layout_place(EIDOS_LAYOUT_STACKED, 0, 0, &pl);
    eidos_display_viewport(&pl, T_OUT_W, T_OUT_H, &vp);
    eidos_mod_panel_space(&pl, &space);
    BW_REQUIRE("there is a picture to show",
               eidos_mod_panel_view(&view, &vp, &space,
                                    pl.width, pl.height) == 0);
    BW_CHECK_EQ("the picture is the panel's own width",
                (long)view.w, (long)EIDOS_MOD_PANEL_W);
    BW_CHECK_EQ("and its height",
                (long)view.h, (long)EIDOS_MOD_PANEL_H);
    BW_CHECK_EQ("the picture carries the model's text",
                (long)picture_text_pixels(view.rgba, view.w, view.h),
                (long)(text_pixels("AB") + text_pixels("CD")));
    BW_REQUIRE("the picture has a text pixel",
               picture_first_text(view.rgba, view.w, view.h, &tx, &ty));
    BW_CHECK_EQ("and the text is the light colour, in R, G, B, A order",
                (long)pixel_is(view.rgba, view.w, view.h, tx, ty,
                               T_TEXT_R, T_TEXT_G, T_TEXT_B, T_ALPHA), 1);
    BW_CHECK_EQ("the picture's edge is the border",
                (long)pixel_is(view.rgba, view.w, view.h, 0, 0,
                               T_EDGE_R, T_EDGE_G, T_EDGE_B, T_ALPHA), 1);
    BW_CHECK_EQ("and its ground is the dark one",
                (long)pixel_is(view.rgba, view.w, view.h, view.w / 2,
                               view.h - 3,
                               T_GROUND_R, T_GROUND_G, T_GROUND_B, T_ALPHA), 1);
    picture_text_rows(view.rgba, view.w, view.h, &r0, &r1);
    BW_CHECK_EQ("the two lines are two lines apart",
                (long)(r1 - r0), (long)(2 * EIDOS_FONT_LINE));
    BW_CHECK_EQ("the picture is drawn from the model in force",
                (long)view.generation, (long)bw_mod_panel_generation());

    memcpy(g_picture_copy, view.rgba, sizeof g_picture_copy);
    gen = view.generation;
    bw_mod_panel_set(0, "XY");
    BW_CHECK_EQ("the model changed",
                (long)(bw_mod_panel_generation() > gen), 1);
    BW_REQUIRE("the panel is still there",
               eidos_mod_panel_view(&view, &vp, &space,
                                    pl.width, pl.height) == 0);
    BW_CHECK_EQ("the picture is drawn again",
                (long)(memcmp(g_picture_copy, view.rgba,
                              sizeof g_picture_copy) != 0), 1);
    BW_CHECK_EQ("and it carries the new text",
                (long)picture_text_pixels(view.rgba, view.w, view.h),
                (long)(text_pixels("XY") + text_pixels("CD")));
    BW_CHECK_EQ("and the view says which model it is from",
                (long)view.generation, (long)bw_mod_panel_generation());

    memcpy(g_picture_copy, view.rgba, sizeof g_picture_copy);
    gen = view.generation;
    bw_mod_panel_set(0, "XY");
    BW_CHECK_EQ("the model did not change",
                (long)(bw_mod_panel_generation() == gen), 1);
    BW_REQUIRE("the panel is still there",
               eidos_mod_panel_view(&view, &vp, &space,
                                    pl.width, pl.height) == 0);
    BW_CHECK_EQ("so the picture does not move",
                (long)(memcmp(g_picture_copy, view.rgba,
                              sizeof g_picture_copy) == 0), 1);
    BW_CHECK_EQ("and the view carries the same model",
                (long)view.generation, (long)gen);

    printf("the panel's place\n");
    eidos_layout_place(EIDOS_LAYOUT_STACKED, 0, 0, &pl);
    eidos_mod_panel_space(&pl, &space);
    BW_CHECK_EQ("the arrangement grows for the panel",
                (long)pl.width,
                (long)(EIDOS_DS_SCREEN_WIDTH + EIDOS_MOD_PANEL_W));
    BW_CHECK_EQ("the panel is beside the screens",
                (long)(space.rect.x >= (int)EIDOS_DS_SCREEN_WIDTH), 1);
    BW_CHECK_EQ("the panel does not cover the first screen",
                (long)rects_overlap(space.rect, pl.screen[0]), 0);
    BW_CHECK_EQ("nor the second",
                (long)rects_overlap(space.rect, pl.screen[1]), 0);
    BW_CHECK_EQ("and the panel is inside the arrangement",
                (long)(space.rect.x >= 0 &&
                       space.rect.y >= 0 &&
                       space.rect.x + space.rect.w <= pl.width &&
                       space.rect.y + space.rect.h <= pl.height), 1);

    eidos_layout_place(EIDOS_LAYOUT_SIDE_BY_SIDE, 0, 0, &pl);
    eidos_mod_panel_space(&pl, &space);
    BW_CHECK_EQ("the arrangement grows downwards for the panel",
                (long)pl.height,
                (long)(EIDOS_DS_SCREEN_HEIGHT + EIDOS_MOD_PANEL_H));
    BW_CHECK_EQ("the panel is below the screens",
                (long)(space.rect.y >= (int)EIDOS_DS_SCREEN_HEIGHT), 1);
    BW_CHECK_EQ("the panel does not cover the first screen",
                (long)rects_overlap(space.rect, pl.screen[0]), 0);
    BW_CHECK_EQ("nor the second",
                (long)rects_overlap(space.rect, pl.screen[1]), 0);
    BW_CHECK_EQ("and the panel is inside the arrangement",
                (long)(space.rect.x >= 0 &&
                       space.rect.y >= 0 &&
                       space.rect.x + space.rect.w <= pl.width &&
                       space.rect.y + space.rect.h <= pl.height), 1);

    eidos_display_viewport(&pl, T_OUT_W, T_OUT_H, &vp);
    BW_REQUIRE("the picture is shown",
               eidos_mod_panel_view(&view, &vp, &space,
                                    pl.width, pl.height) == 0);
    BW_CHECK_EQ("the picture is not over the first screen",
                (long)rects_overlap(view.dst, vp.screen[0]), 0);
    BW_CHECK_EQ("nor the second",
                (long)rects_overlap(view.dst, vp.screen[1]), 0);
    BW_CHECK_EQ("and it is inside the picture's own area",
                (long)(view.dst.x >= vp.content.x &&
                       view.dst.y >= vp.content.y &&
                       view.dst.x + view.dst.w <= vp.content.x + vp.content.w &&
                       view.dst.y + view.dst.h <= vp.content.y + vp.content.h), 1);

    printf("a hidden panel\n");
    eidos_mod_panel_set_visible(0);
    eidos_layout_place(EIDOS_LAYOUT_STACKED, 0, 0, &pl);
    eidos_mod_panel_space(&pl, &space);
    BW_CHECK_EQ("the arrangement is the two screens alone",
                (long)pl.width, (long)EIDOS_DS_SCREEN_WIDTH);
    BW_CHECK_EQ("and its height too",
                (long)pl.height, (long)(2 * EIDOS_DS_SCREEN_HEIGHT));
    BW_CHECK_EQ("the panel takes no space",
                (long)space.rect.w, 0);
    BW_CHECK_EQ("and the arrangement does not grow for it",
                (long)(space.grow_w == 0 && space.grow_h == 0), 1);
    eidos_display_viewport(&pl, T_OUT_W, T_OUT_H, &vp);
    BW_CHECK_EQ("and there is no picture to show",
                (long)eidos_mod_panel_view(&view, &vp, &space,
                                           pl.width, pl.height), -1);
    eidos_mod_panel_set_visible(1);

    printf("a panel with no line in it\n");
    bw_mod_panel_reset();
    eidos_layout_place(EIDOS_LAYOUT_STACKED, 0, 0, &pl);
    eidos_display_viewport(&pl, T_OUT_W, T_OUT_H, &vp);
    eidos_mod_panel_space(&pl, &space);
    BW_CHECK_EQ("the arrangement is the two screens alone",
                (long)pl.width, (long)EIDOS_DS_SCREEN_WIDTH);
    BW_CHECK_EQ("and there is no picture to show",
                (long)eidos_mod_panel_view(&view, &vp, &space,
                                           pl.width, pl.height), -1);

    printf("the port's own reset\n");
    bw_mod_savedata_set_namespace(T_NS_A);
    bw_mod_panel_set(0, "deaths 3");
    bw_mod_panel_set_port(T_NS_A, 0, "Time 0:12");
    BW_REQUIRE("the panel has a line in it",
               bw_mod_panel_shown_used() > 0);
    bw_port_reset();
    BW_CHECK_EQ("the reset empties the panel",
                (long)bw_mod_panel_shown_used(), 0);
    BW_CHECK_EQ("and the mod's own line is gone",
                strcmp(bw_mod_panel_line_for(T_NS_A, 0), ""), 0);
    BW_CHECK_EQ("and the port's own line is gone",
                (long)bw_mod_panel_port_count(T_NS_A), 0);
    BW_CHECK_EQ("and the choice of the panel is dropped",
                strcmp(bw_mod_panel_shown(), ""), 0);
    bw_mod_savedata_set_namespace("");

    BW_REPORT("test_mod_panel");
}
