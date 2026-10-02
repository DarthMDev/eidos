/*
 * function: func_0206127C, func_0206128C, func_020612A0, func_020612B0,
 *           func_020612C4
 * isa:      arm
 *
 * The heap allocator objects' functions, which aletheia has not found yet:
 * five ARM functions between func_0206123C and func_020612C8 that the
 * listing shows as .word runs, reached only through the allocator tables in
 * the ARM9 data (0x020A1704 holds 0x020612C4, 0x0206128C, 0x020612A0, ...).
 * An allocator is { table, heap, alignment } (the SDK's NNSFndAllocator);
 * these are its alloc and free for an expanded heap and a frame heap.  Port
 * answers until aletheia matches them; a matched definition of one of
 * these names makes the boot's configure step report the clash
 * (cmake/boot.cmake, "NOT EXCLUDED"), which is the signal to delete it
 * here.  The `function:` line above is how tools/armdata.py learns the
 * addresses, so the run-time scan relocates the table words to them
 * (docs/armdata.md, "Code pointers the metadata does not list").
 *
 * Each one, from aletheia's asm/arm9/main.s (the words decoded):
 *
 *   0x0206127C  ldr r2, [r0, #0x24]; str r2, [r1]; str r1, [r0, #0x24];
 *               bx lr                    push `node` on the list at +0x24
 *   0x0206128C  ldr ip, =func_02060CA0; mov r2, r0; ldmib r2, {r0, r2};
 *               bx ip                    func_02060CA0(a->heap, size, a->align)
 *   0x020612A0  ldr ip, =func_02060E30; ldr r0, [r0, #4]; bx ip
 *                                        func_02060E30(a->heap, block)
 *   0x020612B0  ldr ip, =func_02061124; mov r2, r0; ldmib r2, {r0, r2};
 *               bx ip                    func_02061124(a->heap, size, a->align)
 *   0x020612C4  bx lr                    a frame heap frees nothing
 */

#include "stubs.h"

struct BwAllocator {
    const void *table;
    void       *heap;
    s32         align;
};

void *func_02060CA0(void *heap, u32 size, s32 align);
void  func_02060E30(void *heap, void *block);
void *func_02061124(char *heap, int size, int align);

void  func_0206127C(void *list, void *node);
void *func_0206128C(struct BwAllocator *a, u32 size);
void  func_020612A0(struct BwAllocator *a, void *block);
void *func_020612B0(struct BwAllocator *a, u32 size);
void  func_020612C4(struct BwAllocator *a, void *block);

void func_0206127C(void *list, void *node)
{
    u8 *l = (u8 *)list;

    *(void **)node = *(void **)(l + 0x24);
    *(void **)(l + 0x24) = node;
}

void *func_0206128C(struct BwAllocator *a, u32 size)
{
    return func_02060CA0(a->heap, size, a->align);
}

void func_020612A0(struct BwAllocator *a, void *block)
{
    func_02060E30(a->heap, block);
}

void *func_020612B0(struct BwAllocator *a, u32 size)
{
    return func_02061124((char *)a->heap, (int)size, (int)a->align);
}

void func_020612C4(struct BwAllocator *a, void *block)
{
    (void)a;
    (void)block;
}
