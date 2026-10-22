/*
 * function: STARTMENU_MenuExit
 * address:  0x021BA014
 * packet:   pkt_ov088_021BA014
 * region:   ov088
 * compiler: dsi/1.1p1
 * flags:    -O4,p
 * lang:     c99
 * isa:      thumb
 * patches: src/game/ov088/startmenu.c STARTMENU_MenuExit
 *
 * The src/game form of stubs/ov/reimpl_ov088_func_021BA014.c (the same code, the
 * tree's names): the title menu's Settings row (docs/hooks.md,
 * "title_settings").  Written by tools/ov088_patches.py from
 * that file; a change goes into the reimpl, then this is
 * written again.
 * port hook "title_settings" (docs/hooks.md, docs/settings-design.md):
 * the menu proc's exit function. Selection 9 starts the settings screen; B on the
 * settings screen (selection 8) returns to the menu instead of the title screen.
 *
 * With the hook off this is the matched function, unchanged.
 *
 * original status:   EXACT
 *
 * symbols:
 *   PROCSYS_FreeProcWork  0x020315D4  arm
 *   GFL_HEAP_DeleteChecked  0x020306F0  arm
 *   GAME_BEACON_RegisterClearCallback  0x020274C8  thumb
 *   STARTMENU_ReplaceWithBattleFlagProcNoSave  0x021BC114  thumb  ov088
 *   STARTMENU_ReplaceWithBattleFlagProcWithSave  0x021BC0FC  thumb  ov088
 *   STARTMENU_ChangeToIntroProc  0x021BC0E4  thumb  ov088
 *   GFL_HEAP_Alloc  0x02030734  arm
 *   MI_CpuFill8  0x02082BCC  arm
 *   PROCSYS_ReplaceGlobal  0x020312F8  arm
 *   d_021BDA00     0x021BDA00  data  ov088  a1
 *   d_021BD66C     0x021BD66C  data  ov088  a4
 *   d_021DF538     0x021DF538  data  ov121  a4
 *   d_02213C4C     0x02213C4C  data  a4
 *   d_0209DEF0     0x0209DEF0  data  a4
 *   d_021D77AC     0x021D77AC  data  ov016  a4
 *   d_02184904     0x02184904  data  ov168  a4
 *   d_021F6D78     0x021F6D78  data  ov107  a4
 *   d_021BD574     0x021BD574  data  a4
 *   ov121_id       0x00000079  data
 *   ov199_id       0x000000C7  data
 *   ov016_id       0x00000010  data
 *   ov168_id       0x000000A8  data
 *   ov107_id       0x0000006B  data
 *   ov088_id       0x00000058  data
  */
/* sdk/mod_title.h, spelled out: the overlay link header declares what the
 * file itself does not (docs/hooks.md). */
extern int bw_mod_title_settings_on(void);
extern int bw_mod_title_in_settings(void);
extern int bw_mod_title_set_in_settings(int on);
extern void eidos_sui_leave(void);
extern void bw_mod_title_set_return(int overlay, const void *table);
extern const void *eidos_opt_proc_table(void);
#define BW_TITLE_ROW_SETTINGS 7
#define BW_TITLE_SEL_SETTINGS 9
typedef unsigned char u8;
typedef unsigned int u32;

extern void PROCSYS_FreeProcWork(int a);
extern void GFL_HEAP_DeleteChecked(int a);
extern void GAME_BEACON_RegisterClearCallback(void);
extern void STARTMENU_ReplaceWithBattleFlagProcNoSave(void);
extern void STARTMENU_ReplaceWithBattleFlagProcWithSave(void);
extern void STARTMENU_ChangeToIntroProc(void);
extern void *GFL_HEAP_Alloc(u32 heap, u32 size, u32 clear, const char *file, u32 line);
extern void MI_CpuFill8(void *, int, int);
extern void PROCSYS_ReplaceGlobal(int id, void *tmpl, void *arg);
extern char d_021BDA00[];
extern char d_021DF538[];
extern char d_02213C4C[];
extern char d_0209DEF0[];
extern char d_021D77AC[];
extern char d_02184904[];
extern char d_021F6D78[];
extern char d_021BD574[];
extern char ov121_id[];
extern char ov199_id[];
extern char ov016_id[];
extern char ov168_id[];
extern char ov107_id[];
extern char ov088_id[];
extern char d_021BD66C[];

int STARTMENU_MenuExit(int a0, int a1, int a2, char *a3)
{
    u8 sel = *(u8 *)(a3 + 0x129);
    u8 flag = *(u8 *)(a3 + 0x12c);
    char *w;

    PROCSYS_FreeProcWork(a0);
    GFL_HEAP_DeleteChecked(0xb);
    switch (sel) {
    case 0:
        GAME_BEACON_RegisterClearCallback();
        if (flag == 0)
            STARTMENU_ReplaceWithBattleFlagProcNoSave();
        else
            STARTMENU_ReplaceWithBattleFlagProcWithSave();
        break;
    case 1:
        GAME_BEACON_RegisterClearCallback();
        STARTMENU_ChangeToIntroProc();
        break;
    case 2:
        PROCSYS_ReplaceGlobal((int)ov121_id, d_021DF538, 0);
        break;
    case 3:
        w = GFL_HEAP_Alloc(1, 0x14, 0, d_021BDA00, 0x200);
        MI_CpuFill8(w, 0, 0x14);
        *(u32 *)(w + 8) = 0;
        *(u32 *)(w + 0x10) = 1;
        PROCSYS_ReplaceGlobal((int)ov199_id, d_02213C4C, w);
        break;
    case 4:
        PROCSYS_ReplaceGlobal(-1, d_0209DEF0, 0);
        break;
    case 5:
        PROCSYS_ReplaceGlobal((int)ov016_id, d_021D77AC, 0);
        break;
    case 6:
        PROCSYS_ReplaceGlobal((int)ov168_id, d_02184904, 0);
        break;
    case 7:
        w = GFL_HEAP_Alloc(1, 8, 1, d_021BDA00, 0x216);
        *(u8 *)w = 1;
        PROCSYS_ReplaceGlobal((int)ov107_id, d_021F6D78, w);
        break;
    case BW_TITLE_SEL_SETTINGS:
        /* The settings screen is a process of its own (engine/platform/
         * mod_options_proc.c); it hands back to this menu. */
        bw_mod_title_set_return((int)ov088_id, d_021BD66C);
        PROCSYS_ReplaceGlobal(-1, (void *)eidos_opt_proc_table(), 0);
        break;
    default:
        if (bw_mod_title_in_settings()) {
            eidos_sui_leave();
            bw_mod_title_set_in_settings(0);
            PROCSYS_ReplaceGlobal((int)ov088_id, d_021BD66C, 0);
            break;
        }
        PROCSYS_ReplaceGlobal((int)ov088_id, d_021BD574, 0);
        break;
    }
    return 1;
}
