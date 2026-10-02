/*
 * func_0206434C (ENV_SetResourceSet) -- REIMPLEMENTATION of aletheia's EXACT src/matched/arm9_sdk/env_setresourceset.c,
 * for an argument the matched C passes in a register without naming it
 * (docs/boot.md, "Arguments in registers"; tools/regargs.py).
 *
 * 0x0206434C (named ENV_SetResourceSet by the SDK table; it sets a cell
 * animation's sequence) is called as func_0206434C(anim, sequence) by
 * every caller (func_0206428C, func_020642C8, func_020643CC), and its
 * first callee func_02062FF0 (`void func_02062FF0(struct CellAnim *anim,
 * void *bank)`) stores that sequence.  The matched C names only the
 * animation; on the DS the sequence stays in r1 (aletheia's
 * asm/arm9/main.s: `push {r4, lr}; mov r4, r0; bl 0x02062FF0`).  A host
 * passed an indeterminate sequence and the title screen's animation read
 * from it.  This file names it.
 *
 * cmake/boot.cmake lists the matched file as replaced.
 *
 * symbols:
 *   func_02062FF0   0x02062FF0  arm
 *   func_0206419C   0x0206419C  arm
 */

extern void func_02062FF0(void *p, void *seq);
extern void func_0206419C(void *p);

void func_0206434C(void *p, void *seq)
{
    func_02062FF0(p, seq);
    func_0206419C(p);
}
