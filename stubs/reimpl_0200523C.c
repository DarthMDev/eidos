/*
 * func_0200523C -- the boot sequence before the frame loop.
 * REIMPLEMENTATION of aletheia's EXACT src/matched/arm9/func_0200523C.c,
 * for the values its calls hand on in r0.
 *
 * The matched source declares every callee `(void)` and calls them as
 * statements, which reproduces the bytes: on the DS each `bl` with no
 * argument set-up passes whatever the previous call returned in r0
 * (aletheia's asm/arm9/main.s, 0x0200527A-0x020052BC).  Three of those
 * values are arguments their callee uses:
 *
 *   sub_020070A4()            returns the save system
 *   sub_0200873C(sys)         = func_020071CC(sys, 0x1B): one save block
 *   func_02008414(block)      reads that block
 *   sub_02012FF0(sys)         the save system again (0x02005286-0x0200528A)
 *
 * and the last pair, 0x020052B8-0x020052BC, is sub_0200873C(sub_020070A4())
 * again with its result unused.  A host passes what the C says, and
 * sub_020071CC read through NULL.  This file says the three arguments out
 * loud; every other call is the matched C's, in its order.  The r0/r1 that
 * reach func_02005B24 (left by func_020173D0) only reach func_020726D0,
 * which takes none, so they are passed as 0.  cmake/boot.cmake lists the
 * matched file as replaced.
 *
 * symbols:
 *   ov010_id        0x0000000A  data
 *   ov011_id        0x0000000B  data
 */

#include "stubs.h"
#include "../sdk/dsmem.h"

void  func_02034AC4(void *id);
void  func_0201C41C(int a0);
void  func_0203CB34(int a0, int a1, int a2, int a3);
void  func_02010794(int a0);
void  func_020107CC(void);
void  func_020312CC(int a0, void *a1, int a2);
void  func_02006F20(int a0);
void *func_020070A4(void);
void *func_0200873C(void *sys);
u8    func_02008414(void *block);
void  func_02012FF0(void *sys);
void  func_0200F6B8(void);
void  func_0200F6D8(int a0);
void  func_02034704(void (*fn)(void));
void  func_0201F344(int a0);
void  func_020173D0(int a0);
void  func_02005B24(u32 a0, u32 a1);
void  func_02006544(int a0, void (*a1)(void), void (*a2)(void));
void  func_0200530C(void);
void  func_020082DC(void);
void  func_020266E0(int a0);
void  func_02010DB4(void);
void  func_0200FB6C(void);
void  func_02006C00(void);
void  func_02006C2C(void);
void  func_0200523C(void);

extern u8 ov010_id[];
extern u8 ov011_id[];

void func_0200523C(void)
{
    func_02034AC4(ov010_id);
    func_02034AC4(ov011_id);
    func_0201C41C(0);
    func_0203CB34(1, 0, 1, 0xC);
    func_02010794(0xC);
    func_020107CC();
    func_020312CC(0xC - 0xD, bw_ds_ptr(0x020A0480u), 0);
    func_02006F20(7);
    (void)func_02008414(func_0200873C(func_020070A4()));
    func_02012FF0(func_020070A4());
    func_0200F6B8();
    func_0200F6D8(8);
    func_02034704(func_0200FB6C);
    func_0201F344(0);
    func_020173D0(0);
    func_02005B24(0, 0);
    func_02006544(1, func_02006C00, func_02006C2C);
    (void)func_0200873C(func_020070A4());
    func_0200530C();
    func_020082DC();
    func_020266E0(1);
    func_02010DB4();
}
