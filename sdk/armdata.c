/*
 * sdk/armdata.c -- the ARM9 image's initialised data, in the DS address
 * space, with its pointers relocated and the matched code's names bound.
 *
 * Why this exists: the matched code names the game's data by extern names
 * pinned to DS addresses, casts their addresses as literals, and follows
 * pointers stored inside the data.  On the DS all three are the ARM9 image
 * in RAM.  This module puts the player's own image where the DS has it (the
 * port's flat main RAM, sdk/dsmem.c), so all three reach the same bytes in
 * the DS's own layout, and then does the two things a host needs on top: it
 * rewrites every pointer word to a host pointer, and it points each name's
 * variable (tools/armdata.py's forced-include header) at its address.
 * docs/armdata.md is the design and says why the data is not a set of host
 * globals instead.
 *
 * The tables come from tools/armdata.py and contain addresses and names
 * only; the bytes come from bw_arm9_rodata at run time.
 *
 * Order at reset (bw_game_globals_register calls both hooks):
 *   1. bw_data_reset_load, first, while the registry is empty: copy each
 *      initialised range out of arm9_dec.bin into flat main RAM, zero each
 *      .bss range.  Ranges outside main RAM (the ITCM and VRAM autoloads,
 *      which hold code) are skipped: no matched code reads them as data.
 *   2. sdk/game_globals.c registers its objects.
 *   3. bw_data_reset_bind, last: bind every name (a name whose address a
 *      registered or port object owns resolves to that object) and rewrite
 *      every pointer word in the copy.  A pointer word inside a port
 *      object's DS range was left out by the generator: its owner fills it.
 *
 * An overlay's image (img->overlay >= 0, docs/overlays.md) goes through the
 * same three steps when sdk/overlay_image.c loads it, with its bytes out of
 * the overlay's own file; its host objects are filled but not registered.
 *
 * Every pointer word is checked against the image before it is rewritten:
 * the metadata says which address the word holds, and a different value
 * means the image is not the build aletheia describes.  That is a panic at
 * reset, not a silently wrong pointer.
 */

#include <stdio.h>
#include <string.h>

#include "port.h"
#include "dsmem.h"
#include "armdata.h"
#include "overlay_image.h"

#define PIECE 0x10000u            /* sdk/dsmem.c commits main RAM in 64 KiB */
#define MAX_RANGES 16

static const BwDataImage *g_installed;
static unsigned           g_flags;
static char               g_error[256];

/* The flat host address of each loaded range's first byte, or NULL. */
static u8                *g_flat[MAX_RANGES];
static const BwDataImage *g_flat_img;

static int fail(int code, u32 addr, const char *what)
{
    snprintf(g_error, sizeof(g_error), "%s (0x%08lX)", what, (unsigned long)addr);
    return code;
}

const char *bw_data_error(void)
{
    return g_error;
}

int bw_data_relocation_supported(void)
{
    return sizeof(void *) == 4u && sizeof(BwDataFn) == 4u;
}

void bw_data_install(const BwDataImage *img, unsigned flags)
{
    g_installed = img;
    g_flags = flags;
    g_flat_img = NULL;
}

const BwDataImage *bw_data_installed(void)
{
    return g_installed;
}

static int in_main(u32 addr, u32 size)
{
    return addr >= BW_DS_MAIN_BASE && size <= BW_DS_MAIN_SIZE
        && addr - BW_DS_MAIN_BASE <= BW_DS_MAIN_SIZE - size;
}

/*
 * The flat host block for [addr, addr + size), committed, or NULL.  Walks
 * the range a piece at a time, because translating an address is what
 * commits its piece; and checks that every piece continues the first one,
 * which is what an empty registry guarantees and a registered object at a
 * piece start would break.
 */
static u8 *flat_block(u32 addr, u32 size)
{
    u8 *base = (u8 *)bw_ds_try_ptr(addr);
    u32 off;

    if (base == NULL) {
        return NULL;
    }
    off = (PIECE - (addr & (PIECE - 1u))) & (PIECE - 1u);
    for (; off < size; off += PIECE) {
        if ((u8 *)bw_ds_try_ptr(addr + off) != base + off) {
            return NULL;
        }
    }
    if (size != 0 && (u8 *)bw_ds_try_ptr(addr + size - 1u) != base + size - 1u) {
        return NULL;
    }
    return base;
}

