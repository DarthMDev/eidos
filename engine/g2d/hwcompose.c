/*
 * engine/g2d/hwcompose.c -- both DS screens from the 2D hardware state.
 *
 * The asset path (bg.c, bg_affine.c, compose.c, window.c) draws what a
 * parsed NSCR/NCGR/NCLR/NCER says.  Once the game's own code runs, the
 * picture is what that code wrote into the hardware: DISPCNT and the BG,
 * window and blend registers, palette RAM, OAM, and VRAM through the bank
 * mapping.  This file reads those bytes and builds the very layer
 * descriptions the asset path builds, then calls g2d_frame_compose_desc,
 * so every rule the two share (screen-entry layout, tile fetch, the 8.8
 * affine fetch, priority, windows, the colour effects) is one piece of
 * code, not two.
 *
 * What maps onto an existing layer type, and how:
 *
 *   text BG             G2dBgLayer over a G2dNscr whose entries are the
 *                       map read out of VRAM at the screen base, and a
 *                       G2dNcgr whose tile data points into VRAM at the
 *                       character base.  The four hardware sizes are the
 *                       game's size codes 1..4 (256x256, 512x256 is code
 *                       3, 256x512 code 2, 512x512 code 4) read in VRAM
 *                       order, which is what g2d_bg_screen_index already
 *                       transcribes from func_02040EE0.
 *   affine BG           G2dBgAffineLayer, the map and the 8bpp tiles in
 *                       VRAM, BGxPA..PD and BGxX/BGxY straight from the
 *                       registers.
 *   OBJ                 resolved here into the OBJ line buffer the
 *                       hardware has (one OBJ pixel per screen pixel, see
 *                       below) and handed over as full-screen poses.
 *   the 3D layer        G2dLayer3d at BG0, exactly as g2d composes it.
 *   windows, blending   G2dWindows and G2dBlend from WIN0H..WINOUT and
 *                       BLDCNT/BLDALPHA/BLDY.
 *
 * What no asset layer expresses (an extended rotation/scaling BG with
 * 16-bit map entries, a 256-colour or direct-colour bitmap BG, a 256-colour
 * text BG through an extended palette) is resolved from the same bytes
 * into a G2dLayerRaster, which bg.c's stack walks like any other BG.
 *
 * **The OBJ line buffer.**  The hardware does not stack OBJs: it renders
 * them into one OBJ pixel per screen position first and only then sets
 * that one pixel against the BGs.  Among OBJs the lower priority value
 * wins, and on a tie the lower OAM index.  So two overlapping OBJs never
 * blend with each other, and an OBJ hidden under another OBJ is not "the
 * layer beneath" for alpha blending.  The line buffer is built here with
 * that rule and handed to the compositor as one pose per (priority,
 * palette, semi-transparency) actually used; the poses never overlap, so
 * the compositor sees at most one OBJ sample per pixel, which is the
 * hardware's picture.  OBJs in OBJ-window mode draw nothing and form the
 * OBJ window mask instead.
 *
 * **Tile addressing.**  An OBJ's tile number is in 32-byte units with 2D
 * mapping (a 32-tile-wide sheet, so a block row steps 1024 bytes) and in
 * units of the DISPCNT boundary (32 << bits 20-21) with 1D mapping, where
 * an OBJ's tiles are consecutive.  A BG's character base is DISPCNT bits
 * 24-26 times 64 KB plus BGxCNT bits 2-5 times 16 KB, and its screen base
 * DISPCNT bits 27-29 times 64 KB plus BGxCNT bits 8-12 times 2 KB; the
 * DISPCNT halves exist on the main engine only.  A bitmap BG's base is
 * BGxCNT bits 8-12 times 16 KB.  Every address wraps inside its engine's
 * window, as the hardware's mirrored windows do.
 *
 * **Colour.**  Palette RAM and extended palettes are BGR555; a colour is
 * expanded as `c * 255 / 31`, the same expansion NCLR parsing uses, so
 * the 5-bit fold in window.c is exact on every colour drawn here.  The
 * master brightness (MASTER_BRIGHT, 0x0400006C / 0x0400106C) is applied
 * last with the same arithmetic as BLDY.  On hardware it acts on the
 * 6-bit-per-channel output stage; this does it in 5 bits, which can differ
 * by one 6-bit step on a partially faded pixel and is exact at factor 0
 * and 16.
 *
 * Not drawn, and reported through the G2D_HW_UNSUP_* flags: BG mode 6's
 * large-screen bitmap, BG mode 7, display mode 3 (main memory display),
 * bitmap OBJs, and mosaic (ignored: the layer is drawn without it).  The
 * display capture is out of scope.  BG0HOFS does not scroll the 3D layer
 * here.
 *
 * The register layouts are public NDS hardware documentation.  Nothing
 * here is derived from any other decompilation, port or emulator.
 */

#include <stdlib.h>
#include <string.h>

#include "dsmem.h"
#include "g2d.h"

#define SCREEN_W   G2D_SCREEN_WIDTH
#define SCREEN_H   G2D_SCREEN_HEIGHT
#define SCREEN_PX  (SCREEN_W * SCREEN_H)

/* Byte offsets into one engine's half of the register file. */
#define REG_DISPCNT   0x00u
#define REG_BG0CNT    0x08u
#define REG_BG0HOFS   0x10u
#define REG_BG2PA     0x20u
#define REG_BG2X      0x28u
#define REG_WIN0H     0x40u
#define REG_WIN1H     0x42u
#define REG_WIN0V     0x44u
#define REG_WIN1V     0x46u
#define REG_WININ     0x48u
#define REG_WINOUT    0x4Au
#define REG_BLDCNT    0x50u
#define REG_BLDALPHA  0x52u
#define REG_BLDY      0x54u
#define REG_MASTER_BRIGHT 0x6Cu
#define REG_SUB_BASE  0x1000u
#define REG_POWCNT1   0x304u

