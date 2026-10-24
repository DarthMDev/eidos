# Unified view: implementation plan

The design rules are [`unified-view.md`](unified-view.md); the scene list is
[`unified-view-inventory.md`](unified-view-inventory.md).

## Order

1. **Finish the inventory.** Every scene, every element, every input region,
   with code evidence. No compositor work before the list is stable.
   The code-derived inventory is in `unified-view-inventory.md`; the second
   pass's corrections, missing scenes and composition elements are in its
   section 0.
2. **Canvas and compositor.** One 256x192 RGBA canvas. Layers with source,
   crop, mask, alpha and priority. A test harness renders synthetic recipes.
3. **Element extraction.** Rect patches with colour key, BG/OBJ layers onto
   transparent targets, and game text windows. Each extractor gets a test.
4. **Scene classifier.** Process stack, field sub-screen mode, battle view
   state and boot state to a recipe id. A scene that cannot be classified is
   a bug, not a fallback.
5. **Input.** Canvas hit regions, pointer mapping, drag, and the existing
   keyboard/gamepad path. Every visible control has a region.
6. **Field and C-Gear.** Field world plus the C-Gear bar or button, and the
   field text windows.
7. **Start menu panel.** The mock's right-side panel, driven by the game's
   menu entries, with scrolling and the chevron.
8. **Full-canvas menus**, one at a time: Bag, Options, party/Pokémon list,
   summary, Pokédex, trainer card, save, PC, phone, Mystery Gift,
   multiplayer/session.
9. **Battle.** Scene, text, action/move/target/bag/party controls, then the
   transitions and effects that use both screens.
10. **Boot and title.** Logos, copyright, opening, title screen and title
    menu fused per the inventory. Then name entry and new-game intro.
11. **Ratios.** Anchors and safe margins already exist; add a wider canvas
    and verify each recipe at the new aspect.

Each step ends with the scenes it owns working at 256x192 and a test or a
capture showing the composition. A step may not leave a scene on the old
two-screen path.

## Tests

- **Recipe tests**: a synthetic source pair and a recipe produce the
  expected canvas (paper-computed pixels, no game data).
- **Extraction tests**: a BG/OBJ/window/colour-key extractor against a
  synthetic hardware state, and against a real scene where data is
  available.
- **Scene tests**: a fake process stack / sub-screen mode / battle step
  selects the expected recipe.
- **Input tests**: a pointer at a canvas point hits the expected control;
  keyboard/gamepad still reaches the game.
- **Capture tests**: the four mock states, plus each menu and battle state,
  rendered to PNG for comparison.
- **Regression**: the existing two-screen configuration remains available
  until the unified view owns every scene, then is removed deliberately.

## Open decisions recorded for implementation

- Where the C-Gear bar sits, and whether it is always visible or revealed by
  a button/key.
- The start panel's scroll behaviour when the game has seven entries.
- Which full-canvas screens keep the game's top screen as a backdrop versus
  replacing it.
- The battle control layout (the inventory proposes the first one).
