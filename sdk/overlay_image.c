/*
 * sdk/overlay_image.c -- loading an overlay that is compiled into the
 * executable: its file copied into its RAM window, its names bound, its
 * pointers relocated; and the run-time half of tools/ovlink.py's
 * trampolines and stubs.  docs/overlays.md is the design.
 *
 * Why this exists.  On the DS, FS_LoadOverlay copies the overlay's file to
 * its RAM window, zeroes the .bss after it and runs its static
 * initialisers; the code then runs from the window.  In the port the code
 * is host code (renamed per overlay by tools/ovlink.py so overlays sharing
 * a window link side by side), but the data stays where the DS has it: the
 * window in the port's flat main RAM (sdk/dsmem.c), the same model as the
 * ARM9 image's data (docs/armdata.md).  So a load is, per BwOvlImage:
 *
 *   1. bw_data_load: copy the overlay's file to [ram_address, +ram_size),
 *      zero the .bss, fill its host objects;
 *   2. bw_data_bind: point every data name the overlay's sources use at its
 *      address (the overlay's own pointer variables, bw_dsref_ovNNN_*);
 *   3. bw_data_relocate: rewrite the pointer words the metadata lists, to
 *      the overlay's own functions, the ARM9's, a trampoline, or data.
 *
 * Unloading needs nothing: the window keeps its bytes until the next
 * overlay is copied over them, as on the DS, and sdk/overlay.c stops
 * calling the overlay resident.
 *
 * This module plugs into sdk/overlay.c through bw_overlay_set_provider; the
 * loader keeps the residency bookkeeping.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port.h"
#include "dsmem.h"
#include "armdata.h"
#include "overlay.h"
#include "overlay_image.h"
#include "mod_slot.h"

#define OVL_MAX 1024u

static const BwOvlImageSet *g_set;
static unsigned             g_flags;

/* Overlay files, read once (or set by a test) and kept. */
typedef struct OvlFile {
    u8  *bytes;
    u32  size;
    u8   tried;
} OvlFile;

static OvlFile g_files[OVL_MAX];

/* ------------------------------------------------------------------ */
/* The overlay files                                                    */
/* ------------------------------------------------------------------ */

/* <extract>/<rel>: the data root's parent, as sdk/narc.c finds arm9_dec.bin. */
static int extract_path(char *out, size_t cap, const char *rel)
{
    const char *root = bw_data_root();
    size_t n = strlen(root);

    if (n + strlen(rel) + 2 > cap) {
        return -1;
    }
    memcpy(out, root, n + 1);
    while (n > 0 && (out[n - 1] == '/' || out[n - 1] == '\\')) {
        n--;
    }
    while (n > 0 && out[n - 1] != '/' && out[n - 1] != '\\') {
        n--;
    }
    strcpy(out + n, rel);
    return 0;
}