/* The extra bytes after each BG window copy, so a tile block or a map that
 * starts near the end of the window reads its wrapped continuation. */
#define BG_GUARD 0x10000u

/* What one hardware BG is under the current BG mode. */
enum { KIND_NONE, KIND_TEXT, KIND_AFFINE, KIND_EXT, KIND_LARGE };

static const u8 k_bg_kind[8][4] = {
    { KIND_TEXT, KIND_TEXT, KIND_TEXT,   KIND_TEXT   },   /* mode 0 */
    { KIND_TEXT, KIND_TEXT, KIND_TEXT,   KIND_AFFINE },   /* mode 1 */
    { KIND_TEXT, KIND_TEXT, KIND_AFFINE, KIND_AFFINE },   /* mode 2 */
    { KIND_TEXT, KIND_TEXT, KIND_TEXT,   KIND_EXT    },   /* mode 3 */
    { KIND_TEXT, KIND_TEXT, KIND_AFFINE, KIND_EXT    },   /* mode 4 */
    { KIND_TEXT, KIND_TEXT, KIND_EXT,    KIND_EXT    },   /* mode 5 */
    { KIND_TEXT, KIND_NONE, KIND_LARGE,  KIND_NONE   },   /* mode 6 */
    { KIND_NONE, KIND_NONE, KIND_NONE,   KIND_NONE   },   /* mode 7 */
};

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

static G2dColor bgr555(u16 c)
{
    G2dColor out;

    out.r = (u8)((u32)(c & 0x1Fu) * 255u / 31u);
    out.g = (u8)((u32)((c >> 5) & 0x1Fu) * 255u / 31u);
    out.b = (u8)((u32)((c >> 10) & 0x1Fu) * 255u / 31u);
    return out;
}

static void put_rgb(u8 *px, G2dColor c)
{
    px[0] = c.r;
    px[1] = c.g;
    px[2] = c.b;
    px[3] = 255;
}

static void fill(u8 *rgba, u8 v)
{
    u32 i;

    for (i = 0; i < SCREEN_PX; i++) {
        rgba[i * 4u + 0u] = v;
        rgba[i * 4u + 1u] = v;
        rgba[i * 4u + 2u] = v;
        rgba[i * 4u + 3u] = 255;
    }
}

/* Gather 16 KB pages into one contiguous window, NULL pages as zero. */
static void gather(u8 *dst, const u8 *const *pages, u32 n_pages)
{
    u32 i;

    for (i = 0; i < n_pages; i++) {
        if (pages[i] != NULL) {
            memcpy(dst + (size_t)i * G2D_HW_PAGE_SIZE, pages[i],
                   G2D_HW_PAGE_SIZE);
        } else {
            memset(dst + (size_t)i * G2D_HW_PAGE_SIZE, 0, G2D_HW_PAGE_SIZE);
        }
    }
}

/* The 8.8 shift with the fraction discarded towards minus infinity, and
 * the 28-bit sign of BGxX/BGxY: the same two rules bg_affine.c applies. */
static s32 fx8_floor(s64 v)
{
    return (s32)((v >= 0) ? (v >> 8) : -(((-v) + 255) >> 8));
}

static s32 sign_extend_28(u32 u)
{
    u &= 0x0FFFFFFFu;
    if (u & 0x08000000u) {
        u |= 0xF0000000u;
    }
    return (s32)u;
}

/* ------------------------------------------------------------------ */
/* One engine's working state                                          */
/* ------------------------------------------------------------------ */

typedef struct Engine {
    const G2dHwState *hw;
    const u8 *io;          /* this engine's half of the register file */
    int       main;
    u32       dispcnt;
    u32       unsup;

    u8  *bgv;              /* BG window, contiguous, plus BG_GUARD    */
    u32  bg_size;
    u8  *objv;             /* OBJ window, contiguous                   */
    u32  obj_size;

    G2dColor   bg_colors[256], obj_colors[256];
    G2dPalette bg_pal, obj_pal;
    G2dColor  *obj_ext_colors;      /* 16 * 256, or NULL                */
    G2dPalette obj_ext_pal[16];

    /* Text layers: the map copies and the character views they borrow. */
    G2dNscr    nscr[4];
    G2dNcgr    ncgr[4];
    G2dBgLayer bgs[4];
    G2dBgAffineLayer abgs[2];
    u32        n_abgs;
    G2dLayerRaster rasters[4];
    u8        *raster_px[4];
    u32        n_rasters;
    G2dLayer3d layer3d;

    /* The OBJ line buffer. */
    u8  *obj_index;        /* palette index, 0 = no OBJ pixel           */
    u8  *obj_prio;
    u8  *obj_src;          /* 0 = OBJ palette, 1 + n = extended slot n  */
    u8  *obj_semi;
    u8  *objwin;
    G2dPose     *poses;    /* one per used key                          */
    G2dObjEntry *objs;
    u32          n_objs;
} Engine;

static u16 reg16(const Engine *e, u32 off)
{
    return bw_rd16(e->io + off);
}

static u32 reg32(const Engine *e, u32 off)
{
    return bw_rd32(e->io + off);
}

static void engine_free(Engine *e)
{
    u32 i;

    free(e->bgv);
    free(e->objv);
    free(e->obj_ext_colors);
    for (i = 0; i < 4u; i++) {
        free(e->nscr[i].entries);
        free(e->raster_px[i]);
    }
    if (e->poses != NULL) {
        for (i = 0; i < e->n_objs; i++) {
            free(e->poses[i].indices);
        }
    }
    free(e->poses);
    free(e->objs);
    free(e->obj_index);
    free(e->obj_prio);
    free(e->obj_src);
    free(e->obj_semi);
    free(e->objwin);
}

