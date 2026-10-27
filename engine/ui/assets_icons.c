/*
 * engine/ui/assets_icons.c -- the game's icons and sprites for the kit.
 *
 * Every image is decoded from the player's own data the first time it is
 * asked for and kept; nothing is shipped.  The images are RGBA at the DS
 * pixel size, alpha 0 where the palette index is 0 and 255 elsewhere.
 *
 * Item icons
 * ----------
 *
 * Archive a/0/2/5 (the game's item graphics archive, id 0x19) holds, for
 * each item, one character member (NCGR, 4 bits per pixel) and one palette
 * member (NCLR, 16 colours).  Which members belong to an item is a table
 * in the ARM9 image (the game's item_gfx_member, 0x0209EB6C): two u16 per
 * item, the character member then the palette member (the game reads them
 * as FILE_UTIL_Get(item, 1) and FILE_UTIL_Get(item, 2)).  The character
 * data starts 0x30 bytes into the member and is 16 tiles of 8x8 in one
 * row-major 4x4 block (OBJ order); the artwork is 24x24 and sits in the
 * top left of that block (checked over all 626 items: every drawn pixel is
 * inside 24x24), so the kit crops to 24x24.  The palette's 16 colours
 * start 0x28 bytes into the NCLR.
 *
 * Pokemon icons
 * -------------------------------------------------------------
 *
 * Archive a/0/0/7: member 0 is the shared palette (3 banks of 16 colours,
 * from +0x28), members 1 to 6 are the cell and animation data of the
 * icons (not needed here: the kit draws the frames itself), and from member
 * 7 the characters, two members per icon.  The member of an icon is the
 * game's POKE_TOOL_CalcGraphicIndex: species * 2 + 7, or for an alternate
 * form (first + form - 1) * 2 + 1311, with first the u16 at +0x1E of the
 * species' personal record (a/0/1/6) and the form count the byte at +0x20
 * (a form past the count is form 0; bit 7 of the byte at +0x21 says the
 * forms are not drawn separately).  The second member of a pair is the
 * female graphic and is empty for most species; the kit asks for the male
 * (or only) one.  The character member is 0x400 bytes of data from +0x30:
 * two animation frames of 512 bytes, each 4x4 tiles in row-major order, a
 * 32x32 picture.
 *
 * The palette bank of an icon is a nibble of a table in the ARM9 image
 * (0x0209E474, one byte per icon index: low nibble male, high nibble
 * female); the index is the species, or for a form first + form + 651.
 * (Both tables are at the Pokemon Black addresses.)
 *
 * (tools/assets/icons.py in the decomp's tooling reads a different archive,
 * a/1/6/7, whose 16x16 pictures are not these icons.)
 *
 * The walking figures are in assets_sprites.c.  Trainer fronts and Pokemon
 * fronts are not decoded yet (the notes are with the two stubs below).
 */

#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "kit.h"
#include "port.h"

#define ITEM_MAX        627
#define ITEM_TABLE_ADDR 0x0209EB6Cu
#define ICON_PAL_ADDR   0x0209E474u
#define ICON_PAL_BYTES  0x300u
#define ICON_MEMBERS    1431

typedef struct UiOwned {
    UiImage im;             /* im.rgba points at the block we own          */
} UiOwned;

static UiOwned *g_item[ITEM_MAX];
static UiOwned *g_mon[ICON_MEMBERS * 2];
static u8      *g_mon_pal;          /* member 0 of a/0/0/7                  */
static u32      g_mon_pal_size;
static int      g_mon_pal_tried;

static u32 rd16(const u8 *p) { return (u32)p[0] | ((u32)p[1] << 8); }

static UiOwned *make_image(u8 *rgba, int w, int h)
{
    UiOwned *o;

    if (rgba == NULL) {
        return NULL;
    }
    o = (UiOwned *)malloc(sizeof *o);
    if (o == NULL) {
        free(rgba);
        return NULL;
    }
    o->im.w = w;
    o->im.h = h;
    o->im.rgba = rgba;
    return o;
}

static void free_images(UiOwned **list, int n)
{
    int i;

    for (i = 0; i < n; i++) {
        if (list[i] != NULL) {
            free((void *)list[i]->im.rgba);
            free(list[i]);
            list[i] = NULL;
        }
    }
}

void ui_icons_reset(void)
{
    free_images(g_item, ITEM_MAX);
    free_images(g_mon, ICON_MEMBERS * 2);
    free(g_mon_pal);
    g_mon_pal = NULL;
    g_mon_pal_size = 0;
    g_mon_pal_tried = 0;
}

