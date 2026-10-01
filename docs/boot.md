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

`tools/closure.py` seeded at `0x02005328` reaches 69 matched translation
units and **no unmatched game function**. What the port has to supply is 48
SDK names and 45 ARM9 data objects. Everything past the first overlay load
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
left alone and found the honest way: a host fault at a DS-shaped address is
diagnosed by `bw_ds_ptr`'s companion `bw_ds_fault_hint`, and the site gets a
port-side wrapper.

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
| title | `docs/title-closure.md` (analysis only) |
| msvc | `cmake/msvc.cmake`, `tools/check.py` MSVC configurations |
