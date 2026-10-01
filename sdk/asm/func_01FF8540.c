/*
 * sdk/asm/func_01FF8540.c -- OSi_ReloadRomData at 0x01FF8540 (ITCM).
 *
 * Replaces src/matched/itcm_sdk/func_01FF8540.c (TwlSDK os_reset.o, an asm
 * function), the reload step of OS_ResetSystem:
 *
 *   void OSi_ReloadRomData(BOOL ontwl)
 *
 *   shakeHand01 (ontwl only):
 *     strh 1 -> HW_BOOT_SYNC_PHASE (0x02FFFC28)
 *     r2 = ldrh SHAKEHAND_7 (0x02FFFC26); r3 = ldrh SHAKEHAND_9 (0x02FFFC24)
 *     do { r3++; strh r3 -> SHAKEHAND_9 } while (ldrh SHAKEHAND_7 == r2)
 *     r3++; strh r3 -> SHAKEHAND_9
 *   offset = ontwl ? 0 : ldr HW_ROM_BASE_OFFSET_BUF (0x02FFFC2C); push it
 *   if (offset >= 0x8000, unsigned): read 0x160 header bytes from ROM
 *       offset `offset` to HW_ROM_HEADER_BUF (0x02FFFE00)
 *   ARM9 static: src = hdr[0x20] + offset, dst = hdr[0x28], len = hdr[0x2C];
 *       subs r3, src, #0x8000; if lt (signed): src = 0x8000, dst -= r3,
 *       len += r3 (skip the secure area); if len > 0 (signed) read it
 *   ARM7 static: src = hdr[0x30] + offset, dst = hdr[0x38], len = hdr[0x3C];
 *       if len > 0 read it (no secure-area clamp)
 *   ltd statics (ontwl only), from the TWL header at 0x02FFE000:
 *       src = [0x1C0] + 0x4000, dst = [0x1C8] + 0x4000, len = [0x1CC] - 0x4000,
 *       same clamp, read if len > 0; then the same for [0x1D0], [0x1D8], [0x1DC]
 *   arrangeCache: clean every D-cache set/way, invalidate both caches,
 *       drain the write buffer (CP15)
 *   shakeHand02 (ontwl only): as shakeHand01 with phase 2
 *
 * Every read is func_01FF8700 (OSi_ReadCardRom32, matched C,
 * void (u32 src, void *dst, int len)); the destinations are DS addresses
 * from the headers, so they go through the address space.  The CP15 block
 * is a no-op on the coherent host.  The handshake spins on halfwords the
 * ARM7 writes, so they are volatile reads; on the host the ARM7 layer (or
 * another host thread) must answer, or the NITRO path (ontwl FALSE, which
 * skips both handshakes) must be the one taken.  The comparisons keep the
 * asm's signedness: `cs` unsigned for the header, `lt` and `gt` signed for
 * the clamp and the lengths.  The caller (func_01FF8414.c) uses the SDK
 * name; the matched spelling forwards.
 */

#include "asmsdk.h"
#include "../dsmem.h"

extern void func_01FF8700(u32 src, void *dst, int len);

static u32 bw_reset_rd32(u32 addr)
{
    return *(const volatile u32 *)bw_ds_ptr(addr);
}

static void bw_boot_shake_hand(u16 phase)
{
    volatile u16 *sync = (volatile u16 *)bw_ds_ptr(0x02FFFC28u);
    volatile u16 *hand9 = (volatile u16 *)bw_ds_ptr(0x02FFFC24u);
    volatile u16 *hand7 = (volatile u16 *)bw_ds_ptr(0x02FFFC26u);
    u32 seen7;
    u32 count9;

    *sync = phase;
    seen7 = *hand7;
    count9 = *hand9;
    do {
        count9++;
        *hand9 = (u16)count9;
    } while (*hand7 == seen7);
    count9++;
    *hand9 = (u16)count9;
}

/* Clamp a segment that starts inside the secure area, then read it. */
static void bw_reload_segment(u32 src, u32 dst, u32 len, int clamp)
{
    if (clamp && (s32)src < 0x8000) {
        u32 under = src - 0x8000u;   /* r3, negative as s32 */

        src = 0x8000u;
        dst -= under;
        len += under;
    }
    if ((s32)len > 0) {
        func_01FF8700(src, bw_ds_ptr(dst), (int)len);
    }
}

void OSi_ReloadRomData(int ontwl)
{
    u32 offset;

    if (ontwl != 0) {
        bw_boot_shake_hand(1);
    }

    offset = ontwl != 0 ? 0u : bw_reset_rd32(0x02FFFC2Cu);
    if (offset >= 0x8000u) {
        func_01FF8700(offset, bw_ds_ptr(0x02FFFE00u), 0x160);
    }

    bw_reload_segment(bw_reset_rd32(0x02FFFE20u) + offset, bw_reset_rd32(0x02FFFE28u),
                      bw_reset_rd32(0x02FFFE2Cu), 1);
    bw_reload_segment(bw_reset_rd32(0x02FFFE30u) + offset, bw_reset_rd32(0x02FFFE38u),
                      bw_reset_rd32(0x02FFFE3Cu), 0);

    if (ontwl != 0) {
        bw_reload_segment(bw_reset_rd32(0x02FFE1C0u) + 0x4000u, bw_reset_rd32(0x02FFE1C8u) + 0x4000u,
                          bw_reset_rd32(0x02FFE1CCu) - 0x4000u, 1);
        bw_reload_segment(bw_reset_rd32(0x02FFE1D0u) + 0x4000u, bw_reset_rd32(0x02FFE1D8u) + 0x4000u,
                          bw_reset_rd32(0x02FFE1DCu) - 0x4000u, 1);
    }

    /* arrangeCache: CP15 clean/invalidate and drain, nothing on the host. */

    if (ontwl != 0) {
        bw_boot_shake_hand(2);
    }
}

void func_01FF8540(BOOL ontwl)
{
    OSi_ReloadRomData((int)ontwl);
}
