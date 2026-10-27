/*
 * engine/g2d/rig.c -- loading one poseable multi-part sprite bundle out
 * of a rig archive (a/0/0/4 and its siblings).
 *
 * A rig archive stores, per species/form index N, a fixed-size block of
 * sub-files (20 of them in a/0/0/4).  The members are NOT identified by a
 * fixed local-offset table: the block is scanned and every member is
 * classified by its own post-decompression magic, which is what lets the
 * same reader handle archives with different block layouts.  The rules,
 * the same ones aletheia's tools/assets/rig.py applies:
 *
 *   - the LARGEST RGCN member is the main tile sheet (a/0/0/4 duplicates
 *     both of its sheets, and the largest is always the main one; ties
 *     go to the lowest local index, which is the first copy);
 *   - the first RECN is the cell bank, the first RCMN the node table,
 *     the first RNAN the cell animation;
 *   - every RLCN is concatenated, in local order, into one palette-bank
 *     table, which is what an OAM's palette nibble indexes into.
 *
 * A member whose compression stream does not decode is skipped rather
 * than fatal.  The archive carries unused duplicates with corrupt or
 * truncated streams; letting one of those sink a block would lose
 * species that decode perfectly well from their real members.
 */

#include <stdlib.h>
#include <string.h>

#include "g2d.h"

#define FOURCC(a, b, c, d) \
    ((u32)(a) | ((u32)(b) << 8) | ((u32)(c) << 16) | ((u32)(d) << 24))

static int rig_own(G2dRig *rig, u8 *buf)
{
    u8 **grown = (u8 **)realloc(rig->owned, (rig->owned_count + 1) * sizeof(u8 *));

    if (grown == NULL) {
        free(buf);
        return -1;
    }
    rig->owned = grown;
    rig->owned[rig->owned_count++] = buf;
    return 0;
}

/*
 * One member's post-decompression bytes, owned by the rig.  Returns NULL
 * (with *size 0) when the member is unreadable or its container does not
 * decode: the caller treats that exactly as "unrecognized magic".
 */
static const u8 *rig_member(G2dRig *rig, BwArcFile *arc, u32 index, u32 *size)
{
    long raw_size = bw_arc_member_size(arc, index);
    u8 *raw;
    u8 *dec;
    u32 dec_size = 0;

    *size = 0;
    if (raw_size <= 0) {
        return NULL;
    }
    raw = (u8 *)malloc((size_t)raw_size);
    if (raw == NULL) {
        return NULL;
    }
    if (bw_arc_read_member(arc, index, raw, (u32)raw_size) != raw_size) {
        free(raw);
        return NULL;
    }
    dec = g2d_decompress(raw, (u32)raw_size, &dec_size);
    if (dec != NULL) {
        free(raw);
        if (rig_own(rig, dec) != 0) {
            return NULL;
        }
        *size = dec_size;
        return dec;
    }
    /*
     * No container this decoder handles.  If the first byte merely looked
     * like a compression tag, the member is used as it is; that is the
     * same outcome the asset tool reaches by catching its decoder's error.
     */
    if (rig_own(rig, raw) != 0) {
        return NULL;
    }
    *size = (u32)raw_size;
    return raw;
}

int g2d_rig_load(G2dRig *rig, BwArcFile *arc, u32 block_start, u32 block_size)
{
    const u8 *ncgr = NULL, *ncer = NULL, *nmcr = NULL, *nanr = NULL;
    u32 ncgr_size = 0, ncer_size = 0, nmcr_size = 0, nanr_size = 0;
    u32 local;

    memset(rig, 0, sizeof(*rig));

    for (local = 0; local < block_size; local++) {
        u32 index = block_start + local;
        u32 size = 0;
        const u8 *data;
        u32 magic;

        if (index >= arc->count) {
            break;
        }
        data = rig_member(rig, arc, index, &size);
        if (data == NULL || size < 4) {
            continue;
        }
        magic = bw_rd32(data);
        if (magic == FOURCC('R', 'G', 'C', 'N')) {
            if (size > ncgr_size) {         /* largest wins, first on a tie */
                ncgr = data;
                ncgr_size = size;
            }
        } else if (magic == FOURCC('R', 'E', 'C', 'N')) {
            if (ncer == NULL) { ncer = data; ncer_size = size; }
        } else if (magic == FOURCC('R', 'C', 'M', 'N')) {
            if (nmcr == NULL) { nmcr = data; nmcr_size = size; }
        } else if (magic == FOURCC('R', 'N', 'A', 'N')) {
            if (nanr == NULL) { nanr = data; nanr_size = size; }
        } else if (magic == FOURCC('R', 'L', 'C', 'N')) {
            (void)g2d_palette_append_nclr(&rig->palette, data, size);
        }
    }

    if (ncgr == NULL || ncer == NULL || nmcr == NULL) {
        g2d_rig_free(rig);
        return -1;
    }
    if (g2d_ncgr_parse(&rig->ncgr, ncgr, ncgr_size) != 0
        || g2d_ncer_parse(&rig->ncer, ncer, ncer_size) != 0
        || g2d_nmcr_parse(&rig->nmcr, nmcr, nmcr_size) != 0) {
        g2d_rig_free(rig);
        return -1;
    }
    /* The animation is a refinement, not a requirement, for a bare cell
     * layout; without it no node resolves a cell and the pose is empty. */
    if (nanr != NULL && g2d_nanr_parse(&rig->nanr, nanr, nanr_size) == 0) {
        rig->has_nanr = 1;
    }
    return 0;
}

int g2d_rig_load_palette(G2dRig *rig, BwArcFile *arc, u32 index)
{
    u32 size = 0;
    const u8 *data = index < arc->count ? rig_member(rig, arc, index, &size) : NULL;

    if (data == NULL || size < 4 || bw_rd32(data) != FOURCC('R', 'L', 'C', 'N')) {
        return -1;
    }
    return g2d_palette_append_nclr(&rig->palette, data, size);
}

void g2d_rig_free(G2dRig *rig)
{
    u32 i;

    g2d_ncgr_free(&rig->ncgr);
    g2d_ncer_free(&rig->ncer);
    g2d_nmcr_free(&rig->nmcr);
    g2d_nanr_free(&rig->nanr);
    g2d_palette_free(&rig->palette);
    for (i = 0; i < rig->owned_count; i++) {
        free(rig->owned[i]);
    }
    free(rig->owned);
    rig->owned = NULL;
    rig->owned_count = 0;
    rig->has_nanr = 0;
}
