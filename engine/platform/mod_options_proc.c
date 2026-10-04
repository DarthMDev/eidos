/*
 * engine/platform/mod_options_proc.c -- the settings screen, drawn the way the
 * game draws its own Options screen.
 *
 * It is a game process (the {init, main, end} table the process manager runs,
 * src/matched/arm9/func_020315F0.c) written in port C that calls the game's own
 * graphics, window, text and sprite functions.  The set-up follows the Options
 * screen of overlay 68 (asm/overlays/ov068.s, func_021B9980 / func_021B9850,
 * and the matched func_021B9E28 for the sprites): the same VRAM banks, the same
 * six background layers, the same graphics archive (a/0/2/7, "arc 0x1B") with
 * the same members, bars 24 pixels apart, and the same sprite bank.  Nothing
 * of the game's art is in this file: it is loaded from the player's own data
 * at run time.
 *
 * The title menu starts it (stubs/ov/reimpl_ov088_func_021BA014.c) and it hands
 * back to the title menu when the player leaves.  What the screen shows and how
 * its buttons behave is engine/platform/settings_ui.c; this file only draws.
 *
 * Layout (docs/settings-design.md): the main screen is a list of six bars, the
 * sixth the grey one; the other screen is the dark grid with the help text of
 * the highlighted row.
 */

#include <stdio.h>
#include <string.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef signed short s16;

/* The ARM9 matched files spell some functions sub_<addr>; the generated
 * header of overlay 88 (ovl/boot/include/ov088.h) renames the calls, and so
 * does this list.  The title menu calls all of these. */
#define func_020056A0 sub_020056A0
#define func_020061E4 sub_020061E4
#define func_0201C440 sub_0201C440
#define func_0201C4C0 sub_0201C4C0
#define func_0201C724 sub_0201C724
#define func_0201D304 sub_0201D304
#define func_0203064C sub_0203064C
#define func_02030EAC sub_02030EAC
#define func_0203101C sub_0203101C
#define func_020310C4 sub_020310C4
#define func_02031140 sub_02031140
#define func_020315D4 sub_020315D4
#define func_020362DC sub_020362DC
#define func_0203633C sub_0203633C
#define func_02040588 sub_02040588
#define func_020409B4 sub_020409B4
#define func_02041B6C sub_02041B6C
#define func_0204335C sub_0204335C
#define func_020434DC sub_020434DC
#define func_02043534 sub_02043534
#define func_02043598 sub_02043598
#define func_020435F4 sub_020435F4
#define func_02043B5C sub_02043B5C
#define func_02045088 sub_02045088
#define func_020450C8 sub_020450C8
#define func_020450F0 sub_020450F0
#define func_020452E8 sub_020452E8
#define func_02045334 sub_02045334
#define func_02045374 sub_02045374
#define func_02045730 sub_02045730
#define func_02045770 sub_02045770
#define func_020457B0 sub_020457B0
#define func_02045808 sub_02045808
#define func_020490F4 sub_020490F4
#define func_02049658 sub_02049658
#define func_020498F4 sub_020498F4
#define func_02049B40 sub_02049B40
#define func_02049B68 sub_02049B68
#define func_0204A48C sub_0204A48C
#define func_0204A5AC sub_0204A5AC
#define func_0204A6C8 sub_0204A6C8
#define func_0204A8D4 sub_0204A8D4
#define func_0204ABF0 sub_0204ABF0
#define func_0204ADA4 sub_0204ADA4
#define func_0204AF28 sub_0204AF28
#define func_0204AFD8 sub_0204AFD8
#define func_0204B100 sub_0204B100
#define func_0204B1CC sub_0204B1CC
#define func_0204B270 sub_0204B270
#define func_0204B294 sub_0204B294
#define func_0204B3B4 sub_0204B3B4
#define func_0204B404 sub_0204B404
#define func_0204B454 sub_0204B454
#define func_0204B6F4 sub_0204B6F4
#define func_020434CC sub_020434CC
#define func_0204B92C sub_0204B92C

/* ---- the game's functions (ARM9, all matched; see the symbol tables) ---- */
extern void func_0203064C(u32, u32, u32);
extern void *func_0203159C(void *, u32, u32);
extern void func_020315D4(void *);
extern void func_020306F0(u32);
extern void func_020312F8(int, const void *, void *);
extern void func_02082BCC(void *, int, int);

