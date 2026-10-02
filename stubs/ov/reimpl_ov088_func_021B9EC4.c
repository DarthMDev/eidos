/*
 * function: func_021B9EC4
 * address:  0x021B9EC4
 * region:   ov088
 * isa:      thumb
 * replaces: src/matched/ov/ov088_func_021B9EC4.c
 *
 * symbols:
 *   func_020490F4  0x020490F4  arm
 *   func_0204A48C  0x0204A48C  arm
 *   func_0204B100  0x0204B100  arm
 *   func_0204B270  0x0204B270  arm
 *   func_0204A6C8  0x0204A6C8  arm
 *   func_0204ABF0  0x0204ABF0  arm
 *   func_0204AF28  0x0204AF28  arm
 *   func_0204B294  0x0204B294  arm
 *   func_0204B92C  0x0204B92C  arm
 *   func_02043598  0x02043598  arm
 *   func_02049238  0x02049238  arm
 *   d_020A1448     0x020A1448  data  a4
 *   d_021BD508     0x021BD508  data  ov088  a4
 *
 * REIMPLEMENTATION of aletheia's EXACT src/matched/ov/ov088_func_021B9EC4.c
 * (the title screen's sprite set-up), for a value the matched C hands on in
 * r0.  tools/ovlink.py compiles it in that file's place.
 *
 * The matched source has
 *
 *     p[0] = func_0204B100(0x40, 0, heap);
 *     func_0204B270();
 *
 * and func_0204B270 is sub_0204B270(struct Bank *bank) (declared by
 * src/matched/arm9/sub_0204B100.c, which calls it the same way with the
 * bank it built): on the DS the bank func_0204B100 returned is still in r0
 * at the second call (aletheia's asm/overlays/ov088.s, 0x021B9EE6-0x021B9EEC:
 * `blx 0x0204B100; str r0, [r5]; blx 0x0204B270`).  A host passes nothing
 * and the callee wrote through NULL + 0x48.  This file passes the bank;
 * everything else is the matched C.
 */

typedef struct {
    unsigned short a;
    unsigned short b;
    unsigned short c;
    unsigned char d;
    unsigned char e;
} Local;

extern char d_020A1448[];
extern char d_021BD508[];

extern int func_020490F4(int, int);
extern void func_0204A48C(void *, void *, int);
extern int func_0204B100(int, int, int);
extern void func_0204B270(int bank);
extern int func_0204A6C8(int, int, int, int, int);
extern int func_0204ABF0(int, int, int, int, int);
extern int func_0204AF28(int, int, int, int);
extern int func_0204B294(int, int, int, int, Local *, int, int);
extern void func_0204B92C(int, int);
extern void func_02043598(int, int);
extern void func_02049238(int);

void func_021B9EC4(int *p, int heap)
{
    Local s;
    int h;
    h = func_020490F4(0x1a, heap);
    func_0204A48C(d_020A1448, d_021BD508, heap);
    p[0] = func_0204B100(0x40, 0, heap);
    func_0204B270(p[0]);
    p[12] = func_0204A6C8(h, 5, 1, 1, heap);
    p[11] = func_0204ABF0(h, 4, 1, 0, heap);
    p[13] = func_0204AF28(h, 6, 7, heap);
    s.a = 0x80;
    s.b = 0x60;
    s.c = 0;
    s.e = 0;
    s.d = 0;
    p[1] = func_0204B294(p[0], p[12], p[11], p[13], &s, 1, heap);
    func_0204B92C(p[1], 1);
    func_02043598(0x10, 1);
    func_02049238(h);
}
