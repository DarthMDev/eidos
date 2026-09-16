/*
 * sdk/port.h -- the port's own API surface: the data root, the archive
 * handles, and the panic path the stubs use.
 *
 * Nothing in this header is an SDK name.  The SDK-named shims live one file
 * per SDK module (fs.c, mi.c, os_alloc.c); this is the glue that tells them
 * where the player's extracted assets are.
 */

#ifndef BW_PORT_PORT_H
#define BW_PORT_PORT_H

#include <stddef.h>

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Data root                                                           */
/* ------------------------------------------------------------------ */

/*
 * The directory tools/ndsrom.py's `extract` wrote, i.e. the one holding
 * `a/0/0/2`.  Set once at start-up; BW_DATA_ROOT from the environment wins
 * over the compiled-in default, and an explicit call wins over both.
 */
void        bw_set_data_root(const char *path);
const char *bw_data_root(void);

/*
 * Join the data root and a NitroFS-relative path such as "a/0/1/6" into
 * `out`.  Returns 0 on success, -1 if it would not fit.
 */
int bw_data_path(char *out, size_t out_size, const char *rel);

/*
 * The decompressed ARM9 image, extract/arm9_dec.bin, and the address it is
 * linked at.  Some of the matched code reads constant tables that live inside
 * the ARM9 binary rather than in a NitroFS file (the nature multiplier table
 * at 0x0209E324 is the one this lane needs).  Those are game data: they are
 * read out of the player's own extracted image at run time and never appear
 * in this repository.
 */
#define BW_ARM9_LOAD_ADDRESS 0x02004000u

/*
 * Map `size` bytes at ARM9 address `address` out of extract/arm9_dec.bin.
 * The image is loaded once and cached.  Returns NULL if the file is missing
 * or the range is out of bounds; the pointer stays valid for the process.
 */
const void *bw_arm9_rodata(u32 address, u32 size);

/* ------------------------------------------------------------------ */
/* Archives                                                            */
/* ------------------------------------------------------------------ */

/*
 * A NARC opened for streaming, laid out so that the matched readers
 * (func_02049578, func_020495F4, func_0204944C in src/matched/) see exactly
 * the object they were compiled against:
 *
 *     struct ArcFile { u8 file[0x48]; u32 imageBase; u16 fatOffset; u16 count; };
 *
 * The first 0x48 bytes are the SDK's FSFile, which our fs.c shim owns; the
 * three fields after it are the game's own and the matched code reads them
 * directly.  Keeping the prefix at exactly 0x48 bytes is a hard requirement
 * of that layout and is asserted at start-up.
 */
#define BW_FSFILE_SIZE 0x48

typedef struct BwArcFile {
    u8  file[BW_FSFILE_SIZE];
    u32 imageBase;   /* byte offset of the GMIF chunk within the file  */
    u16 fatOffset;   /* byte offset of the BTAF chunk within the file  */
    u16 count;       /* number of sub-files                            */
} BwArcFile;

/*
 * The FS shim's own open/size, on a host path.  `file` points at the 0x48
 * bytes the SDK's FSFile occupies, i.e. at a BwArcFile's `file` member.
 */
int bw_fs_open(void *file, const char *host_path);
u32 bw_fs_size(void *file);

/*
 * Open a NARC under the data root ("a/0/1/6") and fill in imageBase,
 * fatOffset and count from its chunk table.  Returns 0 on success.
 */
int  bw_arc_open(BwArcFile *arc, const char *rel_path);
void bw_arc_close(BwArcFile *arc);

/*
 * Read one whole sub-file into a caller-supplied buffer.  This is the port's
 * own convenience reader for test fixtures and for asset loading that does
 * not go through a matched game path; the matched readers are used wherever
 * one exists.  Returns the number of bytes read, or -1.
 */
long bw_arc_read_member(BwArcFile *arc, u32 member, void *dst, u32 dst_size);

/* Size of one sub-file without reading it, or -1. */
long bw_arc_member_size(BwArcFile *arc, u32 member);

/* ------------------------------------------------------------------ */
/* Well-known archive handles the matched code reaches through globals  */
/* ------------------------------------------------------------------ */

/*
 * `g_0214652C` is the ARM9 global the matched personal-data reader
 * (src/matched/unit_0201ADA4.c, src/matched/unit_0201AFD4.c) streams the
 * 0x3C-byte personal record through.  On the DS some not-yet-matched
 * start-up code fills it in; here the port opens a/0/1/6 and publishes it.
 */