extern void func_0204335C(u32);
extern void func_020433E0(const void *);
extern void func_0204363C(u32);
extern void func_0204361C(void);
extern void func_020434CC(void);
extern void func_02043588(void);
extern void func_020434DC(u32, u32);
extern void func_02043598(u32, u32);
extern void func_0201DD60(void);
extern void func_0201DD78(u32, u32, u32);
extern void func_0207D534(volatile u16 *, int);
extern void func_02043534(u32);
extern void func_020435F4(u32);

extern void func_0203F8F4(u32);
extern void func_0203FC28(const void *);
extern void func_0203FCA0(int, const void *, int);
extern void func_020414C4(u32);
extern void func_020414EC(u32);
extern void func_020409B4(u32);
extern void func_02040588(u32, u32);
extern void func_02040C14(u32, u32, u32, u32);
extern void func_02041B6C(u32);
extern void func_020419E4(void);
extern void func_020450C8(void);
extern void func_0203F9B4(void);

extern void *func_020490F4(u32, u32);
extern void func_02049238(void *);
extern void func_02049B68(void *, u32, u32, u32, u32, u32);
extern void func_02049BDC(void *, u32, u32, u32, u32, u32, u32);
extern void func_02049B40(u32, u32, u32, u32, u32, u32);
extern void func_02049658(void *, u32, u32, u32, u32, u32, u32);
extern void func_020498F4(void *, u32, u32, u32, u32, u32, u32);

extern void func_02045088(u32);
extern u32 func_020450F0(u32, u32, u32, u32, u32, u32, u32);
extern void *func_02045770(u32);
extern void func_02043B5C(void *, u32);
extern void func_02045374(u32);
extern void func_02045334(u32);
extern u8 func_02045730(u32);
extern void func_020452E8(u32);
extern void *func_020457B0(u32, u32);
extern void func_02045994(void *, const u16 *, u32);
extern void func_02045808(void *);
extern u32 func_0201D7EC(int, int, int, int, u32);
extern void func_0201D83C(u32);
extern u32 func_0201C440(u32);
extern void func_0201C4C0(u32);
extern void func_0201C724(u32, void *, int, int, void *, u32, u32);
extern u32 func_0201D304(void *, u32, u32);

extern u32 func_0203101C(u32, u32, u32, u32);
extern void func_020310C4(u32);
extern void func_02030EAC(u32);
extern void func_02031140(u32);
extern u32 func_020056A0(void (*)(u32, u32), u32, u32);

extern u32 func_020362DC(void);
extern u32 func_0203633C(void);
extern void func_020061E4(u32);
extern void func_0204E720(u32, int, int, int);
extern int func_0204E804(void);

/* the sprite system's per-frame work (the screen has no sprites of its own,
 * but the game's objects, if any, are drawn and sent with these) */
extern void func_0204A600(void);
extern void func_0204A648(void);

/* ---- the port ---- */
extern void *bw_ds_ptr(u32);
extern void bw_io_write16(u32, u16);
extern void bw_io_write32(u32, u32);
extern u32 bw_io_read32(u32);
extern int bw_mod_title_return_ov(void);
extern const void *bw_mod_title_return_table(void);

extern void eidos_sui_enter(void);
extern void eidos_sui_leave(void);
extern int eidos_sui_input(unsigned pressed, unsigned held);
extern int eidos_sui_row_count(void);
extern void eidos_sui_row_text(int row, char *label, int nl, char *value, int nv);
extern int eidos_sui_cursor_row(void);
extern int eidos_sui_row_choices(int row, int *active, char names[3][20]);
extern int eidos_sui_help_count(void);
extern void eidos_sui_help_line(int i, char *out, int n);

#define OPT_HEAP 0xb
#define OPT_HEAP_SIZE 0x80000u
#define OPT_ROWS 6              /* bars on the list screen */

/* DISPCNT / DISPSTAT style registers are written through the port's I/O. */
#define REG_DISPCNT_MAIN 0x04000000u
#define REG_DISPCNT_SUB  0x04001000u

/* VRAM banks of the Options screen (d_021BB598, overlay 68): the fields of
 * struct DispVram in src/matched/arm9/func_020433E0.c. */
static const u32 k_banks[12] = {
    1, 0, 4, 0, 2, 0, 0x100, 0, 0, 0, 0x200010, 0x200010
};

/* d_021BB540: the background system's mode, the same four words as the title's. */
static const u32 k_bgmode[4] = { 1, 0, 0, 0 };

