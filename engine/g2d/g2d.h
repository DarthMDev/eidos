/*
 * engine/g2d/g2d.h -- the DS 2D (OBJ/OAM) software compositor.
 *
 * This is the first piece of eidos's 2D engine shim: a pure C99 library,
 * no SDL and no third-party code, that turns the Nitro 2D resources the
 * game ships (NCGR character data, NCLR palettes, NCER cell banks, NANR
 * cell animations, NMCR multi-cell node tables) into a 32-bit RGBA
 * buffer, using the DS OBJ rules the decompilation attests.
 *
 * Where the behaviour comes from, in order of authority:
 *
 *   1. aletheia's matched sources.  src/matched/unit_02067B40.c is the
 *      composer's flip lane and states the per-OAM flip arithmetic
 *      (x' = -(x + objWidth), y' = -(y + objHeight)) together with the
 *      two OBJ dimension tables at 0x020A17F0 / 0x020A17D8.
 *      src/matched/unit_020640B0.c is the per-node SRT control block and
 *      states that the NMCR node's own signed (x, y) is written into the
 *      node's translation verbatim, with no second placement source.
 *   2. aletheia's docs/ASSETS.md, which records the attested painter's
 *      order (func_020643CC walks nodes forward, func_02067998 walks a
 *      cell's OAMs forward, the earliest-emitted OAM lands in the lowest
 *      hardware OAM slot, and a lower slot draws in front; attr2's
 *      priority outranks the slot index), and the affine double-size
 *      correction from func_02066E30 (attr0 bits 8-9 == 3 shifts the
 *      entry by half its own width and height).
 *   3. public hardware documentation of the NDS OBJ attribute layout for
 *      the plain bitfield positions (shape/size/flip/priority/palette).
 *
 * Nothing here is derived from any other decompilation, port or emulator.
 *
 * Ownership: every parser below borrows the caller's buffer and does not
 * copy it, except where a field is documented as owned.  A G2dRig owns
 * everything it loaded and is released with g2d_rig_free().
 */

#ifndef EIDOS_ENGINE_G2D_H
#define EIDOS_ENGINE_G2D_H

#include <stddef.h>

#include "port.h"
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* BIOS-style container decompression (LZ77 0x10 / LZ11 0x11)          */
/* ------------------------------------------------------------------ */

/*
 * A NARC sub-file may be wrapped in a BIOS compression container.  The
 * tag byte is 0x10 (LZ77) or 0x11 (LZ11) with a 24-bit decompressed size
 * in the next three bytes; LZ11 spells a zero there as "a 32-bit size
 * follows".  Any other tag is not a container this engine decodes.
 *
 * Returns a malloc'd buffer of *out_size bytes, or NULL when `data` does
 * not open with a container this decoder handles, or when the stream is
 * malformed.  A stream that reaches its declared size with more than
 * three bytes of its container left over is rejected: the tag byte was
 * not a real header, just a coincidental first byte (the same guard
 * aletheia's tools/assets/narc.py applies, and the reason several rig
 * sub-files that "sniff" as LZ77 are skipped rather than mis-decoded).
 */
u8 *g2d_decompress(const u8 *data, u32 size, u32 *out_size);

/* ------------------------------------------------------------------ */
/* NCLR -- palette                                                     */
/* ------------------------------------------------------------------ */

typedef struct G2dColor {
    u8 r, g, b;
} G2dColor;

typedef struct G2dPalette {
    G2dColor *colors;   /* owned */
    u32       count;
} G2dPalette;

/*
 * Append one RLCN (NCLR) resource's colours to `pal`, growing it.  BGR555
 * is expanded to 8 bits per channel as `c * 255 / 31`.  A block's OAM
 * palette-bank nibble indexes into the concatenation of every NCLR of
 * that block, in local order, which is what g2d_rig_load() builds.
 */
int  g2d_palette_append_nclr(G2dPalette *pal, const u8 *data, u32 size);
void g2d_palette_free(G2dPalette *pal);

/* ------------------------------------------------------------------ */
/* NCGR -- character (tile) data                                       */
/* ------------------------------------------------------------------ */

#define G2D_NCGR_FMT_4BPP 3
#define G2D_NCGR_FMT_8BPP 4

