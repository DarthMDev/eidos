/*
 * sdk/asm/func_01FF82A8.c -- OSi_VerifyStaticSegmentHash at 0x01FF82A8 (ITCM, Thumb).
 *
 * Replaces src/matched/itcm_sdk/func_01FF82A8.c (TwlSDK os_reset.o, an asm
 * function built with SDK_OS_RESET_COMPACT):
 *
 *   BOOL func_01FF82A8(const u8 *src, u32 len, const void *digest)
 *
 *   ctx at sp+0x54 (BIOS SHA-1 context, its +0x60 hook word set to 0 first),
 *   pad at sp+0x14 (64 bytes), hash at sp+0x00 (20 bytes):
 *
 *     str 0, [ctx+0x60]; swi 0x24             SHA1_Init(ctx)
 *     pad[i] = key[i] ^ 0x36, i = 0..63       key: the 64 bytes at 0x01FF8328
 *     swi 0x25 (ctx, pad, 0x40)               SHA1_Update
 *     swi 0x25 (ctx, src, len)                SHA1_Update
 *     swi 0x26 (hash, ctx)                    SHA1_GetHash
 *     str 0, [ctx+0x60]; swi 0x24             SHA1_Init(ctx)
 *     pad[i] = key[i] ^ 0x5C
 *     swi 0x25 (ctx, pad, 0x40)
 *     swi 0x25 (ctx, hash, 0x14)
 *     swi 0x26 (hash, ctx)
 *     swi 0x28 (hash, digest)                 SHA1_Compare (TRUE if equal)
 *     cmp r0, #0; beq out; mov r0, #1         normalise to 0 or 1
 *
 * That is HMAC-SHA1 with a 64-byte key (exactly one block, so no key hash),
 * compared against `digest`.  The key sits right after the code: 64 Thumb
 * instructions (0x80 bytes) from 0x01FF82A8 end at 0x01FF8328, where the
 * sixteen dcd words of the matched file begin, and the pool word naming
 * 0x01FF8328 follows them at 0x01FF8368, ending at 0x01FF836C where the
 * next function (OSi_VerifyStaticSegments) starts.  The words are stored
 * little-endian, so key byte 4*i + j is bits 8*j..8*j+7 of word i.
 *
 * The SWIs 0x24 to 0x28 are the DSi BIOS SHA-1 services.  Their result is
 * plain computation (FIPS 180 SHA-1), so the host computes it here with a
 * local SHA-1; nothing else in the BIOS context is observable (the context
 * lives on this function's stack).  The only caller,
 * OSi_VerifyStaticSegments (func_01FF836C.c), reaches it on the reset path
 * when the TWL header flags ask for verification; inputs are host pointers.
 */

#include <string.h>

#include "asmsdk.h"

static const u32 bw_static_hmac_key[16] = {
    0xDEC00621u, 0x3FCE98BAu, 0x9DE392A6u, 0x01EDF246u,
    0x08CCE376u, 0xFA632356u, 0xDFECD4CAu, 0x3478629Au,
    0x3C636D8Fu, 0x92CA22FEu, 0x23978820u, 0xC2AECFD2u,
    0xFE8D6732u, 0x986483CAu, 0x373EFDACu, 0x24584687u,
};

typedef struct BwSha1 {
    u32 h[5];
    u64 length;        /* bytes hashed so far */
    u8 block[64];
    u32 fill;          /* bytes waiting in block */
} BwSha1;

static u32 bw_rol32(u32 x, unsigned n)
{
    return (x << n) | (x >> (32u - n));
}