/* The six layers (d_021BB614, overlay 68): the first word is the layer, then
 * the eight words func_0203FCA0 takes, then its mode. */
typedef struct LayerCfg {
    int layer;
    u32 cfg[8];
    int mode;
} LayerCfg;

static const LayerCfg k_layers[6] = {
    { 0, { 0, 0, 0x800, 0, 0x1000001, 0x8000, 0x100, 0 }, 0 },
    { 1, { 0, 0, 0x1000, 0, 0x2010002, 0x8000, 0x200, 0 }, 0 },
    { 2, { 0, 0, 0x1000, 0, 0x4030002, 0x8000, 0x300, 0 }, 0 },
    { 3, { 0, 0, 0x800, 0, 0x5050001, 0x8000, 0, 0 }, 0 },
    { 4, { 0, 0, 0x800, 0, 0x1000001, 0x8000, 0, 0 }, 0 },
    { 5, { 0, 0, 0x800, 0, 0x2010001, 0x8000, 0, 0 }, 0 },
};

/* Where the Options screen puts the choices of a row (d_021BB704 and the text
 * positions beside it): three choices at 112, 160 and 208, two at 112 and 184;
 * eidos's names are longer than SLOW, MID and FAST, so three sit at 100, 148 and
 * 196. */
static const int k_text3[3] = { 100, 148, 196 };
static const int k_text2[2] = { 112, 184 };

typedef struct OptWork {
    u32 heap;
    u32 tcb;                    /* task list, run every frame */
    u32 vtask;                  /* the vblank task */
    u32 font, print;
    u32 win_head, win_row[OPT_ROWS], win_help;
    int built;
    int state_dirty;
    int ending;
} OptWork;

static OptWork *g_w;

/* ---- text ---- */

static void *ascii_obj(const char *t, int n)
{
    u16 buf[96];
    void *o;
    int i;

    for (i = 0; i < n && i < 94; i++) {
        buf[i] = (u8)t[i];
    }
    buf[i] = 0xFFFF;
    o = func_020457B0((u32)(i + 1), OPT_HEAP);
    func_02045994(o, buf, (u32)(i + 1));
    return o;
}

/* One text in a window: at x, or ending at `right` when right >= 0; cut at
 * the end until it fits `room` pixels.  Returns the width drawn. */
static int put_text(u32 win, const char *t, int x, int right, int room, int y,
                    u32 fg, u32 shadow)
{
    int n = 0, w;
    void *o;

    while (t[n]) {
        n++;
    }
    if (n == 0) {
        return 0;
    }
    o = ascii_obj(t, n);
    w = (int)func_0201D304(o, g_w->font, 0);
    while (w > room && n > 1) {
        func_02045808(o);
        n--;
        o = ascii_obj(t, n);
        w = (int)func_0201D304(o, g_w->font, 0);
    }
    if (right >= 0) {
        x = right - w;
    }
    func_0201DD78(fg, shadow, 0);       /* the text colours, baked in as it prints */
    func_0201C724(g_w->print, func_02045770(win), x, y, o, g_w->font, 0x440);
    func_02045808(o);
    return w;
}

static void win_clear_and_commit_begin(u32 win)
{
    func_02043B5C(func_02045770(win), 0);
}

static void win_commit(u32 win)
{
    func_02045334(win);
    func_02045374(win);
    func_02041B6C(func_02045730(win));
}

/* The list screen: bar i holds row i + 1 of the model (row 0 is the header). */
static void draw_rows(void)
{
    char label[48], value[96];
    int i, n = eidos_sui_row_count();

    eidos_sui_row_text(0, label, sizeof label, value, sizeof value);
    win_clear_and_commit_begin(g_w->win_head);
    put_text(g_w->win_head, label, 4, -1, 16 * 8 - 8, 0, 0xf, 0xe);
    win_commit(g_w->win_head);

    /* Bars 0..4 are the option rows (model rows 1..n-2); bar 5, the grey
     * one, is the model's last row (Done or Back). */
    for (i = 0; i < OPT_ROWS; i++) {
        u32 win = g_w->win_row[i];
        int mrow = i == OPT_ROWS - 1 ? n - 1 : i + 1;
        int have = i == OPT_ROWS - 1 ? 1 : mrow < n - 1;

        char names[3][20];
        int active = 0, nch = 0, c;
        u32 fg = 0xf, sh = 0xe;   /* white, dark shadow: arc 0x17's palette */

        win_clear_and_commit_begin(win);
        if (have) {
            eidos_sui_row_text(mrow, label, sizeof label, value, sizeof value);
            put_text(win, label, 8, -1, 100, 4, fg, sh);
            if (i < OPT_ROWS - 1) {
                nch = eidos_sui_row_choices(mrow, &active, names);
            }
            if (nch > 0) {
                for (c = 0; c < nch; c++) {
                    /* the active choice in the bright colour, the others dim */
                    put_text(win, names[c], nch == 3 ? k_text3[c] : k_text2[c],
                             -1, 60, 4,
                             c == active ? fg : 0x2, sh);   /* the other choices: light grey */
                }
            } else if (value[0] != 0) {
                put_text(win, value, 120, -1, 240 - 120 - 8, 4, fg, sh);
            }
        }
        win_commit(win);
    }
}

