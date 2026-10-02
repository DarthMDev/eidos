/*
 * sdk/asm/ov/ov009_func_0217EA9C.c -- gsiMemoryCallbacksSet at 0x0217EA9C
 * (ov009, the GameSpy layer of the network library).
 *
 * Replaces src/matched/ov_sdk/ov009_func_0217EA9C.c (an asm body); compiled
 * as part of ov009 under its names (tools/ovlink.py, docs/overlays.md).
 *
 *     ldr ip, =d_021A3730; stmia ip, {r0, r1, r2}; str r3, [ip, #0xc]; bx lr
 *
 * Stores the four allocator callbacks (malloc, free, realloc, memalign) into
 * the table at 0x021A3730 in ov009's data, where the rest of the layer reads
 * them.  The table's slots are the DS's 32-bit words, so the host pointers
 * fit them where pointers are four bytes, the configuration the boot runs
 * in (docs/armdata.md, "Pointer width").
 *
 * symbols:
 *   d_021A3730                   0x021A3730  data
 */

extern unsigned char d_021A3730[];

void gsiMemoryCallbacksSet(void *p_malloc, void *p_free, void *p_realloc, void *p_memalign);

void gsiMemoryCallbacksSet(void *p_malloc, void *p_free, void *p_realloc, void *p_memalign)
{
    void **table = (void **)(void *)d_021A3730;

    table[0] = p_malloc;
    table[1] = p_free;
    table[2] = p_realloc;
    table[3] = p_memalign;
}
