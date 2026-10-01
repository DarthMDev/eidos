/*
 * engine/platform/font.h -- the setup window's bitmap font (font.c says
 * why there is one).  5 x 7 pixels plus two rows of descender, in a 6 x 10
 * cell: printable ASCII, and a box for everything else.
 */

#ifndef EIDOS_ENGINE_FONT_H
#define EIDOS_ENGINE_FONT_H

#include "types.h"

#define EIDOS_FONT_COLS    5    /* lit columns per glyph */
#define EIDOS_FONT_ROWS    9    /* rows per glyph, descenders included */
#define EIDOS_FONT_ADVANCE 6    /* pixels from one glyph to the next */
#define EIDOS_FONT_LINE    10   /* pixels from one line to the next */

/* The glyph's EIDOS_FONT_ROWS rows; bit 4 is the leftmost column. */
const u8 *eidos_font_glyph(u32 ch);

/* The next code point of a UTF-8 string, advancing *s; 0 at the end,
 * U+FFFD for a malformed sequence. */
u32 eidos_utf8_next(const char **s);

#endif /* EIDOS_ENGINE_FONT_H */