static void draw_help(void)
{
    char line[96];
    int i, n = eidos_sui_help_count();

    win_clear_and_commit_begin(g_w->win_help);
    for (i = 0; i < n && i < 2; i++) {
        eidos_sui_help_line(i, line, sizeof line);
        if (line[0] != 0) {
            put_text(g_w->win_help, line, 4, -1, 30 * 8 - 8, 2 + 16 * i, 0xf, 0xe);
        }
    }
    win_commit(g_w->win_help);
}

/*
 * The selected bar: the game marks it with a sprite whose colours a palette
 * animation drives (func_021B9D68).  Here the bar's tile map goes to a
 * deeper copy of its palette instead: palette bank 9 of the main
 * background, made from bank 2 (the bars) with the red and green darkened.
 */
extern void func_020413B0(u32, u32, u32, u32, u32, u32);

/*
 * The colours.  The game's Options art is purple and green; the title menu this
 * screen is reached from is near-black with light borders and a teal
 * selection.  The art keeps its shapes and the palettes are set to the title
 * menu's colours (measured on its rows: fill 24/24/24, border 238 with 98 shading,
 * selected fill 57/65/82 with a teal 8/148/139 border).  Set directly in
 * the palette memory after the game has loaded the originals.
 */
static u16 rgb8(u32 r, u32 g, u32 b)
{
    return (u16)((r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10));
}

/* The bar palette (banks 0 and 2 of the main background): index 4 is the
 * fill, 5 and 1 the inner lines, 3, 9 and 15 the border and its end ticks,
 * 6 and 14 the border's shading, 7, 8 and 10 the dark edge. */
static void theme_bar(u16 *bank, u32 fill, const u32 line[3], const u32 shade[3])
{
    bank[4] = rgb8(fill, fill, fill);
    bank[5] = rgb8(16, 16, 16);
    bank[1] = rgb8(41, 41, 41);
    bank[2] = rgb8(57, 57, 57);
    bank[10] = rgb8(24, 24, 24);
    bank[7] = rgb8(16, 16, 16);
    bank[8] = rgb8(41, 41, 41);
    bank[3] = rgb8(line[0], line[1], line[2]);
    bank[9] = rgb8(line[0], line[1], line[2]);
    bank[15] = rgb8(line[0], line[1], line[2]);
    bank[6] = rgb8(shade[0], shade[1], shade[2]);
    bank[14] = rgb8(shade[0], shade[1], shade[2]);
}

static void make_palettes(void)
{
    static const u32 white[3] = { 238, 238, 238 }, grey[3] = { 98, 98, 98 };
    static const u32 teal[3] = { 8, 148, 139 }, deep[3] = { 6, 100, 95 };
    u16 *main_pal = (u16 *)bw_ds_ptr(0x05000000u);
    u16 *sub_pal = (u16 *)bw_ds_ptr(0x05000400u);
    int i;

    theme_bar(main_pal + 0 * 16, 24, white, grey);   /* the grey bar */
    theme_bar(main_pal + 2 * 16, 24, white, grey);   /* the bars */
    theme_bar(main_pal + 9 * 16, 24, teal, deep);    /* the selected bar */
    main_pal[9 * 16 + 4] = rgb8(57, 65, 82);
    /* bank 13 is for a bar with no row: all of it the backdrop's colour */
    for (i = 0; i < 16; i++) {
        main_pal[13 * 16 + i] = rgb8(32, 32, 32);
    }
    /* the grid of the other screen: neutral crosses on black */
    sub_pal[1 * 16 + 2] = rgb8(41, 41, 41);
    sub_pal[1 * 16 + 3] = rgb8(24, 24, 24);
    /* the text, in both screens' bank 15: 15 the text, 14 its shadow, 2 dimmer */
    for (i = 0; i < 16; i++) {
        main_pal[15 * 16 + i] = 0;
        sub_pal[15 * 16 + i] = 0;
    }
    main_pal[15 * 16 + 15] = sub_pal[15 * 16 + 15] = rgb8(255, 255, 255);
    main_pal[15 * 16 + 14] = sub_pal[15 * 16 + 14] = rgb8(66, 66, 74);
    main_pal[15 * 16 + 2] = sub_pal[15 * 16 + 2] = rgb8(139, 139, 139);
}

