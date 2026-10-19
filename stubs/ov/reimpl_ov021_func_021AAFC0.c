/*
 * function: EFFECT_ENCOUNT_CreateEffectObj
 * address:  0x021AAFC0
 * packet:   pkt_ov021_021AAFC0
 * region:   ov021
 * compiler: dsi/1.1p1
 * flags:    -O4,p
 * lang:     c99
 * isa:      thumb
 * replaces: src/matched/ov/ov021_func_021AAFC0.c
 * Port reimplementation for one return type: FLDEFF_ENCOUNT_Start returns
 * the new effect object (a pointer), stored whole at +0x5B4, but the matched
 * C declares it u8.  mwcc keeps the whole register, so the DS stores the
 * full pointer; a host compiler keeps the low byte, and a later use of the
 * object reads through it.  Here the return is a full word, as aletheia's
 * src/game has it (ov021/effect_encount.c, FieldEffectObj *); this file goes
 * when the port compiles src/game (docs/game-tree.md).
 *
 * original status:   EXACT
 *
 * symbols:
 *   FLDEFF_ENCOUNT_Start  0x021ADAAC  thumb
 *   MI_CpuFill8  0x02082BCC  arm
 */

typedef unsigned char u8;
typedef unsigned short u16;

typedef struct {
    u16 h0;
    u16 h2;
    u16 h4;
    u16 h6;
    int f8;
    u8 bc;
    u8 pad[3];
} R;

typedef struct {
    int f0;
    char pad[0x5b0];
    int f5b4;
} D;

extern int FLDEFF_ENCOUNT_Start(int a, u8 *b, int c, int d, int e, int f);
extern void MI_CpuFill8(void *p, int v, int n);

void EFFECT_ENCOUNT_CreateEffectObj(int a, D *d, R *r)
{
    if (d->f5b4 == 0) {
        d->f5b4 = FLDEFF_ENCOUNT_Start(a, (u8 *)d->f0, r->h2, r->h6, r->f8, r->bc);
        if (d->f5b4 == 0) {
            MI_CpuFill8(r, 0, 0x10);
        }
    }
}