/* 16 colours from an NCLR member (the TTLP data starts 0x28 in). */
static int read_pal16(const u8 *nclr, u32 size, u32 bank, u16 *out)
{
    u32 off = 0x28 + bank * 32, i;

    if (off + 32 > size) {
        return -1;
    }
    for (i = 0; i < 16; i++) {
        out[i] = (u16)rd16(nclr + off + i * 2);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Item icons                                                          */
/* ------------------------------------------------------------------ */

const UiImage *ui_item_icon(u16 item)
{
    const u8 *tab;
    u8 *chr, *pal;
    u32 chr_size = 0, pal_size = 0, lo, hi;
    u16 p16[16];
    UiOwned *o;

    if (item == 0 || item >= ITEM_MAX) {
        return NULL;
    }
    if (g_item[item] != NULL) {
        return &g_item[item]->im;
    }
    tab = (const u8 *)bw_arm9_rodata(ITEM_TABLE_ADDR, ITEM_MAX * 4);
    if (tab == NULL) {
        return NULL;
    }
    lo = rd16(tab + item * 4u);
    hi = rd16(tab + item * 4u + 2);
    chr = ui_arc_member(UI_ARC_ITEM, lo, &chr_size);
    pal = ui_arc_member(UI_ARC_ITEM, hi, &pal_size);
    o = NULL;
    if (chr != NULL && pal != NULL && chr_size >= 0x30 + 512 &&
        read_pal16(pal, pal_size, 0, p16) == 0) {
        o = make_image(ui_decode_4bpp(chr + 0x30, 4, 4, p16, 24, 24), 24, 24);
    }
    free(chr);
    free(pal);
    g_item[item] = o;
    return o != NULL ? &o->im : NULL;
}

/* ------------------------------------------------------------------ */
/* Pokemon icons                                                       */
/* ------------------------------------------------------------------ */

/* The form block of a species: first index, count, shared flag. */
static int personal_forms(u16 species, u32 *first, u32 *count, int *shared)
{
    u32 size = 0;
    u8 *rec = ui_arc_member(UI_ARC_PERSONAL, species, &size);

    if (rec == NULL || size < 0x24) {
        free(rec);
        return -1;
    }
    *first = rd16(rec + 0x1E);
    *count = rec[0x20];
    *shared = (rec[0x21] >> 7) & 1;
    free(rec);
    return 0;
}

const UiImage *ui_mon_icon(u16 species, u8 form, int frame)
{
    u32 member, pal_index, first, count;
    int shared;
    const u8 *ptab;
    u8 *chr;
    u32 chr_size = 0, bank;
    u16 p16[16];
    UiOwned *o;
    int slot;

    if (species == 0 || species > 649 || frame < 0 || frame > 1) {
        return NULL;
    }
    member = (u32)species * 2 + 7;
    pal_index = species;
    if (form != 0 && personal_forms(species, &first, &count, &shared) == 0) {
        if (form >= count) {
            form = 0;
        }
        if (!shared && form != 0) {
            member = (first + form - 1u) * 2 + 1311;
            pal_index = first + form + 651u;
        }
    }
    if (member >= ICON_MEMBERS) {
        return NULL;
    }
    slot = (int)member * 2 + frame;
    if (g_mon[slot] != NULL) {
        return &g_mon[slot]->im;
    }
    if (!g_mon_pal_tried) {
        g_mon_pal_tried = 1;
        g_mon_pal = ui_arc_member(UI_ARC_MON, 0, &g_mon_pal_size);
    }
    if (g_mon_pal == NULL) {
        return NULL;
    }
    ptab = (const u8 *)bw_arm9_rodata(ICON_PAL_ADDR, ICON_PAL_BYTES);
    bank = (ptab != NULL && pal_index < ICON_PAL_BYTES) ? (u32)(ptab[pal_index] & 15) : 0;
    if (read_pal16(g_mon_pal, g_mon_pal_size, bank < 3 ? bank : 0, p16) != 0) {
        return NULL;
    }
    chr = ui_arc_member(UI_ARC_MON, member, &chr_size);
    o = NULL;
    if (chr != NULL && chr_size >= 0x30 + 1024) {
        o = make_image(ui_decode_4bpp(chr + 0x30 + (u32)frame * 512, 4, 4, p16, 32, 32), 32, 32);
    }
    free(chr);
    g_mon[slot] = o;
    return o != NULL ? &o->im : NULL;
}

/* ------------------------------------------------------------------ */
/* Sprites: not decoded                                                */
/* ------------------------------------------------------------------ */

/*
 * Pokemon fronts: archive a/0/0/4 holds 20 members per species (species *
 * 20 + n): n 18 and 19 are the normal and shiny palettes, n 0..8 and 9..17
 * two bundles each of character sheets (LZ11), a cell bank, a cell
 * animation and a multi-cell resource.  The picture is not one image: the
 * multi-cell resource places body-part cells (the poseable rig described in
 * the decomp's tools/assets/rig.py), so a front needs the NMCR node table,
 * the NANR sequences and the part transforms of the first pose, plus the
 * LZ11 decoder, all in C.  Trainer fronts are the same shape in another
 * archive (rig.py tried a/0/7/2).  Not done: the kit returns NULL.
 */

const UiImage *ui_trainer_front(u16 trainer_class)
{
    (void)trainer_class;
    return NULL;
}

const UiImage *ui_mon_front(u16 species, u8 form, int shiny)
{
    (void)species;
    (void)form;
    (void)shiny;
    return NULL;
}
