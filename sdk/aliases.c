/*
 * sdk/aliases.c -- name aliases between matched files that spell the
 * same address two different ways.
 *
 * The DS build never needs these.  tools/csrc.py resolves a relocation to an
 * *address*, so a file that declares `extern void func_02034734(...)` and a
 * file that defines `void sub_02034734(...)` agree perfectly: both name
 * 0x02034734 and the splice lands at 0x02034734.  A host linker resolves by
 * name, and those two are different names.
 *
 * So every divergent pair needs one forwarder here, and
 * `python tools/closure.py` prints them under `alias` so the list can be
 * kept complete mechanically rather than by waiting for a link error.
 *
 * These forwarders are port code and are never compiled into the DS build.
 * The right long-term fix is a naming convention in src/matched/ (one prefix
 * per address, chosen by whoever matches it first), but renaming a symbol in
 * a matched file means re-verifying that file against the ROM, so it is the
 * integrator's call and not this lane's.
 */

#include "port.h"

/*
 * 0x02034734, the assertion sink.
 *
 *   defined  as sub_02034734  in src/matched/sub_02034734.c
 *   declared as func_02034734 in src/matched/func_02049578.c
 *                              and src/matched/func_0204944C.c
 *
 * The variadic tail is dropped rather than forwarded: sub_02034734's body
 * reads only the handler out of g_Sink and ignores every argument, so there
 * is nothing to forward, and C has no portable way to pass `...` on.
 */
extern void sub_02034734(const char *expr, u32 line, const char *file, ...);

void func_02034734(const char *expr, u32 line, const char *file, ...)
{
    sub_02034734(expr, line, file);
}

/*
 * 0x02060CA0, the expanded heap's allocation front door.
 *
 *   defined  as func_02060CA0 in src/matched/func_02060CA0.c
 *   declared as sub_02060CA0  in src/matched/sub_02030098.c
 */
extern void *func_02060CA0(void *arena, u32 size, s32 align);

void *sub_02060CA0(void *arena, u32 size, s32 align)
{
    return func_02060CA0(arena, size, align);
}

/*
 * 0x02030F04, the task-callback free-list pop.
 *
 *   defined  as func_02030F04 in src/matched/func_02030F04.c under ILP32,
 *                             and in stubs/reimpl_02030F04.c under LP64
 *   declared as sub_02030F04  in src/matched/sub_020311B8.c
 *
 * The forwarder is the same either way, which is the point of resolving the
 * pointer-width question by symbol rather than by call site.
 */
extern void *func_02030F04(void *list);

void *sub_02030F04(void *list)
{
    return func_02030F04(list);
}

#if EIDOS_HEAP_MATCHED

/*
 * The expanded heap, compiled as written under ILP32 (CMakeLists.txt,
 * EIDOS_HEAP_MATCHED).  Every forwarder below exists only in that
 * configuration: under LP64 the same names are the stand-ins in stubs/, and
 * the matched definitions are not in the build.  The casts are between a
 * pointer and a u32, which is exactly what the two matched declarations
 * disagree about and is lossless where these are compiled.
 */

/*
 * 0x02060C5C, create an expanded heap over a region.
 *
 *   defined  as func_02060C5C in src/matched/func_02060C5C.c
 *   declared as sub_02060C5C  in src/matched/sub_0202FC48.c
 */
extern void *func_02060C5C(void *start, u32 size, u32 attr);

void *sub_02060C5C(void *mem, u32 size, u32 mode)
{
    return func_02060C5C(mem, size, mode);
}

/*
 * 0x0202FED8, bind a region to a spare manager entry as a child heap.
 *
 *   defined  as func_0202FED8 in src/matched/func_0202FED8.c (all u32)
 *   declared as sub_0202FED8  in src/matched/sub_0202FDC4.c (res and bound
 *                             as pointers)
 */
extern int func_0202FED8(u32 a0, u32 a1, u32 a2, u32 a3);

int sub_0202FED8(void *res, u32 idx, void *bound, u32 param)
{
    return func_0202FED8((u32)(uintptr_t)res, idx, (u32)(uintptr_t)bound,
                         param);
}

/*
 * 0x02030164, the raw free.
 *
 *   defined  as func_02030164 in src/matched/func_02030164.c
 *   declared as sub_02030164  in src/matched/sub_020307B0.c
 */
extern u32 func_02030164(void *p);

void sub_02030164(void *p)
{
    (void)func_02030164(p);
}

/*
 * 0x02060E6C, the free-list total.
 *
 *   defined  as func_02060E6C in src/matched/func_02060E6C.c (int *, int)
 *   declared as sub_02060E6C  in src/matched/sub_02030344.c and
 *                             sub_02030958.c (void *, void *)
 */
extern int func_02060E6C(int *heap);

void *sub_02060E6C(void *arena)
{
    return (void *)(uintptr_t)(u32)func_02060E6C((int *)arena);
}

/*
 * 0x02060E94, the largest block the heap could allocate at an alignment.
 *
 *   defined  as func_02060E94 in src/matched/func_02060E94.c
 *   declared as sub_02060E94  in src/matched/sub_02030958.c
 */
extern s32 func_02060E94(void *heap, s32 align);

void *sub_02060E94(void *arena, u32 mode)
{
    return (void *)(uintptr_t)(u32)func_02060E94(arena, (s32)mode);
}

/*
 * 0x02060F90, an allocated block's size.
 *
 *   defined  as func_02060F90 in src/matched/func_02060F90.c
 *   declared as sub_02060F90  in src/matched/sub_020309B8.c
 */
extern int func_02060F90(char *p);

u32 sub_02060F90(void *block)
{
    return (u32)func_02060F90((char *)block);
}

#endif /* EIDOS_HEAP_MATCHED */