int  bw_open_personal_archive(void);
void bw_close_personal_archive(void);

/*
 * `func_02019B04_Table` is the 25 x 5 nature multiplier table at ARM9
 * 0x0209E324 that src/matched/unit_02019B04.c indexes.  It is game data and
 * is copied out of the player's own extract/arm9_dec.bin at run time.
 * Returns 0 on success, -1 if the ARM9 image is not readable.
 */
int bw_load_nature_table(void);

/*
 * `g_020A7820` is the signed watched-heap id src/matched/sub_02030A00.c
 * gates the allocation audit on, an ARM9 data word at 0x020A7820.  Loaded
 * out of the player's own extract/arm9_dec.bin; the port's own definition is
 * -1 ("watch nothing") so a run with no image behaves like the retail one.
 * Returns 0 on success, -1 if the ARM9 image is not readable.
 */
int bw_load_watch_id(void);

/* ------------------------------------------------------------------ */
/* The host's aligned blocks                                           */
/* ------------------------------------------------------------------ */

/*
 * malloc with an alignment, and its release.  The malloc base is stored in
 * the word immediately before the pointer handed out, so a block may be
 * freed knowing only the aligned pointer.  This is what sdk/os_alloc.c's SDK
 * pair OS_AllocFromHeap / OS_FreeToHeap goes through.
 */
void *bw_aligned_alloc(u32 size, u32 align);
void  bw_aligned_free(void *p);

/*
 * One block of the expanded heap the game's own allocator bottoms out in,
 * and its release.  `size` is the whole block including the 0x1c-byte header
 * src/matched/sub_02030098.c writes in front of the payload, and what comes
 * back is the block, not the payload; `align` applies to the *payload*, i.e.
 * to `block + 0x1c`, which is the one place the host and the DS have to be
 * told apart.  sdk/os_alloc.c has the reasoning.
 *
 * Callers: stubs/reimpl_02060A08.c and stubs/reimpl_02060ABC.c, the two
 * halves src/matched/func_02060CA0.c dispatches to, and
 * stubs/reimpl_02030164.c, the raw free under src/matched/sub_020307B0.c.
 */
void *bw_block_alloc(u32 size, u32 align);
void  bw_block_free(void *block);

/* ------------------------------------------------------------------ */
/* The heap environment                                                */
/* ------------------------------------------------------------------ */

/*
 * `g_EnvModule` at 0x02146954 is the game's heap manager, and every matched
 * function in the heap layer reads it: the heap count at +8 bounds the id,
 * the byte table at +4 maps a public heap id to a record index, the record
 * array at +0 holds the arena handle the raw allocator allocates out of and
 * the live-allocation count it increments, and the halfword at +0xe is the
 * result code each of them writes (1 = id out of range, 2 = no such heap,
 * 3 = exhausted, 0 = success).  src/matched/sub_02030098.c's header is the
 * authority on all of it.
 *
 * On the DS the heap constructor at 0x0202FC48 fills the manager in.  That
 * function is not decompiled, so there is no matched constructor for the
 * port to call and it initialises the fields the matched code reads itself,
 * which is what this does.  `heaps` heap ids become valid, each with its own
 * arena handle; BW_HEAP_COUNT is the default.  Returns 0.
 */
#define BW_HEAP_COUNT 16u

int  bw_heap_init(u32 heaps);

/* Live allocations on one heap, as the manager counts them.  -1 if the id is
 * out of range.  The count is incremented by the matched raw allocator and
 * decremented by stubs/reimpl_02030164.c, so a balanced test ends at zero. */
long bw_heap_live_blocks(u32 heap_id);

/* The release side of the counter the matched raw allocator increments.
 * Called by stubs/reimpl_02030164.c with the heap id it recovered from the
 * block header. */
void bw_heap_note_release(u32 heap_id);

/* ------------------------------------------------------------------ */
/* Panic                                                               */
/* ------------------------------------------------------------------ */

/*
 * Called by every stub in stubs/ and by the assert sink.  Prints the
 * address and the reason on stderr and aborts; a port that reaches an
 * unimplemented game function has a hole in its closure, and failing loudly
 * is the only way the closure script and the linker keep agreeing.
 */
void bw_panic(const char *what, u32 address, const char *detail);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_PORT_H */
