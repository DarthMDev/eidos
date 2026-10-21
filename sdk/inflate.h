/*
 * sdk/inflate.h -- a raw DEFLATE (RFC 1951) decoder and the CRC-32 that
 * zip files use.
 *
 * Why it exists: the .bw mod package (sdk/mod_bw.h) is a zip whose members
 * are deflate streams, and the port may not use a third-party library.  The
 * decoder takes the whole compressed stream and the exact size it must
 * produce (a zip header states both), so it writes into a buffer the caller
 * sized and never grows anything.  Every read of the input and every write
 * of the output is bounds checked: a hostile or damaged stream returns an
 * error code and never touches memory outside the two buffers.
 */

#ifndef BW_PORT_INFLATE_H
#define BW_PORT_INFLATE_H

#include <stddef.h>

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BW_INFLATE_OK          0
#define BW_INFLATE_INPUT      -1  /* stream ends early, or a bad block header */
#define BW_INFLATE_CODE       -2  /* an invalid Huffman code set or symbol    */
#define BW_INFLATE_DISTANCE   -3  /* a match reaches before the output start  */
#define BW_INFLATE_OVERFLOW   -4  /* the stream would produce more than dst_len */
#define BW_INFLATE_SHORT      -5  /* the stream ends before dst_len bytes     */

/*
 * Decode the raw deflate stream src[0..src_len) into dst[0..dst_len).  The
 * stream must end (final block) after exactly dst_len output bytes.  On
 * success returns BW_INFLATE_OK and, when src_used is not NULL, stores the
 * number of input bytes the stream occupied.  On failure returns one of the
 * negative codes above; dst then holds partial output and must be discarded.
 */
int bw_inflate(const u8 *src, size_t src_len, u8 *dst, size_t dst_len,
               size_t *src_used);

/* A short text for an error code, for messages. */
const char *bw_inflate_error(int code);

/*
 * CRC-32 (IEEE 802.3, reflected, as zip stores it).  Start with crc = 0 and
 * feed the data in any number of pieces:  crc = bw_crc32(crc, p, n).
 */
u32 bw_crc32(u32 crc, const u8 *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_INFLATE_H */