/* "unspecified, derive from data" sentinel in the CHAR block's dims */
#define G2D_NCGR_DIM_SENTINEL 0xFFFFu

typedef struct G2dNcgr {
    u16 tile_num_x;      /* sheet width in 8x8 blocks, or the sentinel  */
    u16 tile_num_y;      /* sheet height in 8x8 blocks, or the sentinel */
    u32 pixel_fmt;       /* 3 = 4bpp, 4 = 8bpp                          */
    u32 mapping_type;
    u32 char_format;     /* 0 = 8x8 tiled, 1 = linear bitmap            */
    int sentinel_dims;   /* either dimension is 0xFFFF                  */
    u32 width, height;   /* pixels; 0 when sentinel_dims                */
    u8 *pixels;          /* owned, width*height palette indices, or NULL */
    const u8 *tile_data; /* borrowed from the caller's buffer            */
    u32 tile_data_size;
    u32 bytes_per_tile;  /* 32 (4bpp) or 64 (8bpp)                       */
} G2dNcgr;

int  g2d_ncgr_parse(G2dNcgr *out, const u8 *data, u32 size);
void g2d_ncgr_free(G2dNcgr *ncgr);

/*
 * One 8x8 block's 64 indices, row-major.  The block index is
 * two-dimensional over the sheet: block i is at column i % tile_num_x,
 * row i / tile_num_x.  Out-of-range blocks read as index 0.
 */
void g2d_ncgr_block(const G2dNcgr *ncgr, u32 block_index, u8 out[64]);

/* ------------------------------------------------------------------ */
/* NCER -- cell bank (OAM records)                                     */
/* ------------------------------------------------------------------ */

typedef struct G2dOam {
    s32 x, y;          /* cell-local, with the attested corrections applied */
    u16 shape, size;
    u16 tile_idx;
    u16 priority;      /* attr2 bits 10-11, lower draws in front            */
    u16 pal;           /* attr2 bits 12-15, 16-colour bank selector         */
    int hflip, vflip;
    int affine_double; /* attr0 bits 8-9 == 3                               */
    u32 w, h;          /* object dimensions from shape/size                 */
} G2dOam;

typedef struct G2dCell {
    u16     num_oam;
    u16     cell_attr;
    G2dOam *oams;      /* owned */
} G2dCell;

typedef struct G2dNcer {
    u16      num_cells;
    u16      cell_info_type;  /* 0 = 8-byte cell record, 1 = 16-byte + bbox */
    u32      mapping_type;
    G2dCell *cells;           /* owned */
} G2dNcer;

int  g2d_ncer_parse(G2dNcer *out, const u8 *data, u32 size);
void g2d_ncer_free(G2dNcer *ncer);

/* The two OBJ dimension tables, indexed [shape][size]. */
u32 g2d_obj_width(u32 shape, u32 size);
u32 g2d_obj_height(u32 shape, u32 size);

/* ------------------------------------------------------------------ */
/* NANR -- cell animation                                             */
/* ------------------------------------------------------------------ */

#define G2D_ANIM_ELEMENT_INDEX 0
#define G2D_ANIM_ELEMENT_SRT   1
#define G2D_ANIM_ELEMENT_T     2

typedef struct G2dNanrSequence {
    u16 num_frames;
    u16 loop_start;
    u32 anim_type;   /* low byte selects the frame-data record shape */
    u32 play_mode;
    u32 frames_ofs;  /* byte offset into the shared frame-entry array */
} G2dNanrSequence;

typedef struct G2dNanr {
    u16 num_sequences;
    u16 num_elements;
    u32 seq_ofs, frame_ofs, frame_data_ofs;
    const u8 *body;   /* borrowed */
    u32 body_size;
    G2dNanrSequence *sequences; /* owned */
} G2dNanr;

typedef struct G2dFrame {
    u16 cell_idx;
    s16 px, py;
    s32 sx, sy;   /* fx32, 4096 == 1.0 */
    u16 rot_z;
} G2dFrame;

int  g2d_nanr_parse(G2dNanr *out, const u8 *data, u32 size);
void g2d_nanr_free(G2dNanr *nanr);

/* 0 on success, -1 when the sequence has no such frame. */
int  g2d_nanr_frame_data(const G2dNanr *nanr, u32 seq, u32 frame, G2dFrame *out);

