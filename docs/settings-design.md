# eidos settings screen: research and design

Pinned aletheia 71f6d529b. Addresses are DS addresses; file names are
`src/matched/ov/ov088_func_<addr>.c` unless noted. The screen has four tabs:
Display, Layout, Controls and Sound (Sound holds one item for now: master
volume).

## (a) The title menu (overlay 88)

The menu is a GFL proc. `d_021BD66C` is its template, three function
pointers {init 021B9F94, main 021B9FE4, exit 021BA014}, started with
`func_020312F8(ov088_id, template, arg)` (the proc API: NextProc into an
overlay). The title screen proc is `d_021BD574` (its exit, 021B9874, starts
the menu). The menu's main function runs a state machine; the state table is
`d_021BD63C` (12 entries, each state a function returning the next state):

| State | Function | Role |
|---|---|---|
| 0 | 021BA114 | set-up: bg, banks, then 4B4, 544, AD08, 184, 380, 608, AC24 |
| 1, 2 | 021BA1A0, 021BA218 | fade in, wait |
| 3 | 021BA230 | A-press flash (row redraw, palette 3 and 1) |
| 4 | 021BA2C8 | scroll tick |
| 5 | 021BA338 | the cursor state: A, B, up, down |
| 6 | 021BA4A0 | scroll step |
| 7 | 021BA540 | CONTINUE confirm |
| 8, 9 | 021BA6B0, 021BA7AC | save-data checks, confirm windows |
| 10 | 021BA880 | message state |
| 11 | 021BA984 | unlabelled tail-call stub inside 021BA880's file |

State 11 is not a code entry in the boot: `func_021BC0AC(p, 11)` (the
leave path of MYSTERY GIFT, WFC, MIC TEST) crashes with "execute of host
address 0x021BA985". The hook's A handler uses `func_021BC080(p, 1)` (state
2, as kind 1 does) instead. Forward to aletheia/the overlay lane.

**Rows.** There are eight row kinds, not an item table. Work area: `+0x120`
an 8 byte list of kinds in display order (8 = end), `+0x128` the cursor
(index into the list), `+0x129` the chosen kind, `+0x12A` the state
counter, `+0x12F` top tile row of the cursor row, `+0x130..0x138` scroll.
Labels come from message bank `a/0/0/2` member 179 (opened with
`func_02045B38(0, 2, 0xB3, 0xB)`, text is plain ASCII code units, 0xFFFF
ends; cipher in `tools/assets/text.py`):

| Kind | Message | Text | Shown when |
|---|---|---|---|
| 0 | none | CONTINUE panel (14 tile rows, save summary) | `func_020071EC(save) == 1` |
| 1 | 1 | NEW GAME | always |
| 2 | 2 | MYSTERY GIFT | always |
| 3 | 4 | BATTLE COMPETITION | `func_0200C308(save, 1)` |
| 4 | 5 | GAME SYNC SETTINGS | `func_0200C308(save, 2)` |
| 5 | 3 | NINTENDO WFC SETTINGS | always |
| 6 | 7 | MIC TEST | always |
| 7 | 6 | RELOCATOR | `func_0200C308(save, 3)` |

The list is built by `func_021BB4B4`. Per kind: a row height in tile rows
(`d_021BD5EC[kind].f2`: 3 for kinds 1..7, 14 for CONTINUE), a window in an
8 window bg manager (`func_021BB380`), a row buffer at `+0xF8 + 4*kind`
(screen map from arc 0x23 member 7, height*0x34 bytes, `func_021BB544`,
freed by `func_021BB5C4`) and a text slot at `+0x80 + 8*slot`
(`func_021BAD08` creates 14 slots from `d_021BD7C4` and prints each message
with `func_0201F250` after `func_02045EC0(bank, id)` and `func_0201C724`).
`func_021BB608` lays rows out from tile row 2; `func_021BB798` writes a
kind's map to bg layers 1 and 2; `func_021BB810` redraws a row with a
palette (1 normal, 2 the cyan highlight, 3 the flash); `func_021BB890`
moves the cursor and scrolls. The size 8 is fixed in the list, the
cursor bound, the buffers (8 at `+0xF8`), the window manager (8) and the
`+0x118` handle that follows them.

