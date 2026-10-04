# Graphics backends: plan

Status of the renderer and where it goes. Written 2026-10-04.

## What runs today

- The 2D engine (backgrounds, sprites, priorities, blending) and the 3D engine
  (polygons, textures, lighting) are software models of the DS chips, written in
  C (`engine/g2d`, `engine/g3d`). They are the reference: every other renderer is
  tested against them.
- The presenter (`engine/platform/plat_gl.c`) is the GPU part. It uploads each
  screen as a texture and scales it to the window with one OpenGL 3.3 core
  shader. SDL's own renderer is the fallback when OpenGL 3.3 is not available
  (also used by the dummy video driver in the headless tests, or with
  `EIDOS_RENDERER=sdl`).
- Internal resolution: the 2D layers have DS resolution, so a screen without the
  3D layer stays 256x192 and the GPU scales it. Only the 3D layer gains from a
  larger frame. The software 3D rasteriser costs about 360 ms per frame at 6x, so
  it is capped (see `BOOT_CPU_3D_MAX` in `demo/eidos_boot.c`) until the GPU
  rasteriser exists.

## Target backends

The renderer is to have four GPU backends behind one interface, plus the
software path as fallback and reference.

| Backend | Platforms | Role | Minimum |
|---|---|---|---|
| OpenGL 3.3 core | Windows, Linux, macOS (up to 4.1) | default, widest reach | any GPU with OpenGL 3.3 |
| Direct3D 12 | Windows | native Windows path | Shader Model 6 class GPU (RT64's requirement) |
| Vulkan | Windows, Linux, Android | native Linux path, high end | Vulkan 1.2 (RT64's requirement) |
| Metal | macOS, iOS | native Apple path, replaces deprecated OpenGL | Argument Buffers Tier 2 (RT64's requirement) |
| Software (C) | everywhere | fallback for weak hardware, test reference | none |

Order of work: OpenGL 3.3 first (in progress), then the interface is frozen from
what OpenGL needed, then the others. A project that needs only one of them can
stop early: OpenGL covers every platform.

## Why this set

What comparable projects use (checked 2026-10-04):

- RT64 (the renderer of Zelda64Recomp and N64Recomp): Direct3D 12, Vulkan and
  Metal. It needs D3D12 Shader Model 6, Vulkan 1.2 or Metal Argument Buffers
  Tier 2, so it excludes older GPUs.
- Ship of Harkinian (libultraship, Fast3D): Direct3D 11 (Windows default), OpenGL
  (all platforms) and Metal; a Vulkan backend is in review.
- melonDS: a classic OpenGL renderer that needs OpenGL 3.1, and a compute shader
  renderer that needs OpenGL 4.3 and reproduces the DS rasteriser more closely
  with upscaling. The compute renderer cannot run on macOS, which stops at 4.1.
- macOS: OpenGL has been deprecated since 10.14 and is frozen at the 4.1 core
  profile. It still works, including on Apple Silicon, and Apple has announced no
  removal date. Metal is the long term route there.

SDL2 gives the window, input and audio only; its 2D renderer cannot run custom
shaders. SDL3's GPU layer wraps Vulkan, Direct3D 12 and Metal and may replace
hand written backends later; eidos is on SDL2 today.

## Design rules for the interface

- The backend takes a snapshot of the frame (geometry, decoded textures, the
  register state) and never reads emulated memory itself; the game threads
  produce the snapshot, the thread that owns the graphics context consumes it.
- The 2D compositor stays on the CPU at DS resolution (it is cheap) and hands
  the backend two small images per 3D screen: the screen with the software 3D
  and the screen with the 3D layer off. The backend draws the 3D at any
  resolution and composites it in a shader.
- The software 3D rasteriser always runs at 1x as well: capture (DISPCAPCNT),
  the 3D-over-2D priority mask and the tests need it.
- Every backend is tested against the software output at 1x on fixed scenes with
  a stated tolerance.
- No backend may change the OS display mode (fullscreen is desktop fullscreen).

## Not done yet

Edge marking, fog and anti-aliasing passes, the rear-plane bitmap, shadow
polygons and `DISP_1DOT_DEPTH` on the GPU path; any of these missing falls back
to the software frame for that scene.
