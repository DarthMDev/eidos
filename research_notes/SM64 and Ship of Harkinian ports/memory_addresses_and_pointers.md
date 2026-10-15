# Super Mario 64 Port and Ship of Harkinian: memory model, address handling, pointer width, struct layout

Scope: how the *Super Mario 64* decompilation + `sm64-port`/`sm64ex` lineage and the *Ship of Harkinian* (Ocarina of Time) lineage backed by `libultraship` (LUS) provide memory, translate or eliminate console (N64) addresses, and compile as 64-bit. Answers are ordered by the assigned key questions. "SoH" = Ship of Harkinian; "LUS" = libultraship. Note: much of the LUS/HarbourMasters evidence available is for the sibling port *2 Ship 2 Harkinian* (Majora's Mask, `2ship2harkinian`) and LUS itself, which share the same address/pointer architecture; I flag where evidence is from a sibling rather than OOT proper.

---

## What is the N64 memory map, and how do the ports represent it?

### Takeaway
The N64 has a 32-bit (sign-extended into 64) virtual address space with KSEG0 at `0x80000000` directly mapping RDRAM, KSEG1 at `0xA0000000` (uncached), and memory-mapped device registers and cartridge/SRAM regions at fixed physical addresses. Both ports *define* these constants but effectively compile out the translation on PC: KSEG0 becomes a vestigial constant and the direct-map arithmetic is neutralized.

### Cited Findings
- N64 virtual segments: `0x00000000–0x7FFFFFFF` KUSEG (TLB), `0x80000000–0x9FFFFFFF` KSEG0 (direct-mapped, cached), `0xA0000000–0xBFFFFFFF` KSEG1 (direct-mapped, uncached), `0xC0000000–0xDFFFFFFF` KSSEG, `0xE0000000–0xFFFFFFFF` KSEG3. For directly mapped segments, "addresses are directly translated to physical addresses by subtracting by the base address of the respective segment," limiting them to physical `0x00000000–0x1FFFFFFF`. Internally all addresses are 64 bits; in 32-bit mode the upper 32 bits are sign-extended — [N64brew Wiki, Memory map](https://n64brew.dev/wiki/Memory_map).
- N64 physical map: RDRAM at `0x00000000`, RDRAM registers at `0x03F00000`, RCP registers at `0x04000000–0x04FFFFFF` (SP `0x04000000`/`0x04001000`, DP `0x04100000`, MI `0x04300000`, VI `0x04400000`, AI `0x04500000`, PI `0x04600000`, RI `0x04700000`, SI `0x04800000`), PI cartridge bus regions (`0x08000000` SRAM/FlashRAM, `0x10000000` cart ROM), and SI bus (`0x1FC00000` PIF ROM/RAM) — [N64brew Wiki, Memory map](https://n64brew.dev/wiki/Memory_map).
- `2ship2harkinian` (LUS ecosystem) defines the constant but not the mapping: `#define KSEG0 0x80000000 // 0x80000000 - 0x9FFFFFFF  Physical memory, cached, unmapped`, `#define RDRAM_CACHED KSEG0`, and then `#define PHYSICAL_TO_VIRTUAL(addr) (addr) //((uintptr_t)(addr) + RDRAM_CACHED)` — the real mapping is commented out and replaced with the identity — [2ship2harkinian `mm/include/macros.h`](https://github.com/HarbourMasters/2ship2harkinian/blob/045edd624f93a7d1e793312514d2a4c6a77dc3cb/mm/include/macros.h).
- sm64 decompilation defines the same idea via conversion macros with the real bit arithmetic live on N64 and an identity fallback: `#define VIRTUAL_TO_PHYSICAL(addr) ((uintptr_t)(addr) & 0x1FFFFFFF)`, `#define PHYSICAL_TO_VIRTUAL(addr) ((uintptr_t)(addr) | 0x80000000)`, but under `NO_SEGMENTED_MEMORY` all become plain casts — [sm64-port `include/macros.h`](https://github.com/sm64-port/sm64-port/blob/master/include/macros.h) and [n64decomp/sm64 `include/macros.h`](https://github.com/n64decomp/sm64/blob/master/include/macros.h).
- The PC port build defines that switch: `PLATFORM_CFLAGS += -DNO_SEGMENTED_MEMORY -DUSE_SYSTEM_MALLOC` in the port branch of the Makefile — [sm64-port `Makefile`](https://github.com/sm64-port/sm64-port/blob/master/Makefile).

### Inferences
- The ports do not implement an N64 memory map or MMU. `KSEG0`/`RDRAM_CACHED` survive as documentation/legacy constants; the actual CPU never sees console virtual addresses because the OS translation is compiled out.
- "Memory-mapped registers at 0xA4/0xA6" (VI/AI/SI mapped through KSEG1) do not appear as such in port code; device access is replaced by LUS/port function calls (e.g. `osAiSetFrequency` is a pure computation, `osEepromLongRead/Write` are file I/O) — see [ultra_reimplementation.c](https://github.com/sm64-port/sm64-port/blob/master/src/pc/ultra_reimplementation.c).

### Gaps
- I did not find port code that lays out a table of the physical device-register map (`0xA4xxxxxx` etc.); the ports reimplement the `os*` API instead. Whether `0xA4`/`0xA6` appear anywhere in port sources was not exhaustively searched.

---

## Do the ports MAP console addresses into host memory, or REWRITE console addresses in the source?

### Takeaway
Both ports **remove/neutralize** console addresses rather than map them. Translation functions either become identity casts or are resolved once at init into real host pointers; addresses embedded in data are host pointers produced by the C compiler/linker or by an asset-export step. Neither port runs a live MMU-style mapping.

### Cited Findings
- Runtime DMA path treats the console "device address" as a host pointer directly: `s32 osPiStartDma(..., uintptr_t devAddr, void *vAddr, ...) { memcpy(vAddr, (const void *) devAddr, nbytes); ... }` — i.e. the N64 ROM/RDRAM address is dereferenced as a host pointer; and `uintptr_t osVirtualToPhysical(void *addr) { return (uintptr_t) addr; }` (identity) — [ultra_reimplementation.c](https://github.com/sm64-port/sm64-port/blob/master/src/pc/ultra_reimplementation.c).
- Display-list pointer fields are resolved by a bare cast, not a translation call: `static inline void *seg_addr(uintptr_t w1) { return (void *) w1; }` used throughout `gfx_run_dl` for `G_MTX`, `G_MOVEMEM`, `G_VTX`, `G_DL`, etc. — [sm64-port `src/pc/gfx/gfx_pc.c`](https://github.com/sm64-port/sm64-port/blob/master/src/pc/gfx/gfx_pc.c).
- The decomp keeps a 32-entry graphics segment table and resolution helpers, but they are bypassed on PC: `uintptr_t sSegmentTable[32];`, `set_segment_base_addr()` (masks `& 0x1FFFFFFF`), `get_segment_base_addr()` (or's `0x80000000`), and `segmented_to_virtual()` (computes `(sSegmentTable[segment] + offset) | 0x80000000`). Under `NO_SEGMENTED_MEMORY` the resolver collapses to `return (void *) addr;` and `load_segment*`/`move_segment_table_to_dmem` become no-op macros — [sm64-port `src/game/memory.c`](https://github.com/sm64-port/sm64-port/blob/master/src/game/memory.c), [sm64-port `src/game/memory.h`](https://github.com/sm64-port/sm64-port/blob/master/src/game/memory.h).
- On PC the Makefile compiles with `-DNO_SEGMENTED_MEMORY` and links assets as generated C/mio0 objects instead of loading them into console segments; the N64 linker-segment rules (`SEG_FILES`, `mio0`) are inside `ifeq ($(TARGET_N64),1)` — [sm64-port `Makefile`](https://github.com/sm64-port/sm64-port/blob/master/Makefile).
- LUS is explicitly a header-compatible wrapper around reimplemented libultra functions — "compile-time WINE for the N64" — and the porting process asks the porter to "Address pointer size differences and other architecture-specific issues" during a "64-bit Fixes" step rather than to implement address mapping — [Libultraship Porting Guide](https://libultraship.com/PORTING).
- LUS represents data by *resource identity*, not console address: the Fast3D renderer defines `MAX_SEGMENT_POINTERS = 16` and dedicated OTR opcodes for loading by hash/path rather than by address (`OTR_G_SETTIMG_OTR_HASH`, `OTR_G_SETTIMG_OTR_FILEPATH`, `OTR_G_VTX_OTR_FILEPATH`, `OTR_G_DL_OTR_HASH`, …) — [LUS `fast/interpreter.h` API](https://libultraship.com/api/interpreter_8h.html), [LUS `fast/lus_gbi.h` source](https://libultraship.com/api/lus__gbi_8h_source.html).
- Porting guide asset guidance: OTR/O2R export is recommended, and if not used, "You must manually 'massage' the assets to handle endianness (byte order) and bit-width differences between N64 and PC." — [Libultraship Porting Guide](https://libultraship.com/PORTING).

### Inferences
- The ports' strategy is closer to "rewrite the source so console addresses *are* host addresses" than to "map console addresses." The translation arithmetic (`VIRTUAL_TO_PHYSICAL`, `SEGMENTED_TO_K0`, segment tables) is kept for N64 targets and as scaffolding but is reduced to identity on PC.
- Because pointer-bearing data is emitted as C symbol references (geo layouts, DLs) or resolved at init, the host linker assigns the real addresses; hence no runtime mapping table is required for those.
- For LUS, the analogue of "mapping" is the resource/archive layer (O2R/OTR keyed by hash/path), which decouples game data from any console address entirely.

### Gaps
- I did not obtain a single definitive statement that SoH/OOT *compiles* `SEGMENTED_TO_VIRTUAL`-equivalents to identity in `Shipwright` proper (the file I verified is `2ship2harkinian/mm/include/macros.h`). SoH is reportedly a different codebase from 2Ship, so this should be treated as strong-but-adjacent evidence for LUS ports generally, not OOT-specifically.

---

## Is there an equivalent to eidos' per-address helper (`bw_ds_ptr`)? How many call sites, and how are they generated?

### Takeaway
No. Neither sm64-port nor LUS exposes a single per-address translation function analogous to `bw_ds_ptr`. sm64 has, at most, a *segment* resolver (`segmented_to_virtual`) plus a bare pointer cast (`seg_addr`) at DL consumption, and LUS resolves data by resource identity. There is no per-call-site translation helper and no codegen that rewrites console repeated-address casts.

### Cited Findings
- sm64's closest equivalents: `segmented_to_virtual()` / `virtual_to_segmented()` and the `sSegmentTable[32]` (graphics segments only), and the runtime bare cast `seg_addr(uintptr_t w1) { return (void *) w1; }` — [memory.c](https://github.com/sm64-port/sm64-port/blob/master/src/game/memory.c), [gfx_pc.c](https://github.com/sm64-port/sm64-port/blob/master/src/pc/gfx/gfx_pc.c). These act on whole graphics segments, not on arbitrary console addresses, and neither is generated per call site.
- On PC the segment resolver is identity and the DL cast is a no-op, so there is nothing to count: the "translation" function degenerates away (`-DNO_SEGMENTED_MEMORY`) — [sm64-port `Makefile`](https://github.com/sm64-port/sm64-port/blob/master/Makefile), [memory.h](https://github.com/sm64-port/sm64-port/blob/master/src/game/memory.h).
- LUS's address-bearing data (textures, display lists, vertices, matrices) is loaded via dedicated OTR opcodes keyed by hash or file path, not via a pointer-arithmetic helper — [LUS `fast/lus_gbi.h` source](https://libultraship.com/api/lus__gbi_8h_source.html).

### Inferences
- eidos' `bw_ds_ptr` (rewriting provable console-address casts into a translation call) has no architectural counterpart in these ports; the ports instead avoid console addresses in the first place (assets become host data; DL words become host pointers).
- If a per-address helper *were* needed, it would be invisible in these repos because the equivalent console addresses were not preserved.

### Gaps
- I found no documentation, tool, or codegen step in either project that enumerates or rewrites individual console-address cast sites, and therefore no call-site count. I cannot report a number; reporting one would be fabrication.

---

## Pointer width: how did the ports reach 64-bit? Is the claim "sm64 has fully declared structs so the host compiler lays every one out, with no explicit padding" true?

### Takeaway
The claim is **false / incomplete**. sm64 does *not* rely on natural layout with no explicit padding: it has explicit padding fields (e.g. `u8 filler[4]`), address-typed fields declared as integers (`uintptr_t GeoLayout/LevelScript/BehaviorScript`), pointer-width-parameterized conditionals (`#if IS_64_BIT`), and a helper macro specifically to scale hardcoded sizes by pointer width. sm64 reaches 64-bit by *parameterizing layout on pointer width and allowing struct layout to change*, plus keeping the byte-layout-sensitive cases (save data, display-list/Gfx words) specially handled — not by "declaring structs fully."

### Cited Findings
- The decompilation defines pointer width and endianness per host and provides a size-scaling macro: `#ifdef TARGET_N64 / #define IS_64_BIT 0 / #define IS_BIG_ENDIAN 1 / #else ... #define IS_64_BIT (UINTPTR_MAX == 0xFFFFFFFFFFFFFFFFU) / #define IS_BIG_ENDIAN (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__) #endif` and `#define DOUBLE_SIZE_ON_64_BIT(size) ((size) * (sizeof(void *) / 4))` — [n64decomp/sm64 `include/platform_info.h`](https://github.com/n64decomp/sm64/blob/master/include/platform_info.h).
- The same `IS_64_BIT` macro gates struct layout changes. In `struct Object`, address fields are split out of the `u32` raw-data union on 64-bit: the union with `s16 *asS16P[0x50]; … void *asVoidPtr[0x50];` is wrapped in `#if !IS_64_BIT`, and a *separate* `ptrData` union with those pointer arrays is added under `#if IS_64_BIT` — because 0x50 pointers cannot fit in the 0x140-byte (`u32[0x50]`) payload on a 64-bit host — [n64decomp/sm64 `include/types.h`](https://github.com/n64decomp/sm64/blob/master/include/types.h), [sm64-port `include/types.h`](https://github.com/sm64-port/sm64-port/blob/master/include/types.h).
- Address-typed values are declared as integers, not pointers, exactly the pattern eidos worries about: `typedef uintptr_t GeoLayout; typedef uintptr_t LevelScript; … typedef uintptr_t BehaviorScript;` and `uintptr_t bhvStack[8];` — [n64decomp/sm64 `include/types.h`](https://github.com/n64decomp/sm64/blob/master/include/types.h).
- Explicit padding exists in the decomp: `struct MarioBodyState { … u8 filler[4]; };` (renamed to `u8 padding[4];` in sm64-port) — [n64decomp types.h](https://github.com/n64decomp/sm64/blob/master/include/types.h), [sm64-port types.h](https://github.com/sm64-port/sm64-port/blob/master/include/types.h). Other decomp structs use explicit pad arrays too, e.g. `UNUSED u8 pad[8];` in controller/status structs — [SM64-Decomp `src/game/game.c`](https://github.com/justinwash/SM64-Decomp/blob/master/src/game/game.c).
- The N64-layout byte offsets in comments are stale on 64-bit and *are allowed to be*: e.g. `struct GraphNode` is documented at N64 offsets `/*0x00*/ s16 type; /*0x02*/ s16 flags; /*0x04*/ struct GraphNode *prev; … /*0x10*/ struct GraphNode *children;` (N64 size 0x14). On a 64-bit host the three/four pointers become 8 bytes, so the struct is 0x24 and the comments no longer describe host layout — the port accepts this — [sm64-port `include/types.h`](https://github.com/sm64-port/sm64-port/blob/master/include/types.h).
- The equivalent problem in LUS is documented directly: display-list word type is pointer-sized, so DL size doubles. "Where this becomes a problem is with our definition of `Gfx` words being `uintptr_t` instead of `uint32_t`. On 64bit machines, the size of `Gfx` is double compared to 32bit/N64 hardware… `Gfx` has a size of `0x8` on 32bit and a size of `0x10` on 64bit." — [2ship2harkinian issue #137](https://github.com/HarbourMasters/2ship2harkinian/issues/137).
- The LUS porting workflow treats pointer width as expected friction: "64-bit Fixes: Address pointer size differences and other architecture-specific issues." — [Libultraship Porting Guide](https://libultraship.com/PORTING).
- Both 32-bit and 64-bit host builds are officially supported for sm64-port: the README instructs MinGW 64-bit (`mingw-w64-x86_64-gcc`) or 32-bit (`mingw-w64-i686-gcc`) — [gperrio/sm64-port README](https://github.com/gperrio/sm64-port) (fork of the canonical [sm64-port/sm64-port](https://github.com/sm64-port/sm64-port)).

### Inferences
- sm64 did *not* solve 64-bit by preserving N64 byte layout; it solved it by (a) making layout pointer-width-dependent (`IS_64_BIT`, `DOUBLE_SIZE_ON_64_BIT`), (b) keeping address values in `uintptr_t`, and (c) relying on the fact that most of its structs are never serialized to disk or indexed by raw byte offset. The byte-layout-sensitive parts (save data; the `Gfx` DL word) are the ones that caused bugs.
- Therefore the eidos premise — "sm64 reached 64-bit *because* its structs are fully declared, with no explicit padding" — is wrong on both clauses: explicit padding exists, and full declaration is not sufficient (the LUS `Gfx` case shows a fully declared struct still breaks on width). What actually matters is that sm64 does not require exact N64 byte offsets for the affected structs, unlike eidos' `u8 pad[0x14]`-pinned structs.
- The eidos approach (rewrite console-address casts into a call + compile ILP32 to honor explicit padding) is close to the *opposite* of sm64's: sm64 compiles LP64 and lets layout change, and removes console addresses from the data path.

### Gaps
- I did not find an sm64 project document that states *why* 64-bit was chosen over 32-bit; the "because fully declared" rationale appears to be the eidos project's own hypothesis, not an sm64-documented decision.
- I did not measure how many call sites rely on N64 byte offsets (e.g. `object_fields.h` indices) versus named fields; a quantitative count is not available from the sources reviewed.

---

## Did the N64 decomps have the same problems as eidos (addresses typed as integers, hardcoded sizes/offsets, explicit padding, cross-file prototype disagreements)?

### Takeaway
Yes for the first three: addresses typed as integers, hardcoded sizes/offsets, and explicit padding all demonstrably occur in the sm64 decomp and in the LUS port lineage, and the projects added targeted machinery to cope. Cross-file prototype disagreements were not found in any primary source and should be treated as unverified here.

### Cited Findings
- Addresses typed as integers: `typedef uintptr_t GeoLayout / LevelScript / BehaviorScript;` and `uintptr_t bhvStack[8]` — [n64decomp types.h](https://github.com/n64decomp/sm64/blob/master/include/types.h).
- Hardcoded sizes scaled for pointer width: `#define DOUBLE_SIZE_ON_64_BIT(size) ((size) * (sizeof(void *) / 4))` exists precisely because sizes were hardcoded for 32-bit — [platform_info.h](https://github.com/n64decomp/sm64/blob/master/include/platform_info.h). The LUS `Gfx` 0x8→0x10 change is the same class of hardcoded-size problem — [issue #137](https://github.com/HarbourMasters/2ship2harkinian/issues/137).
- Hardcoded offsets / segment constants that had to be recomputed for 64-bit: the LUS port derives DL segment constants from struct layout at compile time rather than using the N64 literals — `#define D_0E000000_TO_SEGMENTED(member) ((0x0E000000 + offsetof(GfxMasterList, member)) | 1)` (plus `D_01000000_TO_SEGMENTED`, `D_0F000000_TO_SEGMENTED`), in a `// #region 2S2H [Port]` block — [2ship2harkinian `mm/include/macros.h`](https://github.com/HarbourMasters/2ship2harkinian/blob/045edd624f93a7d1e793312514d2a4c6a77dc3cb/mm/include/macros.h). The issue describes the underlying N64 offset `0x0C000010` becoming an invalid index because `Gfx` doubled — [issue #137](https://github.com/HarbourMasters/2ship2harkinian/issues/137).
- Cross-file layout assumptions also break at runtime: a port comment notes that statically linked global animations "can end up with different pointer addresses if an animation gets set from one file and compared against the static animation in another," requiring a string-compare workaround (`BEN_ANIM_EQUAL`) — [2ship2harkinian `mm/include/macros.h`](https://github.com/HarbourMasters/2ship2harkinian/blob/045edd624f93a7d1e793312514d2a4c6a77dc3cb/mm/include/macros.h).
- Explicit padding: `u8 filler[4]` / `u8 padding[4]` — [n64decomp types.h](https://github.com/n64decomp/sm64/blob/master/include/types.h); `UNUSED u8 pad[8];` — [SM64-Decomp game.c](https://github.com/justinwash/SM64-Decomp/blob/master/src/game/game.c).
- The decomp explicitly supports "alternate, more portable code" via `NON_MATCHING=1`/`AVOID_UB=1`, and the port forces `NON_MATCHING := 1` — an acknowledgement that the matching (byte-exact) build and the portable build differ — [sm64-port `Makefile`](https://github.com/sm64-port/sm64-port/blob/master/Makefile), [sm64-port `include/macros.h`](https://github.com/sm64-port/sm64-port/blob/master/include/macros.h).

### Inferences
- The problems eidos describes are not unique to eidos; they are intrinsic to porting these decomps and recur across titles and pointer widths. The N64 decomps solved them piecemeal (per-struct `IS_64_BIT` branches, `DOUBLE_SIZE_ON_64_BIT`, `offsetof`-derived constants, string-compare fallbacks), not by a uniform rule.

### Gaps
- **Cross-file prototype disagreements:** I found no primary-source documentation of this issue in sm64-port/sm64ex, LUS, or 2ship. I cannot confirm or deny it for these projects; treat as unverified.
- I could not find a decomp document that enumerates all structs needing `IS_64_BIT` handling; the count of affected structs is unknown.

---

## How do the ports handle the N64's BIG-ENDIAN data on a little-endian host?

### Takeaway
Two mechanisms, split by data type. (1) Graphics/texture pixel data is byte-swapped by hand in the renderer, reading bytes most-significant-first per pixel format. (2) In the LUS ecosystem, bulk assets are converted to native host byte order by the asset-export step (OTR/O2R), and the runtime is expected not to byte-swap game data. Save files are the exception and end up little-endian (see next section).

### Cited Findings
- sm64's renderer reads 16-bit BE texels manually: `uint16_t col16 = (rdp.loaded_texture[tile].addr[2*i] << 8) | rdp.loaded_texture[tile].addr[2*i+1];` with the comment `// Big endian load` (in `import_texture_rgba16`, `import_texture_ci4`, `import_texture_ci8`) — [sm64-port `src/pc/gfx/gfx_pc.c`](https://github.com/sm64-port/sm64-port/blob/master/src/pc/gfx/gfx_pc.c).
- The decomp has an `IS_BIG_ENDIAN` define (`__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__` on host; forced `1` on N64) — [platform_info.h](https://github.com/n64decomp/sm64/blob/master/include/platform_info.h).
- LUS asset guidance: exporting to OTR/O2R is recommended; the alternative is to "manually 'massage' the assets to handle endianness (byte order) and bit-width differences between N64 and PC" — [Libultraship Porting Guide](https://libultraship.com/PORTING).
- In a LUS-based port's reference doc: "N64 MIPS R4300i is big-endian. All multi-byte values in ROM and RDRAM are big-endian… libultraship handles any necessary byte swapping transparently through the resource system. Data loaded from .o2r archives is already in native host byte order. Do not add manual byte-swap code in game logic. If you encounter endianness issues, it means the asset extraction or resource loading layer needs fixing." — [BattleShip `docs/n64_reference.md`](https://github.com/JRickey/BattleShip/blob/main/docs/n64_reference.md) (third-party SSB64 LUS port; corroborating, not first-party LUS docs).
- ROM byte-order background (native `.z64` is big-endian `80 37 12 40`; PC is little-endian) — [N64 ROM format reference](http://n64dev.org/romformats.html).

### Inferences
- What breaks on a little-endian host: multi-byte texture/tile data interpreted as native integers, and any serialized structs. Textures are handled per-format in the renderer; struct-like bulk data is handled at export time.
- The LUS philosophy is "fix endianness in the exporter, not the game" — the opposite of sprinkling swaps through game logic.

### Gaps
- I did not find first-party LUS source proving the exporter always writes native-endian OTR/O2R data for OOT specifically (the claim is from LUS porting guidance + a third-party port doc).

---

## Do the ports preserve save-file byte compatibility with the original console?

### Takeaway
For sm64: **no** — port saves are little-endian and a dedicated third-party converter exists to move between the console's big-endian EEPROM format and the ports' little-endian format. The port writes the in-memory save buffer raw to disk. For SoH/OOT: not established by the sources reviewed.

### Cited Findings
- The sm64 PC port writes the in-memory EEPROM buffer verbatim: `osEepromLongWrite` memcpy's the game's buffer and `fwrite`s it to `sm64_save_file.bin`; `osEepromLongRead` `fread`s it back and memcpy's into the caller's buffer — [ultra_reimplementation.c](https://github.com/sm64-port/sm64-port/blob/master/src/pc/ultra_reimplementation.c). No byte-swap occurs, so the on-disk bytes follow host endianness (little-endian on x86/x64).
- A dedicated converter confirms the divergence: "This program is designed to convert Super Mario 64 EEPROM saves between the original N64 Big Endian format and the Little Endian format used by many ports for Little Endian CPU architectures based on the SM64 Decompilation project." — [DarkMatterCore/sm64dcsc README](https://github.com/DarkMatterCore/sm64dcsc/blob/master/README.md).
- The decomp handles endianness as a compile-time property but does not force the save struct to console byte order — [platform_info.h](https://github.com/n64decomp/sm64/blob/master/include/platform_info.h); the save path above performs no swap.
- sm64 uses a fixed 512-byte EEPROM save buffer addressed in 8-byte units (`content[512]`, `address * 8`) — [ultra_reimplementation.c](https://github.com/sm64-port/sm64-port/blob/master/src/pc/ultra_reimplementation.c); background on SM64's 512-byte EEPROM — [hack64 SM64 EEPROM](https://hack64.net/Thread-SM64-EEPROM).

### Inferences
- Because sm64's save data is (a) fixed-size integers and (b) written raw, its *size/layout* survives 64-bit compilation even though its *byte order* does not match the console. So struct-layout choices do not break save compatibility (no pointers are persisted), but endianness does — hence the converter.
- This is the one place where exact byte layout matters, and it matters in endianness, not in pointer width, for sm64.
- For a port that compiled ILP32 with explicit padding like eidos, save compatibility would depend on whether persistable data uses pointers/padded structs; sm64's save data deliberately does not, which is why it tolerates LP64.

### Gaps
- **SoH/OOT save compatibility:** I did not find a primary source establishing whether Ship of Harkinian preserves console (big-endian, SRAM/flash) save byte layout or uses a port-specific format. Treat as unverified.
- I did not verify whether sm64 saves include a checksum field whose endianness affects validation; not confirmed from the sources reviewed.

---

## Cross-cutting synthesis for the eidos decision

- **Same problem class, different remedy.** sm64 and LUS hit the same issues eidos names (integer-typed addresses, hardcoded sizes/offsets, explicit padding, pointer-width-dependent layout), but they did not solve them by per-address translation helpers or by pinning 32-bit layout. They (i) removed console addresses from the data path (identity `osVirtualToPhysical`, `-DNO_SEGMENTED_MEMORY`, OTR resource identity), (ii) parameterized layout on pointer width (`IS_64_BIT`, `DOUBLE_SIZE_ON_64_BIT`, `#if IS_64_BIT` struct variants, `offsetof`-derived constants), and (iii) accepted that non-serialized structs change layout on 64-bit.
- **The eidos hypothesis is not supported.** "sm64 chose 64-bit because its structs are fully declared, with no explicit padding" is contradicted by `u8 filler[4]`/`u8 pad[8]`, by `uintptr_t` address fields, and by the explicit `IS_64_BIT` layout branches. Full declaration is not what enabled 64-bit; the absence of a requirement for exact byte offsets (except in saves and DL words) did.
- **The decisive question for eidos is not "32 vs 64 bit" per se** but "which of my structs must have exact byte layout, and why." sm64's answer: only persisted save data and the DL/`Gfx` word; everything else may re-lay-out. Where eidos pins layout with `u8 pad[0x14]`, that pinning is a *choice* that forces ILP32, not a necessity demonstrated by sm64.

## Overall gaps
- No sm64- or LUS-authored document stating the 64-bit-vs-32-bit decision rationale was found; the "fully declared structs" rationale appears to originate from the eidos project itself.
- No per-address translation helper or call-site count exists in these projects (the mechanism is architecturally absent).
- Cross-file prototype disagreements and SoH-specific save-file compatibility were not verifiable from primary sources within this research.