int bw_data_load(const BwDataImage *img)
{
    u32 i;

    g_flat_img = NULL;
    if (img->range_count > MAX_RANGES) {
        return fail(BW_DATA_E_LAYOUT, 0, "more ranges than sdk/armdata.c keeps");
    }
    for (i = 0; i < img->range_count; i++) {
        const BwDataRange *r = &img->ranges[i];
        u8 *dst;

        g_flat[i] = NULL;
        if (!in_main(r->addr, r->size)) {
            continue;               /* ITCM, VRAM: code, see the header */
        }
        dst = flat_block(r->addr, r->size);
        if (dst == NULL) {
            return fail(BW_DATA_E_LAYOUT, r->addr,
                        "range is not one flat block of main RAM; "
                        "bw_data_load must run before anything is registered");
        }
        if (r->copy && img->overlay >= 0) {
            /* An overlay: image_addr is an offset into its own file. */
            const void *src = bw_ovl_file_bytes((u32)img->overlay, r->image_addr, r->size);
            if (src == NULL) {
                return fail(BW_DATA_E_NOIMAGE, (u32)img->overlay,
                            "the overlay's file (extract/overlays_dec/ or "
                            "extract/overlays/) is missing or shorter than "
                            "layout.json says");
            }
            memcpy(dst, src, r->size);
        } else if (r->copy) {
            const void *src = bw_arm9_rodata(r->image_addr, r->size);
            if (src == NULL) {
                return fail(BW_DATA_E_NOIMAGE, r->image_addr,
                            "extract/arm9_dec.bin is missing or shorter than "
                            "layout.json says");
            }
            memcpy(dst, src, r->size);
        } else {
            memset(dst, 0, r->size);
        }
        g_flat[i] = dst;
    }
    /* The host objects: their bytes from the copy, then registered. */
    for (i = 0; i < img->object_count; i++) {
        const BwDataObject *o = &img->objects[i];
        u32 k;

        memset(o->host, 0, BW_DATA_CELLS(o->size) * sizeof(BwDataCell));
        for (k = 0; k < img->range_count; k++) {
            const BwDataRange *r = &img->ranges[k];
            if (g_flat[k] != NULL && o->addr - r->addr < r->size) {
                u32 off = o->addr - r->addr;
                u32 n = r->size - off < o->size ? r->size - off : o->size;
                memcpy(o->host, g_flat[k] + off, n);
            }
        }
        /* An overlay's objects are not registered: the window passes to
         * the next overlay and a registration cannot be withdrawn
         * (docs/overlays.md, "Data"). */
        if (img->overlay < 0) {
            bw_ds_register(o->addr, o->host, o->size, o->name);
        }
    }
    g_flat_img = img;
    return BW_DATA_OK;
}

/* The host address of the word at `where`: inside a host object, or in the
 * flat copy of a loaded range. */
static u8 *flat_word(const BwDataImage *img, u32 where)
{
    u32 i;

    for (i = 0; i < img->object_count; i++) {
        const BwDataObject *o = &img->objects[i];
        if (where - o->addr < o->size && o->size - (where - o->addr) >= 4u) {
            return (u8 *)o->host + (where - o->addr);
        }
    }

    for (i = 0; i < img->range_count; i++) {
        const BwDataRange *r = &img->ranges[i];
        if (g_flat[i] != NULL && where - r->addr < r->size
            && r->size - (where - r->addr) >= 4u) {
            return g_flat[i] + (where - r->addr);
        }
    }
    return NULL;
}

int bw_data_bind(const BwDataImage *img)
{
    u32 i;

    for (i = 0; i < img->name_count; i++) {
        const BwDataName *n = &img->names[i];
        void *p;

        if (n->bind == BW_DATA_BIND_OBJECT || n->ref == NULL) {
            continue;               /* its own storage, registered at load */
        }
        switch (n->bind) {
        case BW_DATA_BIND_CONST:
            p = (void *)(uintptr_t)n->addr;
            break;
        case BW_DATA_BIND_HOST:
            p = (u8 *)(uintptr_t)n->anchor + (n->addr - n->anchor_addr);
            break;
        default:
            p = bw_ds_try_ptr(n->addr);
            /* Commit the object's whole known extent. */
            if (p != NULL && n->size > 1u) {
                u32 off;
                for (off = PIECE; off < n->size; off += PIECE) {
                    (void)bw_ds_try_ptr(n->addr + off);
                }
                (void)bw_ds_try_ptr(n->addr + n->size - 1u);
            }
            break;
        }
        if (p == NULL) {
            snprintf(g_error, sizeof(g_error),
                     "%s: no DS region or object backs 0x%08lX",
                     n->name, (unsigned long)n->addr);
            return BW_DATA_E_UNBACKED;
        }
        *n->ref = p;
    }
    return BW_DATA_OK;
}

