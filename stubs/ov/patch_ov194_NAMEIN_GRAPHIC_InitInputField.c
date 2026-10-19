/*
 * patches: src/game/ov194/namein_graphic_field.c NAMEIN_GRAPHIC_InitInputField
 *
 * The function as aletheia's src/game has it, with one parameter type: `c`
 * is the font the input field prints with (stored at +0x34, read back as a
 * GfFont *).  aletheia keeps it u16 on purpose (the original source most
 * likely declared it so, and the pointer type moves the entry spills 4
 * bytes off); mwcc never narrows a parameter on entry, so the DS keeps the
 * whole pointer, while a host compiler cuts it to 16 bits and the name
 * entry screen reads the font at 0x0000FD34.  Here it is u32.  This patch
 * stays while aletheia keeps u16 (see its docs/DEBT.tsv, narrow-type).
 */

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

struct BmpWin;
struct Strbuf;
struct Actor;
struct Obj;
struct Vec2;

extern void MI_CpuFill8(void *,unsigned,unsigned);
extern struct BmpWin *BMP_WIN_Create(unsigned int bgLayer, unsigned int posX, unsigned int posY, unsigned char width, unsigned char height, unsigned char palette, unsigned char allocDir);
extern void BMP_WIN_TransferChars(struct BmpWin *win);
extern void BMP_WIN_MakeScreen(struct BmpWin *win);
extern u8 BMP_WIN_GetBgLayer(struct BmpWin *win);
extern void BG_SYS_PushTile(u32 index);
extern struct Strbuf *STRBUF_Create(u32 capacity, u32 heapId);
extern int NAMEIN_GRAPHIC_LoadCharMember(int a, int b);
extern int NAMEIN_GRAPHIC_LoadPairMember(int a);
extern void *NAMEIN_GRAPHIC_GetActor(u8 *p, u32 i);
extern u16 STRBUF_GetLength(struct Strbuf *strbuf);
extern u16 STRBUF_GetEosChar(void);
extern void STRBUF_ToChars(struct Strbuf *strbuf, unsigned short *out, unsigned int maxChars);
extern void CLACT_SetActorAnimSeq(struct Actor *actor, unsigned short value);
extern void CLACT_SetActorAutoAnim(struct Actor *actor, unsigned int enable);
extern void CLACT_AddActorSurfacePos(struct Obj *p, const struct Vec2 *delta, u32 which);
extern void CLACT_SetActorDrawEnable(struct Actor *actor, unsigned int value);
extern void CLACT_StartActorAnim(struct Actor *a);
int NAMEIN_GRAPHIC_RefreshInputField(u8 *p);

void NAMEIN_GRAPHIC_InitInputField(u8 *p, int a, u16 b, u32 c, int d, u8 *e, int f)
{
    u16 pos[2];
    int i;
    int h;
    int base;
    MI_CpuFill8(p, 0, 0x94);
    i = 0;
    *(int *)(p + 0x34) = c;
    *(u16 *)(p + 0x22) = b;
    *(int *)(p + 0x38) = d;
    h = (int)BMP_WIN_Create(0, 8, 2, 0x18, 2, 15, 1);
    *(int *)(p + 0x40) = h;
    BMP_WIN_TransferChars((struct BmpWin *)h);
    BMP_WIN_MakeScreen((struct BmpWin *)h);
    BG_SYS_PushTile(BMP_WIN_GetBgLayer((struct BmpWin *)h));
    *(int *)(p + 0x2c) = *(int *)(p + 0x40);
    *(u8 *)(p + 0x30) = i;
    *(int *)(p + 0x3c) = (int)STRBUF_Create(0xe, f);
    for (i = 0; i < 3; i++) {
        *(int *)(p + i * 4 + 0x84) = NAMEIN_GRAPHIC_LoadCharMember(i, f);
    }
    *(int *)(p + 0x90) = NAMEIN_GRAPHIC_LoadPairMember(f);
    for (i = 0; i < 16; i++) {
        *(int *)(p + i * 4 + 0x44) = (int)NAMEIN_GRAPHIC_GetActor(e, i);
    }
    if (STRBUF_GetLength((struct Strbuf *)a) == 0) {
        *(u16 *)p = STRBUF_GetEosChar();
    } else {
        STRBUF_ToChars((struct Strbuf *)a, (u16 *)p, b + 1);
        *(u16 *)(p + 0x1e) = STRBUF_GetLength((struct Strbuf *)a);
    }
    pos[1] = 0x25;
    base = 0x40 - (*(u16 *)(p + 0x22) * 14) / 2;
    if (base < 0) {
        base = 0;
    }
    base += 0x40;
    for (i = 0; i < 16; i++) {
        pos[0] = base + i * 14;
        CLACT_SetActorAnimSeq((struct Actor *)(*(int *)(p + i * 4 + 0x44)), 1);
        CLACT_SetActorAutoAnim((struct Actor *)(*(int *)(p + i * 4 + 0x44)), 1);
        CLACT_AddActorSurfacePos((struct Obj *)(*(int *)(p + i * 4 + 0x44)), (const struct Vec2 *)pos, 0);
        CLACT_SetActorDrawEnable((struct Actor *)(*(int *)(p + i * 4 + 0x44)), 1);
    }
    for (i = *(u16 *)(p + 0x22); i < 16; i++) {
        CLACT_SetActorDrawEnable((struct Actor *)(*(int *)(p + i * 4 + 0x44)), 0);
    }
    CLACT_StartActorAnim((struct Actor *)(*(int *)(p + *(u16 *)(p + 0x1e) * 4 + 0x44)));
    *(int *)(p + 0x28) = 1;
    NAMEIN_GRAPHIC_RefreshInputField(p);
}
