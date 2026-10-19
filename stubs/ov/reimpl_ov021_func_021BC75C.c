/*
 * function: FIELD_PLACE_NAME_RegisterClactPalette
 * address:  0x021BC75C
 * packet:   pkt_ov021_021BC75C
 * region:   ov021
 * compiler: dsi/1.1p1
 * flags:    -O4,p
 * lang:     c99
 * isa:      thumb
 * replaces: src/matched/ov/ov021_func_021BC75C.c
 * Port reimplementation for one return type: FIELD_PLACE_NAME_GetArcHandle
 * returns the place-name archive handle (a pointer), but the matched C
 * declares it u16.  mwcc trusts a u16 return to be in range and keeps the
 * whole register, so the DS passes the full pointer to
 * CLACT_RegisterPaletteEx; a host compiler narrows it to 16 bits and the
 * field map crashed reading the handle at 0x000069E2 when it created the
 * place-name banner.  Here the handle is a pointer, as aletheia's src/game
 * (ov021/field_place_name.c) has it; this file goes when the port compiles
 * src/game (docs/game-tree.md).
 *
 * original status:   EXACT*
 * symbols:
 *   FIELD_PLACE_NAME_GetHeapId  0x021BD090  thumb  ov021
 *   FIELD_PLACE_NAME_GetArcHandle  0x021BD094  thumb  ov021
 *   CLACT_RegisterPaletteEx  0x0204AC18  arm  arm9
 */

typedef unsigned short u16;

typedef struct {
    char pad[0x2C];
    void *f2C;
} Obj;

typedef unsigned char  u8;
extern u16 FIELD_PLACE_NAME_GetHeapId(u8 *p);
extern void *FIELD_PLACE_NAME_GetArcHandle(Obj *p);
typedef signed int     s32;
typedef unsigned int   u32;
extern s32 CLACT_RegisterPaletteEx(void *a, u32 b, u32 c, u32 d, u16 e, u16 f, u16 g);

void FIELD_PLACE_NAME_RegisterClactPalette(Obj *p)
{
    u16 a = FIELD_PLACE_NAME_GetHeapId((u8 *)p);
    void *b = FIELD_PLACE_NAME_GetArcHandle(p);

    p->f2C = (void *)CLACT_RegisterPaletteEx(b, 3, 0, 0x20, 0, 1, a);
}
