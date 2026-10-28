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
 *
 * ---------------------------------------------------------------------------
 * WAVE 2 RESYNC (and the generator this file still wants)
 * ---------------------------------------------------------------------------
 *
 * aletheia's cleanup waves renamed the *definitions* in the matched files to
 * descriptive names (func_02060CA0 -> NNS_FndAllocFromExpHeapEx,
 * sub_02034734 -> SAVEDATA_PanicFailPrintf, func_0202FED8 ->
 * GFL_HEAP_AddRecord, ...).  The callers in the matched corpus were renamed
 * with them; the forwarders below, which named the *definition* by its old
 * address spelling, were not, so each one's body called a name nothing
 * defines any more.  Every extern and call here was resynced to the name the
 * manifest (layout/matched/, the shard `functions`/`addresses` rows) now
 * gives the defining address.
 *
 * This is exactly the drift the file's header warns about, and it is why the
 * list should be *generated* rather than hand-kept.  The design, for the lane
 * that owns tools/: for every matched source the build compiles, walk its
 * `references()` (tools/closure.py's SourceScan); for each referenced name
 * classified `alias`, emit
 *
 *     <decl-of-the-definition>  <referenced-name>(<params>)
 *     { return <defined-name>(<args>); }
 *
 * keyed on the address so a rename on either side is picked up at the next
 * configure.  The signature is the only hard part: it comes from the
 * defining file's own prototype (closure.SourceScan keeps `heads`/`declared`),
 * and the variadic case (SAVEDATA_PanicFailPrintf) needs the tail dropped as
 * below.  Until that generator exists, this file is the manual form of it.
 */

#include "port.h"

/*
 * 0x02034734, the assertion sink.
 *
 *   defined  as SAVEDATA_PanicFailPrintf in src/matched/sub_02034734.c
 *   declared as func_02034734 in src/matched/func_02049578.c
 *                              and src/matched/func_0204944C.c
 *
 * The variadic tail is dropped rather than forwarded: SAVEDATA_PanicFailPrintf's
 * body reads only the handler out of g_Sink and ignores every argument, so
 * there is nothing to forward, and C has no portable way to pass `...` on.
 */
extern void SAVEDATA_PanicFailPrintf(const char *expr, u32 line, const char *file, ...);

void func_02034734(const char *expr, u32 line, const char *file, ...)
{
    SAVEDATA_PanicFailPrintf(expr, line, file);
}

/*
 * 0x02060CA0, the expanded heap's allocation front door.
 *
 *   defined  as NNS_FndAllocFromExpHeapEx in src/matched/func_02060CA0.c
 *   declared as sub_02060CA0  in src/matched/sub_02030098.c
 */
extern void *NNS_FndAllocFromExpHeapEx(void *heap, u32 size, s32 align);

void *sub_02060CA0(void *arena, u32 size, s32 align)
{
    return NNS_FndAllocFromExpHeapEx(arena, size, align);
}

/*
 * 0x02030F04, the task-callback free-list pop.
 *
 *   defined  as TCBL_PopFreeTask in src/matched/func_02030F04.c under ILP32,
 *                             and as func_02030F04 in stubs/reimpl_02030F04.c
 *                             under LP64
 *   declared as sub_02030F04  in src/matched/sub_020311B8.c
 *
 * The forwarder is the same either way, which is the point of resolving the
 * pointer-width question by symbol rather than by call site.
 */
#if EIDOS_TCBL_MATCHED
extern void *TCBL_PopFreeTask(void *list);
#else
extern void *func_02030F04(void *list);
#endif

void *sub_02030F04(void *list)
{
#if EIDOS_TCBL_MATCHED
    return TCBL_PopFreeTask(list);
#else
    return func_02030F04(list);
#endif
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
 *   defined  as NNS_FndCreateExpHeapEx in src/matched/func_02060C5C.c
 *   declared as sub_02060C5C  in src/matched/sub_0202FC48.c
 */
extern void *NNS_FndCreateExpHeapEx(void *start, u32 size, u32 attr);

void *sub_02060C5C(void *mem, u32 size, u32 mode)
{
    return NNS_FndCreateExpHeapEx(mem, size, mode);
}

/*
 * 0x0202FED8, bind a region to a spare manager entry as a child heap.
 *
 *   defined  as GFL_HEAP_AddRecord in src/matched/func_0202FED8.c (all u32)
 *   declared as sub_0202FED8  in src/matched/sub_0202FDC4.c (res and bound
 *                             as pointers)
 */
extern int GFL_HEAP_AddRecord(u32 a0, u32 a1, u32 a2, u32 a3);

int sub_0202FED8(void *res, u32 idx, void *bound, u32 param)
{
    return GFL_HEAP_AddRecord((u32)(uintptr_t)res, idx,
                              (u32)(uintptr_t)bound, param);
}

/*
 * 0x02030164, the raw free.
 *
 *   defined  as GFL_HEAP_FreeMemory in src/matched/func_02030164.c
 *   declared as sub_02030164  in src/matched/sub_020307B0.c
 */
extern u32 GFL_HEAP_FreeMemory(void *p);

void sub_02030164(void *p)
{
    (void)GFL_HEAP_FreeMemory(p);
}

/*
 * 0x02060E6C, the free-list total.
 *
 *   defined  as NNSi_FndGetListTotalSize in src/matched/func_02060E6C.c
 *   declared as sub_02060E6C  in src/matched/sub_02030344.c and
 *                             sub_02030958.c
 */
extern int NNSi_FndGetListTotalSize(int *heap);

void *sub_02060E6C(void *arena)
{
    return (void *)(uintptr_t)(u32)NNSi_FndGetListTotalSize((int *)arena);
}

/*
 * 0x02060E94, the largest block the heap could allocate at an alignment.
 *
 *   defined  as NNSi_FndGetListAllocatableSize in src/matched/func_02060E94.c
 *   declared as sub_02060E94  in src/matched/sub_02030958.c
 */
extern s32 NNSi_FndGetListAllocatableSize(void *heap, s32 align);

void *sub_02060E94(void *arena, u32 mode)
{
    return (void *)(uintptr_t)(u32)NNSi_FndGetListAllocatableSize(arena,
                                                                  (s32)mode);
}

/*
 * 0x02060F90, an allocated block's size.
 *
 *   defined  as NNSi_FndGetMBlockSize in src/matched/func_02060F90.c
 *   declared as sub_02060F90  in src/matched/sub_020309B8.c
 */
extern int NNSi_FndGetMBlockSize(char *p);

u32 sub_02060F90(void *block)
{
    return (u32)NNSi_FndGetMBlockSize((char *)block);
}

#endif /* EIDOS_HEAP_MATCHED */

#if !EIDOS_TCBL_MATCHED
/*
 * The task-callback list under LP64: the matched callers use aletheia's
 * names for the nine pointer-width-dependent functions, and the stand-ins
 * in stubs/reimpl_02030*.c keep the address names.  Each name forwards to
 * its stand-in.
 */
extern void *sub_02030EF0(void *list, u32 idx);
extern void sub_02030F34(void *list, void *node);
extern void sub_02030F50(void *list, void *node);
extern void sub_02030F70(void *node);
extern void *sub_02030FB4(void *node);

void *TCBL_GetTaskSlot(void *list, u32 idx);
void TCBL_PushFreeTask(void *list, void *node);
void TCBL_InitTaskLinks(void *list, void *node);
void TCBL_FreeTask(void *node);
void *TCBL_GetTaskWork(void *node);

void *TCBL_GetTaskSlot(void *list, u32 idx) { return sub_02030EF0(list, idx); }
void TCBL_PushFreeTask(void *list, void *node) { sub_02030F34(list, node); }
void TCBL_InitTaskLinks(void *list, void *node) { sub_02030F50(list, node); }
void TCBL_FreeTask(void *node) { sub_02030F70(node); }
void *TCBL_GetTaskWork(void *node) { return sub_02030FB4(node); }
#endif /* !EIDOS_TCBL_MATCHED */

/*
 * 0x020307B0, the heap's release entry: the matched GFL_HEAP_Free
 * (src/matched/arm9_sdk/sub_020307B0.c), by the address name the LP64
 * free stand-in and the overlay sets without the boot call it by.
 */
extern u32 GFL_HEAP_Free(void *p);
void sub_020307B0(void *block);

void sub_020307B0(void *block)
{
    (void)GFL_HEAP_Free(block);
}

#if !EIDOS_HEAP_MATCHED
/*
 * The expanded heap's two free-list walks under LP64: the matched front
 * door (src/matched/func_02060CA0.c) calls them by aletheia's names, the
 * stand-ins (stubs/reimpl_02060A08.c, reimpl_02060ABC.c) keep the address
 * names.
 */
extern void *func_02060A08(void *heap, u32 size, s32 align);
extern void *func_02060ABC(void *heap, u32 size, s32 align);

void *NNSi_FndAllocFromExpHeapHead(void *heap, u32 size, s32 align);
void *NNSi_FndAllocFromExpHeapTail(void *heap, u32 size, s32 align);

void *NNSi_FndAllocFromExpHeapHead(void *heap, u32 size, s32 align)
{
    return func_02060A08(heap, size, align);
}

void *NNSi_FndAllocFromExpHeapTail(void *heap, u32 size, s32 align)
{
    return func_02060ABC(heap, size, align);
}
#endif /* !EIDOS_HEAP_MATCHED */
