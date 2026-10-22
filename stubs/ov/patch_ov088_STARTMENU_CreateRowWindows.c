/*
 * function: STARTMENU_CreateRowWindows
 * address:  0x021BAD08
 * packet:   pkt_ov088_021BAD08
 * region:   ov088
 * compiler: dsi/1.1p1
 * flags:    -O4,p
 * lang:     c99
 * isa:      thumb
 * patches: src/game/ov088/startmenu.c STARTMENU_CreateRowWindows
 *
 * The src/game form of stubs/ov/reimpl_ov088_func_021BAD08.c (the same code, the
 * tree's names): the title menu's Settings row (docs/hooks.md,
 * "title_settings").  Written by tools/ov088_patches.py from
 * that file; a change goes into the reimpl, then this is
 * written again.
 * port hook "title_settings" (docs/hooks.md, docs/settings-design.md):
 * the title menu text. Window 7 reads message 6 (the game's conditional row); when
 * the SETTINGS row owns kind 7 it holds the string "SETTINGS" instead.
 * On the settings screen the row windows hold eidos_sui text: kind k (row k-1)
 * prints in slot k+5, label at the left and value right-aligned.
 *
 * With the hook off this is the matched function, unchanged.
 *
 * original status:   EXACT
 *
 * Title menu graphics setup: creates the fourteen handles at +0x80 from
 * the 6-byte table gStartMenuWindowTemplates (released by STARTMENU_DeleteRowWindows), then draws each
 * menu text window into them.  The real extent is 1116 bytes (a literal
 * pool sits mid-body at 0x021BAEF8).
 *
 * Match levers: the bg = p->f68 load is hoisted into a local ahead of each
 * PRINTSYS_PrintQueColor call; slot[1].on = 1 sits inside both arms of the if so the
 * constant 1 stays in r7; the last seven blocks declare bg in block scope,
 * which hands out their spill slots high to low (function-scope bg gives
 * them low to high; see docs/idioms/function-scope-versus-block-scope-
 * decides-spill-slot-order.md).
 *
 * symbols:
 *   BMP_WIN_Init  0x02045088  arm
 *   BMP_WIN_Create  0x020450F0  arm
 *   MSGDATA_AllocString  0x02045EC0  arm
 *   BMP_WIN_GetBmp  0x02045770  arm
 *   STRBUF_Delete  0x02045808  arm
 *   PRINTSYS_PrintQueColor  0x0201C724  thumb
 *   WORDSET_SetSlotMyStatus  0x0201F02C  thumb
 *   WORDSET_CopyTaggedText  0x0201F250  thumb
 *   WORDSET_SetSlotFromMsg59  0x0201F158  thumb
 *   WORDSET_SetSlotNumber  0x0201EFA4  thumb
 *   MYSTATUS_GetSex  0x02008550  thumb
 *   ZONEDATA_Init  0x020139B0  thumb
 *   ZONEDATA_CheckEventFlag  0x020142E8  thumb
 *   PLAYER_LOCATION_GetData  0x02008854  thumb
 *   PLAYER_LOCATION_GetLocationA  0x02008848  thumb
 *   PLAYER_LOCATION_CopyRecord  0x0200890C  thumb
 *   ZONEDATA_GetPlaceNameId  0x02013E4C  thumb
 *   ZONEDATA_Exit  0x02013A48  thumb
 *   SYSTEM_DATA_GetPlayTime  0x02008748  thumb
 *   PLAYTIME_GetHours  0x0200864C  thumb
 *   PLAYTIME_GetMinutes  0x02008650  thumb
 *   ZUKAN_SAVE_GetBlock  0x0200CA10  thumb
 *   ZUKAN_SAVE_CountSeen  0x0200D0AC  thumb
 *   MISC_GetBlock  0x0200C0F0  thumb
 *   MISC_CountBadges  0x0200C258  thumb
 *   gStartMenuWindowTemplates     0x021BD7C4  data  a1
  */
/* sdk/mod_title.h, spelled out: the overlay link header declares what the
 * file itself does not (docs/hooks.md). */
