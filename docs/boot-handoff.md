# Boot run: hand-off (wave 4, bootrun2)

Where the native boot stands at the end of this lane, how to see it, and
what is left. docs/boot.md is the design; this is the state.

## Furthest point

The game's own boot runs to the **title menu**. In order:

| Frame | What | Resident overlays |
|---|---|---|
| before 0 | system and heap set-up, the boot sequence (`func_0200523C`): ov010 and ov011, the network set-up in ov009 and ov013, the save layout (69 blocks), ov228's save-data set-up | 10 11 |
| 30 | the Nintendo and The Pokemon Company logos | 10 11 88 |
| 240 | the copyright screen | |
| about 600 | the opening cinematic | 10 11 179 |
| about 4600 | the title screen, "PRESS START" | 10 11 88 |
| 4800 + START | the title menu: NEW GAME, MYSTERY GIFT, NINTENDO WFC SETTINGS, MIC TEST | 10 11 88 |

With no input the game loops back to the opening (its attract loop); 20000
frames ran headless without a stop. With START pressed at frame 4800 the
menu opens and runs (9000 frames, no stop). Whether the menu reacts to
further input (choosing NEW GAME) has not been tried.

Last clean build: MinGW i686 (ILP32), `EIDOS_BOOT=ON`, against aletheia's
main checkout as it was before commit a359b6488 broke it (about 46% of
1542 files stopped compiling, among them `func_02039ACC.c`'s duplicate
parameter name). A rebuild against the pin 71f6d529b (a detached aletheia
worktree with junctions for its untracked `extract/`, `asm/` and
`tools/gcc-host/`) was configuring when the lane stopped; its result is not
known.

## Reproducing it

```sh
T=<aletheia>/tools
export PATH="$T/gcc-host/xpack-mingw-w64-gcc-14.2.0-1/bin:$T/cmake/cmake-3.31.6-windows-x86_64/bin:$T/ninja:$PATH"
cmake -S . -B b32 -G Ninja -DALETHEIA_ROOT=<aletheia> \
      -DCMAKE_TOOLCHAIN_FILE=cmake/ilp32-toolchain.cmake -DEIDOS_BOOT=ON
cmake --build b32 --target eidos_boot
D=<aletheia>/extract/data
b32/eidos_boot --data-root $D --frames 6000 --press START@4800 \
               --screenshot 30,240,3000,4800,5000 --screenshot-dir shots
b32/eidos_boot --data-root $D --window          # play: Enter is START
```

`--screenshot` writes `shots/eidos_boot_frame<N>.png` (both screens, top
above bottom). A run that stops prints the frame it reached and the
resident overlays with the report.

## Tests

- `test_boot_run` (ILP32, data-gated): `--frames 6000 --press START@4800`,
  passes when all 6000 frames run and the resident overlays at the end are
  `10 11 88` (the menu). A stop passes only if it matches the known stop
  and is at or past frame 6000 (`cmake/boot.cmake` sets FRAMES, MIN_FRAMES,
  PRESS, RESIDENT; update them as the boot gets further). Verified by hand
  on the last clean build; the full ctest run against the pin is pending.
- `test_boot_window`: the same with `--window` through SDL's dummy drivers,
  300 frames; it guards the window thread and audio queue fix.
- `test_dsmem` (unregister, registered, identity) and `test_armdata` (the
  code-pointer scan) gained checks.
- LP64 MinGW default build (no boot): 54/54 tests pass, warning-free.

## Open items, in order

1. Verification left unfinished: the ILP32 full ctest against the pin
   (`EIDOS_BOOT=ON`), an LP64 build with `EIDOS_BOOT=ON` (it must link
   warning-free and the boot must refuse cleanly), and `tools/check.py
   --only msvc-x86 --define EIDOS_BOOT=ON` (MSVC may need `/wd4047` for the
   matched code's pointer-to-int stores that GCC needed
   `-Wno-int-conversion` for; the new `sdk/asm` files and
   `engine/platform` push mode have not been compiled under MSVC).
2. Past the title menu: choose NEW GAME (`--press A@<frame>`) and run into
   the next screens (the overlays the integrator named, ov204 and ov194),
   fixing each stop the same way.
