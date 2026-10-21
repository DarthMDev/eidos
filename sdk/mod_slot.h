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
 * The on/off switch is sdk/mod_hooks.h's bw_hook_enabled, and the registry
 * asks it: every link of a chain carries the switch its mod's manifest
 * named, and a link whose switch is off is skipped, so the walk goes on to
 * the next link inward instead of falling past the whole chain.  The switch
 * a link answers by is the name the manifest stated, or the row's own
 * function name when the manifest stated none (docs/mod-overrides.md, "The
 * switch").
 *
 * The declared set is here too: the build emits the whole of
 * mod/declared_set.txt as one table and registers it at start-up, and
 * bw_mod_slot_declared answers whether a (region, address) pair is
 * declared (docs/mod-state.md, section 4).
 *
 * The rows' own names and their normalised prototypes come with it: the
 * build publishes them beside the table (bw_mod_publish_protos), the name is
 * the switch a link without one answers by, and the prototype is what the
 * manifest's `sig=` is compared with (docs/mod-overrides.md, "The prototype
 * is checked").
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

/* One row of the declared set: the (region, address) pair a mod may
 * override (docs/mod-interface.md, section 1).  The build emits the whole
 * set as one table, the arm9 rows and the overlay rows together
 * (docs/mod-state.md, section 4). */
typedef struct BwModDeclared {
    unsigned int region;   /* BW_MOD_REGION_ARM9 or BW_MOD_REGION_OVL(id) */
    unsigned int addr;     /* the even function start */
} BwModDeclared;

/* One declared row's own name and its normalised prototype, as the build
 * publishes them (docs/mod-overrides.md, "The prototype is checked").  The
 * name is the function name the row carries in mod/declared_set.txt, and it
 * is the switch a hook on the row answers by when the mod's manifest states
 * none.  The prototype is the game's own prototype, normalised the way
 * tools/ovlink.py's signature normalises it: "<return type> (<parameter
 * types>)", the parameters separated by ", " and "void" when there are none.
 * A row the build does not carry has no prototype, and the manifest's
 * `sig=` cannot be compared with it. */
typedef struct BwModSlotProto {
    unsigned int region;   /* BW_MOD_REGION_ARM9 or BW_MOD_REGION_OVL(id) */
    unsigned int addr;     /* the even function start */
    const char *name;      /* the row's function name, or NULL */
    const char *proto;     /* the normalised prototype, or NULL */
} BwModSlotProto;

/* Register the declared set: the whole table of mod/declared_set.txt, as
 * the build emits it.  The build calls this at start-up (bw_boot_link_init),
 * before any mod loads, so the manifest's check answers for every region
 * whether or not that region has loaded. */
void bw_mod_declare_table(const BwModDeclared *rows, unsigned int count);

/* Publish the declared rows' names and prototypes.  The build calls this at
 * start-up, beside the declared set: the boot's rows from
 * bw_boot_link_init, the overlays' rows from the generated dispatcher.  A
 * later call adds rows and fills in what an earlier one left out; it does
 * not take the earlier ones away. */
void bw_mod_publish_protos(const BwModSlotProto *rows, unsigned int count);

/* The row's own name, its normalised prototype, or NULL when the build did
 * not publish that row.  The address may be the even start or the
 * Thumb-tagged value a code pointer holds: the low bit is masked before the
 * lookup. */
const char *bw_mod_slot_row_name(unsigned int region, unsigned int addr);
const char *bw_mod_slot_prototype(unsigned int region, unsigned int addr);

/* Is (region, addr) a declared address?  1 when it is, 0 when it is not.
 * The address may be the even start or the Thumb-tagged value a code
 * pointer holds: the low bit is masked before the lookup. */
int bw_mod_slot_declared(unsigned int region, unsigned int addr);

/* The override installed for (region, addr), or NULL.  A generated slot asks
 * this before it calls anything.  The link the answer is the first link of
 * the chain whose switch is on: a link whose switch is off is skipped, and
 * the walk goes on to the next link inward. */
void *bw_mod_slot_lookup(unsigned int region, unsigned int addr);

/* The original function for (region, addr) as the build published it, or
 * NULL when nothing published one.  A wrap override calls it.  The walk
 * goes on inward the same way: a link whose switch is off is skipped. */
void *bw_mod_slot_original(unsigned int region, unsigned int addr);

/* The switch a mod's hook on (region, addr) answers by: the name the mod's
 * manifest stated, or NULL when it stated none and the row's own name
 * stands.  The manifest's apply hands the name to the registry
 * (sdk/mod_manifest.c), because the loader resolves the symbol and
 * installs the hook itself and does not carry the name; the install takes
 * it from here.  `owner` names the mod, the way the install's owner does. */
void bw_mod_slot_set_switch(const char *owner, unsigned int region,
                            unsigned int addr, const char *switch_name);

/* Forget the switch names a mod handed over: the mod's manifest is going
 * away, and the names it carried went with it.  The manifest's release path
 * calls this before it frees the record (sdk/mod_manifest.c). */
void bw_mod_slot_forget_switches(const char *owner);

/* Install `override` for (region, addr).  `owner` names the installing mod
 * for the conflict report.  A later install of one pair wins and the earlier
 * is reported shadowed (docs/mod-overrides.md).  Returns 0, or -1 when the
 * table is full. */
int bw_mod_slot_install(unsigned int region, unsigned int addr, void *override,
                        const char *owner);

/* Install `override` for (region, addr) in the row's mode (sdk/mod_manifest.h
 * carries the two).  The registry holds a chain per pair: the new link goes
 * in front, so the chain is the load order reversed and the last-loaded wrap
 * runs first.  A wrap asks the registry for the next link inward with
 * bw_mod_slot_original, so the wraps nest; a replace takes the slot for
 * itself, the links under it do not run, and the port names them
 * (docs/mod-overrides.md).  Returns 0, or -1 when the table is full or the
 * pair already holds as many links as it may. */
int bw_mod_slot_install_mode(unsigned int region, unsigned int addr,
                             void *override, const char *owner, int mode);

/* Take one hook out of (region, addr)'s chain: the one with this function,
 * put in by this mod.  `override` NULL means the live hook, the head of the
 * chain.  The link the pair holds after the removal is the one the slot
 * calls, and when the chain is empty the pair holds the game's own function
 * again.  A hook that was shadowed comes out too, so a chain never keeps a
 * pointer into a library that is about to be unloaded.  Returns 0, or -1 when
 * the pair holds no such hook. */
int bw_mod_slot_remove(unsigned int region, unsigned int addr, void *override,
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