static void highlight(OptWork *w)
{
    int cr = eidos_sui_cursor_row(), bar, i;

    bar = cr == eidos_sui_row_count() - 1 ? OPT_ROWS - 1 : cr - 1;
    if (!w->built) {
        make_palettes();
        w->built = 1;
    }
    for (i = 0; i < OPT_ROWS; i++) {
        /* bars 0..4 use palette bank 2, the grey bar bank 0 (the screen map);
         * a bar with no row takes the empty bank 13, so it is not drawn */
        u32 pal = i == OPT_ROWS - 1 ? 0u : 2u;

        if (i < OPT_ROWS - 1 && i >= eidos_sui_row_count() - 2) {
            pal = 13u;
        }
        if (i == bar) {
            pal = 9u;
        }
        func_020413B0(2, 0, (u32)(2 + 3 * i), 32, 3, pal);
    }
    func_02041B6C(2);
}

/* ---- the screen ---- */

static void vblank_task(u32 task, u32 arg)
{
    (void)task;
    (void)arg;
    func_020419E4();
    func_0204A648();
}

static void gfx_build(OptWork *w)
{
    void *h;
    u32 heap = OPT_HEAP;
    int i;

    func_0203F8F4(heap);
    func_02045088(heap);
    func_0203FC28(k_bgmode);
    for (i = 0; i < 6; i++) {
        func_0203FCA0(k_layers[i].layer, k_layers[i].cfg, k_layers[i].mode);
        func_020414C4((u32)k_layers[i].layer);   /* func_020414AC: an asm body the boot cannot link, */
        func_020414EC((u32)k_layers[i].layer);   /* it is exactly these two calls */
        func_02040588((u32)k_layers[i].layer, 1);
    }
    func_02040588(3, 0);
    func_02040C14(4, 0, 1, 0);
    func_02040C14(3, 0, 1, 0);

    h = func_020490F4(0x1b, heap);
    func_02049B68(h, 1, 0, 0, 0, heap);                 /* main palette */
    func_02049658(h, 0, 2, 0, 0, 0, heap);              /* layer 2: the bars */
    func_020498F4(h, 2, 2, 0, 0, 0, heap);
    func_02049658(h, 0x10, 3, 1, 0, 0, heap);           /* layer 3: text tiles */
    func_02049B68(h, 8, 4, 0, 0, heap);                 /* sub palette */
    func_02049658(h, 7, 5, 0, 0, 0, heap);              /* layer 5: the grid */
    func_020498F4(h, 9, 5, 0, 0, 0, heap);
    func_02049658(h, 0x10, 4, 1, 0, 0, heap);           /* layer 4: text tiles */
    func_02049238(h);

    w->win_help = func_020450F0(4, 1, 0x13, 0x1e, 4, 0xf, 1);
    w->win_head = func_020450F0(0, 0, 0, 0x10, 3, 0xf, 1);
    for (i = 0; i < OPT_ROWS; i++) {
        w->win_row[i] = func_020450F0(3, 1, (u32)(2 + 3 * i), 0x1e, 3, 0xf, 1);
    }
    for (i = 0; i < OPT_ROWS; i++) {
        func_02043B5C(func_02045770(w->win_row[i]), 0);
    }
    func_02043B5C(func_02045770(w->win_help), 0);
    func_02043B5C(func_02045770(w->win_head), 0);

    w->font = func_0201D7EC(0x17, 0, 0, 0, heap);
    w->print = func_0201C440(heap);
    func_020409B4(0);
    func_020409B4(2);
    func_020409B4(3);
    func_020409B4(4);
    func_020409B4(5);
}

