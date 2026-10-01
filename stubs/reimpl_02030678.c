/*
 * sub_02030678 -- create a heap in a caller's block or stop.
 * REIMPLEMENTATION of aletheia's EXACT src/matched/arm9/sub_02030678.c, for
 * the reason stubs/reimpl_0203064C.c gives: a register-forwarded argument.
 *
 * The matched source is
 *
 *     void sub_02030678(void *a, u32 b, u32 c)
 *     {
 *         if (sub_0202FE68(a, b) != 0) return;
 *         sub_020306A0(sub_02030524(), 0, c);
 *     }
 *
 * and sub_0202FE68 (src/matched/arm9/sub_0202FE68.c) takes three
 * arguments, (block, size, id): the id is `c`, which the DS passes along in
 * r2 untouched.  The host passes it explicitly.  cmake/boot.cmake lists the
 * matched file as replaced.
 */

#include "stubs.h"

int  sub_0202FE68(void *bound, u32 param, u32 dst);
u16  sub_02030524(void);
void func_020306A0(int n);
void sub_02030678(void *block, u32 size, u32 id);

void sub_02030678(void *block, u32 size, u32 id)
{
    if (sub_0202FE68(block, size, id) != 0) {
        return;
    }
    func_020306A0((int)sub_02030524());
}
