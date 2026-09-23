/*
 * sdk/os_arena.c -- the OS arena, the memory the game's heaps are built in.
 *
 * On the DS the SDK hands the game fixed regions of main RAM ("arenas") and
 * the game carves its heaps out of them once at boot.  The call that does it
 * is OS_AllocFromArenaLo at 0x02086788 (aletheia's layout/symbols_sdk.txt):
 * take `size` bytes off the low end of arena `id`, aligned to `align`, and
 * move the low end up.  There is no free; an arena only ever shrinks from the
 * bottom.  src/matched/sub_0202FC48.c, the heap environment constructor, is
 * the port's one caller, under the name sub_02086788: it takes the entry
 * table, the id table and every heap's region from arena 0.
 *
 * The host has no main RAM to hand out, so this file holds one region the
 * port allocates itself and serves every arena id out of it.  That is the
 * only substitution: from here upwards the heaps are the game's own
 * NitroSystem expanded heaps (src/matched/func_02060814.c and friends),
 * walking free lists inside this region exactly as they walk main RAM on the
 * DS, and a pointer the game's allocator hands out is an address inside it.
 *
 * Built only where EIDOS_HEAP_MATCHED is on, which needs 32-bit pointers:
 * the expanded heap's block header is 0x10 bytes with two pointers in it, and
 * the matched code both hardcodes the 0x10 and does its address arithmetic in
 * `int` (docs/pointer-width.md).
 */

#include <stdlib.h>
#include <string.h>

#include "port.h"

static u8  *g_arena_base;
static u32  g_arena_size;
static u32  g_arena_lo;      /* offset of the low end, grows upwards */

int bw_arena_reset(u32 size)
{
    free(g_arena_base);
    g_arena_base = NULL;
    g_arena_size = 0;
    g_arena_lo = 0;

    if (size == 0) {
        return 0;
    }
    /* malloc's own alignment is at least 8 on every host this builds for,
     * which is more than any alignment the game asks the arena for (4). */
    g_arena_base = (u8 *)malloc(size);
    if (g_arena_base == NULL) {
        return -1;
    }
    /* A fresh DS arena is whatever main RAM held; the heap constructor
     * writes every header it later reads, so nothing depends on this, and a
     * known fill makes a stray read show up as a pattern rather than as
     * whatever the host left there. */
    memset(g_arena_base, 0xA5, size);
    g_arena_size = size;
    return 0;
}

u32 bw_arena_used(void)
{
    return g_arena_lo;
}

u32 bw_arena_size(void)
{
    return g_arena_size;
}

int bw_arena_contains(const void *p)
{
    const u8 *b = (const u8 *)p;

    return g_arena_base != NULL && b >= g_arena_base
        && b < g_arena_base + g_arena_size;
}

/*
 * OS_AllocFromArenaLo.  Returns NULL when the arena cannot satisfy the
 * request, which is the SDK's contract and what sub_0202FC48 tests for: it
 * records the index of the heap that did not fit in the manager's error
 * halfword and returns 0.
 */
void *sub_02086788(u32 id, u32 size, u32 align)
{
    u32 lo;

    (void)id;
    if (g_arena_base == NULL) {
        return NULL;
    }
    if (align < 4u) {
        align = 4u;
    }
    lo = (g_arena_lo + (align - 1u)) & ~(align - 1u);
    if (lo > g_arena_size || size > g_arena_size - lo) {
        return NULL;
    }
    g_arena_lo = lo + size;
    return g_arena_base + lo;
}
