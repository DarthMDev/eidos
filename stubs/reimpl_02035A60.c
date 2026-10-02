/*
 * func_02035A60 -- the touch panel's per-frame sample.
 * REIMPLEMENTATION of aletheia's EXACT src/matched/arm9/func_02035A60.c,
 * for a value the matched C hands on in r0.
 *
 * The matched source is
 *
 *     void func_02035A60(void)
 *     {
 *         func_02035224();
 *         func_020358A0();
 *     }
 *
 * func_02035224 returns the system context (the word at 0x021469BC,
 * aletheia's asm/arm9/main.s, 0x02035224-0x02035244), and func_020358A0
 * (src/matched/arm9/func_020358A0.c) is `void func_020358A0(struct Tp
 * *ctx)`: on the DS the first call's r0 is the second's argument
 * (0x02035A64-0x02035A68, two `bl` with nothing between).  A host passes
 * nothing and the sampler read NULL + 0x3E.  This file passes the context.
 * cmake/boot.cmake lists the matched file as replaced.
 */

#include "stubs.h"

void *func_02035224(void);
void  func_020358A0(void *ctx);
void  func_02035A60(void);

void func_02035A60(void)
{
    func_020358A0(func_02035224());
}
