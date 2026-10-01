/*
 * func_02047990 -- the 3D system's deferred buffer swap, run once per frame
 * from NitroMain's loop.  REIMPLEMENTATION, not a match.
 *
 * Why this is a port-side file
 *
 *   NitroMain (stubs/reimpl_02005124.c) calls it with `blx 0x02047990`, and
 *   it is the only callee of NitroMain aletheia has not matched: nothing
 *   else in the image branches to it, so the disassembler never traced it
 *   and asm/arm9/main.s carries 0x02047990..0x020479D7 as `.word` data,
 *   right after the matched func_02047970.  Decoded by hand (ARM):
 *
 *     02047990  E59F0038  ldr   r0, =g_02146A5C        (pool 0x020479D0)
 *     02047994  E5902000  ldr   r2, [r0]
 *     02047998  E3520000  cmp   r2, #0
 *     0204799C  012FFF1E  bxeq  lr                     no context: return
 *     020479A0  E5921094  ldr   r1, [r2, #0x94]
 *     020479A4  E3510001  cmp   r1, #1
 *     020479A8  112FFF1E  bxne  lr                     no request: return
 *     020479AC  E5923084  ldr   r3, [r2, #0x84]
 *     020479B0  E5922080  ldr   r2, [r2, #0x80]
 *     020479B4  E59F1018  ldr   r1, =0x04000540        (pool 0x020479D4)
 *     020479B8  E1822083  orr   r2, r2, r3, lsl #1
 *     020479BC  E5812000  str   r2, [r1]               SWAP_BUFFERS
 *     020479C0  E5900000  ldr   r0, [r0]
 *     020479C4  E3A01000  mov   r1, #0
 *     020479C8  E5801094  str   r1, [r0, #0x94]        request consumed
 *     020479CC  E12FFF1E  bx    lr
 *
 * What attests the fields
 *
 *   g_02146A5C is the 3D system context src/matched/arm9/func_02046440.c
 *   allocates (0xA0 bytes).  src/matched/arm9/func_02046BE0.c (EXACT) is
 *   the setter for the two words this reads: it stores its arguments to
 *   +0x80 and +0x84.  src/matched/arm9/func_02047970.c (EXACT) raises the
 *   request: it sets +0x94 to 1.  The store is the SDK's G3_SwapBuffers
 *   encoding, SWAP_BUFFERS (0x04000540) = sort mode | (buffer mode << 1),
 *   so +0x80 is the translucent-polygon sort mode and +0x84 the depth
 *   buffering mode.
 *
 *   The struct below has the DS layout, spelled the way func_02046BE0.c and
 *   func_02047970.c spell theirs (byte padding to the fixed offsets).  Under
 *   the reference ILP32 configuration it agrees with every matched file that
 *   reaches the context; on LP64 it agrees with those two, which is the
 *   same caveat they carry (README.md, "Pointer width").
 *
 * Port-side access
 *
 *   The register store goes through sdk/dsmem.h's bw_io_write32, so the
 *   write hook (the renderer's swap) runs at once, as the hardware latches
 *   the request at this store.
 */

#include "stubs.h"
#include "dsmem.h"

struct G3dSwapCtx {
    u8  pad_00[0x80];
    u32 sort_mode;      /* +0x80, func_02046BE0's first argument  */
    u32 buffer_mode;    /* +0x84, func_02046BE0's second argument */
    u8  pad_88[0x94 - 0x88];
    u32 swap_request;   /* +0x94, set to 1 by func_02047970        */
};

extern struct G3dSwapCtx *g_02146A5C;

void func_02047990(void);

#define G3_REG_SWAP_BUFFERS 0x04000540u

void func_02047990(void)
{
    struct G3dSwapCtx *ctx = g_02146A5C;

    if (ctx == NULL) {
        return;
    }
    if (ctx->swap_request != 1) {
        return;
    }
    bw_io_write32(G3_REG_SWAP_BUFFERS,
                  ctx->sort_mode | (ctx->buffer_mode << 1));
    g_02146A5C->swap_request = 0;
}