/* ------------------------------------------------------------------ */
/* BG layers                                                           */
/* ------------------------------------------------------------------ */

static u32 char_base(const Engine *e, u16 cnt)
{
    u32 base = ((u32)(cnt >> 2) & 0xFu) * 0x4000u;

    if (e->main) {
        base += ((e->dispcnt >> 24) & 7u) * 0x10000u;
    }
    return base % e->bg_size;
}

static u32 screen_base(const Engine *e, u16 cnt)
{
    u32 base = ((u32)(cnt >> 8) & 0x1Fu) * 0x800u;

    if (e->main) {
        base += ((e->dispcnt >> 27) & 7u) * 0x10000u;
    }
    return base % e->bg_size;
}

/* A view of the BG window from `ofs` on, as a G2dNcgr g2d_bg_tile reads. */
static void char_view(const Engine *e, u32 ofs, int bpp8, G2dNcgr *out)
{
    memset(out, 0, sizeof(*out));
    out->pixel_fmt = bpp8 ? G2D_NCGR_FMT_8BPP : G2D_NCGR_FMT_4BPP;
    out->bytes_per_tile = bpp8 ? 64u : 32u;
    out->tile_data = e->bgv + ofs;
    out->tile_data_size = e->bg_size + BG_GUARD - ofs;
    out->tile_num_x = G2D_NCGR_DIM_SENTINEL;
    out->tile_num_y = G2D_NCGR_DIM_SENTINEL;
    out->sentinel_dims = 1;
}

/* The game's size code for a hardware text size (BGxCNT bits 14-15). */
static u32 text_size_code(u32 hw_size)
{
    static const u32 k_code[4] = {
        G2D_BG_SIZE_256x256, G2D_BG_SIZE_512x256,
        G2D_BG_SIZE_256x512, G2D_BG_SIZE_512x512,
    };
    return k_code[hw_size & 3u];
}

static int build_text(Engine *e, u32 n, u16 cnt)
{
    G2dNscr *nscr = &e->nscr[n];
    G2dBgLayer *layer = &e->bgs[n];
    u32 code = text_size_code((u32)cnt >> 14);
    u32 tw, th, i, sb;

    g2d_bg_map_tiles(code, &tw, &th);
    sb = screen_base(e, cnt);
    nscr->tiles_wide = (u16)tw;
    nscr->tiles_high = (u16)th;
    nscr->width = (u16)(tw * 8u);
    nscr->height = (u16)(th * 8u);
    nscr->count = tw * th;
    nscr->entries = (u16 *)malloc((size_t)nscr->count * sizeof(u16));
    if (nscr->entries == NULL) {
        return -1;
    }
    /* At most 8 KB of map from a base inside the window: the guard holds
     * the wrapped continuation. */
    for (i = 0; i < nscr->count; i++) {
        nscr->entries[i] = bw_rd16(e->bgv + sb + i * 2u);
    }
    char_view(e, char_base(e, cnt), (cnt & 0x80u) != 0, &e->ncgr[n]);

    layer->nscr = nscr;
    layer->ncgr = &e->ncgr[n];
    layer->pal = &e->bg_pal;
    layer->screen_size = code;
    layer->screen_order = G2D_BG_ORDER_VRAM;
    layer->priority = cnt & 3u;
    layer->hofs = (s32)(reg16(e, REG_BG0HOFS + n * 4u) & 0x1FFu);
    layer->vofs = (s32)(reg16(e, REG_BG0HOFS + n * 4u + 2u) & 0x1FFu);
    layer->wrap = 1;
    layer->enabled = 1;
    return 0;
}

static void read_affine(const Engine *e, u32 n, G2dAffine *m, s32 *rx, s32 *ry)
{
    u32 r = (n - 2u) * 0x10u;

    m->pa = (s16)reg16(e, REG_BG2PA + r);
    m->pb = (s16)reg16(e, REG_BG2PA + r + 2u);
    m->pc = (s16)reg16(e, REG_BG2PA + r + 4u);
    m->pd = (s16)reg16(e, REG_BG2PA + r + 6u);
    *rx = sign_extend_28(reg32(e, REG_BG2X + r));
    *ry = sign_extend_28(reg32(e, REG_BG2X + r + 4u));
}

static void build_affine(Engine *e, u32 n, u16 cnt)
{
    G2dBgAffineLayer *layer = &e->abgs[e->n_abgs];
    u32 sb = screen_base(e, cnt);

    memset(layer, 0, sizeof(*layer));
    /* An affine layer's 8bpp tile view lives in the slot its own number
     * would use as a text layer; that slot is otherwise idle. */
    char_view(e, char_base(e, cnt), 1, &e->ncgr[n]);
    layer->map = e->bgv + sb;
    layer->map_size = e->bg_size + BG_GUARD - sb;
    layer->ncgr = &e->ncgr[n];
    layer->pal = &e->bg_pal;
    layer->screen_size = ((u32)cnt >> 14) & 3u;
    layer->bg_index = n;
    layer->priority = cnt & 3u;
    layer->wrap = (cnt & 0x2000u) != 0;
    read_affine(e, n, &layer->mtx, &layer->ref_x, &layer->ref_y);
    layer->enabled = 1;
    e->n_abgs++;
}

/* The extended palette slot for BG n, or NULL when extended palettes are
 * off or the slot is not mapped. */
static const u8 *bg_extpal(const Engine *e, u32 slot)
{
    if ((e->dispcnt & 0x40000000u) == 0) {
        return NULL;
    }
    return e->hw->bg_extpal[e->main ? 0 : 1][slot & 3u];
}

