# The eidos UI kit

The one-screen layout needs new interface pieces: on the DS, half of the
interface lived on the second screen. This kit is the set of those pieces.
It covers only what the two screens made necessary (menus, the Bag, the
Pokedex, battle controls, Options, the trainer card, the PC, Save, the
C-Gear). Everything else (trainers, Pokemon, maps, effects) stays the
game's own.

The plan this belongs to is [`unified-view-plan.md`](unified-view-plan.md).

## Rules

1. **The game's own look.** Colours are the ones Black and White draw.
   Text uses the game's font. Icons and sprites are the game's. What is
   new is the shapes: cut corners, slanted titles, rows with an index
   strip, key hints.
2. **Drawn, not shipped.** The kit is code. It draws every piece at run
   time from a few primitives, so it ships no image files and no game data.
   The font, the icons and the sprites come from the player's own data,
   read when the game starts.
3. **One pixel is one DS pixel.** Pieces are drawn at the DS pixel size
   and scaled by a whole number only (3x at 720p, 4x at 1080p). The
   gameplay behind them renders at the window's full resolution.
4. **Relational layout.** A screen is laid out from the logical canvas
   (window size divided by the scale): anchors to edges, fractions of the
   canvas, and a minimum and maximum for each size. A screen that has
   room shows more (more list rows, the bag picture, the empty party
   slots); a screen that has less drops them in a fixed order.
5. **One colour per app.** Each app keeps the colour the game gives it,
   as an accent on a black and grey base.
6. **Editable later.** Colours, sizes and screen layouts are data (see
   "Data format"), so the modding suite can edit them once the framework is
   fixed.

7. **Everything takes the mouse.** In the one-screen layout the whole
   window is the touch screen. Every piece that can be chosen has a hit
   region the size of the piece (at least 40x40 window pixels on touch
   screens), a hover state (the selected look without the outline), and
   a click that does what A does on it. Lists scroll with the wheel,
   tabs and pockets change on a click, the PC box drags and drops, key
   hints are buttons, and a click outside a modal is B.

## Scale and canvas

| Value | Rule |
|---|---|
| UI scale `k` | `max(2, floor(window_height / 240))` |
| Logical canvas | `window_width / k` by `window_height / k` |
| Footer | 17 px high, along the bottom, on every modal |
| Modal | centred above the footer; width and height are fractions of the canvas with clamps (per screen, below) |
| Behind a modal | the game, blurred and darkened |

## Colours

Every colour is sampled from the game's screens; the source is in the last
column.

| Token | RGB | Use | Source |
|---|---|---|---|
| `ink` | 0 0 0 | outlines | buttons |
| `body` | 16 16 16 | panel fill | start-menu button body |
| `body2` | 24 24 24 | rows, title bars | Pokedex title bar |
| `edge` | 57 57 57 | inner edge, dark shadow | start-menu button face |
| `edge2` | 82 82 82 | light edge | start-menu button highlight |
| `grey` | 156 156 156 | text shadow on dark | start-menu labels |
| `light` | 213 213 213 | secondary text | grid lines |
| `cyan` | 0 213 197 | selection, light half | selected start-menu button |
| `cyan2` | 0 139 172 | selection, dark half | selected start-menu button |
| `cyan_edge` | 74 148 255 | selection outline | selected start-menu button |
| `navy`, `navy2` | 8 41 74, 8 74 115 | party rows | party screen plates |
| `sky` | 8 180 255 | party, PC, Save accent | party plate outline |
| `lime` | 148 238 24 | HP label | Pokedex rule |
| `hp_g`, `hp_y`, `hp_r` | 98 255 98, 255 222 49, 255 82 49 | HP fill | party HP bars |
| `red`, `red2`, `pink`, `pink2` | 139 16 41, 90 8 32, 238 82 123, 205 57 90 | Bag | Bag screens |
| `dex`, `dex2`, `dex3` | 230 30 60, 120 14 32, 60 8 18 | Pokedex (red and black) | the Pokedex device |
| `cursor` | 255 16 123 | battle selection | battle move cursor |
| `fight`, `mon`, `bag`, `run` | 222 24 74, 99 222 49, 255 189 41, 49 180 255 | battle commands | battle command labels |
| `card`, `card_l`, `card_d` | 57 255 189, 156 255 213, 0 98 74 | trainer card | trainer card |
| type colours | per type, in the game's type order | type tags | NORMAL and FIRE from the move screen; the others are placeholders until the game's type palette is read |

