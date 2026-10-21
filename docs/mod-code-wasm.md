# Code mods in WebAssembly: the agreed design

Status: design only, agreed with kenon on 2026-10-21. Nothing here is built
yet. It becomes mod API version 4.

## Summary

- A code mod ships one portable file, `code = mod.wasm`, run by an
  embedded runtime. The same file works on every platform.
- Mods are written in C against generated headers and built by kenon
  (Zig) to `wasm32-freestanding`. No WASI: a mod reaches nothing outside
  the game except through imports.
- Native libraries (`.dll`, `.so`, `.dylib`) stay as a developer tier that
  the player must enable explicitly ("unsafe native mod"). Their main use
  is debugging.
- The sandbox protects the player's machine, not the run: a mod that
  writes game memory can still break the game, as a native mod can.

## Runtime

WAMR, fast interpreter, by default (Apache-2.0 with the LLVM exception;
CMake; Windows, Linux, macOS, Android, iOS). Only `.wasm` ships. An AOT
cache compiled on the player's machine may come later. wasm3 is not used
(in maintenance mode, interpreter only).

Measured by kenon (Rust mod, same code three ways): an empty hook call is
1.3 ns native, 24.5 ns compiled wasm and 47 ns interpreted; a full AI turn
is 1.7, 3.0 and 33 microseconds. A frame is 16,700 microseconds.

## Seams kept for a later Lua tier

The bridge is built so that a second scripting backend (Lua 5.4 or Luau)
can be added without changing the registry, the hook slots or the
manifest.

1. One language-neutral API description is the single source:
   `sdk/api/v<N>/` holds `events.json` (name, id, payload fields with C
   type, offset, writable, refusable), `services.json`, `hooks.json` (the
   declared set with each row's prototype and region), `settings.json`, and
   the struct accessor description. The C headers (`mod_api.h`,
   `mod_state.h`, the struct accessors, `eidos_wasm.h`) and the wasm import
   table are generated from it; a Lua binding would be generated from the
   same files. A check fails the build when the generated snapshot and the
   committed one differ without a version bump.
2. A runtime-neutral host layer. The event registry, services, hook slots,
   settings, logging and per-mod state talk to one internal "mod instance"
   interface: call a handler, call an export, read or write game memory,
   marshal a payload, and the per-instance limits. WAMR implements it; a Lua
   backend would be a second implementation of the same interface.
3. One manifest key. `code =` names the file and the kind is taken from the
   file: the wasm magic, a native library header, or for a text script its
   extension (`.lua`).
4. Values, never host pointers. Payloads, hook arguments and game memory
   cross the boundary as integers, DS addresses (u32) and byte copies. A
   game pointer is the object's DS address (exact in the identity-mapped
   32-bit reference build; `bw_ds_addr_of` for host objects, which also
   holds on a 64-bit build). Memory goes through bounds-checked imports
   (`rd8/16/32`, `wr8/16/32`, `copy`).
5. Limits per instance, owned by the interface: memory cap (default
   64 MiB, manifest `memory = <MiB>`), a step budget per call (WAMR's
   instruction metering; a Lua hook count for Lua), and re-entrancy depth
   (a mod calls a service that fires an event back into the same mod).
   Wasm execution is deterministic, which the co-op and net services rely
   on; a Lua backend must keep that (no `os`, `io` or unseeded randomness).

## The wasm module (provisional until api 4 ships)

- Import module `"eidos"`. Logging is `eidos.log(ptr, len)`.
- Exports: `eidos_mod_start() -> i32` (0 = ok), `eidos_mod_stop()`, the
  function table, and `memory`. The instance is the mod's state.
- Handlers are exported functions registered by table index:
  `eidos.on(kind, index)`, `eidos.override(key, index)`; inside a `wrap`
  hook, `eidos.original(...)` calls the next link.
- Stack 64 KiB (template linker flag). The template brings a small
  allocator and the compiler-rt builtins (no libc).
- Threads: the port runs one game thread at a time (the run token), so a
  WAMR thread environment per host thread that calls in is enough.
- Callbacks the game stores (task callbacks, comparators) go through a
  bounded pool of host trampolines that dispatch to wasm exports.

## API versions

Version 4 adds wasm. The port accepts a list of versions (3 and 4), so
native mods built for 3 keep loading.

## Order

Content limits first (items, then moves; docs/research/limits/), the
`.bw` reader (spec in kenon's docs/bw-format.md), then this bridge: events
and services first, overrides and memory access second. The struct
accessor generator lives in eidos `tools/`, reading aletheia's
self-contained headers at the pinned revision.

## kenon's side

The C template, the editor and the Zig build, manifest validation, the
message-bank encoder and NARC member writers, `.bw` packing, a debug runner
using eidos's `--mod <folder>` and `--mod-log <file>`, and one sample mod
per feature that both sides use as a conformance test.
