/*
 * sdk/asm/MI_CpuFill8.c -- MI_CpuFill8 at 0x02082BCC.
 *
 * Replaces src/matched/arm9_sdk/MI_CpuFill8.c (TwlSDK mi_memory.o, an asm
 * function in the vendor source).  void MI_CpuFill8(void *dstp, u8 data,
 * u32 size).
 *
 * The asm writes [dst, dst + size) with `data` without ever storing a single
 * byte (VRAM ignores byte writes):
 *
 *   size 0         return.
 *   dst odd        ldrh [dst-1], keep its low byte, strh with data on top.
 *   size >= 2      data |= data << 8; one strh if dst is not word aligned;
 *                  data |= data << 16; str for size & ~3; strh if size & 2.
 *   size odd       ldrh [dst], keep the high byte, strh with data below.
 *
 * Each byte it changes ends up equal to `data`, and the bytes it reads and
 * writes back are left as they were.  With `data` a u8 (the SDK prototype,
 * and what a caller compiled against it passes) that is exactly memset.
 * A caller that declares the parameter wider (some matched files say u32
 * or int) and passes a value above 0xFF would see the asm spread the high
 * bits into the halfword and word stores; the host takes the low byte, which
 * is the only value an SDK-conforming caller can pass.
 */

#include <string.h>

#include "asmsdk.h"

void MI_CpuFill8(void *dstp, u8 data, u32 size)
{
    if (size == 0) {
        return;
    }
    memset(dstp, (int)data, (size_t)size);
}

void func_02082BCC(void *dstp, u8 data, u32 size)
{
    MI_CpuFill8(dstp, data, size);
}