extern int bw_mod_title_settings_on(void);
/* The widest line of a text in a font (PRINTSYS_MeasureWidestLine). */
extern unsigned int PRINTSYS_MeasureWidestLine(void *text, void *font, unsigned int spacing);
extern int bw_mod_title_in_settings(void);
extern int bw_mod_title_set_in_settings(int on);
#define BW_TITLE_ROW_SETTINGS 7
#define BW_TITLE_SEL_SETTINGS 9
extern int eidos_sui_row_count(void);
extern void eidos_sui_row_text(int row, char *label, int nl, char *value, int nv);
extern int eidos_sui_capturing(void);
extern int eidos_sui_help_count(void);
extern void eidos_sui_help_line(int i, char *out, int n);
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

struct Ent6 {
    u8 a0, a1, a2, a3, a4, a5;
};

struct Slot {
    void *obj;
    u8 on;
};

struct Menu {
    void *f00;
    void *f04;
    u8 pad08[8];
    void *f10;
    u8 pad14[0x58 - 0x14];
    void *f58;
    void *f5c;
    void *f60;
    void *f64;
    void *f68;
    u8 pad6c[0x80 - 0x6c];
    struct Slot slot[14];
};

extern const struct Ent6 gStartMenuWindowTemplates[];

extern void BMP_WIN_Init(int);
extern void *BMP_WIN_Create(int, int, int, int, int, int, int);
extern void *MSGDATA_AllocString(void *, int);
extern void *BMP_WIN_GetBmp(void *);
extern void STRBUF_Delete(void *);
extern void PRINTSYS_PrintQueColor(void *, void *, int, int, void *, void *, int);
extern void WORDSET_SetSlotMyStatus(void *, int, void *);
extern void WORDSET_CopyTaggedText(void *, void *, void *);
extern void WORDSET_SetSlotFromMsg59(void *, int, void *);
extern void WORDSET_SetSlotNumber(void *, int, void *, int, int, int);
extern int MYSTATUS_GetSex(void *);
extern void ZONEDATA_Init(int);
extern int ZONEDATA_CheckEventFlag(void *, int);
extern void *PLAYER_LOCATION_GetData(void *);
extern u16 *PLAYER_LOCATION_GetLocationA(void *);
extern void PLAYER_LOCATION_CopyRecord(void *, void *);
extern void *ZONEDATA_GetPlaceNameId(u16);
extern void ZONEDATA_Exit(void);
extern void *SYSTEM_DATA_GetPlayTime(void *);
extern void *PLAYTIME_GetHours(void *);
extern void *PLAYTIME_GetMinutes(void *);
extern void *ZUKAN_SAVE_GetBlock(void *);
extern void *ZUKAN_SAVE_CountSeen(void *, int);
extern void *MISC_GetBlock(void *);
extern void *MISC_CountBadges(void *);
extern void *STRBUF_Create(unsigned, unsigned);
extern void STRBUF_SetStringN(void *, const unsigned short *, unsigned);
extern unsigned char MISC_GetSlotState(void *, unsigned);

/* The label of kind 7 when it is the SETTINGS row: a string object built
 * from code units (plain ASCII, then the 0xFFFF end code). */
static void *settings_label(void)
{
    static const unsigned short text[9] = {'S', 'E', 'T', 'T', 'I', 'N', 'G', 'S', 0xFFFF};
    void *s = STRBUF_Create(9, 0xb);
    STRBUF_SetStringN(s, text, 9);
    return s;
}

extern void bw_slots_label(char *out, unsigned size);

/* The label of kind 6 (the game's Mic Test): the MULTIPLAYER row. */
static void *multiplayer_label(void)
{
    static const unsigned short text[12] = {'M', 'U', 'L', 'T', 'I', 'P', 'L',
                                            'A', 'Y', 'E', 'R', 0xFFFF};
    void *s = STRBUF_Create(12, 0xb);
    STRBUF_SetStringN(s, text, 12);
    return s;
}

