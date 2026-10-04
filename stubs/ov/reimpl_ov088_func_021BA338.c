/*
 * function: func_021BA338
 * address:  0x021BA338
 * packet:   pkt_ov088_021BA338
 * region:   ov088
 * compiler: dsi/1.1p1
 * flags:    -O4,p
 * lang:     c99
 * isa:      thumb
 * replaces: src/matched/ov/ov088_func_021BA338.c
 * port hook "title_settings" (docs/hooks.md, docs/settings-design.md):
 * the menu cursor state. A on the SETTINGS row (kind 7 while the game's own row
 * is off) opens the settings screen: selection 9, which the exit function maps to
 * the menu proc started again in settings mode. In settings mode this state is
 * the settings screen's input: eidos_sui_input decides, a change rebuilds the
 * rows (same set-up calls as state 0, minus the sprites) and waits in state 4
 * for the text, B leaves like the game's own B.
 *
 * With the hook off this is the matched function, unchanged.
 *
 * original status:   EXACT
 *
 * symbols:
 *   func_0202EA40  0x0202EA40  thumb
 *   func_020362DC  0x020362DC  arm
 *   func_020061E4  0x020061E4  thumb
 *   func_021BC0AC  0x021BC0AC  thumb  ov088
 *   func_0200C2F0  0x0200C2F0  thumb
 *   func_021BC080  0x021BC080  thumb  ov088
 *   func_0203633C  0x0203633C  arm
 *   func_021BB890  0x021BB890  thumb  ov088
 *   func_021BB474  0x021BB474  thumb  ov088
 *   func_021BB164  0x021BB164  thumb  ov088
 *   func_021BB5C4  0x021BB5C4  thumb  ov088
 *   func_021BB4B4  0x021BB4B4  thumb  ov088
 *   func_021BB544  0x021BB544  thumb  ov088
 *   func_021BAD08  0x021BAD08  thumb  ov088
 *   func_021BB380  0x021BB380  thumb  ov088
 *   func_021BB608  0x021BB608  thumb  ov088
  */
/* sdk/mod_title.h, spelled out: the overlay link header declares what the
 * file itself does not (docs/hooks.md). */
extern int bw_mod_title_settings_on(void);
extern int bw_mod_title_in_settings(void);
extern int bw_mod_title_set_in_settings(int on);
extern int bw_mod_title_set_rebuild(void);
extern void eidos_sui_enter(void);
extern int eidos_sui_input(unsigned pressed, unsigned held);
extern void func_021BB474(char *p);
extern void func_021BB164(char *p);
extern void func_021BB5C4(char *p);
extern void func_021BB4B4(char *p);
extern void func_021BB544(char *p);
extern void func_021BAD08(char *p);
extern void func_021BB380(char *p);
extern void func_021BB608(char *p);
#define BW_TITLE_ROW_SETTINGS 7
#define BW_TITLE_SEL_SETTINGS 9
typedef unsigned char u8;
typedef unsigned int u32;

extern void func_0202EA40(void *p);
extern u32 func_020362DC(void);
extern void func_020061E4(u32);
extern int func_021BC0AC(char *p, int v);
extern void func_0200C2F0(int, int, int);
extern int func_021BC080(char *p, int v);
extern u32 func_0203633C(void);
extern int func_021BB890(char *p, int d);
extern u8 func_0200C308(void *, unsigned);

/* The settings screen's cursor state. */
static int settings_state(char *p)
{
    int r;
    func_0202EA40(*(void **)(p + 0x11c));
    r = eidos_sui_input(func_020362DC(), func_0203633C());
    if (r & 0x10)
        func_020061E4(0x548);
    if (r & 0x20)
        func_020061E4(0x54c);
    if (r & 0x40)
        func_020061E4(0x551);
    if (r & 2) {
        *(u8 *)(p + 0x129) = 8;
        return func_021BC080(p, 1);
    }
    if (r & 1) {
        func_021BB474(p);
        func_021BB164(p);
        func_021BB5C4(p);
        func_021BB4B4(p);
        func_021BB544(p);
        func_021BAD08(p);
        func_021BB380(p);
        func_021BB608(p);
        bw_mod_title_set_rebuild();
        return 4;
    }
    return 5;
}

int func_021BA338(char *p)
{
    if (bw_mod_title_settings_on() && bw_mod_title_in_settings())
        return settings_state(p);
    func_0202EA40(*(void **)(p + 0x11c));
    if (func_020362DC() & 1) {
        func_020061E4(0x54c);
        switch (*(u8 *)(p + *(u8 *)(p + 0x128) + 0x120)) {
        case 0:
            *(u8 *)(p + 0x129) = *(u8 *)(p + *(u8 *)(p + 0x128) + 0x120);
            return func_021BC0AC(p, 7);
        case 1:
            return func_021BC0AC(p, 8);
        case 2:
            *(u8 *)(p + 0x129) = *(u8 *)(p + *(u8 *)(p + 0x128) + 0x120);
            return func_021BC0AC(p, 11);
        case 3:
            func_0200C2F0(*(int *)(p + 8), 1, 2);
            *(u8 *)(p + 0x129) = *(u8 *)(p + *(u8 *)(p + 0x128) + 0x120);
            return func_021BC0AC(p, 11);
        case 4:
            func_0200C2F0(*(int *)(p + 8), 2, 2);
            *(u8 *)(p + 0x129) = *(u8 *)(p + *(u8 *)(p + 0x128) + 0x120);
            return func_021BC0AC(p, 11);
        case 7:
            if (bw_mod_title_settings_on() && !func_0200C308(*(void **)(p + 8), 3)) {
                /* The leave path of kind 1 (func_021BA6B0): state 11's table
                 * entry is the unlabelled stub at 0x021BA984, which the
                 * boot does not list as a code entry yet. */
                *(u8 *)(p + 0x129) = BW_TITLE_SEL_SETTINGS;
                return func_021BC080(p, 1);
            }
            func_0200C2F0(*(int *)(p + 8), 3, 2);
            *(u8 *)(p + 0x129) = *(u8 *)(p + *(u8 *)(p + 0x128) + 0x120);
            return func_021BC0AC(p, 11);
        case 5:
            return func_021BC0AC(p, 9);
        case 6:
            *(u8 *)(p + 0x129) = *(u8 *)(p + *(u8 *)(p + 0x128) + 0x120);
            return func_021BC0AC(p, 11);
        }
    }
    if (func_020362DC() & 2) {
        func_020061E4(0x551);
        *(u8 *)(p + 0x129) = 8;
        return func_021BC080(p, 1);
    }
    if (func_0203633C() & 0x40) {
        if (func_021BB890(p, -1) == 1) {
            func_020061E4(0x548);
            return 6;
        }
    }
    if (func_0203633C() & 0x80) {
        if (func_021BB890(p, 1) == 1) {
            func_020061E4(0x548);
            return 6;
        }
    }
    return 5;
}
