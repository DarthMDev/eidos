# Compiling aletheia's src/game

aletheia keeps its matched code twice. `src/matched/` is the record of the
matching work: one function, or one small unit, per file. `src/game/` is the
program as it was most likely written: one `.c` per original source file
(translation unit, TU), with real names, one declaration per function and the
fixes the restructure made. The port compiles `src/game/`. This document is
the plan for that move and its state.

## Why

- The fixes live only in `src/game/`. A call that relied on a register passing
  through on the DS (`PARTICLE_KillAllEffects()` in `PARTICLE_DeleteSystem`,
  which reads its argument from r0) passes the argument there; on a host the
  callee otherwise reads garbage. Measured on calls whose argument count is
  below the callee's parameter count: 231 call sites in the `src/matched/`
  files the boot compiles, 65 in the whole of `src/game/`.
- The renames, the corrected manifests and the merged declarations are made in
  `src/game/`; `src/matched/` keeps the names it was matched under.
- The finished decompilation is `src/game/` (aletheia's `src/game/README.md`).

## What changes

| Part | `src/matched/` today | `src/game/` |
|---|---|---|
| The unit of compilation | one function or small unit per file | one original source file, often dozens of functions |
| Where a function's file is found | the manifest's `source` field | `tools/gametree.py`: the file headers' `function:` lists, each name's address from the manifests |
| The ARM9 boot closure | files pulled in one by one | whole TUs pulled in, so more code compiles and more callees are reached |
| A function the port supplies itself (a shim in `sdk/`, an `sdk/asm/` body, a `stubs/reimpl_`) | the whole matched file is left out (`EIDOS_BOOT_SHIM_REPLACED`, by path) | the function's definition is cut out of the TU's copy, by name; the rest of the TU compiles |
| An `asm` body | the file is replaced by `sdk/asm/<file>.c` | the body is cut out of the copy and supplied by name from `sdk/asm/` |

## The steps

1. `tools/gametree.py`: src/game's TUs as manifest-shaped entries. Done.
   1367 TUs, 35189 functions, 34919 with an address; the rest are veneer
   spellings and runtime routines, found by name.
2. The cut: a copy step beside `tools/dsaddr.py` that removes named function
   definitions and every `asm` body from a TU's copy, leaving a prototype, so
   the port's own definition is the only one in the link.
3. `tools/closure.py`, `tools/bootlink.py`, `tools/ovlink.py`: an index built
   from `tools/gametree.py` when the build compiles src/game, keyed the same
   way, so the walk, the boot generator and the overlay linker run unchanged
   above it.
4. `cmake/boot.cmake` and `CMakeLists.txt`: the boot's seeds, the shim list
   and the eidos library's matched files named by function instead of by
   `src/matched/` path; a CMake switch, `EIDOS_SOURCE_TREE` (matched or game),
   while both work.
5. The pin moves to an aletheia revision with src/game, the tests run, the
   title and NEW GAME runs reach at least where they do from src/matched, and
   the switch's default becomes `game`.
6. `stubs/` and `sdk/asm/` drop what src/game made unnecessary.