/*
 * The frame index active at elapsed time `t` (NDS frames), summing the
 * per-entry durations and looping from loopStartFrameIdx once the
 * sequence's own total is exceeded -- the same walk func_020643CC runs.
 */
u32 g2d_nanr_frame_at_time(const G2dNanr *nanr, u32 seq, u32 t);

/* ------------------------------------------------------------------ */
/* NMCR -- multi-cell node placement                                   */
/* ------------------------------------------------------------------ */

typedef struct G2dMcNode {
    u16 anim_seq_idx;
    s16 x, y;
    u8  node_attr;
    u8  cell_anim_slot;
} G2dMcNode;

typedef struct G2dMcGroup {
    u16        num_nodes;
    u16        num_cell_anims;
    G2dMcNode *nodes;   /* owned */
} G2dMcGroup;

typedef struct G2dNmcr {
    u16         num_multi_cell;
    u16         signature;
    G2dMcGroup *groups; /* owned */
} G2dNmcr;

int  g2d_nmcr_parse(G2dNmcr *out, const u8 *data, u32 size);
void g2d_nmcr_free(G2dNmcr *nmcr);

/* ------------------------------------------------------------------ */
/* Compositing                                                         */
/* ------------------------------------------------------------------ */

typedef struct G2dPose {
    u32 width, height;
    u8 *indices;   /* owned, width*height palette indices; 0 = transparent */
} G2dPose;

/*
 * Compose one multi-cell group at elapsed time `t` onto its own bounding
 * canvas (two pixels of margin on every side, the convention the oracle
 * uses so the two rasters are directly comparable).
 *
 * Returns 0 on success, -1 on a parse/bounds failure, and
 * G2D_ERR_NONIDENTITY when a node's active frame carries a non-identity
 * SRT: the affine resampling path is not part of this first piece, and
 * failing loudly is better than silently drawing the identity pose.
 */
#define G2D_ERR_NONIDENTITY (-2)

int  g2d_compose_pose(const G2dNcgr *ncgr, const G2dNcer *ncer,
                      const G2dNanr *nanr, const G2dMcGroup *group,
                      u32 t, u32 n_pal_colors, G2dPose *out);
void g2d_pose_free(G2dPose *pose);

/*
 * Expand a composed pose into a 32-bit RGBA buffer (width*height*4 bytes,
 * caller-allocated).  Index 0 is the transparent/background index and is
 * written as the opaque dark grey (16, 16, 16) the oracle uses so a
 * pixel-for-pixel comparison has no alpha ambiguity; an index past the
 * end of the palette is written as magenta (255, 0, 255).  Every pixel
 * is fully opaque.
 */
void g2d_pose_to_rgba(const G2dPose *pose, const G2dPalette *pal, u8 *rgba);

/* ------------------------------------------------------------------ */
/* NSCR -- BG screen (tile map)                                        */
/* ------------------------------------------------------------------ */

/*
 * The second piece of the 2D engine: text-mode BG layers.
 *
 * Where the behaviour comes from, in the same order of authority as the
 * OBJ half above:
 *
 *   1. aletheia's matched sources.  src/matched/func_02040EE0.c is the
 *      (x, y) -> screen-entry index helper the whole BG layer uses and
 *      spells out the VRAM screen layout of every size the game's size
 *      code enumerates (see g2d_bg_screen_index).  docs/notes for
 *      func_0203FCA0 (bg_sys.c's per-layer initialiser) pins the BGxCNT
 *      bit assignment this engine's layer fields mirror: bits 0-1
 *      priority, bits 2-4 character base, bit 7 the 16/256 colour mode,
 *      bits 8-12 screen base, bit 13 (extended palette for a text layer,
 *      wraparound for an affine one), bits 14-15 screen size.
 *      src/matched/func_02042ED8.c is the text-screen fill and shows the
 *      screen entry is one u16 per tile; src/matched/func_02042E34.c is
 *      the 8bpp-screen fill, one byte per entry, which is the affine
 *      case this engine does not render yet.
 *   2. aletheia's tools/assets/background.py and docs/ASSETS.md's NSCR
 *      sections for the on-disk NSCR chunk and for the NCGR/NCLR pairing
 *      rule (a property of the archives, not of the hardware, so the
 *      engine does not decide it: the caller names the pair).
 *   3. public hardware documentation of the NDS BG screen entry for the
 *      plain bitfield positions.
 */

