/*
 * install/sha1.c -- SHA-1 (FIPS 180-4), for identifying the player's dump.
 *
 * SHA-1 is not used for anything security-related here: it is the hash
 * aletheia records for the one dump it is built against (and the one the
 * DS community has always used to name dumps), so it is the hash the
 * installer has to compute to say "this is that ROM".  Checked against the
 * FIPS test vectors in tests/test_extract.c.
 */

#include <string.h>

#include "install.h"

#define ROL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

static void compress(u32 h[5], const u8 *p)
{
    u32 w[80];
    u32 a, b, c, d, e, t;
    int i;

    for (i = 0; i < 16; i++) {
        w[i] = ((u32)p[4 * i] << 24) | ((u32)p[4 * i + 1] << 16)
             | ((u32)p[4 * i + 2] << 8) | (u32)p[4 * i + 3];
    }
    for (i = 16; i < 80; i++) {
        t = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16];
        w[i] = ROL(t, 1);
    }
    a = h[0]; b = h[1]; c = h[2]; d = h[3]; e = h[4];
    for (i = 0; i < 80; i++) {
        u32 f, k;
        if (i < 20) {
            f = (b & c) | (~b & d);             k = 0x5A827999u;
        } else if (i < 40) {
            f = b ^ c ^ d;                      k = 0x6ED9EBA1u;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);    k = 0x8F1BBCDCu;
        } else {
            f = b ^ c ^ d;                      k = 0xCA62C1D6u;
        }
        t = ROL(a, 5) + f + e + k + w[i];
        e = d; d = c; c = ROL(b, 30); b = a; a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
}

void bw_sha1_init(BwSha1 *s)
{
    s->h[0] = 0x67452301u;
    s->h[1] = 0xEFCDAB89u;
    s->h[2] = 0x98BADCFEu;
    s->h[3] = 0x10325476u;
    s->h[4] = 0xC3D2E1F0u;
    s->length = 0;
    s->used = 0;
}

void bw_sha1_update(BwSha1 *s, const void *data, size_t len)
{
    const u8 *p = (const u8 *)data;

    s->length += len;
    if (s->used != 0) {
        size_t take = 64u - s->used;
        if (take > len) {
            take = len;
        }
        memcpy(s->block + s->used, p, take);
        s->used += (u32)take;
        p += take;
        len -= take;
        if (s->used < 64u) {
            return;
        }
        compress(s->h, s->block);
        s->used = 0;
    }
    while (len >= 64u) {
        compress(s->h, p);
        p += 64;
        len -= 64;
    }
    memcpy(s->block, p, len);
    s->used = (u32)len;
}

void bw_sha1_final(BwSha1 *s, u8 digest[20])
{
    u64 bits = s->length * 8u;
    int i;

    s->block[s->used++] = 0x80;
    if (s->used > 56u) {
        memset(s->block + s->used, 0, 64u - s->used);
        compress(s->h, s->block);
        s->used = 0;
    }
    memset(s->block + s->used, 0, 56u - s->used);
    for (i = 0; i < 8; i++) {
        s->block[56 + i] = (u8)(bits >> (56 - 8 * i));
    }
    compress(s->h, s->block);
    for (i = 0; i < 20; i++) {
        digest[i] = (u8)(s->h[i / 4] >> (24 - 8 * (i % 4)));
    }
}

void bw_sha1_hex(const u8 digest[20], char out[41])
{
    static const char hex[] = "0123456789abcdef";
    int i;

    for (i = 0; i < 20; i++) {
        out[2 * i] = hex[digest[i] >> 4];
        out[2 * i + 1] = hex[digest[i] & 15];
    }
    out[40] = '\0';
}
