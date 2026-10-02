/*
 * func_02023EF4 -- arm a brightness fade on the main and/or sub engine.
 * REIMPLEMENTATION of aletheia's EXACT src/matched/arm9/func_02023EF4.c,
 * for one callee declared twice with two prototypes.
 *
 * The matched source declares G2x_SetBlendBrightness_ (0x0207D658) under
 * two names at the one address, with two and with three parameters, so that
 * mwcc emits the two call sites differently (its note says why).  A host link
 * has one function there, defined with three parameters
 * (src/matched/arm9_sdk/func_0207D658.c: register, plane mask, brightness),
 * and the boot's per-file rename makes both names that one function, which
 * a C compiler rejects as conflicting declarations.
 *
 * Both calls pass three values on the DS.  The first call site names two,
 * and its third travels in r2 untouched: the function's own `from` argument,
 * which the prologue copies to r6 and leaves in r2 (aletheia's
 * asm/arm9/main.s, 0x02023EF4-0x02023F10, `adds r6, r2, #0` and no write to
 * r2 before the blx).  The second names all three, `from` among them.  So
 * both are G2x_SetBlendBrightness_(reg, to, from), which is what this file
 * says.  cmake/boot.cmake lists the matched file as replaced.
 */

#include "stubs.h"
#include "../sdk/dsmem.h"

void func_0207D658(u32 addr, int plane, int brightness);
void func_02023E9C(void *rec, void *p, u16 steps, s16 from, u32 a4, u32 a5);
void func_02023EF4(void *p, u16 steps, s16 from, s16 to, u32 mask);

extern char data_0214691C[];
extern char data_021468FC[];

void func_02023EF4(void *p, u16 steps, s16 from, s16 to, u32 mask)
{
    if (p == 0) {
        return;
    }
    if (mask & 1u) {
        func_0207D658((u32)(uintptr_t)bw_ds_ptr(0x04000050u), to, from);
        func_02023E9C(data_0214691C, p, steps, from, (u32)to, 1);
    }
    if (mask & 2u) {
        func_0207D658((u32)(uintptr_t)bw_ds_ptr(0x04001050u), to, from);
        func_02023E9C(data_021468FC, p, steps, from, (u32)to, 2);
    }
}
