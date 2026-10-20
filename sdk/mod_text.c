/*
 * sdk/mod_text.c -- the port's own text path: the side of the message
 * service that puts the text on the screen (docs/mod-services.md,
 * "Messages"; sdk/mod_message.h, "The port's own text path").
 *
 * What this file owns
 * -------------------
 *
 * The message service (sdk/mod_message.c) owns the queue and the
 * encoding.  It does not know how a text is shown: it asks through its
 * sink, and the sink's three calls are the game's own three jobs with a
 * text.  This file is the port's side of that sink.
 *
 *   show  the text goes into the game's own window, in the game's font:
 *         the string object is built out of the code units and the end
 *         code, and it is printed with the game's own print queue.
 *   ask   the question goes into the game's own yes/no window and the
 *         player's answer comes back.
 *   tick  the game advances to the next point where it would accept a
 *         message.
 *
 * The show step
 * -------------
 *
 * The queue is drained at the point where the game would accept a
 * message: the hook (a port source that stands in for a matched overlay
 * file, or the platform layer) opens the game's own window, hands the
 * port its three parts with bw_mod_text_set_window(), and calls
 * bw_mod_text_pump().  The pump takes the next queued text and the
 * sink's show prints it into that window.
 *
 * The field's message window and the field's yes/no window are overlay
 * 21's (src/game/ov021/field_msgbg.c) and the battle's message box is
 * overlay 93's (src/game/ov093/btlv_core.c), so the window and the
 * yes/no window are the hook's to hand over: the ARM9 cannot call into
 * an overlay.  The font, the string object and the print are the game's
 * own functions (src/game/arm9/), and they are here.
 *
 * The font's own table
 * --------------------
 *
 * A character the game font lacks shows as '?' (docs/mod-services.md).
 * The port's own text path answers the service's question with the
 * game's own answer: the font's glyph table (GF_FONT_GetGlyphIndex)
 * gives a character's glyph, and the glyph of '?' is the one a missing
 * character is shown as.  Without the game there is no font, and the
 * answer is the built-in one the service keeps (the printable ASCII
 * range).
 *
 * The port's own functions are weak
 * ---------------------------------
 *
 * Every game function below is weak, the way sdk/mod_state.c and
 * sdk/mod_event_field.c declare theirs: the library is linked by the
 * tests, which have no game code in them, and a weak declaration lets
 * the link succeed without it.  Each call is made only after the name
 * has been checked, so a link without the game reports that there is no
 * text path instead of calling into nothing.
 */

#include "mod_text.h"
#include "mod_message.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* The game's own text functions                                       */
/* ------------------------------------------------------------------ */

/*
 * The game's own functions, each with the address the corpus gives it.
 * They are weak so that a link without the game still succeeds; see the
 * file header.
 */
#if defined(__GNUC__) || defined(__clang__)
#define BW_PORT_WEAK __attribute__((weak))
#else
#define BW_PORT_WEAK
#endif

/* The game's own objects, as the game names them. */
struct Strbuf;
struct PrintQueue;
struct Bmp;
struct GfFont;
struct BmpWin;

extern struct Strbuf *STRBUF_Create(u32 capacity, u32 heapId)
    BW_PORT_WEAK;                                     /* 0x020457B0 */
extern void STRBUF_SetStringN(struct Strbuf *strbuf, const u16 *srcChars,
                               u32 charCount)
    BW_PORT_WEAK;                                     /* 0x02045994 */
extern void STRBUF_Delete(struct Strbuf *strbuf)
    BW_PORT_WEAK;                                     /* 0x02045808 */
extern void PRINTSYS_PrintQueColor(struct PrintQueue *queue, struct Bmp *bmp,
                                   u32 x, s16 y, struct Strbuf *str,
                                   struct GfFont *font, u16 color)
    BW_PORT_WEAK;                                     /* 0x0201C724 */
extern struct Bmp *BMP_WIN_GetBmp(struct BmpWin *bmpWin)
    BW_PORT_WEAK;                                     /* 0x02045770 */
extern struct GfFont *GF_FONT_Create(int archiveId, int fileId, int format,
                                     int isFixedWidth, u32 heapId)
    BW_PORT_WEAK;                                     /* 0x0201D7EC */
