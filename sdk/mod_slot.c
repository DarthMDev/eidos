/*
 * sdk/mod_slot.c -- see mod_slot.h.  The (region, address) registry, and
 * the declared set the manifest's check asks (docs/mod-state.md, section 4).
 */
#include "mod_slot.h"

#include <stdio.h>
#include <string.h>

typedef struct BwModSlot {
    unsigned int region;
    unsigned int addr;
    void        *override;
    void        *original;
    const char  *owner;      /* the installing mod, for the conflict report */
} BwModSlot;

static BwModSlot g_slots[BW_MOD_SLOT_MAX];
static unsigned  g_count;

static BwModSlot *find(unsigned int region, unsigned int addr, int create)
{
    unsigned int i;

    for (i = 0; i < g_count; i++) {
        if (g_slots[i].region == region && g_slots[i].addr == addr) {
            return &g_slots[i];
        }
    }
    if (!create || g_count >= BW_MOD_SLOT_MAX) {
        return NULL;
    }
    memset(&g_slots[g_count], 0, sizeof(g_slots[g_count]));
    g_slots[g_count].region = region;
    g_slots[g_count].addr = addr;
    return &g_slots[g_count++];
}

void *bw_mod_slot_lookup(unsigned int region, unsigned int addr)
{
    BwModSlot *s = find(region, addr, 0);
    return s != NULL ? s->override : NULL;
}

void *bw_mod_slot_original(unsigned int region, unsigned int addr)
{
    BwModSlot *s = find(region, addr, 0);
    return s != NULL ? s->original : NULL;
}

int bw_mod_slot_install(unsigned int region, unsigned int addr, void *override,
                        const char *owner)
{
    BwModSlot *s = find(region, addr, 1);

    if (s == NULL) {
        return -1;
    }
    if (override == NULL) {
        /* A revert: the mod is going away and the slot goes back to the
         * game's own function.  That is not a second mod shadowing the
         * first, so nothing is reported (docs/mod-overrides.md, "Two mods on
         * one function"). */
        s->override = NULL;
        s->owner = NULL;
        return 0;
    }
    if (s->override != NULL && s->override != override) {
        fprintf(stderr,
                "eidos: mod slot 0x%08X (region %u): %s shadows %s\n",
                addr, region, owner != NULL ? owner : "a mod",
                s->owner != NULL ? s->owner : "another mod");
    }
    s->override = override;
    s->owner = owner;
    return 0;
}

void bw_mod_slot_publish(unsigned int region, unsigned int addr, void *original)
{
    BwModSlot *s = find(region, addr, 1);

    if (s != NULL) {
        s->original = original;
    }
}

void bw_mod_slot_publish_table(const BwModSlotOriginal *rows, unsigned int count)
{
    unsigned int i;

    for (i = 0; i < count; i++) {
        bw_mod_slot_publish(rows[i].region, rows[i].addr, rows[i].original);
    }
}

/*
 * A link with no compiled overlay has nothing to publish.  The generated
 * ovl_dispatch.c registers its dispatcher at start-up; until then, and in a
 * link with no overlay, this is a no-op.
 */
static void (*g_overlay_publish)(unsigned int);

void bw_mod_slot_set_overlay_publish(void (*fn)(unsigned int id))
{
    g_overlay_publish = fn;
}

void bw_ovl_mod_publish(unsigned int id)
{
    if (g_overlay_publish != NULL) {
        g_overlay_publish(id);
    }
}

/* ------------------------------------------------------------------ */
/* The declared set                                                    */
/* ------------------------------------------------------------------ */
/*
 * The declared set is the whole of mod/declared_set.txt: the (region,
 * address) pairs a mod may override (docs/mod-interface.md, section 1).
 * The build emits it as one table and registers it at start-up
 * (docs/mod-state.md, section 4), so the manifest's check answers for
 * every region: a row the link does not carry has no slot, and an
 * overlay's slot does not exist until that overlay loads, but the address
 * is declared either way.
 *
 * The set holds a few hundred rows, so it is kept sorted by region and
 * address and searched by bisection.
 */
#define BW_MOD_DECLARED_MAX 512

static BwModDeclared g_declared[BW_MOD_DECLARED_MAX];
static unsigned int  g_declared_count;

/* Where a row stands against the (region, addr) key: -1 below it, 0 on
 * it, 1 above it. */
static int declared_cmp(unsigned int region, unsigned int addr,
                        const BwModDeclared *row)
{
    if (row->region != region) {
        return row->region < region ? -1 : 1;
    }
    if (row->addr != addr) {
        return row->addr < addr ? -1 : 1;
    }
    return 0;
}

/* The rows in order, by insertion sort: the build's table is already
 * sorted, so this is a walk over it, and a table a test registers in any
 * other order still ends up searchable. */
static void sort_declared(void)
{
    unsigned int i, j;

    for (i = 1; i < g_declared_count; i++) {
        BwModDeclared row = g_declared[i];

        for (j = i; j > 0 &&
                    declared_cmp(row.region, row.addr, &g_declared[j - 1]) < 0;
             j--) {
            g_declared[j] = g_declared[j - 1];
        }
        g_declared[j] = row;
    }
}

void bw_mod_declare_table(const BwModDeclared *rows, unsigned int count)
{
    unsigned int i;

    g_declared_count = 0;
    for (i = 0; i < count; i++) {
        if (g_declared_count >= BW_MOD_DECLARED_MAX) {
            fprintf(stderr,
                    "eidos: the declared set holds %u rows, %d fit: the rest "
                    "answer as not declared\n",
                    count, BW_MOD_DECLARED_MAX);
            break;
        }
        g_declared[g_declared_count++] = rows[i];
    }
    sort_declared();
}

int bw_mod_slot_declared(unsigned int region, unsigned int addr)
{
    unsigned int lo = 0;
    unsigned int hi = g_declared_count;

    /* The set holds the even start; a code pointer holds the Thumb-tagged
     * value, and the low bit says which. */
    addr &= ~1u;

    while (lo < hi) {
        unsigned int mid = lo + (hi - lo) / 2;
        int order = declared_cmp(region, addr, &g_declared[mid]);

        if (order > 0) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo < g_declared_count &&
           declared_cmp(region, addr, &g_declared[lo]) == 0;
}
