# Booting the game's own code

Milestone 2's last open item and Milestone 6's third are the same piece of
work: run the game's real start-up, not a demo's, on the host. aletheia's
ARM9 binary is matched C end to end (every game function and every SDK
function linked into it), so the code exists. This document is the plan for
running it, and the contract the pieces are built against.

## What runs

The ARM9 entry point (`crt0`) calls the game's `NitroMain` at `0x02005124`
(Thumb, 180 bytes). `NitroMain` calls the system initialiser `0x02005328`
(`OS_InitTick`, `OS_InitAlarm`, `RTC_Init`, `GX_Init`, `FS_Init`, the DWC
utility arena, the power manager), then enters the frame loop.

`tools/closure.py` seeded at NitroMain's eighteen direct callees reaches
about 1330 matched translation units (1471 when the files the port's shims
replace are followed too); "Linking the boot" below is what it takes to
link them. Everything past the first overlay load
(title screen, field, battle) lives in overlays, which are about 61% matched;
those screens wait on aletheia, and `docs/title-closure.md` lists exactly
which unmatched functions block the title screen.

`NitroMain` itself is not matched C in aletheia: its disassembler never
traced it (it is reached through a register from `crt0`), so the listing
carries its 180 bytes as data words. The port supplies it as
`stubs/reimpl_02005124.c`, written from the disassembly, until aletheia
matches it.

## Three layers, and where each SDK name goes

1. **Matched game code**, compiled from aletheia unchanged (portability
   rule 1). This includes most of `src/matched/arm9_sdk/`: SDK functions
   that only compute or touch memory and registers run as written.
2. **The DS address space** (`sdk/dsmem.h`, `sdk/dsmem.c`): host memory
   standing in for the DS's fixed regions, so a matched store to
   `0x04000208` lands somewhere real and the rest of the port can read it.
3. **High-level stand-ins** for what cannot run as compiled ARM code on a
   host: the CPU (CPSR, interrupt entry, thread context switch), the ARM7
   on the other end of PXI, DMA engines and the cartridge. These are port
   shims under `sdk/`, one file per SDK module, the existing convention.

The rule for a given SDK function: **compile the matched source when its
behaviour is memory and register traffic; write a shim when it depends on
the CPU or on the ARM7.** Each shim's header comment says which rule put it
there.

## Fixed addresses: the build rewrites them, aletheia does not change

2807 casts in the matched corpus turn an integer literal into a pointer
(`*(vu16 *)0x04000208 = 1`): 2227 into I/O, 296 into main RAM, the rest into
palette, VRAM, OAM and the shared work area. On a host those are wild
pointers.

Mapping real memory at the DS's addresses would make every one of them work,
but it is not portable: macOS reserves the low 4 GiB of a 64-bit process
(`__PAGEZERO`) and iOS cannot lift that at all, and a fixed mapping is a
fight with ASLR everywhere else. eidos targets all of them, so the port
does it in the build instead.

`tools/dsaddr.py` copies each matched source into the build tree and
rewrites every *integer-literal-to-pointer cast whose value is in a DS
region* into

```c
((T *)bw_ds_ptr(0x04000208u))
```

`bw_ds_ptr` (in `sdk/dsmem.h`) translates a DS address into the host block
that stands for it. The rewrite is purely textual on the cast, so the
compiled code is the matched code with one function-call-shaped address in
place of a constant; nothing else moves. The 342 I/O literals that are not
inside a cast (`#define REG_BASE 0x04000000`, arithmetic, comparisons) are
left alone: `tools/dsaddr.py` lists them, and each one that a built file
reaches gets a port-side wrapper before that file joins the build.

Literals that name a game global's own address are not rewritten: a matched
file reaches globals through `extern` names, which `sdk/game_globals.c` and
the generated data definitions resolve as ordinary host objects.

## The DS address space (`sdk/dsmem.h`)

Regions the port backs with host memory, by DS address:

