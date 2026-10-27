# The unified view

This document is the design for the one-screen composition. It is not a
two-screen layout with one screen hidden: the port draws one logical
256x192 view, and every scene chooses, deliberately, which elements from
the game's two DS screens appear in it.

The inventory of every scene and its composition rule is
[`unified-view-inventory.md`](unified-view-inventory.md); the order of the
work is [`unified-view-plan.md`](unified-view-plan.md).

## The rule

There is no base screen.

A scene is a **recipe**: an ordered list of layers, each layer a named
element taken from one of these sources:

1. the game's top framebuffer;
2. the game's bottom framebuffer;
3. the game's hardware state for one element (a BG layer, an OBJ set, a
   window, a 3D view, a text printer);
4. a port-drawn widget that uses the game's own data, font or art;
5. a port-drawn overlay (the start-menu panel, the C-Gear bar).

The recipe says where each layer goes, how it is cropped or masked, its
alpha, its priority, and whether it is interactive. A scene with nothing
useful from the bottom screen simply has no bottom layers. A scene that
normally uses both screens may use elements of both, or replace one
element with a port widget.

No rule says "top is the base". The field world is usually the largest
layer because that is what the game draws largest, not because the top
screen has priority.

## Canvas and ratios

The logical canvas is 256x192, the DS screen's own size. All recipes are
written in that coordinate space.

Future ratios are handled by the recipe's layout slots:

- every element has an anchor (left/centre/right, top/middle/bottom) and a
  size mode (fixed, fit, stretch);
- the canvas has a safe rectangle and four margins;
- a wider canvas grows the margins and keeps the anchors, so a scene can
  be re-laid out without being re-invented;
- today the safe rectangle is the whole 256x192 canvas.

Nothing in a recipe hardcodes a window size. The presenter scales the
finished canvas, as it already does for the DS screens.

## What "an element" means

The port needs more than "copy the bottom screen". An element is one of:

- **A rectangle of a composed screen**, optionally with a colour key or a
  mask. This is the cheap path for an opaque panel or a text window whose
  backdrop is a known colour.
- **A hardware layer**: one BG, the OBJ/OAM set, a window, the 3D view.
  The g2d compositor can render a selected layer or set of layers onto a
  transparent target. This is the path for a menu whose backdrop must not
  come with it.
- **A game window/text object**: for dialogue, yes/no prompts, list rows.
  The port can keep the game's own printer and window, and place them on
  the unified canvas.
- **A port widget**: the start-menu panel, the C-Gear bar, a re-laid-out
  battle control. It draws in the port's canvas, using the game's font and
  data where possible.

The inventory names, for each scene, which of these applies. A recipe may
use a mix.

## Placeholders

A screen is a placeholder for a scene when it has no scene content: black,
a disabled display, or the game's idle C-Gear when the scene doesn't use
it. Placeholders are hidden by the recipe; they are never copied just
because the hardware produced a frame.

The C-Gear is not automatically a placeholder during play. It becomes a
port widget (a compact bar or a button that reveals it), so the player
keeps access to it without giving it half the canvas.

## The start menu

The field start menu is the mock's right-side panel:

- the field remains a full-canvas layer;
- the panel is a port layer on the right, translucent, with the game's own
  entries, labels and icons;
- its entries come from the game's field menu, so all seven slots and the
  entries that unlock later appear; it scrolls if the panel cannot hold
  them;
- a left chevron closes it;
- the game's hidden field-menu state still owns selection and actions; the
  panel mirrors it.

This is the one place where the composition is deliberately an overlay
rather than a full-canvas replacement.

## Full-canvas screens

Bag, Options, Pokédex, party/Pokémon list, summary, trainer card, PC,
phone/Xtransceiver and the other menu apps use the whole canvas. Most can
start from the game's bottom screen, but each recipe still names the
elements it keeps: the category column, the grid, the description, the
buttons, the text window. Elements that only exist because the game had a
second screen (a dead backdrop, a duplicated frame) are dropped.

The save/report screen is the exception: it is genuinely bottom-only and
fits as a centred 240x160 panel over a dimmed field, not a full-canvas
rewrite. PC's box grid is top-screen and its party strip is bottom-screen;
both are kept in one canvas.

## Battle

Battle is one recipe family: the 3D scene is full-canvas, the HP plates are
redrawn at the top corners, the message slot stays at the bottom, and the
action/move/target/bag/party controls are port layouts over the lower part
of the scene. Target selection puts chevrons on the battlers themselves.
Battle text and prompt text share one slot.

## Text

Dialogue, yes/no prompts and menu help are elements of their own, not part
of a screen. A text window can sit over the field, over a menu, or at the
bottom of a battle composition. Its recipe names the window, the printer
and the font; its position is a slot, so it can move when the canvas
changes.

In Black the field dialogue box, yes/no box, Y shortcut list and place-name
banner are already **top-screen** elements; keep their native rectangles.
Battle text is also top-screen; the bottom-screen battle prompt is a
duplicate and is suppressed. Menu help and prompt text from the lower
screen become slots in the same canvas.

## Boot and title

The boot/title path does not have one screen assignment. The logos and
copyright use both engines; the opening is main-on-top with a black
bottom; the title attract is main-on-**bottom**, with sub BG6/BG7 and a
sub-OBJ sprite on top; the title menu is main-on-top with a black bottom;
name entry is main-on-bottom. The recipe uses the **main composite** as the
picture and adds the useful sub layers (BG6/BG7 art, prompt, icon, info
panel), not a fixed "top" or "bottom" rule.

The title screen's visible PRESS START is not code text; the only reserved
slot is a hidden 32x2-tile BG5 window at y=152. The unified view redraws
that prompt in the slot. The title logo sits in the upper band; the version
mascot sprite becomes a 96x96 accent, not a second screen.

## Input

Both input paths stay:

- keyboard and gamepad keep their existing DS-button bindings;
- touch/pointer is a first-class path for every visible control.

Each interactive element registers a hit region in canvas coordinates.
The platform maps pointer coordinates through the canvas transform, then
the scene recipe's hit regions decide what was touched. A touch on the
start-menu panel selects a row; a touch on a bag tab switches category; a
touch on a battle control selects it. The game still receives the input it
expects.

## Scene detection

The recipe is selected from game state, not from a guess about a picture:

- the current process and its overlay id (the game's process stack);
- the current field sub-screen mode and menu mode;
- the current battle view step/menu state;
- the title/boot process (`0x021BD8E8` logos, `0x021B68D8` opening,
  `0x021BD574` title, `0x021BD66C` menu, `0x021BD818` intro,
  `0x021F5580` name entry);
- explicit transitions (fade, wipe, load).

The inventory records the evidence for each scene. Where a state is not
exposed, the port adds an accessor or hooks the entry/exit point; it does
not sample pixels to guess.

## Transitions

A scene change is a recipe change. Elements that both recipes share stay
in place; elements that leave fade out; elements that arrive fade in. The
game's own fades and wipes remain the timing source. No recipe may flash
the whole canvas between two states.

## What is deliberately not decided here

- The exact palette for port widgets (the game's own where possible).
- The final list of scenes; it comes from the inventory.
- The re-layout of every battle control; it comes from the battle part of
  the inventory.

## Status

The decisions that override this document (two-screen layouts kept,
fallback per scene, mod fallback, the interface kit) are in
[`unified-view-plan.md`](unified-view-plan.md). Design only. No implementation is part of this document. The scene
inventory is [`unified-view-inventory.md`](unified-view-inventory.md).
