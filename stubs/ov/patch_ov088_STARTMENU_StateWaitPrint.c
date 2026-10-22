/*
 * function: STARTMENU_StateWaitPrint
 * address:  0x021BA2C8
 * packet:   pkt_ov088_021BA2C8
 * region:   ov088
 * compiler: dsi/1.1p1
 * flags:    -O4,p
 * lang:     c99
 * isa:      thumb
 * patches: src/game/ov088/startmenu.c STARTMENU_StateWaitPrint
 *
 * The src/game form of stubs/ov/reimpl_ov088_func_021BA2C8.c (the same code, the
 * tree's names): the title menu's Settings row (docs/hooks.md,
 * "title_settings").  Written by tools/ov088_patches.py from
 * that file; a change goes into the reimpl, then this is
 * written again.
 * port hook "title_settings" (docs/hooks.md): when the settings screen has
 * rebuilt its rows (bw_mod_title_take_rebuild) the cursor state comes back at
 * once, with no fade. With the hook off this is the matched function,
 * unchanged.
 * original status:   EXACT*
 * note:     listing extent (112) runs 2 bytes past the compiled function (110);
 *           trailing alignment padding, no byte difference.
 *
 * symbols:
 *   PRINTSYS_QUE_Main  0x0201C4E4  thumb
 *   BMP_WIN_GetBmp  0x02045770  arm
 *   PRINTSYS_QUE_IsExistTarget  0x0201C6C4  thumb
 *   BMP_WIN_TransferChars  0x02045334  arm
 *   PRINTSYS_QUE_IsFinished  0x0201C6B4  thumb
 *   STARTMENU_RefreshSaveStatus  0x021BC0C8  thumb  ov088
 *   STARTMENU_BeginFadeOut  0x021BC054  thumb  ov088
 */

extern int bw_mod_title_take_rebuild(void);
extern int PRINTSYS_QUE_Main(void *p);
extern void *BMP_WIN_GetBmp(void *p);
extern int PRINTSYS_QUE_IsExistTarget(void *t, void *p);
extern void BMP_WIN_TransferChars(void *p);
extern int PRINTSYS_QUE_IsFinished(void *p);
extern int STARTMENU_RefreshSaveStatus(void *p);
extern int STARTMENU_BeginFadeOut(void *p, int k);

int STARTMENU_StateWaitPrint(char *p)
{
    unsigned i;
    void *t;
    PRINTSYS_QUE_Main(*(void **)(p + 0x68));
    for (i = 0; i < 14; i++) {
        t = *(void **)(p + 0x68);
        if (*(unsigned char *)(p + i * 8 + 0x84) != 0 && PRINTSYS_QUE_IsExistTarget(t, BMP_WIN_GetBmp(*(void **)(p + i * 8 + 0x80))) == 0) {
            BMP_WIN_TransferChars(*(void **)(p + i * 8 + 0x80));
            *(unsigned char *)(p + i * 8 + 0x84) = 0;
        }
    }
    if (PRINTSYS_QUE_IsFinished(*(void **)(p + 0x68)) == 1) {
        if (bw_mod_title_take_rebuild())
            return 5;
        if (STARTMENU_RefreshSaveStatus(p) == 1)
            return 10;
        return STARTMENU_BeginFadeOut(p, 5);
    }
    return 4;
}
