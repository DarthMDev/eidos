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
