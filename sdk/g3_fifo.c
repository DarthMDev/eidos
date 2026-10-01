/*
 * sdk/g3_fifo.c -- the GX library's FIFO senders, onto engine/g3d/hw3d.c.
 *
 * Why these are shims: all three are hand-written ARM in the SDK (gxasm.o
 * and g3x.o), `asm` functions in aletheia, so there is nothing to compile
 * for a host; and their whole job is a burst of stores to the geometry
 * FIFO, which plain-memory registers would collapse into one
 * (engine/g3d/hw3d.c, "How a store reaches the FIFO").
 *
 *   GX_SendFifo48B   0x02080C84   twelve words from src: G3_LoadMtx43 and
 *                                 G3_MultMtx43 send their matrix with it
 *   GX_SendFifo64B   0x02080CA8   sixteen words: G3_LoadMtx44, G3_MultMtx44
 *   GXi_NopClearFifo128_ 0x0207DECC  128 zero words: G3X_ClearFifo
 *
 * The two senders take the destination in r1 and the matched callers
 * (src/matched/arm9_sdk/g3_loadmtx43.c and friends) declare them with the
 * source alone, because the ARM tail call leaves 0x04000400 in r1 from the
 * opcode store just before it.  So the shims take the source alone too and
 * always send to the FIFO, which is the only destination the SDK ever gives
 * them.  GXi_NopClearFifo128_ takes the destination explicitly and checks
 * it.  Both spellings are defined, as sdk/gx.c does.
 */

#include "port.h"
#include "hw3d.h"

void GX_SendFifo48B(const void *src)
{
    g3d_hw_fifo_send(src, 48u);
}

void func_02080C84(const void *src)
{
    GX_SendFifo48B(src);
}

void GX_SendFifo64B(const void *src)
{
    g3d_hw_fifo_send(src, 64u);
}

void func_02080CA8(const void *src)
{
    GX_SendFifo64B(src);
}

void GXi_NopClearFifo128_(void *dst)
{
    static const u8 zeros[128u * 4u] = { 0 };

    if (!g3d_hw_is_fifo(dst)) {
        bw_panic("GXi_NopClearFifo128_", 0x0207DECCu,
                 "destination is not the geometry FIFO");
    }
    g3d_hw_claim(dst);
    g3d_hw_fifo_send(zeros, (u32)sizeof(zeros));
}

void func_0207DECC(void *dst)
{
    GXi_NopClearFifo128_(dst);
}