/*
 * The layer shapes the asset types do not cover, resolved per pixel into
 * a G2dLayerRaster.  `ext` is the extended palette slot or NULL.
 */
enum { RAS_TEXT_EXT, RAS_AFFINE_EXT, RAS_BITMAP256, RAS_DIRECT };

static int ras_pixel(const Engine *e, int kind, u32 n, u16 cnt,
                     const u8 *ext, s32 x, s32 y, G2dColor *out)
{
    if (kind == RAS_TEXT_EXT) {
        const G2dBgLayer *layer = &e->bgs[n];
        u32 tw = layer->nscr->tiles_wide, th = layer->nscr->tiles_high;
        u32 px = ((u32)(x + layer->hofs)) % (tw * 8u);
        u32 py = ((u32)(y + layer->vofs)) % (th * 8u);
        u16 entry = layer->nscr->entries[
            g2d_bg_screen_index((int)(px / 8u), (int)(py / 8u),
                                layer->screen_size)];
        u8 tile[64];
        u8 v;

        g2d_bg_tile(layer->ncgr, G2D_SCR_TILE(entry), G2D_SCR_HFLIP(entry),
                    G2D_SCR_VFLIP(entry), tile);
        v = tile[(py % 8u) * 8u + (px % 8u)];
        if (v == 0) {
            return 0;
        }
        *out = bgr555(bw_rd16(ext + ((u32)G2D_SCR_PAL(entry) * 256u + v) * 2u));
        return 1;
    } else {
        G2dAffine m;
        s32 rx, ry, tx, ty;
        u32 w, h;
        int wrap = (cnt & 0x2000u) != 0;

        read_affine(e, n, &m, &rx, &ry);
        if (kind == RAS_AFFINE_EXT) {
            w = h = 128u << (((u32)cnt >> 14) & 3u);
        } else {
            static const u32 k_w[4] = { 128u, 256u, 512u, 512u };
            static const u32 k_h[4] = { 128u, 256u, 256u, 512u };

            w = k_w[((u32)cnt >> 14) & 3u];
            h = k_h[((u32)cnt >> 14) & 3u];
        }
        tx = fx8_floor((s64)rx + (s64)m.pa * x + (s64)m.pb * y);
        ty = fx8_floor((s64)ry + (s64)m.pc * x + (s64)m.pd * y);
        if (wrap) {
            tx = (s32)((u32)tx & (w - 1u));
            ty = (s32)((u32)ty & (h - 1u));
        } else if (tx < 0 || ty < 0 || (u32)tx >= w || (u32)ty >= h) {
            return 0;
        }
        if (kind == RAS_AFFINE_EXT) {
            u32 sb = screen_base(e, cnt);
            u16 entry = bw_rd16(e->bgv + sb
                                + (((u32)ty / 8u) * (w / 8u) + (u32)tx / 8u) * 2u);
            u8 tile[64];
            u8 v;

            g2d_bg_tile(&e->ncgr[n], G2D_SCR_TILE(entry), G2D_SCR_HFLIP(entry),
                        G2D_SCR_VFLIP(entry), tile);
            v = tile[((u32)ty % 8u) * 8u + ((u32)tx % 8u)];
            if (v == 0) {
                return 0;
            }
            *out = (ext != NULL)
                ? bgr555(bw_rd16(ext + ((u32)G2D_SCR_PAL(entry) * 256u + v) * 2u))
                : e->bg_colors[v];
            return 1;
        } else {
            u32 base = ((u32)(cnt >> 8) & 0x1Fu) * 0x4000u;
            u32 at = (u32)ty * w + (u32)tx;

            if (kind == RAS_BITMAP256) {
                u8 v = e->bgv[(base + at) % e->bg_size];

                if (v == 0) {
                    return 0;
                }
                *out = e->bg_colors[v];
                return 1;
            } else {
                u32 a = (base + at * 2u) % e->bg_size;
                u16 c = bw_rd16(e->bgv + a);

                if ((c & 0x8000u) == 0) {
                    return 0;    /* bit 15 clear: a transparent pixel */
                }
                *out = bgr555(c);
                return 1;
            }
        }
    }
}

static int build_raster(Engine *e, int kind, u32 n, u16 cnt, const u8 *ext)
{
    u32 slot = e->n_rasters;
    G2dLayerRaster *r = &e->rasters[slot];
    u8 *px = (u8 *)calloc(SCREEN_PX, 4u);
    s32 x, y;

    if (px == NULL) {
        return -1;
    }
    e->raster_px[slot] = px;
    for (y = 0; y < (s32)SCREEN_H; y++) {
        for (x = 0; x < (s32)SCREEN_W; x++) {
            G2dColor c;

            if (ras_pixel(e, kind, n, cnt, ext, x, y, &c)) {
                put_rgb(px + ((size_t)y * SCREEN_W + (u32)x) * 4u, c);
            }
        }
    }
    memset(r, 0, sizeof(*r));
    r->rgba = px;
    r->width = SCREEN_W;
    r->height = SCREEN_H;
    r->priority = cnt & 3u;
    r->bg_index = n;
    r->enabled = 1;
    e->n_rasters++;
    return 0;
}

