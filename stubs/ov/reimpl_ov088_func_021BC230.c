/*
 * function: func_021BC230
 * address:  0x021BC230
 * packet:   pkt_ov088_021BC230
 * region:   ov088
 * compiler: dsi/1.1p1
 * flags:    -O4,p
 * lang:     c99
 * isa:      thumb
 * replaces: src/matched/ov/ov088_func_021BC230.c
 * port hook "title_settings" (docs/hooks.md, docs/multiplayer.md): the new
 * game's intro.  In a multiplayer game the name entry opens with the
 * player's multiplayer name already typed (the name the player set on the
 * Multiplayer screen); the player can still change it.
 *
 * Without a multiplayer game this is the matched function, unchanged.
 *
 * original status:   EXACT
 *
 * symbols:
 *   func_021BD2F4  0x021BD2F4  thumb  ov088
 *   func_02031330  0x02031330  arm
 *   func_02031348  0x02031348  arm
 *   func_02008550  0x02008550  thumb
 *   func_021BC4A4  0x021BC4A4  thumb  ov088
 *   func_020458F8  0x020458F8  arm
 *   func_02045994  0x02045994  arm
 *   d_021BFD24     0x021BFD24  data  ov204  a4
 *   d_021F5580     0x021F5580  data  ov194  a4
 *   ov204_id       0x000000CC  data
 *   ov194_id       0x000000C2  data
  */
/* sdk/mp.h and sdk/mod_net.h, spelled out: the overlay link header declares
 * what the file itself does not (docs/hooks.md). */
extern int bw_mp_state(void);
extern const char *bw_mod_net_name(void);
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

extern void func_021BD2F4(void *saveIntr);
extern u32 func_02031330(void *procSys);
extern void func_02031348(void *procSys, u32 overlayId, const void *data, void *work);
extern u32 func_02008550(void *myStatus);
extern void func_021BC4A4(void *myStatus, void *name, u32 sex);
extern u16 func_020458F8(void *strbuf);
extern void func_02045994(void *strbuf, const u16 *chars, u32 n);
extern char d_021BFD24[];
extern char d_021F5580[];
extern char ov204_id[];
extern char ov194_id[];

/* The work of the intro (src/game/ov088/startmenu_types.h,
 * NewGameIntroWork) and of the name entry (src/game/ov010/namein_args.h),
 * the fields this function reads. */
typedef struct IntroParam {
    void *myStatus;
    void *config;
    u32 phase;
    void *saveIntr;
    u32 voiceHandle;
    u32 retryNameEntry;
} IntroParam;

typedef struct NameArgs {
    int kind;
    u16 species;
    u16 traits;
    void *mon;
    void *boxName;
    void *boxManager;
    int boxIndex;
    int maxLen;
    int canceled;
    void *name;
    void *saveIntr;
    void *pokedex;
} NameArgs;

typedef struct IntroWork {
    u32 startMode;
    void *config;
    void *myStatus;             /* startArg.myStatus */
    void *saveOptions;
    IntroParam introParam;
    NameArgs *nameInArgs;
    void *saveIntr;
    void *procSys;
    void *waveArcSet;
    u8 padding38[0x18];
    u32 voiceHandle;
    void *myStatus2;            /* the player's status the intro fills */
    void *config2;
} IntroWork;

/* The multiplayer name, typed in already when nothing is typed yet. */
static void prefill_name(NameArgs *a)
{
    const char *n;
    u16 buf[8];
    int i;

    if (bw_mp_state() == 0 || a->name == 0 || func_020458F8(a->name) != 0) {
        return;
    }
    n = bw_mod_net_name();
    if (n == 0 || n[0] == '\0') {
        return;
    }
    for (i = 0; i < 7 && n[i] != '\0'; i++) {
        buf[i] = (u8)n[i];
    }
    buf[i] = 0xFFFF;
    func_02045994(a->name, buf, (u32)(i + 1));
}

int func_021BC230(void *proc, int *seq, void *procWork, IntroWork *work)
{
    int busy;

    (void)proc;
    (void)procWork;
    func_021BD2F4(work->saveIntr);
    busy = (int)func_02031330(work->procSys);
    switch (*seq) {
    case 0:
        func_02031348(work->procSys, (u32)ov204_id, d_021BFD24, &work->introParam);
        *seq = 1;
        break;
    case 1:
        if (busy != 1)
            *seq = 2;
        break;
    case 2:
        work->nameInArgs->species = (u16)func_02008550(work->myStatus);
        work->nameInArgs->saveIntr = work->saveIntr;
        prefill_name(work->nameInArgs);
        func_02031348(work->procSys, (u32)ov194_id, d_021F5580, work->nameInArgs);
        *seq = 3;
        break;
    case 3:
        if (busy != 1)
            *seq = 4;
        break;
    case 4:
        func_021BC4A4(work->myStatus2, work->nameInArgs->name, func_02008550(work->myStatus2));
        work->introParam.phase = 7;
        func_02031348(work->procSys, (u32)ov204_id, d_021BFD24, &work->introParam);
        *seq = 6;
        break;
    case 6:
        if (busy != 1)
            *seq = 5;
        break;
    case 5:
        if (work->introParam.retryNameEntry == 0)
            *seq = 7;
        else
            *seq = 2;
        break;
    case 7:
        return 1;
    }
    return 0;
}
