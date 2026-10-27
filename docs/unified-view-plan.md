# Unified view: plan

The design rules are in [`unified-view.md`](unified-view.md). The list of
scenes is in [`unified-view-inventory.md`](unified-view-inventory.md). This
document says what we build, in which order, and how we rework the game's
art so that two screens of content fit on one.

## Decisions

These are fixed. They replace any rule in the other two documents that
disagrees.

1. **Two screens stay.** The two-screen layouts (stacked, side by side)
   remain a permanent choice in the settings. The one-screen view is a
   third layout, not a replacement.
2. **Fallback per scene.** A scene without a recipe shows the two-screen
   layout, scaled into the window. This is normal operation, not a bug. The
   switch between a recipe and the fallback uses the game's own fade, so
   it never flashes.
3. **Mods fall back.** A mod screen (new overlay, new menu) uses the
   fallback, unless the mod ships a recipe for it. Recipes are data files,
   so a mod can ship one without code.
4. **Multiplayer screens get recipes** like any other scene (session
   screens, the other players' avatars and name tags, trade and battle
   screens).
5. **A new interface kit.** The pieces the second screen used to hold
   (menus, Bag, Pokedex, battle controls, Options, trainer card, PC, Save,
   C-Gear) are redrawn with the kit in [`ui-kit.md`](ui-kit.md): the
   game's colours, font and icons, new shapes. Everything else stays the
   game's own art. The first rounds below reused the game's art only; they
   remain as the record of what that approach can and cannot do.
6. **The canvas fills the window.** The one-screen view does not keep the
   DS's 4:3 ratio. The 3D (field and battle) is rendered straight into the
   window's shape: the projection is widened horizontally and the vertical
   view stays the DS's own, so the player sees more to the sides and
   nothing is stretched or zoomed. A slight zoom-out is allowed only where
   a scene needs room for its panels, and stays small.