static void screen_build(OptWork *w)
{
    func_0207D534((volatile u16 *)bw_ds_ptr(0x0400006Cu), -16);
    func_0207D534((volatile u16 *)bw_ds_ptr(0x0400106Cu), -16);
    func_02043534(0);
    func_020435F4(0);
    bw_io_write16(0x04000050, 0);
    bw_io_write16(0x04001050, 0);
    bw_io_write32(REG_DISPCNT_MAIN, bw_io_read32(REG_DISPCNT_MAIN) & 0xFFFF1FF7u);
    bw_io_write32(REG_DISPCNT_SUB, bw_io_read32(REG_DISPCNT_SUB) & 0xFFFF1FF7u);
    func_0204335C(0);
    func_020433E0(k_banks);
    func_0204363C(0);
    func_0204361C();
    func_020434CC();
    func_02043588();
    func_0201DD60();
    func_0201DD78(0xf, 0xe, 0);
    w->tcb = func_0203101C(OPT_HEAP, OPT_HEAP, 4, 0x20);
    w->vtask = func_020056A0(vblank_task, 0, 0);
    gfx_build(w);
    draw_rows();
    draw_help();
    /* func_020434DC and func_02043598 return early when any bit of the mask is
     * already set, so the planes go on one at a time, as the game does. */
    func_02040588(1, 0);            /* layer 1 has no graphics */
    func_02040588(3, 1);            /* the text layer */
    func_02040588(4, 1);            /* the other screen's text layer */
    func_0204E720(3, 0x10, 0, 2);   /* fade in */
}

static void screen_free(OptWork *w)
{
    int i;

    func_020452E8(w->win_help);
    func_020452E8(w->win_head);
    for (i = 0; i < OPT_ROWS; i++) {
        func_020452E8(w->win_row[i]);
    }
    func_0201C4C0(w->print);
    func_0201D83C(w->font);
    func_0204363C(1);
    func_0204361C();
    func_02030EAC(w->vtask);
    func_02031140(w->tcb);
    func_020450C8();
    func_0203F9B4();
}

/* ---- the process ---- */

static u32 opt_init(void *proc, u32 *seq, u32 pwork, u32 work)
{
    OptWork *w;

    (void)seq;
    (void)pwork;
    (void)work;
    func_0203064C(1, OPT_HEAP, OPT_HEAP_SIZE);
    w = (OptWork *)func_0203159C(proc, (u32)sizeof(OptWork), OPT_HEAP);
    func_02082BCC(w, 0, (int)sizeof(OptWork));
    w->heap = OPT_HEAP;
    g_w = w;
    eidos_sui_enter();
    return 1;
}

static u32 opt_main(void *proc, u32 *seq, u32 pwork, u32 work)
{
    OptWork *w = g_w;

    (void)proc;
    (void)pwork;
    (void)work;
    switch (*seq) {
    case 0:
        screen_build(w);
        *seq = 1;
        break;
    case 1:
        highlight(w);               /* the palette has reached the screen by now */
        if (func_0204E804() == 0) {
            *seq = 2;
        }
        break;
    case 2: {
        int r = eidos_sui_input(func_020362DC(), func_0203633C());

        if (r & 0x10) {
            func_020061E4(0x548);
        }
        if (r & 0x20) {
            func_020061E4(0x54c);
        }
        if (r & 0x40) {
            func_020061E4(0x551);
        }
        if (r & 1) {
            draw_rows();
            draw_help();
        }
        highlight(w);
        if (r & 2) {
            func_0204E720(3, 0, 0x10, 2);
            *seq = 3;
        }
        break;
    }
    case 3:
        if (func_0204E804() == 0) {
            screen_free(w);
            return 1;
        }
        break;
    }
    func_020310C4(w->tcb);
    func_0204A600();
    return 0;
}

static u32 opt_end(void *proc, u32 *seq, u32 pwork, u32 work)
{
    (void)seq;
    (void)pwork;
    (void)work;
    eidos_sui_leave();
    func_020315D4(proc);
    func_020306F0(OPT_HEAP);
    g_w = NULL;
    /* back to the title menu, loaded again like the game loads it */
    func_020312F8(bw_mod_title_return_ov(), bw_mod_title_return_table(), NULL);
    return 1;
}

typedef u32 (*ProcFn)(void *, u32 *, u32, u32);

static const struct {
    ProcFn init, main, end;
} k_proc = { opt_init, opt_main, opt_end };

const void *eidos_opt_proc_table(void)
{
    return &k_proc;
}
