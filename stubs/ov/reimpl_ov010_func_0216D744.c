/*
 * function: FLDMMDL_CallDrawInitProc
 * address:  0x0216D744
 * packet:   pkt_0216D744
 * region:   ov010
 * compiler: dsi/1.1p1
 * flags:    -O4,p
 * lang:     c99
 * isa:      thumb
 * replaces: src/matched/ov/func_0216D744.c
 * Port reimplementation for one call: the matched C calls the draw-init
 * procedure with no arguments.  On the DS the procedure finds the field
 * model in r0, where FLDMMDL_CallDrawInitProc's own argument still is; on a
 * host it reads whatever is on the stack (the field map wrote through a
 * code address in FLDMMDL_DRAW_InitDirAnimeB).  Here r0 and r1 are passed
 * as the DS has them; everything else is the matched function.  This file
 * goes when the port compiles src/game (docs/game-tree.md).
 *
 * original status:   EXACT*
 *
 * symbols:
 *   (none: no relocation site)
 */
/* aletheia's src/matched/include/Obj00113.h; only f0 is read. */
struct Obj00113 {
    void * f0;
};

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;



int FLDMMDL_CallDrawInitProc(int a0, int a1)
{

a1 = a0 + 0;
a1 = a1 + 0x90;
a1 = *(int *)&((struct Obj00113 *)((char *)a1))->f0;
a1 = *(int *)&((struct Obj00113 *)((char *)a1))->f0;
    if (a1 == 0) goto loc_0216D754;
loc_0216D752:
    a0 = ((int (*)(int, int))a1)(a0, a1);
loc_0216D754:
    return a0;}