| DS range | What | Host backing |
|---|---|---|
| `0x02000000`-`0x02FFFFFF` | main RAM (16 MiB, TWL) | first the registry of game objects (a global defined by the port registers its DS address, so a literal cast and an `extern` name reach the same bytes), then a host block for the rest (the shared work area at `0x027FF000`/`0x02FFF000` and friends) |
| `0x03000000`-`0x03FFFFFF` | shared WRAM | 256 KiB block |
| `0x04000000`-`0x04FFFFFF` | I/O registers | a register file with read and write hooks |
| `0x05000000`-`0x050007FF` | palette RAM | 2 KiB block |
| `0x06000000`-`0x06FFFFFF` | VRAM, banked | the nine banks A-I, mapped through VRAMCNT |
| `0x07000000`-`0x070007FF` | OAM | 2 KiB block |

The register file is plain memory with optional hooks: a write hook runs
after the store (VRAMCNT remaps banks; IF is write-one-to-clear; DMA
control starts a copy), a read hook supplies live values (VCOUNT, KEYINPUT,
the IPC FIFO). The renderer reads the same registers and memory the game
wrote (DISPCNT, BGxCNT, scroll, BLDCNT, WIN, VRAM, palette, OAM), so the
picture is the game's own state, not a parallel description of it.

