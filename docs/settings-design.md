# eidos settings screen: research and design

Pinned aletheia 71f6d529b. Addresses are DS addresses; file names are
`src/matched/ov/ov088_func_<addr>.c` unless noted. The screen has four tabs:
Display, Layout, Controls and Sound (Sound holds one item: master
volume). The port's own screen adds two more sections, Mods and Network,
which scroll in the section list; the game's own screen is the four it has.

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
2, as kind 1 does) instead.

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
table at 0x020A6C58 (aletheia's `docs/ASSETS.md`). The menu uses arc 0x23 (`a/0/3/5`:
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

Reference renders (not in the repository): `ref/opt_bg_top.png`
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
(left cap, middle, right cap). The port's own screen has seven sections, and
the section list scrolls for the ones that do not fit.

### The port's pages in the game's Options screen

`stubs/ov/patch_ov068_option_menu.c` adds pages to the screen itself, in its
own look. The sixth (grey) bar is the page bar: the page's name in the
middle and a double arrow at each end (the bag's pocket arrows, in this
screen's text colours). A touch on an arrow, or L and R, changes the page
and lights the arrow for a moment. GAME is the game's
own page, unchanged. DISPLAY has the window mode, the pixel scale, the 3D
resolution, the screen layout and the screen swap; SOUND has the volume;
EXTRAS has Instant text (engine/platform/text_instant.c: the game's text
streams start with the wait -255, a whole page a frame; the page's own stop
for a button is the game's). The title screen's settings have the same
option on an Extras tab.
A port page uses the game's label column, the game's choice columns (three
at 112/160/208 px, two at 112/184 px), the game's marker sprites and the
game's colours (palettes 0, 3, 7 and 12). A setting with many values is a
stepper: "<", the value, ">". The port's settings apply and are saved at
once; the game's are saved on CONFIRM, as before, and the discard prompt is
about the game's settings only. The windows of a page use the screen's own
font and heap, and are deleted before the screen's heap is (OPTION_MENU_ExitList).

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
- Rows are option bars: a label and a right-aligned value. Two levels: a
  list of sections (header "Settings"), and inside a section its rows
  (header is the section name). The lower screen shows help text for the
  highlighted row (slot 13 re-made as a window on layer 4).
- Settings live in an eidos struct (`sdk/settings.h`: scale, filter, layout,
  key bindings, master volume), saved as a small file through `plat_save`.
- Messages: an eidos member appended to `a/0/0/2`, generated from a repo text
  file into a cache mod root (`bw_add_mod_root`), opened with
  `func_02045B38`, so mods can translate it.

### What the screen does

The screen is built, in the shape the title menu already has: the menu proc
of overlay 88 started again in "settings mode" (design 1, through the game's
own bg, bar tiles, font and print queue). Files:

- `engine/platform/settings_ui.c` (model, no game calls): seven sections (the
  game's four, plus Mods and Network), a row
  table per section, navigation, value changes, capture, Revert and Restore
  defaults rows, and one marked table of UI text ("UI TEXT"). It talks to
  `settings.h` only. Test: `tests/test_settings_ui.c`.
- `stubs/ov/reimpl_ov088_func_021BB4B4.c`: the row list is one kind per
  settings row (kind 1 is the header bar, kinds 2 to 7 the visible rows).
- `..._021BAD08.c`: prints the label (left) and the value (right aligned by
  the font's own width function, cut to fit) into each row's text window.
- `..._021BB608.c`: lays the rows out 3 tile rows apart, hides every sprite
  (this removes the stray WFC icon) and puts the highlight on the cursor row.
- `..._021BA338.c`: in settings mode the cursor state is the input handler.
  A change that needs a redraw runs the state 0 set-up calls again (list,
  buffers, text, window manager, layout) and waits in state 4.
- `..._021BA2C8.c`: state 4 returns to the cursor state at once after such a
  rebuild (no fade); the first entry still fades in as the game does.
- `..._021BA014.c`: leaving (B) commits the settings and returns to the menu.

Controls: up and down move (with repeat), left and right change a value
(A also cycles a choice). A on a section opens it, B in a section goes back
to the section list, B in the list leaves and commits. A on a
Controls row arms the capture ("Press a key"); the next key or pad control
becomes that button's primary binding (slot 0), Escape cancels. A on
"Revert changes" restores the last committed state, A on "Restore defaults"
sets every option and binding to its default; both stay uncommitted until
the screen is left. A section with more than six rows scrolls (Controls). Control rows show
"key / pad" in short names.

Not done: the row table is still the menu's eight kinds (six visible
rows, header and cursor sprites hidden); the text table is in one file but
not yet a message member mods can replace; stylus input; the header uses the
same bar as the rows (the grey variant of the Options screen is not used); a
capture binds only slot 0 (the other slots stay as they are and can be
edited in settings.ini).

### The title_settings hook

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


### What is left

- Replace the menu's row model with an eidos row table (kind, text source,
  action) so rows can be added without a spare kind, and move the UI text
  to a message member mods can replace.
- `ov068 func_021B9980` is unmatched; `0x021BA984` is not a listed code
  entry in aletheia.

## The Options-style screen (engine/platform/mod_options_proc.c)

Rather than reuse the title menu's process with its black bars, the screen
has a process of its own that follows the game's Options screen (overlay 68)
closely, so it looks like part of the game.

What it reproduces, all from the player's own data at run time:

- The set-up of `func_021B9980` / `func_021B9850` (asm only in aletheia, so
  read from `asm/overlays/ov068.s`): the VRAM bank table (`d_021BB598`), the
  background system mode, six layer templates (`d_021BB614`, six entries of
  ten words), the graphics archive `a/0/2/7` (arc 0x1B) with its members:
  palette 1, char 0 and screen 2 (the six bars, 256x512 px map, the sixth bar
  grey), char 13 and screen 14 (the flat back), char 7 and screen 9 with
  palette 8 (the dark grid of the other screen), char 16 (window tiles), the
  text palette (member 8, bank 4, into bank 15), the sprite bank of
  `func_021B9E28` (members 3 to 6 and 10 to 12).
- Three kinds of text window: a 16x3 tile header on layer 0, one 30x3 window per
  bar on layer 3 (at tile row 2 + 3 per bar), and a 30x4 help window at the
  bottom of the other screen on layer 4.
- The choice layout of a row: label at the left, then the choices side by side
  (three at x = 112, 160, 208; two at 112 and 184), the active one marked by a
  sprite at 120, 165, 211 or 120, 192, as `d_021BB704` places them. Rows whose
  options have more than three values (resolution, volume, gap, key bindings)
  show label and value instead.
- The list is six bars: up to five option rows and the grey bar, which is
  "Done" on the list of sections and "Back" inside one (the game's CONFIRM bar).

The process table is `{init, main, end}` like every game process
(`src/matched/arm9/func_020315F0.c`), started with `func_020312F8(-1, table, 0)`
from the title menu's exit function and handing back to the title with the
overlay id and table noted by `bw_mod_title_set_return`. The model (rows, input,
help text) stays in `engine/platform/settings_ui.c`; this file only draws.

The exact sprite and cursor art positions are taken from the tables and
checked only against the game's data, not against a running original. The
stylus is not read.
