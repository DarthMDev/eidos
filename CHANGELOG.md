# Changelog

Each release's notes on GitHub start with its section here
(tools/release_notes.py). Newest first.

## 0.1.6

- One screen: a new layout that shows the game on one screen. The world
  fills the window, wider than the DS, and the menus, the Bag, the
  Pokedex, the party, battles, Options, Save, the trainer card, the PC,
  name entry and the title screen have a new interface in the game's own
  colours, font and icons. Everything works with the mouse; the keyboard
  and pads work as before. Start it with Widescreen in the launcher, or
  choose ONE in Options, DISPLAY, SCREENS. Screens without the new
  interface show the two DS screens side by side.
- Names can be typed on the keyboard in the one-screen name entry.
- Multiplayer: a player who left no longer stays on the map; the other
  player stays visible while a menu is open; avatars keep up with running
  and cycling; fast-forward is off in a session; a joiner sends much less
  data.
- The trainer card no longer crashes the game.
- The joiner's waiting text is no longer drawn twice.
- The name for multiplayer is asked when Multiplayer first opens.
- The volume starts at 50%.
- The title menu no longer offers NINTENDO WFC SETTINGS, which closed
  years ago and crashed the game.
- The launcher updates itself when it starts. If an update fails, the
  launcher keeps running on the version it has. Install this version once by
  hand; later ones arrive by themselves.
- A new look for the launcher, two play buttons (Classic and Widescreen),
  and a game data folder per profile, with a safe move of existing data.

## 0.1.5

- 3D outlines at high resolution match the DS: no more gap and thin line
  under objects standing on the floor, and no comb or flicker along edges.

## 0.1.4

- Instant text: each page of a message appears at once, whatever the text
  speed. It is on the new Extras tab in Settings and the EXTRAS page in the
  game's Options.
- Launcher: shows its real version, outlines the folder, copy and remove
  buttons, and highlights text boxes and lists on hover.

## 0.1.3

- The port's settings inside the game's Options screen: DISPLAY and SOUND
  pages next to the game's own, changed with the arrows in the bottom bar or
  with L and R.
- No console window next to the game. Start `eidos --console` to get one for
  a bug report.
- Launcher: the version list keeps working when GitHub limits requests.

## 0.1.2

- Two display settings, each Auto or a fixed size: Pixel scale (how big each
  DS pixel is) and 3D resolution (how detailed the 3D is). The 3D is now
  sharp by default.
- The window opens at a size that fits the screen.

## 0.1.1

- Escape no longer closes the game. Use the window's close button.

## 0.1.0

- First release. The game asks for your own ROM on the first start.
- Multiplayer through the public relay: host a game and share the code.
- The launcher: profiles, versions and mods.