static int build_bgs(Engine *e, const u8 *rgba3d)
{
    u32 mode = e->dispcnt & 7u;
    u32 n;

    if (mode == 7u || (mode == 6u && !e->main)) {
        e->unsup |= G2D_HW_UNSUP_BG_MODE;
        return 0;
    }
    for (n = 0; n < 4u; n++) {
        u16 cnt = reg16(e, REG_BG0CNT + n * 2u);
        u32 kind = k_bg_kind[mode][n];

        if ((e->dispcnt & (0x100u << n)) == 0 || kind == KIND_NONE) {
            continue;
        }
        if (cnt & 0x40u) {
            e->unsup |= G2D_HW_UNSUP_MOSAIC;
        }
        if (n == 0 && e->main && (e->dispcnt & 0x8u)) {
            /* BG0 is the 3D layer. */
            if (rgba3d == NULL) {
                e->unsup |= G2D_HW_UNSUP_NO_3D;
                continue;
            }
            e->layer3d.rgba = rgba3d;
            e->layer3d.width = SCREEN_W;
            e->layer3d.height = SCREEN_H;
            e->layer3d.priority = cnt & 3u;
            e->layer3d.bg_index = 0;
            e->layer3d.enabled = 1;
            continue;
        }
        switch (kind) {
        case KIND_TEXT:
            if (build_text(e, n, cnt) != 0) {
                return -1;
            }
            if ((cnt & 0x80u) != 0) {
                /* A 256-colour text BG through an extended palette: BG0
                 * and BG1 take slot 2 and 3 instead with BGxCNT bit 13. */
                u32 slot = n;
                const u8 *ext;

                if (n < 2u && (cnt & 0x2000u)) {
                    slot += 2u;
                }
                ext = bg_extpal(e, slot);
                if ((e->dispcnt & 0x40000000u) != 0) {
                    static const u8 k_zero_pal[G2D_HW_EXTPAL_SLOT_SIZE];

                    if (build_raster(e, RAS_TEXT_EXT, n, cnt,
                                     ext != NULL ? ext : k_zero_pal) != 0) {
                        return -1;
                    }
                    e->bgs[n].enabled = 0;
                }
            }
            break;
        case KIND_AFFINE:
            build_affine(e, n, cnt);
            break;
        case KIND_EXT:
            if ((cnt & 0x80u) == 0) {
                char_view(e, char_base(e, cnt), 1, &e->ncgr[n]);
                if (build_raster(e, RAS_AFFINE_EXT, n, cnt,
                                 bg_extpal(e, n)) != 0) {
                    return -1;
                }
            } else if (build_raster(e, (cnt & 0x4u) ? RAS_DIRECT
                                                     : RAS_BITMAP256,
                                    n, cnt, NULL) != 0) {
                return -1;
            }
            break;
        default:   /* KIND_LARGE */
            e->unsup |= G2D_HW_UNSUP_BG_MODE;
            break;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* OBJs                                                                */
/* ------------------------------------------------------------------ */

/* One texel of an OBJ's block, raw (0 is transparent). */
static u8 obj_texel(const Engine *e, u32 tile, u32 w, int bpp8, u32 tx, u32 ty)
{
    u32 tile_bytes = bpp8 ? 64u : 32u;
    u32 col = tx / 8u, row = ty / 8u;
    u32 addr;
    u8 b;

    if (e->dispcnt & 0x10u) {
        u32 boundary = 32u << ((e->dispcnt >> 20) & 3u);

        addr = tile * boundary + (row * (w / 8u) + col) * tile_bytes;
    } else {
        addr = tile * 32u + row * 1024u + col * tile_bytes;
    }
    if (bpp8) {
        return e->objv[(addr + (ty % 8u) * 8u + tx % 8u) % e->obj_size];
    }
    b = e->objv[(addr + (ty % 8u) * 4u + (tx % 8u) / 2u) % e->obj_size];
    return (u8)((tx & 1u) ? (b >> 4) : (b & 0x0Fu));
}

static void paint_obj(Engine *e, const u8 *canvas, u32 cw, u32 ch,
                      u32 x0, u32 y0, u32 prio, u32 mode, int bpp8,
                      u32 palrow)
{
    int ext = bpp8 && (e->dispcnt & 0x80000000u) != 0;
    u32 cy, cx;

    for (cy = 0; cy < ch; cy++) {
        u32 sy = (y0 + cy) & 0xFFu;

        if (sy >= SCREEN_H) {
            continue;
        }
        for (cx = 0; cx < cw; cx++) {
            u32 sx = (x0 + cx) & 0x1FFu;
            u8 v = canvas[(size_t)cy * cw + cx];
            size_t at;

            if (sx >= SCREEN_W || v == 0) {
                continue;
            }
            at = (size_t)sy * SCREEN_W + sx;
            if (mode == 2u) {
                e->objwin[at] = 1;
                continue;
            }
            /* Lower priority value wins; a tie keeps the lower OAM index,
             * which was painted first. */
            if (e->obj_index[at] != 0 && e->obj_prio[at] <= prio) {
                continue;
            }
            e->obj_index[at] = bpp8 ? v : (u8)(palrow * 16u + v);
            e->obj_src[at] = (u8)(ext ? palrow + 1u : 0u);
            e->obj_prio[at] = (u8)prio;
            e->obj_semi[at] = (u8)(mode == 1u);
        }
    }
}

static int build_objs(Engine *e)
{
    const u8 *oam = e->hw->oam + (e->main ? 0u : 0x400u);
    u8 src[64 * 64];
    u8 canvas[128 * 128];
    u32 i;

    for (i = 0; i < 128u; i++) {
        const u8 *ent = oam + i * 8u;
        u16 a0 = bw_rd16(ent), a1 = bw_rd16(ent + 2), a2 = bw_rd16(ent + 4);
        int affine = (a0 & 0x100u) != 0;
        u32 mode = ((u32)a0 >> 10) & 3u;
        u32 shape = (u32)a0 >> 14;
        u32 w, h, cw, ch, tx, ty;
        int bpp8 = (a0 & 0x2000u) != 0;
        u32 tile = a2 & 0x3FFu;

        if (!affine && (a0 & 0x200u)) {
            continue;   /* disabled */
        }
        if (shape == 3u) {
            continue;   /* prohibited shape */
        }
        if (mode == 3u) {
            e->unsup |= G2D_HW_UNSUP_BITMAP_OBJ;
            continue;
        }
        if (a0 & 0x1000u) {
            e->unsup |= G2D_HW_UNSUP_MOSAIC;
        }
        w = g2d_obj_width(shape, (u32)a1 >> 14);
        h = g2d_obj_height(shape, (u32)a1 >> 14);
        for (ty = 0; ty < h; ty++) {
            for (tx = 0; tx < w; tx++) {
                src[ty * w + tx] = obj_texel(e, tile, w, bpp8, tx, ty);
            }
        }
        if (affine) {
            const u8 *grp = e->hw->oam + (e->main ? 0u : 0x400u)
                          + (((u32)a1 >> 9) & 0x1Fu) * 32u;
            G2dAffine m;
            int dbl = (a0 & 0x200u) != 0;

            m.pa = (s16)bw_rd16(grp + 6);
            m.pb = (s16)bw_rd16(grp + 14);
            m.pc = (s16)bw_rd16(grp + 22);
            m.pd = (s16)bw_rd16(grp + 30);
            g2d_obj_affine_size(w, h, dbl, &cw, &ch);
            g2d_obj_affine_render(src, w, h, &m, dbl, canvas);
        } else {
            int hf = (a1 & 0x1000u) != 0, vf = (a1 & 0x2000u) != 0;

            cw = w;
            ch = h;
            for (ty = 0; ty < h; ty++) {
                for (tx = 0; tx < w; tx++) {
                    u32 sx = hf ? w - 1u - tx : tx;
                    u32 sy = vf ? h - 1u - ty : ty;

                    canvas[ty * w + tx] = src[sy * w + sx];
                }
            }
        }
        paint_obj(e, canvas, cw, ch, a1 & 0x1FFu, a0 & 0xFFu,
                  ((u32)a2 >> 10) & 3u, mode, bpp8, ((u32)a2 >> 12) & 0xFu);
    }
    return 0;
}

/*
 * The line buffer as compositor input: one full-screen pose per (priority,
 * palette source, semi-transparency) key that occurs.  The poses partition
 * the OBJ pixels, so at most one of them is opaque at any pixel.
 */
#define OBJ_KEYS (4u * 17u * 2u)

static int emit_obj_poses(Engine *e)
{
    s32 key_slot[OBJ_KEYS];
    u32 k, i;

    for (k = 0; k < OBJ_KEYS; k++) {
        key_slot[k] = -1;
    }
    for (i = 0; i < SCREEN_PX; i++) {
        if (e->obj_index[i] != 0) {
            k = ((u32)e->obj_prio[i] * 17u + e->obj_src[i]) * 2u
              + e->obj_semi[i];
            key_slot[k] = 0;
        }
    }
    e->n_objs = 0;
    for (k = 0; k < OBJ_KEYS; k++) {
        if (key_slot[k] == 0) {
            e->n_objs++;
        }
    }
    if (e->n_objs == 0) {
        return 0;
    }
    e->poses = (G2dPose *)calloc(e->n_objs, sizeof(G2dPose));
    e->objs = (G2dObjEntry *)calloc(e->n_objs, sizeof(G2dObjEntry));
    if (e->poses == NULL || e->objs == NULL) {
        return -1;
    }
    i = 0;
    for (k = 0; k < OBJ_KEYS; k++) {
        u32 src;

        if (key_slot[k] != 0) {
            continue;
        }
        key_slot[k] = (s32)i;
        e->poses[i].width = SCREEN_W;
        e->poses[i].height = SCREEN_H;
        e->poses[i].indices = (u8 *)calloc(SCREEN_PX, 1);
        if (e->poses[i].indices == NULL) {
            return -1;
        }
        src = (k / 2u) % 17u;
        e->objs[i].pose = &e->poses[i];
        e->objs[i].pal = (src == 0) ? &e->obj_pal : &e->obj_ext_pal[src - 1u];
        e->objs[i].x = 0;
        e->objs[i].y = 0;
        e->objs[i].priority = k / 34u;
        e->objs[i].semi = (int)(k & 1u);
        i++;
    }
    for (i = 0; i < SCREEN_PX; i++) {
        if (e->obj_index[i] != 0) {
            k = ((u32)e->obj_prio[i] * 17u + e->obj_src[i]) * 2u
              + e->obj_semi[i];
            e->poses[key_slot[k]].indices[i] = e->obj_index[i];
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* One engine                                                          */
/* ------------------------------------------------------------------ */

static void master_bright(const Engine *e, u8 *rgba)
{
    u16 mb = reg16(e, REG_MASTER_BRIGHT);
    u32 mode = ((u32)mb >> 14) & 3u;
    u32 factor = mb & 0x1Fu;
    u32 i;

    if ((mode != 1u && mode != 2u) || factor == 0) {
        return;
    }
    for (i = 0; i < SCREEN_PX; i++) {
        u8 in[3];

        in[0] = rgba[i * 4u];
        in[1] = rgba[i * 4u + 1u];
        in[2] = rgba[i * 4u + 2u];
        g2d_blend_brightness(in, factor, mode == 1u, rgba + i * 4u);
    }
}

static int alloc_engine(Engine *e)
{
    const G2dHwState *hw = e->hw;
    int en = e->main ? 0 : 1;
    const u8 *pal = hw->pal + (e->main ? 0u : 0x400u);
    u32 i;

    e->bg_size = e->main ? 0x80000u : 0x20000u;
    e->obj_size = e->main ? 0x40000u : 0x20000u;
    e->bgv = (u8 *)malloc(e->bg_size + BG_GUARD);
    e->objv = (u8 *)malloc(e->obj_size);
    e->obj_index = (u8 *)calloc(SCREEN_PX, 1);
    e->obj_prio = (u8 *)calloc(SCREEN_PX, 1);
    e->obj_src = (u8 *)calloc(SCREEN_PX, 1);
    e->obj_semi = (u8 *)calloc(SCREEN_PX, 1);
    e->objwin = (u8 *)calloc(SCREEN_PX, 1);
    if (e->bgv == NULL || e->objv == NULL || e->obj_index == NULL
        || e->obj_prio == NULL || e->obj_src == NULL || e->obj_semi == NULL
        || e->objwin == NULL) {
        return -1;
    }
    gather(e->bgv, hw->bg_page[en], e->bg_size / G2D_HW_PAGE_SIZE);
    /* The window mirrors: what follows its end is its start again. */
    memcpy(e->bgv + e->bg_size, e->bgv, BG_GUARD);
    gather(e->objv, hw->obj_page[en], e->obj_size / G2D_HW_PAGE_SIZE);

    for (i = 0; i < 256u; i++) {
        e->bg_colors[i] = bgr555(bw_rd16(pal + i * 2u));
        e->obj_colors[i] = bgr555(bw_rd16(pal + 0x200u + i * 2u));
    }
    e->bg_pal.colors = e->bg_colors;
    e->bg_pal.count = 256;
    e->obj_pal.colors = e->obj_colors;
    e->obj_pal.count = 256;

    if ((e->dispcnt & 0x80000000u) != 0) {
        const u8 *ext = hw->obj_extpal[en];

        e->obj_ext_colors = (G2dColor *)malloc(16u * 256u * sizeof(G2dColor));
        if (e->obj_ext_colors == NULL) {
            return -1;
        }
        for (i = 0; i < 16u * 256u; i++) {
            e->obj_ext_colors[i] = (ext != NULL) ? bgr555(bw_rd16(ext + i * 2u))
                                                 : bgr555(0);
        }
        for (i = 0; i < 16u; i++) {
            e->obj_ext_pal[i].colors = e->obj_ext_colors + i * 256u;
            e->obj_ext_pal[i].count = 256;
        }
    }
    return 0;
}

static void build_windows(const Engine *e, G2dWindows *w)
{
    u16 h0 = reg16(e, REG_WIN0H), h1 = reg16(e, REG_WIN1H);
    u16 v0 = reg16(e, REG_WIN0V), v1 = reg16(e, REG_WIN1V);
    u16 win = reg16(e, REG_WININ), wout = reg16(e, REG_WINOUT);

    memset(w, 0, sizeof(*w));
    w->win0.enabled = (e->dispcnt & 0x2000u) != 0;
    w->win0.x1 = (u32)h0 >> 8;
    w->win0.x2 = h0 & 0xFFu;
    w->win0.y1 = (u32)v0 >> 8;
    w->win0.y2 = v0 & 0xFFu;
    w->win0.layers = win & 0x3Fu;
    w->win1.enabled = (e->dispcnt & 0x4000u) != 0;
    w->win1.x1 = (u32)h1 >> 8;
    w->win1.x2 = h1 & 0xFFu;
    w->win1.y1 = (u32)v1 >> 8;
    w->win1.y2 = v1 & 0xFFu;
    w->win1.layers = ((u32)win >> 8) & 0x3Fu;
    w->obj_enabled = (e->dispcnt & 0x8000u) != 0;
    w->obj_layers = ((u32)wout >> 8) & 0x3Fu;
    w->outside_layers = wout & 0x3Fu;
    w->obj_mask = e->objwin;
    w->mask_width = SCREEN_W;
    w->mask_height = SCREEN_H;
}

static void build_blend(const Engine *e, G2dBlend *b)
{
    u16 cnt = reg16(e, REG_BLDCNT);
    u16 alpha = reg16(e, REG_BLDALPHA);

    b->effect = ((u32)cnt >> 6) & 3u;
    b->first = cnt & 0x3Fu;
    b->second = ((u32)cnt >> 8) & 0x3Fu;
    b->eva = alpha & 0x1Fu;
    b->evb = ((u32)alpha >> 8) & 0x1Fu;
    b->evy = reg16(e, REG_BLDY) & 0x1Fu;
}

/* Display mode 2: one of banks A..D shown directly as 256x192 BGR555. */
static void lcdc_display(const Engine *e, u8 *rgba)
{
    const u8 *bank = e->hw->lcdc_bank[(e->dispcnt >> 18) & 3u];
    u32 i;

    for (i = 0; i < SCREEN_PX; i++) {
        put_rgb(rgba + i * 4u, bgr555(bank != NULL ? bw_rd16(bank + i * 2u) : 0));
    }
}

int g2d_hw_render_engine(const G2dHwState *hw, int engine, const u8 *rgba3d,
                         u8 *rgba, u32 *unsupported)
{
    Engine *e;
    G2dFrameDesc desc;
    G2dWindows windows;
    G2dBlend blend;
    u32 dmode;
    int rc = 0;

    e = (Engine *)calloc(1, sizeof(Engine));
    if (e == NULL) {
        return -1;
    }
    e->hw = hw;
    e->main = (engine == G2D_HW_MAIN);
    e->io = hw->io + (e->main ? 0u : REG_SUB_BASE);
    e->dispcnt = reg32(e, REG_DISPCNT);
    dmode = (e->dispcnt >> 16) & 3u;

    if (dmode == 0u) {
        fill(rgba, 255);          /* display off: the screen is white */
        goto done;
    }
    if (dmode == 3u || (dmode == 2u && !e->main)) {
        e->unsup |= G2D_HW_UNSUP_DISPLAY_MODE;
        fill(rgba, 255);
        goto done;
    }
    if (dmode == 2u) {
        lcdc_display(e, rgba);
        master_bright(e, rgba);
        goto done;
    }
    if (e->dispcnt & 0x80u) {
        fill(rgba, 255);          /* forced blank */
        goto done;
    }

    if (alloc_engine(e) != 0 || build_bgs(e, e->main ? rgba3d : NULL) != 0) {
        rc = -1;
        goto done;
    }
    if (e->dispcnt & 0x1000u) {
        if (build_objs(e) != 0 || emit_obj_poses(e) != 0) {
            rc = -1;
            goto done;
        }
    }
    build_windows(e, &windows);
    build_blend(e, &blend);

    memset(&desc, 0, sizeof(desc));
    desc.bgs = e->bgs;
    desc.n_bgs = 4;
    desc.abgs = e->abgs;
    desc.n_abgs = e->n_abgs;
    desc.g3d = e->layer3d.enabled ? &e->layer3d : NULL;
    desc.objs = e->objs;
    desc.n_objs = e->n_objs;
    desc.backdrop_pal = &e->bg_pal;
    desc.windows = &windows;
    desc.blend = &blend;
    desc.rasters = e->rasters;
    desc.n_rasters = e->n_rasters;
    g2d_frame_compose_desc(&desc, SCREEN_W, SCREEN_H, rgba);
    master_bright(e, rgba);

done:
    if (unsupported != NULL) {
        *unsupported = e->unsup;
    }
    engine_free(e);
    free(e);
    return rc;
}

int g2d_hw_render(const G2dHwState *hw, const u8 *rgba3d,
                  u8 *top, u8 *bottom, u32 unsupported[2])
{
    int main_on_top = (bw_rd16(hw->io + REG_POWCNT1) & 0x8000u) != 0;
    u32 flags[2] = { 0, 0 };
    int rc = 0;

    if (g2d_hw_render_engine(hw, G2D_HW_MAIN, rgba3d,
                             main_on_top ? top : bottom, &flags[0]) != 0) {
        rc = -1;
    }
    if (g2d_hw_render_engine(hw, G2D_HW_SUB, NULL,
                             main_on_top ? bottom : top, &flags[1]) != 0) {
        rc = -1;
    }
    if (unsupported != NULL) {
        unsupported[0] = flags[0];
        unsupported[1] = flags[1];
    }
    return rc;
}

/* ------------------------------------------------------------------ */
/* The live address space                                              */
/* ------------------------------------------------------------------ */

/*
 * The extended palettes are not in the 0x06xxxxxx window at all; sdk/dsmem.c
 * maps them from VRAMCNT into spaces of their own (bw_vram_slot), the same
 * mapping its CPU window uses, so the renderer and a matched store agree on
 * which bank holds what.  Display mode 2 reads banks A..D through their
 * LCDC addresses (0x06800000 + 128 KiB each), which are only mapped while
 * the bank's MST is 0.
 */
void g2d_hw_state_from_dsmem(G2dHwState *hw)
{
    const u8 *io = (const u8 *)(const void *)bw_io(BW_DS_IO_BASE);
    u32 i;
    int e;

    memset(hw, 0, sizeof(*hw));
    hw->io = io;
    hw->pal = (const u8 *)bw_ds_try_ptr(BW_DS_PAL_BASE);
    hw->oam = (const u8 *)bw_ds_try_ptr(BW_DS_OAM_BASE);
    for (i = 0; i < G2D_HW_BG_PAGES; i++) {
        hw->bg_page[0][i] = (const u8 *)bw_ds_try_ptr(0x06000000u + i * G2D_HW_PAGE_SIZE);
    }
    for (i = 0; i < 8u; i++) {
        hw->bg_page[1][i] = (const u8 *)bw_ds_try_ptr(0x06200000u + i * G2D_HW_PAGE_SIZE);
    }
    for (i = 0; i < G2D_HW_OBJ_PAGES; i++) {
        hw->obj_page[0][i] = (const u8 *)bw_ds_try_ptr(0x06400000u + i * G2D_HW_PAGE_SIZE);
    }
    for (i = 0; i < 8u; i++) {
        hw->obj_page[1][i] = (const u8 *)bw_ds_try_ptr(0x06600000u + i * G2D_HW_PAGE_SIZE);
    }

    for (e = G2D_HW_MAIN; e <= G2D_HW_SUB; e++) {
        int bg_kind = e == G2D_HW_MAIN ? BW_VSLOT_BGEXT_MAIN : BW_VSLOT_BGEXT_SUB;
        int obj_kind = e == G2D_HW_MAIN ? BW_VSLOT_OBJEXT_MAIN : BW_VSLOT_OBJEXT_SUB;

        for (i = 0; i < 4u; i++) {
            hw->bg_extpal[e][i] = bw_vram_slot(bg_kind, i * G2D_HW_EXTPAL_SLOT_SIZE, NULL);
        }
        hw->obj_extpal[e] = bw_vram_slot(obj_kind, 0, NULL);
    }
    for (i = 0; i < 4u; i++) {
        hw->lcdc_bank[i] = (const u8 *)bw_ds_try_ptr(0x06800000u + i * 0x20000u);
    }
}

int g2d_hw_render_dsmem(const u8 *rgba3d, u8 *top, u8 *bottom,
                        u32 unsupported[2])
{
    G2dHwState hw;

    g2d_hw_state_from_dsmem(&hw);
    if (hw.io == NULL || hw.pal == NULL || hw.oam == NULL) {
        return -1;
    }
    return g2d_hw_render(&hw, rgba3d, top, bottom, unsupported);
}
