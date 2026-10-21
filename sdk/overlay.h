/*
 * sdk/overlay.h -- the overlay registry: what the port knows about each of
 * the game's 237 ARM9 overlays, and whether this build holds its code.
 *
 * On the DS an overlay is a file the SDK copies to a fixed RAM address,
 * clears the .bss after, and starts by running its static initialisers.  In
 * a native port the code is compiled in, so "loading" overlay N means:
 * check that every function of N is linked into this executable, reset N's
 * data to its load-time state, run its static initialisers, and record it
 * as resident.  sdk/overlay.c implements the SDK names on that model; this
 * header is the table it reads and the hooks port code installs.
 *
 * The table is generated, at build time, by tools/overlay_table.py from
 * aletheia's layout/layout.json (geometry, the overlay table's own fields)
 * and its manifest shards (which functions are matched), plus the list of
 * matched sources this build compiles (which functions are present).  It
 * holds addresses, sizes and names only.
 */

#ifndef BW_PORT_OVERLAY_H
#define BW_PORT_OVERLAY_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* What the port knows about one function of an overlay. */
enum {
    BW_OVF_OPEN    = 0,   /* known to exist, not matched in aletheia      */
    BW_OVF_MATCHED = 1,   /* matched in aletheia, not compiled into this  */
    BW_OVF_PRESENT = 2    /* matched and linked into this executable      */
};

typedef struct BwOverlayFunc {
    u32         address;
    u32         status;   /* BW_OVF_*                                     */
    const char *name;     /* NULL means the matcher's func_<address>      */
} BwOverlayFunc;

/* One static initialiser: the function pointer is set only when the
 * function is present in this build. */
typedef void (*BwOverlayInitFn)(void);

typedef struct BwOverlaySinit {
    u32             overlay;
    u32             address;
    BwOverlayInitFn fn;
} BwOverlaySinit;

typedef struct BwOverlayDesc {
    /* The overlay table entry (y9.bin), field for field. */
    u32 id;
    u32 ram_address;
    u32 ram_size;        /* code and initialised data                      */
    u32 bss_size;
    u32 sinit_init;      /* DS address of the initialiser list             */
    u32 sinit_init_end;
    u32 file_id;
    u32 compressed;      /* compressed size (24 bits) | flags << 24        */
    /* What the port knows, as slices of the table's arrays. */
    u32 func_first;      /* index into funcs[]                             */
    u32 func_count;      /* every function known in this overlay           */
    u32 matched_count;   /* of those, matched in aletheia                  */
    u32 present_count;   /* of those, linked into this build               */
    u32 sinit_first;     /* index into sinits[]                            */
    u32 sinit_count;     /* non-NULL initialisers (the terminator excluded) */
} BwOverlayDesc;

typedef struct BwOverlayTable {
    const BwOverlayDesc  *overlays;
    u32                   overlay_count;
    const BwOverlayFunc  *funcs;        /* sorted by address within each overlay */
    u32                   func_count;
    const BwOverlaySinit *sinits;
    u32                   sinit_count;
} BwOverlayTable;

/* The generated table (build/overlay_table.c). */
extern const BwOverlayTable bw_overlay_table_generated;

/*
 * The table the SDK names read: the generated one unless a test installed
 * its own.  bw_overlay_use_table(NULL) goes back to the generated one.
 * Installing a table also forgets which overlays are resident.
 */
void                  bw_overlay_use_table(const BwOverlayTable *table);
const BwOverlayTable *bw_overlay_table(void);
const BwOverlayDesc  *bw_overlay_desc(u32 id);

/*
 * Whether overlay `id` can be loaded in this build.
 *
 *   BW_OVL_PRESENT   every known function is linked in;
 *   BW_OVL_ABSENT    at least one is not: *missing is the lowest-addressed
 *                    one (status says whether aletheia has matched it);
 *   BW_OVL_UNKNOWN   no function of the overlay is known at all, so
 *                    presence cannot be claimed (*missing is NULL);
 *   BW_OVL_NO_SUCH   the id is past the table.
 *
 * "Every known function" is every function aletheia's metadata names in the
 * overlay: the matched ones, the overlay's SDK symbols, the functions its
 * matched sources call inside its own range, and its static initialisers.
 * It is a lower bound on the overlay's real function list; see docs/files.md,
 * "Overlays".
 */
enum { BW_OVL_PRESENT = 0, BW_OVL_ABSENT = 1, BW_OVL_UNKNOWN = 2, BW_OVL_NO_SUCH = 3 };

int bw_overlay_check(u32 id, const BwOverlayFunc **missing);

/* Writes the one-line diagnosis FS_LoadOverlay panics with into `out`. */
void bw_overlay_describe(u32 id, char *out, u32 out_size);

/*
 * Resetting an overlay's data.  On the DS, loading overlay N re-reads its
 * initialised data from the ROM and zeroes its .bss; in the port that data
 * is host objects, owned by whoever defines the overlay's globals.  The
 * hook is called on every load of a present overlay with its descriptor,
 * after the presence check and before the static initialisers, and must
 * put [ram_address, ram_address + ram_size + bss_size) back to load-time
 * state.  Without a hook, loading a present overlay panics rather than
 * running its code over stale data.
 */
typedef void (*BwOverlayImageHook)(const BwOverlayDesc *overlay);

void bw_overlay_set_image_hook(BwOverlayImageHook hook);

/*
 * Overlays compiled into the executable (sdk/overlay_image.c installs this;
 * docs/overlays.md).  For an overlay `compiled` answers 1 for, the loader
 * skips the all-present check above (a compiled overlay is loadable with
 * stubs in it; reaching one panics), calls `load` in place of the image
 * hook, and `start` in place of the table's static initialisers.  NULL
 * removes it.
 */
typedef struct BwOverlayProvider {
    int  (*compiled)(u32 id);
    void (*load)(const BwOverlayDesc *overlay);
    void (*start)(const BwOverlayDesc *overlay);
} BwOverlayProvider;

void bw_overlay_set_provider(const BwOverlayProvider *provider);

/* 1 if overlay `id` is resident (loaded and not since unloaded or overlaid). */
int bw_overlay_is_loaded(u32 id);

/* FSi_InitOverlay: nothing resident.  FS_Init calls it. */
void bw_overlay_sdk_init(void);

/*
 * The SDK's FSOverlayInfo as the game declares it (src/matched/arm9/
 * sub_02034D60.c, sub_02034DC4.c): the eight header words, the target
 * processor, and the file's ROM extent, all u32 at either pointer width.
 * The two initialiser fields hold DS addresses, never host pointers.
 */
typedef struct BwOverlayInfo {
    u32 id;
    u32 ram_address;
    u32 ram_size;
    u32 bss_size;
    u32 sinit_init;
    u32 sinit_init_end;
    u32 file_id;
    u32 compressed;
    u32 target;          /* MI_PROCESSOR_ARM9 = 0, ARM7 = 1             */
    u32 file_offset;     /* the file's ROM extent (CARDRomRegion), from  */
    u32 file_length;     /* the FAT when the port has one, else 0        */
} BwOverlayInfo;

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_OVERLAY_H */
