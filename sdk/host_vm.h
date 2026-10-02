/*
 * sdk/host_vm.h -- reserve a large zeroed block without committing it.
 *
 * sdk/dsmem.c backs the DS's 16 MiB of main RAM with one contiguous host
 * block, because matched code takes a DS address, turns it into a host
 * pointer once and then does pointer arithmetic from it; a run of separately
 * allocated chunks would make arithmetic across a chunk boundary land in
 * unrelated memory without a trace.  Committing 16 MiB up front for the few
 * pages the game touches outside its registered globals is wasteful, so the
 * block is reserved and committed in pieces.  That needs the operating
 * system, and this is the one file that talks to it (README.md, "Portability
 * rules": OS calls live behind one small file with an #ifdef per family).
 *
 *   Win32   VirtualAlloc(MEM_RESERVE), then MEM_COMMIT per piece on demand.
 *   POSIX   one anonymous private mapping with MAP_NORESERVE; the kernel
 *           already commits a page on first touch, so commit is a no-op.
 *   other   calloc, committed up front.  Correct, merely not lazy.
 *
 * The same file reserves a block at a fixed host address when asked
 * (bw_vm_reserve_at): sdk/dsmem.c's identity mapping, where the DS's main
 * RAM lives at host address 0x02000000 so a DS address stored in the game's
 * data is already the host address of its bytes (docs/boot.md, "Identity
 * mapping").  The block is never moved and never replaces a mapping that is
 * already there: when the range is taken the call fails and the caller
 * falls back to a movable block.
 *
 *   Win32   VirtualAlloc(addr, MEM_RESERVE), which fails if any page of the
 *           range is in use; the result is checked to be exactly addr.  A
 *           Win32 loader maps code pages, locale files and user32's GDI
 *           table into the low addresses before main runs, so the range is
 *           often taken already: bw_vm_relaunch_reserved starts the program
 *           again, suspended, reserves the ranges in the new process
 *           before its loader runs, and the child's bw_vm_reserve_at takes
 *           over a reservation it finds there.  No linker option is
 *           involved, so MSVC and MinGW builds behave alike.
 *   POSIX   mmap(addr, PROT_NONE) with MAP_FIXED_NOREPLACE where the
 *           headers have it (an older kernel takes the flag as a hint), and
 *           a plain hint elsewhere; a block the kernel put anywhere else is
 *           unmapped and the call fails.  Commit is mprotect.
 *   other   always fails.
 */

#ifndef BW_PORT_HOST_VM_H
#define BW_PORT_HOST_VM_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A zero-filled block of `size` bytes, reserved for the process lifetime,
 * or NULL.  Only bytes passed to bw_vm_commit may be touched. */
void *bw_vm_reserve(size_t size);

/* A zero-filled, uncommitted block at exactly host address `addr` (a
 * multiple of 64 KiB), or NULL if the host cannot give that range. */
void *bw_vm_reserve_at(size_t addr, size_t size);

/*
 * Run this program again as a child process with the `count` ranges
 * [addr[i], addr[i] + size[i]) reserved in it before it starts, so that its
 * bw_vm_reserve_at calls for them succeed whatever the loader maps; wait for
 * it, store its exit status in *status and return 1.  Returns 0 (nothing
 * run) on hosts that do not need it (POSIX), in the child itself, and when
 * the child cannot be created with the ranges free.  Win32 only does it.
 */
int bw_vm_relaunch_reserved(const size_t *addr, const size_t *size, int count,
                            int *status);

/* Make [p, p + size) of a reserved block usable.  0 on success.  Idempotent;
 * committed bytes that were never written read as zero. */
int bw_vm_commit(void *p, size_t size);

/* Return [p, p + size) to the reserved, zero state, releasing its pages.
 * Nothing in the range may be touched again before another bw_vm_commit. */
void bw_vm_discard(void *p, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_HOST_VM_H */
