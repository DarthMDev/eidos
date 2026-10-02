/*
 * function: func_02009ED4, func_02024934, func_0200E090, func_0200EA14,
 *           func_0200EA2C, func_0200ED38, func_0200ED3C,
 *           bw_hidden_ov010_0216E474, bw_hidden_ov010_0216E47C
 * isa:      thumb
 *
 * Save-block functions aletheia has not found yet: the size and clear
 * callbacks of seven save blocks, which the game reaches only through the
 * save block table (0x0209DA28, 69 entries of {id, size, init}, which
 * func_02033A88 walks while it lays the save out).  Neither the listing
 * nor the manifests label them: aletheia's disassembler took each for data
 * in a literal pool or a run of words, so no matched source exists, and a
 * code pointer to them is not listed either.  These are port answers until
 * aletheia matches them; each one's bytes are below, read from the player's
 * arm9_dec.bin (the ov010 two from extract/overlays_dec/y9_0010.bin, which
 * aletheia's asm/overlays/ov010.s shows as .word at those addresses).  When
 * a matched file defines one of these names, the boot's configure step
 * reports the clash (cmake/boot.cmake, "NOT EXCLUDED"), which is the signal
 * to delete it here.
 *
 * The `function:` header line above is how tools/armdata.py learns that
 * these addresses are Thumb functions this build defines, so the run-time
 * scan for code pointers relocates the table's words to them
 * (docs/armdata.md, "Code pointers the metadata does not list").
 *
 *   table  entry  address     Thumb code                      meaning
 *     34   size   0x02009ED4  ldr r0,[pc]; bx lr; .word 0xA94  return 0xA94
 *     39   size   0x02024934  movs r0,#0xA8; bx lr             return 0xA8
 *     61   size   0x0200E090  movs r0,#0x85; lsls r0,#4; bx lr return 0x850
 *     66   size   0x0200EA14  movs r0,#0x5B; lsls r0,#2; bx lr return 0x16C
 *     66   init   0x0200EA2C  movs r2,#0x5B; ldr r3,=MI_CpuFill8;
 *                             movs r1,#0; lsls r2,#2; bx r3    fill 0x16C with 0
 *     68   size   0x0200ED38  movs r0,#0xFC; bx lr             return 0xFC
 *     68   init   0x0200ED3C  ldr r3,=MI_CpuFill8; movs r1,#0;
 *                             movs r2,#0xFC; bx r3             fill 0xFC with 0
 *     51   size   0x0216E474  movs r0,#0x5A; lsls r0,#2; bx lr return 0x168 (ov010)
 *     51   init   0x0216E47C  adds r1,r0,#0; movs r0,#0;
 *                             movs r2,#0x5A; ldr r3,=MIi_CpuClear32;
 *                             mvns r0,r0; lsls r2,#2; bx r3    fill 0x168 with ~0
 *
 * The literal-pool words are 0x02082BCC (MI_CpuFill8) and 0x02082A7C
 * (MIi_CpuClear32), layout/symbols_sdk.txt.  Entry 51's pair lies in
 * ov010's window: on the DS the table points into whichever overlay is
 * resident there, which is ov010 whenever the save is laid out, so those
 * two check that and panic otherwise.
 */

#include "stubs.h"
#include "../sdk/port.h"
#include "../sdk/overlay_image.h"

void MI_CpuFill8(void *dest, u8 data, u32 size);
void MIi_CpuClear32(u32 data, void *dest, u32 size);

u32  func_02009ED4(void);
u32  func_02024934(void);
u32  func_0200E090(void);
u32  func_0200EA14(void);
void func_0200EA2C(void *block);
u32  func_0200ED38(void);
void func_0200ED3C(void *block);
u32  bw_hidden_ov010_0216E474(void);
void bw_hidden_ov010_0216E47C(void *block);

u32 func_02009ED4(void)
{
    return 0xA94u;
}

u32 func_02024934(void)
{
    return 0xA8u;
}

u32 func_0200E090(void)
{
    return 0x85u << 4;
}

u32 func_0200EA14(void)
{
    return 0x5Bu << 2;
}

void func_0200EA2C(void *block)
{
    MI_CpuFill8(block, 0, 0x5Bu << 2);
}

u32 func_0200ED38(void)
{
    return 0xFCu;
}

void func_0200ED3C(void *block)
{
    MI_CpuFill8(block, 0, 0xFCu);
}

static void need_ov010(u32 addr)
{
    if (bw_ovl_resident_at(addr) != 10) {
        bw_panic("save block table", addr,
                 "entry 51's function lies in ov010's window, and ov010 is not "
                 "resident (stubs/hidden_save_blocks.c)");
    }
}

u32 bw_hidden_ov010_0216E474(void)
{
    need_ov010(0x0216E474u);
    return 0x5Au << 2;
}

void bw_hidden_ov010_0216E47C(void *block)
{
    need_ov010(0x0216E47Cu);
    MIi_CpuClear32(~0u, block, 0x5Au << 2);
}