static u8 *read_whole(const char *path, u32 *size_out)
{
    FILE *fp = fopen(path, "rb");
    long end;
    u8 *buf;

    if (fp == NULL) {
        return NULL;
    }
    if (fseek(fp, 0, SEEK_END) != 0 || (end = ftell(fp)) <= 0) {
        fclose(fp);
        return NULL;
    }
    rewind(fp);
    buf = (u8 *)malloc((size_t)end);
    if (buf == NULL || fread(buf, 1, (size_t)end, fp) != (size_t)end) {
        free(buf);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    *size_out = (u32)end;
    return buf;
}

static void load_file(u32 id)
{
    static const char *const dirs[] = { "overlays_dec/", "overlays/" };
    OvlFile *f = &g_files[id];
    char rel[64];
    char path[1100];
    u32 k;

    f->tried = 1;
    for (k = 0; k < 2 && f->bytes == NULL; k++) {
        snprintf(rel, sizeof(rel), "%sy9_%04lu.bin", dirs[k], (unsigned long)id);
        if (extract_path(path, sizeof(path), rel) == 0) {
            f->bytes = read_whole(path, &f->size);
        }
    }
}

const void *bw_ovl_file_bytes(u32 id, u32 offset, u32 size)
{
    OvlFile *f;

    if (id >= OVL_MAX) {
        return NULL;
    }
    f = &g_files[id];
    if (!f->tried) {
        load_file(id);
    }
    if (f->bytes == NULL || offset > f->size || size > f->size - offset) {
        return NULL;
    }
    return f->bytes + offset;
}

void bw_ovl_set_file(u32 id, const void *bytes, u32 size)
{
    OvlFile *f;

    if (id >= OVL_MAX) {
        return;
    }
    f = &g_files[id];
    free(f->bytes);
    f->bytes = NULL;
    f->size = 0;
    f->tried = 0;
    if (bytes == NULL || size == 0) {
        return;
    }
    f->bytes = (u8 *)malloc(size);
    if (f->bytes != NULL) {
        memcpy(f->bytes, bytes, size);
        f->size = size;
    }
    f->tried = 1;
}

/* ------------------------------------------------------------------ */
/* The provider sdk/overlay.c calls                                     */
/* ------------------------------------------------------------------ */

const BwOvlImage *bw_ovl_image(u32 id)
{
    u32 i;

    if (g_set == NULL) {
        return NULL;
    }
    for (i = 0; i < g_set->count; i++) {
        if (g_set->images[i] != NULL && g_set->images[i]->id == id) {
            return g_set->images[i];
        }
    }
    return NULL;
}

static int provider_compiled(u32 id)
{
    return bw_ovl_image(id) != NULL;
}

static void load_panic(const BwOverlayDesc *d, const char *step)
{
    char why[400];

    snprintf(why, sizeof(why), "overlay %lu: %s: %s", (unsigned long)d->id, step,
             bw_data_error());
    bw_panic("FS_LoadOverlay", d->ram_address, why);
}

/*
 * Before overlay `id` loads: take back the host objects of every compiled
 * overlay that is no longer resident (sdk/overlay.c has already dropped the
 * ones this load overwrites, and any unloaded since) and of `id` itself (a
 * reload), each one's contents copied back into the window first.  An
 * unloaded overlay's objects stay registered until the next load, over
 * bytes nothing else has claimed yet, as the window keeps them on the DS.
 */
static void withdraw_passed(u32 id)
{
    u32 i;

    for (i = 0; i < g_set->count; i++) {
        const BwOvlImage *o = g_set->images[i];

        if (o != NULL && (o->id == id || !bw_overlay_is_loaded(o->id))) {
            bw_data_withdraw(o->data);
        }
    }
}

static void provider_load(const BwOverlayDesc *d)
{
    const BwOvlImage *img = bw_ovl_image(d->id);
    int rc;

    if (img == NULL) {
        return;
    }
    withdraw_passed(d->id);
    rc = bw_data_load(img->data);
    if (rc != BW_DATA_OK) {
        load_panic(d, "copying the image");
    }
    /* The overlay's declared slots are installed with its window: publish
     * each original so a wrap override can call it (mod_slot.h). */
    bw_ovl_mod_publish(d->id);
    rc = bw_data_bind(img->data);
    if (rc != BW_DATA_OK) {
        load_panic(d, "binding its names");
    }
    rc = bw_data_relocate(img->data);
    if (rc == BW_DATA_E_LP64 && (g_flags & BW_DATA_ALLOW_UNRELOCATED)) {
        return;
    }
    if (rc != BW_DATA_OK) {
        load_panic(d, "relocating its pointers");
    }
}

static void provider_start(const BwOverlayDesc *d)
{
    const BwOvlImage *img = bw_ovl_image(d->id);
    u32 i;

    if (img == NULL) {
        return;
    }
    for (i = 0; i < img->sinit_count; i++) {
        img->sinits[i]();
    }
}

static const BwOverlayProvider k_provider = {
    provider_compiled, provider_load, provider_start
};

void bw_ovl_install(const BwOvlImageSet *set, unsigned flags)
{
    g_set = set;
    g_flags = flags;
    bw_overlay_set_provider(set != NULL ? &k_provider : NULL);
}

/* ------------------------------------------------------------------ */
/* Trampolines and stubs                                                */
/* ------------------------------------------------------------------ */

int bw_ovl_resident_at(u32 addr)
{
    const BwOverlayTable *t = bw_overlay_table();
    u32 i;

    /* The compiled overlays first: a handful, and the usual answer. */
    if (g_set != NULL) {
        for (i = 0; i < g_set->count; i++) {
            const BwOverlayDesc *o;

            if (g_set->images[i] == NULL) {
                continue;
            }
            o = bw_overlay_desc(g_set->images[i]->id);
            if (o != NULL && addr - o->ram_address < o->ram_size + o->bss_size
                && bw_overlay_is_loaded(o->id)) {
                return (int)o->id;
            }
        }
    }
    /* Then every overlay, to name a resident one this build lacks. */
    for (i = 0; i < t->overlay_count; i++) {
        const BwOverlayDesc *o = &t->overlays[i];

        if (addr - o->ram_address < o->ram_size + o->bss_size
            && bw_overlay_is_loaded(o->id)) {
            return (int)o->id;
        }
    }
    return -1;
}

BwDataFn bw_ovl_code_entry(u32 value)
{
    BwDataFn found = NULL;
    /* The DS entry is the function's even address; the low bit is the
     * instruction-set tag.  A data word carries the tag the game gave it and
     * the entry table carries the tag the listing gave the function, and the
     * two disagree for a few (FLDMMDL_GetMovedPosSaveSize is Thumb in its
     * source header and ARM in the listing), so the lookup keys on the
     * address alone: the host function is the same either way. */
    u32 key = value & ~1u;
    u32 i;

    if (g_set == NULL) {
        return NULL;
    }
    for (i = 0; i < g_set->count; i++) {
        const BwOvlImage *o = g_set->images[i];
        u32 lo = 0, hi;

        if (o == NULL || o->entries == NULL) {
            continue;
        }
        hi = o->entry_count;
        while (lo < hi) {
            u32 mid = lo + (hi - lo) / 2u;
            if ((o->entries[mid].addr & ~1u) < key) {
                lo = mid + 1u;
            } else {
                hi = mid;
            }
        }
        if (lo < o->entry_count && (o->entries[lo].addr & ~1u) == key) {
            void *h = bw_mod_slot_lookup(o->id, key);

            if (h != NULL) {
                /* A mod installed an override for this (region, address)
                 * pair (mod_slot.h): it wins over the table entry. */
                return (BwDataFn)h;
            }
            if (found != NULL) {
                return NULL;        /* two overlays: needs a trampoline */
            }
            found = o->entries[lo].fn;
        }
    }
    return found;
}

void bw_ovl_stub(u32 id, u32 addr)
{
    char why[200];

    snprintf(why, sizeof(why),
             "overlay %lu: function 0x%08lX is not in this build (not matched in "
             "aletheia, or its source is excluded; tools/ovlink.py's report says "
             "which)", (unsigned long)id, (unsigned long)addr);
    bw_stub_reached("overlay stub", addr, why);
}

void bw_ovl_arm9_stub(u32 addr, const char *name)
{
    char why[200];

    snprintf(why, sizeof(why),
             "%s: ARM9 function an overlay calls and this link does not define "
             "(tools/ovlink.py --arm9-stubs)", name);
    bw_stub_reached("overlay ARM9 stub", addr, why);
}

void bw_ovl_dispatch_fail(u32 addr, const char *name, int resident, int why)
{
    char text[240];

    switch (why) {
    case BW_OVL_WHY_NOT_COMPILED:
        snprintf(text, sizeof(text),
                 "%s: overlay %d is resident over 0x%08lX but is not compiled "
                 "into this build", name, resident, (unsigned long)addr);
        break;
    case BW_OVL_WHY_SIGNATURE:
        snprintf(text, sizeof(text),
                 "%s: overlay %d is resident over 0x%08lX, and its function there "
                 "has another prototype than the trampoline's", name, resident,
                 (unsigned long)addr);
        break;
    default:
        snprintf(text, sizeof(text),
                 "%s: called with no overlay resident over 0x%08lX", name,
                 (unsigned long)addr);
        break;
    }
    bw_panic("overlay trampoline", addr, text);
}

void bw_mwcc_clear(void *dst, unsigned int size)
{
    memset(dst, 0, size);
}

void *bw_mwcc_memcpy(void *dst, const void *src, unsigned int size)
{
    return memcpy(dst, src, size);
}