extern u32 GF_FONT_Delete(struct GfFont *font)
    BW_PORT_WEAK;                                     /* 0x0201D83C */
extern u32 GF_FONT_GetGlyphIndex(struct GfFont *font, u16 ch)
    BW_PORT_WEAK;                                     /* 0x0201DB20 */

/* ------------------------------------------------------------------ */
/* The port's own constants                                            */
/* ------------------------------------------------------------------ */

/*
 * The font the port's own text path uses when the window has none of
 * its own: arc 0x17 (a/0/2/3), member 0.  It is the font the field's
 * message window uses (src/game/ov021/field_msgbg.c) and the one the
 * port's own screens use (engine/platform/mod_options_proc.c).
 */
#define BW_MOD_TEXT_FONT_ARC     0x17
#define BW_MOD_TEXT_FONT_MEMBER  0

/*
 * The colour the game's own text is drawn in: the letter colour 1, the
 * shadow colour 2 and no background, which is the default the font sets
 * for itself (src/game/arm9/gf_font.c, GF_FONT_InitTextColor).  The
 * colour word is five bits per colour, the letter first.
 */
#define BW_MOD_TEXT_COLOUR       0x0440u

/* ------------------------------------------------------------------ */
/* The state                                                           */
/* ------------------------------------------------------------------ */

/* The window the text goes into. */
static BwModTextWindow g_window;

/* The draw side: the yes/no window and the game's advance. */
static BwModTextDraw g_draw;

/* The port's own font, and the heap it was made in. */
static struct GfFont *g_font;
static u32           g_font_heap;

/* ------------------------------------------------------------------ */
/* The draw side                                                        */
/* ------------------------------------------------------------------ */

void bw_mod_text_set_draw(const BwModTextDraw *draw)
{
    if (draw == NULL) {
        memset(&g_draw, 0, sizeof(g_draw));
        return;
    }
    g_draw = *draw;
}

/* ------------------------------------------------------------------ */
/* The window                                                           */
/* ------------------------------------------------------------------ */

void bw_mod_text_set_window(const BwModTextWindow *win)
{
    if (win == NULL) {
        memset(&g_window, 0, sizeof(g_window));
        return;
    }
    g_window = *win;
}

const BwModTextWindow *bw_mod_text_window(void)
{
    return &g_window;
}

/* ------------------------------------------------------------------ */
/* The font                                                             */
/* ------------------------------------------------------------------ */

void *bw_mod_text_font(u32 heap)
{
    if (GF_FONT_Create == NULL) {
        return NULL;
    }
    if (g_font != NULL && g_font_heap == heap) {
        return g_font;
    }
    if (g_font != NULL) {
        /* The font was made in another heap: the old one goes with it. */
        if (GF_FONT_Delete != NULL) {
            (void)GF_FONT_Delete(g_font);
        }
        g_font = NULL;
    }
    g_font = GF_FONT_Create(BW_MOD_TEXT_FONT_ARC, BW_MOD_TEXT_FONT_MEMBER, 0, 0,
                            heap);
    g_font_heap = heap;
    return g_font;
}

