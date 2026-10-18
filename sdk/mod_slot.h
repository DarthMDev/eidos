/*
 * sdk/mod_slot.h -- the mod seam's slot registry (docs/mod-interface.md,
 * section 2).
 *
 * A declared DS address (mod/declared_set.txt) gets a generated slot
 * function: it asks the registry whether a mod installed an override for the
 * (region, address) pair, and calls that, else the original function.  With
 * no mod installed the slot calls the original, so the build behaves as it
 * did before the seam.
 *
 * The key is the pair, never the address alone: two overlays share one RAM
 * window (0x021C5200 is BTL_SERVER_FLOW_ProcFaintRewards in ov093 and
 * ZUKAN_DETAIL_FORM_SignalAnimDone in ov208), so the overlay id is part of
 * the key.
 *
 * The on/off switch is sdk/mod_hooks.h's bw_hook_enabled: a generated slot
 * asks it by the row's function name, and with the hook off calls the
 * original, the same rule a compiled hook follows (docs/hooks.md).
 */

#ifndef BW_PORT_MOD_SLOT_H
#define BW_PORT_MOD_SLOT_H

#ifdef __cplusplus
extern "C" {
#endif

/* The ARM9's static module.  No overlay id (0 and up) collides with it. */
#define BW_MOD_REGION_ARM9 0xFFFFFFFFu

/* The region of overlay `id`, the pair's overlay half. */
#define BW_MOD_REGION_OVL(id) ((unsigned int)(id))

/* How many (region, address) pairs the registry holds at once: the declared
 * set is a few hundred rows (mod/declared_set.txt), and a mod may install one
 * per row. */
#define BW_MOD_SLOT_MAX 512

/* One published original, for a per-overlay table in a generated link C. */
typedef struct BwModSlotOriginal {
    unsigned int region;
    unsigned int addr;
    void        *original;
} BwModSlotOriginal;

/* The override installed for (region, addr), or NULL.  A generated slot asks
 * this before it calls anything. */
void *bw_mod_slot_lookup(unsigned int region, unsigned int addr);

/* The original function for (region, addr) as the build published it, or
 * NULL when nothing published one.  A wrap override calls it. */
void *bw_mod_slot_original(unsigned int region, unsigned int addr);

/* Install `override` for (region, addr).  `owner` names the installing mod
 * for the conflict report.  A later install of one pair wins and the earlier
 * is reported shadowed (docs/mod-overrides.md).  Returns 0, or -1 when the
 * table is full. */
int bw_mod_slot_install(unsigned int region, unsigned int addr, void *override,
                        const char *owner);

/* Publish the original function for (region, addr).  The build's generated
 * code calls this at start-up (bw_boot_link_init) or when an overlay loads
 * (sdk/overlay_image.c, provider_load), so a wrap can reach the original.
 * A later publish of one pair replaces the earlier. */
void bw_mod_slot_publish(unsigned int region, unsigned int addr, void *original);

/* Publish every row of a generated per-overlay table. */
void bw_mod_slot_publish_table(const BwModSlotOriginal *rows, unsigned int count);

/*
 * Publish the declared originals of the compiled overlay `id`.  The
 * generated ovl_dispatch.c registers its dispatcher at start-up (a
 * constructor), so sdk/overlay_image.c calls this without a link-time
 * reference to the generated code; a link with no compiled overlay leaves
 * the dispatcher unset and this does nothing.
 */
void bw_mod_slot_set_overlay_publish(void (*fn)(unsigned int id));
void bw_ovl_mod_publish(unsigned int id);

#ifdef __cplusplus
}
#endif
#endif /* BW_PORT_MOD_SLOT_H */
