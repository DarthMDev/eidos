/*
 * sdk/inflate.c -- raw DEFLATE decoder and CRC-32, written from RFC 1951.
 *
 * Decoding method.  Bits are read least significant first from a small
 * accumulator.  A Huffman code is decoded with the canonical "count and
 * first-code" method: for each code length the decoder knows how many codes
 * have that length and the first code of that length, so it extends the code
 * one bit at a time until the code falls inside the range for the current
 * length.  That needs no big lookup table, which keeps the decoder short and
 * easy to audit; mod packages are small enough that speed does not matter.
 *
 * Safety.  Every symbol read checks the input end, every output write checks
 * dst_len, and every length set is checked for being over-subscribed.  An
 * incomplete set is accepted only in the one case the RFC allows (a single
 * distance code of length one, or none at all).
 */

#include <string.h>

#include "inflate.h"

#define MAXBITS 15
#define MAXLCODES 286
#define MAXDCODES 30
#define FIXLCODES 288

typedef struct {
    const u8 *src;
    size_t len;
    size_t pos;
    u32 bitbuf;
    int bitcnt;
    u8 *dst;
    size_t dst_len;
    size_t out;
} State;

typedef struct {
    u16 count[MAXBITS + 1];   /* codes of each length             */
    u16 symbol[FIXLCODES];    /* symbols ordered by code          */
} Huff;

/* Read `need` bits (0..16).  Returns -1 when the input is exhausted. */
static int bits(State *s, int need)
{
    u32 val = s->bitbuf;

    while (s->bitcnt < need) {
        if (s->pos >= s->len)
            return -1;
        val |= (u32)s->src[s->pos++] << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = val >> need;
    s->bitcnt -= need;
    return (int)(val & ((1UL << need) - 1));
}

/*
 * Build the decoding tables from `n` code lengths.  Returns 0 for a complete
 * set, a positive number for an incomplete one (codes left over), and a
 * negative number for an over-subscribed set.
 */
static int construct(Huff *h, const u8 *length, int n)
{
    int symbol, len, left;
    u16 offs[MAXBITS + 1];

    for (len = 0; len <= MAXBITS; len++)
        h->count[len] = 0;
    for (symbol = 0; symbol < n; symbol++)
        h->count[length[symbol]]++;
    if (h->count[0] == n)
        return 0;                       /* no codes: complete enough */

    left = 1;
    for (len = 1; len <= MAXBITS; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0)
            return left;
    }
    offs[1] = 0;
    for (len = 1; len < MAXBITS; len++)
        offs[len + 1] = (u16)(offs[len] + h->count[len]);
    for (symbol = 0; symbol < n; symbol++)
        if (length[symbol] != 0)
            h->symbol[offs[length[symbol]]++] = (u16)symbol;
    return left;
}

