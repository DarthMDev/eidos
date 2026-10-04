# Future work

What is known to be missing or worth adding, written 2026-10-04. Items marked
(prior art) come from what other recomp and emulator projects do; see the
"Prior art" section of docs/graphics-backends.md for the sources.

## Known gaps in what exists

- **Tearing.** The presenter swaps with an interval of 0, so a frame can tear.
  A vsync option needs the pacer and the audio to follow the display (dynamic
  audio rate control, as emulators do) so a 59 Hz or 144 Hz display does not
  change the game speed. (prior art)
- **Fullscreen and Borderless are the same.** Both use desktop-resolution
  fullscreen, which never changes the OS video mode. Either merge them into one
  setting or give Borderless a real meaning (a borderless window that is not
  fullscreen, for window managers that treat them differently).
- **Saved window size.** `display.width` and `display.height` are stored but the
  window always opens at the DS size for the layout. Decide: remember the last
  non-maximized size (never a maximized one), or drop the two options.
- **GPU 3D gaps.** The GPU rasteriser (engine/platform/plat_gl3d.c) does not do
  edge marking, fog, the rear-plane bitmap or shadow polygons. A frame that uses
  one is drawn by the CPU rasteriser at a capped scale. Edge marking needs a
  polygon-ID attachment and a post pass; fog needs the depth buffer and the
  FOG_TABLE; the rear-plane bitmap is a textured full-screen quad with depth.
- **2D over 3D.** A translucent 2D layer or sprite blended over a GPU 3D layer is
  lost: the composite shows the GPU frame wherever the 2D screen differs from the
  3D-off screen. The fix is to keep the layer-priority and blend information per
  pixel (a mask plus the 2D colour and its blend factor) and blend in the shader.
- **3D alpha.** Where the GPU draws a translucent pixel over a translucent one the
  alpha is "over"; the CPU rasteriser keeps the larger alpha.
- **Capture scenes stay at DS resolution.** The display capture is read back at
  256x192 (the title and logo scenes capture every frame). Raising them needs a
  high-resolution shadow of the capture target, used while the game does not
  touch that VRAM bank from the CPU. (prior art: RT64 scales framebuffer
  effects and their region copies by the same factor)
- **Untested hosts.** Linux and macOS have never run; the OpenGL path needs
  checking there (macOS stops at OpenGL 4.1, which is enough for 3.3 core).
  Everything on Windows was checked with hidden windows and pixel readback, not
  in a visible window by a person.
- **The stylus** is not read on the settings screen.

## Graphics options other projects offer (prior art)

- An **Auto** internal resolution that follows the window (RT64), next to the
  fixed multiples. The GPU path can already do it; the CPU path keeps its cap.
- **Downsampling**: render at 2x or 4x the target and shrink (RT64), and
  **MSAA** with a choice of 2, 4 or 8 samples. eidos uses 4 samples when the
  game turns anti-aliasing on.
- An **integer scaling** option (pixel-perfect, with borders) next to the
  default fill-the-window picture.
- **Sharp bilinear** is in; further filters (a CRT or LCD-grid shader, an
  xBRZ-style 2D upscaler) are optional presenter shaders.
- Further backends: Direct3D 12, Vulkan and Metal (docs/graphics-backends.md);
  RT64 supports exactly those three.
- A **performance overlay** (frame time per phase) built on the existing
  `--profile` counters.
- **Texture replacement and mods** that supply their own textures and meshes
  (aletheia and eidos are built for modding; docs/eidos-modding.md).

## Settings screen

- The Options-style screen (engine/platform/mod_options_proc.c) reads the
  stylus, shows the cursor and markers exactly as the game's data places them,
  and moves its text into a message bank that a mod can translate.
- More sections as the port grows: audio (music and effects separately),
  controller options (dead zone), language, a reset to defaults per section.
