/*
 * sdk/asm/func_020834C0.c -- MI_UncompressLZ8 at 0x020834C0.
 *
 * Replaces src/matched/arm9_sdk/func_020834C0.c (an asm function: `swpb`,
 * the byte store it writes with, has no C spelling).
 * void MI_UncompressLZ8(const void *srcp, void *destp).
 *
 * The asm:
 *
 *   header   one word (little endian): size = header >> 8; the extended
 *            format is on when header & 0x0F is non-zero (type 0x11 has 1
 *            there, type 0x10 has 0).  The type byte is not otherwise
 *            checked.
 *   loop     while size > 0 (signed): read a flag byte; for each of its 8
 *            bits from the top, while size > 0:
 *   bit 0    copy one literal byte, size -= 1.
 *   bit 1    a back reference.  b0 = the next byte (peeked):
 *              plain:     length = (b0 >> 4) + 3
 *              extended:  b0 >> 4 >= 2: length = (b0 >> 4) + 1
 *                         b0 >> 4 == 0: length = ((b0 & 0xF) << 4
 *                                       | b1 >> 4) + 0x11
 *                         b0 >> 4 == 1: length = ((b0 & 0xF) << 12
 *                                       | b1 << 4 | b2 >> 4) + 0x111
 *            and the byte holding the length's last nibble (b0, b1 or b2)
 *            plus the byte after it give the displacement:
 *            ((low nibble) << 8 | next) + 1.  size -= length, then
 *            `length` bytes are copied from dest - displacement one at a
 *            time, so an overlapping reference repeats the pattern.
 *
 * Edge cases the asm has and this keeps: the copy runs to the end of the
 * reference even when that passes `size` (size goes negative and the outer
 * loop stops), so a stream whose last reference overruns writes past the
 * declared size, exactly as on the DS; a header size of 0 reads nothing
 * after the header; size is compared signed, but it is a 24-bit field and
 * never negative on entry.  Bytes are stored one at a time, which is what
 * the DS does too (`swpb` is a byte write).
 */

#include "asmsdk.h"

void MI_UncompressLZ8(const void *srcp, void *destp)
{
    const u8 *src = (const u8 *)srcp;
    u8 *dst = (u8 *)destp;
    u32 header = bw_rd32(src);
    s32 size = (s32)(header >> 8);
    int extended = (header & 0x0Fu) != 0;

    src += 4;
    while (size > 0) {
        u32 flags = *src++;
        int bit;

        for (bit = 0; bit < 8; bit++) {
            if ((flags & 0x80u) == 0) {
                *dst++ = *src++;
                size -= 1;
            } else {
                u32 b = src[0];
                u32 length;
                u32 disp;
                s32 n;

                if (!extended) {
                    length = 3;
                } else if ((b & 0xE0u) != 0) {
                    length = 1;
                } else {
                    src++;
                    length = (b & 0x0Fu) << 4;
                    if ((b & 0x10u) != 0) {
                        length <<= 8;
                        b = *src++;
                        length += b << 4;
                        length += 0x100;
                    }
                    length += 0x11;
                    b = src[0];
                }
                length += b >> 4;
                src++;
                disp = (((b & 0x0Fu) << 8) | src[0]) + 1;
                src++;
                size -= (s32)length;
                for (n = (s32)length; n > 0; n--) {
                    *dst = *(dst - disp);
                    dst++;
                }
            }
            if (size <= 0) {
                break;
            }
            flags <<= 1;
        }
    }
}

void func_020834C0(const void *srcp, void *destp)
{
    MI_UncompressLZ8(srcp, destp);
}