`sdk/dsmem.c` takes no locks and initialises nothing lazily. The game runs
one thread at a time (the run token below), so the address space is only
ever touched by the thread holding it; and `bw_ds_reset` builds every table
explicitly (regions, register file, VRAMCNT hooks, the bank mapping, the
registry with `sdk/game_globals.c`'s globals) before any game thread exists.
A translation before the first reset panics rather than initialising on
first use. A reset clears the registry and every I/O hook: a module that
registers its own objects or hooks its own registers does so again after
each reset. The approximations (flat TWL main RAM, no WRAMCNT banking, the
first bank owning a page two banks overlap) are listed in the file's header.

## Threads, interrupts and the frame

NitroSDK threads are preemptive only at interrupt boundaries and the game
treats them as such. The port runs each `OSThread` on a host thread under a
single run token: exactly one game thread runs at a time, and the token is
handed over where the DS would switch (`OS_SleepThread`, `OS_WakeupThread`,
`OS_YieldThread`, the end of an interrupt). That is portable (C11 threads,
pthreads, Win32), needs no assembly or `ucontext`, and keeps the game's
single-core assumptions true. Interrupts are delivered on the main thread at
the points the DS would deliver them: vblank from `engine/frame/`, timers
and alarms from `OS_GetTick`'s clock.

## Linking the boot

`-DEIDOS_BOOT=ON` links NitroMain and its closure into `eidos_boot`
(`cmake/boot.cmake`, `tools/bootlink.py`). The matched files are compiled
unchanged; what a host linker needs beyond them is generated, per build,
from aletheia's layout and the port's own sources.

**The closure.** `tools/closure.py` reads, for each matched file, every name
it needs from elsewhere: `extern` declarations (with attributes, struct
bodies and several declarators), file-scope prototypes, and calls with no
declaration at all. A name resolves by spelling first, then by DS address
(the file's `symbols:` block, a `func_`/`sub_` spelling, the SDK table), and
an address resolves to the matched unit that defines a function there even
when the manifest lists only the unit's lead function. Names whose address
is their value (`SDK_IRQ_STACKSIZE`, `ov010_id`) are their own class,
`const`. The walk stops at the files a shim replaces.

**What is left out.** Three kinds of matched file are not compiled:

- the files a port shim replaces: the single list `EIDOS_BOOT_SHIM_REPLACED`
  in `cmake/boot.cmake`, grouped by shim. The configure fails if a file in
  the closure defines a name a shim defines and is not on the list;
- the files whose bodies are mwcc `asm` (88 in the closure). A file
  `sdk/asm/<same name>.c` takes the place of one automatically; until it
  exists, every function the file defines is a stub;
- the files the eidos library already compiles.

**Names by address.** The DS links by address, a host by name. Each
translation unit gets a forced-include header (`-include`, `/FI`) that
renames, for that file only, each spelling to the defined one (`RTC_Init` to
`func_02088788`, `func_02088610` to the shim's `PXI_SendWordByFifo`), about
940 renames in all. Where one file names one object twice with two types,
the second spelling reads through a pointer to the first instead.

**Implicit declarations.** 34 callees in 22 matched files have no
declaration; mwcc compiled them as `int f()` and GCC 14 rejects them. The
header declares each as `int f()` (C99's non-prototype form, the
declaration the DS code was compiled against), or `long long f()` when its
definition returns 64 bits.

**Link-time constants.** 13 names are linker symbols whose address is the
value. The header turns each into `(*bw_lc_NAME)`, a pointer whose value is
the constant; a value inside the DS address space is `bw_ds_ptr(value)`,
set by `bw_boot_link_init()` after `bw_ds_reset`. So
`SDK_AUTOLOAD_DTCM_START` and `SDK_AUTOLOAD_MAIN_TWL_START` both name the
bytes at DS 0x02FE0000: the vblank handler's flag, `OS_WaitIrq` and
`sdk/os_interrupt.c`'s check word are one word. DTCM objects no shim defines
are pinned the same way.

**Stubs, generated and listed** in `<build>/boot/gen/boot_link_report.txt`:

- `boot_data_stub.c`: the ARM9 data objects nothing defines (about 270),
  zero-filled and registered at their DS addresses by
  `bw_boot_data_register()`. TEMPORARY: the data lane's definitions,
  passed as `EIDOS_BOOT_DATA_SOURCES`, replace it name by name.
- `boot_link.c`: a panicking stub for each function nothing defines,
  marked `open` (about 25: unmatched game code, ARM9 calls into overlays,
  veneers) or `asm body` (70, waiting for `sdk/asm/`).

With the stubs, `eidos_boot` links warning-free at both pointer widths. At
ILP32 it starts, runs into `func_02005328`'s first calls and stops at the
first asm stub it reaches (`OS_GetInitArenaHi`). At LP64 the boot's matched
files raise 532 pointer/integer cast warnings, silenced for those files
only: it links, and it is not expected to run (docs/pointer-width.md).

## Lanes

Each lane owns the files named here and nothing else, works in its own
worktree, and is integrated by one person.

| Lane | Owns |
|---|---|
| dsaddr | `tools/dsaddr.py`, its CMake hook, `tests/test_dsaddr.*` |
| dsmem | `sdk/dsmem.c`, `tests/test_dsmem.c` (the header is fixed) |
| nitromain | `stubs/reimpl_02005124.c`, `demo/eidos_boot.c` |
| os | `sdk/os_thread.c`, `sdk/os_irq.c`, `sdk/os_alarm.c`, `tests/test_os*.c` |
| arm7 | `sdk/pxi.c`, `sdk/arm7/*.c` (PM, RTC, CARD, CTRDG, SND replies) |
| render | `engine/g2d/hwcompose.c`, `tests/test_hwcompose.c` |
| fs | `sdk/fs.c` additions, `sdk/overlay.c` |
| link | `tools/closure.py`, `tools/bootlink.py`, `cmake/boot.cmake` |
| title | `docs/title-closure.md` (analysis only) |
| msvc | `cmake/msvc.cmake`, `tools/check.py` MSVC configurations |

## Modding is a design constraint, not a later feature

eidos is meant to carry new Pokemon, moves, sprites, NPCs, maps and AI
(Milestone 8, and the mod suite built on it). Nothing here may freeze a DS
limit the port owns:

- Game tables are host objects registered with `bw_ds_register`, not fixed
  DS memory, so a table can be defined larger than the cartridge's copy and
  the matched code that indexes it keeps working; the limit that remains is
  the one written into matched code (a count compared against a constant),
  and those sites are recorded as they are found, not worked around.
- Assets reach the game through the VFS and its mod overlay (`sdk/vfs.c`);
  member-level NARC overrides are the next step there, so a mod adds one
  sprite or one map without repacking an archive.
- Where the port substitutes behaviour (a shim, an overlay registry entry, a
  hook point), it is a replaceable function or table, not hard-wired, so AI,
  scripts and procs can be swapped by a mod later.