/* Screen entry (u16): tile index, H/V flip, 16-colour palette row. */
#define G2D_SCR_TILE(e)  ((u16)((e) & 0x3FF))
#define G2D_SCR_HFLIP(e) ((int)(((e) >> 10) & 1))
#define G2D_SCR_VFLIP(e) ((int)(((e) >> 11) & 1))
#define G2D_SCR_PAL(e)   ((u16)(((e) >> 12) & 0xF))

typedef struct G2dNscr {
    u16  width, height;        /* pixels, as the SCRN sub-header declares */
    u16  tiles_wide, tiles_high;
    u32  unk;                  /* preserved verbatim; 0 or 1 in this ROM  */
    u16 *entries;              /* owned */
    u32  count;
} G2dNscr;

int  g2d_nscr_parse(G2dNscr *out, const u8 *data, u32 size);
void g2d_nscr_free(G2dNscr *nscr);

/*
 * Read one archive member whole and decompress it when it opens with a
 * BIOS container this engine decodes.  Returns a malloc'd buffer of
 * *out_size bytes that the caller frees, or NULL.  (The rig loader has
 * its own copy of this that hands ownership to the G2dRig; a BG screen
 * has no bundle object to own it, so the caller does.)
 */
u8 *g2d_arc_member(BwArcFile *arc, u32 index, u32 *out_size);

/*
 * One 8x8 tile's 64 indices out of an NCGR's character data, addressed
 * flat by `tile_index * bytes_per_tile` -- which is how a BG screen entry
 * addresses character memory, independent of the CHAR sub-header's
 * tile_num_x/y (those describe a sheet's shape, and a BG tileset's are
 * routinely the 0xFFFF sentinel).  A tile index past the end of the
 * character data reads as 64 zeroes, the same tolerance
 * background.py's NcgrTiles.tile_pixels applies.
 */
void g2d_bg_tile(const G2dNcgr *ncgr, u32 tile_index, int hflip, int vflip,
                 u8 out[64]);

/*
 * The game's BG screen-size codes, as src/matched/func_02040EE0.c
 * enumerates them, and that function's own (x, y) -> entry index mapping.
 * Code 0 is the 16x16-tile (128x128 pixel) affine size; codes 1 to 4 are
 * the four text-mode sizes.  Code 3 (512x256) puts the second 32x32 map
 * after the first, and code 4 (512x512) lays four out in a 2x2 grid --
 * the reason a BG layer cannot simply index its map row-major.
 */
#define G2D_BG_SIZE_128x128 0u
#define G2D_BG_SIZE_256x256 1u
#define G2D_BG_SIZE_256x512 2u
#define G2D_BG_SIZE_512x256 3u
#define G2D_BG_SIZE_512x512 4u
#define G2D_BG_SIZE_NONE    5u

/*
 * G2D_BG_SIZE_FLAT is not a hardware code: it selects the row-major
 * reading of an NSCR's entry array at the resource's own declared
 * dimensions.  It is what the asset lane's renderer does, and it is the
 * only reading available for the many NSCR resources whose declared size
 * is not a hardware screen size at all (a/1/4/3#22 is 512x192).  See the
 * README's discrepancy note.
 */
#define G2D_BG_SIZE_FLAT    0x100u

u32 g2d_bg_screen_index(int x, int y, u32 size_code);

/* Map dimensions in tiles for a size code; 0 for an unknown code. */
void g2d_bg_map_tiles(u32 size_code, u32 *tiles_wide, u32 *tiles_high);

/* ------------------------------------------------------------------ */
/* BG layers and the frame compositor                                  */
/* ------------------------------------------------------------------ */

typedef struct G2dBgLayer {
    const G2dNscr    *nscr;
    const G2dNcgr    *ncgr;
    const G2dPalette *pal;
    u32 screen_size;   /* a G2D_BG_SIZE_* code, or G2D_BG_SIZE_FLAT */
    u32 priority;      /* BGxCNT bits 0-1; lower draws in front      */
    s32 hofs, vofs;    /* BGxHOFS / BGxVOFS, in pixels               */
    int wrap;          /* a hardware text layer always wraps its map */
    int enabled;
} G2dBgLayer;