/* A string object from ASCII text (code units, then the 0xFFFF end code). */
static void *ascii_obj(const char *t, int n)
{
    u16 buf[64];
    void *o;
    int i;
    for (i = 0; i < n && i < 62; i++)
        buf[i] = (u8)t[i];
    buf[i] = 0xFFFF;
    o = STRBUF_Create(i + 1, 0xb);
    STRBUF_SetStringN(o, buf, i + 1);
    return o;
}

#define SETTINGS_SLOT_TILES 22
#define SETTINGS_TEXT_X 6
#define SETTINGS_TEXT_RIGHT (SETTINGS_SLOT_TILES * 8 - 6)

/* One text on a row's slot, at x or (right >= 0) ending at `right`; the
 * text is cut at the end until it is at most `room` pixels wide. Returns
 * the width drawn. */
static int settings_text_y(struct Menu *p, int slot, const char *t, int x, int right, int room, int y)
{
    int n = 0;
    void *o;
    int w;
    while (t[n])
        n++;
    if (n == 0)
        return 0;
    o = ascii_obj(t, n);
    w = PRINTSYS_MeasureWidestLine(o, p->f58, 0);
    while (w > room && n > 1) {
        STRBUF_Delete(o);
        n--;
        o = ascii_obj(t, n);
        w = PRINTSYS_MeasureWidestLine(o, p->f58, 0);
    }
    if (right >= 0)
        x = right - w;
    PRINTSYS_PrintQueColor(p->f68, BMP_WIN_GetBmp(p->slot[slot].obj), x, y, o, p->f58, 0x440);
    p->slot[slot].on = 1;
    STRBUF_Delete(o);
    return w;
}

static int settings_text(struct Menu *p, int slot, const char *t, int x, int right, int room)
{
    return settings_text_y(p, slot, t, x, right, room, 4);
}

/* The bottom screen: the help text of the highlighted row, in slot 13 (a
 * window on the lower screen's first layer, see the table patch below). */
#define SETTINGS_HELP_SLOT 13
#define SETTINGS_HELP_PX (30 * 8 - 8)
extern void BMP_Clear(void *win, unsigned c);
extern void BMP_WIN_TransferChars(void *win);
extern void BMP_WIN_MakeScreen(void *win);
extern void BG_SYS_MarkTileDirty(unsigned bit);
extern unsigned char BMP_WIN_GetBgLayer(void *win);
static void settings_help(struct Menu *p)
{
    char line[96];
    int i, n = eidos_sui_help_count();
    BMP_Clear(BMP_WIN_GetBmp(p->slot[SETTINGS_HELP_SLOT].obj), 0xF);
    for (i = 0; i < n; i++) {
        eidos_sui_help_line(i, line, sizeof line);
        if (line[0] != 0)
            settings_text_y(p, SETTINGS_HELP_SLOT, line, 4, -1, SETTINGS_HELP_PX, 2 + 16 * i + (i >= 2 ? 8 : 0));
    }
    BMP_WIN_TransferChars(p->slot[SETTINGS_HELP_SLOT].obj);
    BMP_WIN_MakeScreen(p->slot[SETTINGS_HELP_SLOT].obj);
    BG_SYS_MarkTileDirty(BMP_WIN_GetBgLayer(p->slot[SETTINGS_HELP_SLOT].obj));
}

static void settings_print(struct Menu *p)
{
    char label[48];
    char value[96];
    int i, n = eidos_sui_row_count();
    for (i = 0; i < n && i < 7; i++) {
        int slot = i + 6;
        int lw;
        eidos_sui_row_text(i, label, sizeof label, value, sizeof value);
        lw = settings_text(p, slot, label, SETTINGS_TEXT_X, -1, SETTINGS_TEXT_RIGHT - SETTINGS_TEXT_X);
        if (value[0] != 0)
            settings_text(p, slot, value, 0, SETTINGS_TEXT_RIGHT, SETTINGS_TEXT_RIGHT - SETTINGS_TEXT_X - lw - 10);
    }
    settings_help(p);
}