/* Decode one symbol.  Returns it, -1 for end of input, -2 for a bad code. */
static int decode(State *s, const Huff *h)
{
    int code = 0, first = 0, index = 0, len;

    for (len = 1; len <= MAXBITS; len++) {
        int b = bits(s, 1);
        int count;

        if (b < 0)
            return -1;
        code |= b;
        count = h->count[len];
        if (code - count < first)
            return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -2;
}

static const u16 lbase[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};
static const u8 lext[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};
static const u16 dbase[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
    8193, 12289, 16385, 24577
};
static const u8 dext[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};

/* Decode the symbols of one Huffman block. */
static int codes(State *s, const Huff *lencode, const Huff *distcode)
{
    for (;;) {
        int symbol = decode(s, lencode);

        if (symbol == -1)
            return BW_INFLATE_INPUT;
        if (symbol < 0)
            return BW_INFLATE_CODE;
        if (symbol < 256) {
            if (s->out >= s->dst_len)
                return BW_INFLATE_OVERFLOW;
            s->dst[s->out++] = (u8)symbol;
        } else if (symbol == 256) {
            return BW_INFLATE_OK;
        } else {
            int extra, len, dsym, dist;
            size_t from;

            symbol -= 257;
            if (symbol >= 29)
                return BW_INFLATE_CODE;
            extra = bits(s, lext[symbol]);
            if (extra < 0)
                return BW_INFLATE_INPUT;
            len = lbase[symbol] + extra;
            dsym = decode(s, distcode);
            if (dsym == -1)
                return BW_INFLATE_INPUT;
            if (dsym < 0 || dsym >= 30)
                return BW_INFLATE_CODE;
            extra = bits(s, dext[dsym]);
            if (extra < 0)
                return BW_INFLATE_INPUT;
            dist = dbase[dsym] + extra;
            if ((size_t)dist > s->out)
                return BW_INFLATE_DISTANCE;
            if ((size_t)len > s->dst_len - s->out)
                return BW_INFLATE_OVERFLOW;
            from = s->out - (size_t)dist;
            while (len-- > 0)
                s->dst[s->out++] = s->dst[from++];
        }
    }
}

static int stored_block(State *s)
{
    u32 len, nlen;

    s->bitbuf = 0;                      /* discard the rest of the byte */
    s->bitcnt = 0;
    if (s->pos > s->len || s->len - s->pos < 4)
        return BW_INFLATE_INPUT;
    len = s->src[s->pos] | ((u32)s->src[s->pos + 1] << 8);
    nlen = s->src[s->pos + 2] | ((u32)s->src[s->pos + 3] << 8);
    s->pos += 4;
    if (len != (~nlen & 0xFFFFu))
        return BW_INFLATE_INPUT;
    if (len > s->len - s->pos)
        return BW_INFLATE_INPUT;
    if (len > s->dst_len - s->out)
        return BW_INFLATE_OVERFLOW;
    memcpy(s->dst + s->out, s->src + s->pos, len);
    s->pos += len;
    s->out += len;
    return BW_INFLATE_OK;
}

static int fixed_block(State *s)
{
    Huff lencode, distcode;
    u8 lengths[FIXLCODES];
    int symbol;

    for (symbol = 0; symbol < 144; symbol++)
        lengths[symbol] = 8;
    for (; symbol < 256; symbol++)
        lengths[symbol] = 9;
    for (; symbol < 280; symbol++)
        lengths[symbol] = 7;
    for (; symbol < FIXLCODES; symbol++)
        lengths[symbol] = 8;
    construct(&lencode, lengths, FIXLCODES);
    for (symbol = 0; symbol < MAXDCODES; symbol++)
        lengths[symbol] = 5;
    construct(&distcode, lengths, MAXDCODES);
    return codes(s, &lencode, &distcode);
}

static int dynamic_block(State *s)
{
    static const u8 order[19] = {
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
    };
    Huff lencode, distcode;
    u8 lengths[MAXLCODES + MAXDCODES];
    int nlen, ndist, ncode, index, err;

    nlen = bits(s, 5);
    ndist = bits(s, 5);
    ncode = bits(s, 4);
    if (nlen < 0 || ndist < 0 || ncode < 0)
        return BW_INFLATE_INPUT;
    nlen += 257;
    ndist += 1;
    ncode += 4;
    if (nlen > MAXLCODES || ndist > MAXDCODES)
        return BW_INFLATE_CODE;

    for (index = 0; index < 19; index++) {
        int v = 0;

        if (index < ncode) {
            v = bits(s, 3);
            if (v < 0)
                return BW_INFLATE_INPUT;
        }
        lengths[order[index]] = (u8)v;
    }
    err = construct(&lencode, lengths, 19);
    if (err != 0)
        return BW_INFLATE_CODE;         /* the code-length code must be complete */

    index = 0;
    while (index < nlen + ndist) {
        int symbol = decode(s, &lencode);

        if (symbol == -1)
            return BW_INFLATE_INPUT;
        if (symbol < 0)
            return BW_INFLATE_CODE;
        if (symbol < 16) {
            lengths[index++] = (u8)symbol;
        } else {
            int len = 0, rep, r;

            if (symbol == 16) {
                if (index == 0)
                    return BW_INFLATE_CODE;
                len = lengths[index - 1];
                r = bits(s, 2);
                rep = 3;
            } else if (symbol == 17) {
                r = bits(s, 3);
                rep = 3;
            } else {
                r = bits(s, 7);
                rep = 11;
            }
            if (r < 0)
                return BW_INFLATE_INPUT;
            rep += r;
            if (index + rep > nlen + ndist)
                return BW_INFLATE_CODE;
            while (rep-- > 0)
                lengths[index++] = (u8)len;
        }
    }
    if (lengths[256] == 0)
        return BW_INFLATE_CODE;         /* no end-of-block code */

    err = construct(&lencode, lengths, nlen);
    if (err < 0 || (err > 0 && nlen - lencode.count[0] != 1))
        return BW_INFLATE_CODE;
    err = construct(&distcode, lengths + nlen, ndist);
    if (err < 0 || (err > 0 && ndist - distcode.count[0] != 1))
        return BW_INFLATE_CODE;
    return codes(s, &lencode, &distcode);
}

int bw_inflate(const u8 *src, size_t src_len, u8 *dst, size_t dst_len,
               size_t *src_used)
{
    State s;
    int last, type, err;

    s.src = src;
    s.len = src_len;
    s.pos = 0;
    s.bitbuf = 0;
    s.bitcnt = 0;
    s.dst = dst;
    s.dst_len = dst_len;
    s.out = 0;

    do {
        last = bits(&s, 1);
        type = bits(&s, 2);
        if (last < 0 || type < 0)
            return BW_INFLATE_INPUT;
        if (type == 0)
            err = stored_block(&s);
        else if (type == 1)
            err = fixed_block(&s);
        else if (type == 2)
            err = dynamic_block(&s);
        else
            err = BW_INFLATE_INPUT;
        if (err != BW_INFLATE_OK)
            return err;
    } while (!last);

    if (s.out != dst_len)
        return BW_INFLATE_SHORT;
    if (src_used != NULL)
        *src_used = s.pos;
    return BW_INFLATE_OK;
}

const char *bw_inflate_error(int code)
{
    switch (code) {
    case BW_INFLATE_OK:       return "ok";
    case BW_INFLATE_INPUT:    return "the compressed data is truncated or damaged";
    case BW_INFLATE_CODE:     return "the compressed data has an invalid code";
    case BW_INFLATE_DISTANCE: return "the compressed data has a bad back reference";
    case BW_INFLATE_OVERFLOW: return "the compressed data is longer than its stated size";
    case BW_INFLATE_SHORT:    return "the compressed data is shorter than its stated size";
    default:                  return "unknown inflate error";
    }
}

u32 bw_crc32(u32 crc, const u8 *data, size_t len)
{
    static u32 table[256];
    static int ready;
    size_t i;

    if (!ready) {
        u32 n, k;

        for (n = 0; n < 256; n++) {
            u32 c = n;

            for (k = 0; k < 8; k++)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[n] = c;
        }
        ready = 1;
    }
    crc = ~crc;
    for (i = 0; i < len; i++)
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}
