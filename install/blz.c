/*
 * install/blz.c -- the BLZ ("backwards LZ") decoder the SDK packs the ARM9
 * binary with.
 *
 * Written from the container description in aletheia's tools/blz.py (whose
 * docstring is the reference, and which decodes the retail ARM9 into the
 * extract/arm9_dec.bin every eidos test that reads ARM9 tables relies on),
 * and checked against it by tests/test_extract.c on containers blz.py
 * compressed.  Every refusal blz.py makes, this makes, in the same order.
 *
 * Reading from the end of the container:
 *
 *     [-4:]   u32  inc_len   how much longer the image is than the container
 *     [-8:-4] u32  hdr_len << 24 | enc_len
 *
 *     dec_len = len - enc_len          the verbatim prefix
 *     pak_len = enc_len - hdr_len      the compressed stream
 *     raw_len = dec_len + enc_len + inc_len
 *
 * inc_len == 0 means the container is stored, not compressed.  The stream
 * is decoded from its last byte backwards, producing the image from its last
 * byte backwards: a flag byte, eight tokens most significant bit first, a
 * clear bit a literal byte and a set bit a big-endian u16 match of
 * (value >> 12) + 3 bytes from (value & 0xFFF) + 3 bytes back in what has
 * been produced so far.
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "install.h"

#define FOOTER   8u
#define HDR_MIN  0x08u
#define HDR_MAX  0x0Bu

static u32 rd32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static void say(char *err, size_t err_len, const char *fmt, ...)
{
    va_list ap;

    if (err != NULL && err_len > 0) {
        va_start(ap, fmt);
        vsnprintf(err, err_len, fmt, ap);
        va_end(ap);
    }
}

/* The footer's numbers, or -1.  Mirrors blz.py's parse_footer. */
static int footer(const u8 *in, u32 n, u32 *dec_len, u32 *pak_len,
                  u32 *raw_len, char *err, size_t err_len)
{
    u32 inc, word, enc, hdr;

    if (n < FOOTER) {
        say(err, err_len, "BLZ: container is only %u bytes, too small for a footer", (unsigned)n);
        return -1;
    }
    inc = rd32(in + n - 4);
    word = rd32(in + n - 8);
    enc = word & 0x00FFFFFFu;
    hdr = word >> 24;
    if (inc == 0) {
        *dec_len = n;
        *pak_len = 0;
        *raw_len = n;
        return 0;
    }
    if (hdr < HDR_MIN || hdr > HDR_MAX) {
        say(err, err_len, "BLZ: header length 0x%02X is outside 0x08..0x0B", (unsigned)hdr);
        return -1;
    }
    if (enc > n) {
        say(err, err_len, "BLZ: encoded length 0x%X exceeds the container size 0x%X", (unsigned)enc, (unsigned)n);
        return -1;
    }
    if (enc < hdr) {
        say(err, err_len, "BLZ: encoded length 0x%X is smaller than the header length 0x%X", (unsigned)enc, (unsigned)hdr);
        return -1;
    }
    if (inc > 0xFFFFFFFFu - n) {
        say(err, err_len, "BLZ: image size overflows (inc_len 0x%X)", (unsigned)inc);
        return -1;
    }
    *dec_len = n - enc;
    *pak_len = enc - hdr;
    *raw_len = n - enc + enc + inc;
    return 0;
}

u32 bw_blz_size(const u8 *in, u32 in_len, char *err, size_t err_len)
{
    u32 dec, pak, raw;

    if (footer(in, in_len, &dec, &pak, &raw, err, err_len) != 0) {
        return 0;
    }
    return raw;
}

int bw_blz_decompress(const u8 *in, u32 in_len, u8 *out, u32 out_len,
                      char *err, size_t err_len)
{
    u32 dec, pak, raw;
    u32 want;        /* bytes the stream must produce */
    u32 made = 0;    /* bytes produced so far */
    u32 sp;          /* stream bytes consumed, counted from its end */
    u8  flags = 0;
    u8  mask = 0;

    if (footer(in, in_len, &dec, &pak, &raw, err, err_len) != 0) {
        return -1;
    }
    if (out_len != raw) {
        say(err, err_len, "BLZ: output buffer is 0x%X bytes, the image is 0x%X", (unsigned)out_len, (unsigned)raw);
        return -1;
    }
    memcpy(out, in, dec);
    if (pak == 0 && raw == dec) {
        return 0;                          /* stored */
    }
    want = raw - dec;

    /* The stream is in[dec .. dec+pak), read backwards; the image is
     * written from out[raw-1] backwards.  "produced[i]" in blz.py is
     * out[raw - 1 - i] here. */
    sp = 0;
    while (made < want) {
        if (mask == 0) {
            if (sp >= pak) {
                say(err, err_len, "BLZ: stream ran out after %u of %u bytes (needed a flag byte)", (unsigned)made, (unsigned)want);
                return -1;
            }
            flags = in[dec + pak - 1 - sp];
            sp++;
            mask = 0x80;
        }
        if (flags & mask) {
            u32 token, len, disp, i;

            if (sp + 1 >= pak) {
                say(err, err_len, "BLZ: stream ran out inside a match token after %u of %u bytes", (unsigned)made, (unsigned)want);
                return -1;
            }
            token = ((u32)in[dec + pak - 1 - sp] << 8) | in[dec + pak - 2 - sp];
            sp += 2;
            len = (token >> 12) + 3u;
            disp = (token & 0x0FFFu) + 3u;
            if (disp > made) {
                say(err, err_len, "BLZ: match at output byte %u reaches %u bytes back, past the decoded region", (unsigned)made, (unsigned)disp);
                return -1;
            }
            if (len > want - made) {
                len = want - made;
            }
            for (i = 0; i < len; i++) {
                out[raw - 1 - made] = out[raw - 1 - (made - disp)];
                made++;
            }
        } else {
            if (sp >= pak) {
                say(err, err_len, "BLZ: stream ran out after %u of %u literal bytes", (unsigned)made, (unsigned)want);
                return -1;
            }
            out[raw - 1 - made] = in[dec + pak - 1 - sp];
            sp++;
            made++;
        }
        mask >>= 1;
    }
    return 0;
}
