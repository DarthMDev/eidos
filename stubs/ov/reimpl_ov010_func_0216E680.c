/*
 * function: EVENT_BATTLE_CallAndInvert
 * address:  0x0216E680
 * packet:   pkt_0216E680
 * region:   ov010
 * compiler: dsi/1.1p1
 * flags:    -O4,p
 * lang:     c99
 * isa:      thumb
 * replaces: src/matched/ov/func_0216E680.c
 * Port reimplementation for one call: the matched C calls the hook with no
 * arguments.  On the DS the hook finds its own address in r0 (a0 still
 * holds it); here that argument is passed.  Everything else is the matched
 * function.  This file goes when the port compiles src/game
 * (docs/game-tree.md).
 *
 * original status:   EXACT
 *
 * symbols:
 *   (none: no relocation site)
 */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;



int EVENT_BATTLE_CallAndInvert(int a0)
{
    int t4;

t4 = a0 + 0;
    if (t4 == 0) goto loc_0216E688;
loc_0216E686:
    a0 = ((int (*)(int))t4)(a0);
loc_0216E688:
a0 = ~t4;
    return a0;}
