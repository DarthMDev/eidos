/*
 * sdk/os_alloc.c -- the OS heap module shim, onto malloc/free.
 *
 * The DS build carves fixed arenas out of main RAM and hands out blocks from
 * them by heap id; the matched game code never sees the arena, only the id
 * and the pointer.  On the host there is no reason to reproduce the arena:
 * the ids are kept as an argument and ignored, and every block comes from
 * malloc.  What the port *does* reproduce is the alignment contract, because
 * the game's allocator honours the `align` argument and some matched code
 * relies on it.
 *
 * What is shimmed here is the SDK pair OS_AllocFromHeap / OS_FreeToHeap and
 * the host's aligned-block helper that the rest of the port allocates
 * through.
 *
 * The game's own allocation entry point is no longer shimmed here.
 * sub_02030734, the raw allocator sub_02030098 and their bookkeeping are
 * matched sources in the build now (CMakeLists.txt, "the heap layer"), so
 * the port supplies only what they bottom out in: stubs/reimpl_02060CA0.c,
 * the expanded-heap allocator the raw allocator calls, and
 * stubs/reimpl_020307B0.c, its release counterpart.  Both go through the two
 * helpers below, which is what makes an allocation and its release agree
 * about where the malloc block really starts.
 */

#include <stdlib.h>
#include <string.h>

#include "port.h"

/*
 * An over-aligned block stores its malloc base in the word immediately
 * before the pointer handed out, so free can recover it.  Alignments up to
 * 32 bytes cover every value the matched code passes (4, 8, 0x20).
 */
#define BW_HEAP_HEADER 16u

void *bw_aligned_alloc(u32 size, u32 align)
{
    unsigned char *base;
    unsigned char *user;
    size_t slack;

    if (align < 4) {
        align = 4;
    }
    if (align > 256) {
        align = 256;
    }
    slack = (size_t)align + BW_HEAP_HEADER;
    base = (unsigned char *)malloc((size_t)size + slack);
    if (base == NULL) {
        return NULL;
    }
    user = base + BW_HEAP_HEADER;
    user += (align - ((size_t)(user - (unsigned char *)0) % align)) % align;
    memcpy(user - sizeof(unsigned char *), &base, sizeof(unsigned char *));
    return user;
}

void bw_aligned_free(void *p)
{
    unsigned char *base;

    if (p == NULL) {
        return;
    }
    memcpy(&base, (unsigned char *)p - sizeof(unsigned char *), sizeof(base));
    free(base);
}

/* ------------------------------------------------------------------ */
/* SDK names                                                           */
/* ------------------------------------------------------------------ */

/*
 * Portability note, and the first real one this lane found.  Some matched
 * sources declare these as all-u32, e.g.
 *
 *     extern void OS_FreeToHeap(u32 a, u32 b, u32 heap);
 *
 * because on the DS a pointer *is* a u32 and the matcher only needs the
 * relocation, not the type.  On an LP64 host that declaration cannot carry a
 * pointer.  Neither of these two is in the current port closure, so nothing
 * is broken today; when a TU that declares them this way enters the closure,
 * the fix belongs in the port layer (a wrapper include that predeclares the
 * pointer-typed form before the matched file's own extern), never in the
 * matched file, which must keep its DS bytes.
 */
void *OS_AllocFromHeap(u32 arena, void *heap, u32 size)
{
    (void)arena;
    (void)heap;
    return bw_aligned_alloc(size, 4);
}

void OS_FreeToHeap(u32 arena, void *heap, void *block)
{
    (void)arena;
    (void)heap;
    bw_aligned_free(block);
}
