/*
 * sdk/asm/MI_CpuMove.c -- MI_CpuMove at 0x02083194.
 *
 * Replaces src/matched/arm9_sdk/MI_CpuMove.c (an asm function): a copy
 * that tolerates overlap.  With nothing to do (size 0, or source equal to
 * destination) it returns; with the source above the destination it is
 * MI_CpuCopy (forward); otherwise it copies backward from the ends, bytes
 * then halfwords then words as alignment allows.  Every path leaves the
 * destination holding the source's original bytes, which is memmove.
 */

#include <string.h>

#include "asmsdk.h"

void MI_CpuMove(const void *src, void *dst, u32 size);

void MI_CpuMove(const void *src, void *dst, u32 size)
{
    if (size != 0u && src != dst) {
        memmove(dst, src, size);
    }
}