const BwDataName *bw_data_find(const BwDataImage *img, const char *name)
{
    u32 i;

    for (i = 0; i < img->name_count; i++) {
        if (strcmp(img->names[i].name, name) == 0) {
            return &img->names[i];
        }
    }
    return NULL;
}

int bw_data_relocate(const BwDataImage *img)
{
    u32 i;

    if (g_flat_img != img) {
        return fail(BW_DATA_E_LAYOUT, 0, "bw_data_relocate before bw_data_load");
    }
    /* First pass: every word must hold what the metadata says. */
    for (i = 0; i < img->reloc_count; i++) {
        const BwDataReloc *r = &img->relocs[i];
        u8 *w = flat_word(img, r->where);

        if (w == NULL) {
            return fail(BW_DATA_E_LAYOUT, r->where,
                        "pointer word outside every loaded range");
        }
        if (bw_rd32(w) != r->target) {
            snprintf(g_error, sizeof(g_error),
                     "word 0x%08lX holds 0x%08lX, the metadata says 0x%08lX: "
                     "arm9_dec.bin is not the image aletheia describes",
                     (unsigned long)r->where, (unsigned long)bw_rd32(w),
                     (unsigned long)r->target);
            return BW_DATA_E_MISMATCH;
        }
    }
    if (!bw_data_relocation_supported()) {
        snprintf(g_error, sizeof(g_error),
                 "%lu pointer words left as DS addresses: a host pointer does "
                 "not fit the DS's 32-bit word on this target (LP64); the "
                 "boot needs an ILP32 build",
                 (unsigned long)img->reloc_count);
        return img->reloc_count != 0 ? BW_DATA_E_LP64 : BW_DATA_OK;
    }
    /* Second pass: resolve and write. */
    for (i = 0; i < img->reloc_count; i++) {
        const BwDataReloc *r = &img->relocs[i];
        u8 *w = flat_word(img, r->where);
        uintptr_t v;
        u32 word;

        switch (r->kind) {
        case BW_DATA_RELOC_CODE:
            if (r->index >= img->func_count) {
                return fail(BW_DATA_E_LAYOUT, r->where, "function index out of range");
            }
            v = (uintptr_t)img->funcs[r->index].fn;
            break;
        case BW_DATA_RELOC_HOST: {
            const BwDataAnchor *a;
            if (r->index >= img->anchor_count) {
                return fail(BW_DATA_E_LAYOUT, r->where, "anchor index out of range");
            }
            a = &img->anchors[r->index];
            v = (uintptr_t)a->host + (r->target - a->addr);
            break;
        }
        default: {
            void *p = bw_ds_try_ptr(r->target);
            if (p == NULL) {
                return fail(BW_DATA_E_UNBACKED, r->target,
                            "pointer target outside every backed DS region");
            }
            v = (uintptr_t)p;
            break;
        }
        }
        word = (u32)v;
        memcpy(w, &word, 4);
    }
    return BW_DATA_OK;
}

void bw_data_trap(u32 addr)
{
    bw_panic("armdata", addr,
             "called through a pointer in the ARM9 data to code the build does "
             "not define (tools/armdata.py lists it as a trap)");
}

/* ------------------------------------------------------------------ */
/* The reset hooks                                                     */
/* ------------------------------------------------------------------ */

void bw_data_reset_load(void)
{
    int rc;

    if (g_installed == NULL) {
        return;
    }
    rc = bw_data_load(g_installed);
    if (rc != BW_DATA_OK) {
        bw_panic("bw_data_reset_load", 0, bw_data_error());
    }
}

void bw_data_reset_bind(void)
{
    int rc;

    if (g_installed == NULL) {
        return;
    }
    rc = bw_data_bind(g_installed);
    if (rc != BW_DATA_OK) {
        bw_panic("bw_data_reset_bind", 0, bw_data_error());
    }
    rc = bw_data_relocate(g_installed);
    if (rc == BW_DATA_E_LP64 && (g_flags & BW_DATA_ALLOW_UNRELOCATED)) {
        return;
    }
    if (rc != BW_DATA_OK) {
        bw_panic("bw_data_reset_bind", 0, bw_data_error());
    }
}