## Text

The game's NFTR fonts (archive `a/0/2/3`): font 0 (12x15) for labels and
names, font 2 (11x13) for descriptions, font 1 (10x10) for small labels,
key hints and numbers. Pixel value 1 is the letter and 2 its shadow; each
piece passes a letter colour and a shadow colour (or none). Characters the
font lacks are not used (the Pokedollar sign is the game's own glyph).

## Pieces

Each piece has a fixed height and a width chosen by the screen.

| Piece | Size | States | Notes |
|---|---|---|---|
| Panel | any | per-app fill and edge | black outline, 1 px edge, cut corners (top left, bottom right by default) |
| Title bar | w x 22 | accent per app | chevron, title, slash, subtitle, right-aligned value, accent rule |
| Key hints | w x 17 | - | key caps (`A`, `B`, `X`, `Y`, `L R`, arrows) and the action |
| Start-menu row | w x 26 | plain, selected | icon in a round badge, label; selected is cyan with a diagonal split and moves 8 px out |
| Party row | w x 30 | lead, plain, empty | index strip, icon, name, gender, level, HP bar, HP numbers |
| Battle plate | w x 24 (foe), w x 36 (player) | - | name, gender, level, HP bar; the player's adds HP numbers and the experience line |
| HP bar | w x 4 | green, yellow, red | thresholds 50% and 20% |
| Command button | w x 28 (FIGHT), w x 20 | selected, plain | colour strip and label in the command's colour; selected has the cursor edge |
| Move row | w x 22 | selected, plain, empty | type tag, name, PP |
| Type tag | w x 13 | per type | |
| Message | w x 30 | - | battle and field prompts |
| Pocket tab | w x 18 | selected, plain | Bag |
| Item row | w x 20 | selected, empty | icon, name, count |
| Pokedex row | w x 18 | selected; seen, caught, unknown | status mark, number, name |
| Info row | w x 15 | - | label and value (height, weight, badges, time) |
| Option row | w x 19 | selected, plain | label and choices; the chosen one is a cyan pill |
| Tab | text width + 16 x 14 | selected, plain | Options pages |
| C-Gear bar | canvas width x 15 | - | place, time on a slanted tab, IR / ONLINE / WIRELESS, battery |
| Badge case | w x 34 | earned, not earned | the game's eight badge shapes |
| Box grid | any | cursor | 6 x 5 cells, ball marks, the cursor's corner brackets |
| List row | w x 18 | selected, plain | label, optional icon and value (action lists, menus) |
| Tabs | text width + 16 x 14 each | selected, plain | pages of a screen |
| Scrollbar | 5 x h | position, size | lists longer than their panel |
| Page dots | 8 per page x 6 | current | paged screens |
| Toggle, slider, stepper | 26 x 12, w x 9, w x 15 | on/off, value | settings, amounts |
| Text field | w x 18 | caret, room left | names, codes |
| Key | w x 16 | selected, plain | on-screen keyboard |
| Dialog | w x by text | choice | yes/no and other questions over any screen |
| Toast | text width x 22 | - | short notices (item obtained, player joined) |
| Progress, spinner | w x 6, 12 x 12 | value, frame | saving, connecting |
| Status badge | 24 x 11 | PSN PAR BRN SLP FRZ FNT PKRS | party rows, battle plates |
| Category icon | 16 x 11 | physical, special, status | move details (stand-ins until the game's own are read) |
| Target chevron | 11 x 8 | - | battle target choice, over the battler |
| Party balls | 10 per slot x 9 | ok, status, fainted, empty | battle |
| Name tag | text width x 16 | per player colour | other players in multiplayer |
| Stat row | w x 15 | value bar | summary |

## Screens

| Screen | Layout | Width | Height | Drops when short |
|---|---|---|---|---|
| Start menu | party on the left, menu on the right, game between | each column 30% (118 to 150) | rows 24 to 30 high | the empty party slots |
| Bag | pockets, items, details | 90% (330 to 520) | 86% (190 to 300) | the bag picture |
| Pokedex | list on the left, entry on the right | 90% (330 to 520) | 86% (190 to 300) | list rows |
| Options | tabs, rows, help | 80% (320 to 440) | 86% (190 to 260) | - |
| Trainer card | facts, trainer, badge case | 72% (300 to 380) | 80% (180 to 220) | - |
| Save | place, party, facts, question, YES / NO | 60% (260 to 320) | 184 | - |
| PC | party, box, details | canvas less 12 (360 to 560) | canvas less the footer | the empty party slots |
| Battle | foe plate top left; player plate and commands bottom right; message bottom left | plates 34% (120 to 150); commands 26% (96 to 120) | anchored | - |
| Field | the C-Gear bar along the top; name tags over other players | canvas | 15 | - |
| Party | six rows in two columns, action list, message | 90% (340 to 520) | 86% (200 to 280) | - |
| Summary | tabs; Pokemon left; stats, ability, experience right | 90% (340 to 520) | 86% (200 to 290) | - |
| Battle target | chevron and name tag over the target, message | anchored | anchored | - |
| Battle Bag | four categories, the item list, party balls | 80% (300 to 440) | 82% (180 to 250) | - |
| Battle switch | party rows with status badges, message | 84% (320 to 460) | 84% (190 to 260) | - |
| Name entry | prompt, field, case tabs, keys, BACK / SPACE / OK | 84% (330 to 460) | 86% (200 to 270) | - |
| Xtransceiver | contacts with reach | 70% (300 to 380) | 80% (180 to 240) | - |
| Poke Mart | money, items with prices, item detail with amount | 86% (330 to 480) | 84% (190 to 260) | - |
| Multiplayer session | code, player slots, notices, waiting state | 74% (300 to 400) | 80% (190 to 240) | - |
| Title menu | CONTINUE card with player, time, badges; the other entries | 60% (260 to 320) | 80% (180 to 230) | - |

## Data format

The framework is not fixed yet. The intent: the kit reads one file of
tokens and one file per screen, and the modding suite edits both.

```json
{
  "tokens": { "body": [16, 16, 16], "cyan": [0, 213, 197] },
  "pieces": { "menu_row": { "height": 26, "cut": 6, "selected": { "fill": "cyan", "split": "cyan2", "edge": "cyan_edge" } } },
  "screens": {
    "bag": {
      "modal": { "width": ["90%", 330, 520], "height": ["86%", 190, 300], "accent": "pink" },
      "columns": ["pockets", "items", "details"],
      "drop": ["bag_art"],
      "hints": [["A", "Use"], ["B", "Back"], ["LEFT RIGHT", "Pocket"]]
    }
  }
}
```

## Status

In the port. The code:

| File | What |
|---|---|
| engine/ui/ui.h, ui.c | The frame: scene detection by recipe, the view, hit regions, the mouse, taps and presses into the game, typing |
| engine/ui/kit.h, kit.c | The pieces above, drawn into the canvas |
| engine/ui/assets*.c | The game's font (a/0/2/3), item icons (a/0/2/5), Pokemon icons (a/0/0/7), walking figures (a/0/3/1, a/0/3/2; 0 the hero, 33 the heroine) |
| engine/ui/recipes.c | The recipe groups in priority order |
| engine/ui/recipe_*.c | One group of screens each: title, battle, bag, dex, party, menus (Options, Save, trainer card), pc, input (name entry, Xtransceiver), field (the world, the C-Gear bar, the start menu) |

Notes for the code:

- Types are the game's own numbers (NORMAL 0 .. STEEL 8, FIRE 9 ..
  DARK 16); UI_TYPE_UNKNOWN is the unknown mark.
- A picture the kit cannot decode (Pokemon and trainer fronts) is copied
  from the screen the game still draws (ui_blit_screen); 3D pictures cannot
  be copied.
- Behind a modal over an app that closed the field, the presenter blurs the
  last live world.
- The one-screen title screen shows the live 3D, without the DS's motion
  trail, so it fills the window.

For checks: EIDOS_UI_MOUSE scripts the mouse ("frame:x:y" click,
"frame:x:y:m" move, "frame:x:y:r" right click, "frame:x:y:w+N" wheel, in
window pixels) and EIDOS_UI_TEXT types text ("frame:text"); `--window`
writes the window picture with each screenshot. On a Windows desktop scaled
150%, EIDOS_WINDOW_SIZE is in logical pixels (854x480 gives 1281x720).

The Python prototype that came first (every piece and screen at five window
sizes) stays local: it reads the extracted game data and captures of the
running game.
