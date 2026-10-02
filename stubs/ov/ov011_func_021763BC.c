/*
 * function: func_021763BC
 * address:  0x021763BC
 * region:   ov011
 * isa:      thumb
 *
 * PORT STAND-IN for an overlay function aletheia has not matched yet
 * (stubs/ov/ov009_func_02157138.c says how these are compiled and retired).
 * Written from aletheia's asm/overlays/ov011.s:
 *
 *     021763BC  push  {r4, r5, r6, lr}
 *     021763BE  movs  r4, #0
 *     021763C0  adds  r5, r0, #0
 *     021763C2  adds  r6, r4, #0
 *   loop:
 *     021763C4  adds  r0, r5, r4
 *     021763C6  strb  r6, [r0, #6]
 *     021763C8  adds  r0, r5, #0
 *     021763CA  adds  r1, r4, #0
 *     021763CC  bl    func_021763D8
 *     021763D0  adds  r4, r4, #1
 *     021763D2  cmp   r4, #6
 *     021763D4  blt   loop
 *     021763D6  pop   {r4, r5, r6, pc}
 *
 * Six one-byte slots at +6 cleared, each followed by func_021763D8(p, i)
 * (matched, src/matched/ov/ov011_func_021763D8.c).  Reached from the save
 * layout (func_0200BDE4, a block's init) with ov011 resident.
 */

typedef unsigned char u8;

extern void func_021763D8(void *p, int i);

void func_021763BC(void *p)
{
    u8 *b = (u8 *)p;
    int i;

    for (i = 0; i < 6; i++) {
        b[6 + i] = 0;
        func_021763D8(p, i);
    }
}