void STARTMENU_CreateRowWindows(struct Menu *p)
{
    u32 i;
    void *a;
    void *s;
    void *b;
    void *bg;
    u16 buf[14];

    BMP_WIN_Init(11);
    for (i = 0; i < 14; i++) {
        int w = gStartMenuWindowTemplates[i].a3;
        int layer = gStartMenuWindowTemplates[i].a0, h = gStartMenuWindowTemplates[i].a4, y = gStartMenuWindowTemplates[i].a2;
        int x = gStartMenuWindowTemplates[i].a1, pal = gStartMenuWindowTemplates[i].a5;
        if (bw_mod_title_settings_on() && bw_mod_title_in_settings()) {
            if (i >= 6 && i <= 12)
                w = SETTINGS_SLOT_TILES;
            if (i == SETTINGS_HELP_SLOT) {
                layer = 4; x = 1; y = 2; w = 30; h = 10; pal = 15;
            }
        }
        p->slot[i].obj = BMP_WIN_Create(layer, x, y, w, h, pal, 1);
    }
    if (bw_mod_title_settings_on() && bw_mod_title_in_settings()) {
        settings_print(p);
        return;
    }

    a = MSGDATA_AllocString(p->f5c, 0);
    bg = p->f68;
    PRINTSYS_PrintQueColor(bg, BMP_WIN_GetBmp(p->slot[0].obj), 0, 4, a, p->f58, 0x440);
    p->slot[0].on = 1;
    STRBUF_Delete(a);
    if (bw_mod_title_settings_on()) {
        /* the save slot on the Continue card: "< 2/3 >", left and right
         * change it (sdk/save_slots.h) */
        char label[16];
        int right = gStartMenuWindowTemplates[0].a3 * 8 - 6;

        bw_slots_label(label, sizeof label);
        if (label[0] != 0)
            settings_text(p, 0, label, 0, right, right / 2);
    }

    a = MSGDATA_AllocString(p->f5c, 12);
    WORDSET_SetSlotMyStatus(p->f60, 0, p->f04);
    WORDSET_CopyTaggedText(p->f60, p->f64, a);
    if (MYSTATUS_GetSex(p->f04) == 0) {
        bg = p->f68;
        PRINTSYS_PrintQueColor(bg, BMP_WIN_GetBmp(p->slot[1].obj), 0, 0, p->f64, p->f58, 0xC80);
        p->slot[1].on = 1;
    } else {
        bg = p->f68;
        PRINTSYS_PrintQueColor(bg, BMP_WIN_GetBmp(p->slot[1].obj), 0, 0, p->f64, p->f58, 0x14C0);
        p->slot[1].on = 1;
    }
    STRBUF_Delete(a);

    a = MSGDATA_AllocString(p->f5c, 16);
    ZONEDATA_Init(0x800B);
    if (ZONEDATA_CheckEventFlag(p->f10, 0x966) == 1) {
        s = ZONEDATA_GetPlaceNameId(PLAYER_LOCATION_GetLocationA(PLAYER_LOCATION_GetData(p->f00))[2]);
    } else {
        PLAYER_LOCATION_CopyRecord(p->f00, buf);
        s = ZONEDATA_GetPlaceNameId(buf[0]);
    }
    ZONEDATA_Exit();
    WORDSET_SetSlotFromMsg59(p->f60, 0, s);
    WORDSET_CopyTaggedText(p->f60, p->f64, a);
    bg = p->f68;
    PRINTSYS_PrintQueColor(bg, BMP_WIN_GetBmp(p->slot[2].obj), 0, 0, p->f64, p->f58, 0x440);
    p->slot[2].on = 1;
    STRBUF_Delete(a);

    a = MSGDATA_AllocString(p->f5c, 9);
    b = SYSTEM_DATA_GetPlayTime(p->f00);
    WORDSET_SetSlotNumber(p->f60, 0, PLAYTIME_GetHours(b), 3, 0, 1);
    WORDSET_SetSlotNumber(p->f60, 1, PLAYTIME_GetMinutes(b), 2, 2, 1);
    WORDSET_CopyTaggedText(p->f60, p->f64, a);
    bg = p->f68;
    PRINTSYS_PrintQueColor(bg, BMP_WIN_GetBmp(p->slot[3].obj), 0, 0, p->f64, p->f58, 0x440);
    p->slot[3].on = 1;
    STRBUF_Delete(a);

    if (ZONEDATA_CheckEventFlag(p->f10, 0x962) == 1) {
        a = MSGDATA_AllocString(p->f5c, 10);
        WORDSET_SetSlotNumber(p->f60, 0, ZUKAN_SAVE_CountSeen(ZUKAN_SAVE_GetBlock(p->f00), 11), 3, 0, 1);
        WORDSET_CopyTaggedText(p->f60, p->f64, a);
        bg = p->f68;
        PRINTSYS_PrintQueColor(bg, BMP_WIN_GetBmp(p->slot[4].obj), 0, 0, p->f64, p->f58, 0x440);
        p->slot[4].on = 1;
        STRBUF_Delete(a);
    }

    a = MSGDATA_AllocString(p->f5c, 11);
    WORDSET_SetSlotNumber(p->f60, 0, MISC_CountBadges(MISC_GetBlock(p->f00)), 2, 0, 1);
    WORDSET_CopyTaggedText(p->f60, p->f64, a);
    bg = p->f68;
    PRINTSYS_PrintQueColor(bg, BMP_WIN_GetBmp(p->slot[5].obj), 0, 0, p->f64, p->f58, 0x440);
    p->slot[5].on = 1;
    STRBUF_Delete(a);

    a = MSGDATA_AllocString(p->f5c, 1);
    {
        void *bg = p->f68;
        PRINTSYS_PrintQueColor(bg, BMP_WIN_GetBmp(p->slot[6].obj), 0, 4, a, p->f58, 0x440);
    }
    p->slot[6].on = 1;
    STRBUF_Delete(a);

    a = MSGDATA_AllocString(p->f5c, 2);
    {
        void *bg = p->f68;
        PRINTSYS_PrintQueColor(bg, BMP_WIN_GetBmp(p->slot[7].obj), 0, 4, a, p->f58, 0x440);
    }
    p->slot[7].on = 1;
    STRBUF_Delete(a);

    a = MSGDATA_AllocString(p->f5c, 4);
    {
        void *bg = p->f68;
        PRINTSYS_PrintQueColor(bg, BMP_WIN_GetBmp(p->slot[8].obj), 0, 4, a, p->f58, 0x440);
    }
    p->slot[8].on = 1;
    STRBUF_Delete(a);

    a = MSGDATA_AllocString(p->f5c, 5);
    {
        void *bg = p->f68;
        PRINTSYS_PrintQueColor(bg, BMP_WIN_GetBmp(p->slot[9].obj), 0, 4, a, p->f58, 0x440);
    }
    p->slot[9].on = 1;
    STRBUF_Delete(a);

    a = MSGDATA_AllocString(p->f5c, 3);
    {
        void *bg = p->f68;
        PRINTSYS_PrintQueColor(bg, BMP_WIN_GetBmp(p->slot[10].obj), 0, 4, a, p->f58, 0x440);
    }
    p->slot[10].on = 1;
    STRBUF_Delete(a);

    if (bw_mod_title_settings_on())
        a = multiplayer_label();
    else
        a = MSGDATA_AllocString(p->f5c, 7);
    {
        void *bg = p->f68;
        PRINTSYS_PrintQueColor(bg, BMP_WIN_GetBmp(p->slot[11].obj), 0, 4, a, p->f58, 0x440);
    }
    p->slot[11].on = 1;
    STRBUF_Delete(a);

    if (bw_mod_title_settings_on() && !MISC_GetSlotState(*(void **)((char *)p + 8), 3))
        a = settings_label();
    else
        a = MSGDATA_AllocString(p->f5c, 6);
    {
        void *bg = p->f68;
        PRINTSYS_PrintQueColor(bg, BMP_WIN_GetBmp(p->slot[12].obj), 0, 4, a, p->f58, 0x440);
    }
    p->slot[12].on = 1;
    STRBUF_Delete(a);
}
