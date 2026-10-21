/*
 * sdk/mod_slot.c -- see mod_slot.h.  The (region, address) registry, and
 * the declared set the manifest's check asks (docs/mod-state.md, section 4).
 *
 * The registry holds a chain of hooks for one pair, not one hook: the two
 * modes of a hook mean two different things (docs/mod-overrides.md,
 * "Modes"), and only one of them is exclusive.
 *
 *   - A wrap runs and calls the next link of the chain itself, through
 *     bw_mod_slot_original, so two wraps on one function nest: the
 *     last-loaded wrap runs first, and each wrap's original is the next one
 *     inward, down to the game's own function or to a replacement.
 *   - A replace takes the slot for itself: the links under it do not run,
 *     and the port reports them as shadowed, naming the key and both mods.
 *
 * The chain runs from the newest hook (the one the slot calls) inward to the
 * oldest, and the game's own function is below them all.  Every install puts
 * the new hook in front, so the chain is the load order reversed.
 *
 * Every link carries the switch it answers by (docs/mod-overrides.md, "The
 * switch"): the name the mod's manifest stated, or the row's own function
 * name when it stated none.  The registry asks bw_hook_enabled with that
 * name, and a link whose switch is off is skipped: the walk goes on to the
 * next link inward, not past the whole chain.  The name is a run-time thing
 * (it comes from the mod's manifest) and the row's own name is a build-time
 * thing, so the manifest's apply hands the name to the registry
 * (bw_mod_slot_set_switch) and the install takes it from there.
 */
#include "mod_slot.h"
#include "mod_hooks.h"
#include "mod_manifest.h"

#include <stdio.h>
#include <string.h>

/*
 * How many hooks one pair may hold: the chain of a pair is the mods that
 * wrap it, and a few mods on one function is the case the chain is for.
 * A pair that reaches the cap refuses the install and says so.
 */
#define BW_MOD_SLOT_HOOKS_MAX 8

/* One link of a chain. */
typedef struct BwModSlotHook {
    void       *fn;           /* the mod's own function */
    const char *owner;        /* the mod that installed it, for the report */
    int         mode;         /* BW_MOD_OVERRIDE_REPLACE or BW_MOD_OVERRIDE_WRAP */
    const char *switch_name;  /* the switch this link answers by, or NULL */
} BwModSlotHook;