7. **Two scales.** The gameplay renders at the window's full resolution.
   The interface (the game's 2D art) is drawn on its own grid at a whole
   multiple of the DS pixel (3x at 720p, 4x or 3x at 1080p), so a panel
   takes less of the picture than it did of the DS screen.
8. **Layouts follow the recent games.** The arrangement of each screen
   takes its cue from the recent main-series games (party on the left and
   menu on the right, battle commands stacked at the bottom right, moves as
   rows, list and details side by side), built from Black and White's own
   art.

## The asset rule: cut, do not scale

Pixel art does not survive non-integer scaling, and a half-size copy of a
DS button is not readable. So a recipe never scales the game's art. It
makes art smaller by removing pixels that carry no information, and it
moves pieces closer together. The experiments in the next section showed
that a small set of operations covers the menus, the battle controls, the
Pokedex and the Bag.

| Operation | What it does | Where it worked |
|---|---|---|
| Flat-run removal | Removes rows or columns that repeat their neighbour, from the middle out. Text, icons and corners never repeat, so they are never touched. | Start-menu tiles: 42 px to 33 px high. |
| Seam cut | Removes one span of columns whose two end columns are identical, so the join is invisible. | Battle move buttons: 125 px to 92 px wide. |
| Half height | Joins the top half and the bottom half of a symmetric frame at a new height. | Move buttons 47 to 40 px; command buttons 22 px. |
| Clean frame | Erases a button's content: each row inside the frame is refilled with that row's own background texture, sampled next to the right edge. Gives an empty frame of the right type colour. | All move buttons (dotted texture, period 5). |
| Content by difference | Content is what differs from the clean frame of the same texture phase. Splits into name, type tag and PP numbers by column gaps. | Move names, type tags, PP. |
| Re-layout | Puts the parts back in a new order or on fewer lines; drops parts that the new size makes redundant ("PP"). | Compact move buttons. |
| Palette swap | Learns the colour map between two states of the same widget (plain and selected) and applies it to any widget of that kind. The game itself selects by palette. | Selected start-menu tiles for every entry. |
| Coloured text cut | Takes coloured text and its outline off a textured backdrop. | FIGHT, BAG, RUN, POKEMON labels. |
| Cursor reuse | Takes the cursor (the pink corner brackets) off one widget and puts its corners on a widget of another size. | Selected move and FIGHT button. |
| Text reflow | Prints a description into a narrower window. The mock cut words from the rendered lines; the port prints with the game's own printer and font into a window of the new width. | Bag item description in 112 px. |
| List window | Shows fewer rows of a list (a window that follows the cursor). The list logic is unchanged. | Pokedex list: 5 of 7 rows. |
| Layer move | Moves one layer (BG, OBJ set, window) to another place. In the port this is the real layer, not keyed pixels. | Battle HP plate moved above the move grid. |
| Drop | Leaves out an element that only exists because there were two screens (empty grid backdrops, duplicated titles, the bottom of the info page's top screen). | Pokedex, party list, Bag. |

What did not work, and what the port does instead:

- **Keying by colour** (to lift the HP plate off the battle scene) works
  on one capture but is fragile. The port renders the layer alone onto a
  transparent target, which the inventory already plans (section "What an
  element means" in `unified-view.md`).
- **Texture phase**: two buttons with the same frame can have the texture
  shifted by a few pixels (the left and right columns of the move grid). A
  clean frame must come from a widget of the same phase, or be
  regenerated from the texture tile.
- **Cursors that overlap frames** (the move cursor brackets) break edge
  detection. The port takes the cursor from its own OBJ, not from the
  composed picture.
- **Slanted frames** cannot lose rows evenly; they lose them in the
  middle (half height) and columns at a flat span only (seam cut).

## Experiments

Captures of the running game, reworked with the operations above (a local Python prototype). The
images are local only (they contain game graphics) and are not in the
repository. The first round kept the DS canvas, 256x192:

| Scene | Result | Cover |
|---|---|---|
| Start menu, rail | Six icon tiles in a column on the right, 40 px wide. The selected entry shows its label as a tab to the left. | 16% of the width |
| Start menu, dock | The same tiles in a row at the bottom; the label tab above the selected tile. | 18% of the height |
| Battle command | The message stays on the left. FIGHT (two rows high, red frame) and BAG, POKEMON, RUN (one row) on the right, in the message band. | Message band only |
| Battle moves | A 2x2 grid of 92x40 move buttons, type frame and type tag kept, "PP" dropped. The message is hidden; the player's HP plate moves up. | Bottom 42% |
| Pokedex list | Top header (title, seen, obtained), 5 list rows with the preview, the bottom bar. | Full canvas |
| Pokedex info | The bottom screen alone; the top screen holds only an empty grid and a title the bottom repeats. | Full canvas |
| Bag | The item box (narrowed) and the description (reflowed to 112 px) take the place of the bag picture; the item list and the bar stay. | Full canvas |

All seven fit with the game's art alone.

### Second round: wide canvas

The same art on a 16:9 canvas, laid out after the recent games, over the
game's own 3D rendered at full resolution.

| Scene | Layout |
|---|---|
| Start menu | Party plates (from the party screen) in a column on the left, the six menu buttons in a column on the right, the field between them. |
| Battle command | FIGHT, POKEMON, BAG, RUN stacked at the bottom right; the message band on the left of them; the HP plates in their DS corners. |
| Battle moves | Four move rows stacked at the bottom right: type tag, name, PP on one line; the frames widened along their texture. |
| Pokedex | Header across the top (widened along its flat spans), sprite and info card on the left, the full list on the right. |
| Bag | Bag picture on the left, item list in the middle, item box and description on the right. |

How the wide 3D was made, and what it showed:

- The experiment scaled the clip matrix (projection times position) in x
  and y. Both the vertices and the game's own BOX_TEST/POS_TEST culling
  go through it, so the game culls against the wider view too.
- At half zoom the field draws the map and the NPCs far past the DS
  view; only the far edge shows the clear colour. At three quarters (the
  16:9 width with the DS's vertical view) nothing is missing. The battle
  backdrop also extends.
- The captures used a uniform zoom of 0.75 and cut the 16:9 band out of
  it, so the picture was never resampled unevenly. The port does better:
  it renders the 3D straight into the window's shape.

Remaining gaps seen in the mocks:

- the party-ball indicator of the battle command screen is not placed
  yet;
- the compact buttons are small for touch (33 to 40 px high at 1x); the
  hit regions must be larger than the art on touch screens;
- the description reflow needs the game's printer to accept a new window
  width (the mock cut words from a capture);
- 2D elements that sit on the 3D (HP plates, the message band) must come
  from their own layers; the mock lifted them off a capture by colour.

## Build order

Each step ends with its scenes working in the one-screen layout, tests,
and captures. Everything not yet done uses the fallback, so each step can
ship.

1. **Frame of the work.** The layout setting (two-screen stacked, side by
   side, one-screen). The wide 3D: projection widened to the window's
   shape, rendered at full resolution. The canvas and compositor (layers
   with source, crop, alpha, priority, anchors to the window's edges, the
   interface scale). The fallback path. A scene id from the process stack
   and overlay id. Recipes loaded from data files, overridable by mods.
2. **Asset kit in C.** The operations of the table above, on layer
   renders, each with a test on a synthetic image. Results are made at
   scene load from the game's VRAM, cached in memory, never written to
   disk.
3. **Field.** World, text windows, place-name banner, the start menu (rail
   or dock), the C-Gear as a button. Multiplayer avatars and name tags.
4. **Battle.** Command, moves, target choice, the in-battle Bag and party,
   then the effects that use both screens.
5. **Main menus.** Pokedex (list, info, area, search), Bag, party list,
   summary, trainer card, Options, save.
6. **The rest, by how often players see it.** PC, Xtransceiver, the
   multiplayer session screens, Mystery Gift, then rare screens. The
   inventory gives the list.
7. **Boot and title.**

## Inputs per recipe

A recipe lists its hit regions in canvas coordinates. A touch or click in
a region becomes the touch the game expects on the bottom screen (the
original coordinates of that control). Keyboard and gamepad are unchanged.
On a touch screen, regions are at least 40x40 window pixels, larger than
the art when needed.

## Tests

- Recipe tests: synthetic layers and a recipe give the expected canvas.
- Kit tests: each operation on a synthetic image (flat runs, seams,
  texture phase, palette swap, reflow).
- Scene tests: a fake process stack selects the expected recipe, or the
  fallback.
- Input tests: a point in a region reaches the game at the original
  control.
- Captures: each finished scene rendered to PNG next to its two-screen
  original.

## Open choices

- Interface scale at 1080p: 3x (more room) or 4x (larger).
- Where the C-Gear button sits on the field.
