/*
 * function: func_021B614C
 * address:  0x021B614C
 * region:   ov179
 * isa:      thumb
 * replaces: src/matched/ov/ov179_func_021B614C.c
 *
 * symbols:
 *   func_020315D4  0x020315D4  arm
 *   func_020306F0  0x020306F0  arm
 *
 * REIMPLEMENTATION of aletheia's EXACT src/matched/ov/ov179_func_021B614C.c
 * (the opening cinematic's proc end callback), for an argument the matched
 * C forwards in r0.  tools/ovlink.py compiles it in that file's place.
 *
 * The matched source is
 *
 *     int func_021B614C(void)
 *     {
 *         func_020315D4();
 *         func_020306F0(0x7f);
 *         return 1;
 *     }
 *
 * The proc runner (src/matched/arm9/func_020315F0.c) calls an end callback
 * as end(proc, &subseq, pwork, work), and func_020315D4 is sub_020315D4
 * (src/matched/arm9/sub_020315D4.c, `void sub_020315D4(struct S020315D4
 * *p)`, which frees p->field14, the proc's work): on the DS the proc is
 * still in r0 at the call (aletheia's asm/overlays/ov179.s, 0x021B614C:
 * `push {r3, lr}; blx 0x020315D4`).  A host passes nothing and the free
 * read an indeterminate pointer.  This file passes the proc.
 */

extern void func_020315D4(void *proc);
extern void func_020306F0(int heap);

int func_021B614C(void *proc)
{
    func_020315D4(proc);
    func_020306F0(0x7f);
    return 1;
}