typedef struct BwModSlot {
    unsigned int   region;
    unsigned int   addr;
    void          *original;   /* the game's own function, as published */
    BwModSlotHook  hooks[BW_MOD_SLOT_HOOKS_MAX];
    unsigned int   hook_count;  /* how many links the chain holds */
    unsigned int   depth;       /* how deep in the chain the call is */
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

/*
 * The switch names the manifest's apply handed over, waiting for the
 * install that follows it (docs/mod-overrides.md, "The switch").  The name
 * is a run-time thing: it comes from the mod's manifest, and the loader
 * installs the hook without it.  So the apply leaves the name here, keyed by
 * the mod that owns the hook and the pair it hooks, and the install takes
 * it from here.
 *
 * The names are the manifest record's own: the record holds them until the
 * mod is unloaded, and the manifest's release path takes them back
 * (bw_mod_slot_forget_switches).
 */
#define BW_MOD_SWITCH_MAX 128

typedef struct BwModSwitchRow {
    const char  *owner;   /* the mod that owns the hook */
    unsigned int region;
    unsigned int addr;
    const char  *name;    /* the switch name the manifest stated */
} BwModSwitchRow;

static BwModSwitchRow g_switches[BW_MOD_SWITCH_MAX];
static unsigned int    g_switch_count;

/* The switch name the mod stated for the pair, or NULL. */
static const char *pending_switch(const char *owner, unsigned int region,
                                  unsigned int addr)
{
    unsigned int i;

    if (owner == NULL) {
        return NULL;
    }
    /* The most recent one wins: the apply hands the name over just before
     * the loader installs that mod's hook. */
    for (i = g_switch_count; i > 0; i--) {
        const BwModSwitchRow *row = &g_switches[i - 1];

        if (row->region == region && row->addr == addr &&
            row->owner != NULL && strcmp(row->owner, owner) == 0) {
            return row->name;
        }
    }
    return NULL;
}

/*
 * The switch a link answers by: the name the mod's manifest stated, or the
 * row's own function name when it stated none.  A row the build did not
 * publish has no name either, and the link is then always on.
 */
static const char *link_switch(const char *owner, unsigned int region,
                               unsigned int addr)
{
    const char *stated = pending_switch(owner, region, addr);

    if (stated != NULL && *stated != '\0') {
        return stated;
    }
    return bw_mod_slot_row_name(region, addr);
}

/*
 * The manifest's apply hands the switch name over: the mod's own name, the
 * pair it hooks and the name it stated.  A name that is NULL (the mod
 * stated none) is not kept: the row's own name stands then, and the install
 * finds it there.
 */
void bw_mod_slot_set_switch(const char *owner, unsigned int region,
                            unsigned int addr, const char *switch_name)
{
    unsigned int i;

    if (owner == NULL || switch_name == NULL || *switch_name == '\0') {
        return;
    }
    for (i = g_switch_count; i > 0; i--) {
        BwModSwitchRow *row = &g_switches[i - 1];

        if (row->region == region && row->addr == addr &&
            row->owner != NULL && strcmp(row->owner, owner) == 0) {
            row->name = switch_name;
            return;
        }
    }
    if (g_switch_count >= BW_MOD_SWITCH_MAX) {
        fprintf(stderr,
                "eidos: mod slot 0x%08X (region %u): too many switch names\n",
                addr, region);
        return;
    }
    g_switches[g_switch_count].owner = owner;
    g_switches[g_switch_count].region = region;
    g_switches[g_switch_count].addr = addr;
    g_switches[g_switch_count].name = switch_name;
    g_switch_count++;
}

/*
 * The mod's manifest is going away, and the names it carried went with it.
 * The links that used them are gone too: the loader takes a mod's own hooks
 * out of the chain before it releases the record.
 */
void bw_mod_slot_forget_switches(const char *owner)
{
    unsigned int i;

    if (owner == NULL) {
        return;
    }
    for (i = 0; i < g_switch_count; ) {
        if (g_switches[i].owner != NULL &&
            strcmp(g_switches[i].owner, owner) == 0) {
            unsigned int k;

            for (k = i + 1; k < g_switch_count; k++) {
                g_switches[k - 1] = g_switches[k];
            }
            g_switch_count--;
            continue;
        }
        i++;
    }
}

/*
 * Is this link's switch on?  The link carries the name it answers by: the
 * name the mod's manifest stated, or the row's own function name when it
 * stated none.  A link with no name at all (a row the build did not
 * publish) is always on: there is no switch to ask.
 */
static int link_on(const BwModSlotHook *h)
{
    if (h->switch_name == NULL || *h->switch_name == '\0') {
        return 1;
    }
    return bw_hook_enabled(h->switch_name) != 0;
}

/*
 * The first link at or after `from` whose switch is on, or -1 when there is
 * none.  A link whose switch is off is skipped, so the walk goes on to the
 * next link inward instead of falling past the whole chain.
 */
static int next_link(BwModSlot *s, unsigned int from)
{
    unsigned int i;

    for (i = from; i < s->hook_count; i++) {
        if (link_on(&s->hooks[i])) {
            return (int)i;
        }
    }
    return -1;
}

/*
 * The live hook of a pair: the head of the chain, the one the slot calls.
 * A pair with no hook holds none, and the slot calls the game's own
 * function.  A hook whose switch is off is skipped, and the next one in the
 * chain takes its place.
 *
 * The lookup also starts the chain's walk: a dispatch begins at the head,
 * so the wrap that runs first is the one the pair holds at the front.
 */
void *bw_mod_slot_lookup(unsigned int region, unsigned int addr)
{
    BwModSlot *s = find(region, addr, 0);
    int i;

    if (s == NULL) {
        return NULL;
    }
    i = next_link(s, 0);
    if (i < 0) {
        /* Every link is switched off: the pair holds the game's own
         * function, and the walk starts over. */
        s->depth = 0;
        return NULL;
    }
    s->depth = (unsigned int)i;
    return s->hooks[i].fn;
}

/*
 * The next function in the chain, for the wrap that is running: the link
 * one inward from the one that asked, or the game's own function when the
 * chain has no link left.  A link whose switch is off is skipped, so a wrap
 * that is switched off does not hide the links under it.
 *
 * The pair's depth says how deep the walk is.  The dispatch starts it at the
 * head (bw_mod_slot_lookup), and every ask moves it one link inward, so the
 * wrap the slot called reaches the wrap below it and that one reaches the
 * game's own function.  A walk that reaches the end starts over, so the
 * next dispatch begins at the head again.
 *
 * One question, one answer: the call does not say which link is asking, so
 * a wrap that asks twice reaches the second link inward the second time.
 * The chain is walked from the head on every dispatch, so a wrap that asks
 * once per dispatch is exact.
 */
void *bw_mod_slot_original(unsigned int region, unsigned int addr)
{
    BwModSlot *s = find(region, addr, 0);
    int i;

    if (s == NULL) {
        return NULL;
    }
    i = next_link(s, s->depth + 1);
    if (i >= 0) {
        void *fn = s->hooks[i].fn;

        s->depth = (unsigned int)i;
        if (s->hooks[i].mode == BW_MOD_OVERRIDE_REPLACE &&
            next_link(s, (unsigned int)i + 1) < 0) {
            /* The innermost link is a replacement: it does not ask for the
             * original, so the walk is over and the next dispatch starts at
             * the head again. */
            s->depth = 0;
        }
        return fn;
    }
    /* The end of the chain: the game's own function, and the walk starts
     * over for the next dispatch. */
    s->depth = 0;
    return s->original;
}

/*
 * Install a hook for a pair.  `mode` is the row's mode (sdk/mod_manifest.h):
 * a wrap nests in front of the chain the pair holds, and a replace takes
 * the slot for itself.
 *
 * The chain is the load order reversed: every install puts the new hook in
 * front, so the last-loaded wrap runs first and each wrap's original is the
 * hook that was loaded before it.
 *
 * The new link carries the switch it answers by: the name the mod's
 * manifest stated, or the row's own function name when it stated none.
 * The loader installs the hook and does not carry the name, so the name
 * comes from the manifest's apply, which handed it over before the install
 * (bw_mod_slot_set_switch).
 *
 * Returns 0, or -1 when the pair cannot take the hook.
 */
int bw_mod_slot_install_mode(unsigned int region, unsigned int addr,
                             void *override, const char *owner, int mode)
{
    BwModSlot *s = find(region, addr, 0);
    unsigned int i;

    if (s == NULL) {
        if (g_count >= BW_MOD_SLOT_MAX) {
            fprintf(stderr,
                    "eidos: mod slot 0x%08X (region %u): the table is full\n",
                    addr, region);
            return -1;
        }
        s = find(region, addr, 1);
        if (s == NULL) {
            return -1;
        }
    }
    if (s->hook_count >= BW_MOD_SLOT_HOOKS_MAX) {
        fprintf(stderr,
                "eidos: mod slot 0x%08X (region %u): too many hooks on one "
                "function\n", addr, region);
        return -1;
    }
    if (mode == BW_MOD_OVERRIDE_REPLACE) {
        /* A replace takes the slot for itself: the links under it do not
         * run, and the port names them (docs/mod-overrides.md, "Several
         * mods on one function").  The same function twice is not a
         * shadow, so it is not reported. */
        for (i = 0; i < s->hook_count; i++) {
            if (s->hooks[i].fn == override) {
                continue;
            }
            fprintf(stderr,
                    "eidos: mod slot 0x%08X (region %u): %s shadows %s\n",
                    addr, region, owner != NULL ? owner : "a mod",
                    s->hooks[i].owner != NULL ? s->hooks[i].owner
                                              : "another mod");
        }
    }
    /* The new hook goes in front of the chain. */
    for (i = s->hook_count; i > 0; i--) {
        s->hooks[i] = s->hooks[i - 1];
    }
    s->hooks[0].fn = override;
    s->hooks[0].owner = owner;
    s->hooks[0].mode = mode;
    s->hooks[0].switch_name = link_switch(owner, region, addr);
    s->hook_count++;
    s->depth = 0;
    return 0;
}

/*
 * Take one hook out of a pair's chain: the one with this function, put in
 * by this mod.  The hook the pair holds after the removal is the one the
 * slot calls, and when the chain is empty the pair holds the game's own
 * function again.
 *
 * `override` NULL means the live hook: the head of the chain.
 *
 * Returns 0, or -1 when the pair holds no such hook.
 */
int bw_mod_slot_remove(unsigned int region, unsigned int addr, void *override,
                        const char *owner)
{
    BwModSlot *s = find(region, addr, 0);
    unsigned int i;

    if (s == NULL || s->hook_count == 0) {
        return -1;
    }
    for (i = s->hook_count; i > 0; i--) {
        BwModSlotHook *h = &s->hooks[i - 1];
        unsigned int k;

        if (override != NULL && h->fn != override) {
            continue;
        }
        if (owner != NULL && h->owner != NULL &&
            strcmp(h->owner, owner) != 0) {
            continue;
        }
        /* The link goes out, and the ones below it move up. */
        for (k = i - 1; k + 1 < s->hook_count; k++) {
            s->hooks[k] = s->hooks[k + 1];
        }
        s->hook_count--;
        s->depth = 0;
        return 0;
    }
    return -1;
}

/*
 * The install the header carries: the hook replaces the game's function,
 * which is the exclusive mode (docs/mod-overrides.md, "Modes").  The loader
 * installs a wrap with bw_mod_slot_install_mode and takes a hook out again
 * with bw_mod_slot_remove, so the chain keeps the hooks of the other mods.
 */
int bw_mod_slot_install(unsigned int region, unsigned int addr, void *override,
                        const char *owner)
{
    if (override == NULL) {
        /* A revert: the live hook goes away and the pair holds the next
         * link of the chain, or the game's own function when the chain is
         * empty.  That is not a second mod shadowing the first, so nothing
         * is reported (docs/mod-overrides.md, "Several mods on one
         * function"). */
        return bw_mod_slot_remove(region, addr, NULL, owner);
    }
    return bw_mod_slot_install_mode(region, addr, override, owner,
                                    BW_MOD_OVERRIDE_REPLACE);
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

/* ------------------------------------------------------------------ */
/* The rows' names and prototypes                                       */
/* ------------------------------------------------------------------ */
/*
 * The declared set says which pairs a mod may override; the rows' own names
 * and their prototypes say what those pairs are.  The build publishes both
 * beside the set (docs/mod-overrides.md, "The prototype is checked"):
 *
 *   - the name is the switch a hook on the row answers by when the mod's
 *     manifest states none;
 *   - the prototype is the game's own prototype, normalised the way
 *     tools/ovlink.py's signature normalises it, and the manifest's `sig=`
 *     is compared with it.
 *
 * The boot's rows come with the boot's link file (bw_boot_link_init) and the
 * overlays' rows with the generated dispatcher, so a row of an overlay that
 * is not compiled in has a name but no prototype.
 */
#define BW_MOD_ROWS_MAX 512

typedef struct BwModRow {
    unsigned int region;
    unsigned int addr;
    const char  *name;    /* the row's function name, or NULL */
    const char  *proto;   /* the normalised prototype, or NULL */
} BwModRow;

static BwModRow   g_rows[BW_MOD_ROWS_MAX];
static unsigned int g_row_count;

/* The row for a pair, or NULL.  A new row is made when `create` says so. */
static BwModRow *find_row(unsigned int region, unsigned int addr, int create)
{
    unsigned int i;

    for (i = 0; i < g_row_count; i++) {
        if (g_rows[i].region == region && g_rows[i].addr == addr) {
            return &g_rows[i];
        }
    }
    if (!create || g_row_count >= BW_MOD_ROWS_MAX) {
        return NULL;
    }
    g_rows[g_row_count].region = region;
    g_rows[g_row_count].addr = addr;
    g_rows[g_row_count].name = NULL;
    g_rows[g_row_count].proto = NULL;
    return &g_rows[g_row_count++];
}

/*
 * Publish the rows' names and prototypes.  The build calls this at
 * start-up, beside the declared set.  A later call adds rows and fills in
 * what an earlier one left out: the boot publishes every row's name, and
 * the overlays' rows add their prototypes when the overlay is compiled in.
 */
void bw_mod_publish_protos(const BwModSlotProto *rows, unsigned int count)
{
    unsigned int i;

    if (rows == NULL) {
        return;
    }
    for (i = 0; i < count; i++) {
        BwModRow *row = find_row(rows[i].region, rows[i].addr & ~1u, 1);

        if (row == NULL) {
            fprintf(stderr,
                    "eidos: the declared rows hold %u entries, %d fit: the "
                    "rest answer as unknown\n", count, BW_MOD_ROWS_MAX);
            return;
        }
        if (rows[i].name != NULL) {
            row->name = rows[i].name;
        }
        if (rows[i].proto != NULL) {
            row->proto = rows[i].proto;
        }
    }
}

/* The row's own name, or NULL when the build did not publish that row. */
const char *bw_mod_slot_row_name(unsigned int region, unsigned int addr)
{
    BwModRow *row = find_row(region, addr & ~1u, 0);

    return row != NULL ? row->name : NULL;
}

/* The row's normalised prototype, or NULL when the build did not publish
 * one: a row the link does not carry has no prototype to compare with. */
const char *bw_mod_slot_prototype(unsigned int region, unsigned int addr)
{
    BwModRow *row = find_row(region, addr & ~1u, 0);

    return row != NULL ? row->proto : NULL;
}
