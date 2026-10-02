/*
 * function: func_02161D98
 * address:  0x02161D98
 * region:   ov010
 * isa:      thumb
 *
 * PORT STAND-IN for an overlay function aletheia has not matched yet
 * (stubs/ov/ov009_func_02157138.c says how these are compiled and retired).
 * Written from aletheia's asm/overlays/ov010.s:
 *
 *     02161D98  push  {r3-r7, lr}
 *     02161D9A  movs  r4, #0
 *     02161D9C  movs  r1, #2
 *     02161D9E  adds  r6, r0, #0
 *     02161DA0  adds  r7, r0, #0
 *     02161DA2  strh  r4, [r0]
 *     02161DA4  strh  r1, [r0, #2]
 *     02161DA6  adds  r6, #8
 *     02161DA8  adds  r7, #0xf8
 *   loop:
 *     02161DAA  movs  r0, #0x18
 *     02161DAC  adds  r5, r4, #0
 *     02161DAE  muls  r5, r0, r5
 *     02161DB0  adds  r0, r6, r5
 *     02161DB2  bl    func_02161E78
 *     02161DB6  adds  r0, r7, r5
 *     02161DB8  bl    func_02161E78
 *     02161DBC  adds  r4, r4, #1
 *     02161DBE  cmp   r4, #0xa
 *     02161DC0  blt   loop
 *     02161DC2  pop   {r3-r7, pc}
 *
 * The init callback of save block 43 (func_0200A848 calls it): a two-u16
 * header {0, 2}, then two tables of ten 0x18-byte records, at +0x08 and
 * +0xF8, each record cleared by func_02161E78 (matched,
 * src/matched/ov/func_02161E78.c).
 */

typedef unsigned char  u8;
typedef unsigned short u16;

extern void func_02161E78(void *record);

void func_02161D98(void *p)
{
    u8 *b = (u8 *)p;
    int i;

    *(u16 *)(b + 0) = 0;
    *(u16 *)(b + 2) = 2;
    for (i = 0; i < 10; i++) {
        func_02161E78(b + 0x08 + i * 0x18);
        func_02161E78(b + 0xF8 + i * 0x18);
    }
}