int bw_mod_text_font_has(u32 codepoint)
{
    struct GfFont *font = g_font;
    u32 index;
    u32 missing;

    if (font == NULL || GF_FONT_GetGlyphIndex == NULL) {
        /*
         * No game, so no font and no glyph table: the answer is the
         * built-in one the message service keeps (the printable ASCII
         * range), so a text reads the same with and without the game.
         */
        return (codepoint >= 0x20u && codepoint <= 0x7Eu) ? 1 : 0;
    }
    if (codepoint > 0xFFFFu) {
        return 0;
    }
    index = GF_FONT_GetGlyphIndex(font, (u16)codepoint);
    missing = GF_FONT_GetGlyphIndex(font, (u16)BW_MOD_MESSAGE_UNKNOWN);
    return index != missing ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* The show                                                             */
/* ------------------------------------------------------------------ */

int bw_mod_text_print(const BwModTextWindow *win, const u16 *units, u32 count)
{
    u16 buf[BW_MOD_MESSAGE_MAX_UNITS + 1];
    struct Strbuf *str;
    struct GfFont *font;
    u32 i;

    if (win == NULL || units == NULL || count == 0u) {
        return -1;
    }
    if (count > BW_MOD_MESSAGE_MAX_UNITS) {
        count = BW_MOD_MESSAGE_MAX_UNITS;
    }
    if (win->print_que == NULL || win->bmp_win == NULL) {
        return -1;
    }
    if (STRBUF_Create == NULL || STRBUF_SetStringN == NULL ||
        STRBUF_Delete == NULL || PRINTSYS_PrintQueColor == NULL ||
        BMP_WIN_GetBmp == NULL) {
        /* No game: there is no print system to print into. */
        return -1;
    }
    /*
     * The game's string object: the code units, then the end code.  The
     * run the service hands over does not carry the end code; the port
     * adds it here, when the object is built (the shape of
     * engine/platform/mod_options_proc.c's ascii_obj).
     */
    for (i = 0; i < count; i++) {
        buf[i] = units[i];
    }
    buf[count] = (u16)BW_MOD_MESSAGE_EOS;
    str = STRBUF_Create(count + 1u, win->heap);
    if (str == NULL) {
        return -1;
    }
    STRBUF_SetStringN(str, buf, count + 1u);
    font = win->font != NULL ? (struct GfFont *)win->font
                             : (struct GfFont *)bw_mod_text_font(win->heap);
    if (font != NULL) {
        PRINTSYS_PrintQueColor((struct PrintQueue *)win->print_que,
                               BMP_WIN_GetBmp((struct BmpWin *)win->bmp_win),
                               (u32)win->x, (s16)win->y, str, font,
                               (u16)BW_MOD_TEXT_COLOUR);
    }
    STRBUF_Delete(str);
    return 0;
}

/* ------------------------------------------------------------------ */
/* The sink                                                             */
/* ------------------------------------------------------------------ */

/*
 * Shows the text in the game's own window.  The window is the one the
 * hook set: the game has it open at the point where the port pumps the
 * queue.
 */
static int text_show(const u16 *units, u32 count)
{
    if (g_window.print_que == NULL || g_window.bmp_win == NULL) {
        /* No window: the text cannot be shown, and saying so is all the
         * service can do with it. */
        return -1;
    }
    return bw_mod_text_print(&g_window, units, count);
}

/*
 * Asks the question in the game's own yes/no window and brings the
 * player's answer back.  Without the field's yes/no window there is
 * nothing to ask with, and the answer is no.
 */
static int text_ask(const u16 *units, u32 count)
{
    if (g_draw.ask == NULL) {
        return 0;
    }
    return g_draw.ask(units, count) != 0 ? 1 : 0;
}

/* Advances the game to the next point where it would accept a message. */
static void text_tick(void)
{
    if (g_draw.tick != NULL) {
        g_draw.tick();
    }
}

static const BwModMessageSink g_sink = {
    text_show,
    text_ask,
    text_tick
};

/* ------------------------------------------------------------------ */
/* The show step                                                       */
/* ------------------------------------------------------------------ */

void bw_mod_text_install(void)
{
    bw_mod_message_set_sink(&g_sink);
    bw_mod_message_set_font_has(bw_mod_text_font_has);
}

int bw_mod_text_pump(void)
{
    if (g_window.print_que == NULL || g_window.bmp_win == NULL) {
        /*
         * No window: the game cannot accept a message here, so the queued
         * texts wait for the next point where it can.  Taking them out of
         * the queue now would lose them.
         */
        return 0;
    }
    return bw_mod_message_pump();
}

/* ------------------------------------------------------------------ */
/* The reset                                                           */
/* ------------------------------------------------------------------ */

void bw_mod_text_reset(void)
{
    if (g_font != NULL && GF_FONT_Delete != NULL) {
        (void)GF_FONT_Delete(g_font);
    }
    g_font = NULL;
    g_font_heap = 0;
    memset(&g_window, 0, sizeof(g_window));
    /*
     * The draw side goes with the reset: it is the overlay's (the field's
     * yes/no window is overlay 21's), and the overlays are unloaded at a
     * reset.  The side that owns it registers it again when it loads.
     */
    memset(&g_draw, 0, sizeof(g_draw));
}
