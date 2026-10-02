/*
 * sub_0204D360 -- REIMPLEMENTATION of aletheia's EXACT src/matched/arm9/sub_0204D360.c,
 * for an argument the matched C passes in a register without naming it
 * (docs/boot.md, "Arguments in registers"; tools/regargs.py).
 *
 * One of the cell-actor handlers func_0204D324 dispatches through the
 * table at 0x020A1394 as handler(obj, arg).  It looks up the animation
 * sequence with sub_020627D8 (`struct AnimSequence *func_020627D8(struct
 * AnimBank *bank, u32 idx)`), and the index is the handler's own second
 * argument, untouched in r1 at the call (aletheia's asm/arm9/main.s at
 * 0x0204D360: `push; mov r4, r0; ldr r0, [r4, #...]; bl func_020627D8`).
 * The matched C names neither.  This file names the index.
 *
 * cmake/boot.cmake lists the matched file as replaced.
 *
 * symbols:
 *   sub_020627D8  0x020627D8  arm
 *   sub_0206434C  0x0206434C  arm
 */

typedef unsigned char u8;
typedef unsigned int  u32;

struct Rec {
    u8 unk_00[1];
};

struct Obj {
    u8         pad_00[8];
    void      *unk_08;
    struct Rec unk_0c;
};

extern void *sub_020627D8(void *h, u32 idx);
extern void  sub_0206434C(struct Rec *r, void *v);

void sub_0204D360(struct Obj *o, u32 idx)
{
    sub_0206434C(&o->unk_0c, sub_020627D8(o->unk_08, idx));
}
