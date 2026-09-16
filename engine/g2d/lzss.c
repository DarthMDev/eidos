/*
 * engine/g2d/lzss.c -- BIOS-style LZ77 (0x10) and LZ11 (0x11) container
 * decompression, the wrapper most Nitro resources arrive in.
 *
 * Both are the well-documented GBA/NDS BIOS decompression formats: a
 * 4-byte header (tag byte, 24-bit decompressed size), then flag bytes,
 * each covering eight units, MSB first, a clear bit meaning "one literal
 * byte" and a set bit meaning "copy from the already-written output".
 * LZ11 adds the two extended token forms for long matches and the
 * "declared size 0 means a 32-bit size follows" header variant.
 *
 * The end-of-stream guard is the one aletheia's tools/assets/narc.py
 * applies and documents: a real container consumes all but at most three
 * bytes (sub-file padding) of its own extent.  Reaching the declared
 * size much earlier means the tag byte was a coincidental data byte, not
 * a header, and the right answer is "this is not compressed" rather than
 * a buffer of nonsense.  Several members of the rig archive depend on
 * that distinction.
 */

#include <stdlib.h>
#include <string.h>

#include "g2d.h"

static int lz_sniff(const u8 *data, u32 size, u32 *dec_size)
{
    u8 tag;

    if (size < 4) {
        return -1;
    }
    tag = data[0];
    if (tag != 0x10 && tag != 0x11) {
        return -1;
    }
    *dec_size = (u32)data[1] | ((u32)data[2] << 8) | ((u32)data[3] << 16);
    return tag;
}

u8 *g2d_decompress(const u8 *data, u32 size, u32 *out_size)
{
    u32 dec_size;
    u32 sp = 4;
    u32 len = 0;
    u8 *out;
    int tag = lz_sniff(data, size, &dec_size);

    if (tag < 0) {
        return NULL;
    }
    if (tag == 0x11 && dec_size == 0) {
        if (sp + 4 > size) {
            return NULL;
        }
        dec_size = bw_rd32(data + sp);
        sp += 4;
    }
    if (dec_size == 0) {
        return NULL;
    }
    out = (u8 *)malloc(dec_size);
    if (out == NULL) {
        return NULL;
    }

    while (len < dec_size) {
        u8 flags;
        int bit;

        if (sp >= size) {
            free(out);
            return NULL;
        }
        flags = data[sp++];
        for (bit = 0; bit < 8 && len < dec_size; bit++) {
            u32 match_len;
            u32 disp;
            u32 base;
            u32 i;

            if ((flags & (0x80u >> bit)) == 0) {
                if (sp >= size) {
                    free(out);
                    return NULL;
                }
                out[len++] = data[sp++];
                continue;
            }
            if (sp >= size) {
                free(out);
                return NULL;
            }
            if (tag == 0x10) {
                u8 b0, b1;

                if (sp + 1 >= size) {
                    free(out);
                    return NULL;
                }
                b0 = data[sp];
                b1 = data[sp + 1];
                sp += 2;
                match_len = (u32)(b0 >> 4) + 3u;
                disp = (((u32)(b0 & 0x0F) << 8) | b1) + 1u;
            } else {
                u8 b0 = data[sp++];
                u32 indicator = (u32)(b0 >> 4);

                if (indicator == 0) {
                    if (sp + 1 >= size) {
                        free(out);
                        return NULL;
                    }
                    match_len = (((u32)(b0 & 0x0F) << 4) | (u32)(data[sp] >> 4)) + 0x11u;
                    disp = (((u32)(data[sp] & 0x0F) << 8) | data[sp + 1]) + 1u;
                    sp += 2;
                } else if (indicator == 1) {
                    if (sp + 2 >= size) {
                        free(out);
                        return NULL;
                    }
                    match_len = (((u32)(b0 & 0x0F) << 12) | ((u32)data[sp] << 4)
                                 | (u32)(data[sp + 1] >> 4)) + 0x111u;
                    disp = (((u32)(data[sp + 1] & 0x0F) << 8) | data[sp + 2]) + 1u;
                    sp += 3;
                } else {
                    if (sp >= size) {
                        free(out);
                        return NULL;
                    }
                    match_len = indicator + 1u;
                    disp = (((u32)(b0 & 0x0F) << 8) | data[sp]) + 1u;
                    sp += 1;
                }
            }
            if (disp > len) {
                free(out);
                return NULL;
            }
            base = len - disp;
            for (i = 0; i < match_len && len < dec_size; i++) {
                out[len++] = out[base + i];
            }
        }
    }

    if (size - sp > 3) {
        /* Not a real container: the tag byte was a data byte. */
        free(out);
        return NULL;
    }
    *out_size = dec_size;
    return out;
}