/*
 * Sample one layer at screen pixel (sx, sy), after scrolling.  Returns 1
 * and writes the palette index and the colour-table index it resolves to
 * when the layer covers that pixel, 0 when it does not (off a non-wrapping
 * map, or past the end of the entry array).  Whether index 0 counts as
 * transparent is the caller's decision: on hardware it is, and
 * g2d_frame_compose treats it so; the asset lane's single-layer renderer
 * paints it as its palette colour instead, and g2d_bg_render matches that.
 */
int g2d_bg_sample(const G2dBgLayer *layer, s32 sx, s32 sy,
                  u8 *out_index, u32 *out_color_index);

/*
 * Render one layer on its own into a width*height RGBA buffer
 * (caller-allocated, width*height*4 bytes), every pixel opaque.  A pixel
 * the layer does not cover is black; a colour index past the end of the
 * palette is magenta; palette index 0 is painted, not skipped.  With
 * screen_size G2D_BG_SIZE_FLAT, no scroll and the NSCR's own dimensions
 * this reproduces aletheia's background.py compose() exactly, which is
 * what tests/test_bg.c checks against the oracle.
 */
void g2d_bg_render(const G2dBgLayer *layer, u32 width, u32 height, u8 *rgba);

/*
 * One OBJ entry for the frame compositor: a composed pose (as
 * g2d_compose_pose produces), its top-left position on screen, the
 * palette its indices resolve through, and its OBJ priority.
 */
typedef struct G2dObjEntry {
    const G2dPose    *pose;
    const G2dPalette *pal;
    s32 x, y;
    u32 priority;      /* attr2 bits 10-11 */
} G2dObjEntry;

#define G2D_SCREEN_WIDTH  256u
#define G2D_SCREEN_HEIGHT 192u

/*
 * Layer up to four BGs and an OBJ list into one RGBA frame.
 *
 * Painter's order, back to front: the backdrop (colour 0 of
 * `backdrop_pal`, the BG palette's first entry), then priority 3 down to
 * priority 0.  Within one priority the BG layers are drawn in descending
 * array order, so bgs[0] (BG0) ends in front of bgs[3] (BG3); the OBJ
 * entries of that priority are drawn after the BGs, so an OBJ wins a tie
 * against a BG.  The OBJ list itself is drawn in reverse order, keeping
 * the OBJ half's convention that the earliest-emitted entry occupies the
 * lowest hardware OAM slot and a lower slot draws in front.
 *
 * Palette index 0 is transparent for both a BG tile and an OBJ pixel.
 * `rgba` is caller-allocated, width*height*4 bytes; every pixel is
 * opaque.
 */
void g2d_frame_compose(const G2dBgLayer *bgs, u32 n_bgs,
                       const G2dObjEntry *objs, u32 n_objs,
                       const G2dPalette *backdrop_pal,
                       u32 width, u32 height, u8 *rgba);

/* ------------------------------------------------------------------ */
/* Rig blocks (a/0/0/4 and its siblings)                               */
/* ------------------------------------------------------------------ */

typedef struct G2dRig {
    G2dNcgr    ncgr;
    G2dNcer    ncer;
    G2dNanr    nanr;
    G2dNmcr    nmcr;
    G2dPalette palette;
    int        has_nanr;
    u8       **owned;     /* the decompressed member buffers */
    u32        owned_count;
} G2dRig;

/*
 * Load one rig bundle out of an opened archive: every sub-file of the
 * block is classified by its own post-decompression magic (the largest
 * RGCN member is the main sheet, the first RECN/RCMN/RNAN member is the
 * cell bank / node table / animation, every RLCN is concatenated into
 * one palette-bank table).  A member whose compression stream is corrupt
 * is skipped, not fatal: the rig archive carries unused duplicates whose
 * streams do not decode and which no pose needs.
 */
int  g2d_rig_load(G2dRig *rig, BwArcFile *arc, u32 block_start, u32 block_size);
void g2d_rig_free(G2dRig *rig);

#ifdef __cplusplus
}
#endif

#endif /* EIDOS_ENGINE_G2D_H */
