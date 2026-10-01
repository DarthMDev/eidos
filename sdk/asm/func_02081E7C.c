/*
 * sdk/asm/func_02081E7C.c -- MATHi_SHA1ProcessBlock at 0x02081E7C.
 *
 * Replaces src/matched/arm9_sdk/func_02081E7C.c, the SDK's sha1.o: an
 * asm-only object (796 bytes of ARM after the four round constants).  The
 * game reaches it only through the pointer MATHi_SHA1ProcessMessageBlockFunc
 * (0x020A8688) in the ARM9 data, which MATH_SHA1Update and
 * MATHi_SHA1Fill call through (docs/armdata.md relocates that word to this
 * function), so cmake/boot.cmake seeds the boot with the file by hand.
 *
 * void (context): the five-word hash state at +0x00, the 64-byte message
 * block at +0x14.  The asm is the standard SHA-1 compression function
 * (FIPS 180-4, 6.1.2): the sixteen block words are read big-endian
 * (`ldmia` then a byte swap built from the 0xFFFF00FF mask), expanded to
 * eighty, run through the four twenty-round stages with the constants
 * 0x5A827999, 0x6ED9EBA1, 0x8F1BBCDC, 0xCA62C1D6, and the result added into
 * the state.  The C below is that function; the block is read bytewise, so
 * it is the same on every host byte order.
 */

#include "asmsdk.h"

static u32 bw_rol(u32 v, unsigned n)
{
    return (v << n) | (v >> (32u - n));
}

void func_02081E7C(void *context)
{
    u32 *h = (u32 *)context;
    const u8 *block = (const u8 *)context + 0x14;
    u32 w[80];
    u32 a, b, c, d, e;
    int i;

    for (i = 0; i < 16; i++) {
        w[i] = ((u32)block[i * 4] << 24) | ((u32)block[i * 4 + 1] << 16)
             | ((u32)block[i * 4 + 2] << 8) | (u32)block[i * 4 + 3];
    }
    for (i = 16; i < 80; i++) {
        w[i] = bw_rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }
    a = h[0];
    b = h[1];
    c = h[2];
    d = h[3];
    e = h[4];
    for (i = 0; i < 80; i++) {
        u32 f;
        u32 k;
        u32 t;

        if (i < 20) {
            f = (b & c) | (~b & d);
            k = 0x5A827999u;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1u;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDCu;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6u;
        }
        t = bw_rol(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = bw_rol(b, 30);
        b = a;
        a = t;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
}