3. aletheia items still stood in by the port (next section).
4. Known approximations, each documented where it lives:
   - identity mapping: VRAM mirrors past each engine window's period are
     not backed (a raw access there faults); a bank keeps stale bytes at
     the address it leaves; registered port objects answer `bw_ds_ptr`, but
     a raw pointer into their DS range reaches the region's bytes;
   - overlay host objects: two names at one address (ov013's `d_021B4BA0`
     and `g_021B4BA0`) are two objects, the second unregistered; an object's
     size is a guess (up to the next known address), so a write past it
     lands in host memory;
   - the code-pointer scan matches exact function entries only in the data
     ranges; a pointer to a function the build lacks stays a DS address and
     faults with "(a DS address)" in the report;
   - audio is rendered on the frame thread and queued; when the queue is
     more than four frames ahead a frame's output is dropped;
   - the engine is compiled `-O2` only without a build type (GCC, Clang);
     MSVC builds keep their configuration's flags.

## Port stand-ins aletheia could replace

| File | Address | Why it exists |
|---|---|---|
| `stubs/reimpl_0206434C.c` | 0x0206434C (`env_setresourceset.c`, SDK name ENV_SetResourceSet) | its callers pass (animation, sequence); the C names one parameter and the sequence travels in r1 to func_02062FF0. Defined as `func_0206434C` because `sub_0204968C.c` uses the ENV_SetResourceSet spelling for 0x020307B0 |
| `stubs/reimpl_0204D340.c`, `reimpl_0204D360.c`, `reimpl_0204D380.c` | 0x0204D340, 0x0204D360, 0x0204D380 | each forwards its incoming r1 (the sequence index) to func_020627D8(bank, idx) |
| `stubs/reimpl_0203064C.c`, `reimpl_02030678.c`, `reimpl_02030608.c` | 0x0203064C, 0x02030678, 0x02030608 | earlier lanes: a heap argument forwarded in r2 |
| `stubs/reimpl_02036FD8.c` | 0x02036FD8 | an ARM9 table's DS address passed as an integer (harmless under identity mapping; the reimplementation still serves the fallback) |
| generated forwarders (`<build>/boot/thunk/*.c`, listed in `boot_thunks.txt`) | 44 thunks | `void f(void) { g(); }` tail calls that pass every argument and the result through; aletheia's C could say `T f(args) { return g(args); }` if mwcc keeps the bytes |

The port C for mwcc asm bodies (`sdk/asm/`) is not something aletheia
replaces; it is the port's answer to an `asm` file. This lane added:
MTX_RotX33_, MTX_RotY33_, MTX_RotZ33_, MTX_Scale43_, func_0207AD58
(MTX_Scale22_), func_0207ADC4 (MTX_Identity33_), func_0207B598
(MTX_Identity43_), func_0207BDB4 (MTX_Identity44_), func_0207B5C0,
func_0207ADE8, func_0207BDE0 (the MTX copies), func_020307C8, MI_Copy48B,
MI_Zero36B, MI_CpuMove, func_02082B0C (MIi_CpuCopyFast), func_02034FD4,
func_02034FE8, func_02062810, func_0206295C, func_020627F0, DWCi_TSKlForm,
CARD_SetPulledOutCallback, CTRDGi_CallbackForSetPhi, func_02066E10,
GX_SetBankForLCDC, GX_BeginLoadClearImage, DWCi_IsFold, OS_ReferentSize,
func_02087EA0 (CLZ).

Every hidden or unmatched function this lane met (the save block callbacks,
the heap allocator's vtable functions, the G3D joint-scale function at
0x02070CD8, ov009 0x02157138, ov010 0x02161D98 and 0x0216E474/C, ov011
0x021763BC) has been matched in aletheia since, and the stand-ins are gone.
The boot's configure step reports a clash ("NOT EXCLUDED, defines a shim's
name") when a matched file arrives for a name port code still defines,
which is the signal to delete the port file; ovlink does the same for
`stubs/ov/` (reported obsolete).

## Half-done

- `tools/regargs.py` classifies register-argument sites from the listing
  by a backward scan in straight-line code; aletheia now has its own
  flow-following version. The eidos copy stays for checking a build.
- `docs/title-closure.md` still describes the older blockers; it was not
  rewritten.