static void bw_sha1_block(BwSha1 *c, const u8 *p)
{
    u32 w[80];
    u32 a, b, d, e, f, k, t, cc;
    unsigned i;

    for (i = 0; i < 16; i++) {
        w[i] = ((u32)p[4 * i] << 24) | ((u32)p[4 * i + 1] << 16)
             | ((u32)p[4 * i + 2] << 8) | (u32)p[4 * i + 3];
    }
    for (i = 16; i < 80; i++) {
        w[i] = bw_rol32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }

    a = c->h[0];
    b = c->h[1];
    cc = c->h[2];
    d = c->h[3];
    e = c->h[4];
    for (i = 0; i < 80; i++) {
        if (i < 20) {
            f = (b & cc) | (~b & d);
            k = 0x5A827999u;
        } else if (i < 40) {
            f = b ^ cc ^ d;
            k = 0x6ED9EBA1u;
        } else if (i < 60) {
            f = (b & cc) | (b & d) | (cc & d);
            k = 0x8F1BBCDCu;
        } else {
            f = b ^ cc ^ d;
            k = 0xCA62C1D6u;
        }
        t = bw_rol32(a, 5) + f + e + k + w[i];
        e = d;
        d = cc;
        cc = bw_rol32(b, 30);
        b = a;
        a = t;
    }
    c->h[0] += a;
    c->h[1] += b;
    c->h[2] += cc;
    c->h[3] += d;
    c->h[4] += e;
}

static void bw_sha1_init(BwSha1 *c)
{
    c->h[0] = 0x67452301u;
    c->h[1] = 0xEFCDAB89u;
    c->h[2] = 0x98BADCFEu;
    c->h[3] = 0x10325476u;
    c->h[4] = 0xC3D2E1F0u;
    c->length = 0;
    c->fill = 0;
}

static void bw_sha1_update(BwSha1 *c, const u8 *p, u32 len)
{
    c->length += len;
    while (len > 0) {
        u32 take = 64u - c->fill;

        if (take > len) {
            take = len;
        }
        memcpy(c->block + c->fill, p, take);
        c->fill += take;
        p += take;
        len -= take;
        if (c->fill == 64u) {
            bw_sha1_block(c, c->block);
            c->fill = 0;
        }
    }
}

static void bw_sha1_final(BwSha1 *c, u8 out[20])
{
    u64 bits = c->length * 8u;
    unsigned i;

    c->block[c->fill++] = 0x80u;
    if (c->fill > 56u) {
        memset(c->block + c->fill, 0, 64u - c->fill);
        bw_sha1_block(c, c->block);
        c->fill = 0;
    }
    memset(c->block + c->fill, 0, 56u - c->fill);
    for (i = 0; i < 8; i++) {
        c->block[56 + i] = (u8)(bits >> (56 - 8 * i));
    }
    bw_sha1_block(c, c->block);
    for (i = 0; i < 5; i++) {
        out[4 * i] = (u8)(c->h[i] >> 24);
        out[4 * i + 1] = (u8)(c->h[i] >> 16);
        out[4 * i + 2] = (u8)(c->h[i] >> 8);
        out[4 * i + 3] = (u8)c->h[i];
    }
}

static void bw_hmac_pad(u8 pad[64], u8 x)
{
    unsigned i;

    for (i = 0; i < 64; i++) {
        pad[i] = (u8)((u8)(bw_static_hmac_key[i / 4] >> (8 * (i % 4))) ^ x);
    }
}

BOOL func_01FF82A8(const u8 *src, u32 len, const void *digest)
{
    BwSha1 ctx;
    u8 pad[64];
    u8 hash[20];

    bw_sha1_init(&ctx);
    bw_hmac_pad(pad, 0x36u);
    bw_sha1_update(&ctx, pad, 0x40u);
    bw_sha1_update(&ctx, src, len);
    bw_sha1_final(&ctx, hash);

    bw_sha1_init(&ctx);
    bw_hmac_pad(pad, 0x5Cu);
    bw_sha1_update(&ctx, pad, 0x40u);
    bw_sha1_update(&ctx, hash, 0x14u);
    bw_sha1_final(&ctx, hash);

    return memcmp(hash, digest, 20) == 0 ? 1 : 0;
}
