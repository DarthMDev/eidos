/*
 * sdk/mod_text.h -- the port's own text path: the side of the message
 * service that puts the text on the screen (docs/mod-services.md,
 * "Messages"; sdk/mod_message.h, "The port's own text path").
 *
 * What this is
 * ------------
 *
 * The message service (sdk/mod_message.c) owns the queue: a mod queues a
 * text, and the port shows it at the next point where the game would
 * accept a message.  The service does not know how a text is shown; it
 * asks through its sink (sdk/mod_message.h).  This file is the port's
 * side of that sink: the text path that takes a run of the game's code
 * units and shows it in the game's own window, in the game's font.
 *
 * The shape is the one the port already uses twice, in
 * engine/platform/mod_options_proc.c and in
 * stubs/ov/reimpl_ov088_func_021BAD08.c: the game's string object is
 * built out of the code units and the end code (0xFFFF), and it is
 * printed with the game's own print queue and the game's own font.
 *
 *   STRBUF_Create(capacity, heap)        the game's string object
 *   STRBUF_SetStringN(str, units, n)     the code units and the end code
 *   PRINTSYS_PrintQueColor(que, bmp, x, y, str, font, colour)
 *
 * The font is the game's own: GF_FONT_Create(0x17, 0, 0, 0, heap), the
 * font at arc 0x17 (a/0/2/3), member 0, the one the field's message
 * window uses (src/game/ov021/field_msgbg.c).  The font's own glyph
 * table answers what the message service asks about a character the font
 * lacks (sdk/mod_message.h, "The encoding"): GF_FONT_GetGlyphIndex
 * gives a character's glyph, and the glyph of '?' is the one a character
 * the font lacks is shown as.
 *
 * Who holds the window
 * --------------------
 *
 * The field's message window and the field's yes/no window are overlay
 * 21's (src/game/ov021/field_msgbg.c), and the battle's message box is
 * overlay 93's (src/game/ov093/btlv_core.c).  The ARM9 cannot call into
 * an overlay, so the side that has the window open is the side that
 * hands it over: the hook, a port source that stands in for a matched
 * overlay file (stubs/ov/reimpl_ovNNN_*.c, docs/hooks.md) or the
 * platform layer.  It opens the game's own window, hands the port its
 * three parts, and lets the port print into it.
 *
 * The three jobs the port's text path has with a text are the game's
 * own three:
 *
 *   show  shows the text in the game's own window: the field's talk
 *         window, or the battle's message box in a battle.  The units
 *         are the game's code units without the end code; the end code
 *         is added here, when the string object is built.
 *   ask   asks the question in the game's own yes/no window and returns
 *         the player's answer: 1 for yes, 0 for no.  A battle has no
 *         yes/no window, which is why a confirm is only allowed in the
 *         field (docs/mod-services.md).
 *   tick  advances the game to the next point where the game would
 *         accept a message.  A confirm waits for that point; without it
 *         a confirm cannot be asked and answers no.
 *
 * This header is the port's own; a mod never sees it.  What a mod sees
 * is the services table (sdk/mod_state.h), whose two message calls the
 * loader binds to bw_mod_message_show() and bw_mod_message_confirm().
 */

#ifndef BW_PORT_MOD_TEXT_H
#define BW_PORT_MOD_TEXT_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* The window                                                           */
/* ------------------------------------------------------------------ */

/*
 * The game's own message window, as the port's text path sees it.
 *
 * The window belongs to the overlay that opened it, so the parts are
 * named here and the types are the port's own (the overlay that hands
 * them over knows what they are):
 *
 *   print_que  the game's print queue: the one the game itself runs,
 *              so the text appears with the game's own speed and the
 *              game's own "the text is still being written" wait.
 *   bmp_win    the game's bitmap window: the window to draw in.
 *   font       the game's font (NULL: the port makes its own, the one
 *              at arc 0x17, member 0).
 *   heap       the heap the game's objects come from.
 *   x, y       where in the window the text starts, in pixels.
 */
typedef struct BwModTextWindow {
    void *print_que;
    void *bmp_win;
    void *font;
    u32   heap;
    u16   x;
    u16   y;
} BwModTextWindow;

/* ------------------------------------------------------------------ */
/* The draw side                                                        */
/* ------------------------------------------------------------------ */

/*
 * The two jobs the port cannot do for itself: the yes/no window is the
 * field's (overlay 21's), and advancing the game is the platform
 * layer's (the game's frame).  Whoever holds them registers them here;
 * with neither of them a text can still be shown, but a confirm cannot
 * be asked and answers no.
 */
typedef struct BwModTextDraw {
    /* The field's yes/no window: it shows the question and waits for the
     * player's answer.  1 for yes, 0 for no. */
    int  (*ask)(const u16 *units, u32 count);
    /* One step of the game: the wait for a confirm's answer needs the
     * game to reach the next point where it would accept a message. */
    void (*tick)(void);
} BwModTextDraw;

/* The draw side.  NULL takes it away. */
void bw_mod_text_set_draw(const BwModTextDraw *draw);

/* ------------------------------------------------------------------ */
/* The window the hook has open                                         */
/* ------------------------------------------------------------------ */

/*
 * The window the text goes into.  The hook sets it before it pumps the
 * queue: the window is the one the game has open at that point.
 */
void bw_mod_text_set_window(const BwModTextWindow *win);

/* The window that is set, or NULL. */
const BwModTextWindow *bw_mod_text_window(void);

/* ------------------------------------------------------------------ */
/* The port's own text path                                            */
/* ------------------------------------------------------------------ */

/*
 * Show a run of the game's code units in the game's own window, in the
 * game's font: the game's string object is built out of the units and
 * the end code, and it is printed with the game's own print queue.
 *
 * Returns 0 when the text was printed, and -1 when there is no window to
 * print in or the game's print system is not there.
 */
int bw_mod_text_print(const BwModTextWindow *win, const u16 *units, u32 count);

/* The game's font, made on first use: the font at arc 0x17, member 0.
 * NULL when the game is not there. */
void *bw_mod_text_font(u32 heap);

/*
 * The game's own answer to "does the font have this character": the
 * font's own glyph table (GF_FONT_GetGlyphIndex).  A character the font
 * lacks is shown as '?', and the service asks this before it writes a
 * character out (sdk/mod_message.h, "The encoding").
 */
int bw_mod_text_font_has(u32 codepoint);

/* ------------------------------------------------------------------ */
/* The show step                                                       */
/* ------------------------------------------------------------------ */

/*
 * Install the message service's sink: the port's own text path is what
 * the service asks to show a text, to ask a question and to advance the
 * game (sdk/mod_message.h, "The sink").  The port calls this at
 * start-up, beside the loader's own set-up.
 */
void bw_mod_text_install(void);

/*
 * The show step: the port calls this where the game would accept a
 * message.  It takes the next queued text and shows it in the window
 * that is set; a question is asked instead of shown, and its answer is
 * recorded.  Returns 1 when a text was shown, 0 when the queue was
 * empty or there is no window to show it in.
 */
int bw_mod_text_pump(void);

/* The reset: the port's own font goes away with the game's, the window
 * with it, and the draw side with the overlay that owns it (at power-on
 * and at every reset, bw_port_reset). */
void bw_mod_text_reset(void);

#ifdef __cplusplus
}
#endif
#endif /* BW_PORT_MOD_TEXT_H */