**Select.** State 5 (`func_021BA338`): A switches on the kind, stores it in
`+0x129`, then returns the flash state; the exit `func_021BA014` switches on
`+0x129`: 0 CONTINUE (menu again or the game), 1 NEW GAME (`d_021BD818`),
2 ov121 (Mystery Gift), 3 ov199, 4 arm9 `d_0209DEF0`, 5 ov016 (WFC),
6 ov168, 7 ov107 (MIC TEST), default (B) the title screen `d_021BD574`.

**Graphics.** Arc id N is NitroFS `a/(N/100)/((N/10)%10)/(N%10)` of the id
table at 0x020A6C58 (docs/ASSETS.md). The menu uses arc 0x23 (`a/0/3/5`:
members 0..3, 6..8, 12 LZ77 tile and screen data; 4, 5, 9, 13 palettes;
10/11, 14/15 cell and animation banks) and arc 0x1F (`a/0/3/1`, sprites).
The row bar is 26x3 tiles on bg layers 1 and 2; the cyan frame is the same
tiles with palette 2; the font object is `func_0201D7EC(0x17, ...)`; the
icon on the right (WFC) is an OBJ sprite at `+0x1C..+0x30`.

**Labels.** A mod can replace or add whole NARC members (sdk/vfs.h,
`<mod>/a/0/0/2/<index>`), but replacing 179 would replace the game's text.
For strings eidos owns, append a new member (index = the NARC's count) and
open it with a second `func_02045B38`. The placeholder builds one string
object instead (`func_020457B0`, `func_02045994`), enough for one label.

## (b) The game's Options screen (overlay 68)

`ov068_func_021B95A0.c` (init) opens message member 29 (OPTIONS, TEXT
SPEED, BATTLE SCENE, BATTLE STYLE, SOUND, CHARACTERS, SAVE BEFORE IR,
CONFIRM, QUIT, SLOW, MID, FAST, ON, OFF, SHIFT, SET, STEREO, MONO, YES, NO,
one help text per row) with heap 0x1C (0x21000 bytes), font
`func_0201D7EC(0x17, 0, 0, 0, 0x1c)`, and two text printers
(`func_02027F60(0, 0xd, ...)`, `(3, 9, ...)`). Its graphics set-up
`func_021B9980` is not matched (asm only, `asm/overlays/ov068.s:541`); it
loads arc 0x1B (`a/0/2/7`) with the same calls the menu uses
(`func_02049B68` palette, `func_02049658` char, `func_020498F4` screen):
members 0 (char), 1 (palette), 2 (screen), 7/8/9 and 13/14 for the other
screen. `func_021B9E28` builds the sprites from members 4..6 and 11..12
(bank of 11 at positions `d_021BB704`, a cursor pair at (0x80, 0x60)).

Reference renders (scratch only, never committed): `ref/opt_bg_top.png`
(members 0/1/2), `opt_bg_b.png` (7/8/9), `opt_tiles0.png`, `opt_tiles7.png`.

- Main screen: six full-width rows, 24 px pitch (3 tile rows, from tile row
  2). Each row is a rounded bar: fill (90, 65, 180), 1 px inner line
  (49, 49, 65), 2 px outline (57, 148, 106), bright end ticks (74, 189, 115)
  at x = 5 and x = 250. The sixth bar is the grey variant, fill about
  (57, 57, 74).
- Other screen backdrop: black with teal crosses on a 32 px grid, cross
  colour (24, 57, 49).
- Text is the game's font (white, dark shadow) in bmp windows over the bars;
  values (ON/OFF, SLOW/MID/FAST) are separate right-aligned strings; the
  selected row is marked by the cursor sprites and the highlight palette.

Reusable at run time from the player's own data: arc 0x1B members 0, 1, 2
(bars, palette), 7, 8, 9 (backdrop), the cursor sprites (4..6, 11..12), the
menu frame (arc 0x23 member 7) and the font. No new art is needed: a tab is
a bar; four tabs fit in one 256 px row as 64 px bars cut from the bar tiles
(left cap, middle, right cap).

## (c) Design

1. **Through the DS hardware state (recommended).** A port-side GFL proc
   `{init, main, exit}` started with `func_020312F8`, drawing with the
   game's functions: bank and bg set-up as `func_021BA9D4` / `func_021BA9E8`
   (`func_0204335C`, `func_020433E0`, `func_0203F8F4`, `func_0203FC28`,
   `func_0203FCA0` per layer), assets via `func_020490F4(0x1B, heap)` then
   `func_02049B68` / `func_02049658` / `func_020498F4`, text via a message
   bank, `func_0201D7EC` (font), `func_0201C440` (window manager),
   `func_0201F250` and `func_0201C724` (print). It goes through the same
   compositor as game screens (layer order, blending, fades, capture,
   scaling), and looks native by construction.
2. Direct drawing into the two 256x192 buffers. Rejected: it bypasses the
   compositor, needs its own font renderer, and duplicates set-up code the
   game provides.

Steps for (1):

- `sdk/mod_settings_proc.c` holds the template and work struct. `init`
  creates the heap (`func_0203064C`) and work (`func_0203159C`); `exit`
  frees them (`func_020315D4`, `func_020306F0`) and calls
  `func_020312F8(ov088_id, d_021BD66C, 0)` to return to the menu. Overlay
  names are not visible to library code, so a thin `stubs/ov/` wrapper
  supplies `ov088_id` and `d_021BD66C`.
- `main` is a state machine like `func_021B9750`: build, fade in, input
  (`func_020362DC` pressed, `func_0203633C` held; 0x40 up, 0x80 down, 1 A,
  2 B), fade out, return 1.
- Rows are option bars: a label and a right-aligned value. L and R switch
  tabs; the tab name is in a header bar.
- Settings live in an eidos struct (`sdk/settings.h`: scale, filter, layout,
  key bindings, master volume), saved as a small file through `plat_save`.
- Messages: an eidos member appended to `a/0/0/2`, generated from a repo text
  file into a cache mod root (`bw_add_mod_root`), opened with
  `func_02045B38`, so mods can translate it.

### What the proof does

`title_settings` (docs/hooks.md) adds SETTINGS after MIC TEST. Kind 7 has a
window, buffer and text slot, but the game uses it only when
`func_0200C308(save, 3)` holds (RELOCATOR). The hook takes kind 7 when that
test is off; when it is on, the game's row stays and SETTINGS is not shown,
because the 8 byte list and 8 kind tables cannot take a ninth row. A on the
row sets selection 9 and a flag; the exit restarts the menu proc, whose list
(`func_021BB4B4`) then holds only SETTINGS: the game's frame and font, a
placeholder. B (selection 8) clears the flag and restarts the menu.
Defaults to on; `EIDOS_HOOKS_OFF=title_settings` or
`--no-hook title_settings` restores the original menu.

### What the UI lane needs next

- Replace the menu's row model (list, buffers, windows, `func_021BB380`,
  `func_021BB544`, `func_021BB5C4`, `func_021BB608`, `func_021BB890`) with an
  eidos row table (kind, text source, action) so rows can be added without a
  spare kind.
- Build the real screen as design (1); the placeholder is only the menu proc
  with one row. Title text on a bar is there, tabs and rows are not.
- Hide the menu's stray sprite icon (top right of the placeholder) in
  `func_021BB9DC` for settings mode.
- To aletheia: `ov068 func_021B9980` unmatched; `0x021BA984` is not a
  listed code entry.
