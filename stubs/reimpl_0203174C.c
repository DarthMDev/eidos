/*
 * func_0203174C -- construct a save block (backup_system.c).
 * REIMPLEMENTATION of aletheia's EXACT src/matched/arm9/func_0203174C.c,
 * for one argument the matched C forwards through a register.
 *
 * The matched source reads
 *
 *     b->f2c = func_02033908(heap, in->f00, in->f04, b->f08, 0x10, 4, b->f140);
 *     b->f30 = func_02033BF8();
 *
 * and func_02033BF8 is sub_02033BF8 (src/matched/arm9/unit_02033BF0.c),
 * `u32 sub_02033BF8(struct Obj02033BF0 *p) { return p->unk0c; }`: it takes
 * the object func_02033908 just returned.  On the DS that object is still
 * in r0 at the call (aletheia's asm/arm9/main.s, 0x0203188C-0x02031894:
 * `bl func_02033908; str r0, [r4, #0x2c]; bl func_02033BF8`), so the call
 * reads its field.  A host passes nothing and the callee read address 0x13
 * (NULL + 0x0C, after the boot's identity mapping made every earlier step
 * work).  This file says the argument out loud; everything else is the
 * matched C.  cmake/boot.cmake lists the matched file as replaced.
 */

#include "stubs.h"

struct In {
    u32 f00;
    u32 f04;
    u32 f08;
    u32 f0c;
    u32 f10;
    u32 f14;
};

struct Blk {
    u32 f00;
    u32 f04;
    u32 f08;
    u32 f0c;
    u8  pad10[0x1c - 0x10];
    u8  f1c;
    u8  f1d;
    u8  pad1e[0x24 - 0x1e];
    u32 f24;
    u32 f28;
    u32 f2c;
    u32 f30;
    u32 f34;
    u8  pad38[0x138 - 0x38];
    u32 f138;
    u32 f13c;
    u32 f140;
};

void *func_02030734(u16 heap, u32 size, u32 align, const u8 *file, u32 line);
u32   func_02033908(u16 a, u32 b, u32 c, u32 d, u32 e, u32 f, u32 g);
u32   sub_02033BF8(u32 obj);
void  func_02033410(struct Blk *b, u32 f);
void  func_020319C0(struct Blk *b);
void  func_02034714(const u8 *file, u32 line, const u8 *expr);
struct Blk *func_0203174C(const struct In *in, u32 heap, u32 c, u32 d, u32 e);

extern const u8 str_020A7848[];
extern const u8 str_020A7858[];
extern const u8 str_020A7868[];
extern u32 g_02146994;

struct Blk *func_0203174C(const struct In *in, u32 heap, u32 c, u32 d, u32 e)
{
    struct Blk *b;
    u32 n;

    if (g_02146994 == 0)
        func_02034714(str_020A7848, 0x10c, str_020A7858);

    b = (struct Blk *)func_02030734((u16)heap, 0x154, 1, str_020A7848, 0x10e);
    b->f00 = in->f08;
    b->f04 = in->f0c;
    b->f08 = in->f10;
    b->f0c = in->f14;

    if (c != 0 && d < b->f08)
        func_02034714(str_020A7848, 0x154 - 0x3e, str_020A7868);

    b->f140 = in->f04 << 1;
    if ((in->f04 & 1) != 0)
        b->f140 = b->f140 + 2;

    n = in->f04 + 2;
    b->f13c = (n >> 4) << 2;
    if ((n & 0xf) != 0)
        b->f13c = b->f13c + 4;

    b->f138 = (u32)(uintptr_t)func_02030734((u16)heap, b->f13c, 1, str_020A7848,
                                            0x125);
    b->f2c = func_02033908((u16)heap, in->f00, in->f04, b->f08, 0x10, 4,
                           b->f140);
    b->f30 = sub_02033BF8(b->f2c);
    if (c != 0) {
        b->f34 = c;
        b->f1d = 0;
    } else {
        b->f34 = (u32)(uintptr_t)func_02030734((u16)heap, b->f08, 1, str_020A7848,
                                               0x131);
        b->f1d = 1;
    }
    b->f1c = 0;
    b->f28 = 0;
    b->f24 = 0;
    if (e == 1)
        func_020319C0(b);
    else
        func_02033410(b, 1);
    return b;
}
