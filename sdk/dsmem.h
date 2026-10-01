/*
 * sdk/dsmem.h -- the DS address space the matched code's fixed addresses
 * land in.  docs/boot.md, "The DS address space", is the design; this is the
 * contract the rewrite (tools/dsaddr.py), the shims and the renderer share.
 *
 * Every function here takes and returns DS addresses as u32 and host memory
 * as pointers.  Nothing in this header stores a host pointer in a DS-shaped
 * u32 (portability rule 5).
 */

#ifndef BW_PORT_DSMEM_H
#define BW_PORT_DSMEM_H

#include <stddef.h>

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Region boundaries                                                   */
/* ------------------------------------------------------------------ */

#define BW_DS_MAIN_BASE     0x02000000u
#define BW_DS_MAIN_SIZE     0x01000000u   /* 16 MiB, TWL main RAM        */
#define BW_DS_WRAM_BASE     0x03000000u
#define BW_DS_WRAM_SIZE     0x00040000u   /* shared WRAM, mirrored        */
#define BW_DS_IO_BASE       0x04000000u
#define BW_DS_IO_SIZE       0x00010000u   /* ARM9 + TWL register window   */
#define BW_DS_PAL_BASE      0x05000000u
#define BW_DS_PAL_SIZE      0x00000800u
#define BW_DS_VRAM_BASE     0x06000000u
#define BW_DS_VRAM_SIZE     0x01000000u
#define BW_DS_OAM_BASE      0x07000000u
#define BW_DS_OAM_SIZE      0x00000800u

/* ------------------------------------------------------------------ */
/* Translation                                                         */
/* ------------------------------------------------------------------ */

/*
 * The host address standing for DS address `addr`.  This is what
 * tools/dsaddr.py puts in place of an integer-literal cast:
 *     *(vu16 *)0x04000208   ->   *((vu16 *)bw_ds_ptr(0x04000208u))
 * Never returns NULL: an address outside every backed region panics with
 * the address and the caller's DS-facing description, because a matched
 * store that silently went nowhere is the worst kind of port bug.
 */
void *bw_ds_ptr(u32 addr);

/* As bw_ds_ptr, but returns NULL instead of panicking (for probes). */
void *bw_ds_try_ptr(u32 addr);

/*
 * The reverse, for the few places the game stores an address as a number
 * and later reads through it: the DS address of a host pointer that lies in
 * a backed region or a registered object, or 0 if it does not.
 */
u32 bw_ds_addr_of(const void *host);

/*
 * Register a host object as the backing of DS range [addr, addr + size).
 * sdk/game_globals.c and the generated data definitions call this for every
 * ARM9 global they define, so a literal cast to a global's address and an
 * `extern` reference to the same global reach the same bytes.  Later
 * registrations of an overlapping range are a programming error and panic.
 */
void bw_ds_register(u32 addr, void *host, u32 size, const char *name);

/* ------------------------------------------------------------------ */
/* I/O register file                                                   */
/* ------------------------------------------------------------------ */

/*
 * The register file is plain memory: a matched store through bw_ds_ptr
 * writes it, and the renderer and the shims read it.  Registers with side
 * effects get a hook.  A write hook runs after the matched code's store has
 * landed (the hook sees the new value and may change it, e.g. IF's
 * write-one-to-clear); a read hook runs before the value is returned.
 *
 * Because the rewrite turns a register access into an ordinary host memory
 * access, hooks cannot fire on the access itself.  They fire at the port's
 * synchronisation points instead: bw_io_sync() is called by every shim that
 * hands control to or from the game (end of frame, interrupt delivery,
 * thread switch, any SDK shim that reads hardware state), and it runs the
 * write hook of every register whose bytes changed since the last sync.
 * The registers that need an immediate answer on read (VCOUNT, KEYINPUT,
 * the IPC FIFO status) are kept current by the shims that change them.
 */
typedef void (*BwIoWriteHook)(u32 addr, u32 old_value, u32 new_value);

/* Byte offset into the register file; addr must be in the I/O window.     */
volatile u8 *bw_io(u32 addr);

/* Typed accessors for port code (matched code goes through bw_ds_ptr).    */
u16  bw_io_read16(u32 addr);
u32  bw_io_read32(u32 addr);
void bw_io_write16(u32 addr, u16 v);   /* runs the hook immediately        */
void bw_io_write32(u32 addr, u32 v);   /* runs the hook immediately        */

/* `width` is 1, 2 or 4.  One hook per register address.                   */
void bw_io_hook(u32 addr, u32 width, BwIoWriteHook hook);

/* Compare the register file with its last-synced copy, run changed hooks. */
void bw_io_sync(void);

/* ------------------------------------------------------------------ */
/* VRAM banks                                                          */
/* ------------------------------------------------------------------ */

/*
 * The nine banks, A..I, as host memory.  VRAMCNT_A..I (0x04000240..0x04000249)
 * decide where each is visible in the 0x06xxxxxx window; bw_ds_ptr resolves
 * a VRAM address through the current mapping, and the renderer reads banks
 * through the same mapping (BG, OBJ, extended palettes, texture slots).
 */
enum { BW_VRAM_A, BW_VRAM_B, BW_VRAM_C, BW_VRAM_D, BW_VRAM_E,
       BW_VRAM_F, BW_VRAM_G, BW_VRAM_H, BW_VRAM_I, BW_VRAM_BANKS };

u8  *bw_vram_bank(int bank, u32 *size_out);
/* Re-read every VRAMCNT and rebuild the mapping (the VRAMCNT write hook). */
void bw_vram_remap(void);

/* Reset every region and the register file to power-on state.             */
void bw_ds_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_DSMEM_H */
