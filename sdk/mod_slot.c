/*
 * sdk/mod_slot.c -- see mod_slot.h.  The (region, address) registry.
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
