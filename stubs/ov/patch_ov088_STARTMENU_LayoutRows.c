/*
 * function: STARTMENU_LayoutRows
 * address:  0x021BB608
 * packet:   pkt_ov088_021BB608
 * region:   ov088
 * compiler: dsi/1.1p1
 * flags:    -O4,p
 * lang:     c99
 * isa:      thumb
 * patches: src/game/ov088/startmenu.c STARTMENU_LayoutRows
 *
 * The src/game form of stubs/ov/reimpl_ov088_func_021BB608.c (the same code, the
 * tree's names): the title menu's Settings row (docs/hooks.md,
 * "title_settings").  Written by tools/ov088_patches.py from
 * that file; a change goes into the reimpl, then this is
 * written again.
 * port hook "title_settings" (docs/hooks.md): on the settings screen the rows
 * stack 3 tiles apart from tile row 2, every sprite is hidden, and the
 * highlight goes on the cursor row. With the hook off this is the matched
 * function, unchanged.
 * original status:   EXACT
 *
 * Lays out the up-to-eight menu rows listed at +0x120 (8 terminates),
 * stacking them by the heights in gStartMenuRowInfo and positioning the row
 * sprites.  Row order is filled in by ov088_func_021BB4B4.c.
 *
 * symbols:
 *   STARTMENU_DrawRowFrame  0x021BB798  thumb  ov088
 *   STARTMENU_SetRowActorY  0x021BB5E0  thumb  ov088
 *   STARTMENU_PaintRowPalette  0x021BB810  thumb  ov088
 *   STARTMENU_ShowRowActors  0x021BB9DC  thumb  ov088
 *   BG_SYS_ClearSlot  0x020414EC  arm
 *   MISC_GetSlotState  0x0200C308  thumb
 *   CLACT_SetActorDrawEnable  0x0204B3DC  arm
 *   gStartMenuRowInfo     0x021BD5EC  data  a4
 */

extern int bw_mod_title_settings_on(void);
extern int bw_mod_title_in_settings(void);
extern int eidos_sui_cursor_row(void);
extern void STARTMENU_ShowRowActors(char *p, int flag);
extern void BG_SYS_ClearSlot(unsigned layer);
typedef unsigned char u8;
typedef signed char s8;
typedef unsigned short u16;
typedef signed short s16;
typedef unsigned int u32;

struct Ent {
    u16 f0;
    u16 f2;
};

extern const struct Ent gStartMenuRowInfo[];

extern void STARTMENU_DrawRowFrame(char *p, u32 i, u32 arg);
extern void STARTMENU_SetRowActorY(char *p, u32 idx, s16 y);
extern void STARTMENU_PaintRowPalette(char *p, u32 i, int y, int a3);
extern u8 MISC_GetSlotState(void *, unsigned);
extern void CLACT_SetActorDrawEnable(void *, int);

void STARTMENU_LayoutRows(char *p)
{
    u32 i;
    s8 y;
    u8 v;
    void *obj;

    i = 0;
    if (bw_mod_title_settings_on() && bw_mod_title_in_settings()) {
        u32 cur = eidos_sui_cursor_row();
        BG_SYS_ClearSlot(1);
        BG_SYS_ClearSlot(2);
        p[0x128] = cur;
        *(u32 *)(p + 0x130) = 0;
        p[0x12f] = 2 + 3 * cur;
        for (i = 0; i < 8; i++) {
            if ((u8)(p + i)[0x120] == 8)
                break;
            STARTMENU_DrawRowFrame(p, (u8)(p + i)[0x120], 2 + 3 * i);
        }
        STARTMENU_ShowRowActors(p, 0);
        for (i = 0; i < 8; i++) {
            if ((u8)(p + i)[0x120] == 8)
                break;
            STARTMENU_PaintRowPalette(p, (u8)(p + i)[0x120], 2 + 3 * i, i == cur ? 2 : 1);
        }
        return;
    }
    p[0x128] = 0;
    *(u32 *)(p + 0x130) = 0;
    y = 2;
    p[0x12f] = 2;
    for (i = 0; i < 8; i++) {
        if ((u8)(p + i)[0x120] == 8)
            break;
        STARTMENU_DrawRowFrame(p, (u8)(p + i)[0x120], y);
        v = (p + i)[0x120];
        if (v == 0) {
            obj = *(void **)(p + 0x1c);
        } else if (v == 5) {
            STARTMENU_SetRowActorY(p, 5, y * 8 + gStartMenuRowInfo[v].f2 * 8 / 2);
            obj = *(void **)(p + 0x30);
        } else if (v == 3) {
            if (MISC_GetSlotState(*(void **)(p + 8), 1) != 1)
                goto next;
            STARTMENU_SetRowActorY(p, 2, y * 8 + gStartMenuRowInfo[(u8)(p + i)[0x120]].f2 * 8 / 2);
            obj = *(void **)(p + 0x24);
        } else if (v == 4) {
            if (MISC_GetSlotState(*(void **)(p + 8), 2) != 1)
                goto next;
            STARTMENU_SetRowActorY(p, 3, y * 8 + gStartMenuRowInfo[(u8)(p + i)[0x120]].f2 * 8 / 2);
            obj = *(void **)(p + 0x28);
        } else if (v == 7) {
            if (MISC_GetSlotState(*(void **)(p + 8), 3) != 1)
                goto next;
            STARTMENU_SetRowActorY(p, 4, y * 8 + gStartMenuRowInfo[(u8)(p + i)[0x120]].f2 * 8 / 2);
            obj = *(void **)(p + 0x2c);
        } else {
            goto next;
        }
        CLACT_SetActorDrawEnable(obj, 1);
    next:
        y += (s8)gStartMenuRowInfo[(u8)(p + i)[0x120]].f2;
    }
    STARTMENU_SetRowActorY(p, 0, 0x40);
    STARTMENU_PaintRowPalette(p, (u8)p[0x120], (s8)p[0x12f], 2);
}
