# Static Recompilation (N64Recomp) and libultraship as a Reusable Platform Layer

Scope note: this covers N64Recomp and the recompiled N64 ports, libultraship (LUS) as a decompilation-port platform layer, static recompilation on other consoles (Xbox 360, PS1, SNES, GBA, DS), and where each technique sits on the emulation-to-native spectrum. Primary sources (project READMEs, author statements, docs) are preferred; AI-generated wiki sources and aggregators are flagged as such. Research date: 2026-10-15.

---

## Q1. What is N64Recomp and how does it work? Is anything interpreted at runtime? Is there a JIT?

### Takeaway
N64Recomp is an ahead-of-time static recompiler that translates an N64 ROM's MIPS machine code, instruction by instruction, into literal C source; the generated C is compiled natively and linked against a separate runtime (N64ModernRuntime). All CPU translation happens at build time — there is no instruction interpreter and no JIT for the game's code.

### Cited Findings
- N64Recomp is "a tool to statically recompile N64 binaries into C code that can be compiled for any platform… for simulating behaviors significantly faster than interpreters or dynamic recompilation can." [N64Recomp README](https://github.com/N64Recomp/N64Recomp)
- Mechanism: the tool takes the binary plus a symbol/metadata list, splits it into functions, and emits one C function per input function, named from the metadata; "Instructions are processed one-by-one and corresponding C code is emitted as each one gets processed. This translation is very literal in order to keep complexity low." Example: `addiu $r4, $r4, 0x20` becomes `ctx->r4 = ADD32(ctx->r4, 0X20);` [N64Recomp README](https://github.com/N64Recomp/N64Recomp)
- Control flow: `jal` (jump-and-link) becomes a direct function call; tail-call-optimizable `j`/`b` become calls; "Branch delay slots are handled by duplicating instructions as necessary"; a `jr` used as a jump table is turned into a switch-case. [N64Recomp README](https://github.com/N64Recomp/N64Recomp)
- Indirect calls cannot be resolved statically, so they become runtime **lookups**, not interpretation: `jalr $25` is emitted as `LOOKUP_FUNC(ctx->r25)(rdram, ctx);` and the runtime "maintain[s] a list of which program sections are loaded and at what address" to pick the function. [N64Recomp README](https://github.com/N64Recomp/N64Recomp)
- Overlays: both statically linked and relocatable overlays are handled; for relocatable overlays the tool emits relocation macros so the runtime can patch immediate fields at load time, e.g. `ctx->r24 = S32(RELOC_HI16(1754, 0X630) << 16);` where the runtime implements `RELOC_HI16`/`RELOC_LO16`. [N64Recomp README](https://github.com/N64Recomp/N64Recomp)
- Configuration is a TOML file passed as the first argument; metadata currently can only be supplied via an ELF file (easiest to obtain from a disassembly/decompilation). Libraries used: rabbitizer (instruction decode/analysis), ELFIO, toml11, fmtlib. [N64Recomp README](https://github.com/N64Recomp/N64Recomp)
- Author (Mr-Wiseguy) on static vs dynamic recompilation: "static recompilation is done ahead of time. And dynamic recompilation is done when the game code is being run in the emulator." Because output is C, he "get[s] the full suite of optimizations that a modern optimizing compiler like Clang would do"; a game frame of logic takes "less than a fraction of a millisecond." [Solid State / Software Engineering Daily transcript, SED1735, Sept 2024](https://softwareengineeringdaily.com/wp-content/uploads/2024/09/SED1735-N64-Recomp.txt)
- Ars Technica: the tool "takes a raw game binary … and reprocesses every single instruction directly and literally into corresponding C code"; the generated code is "not very readable unless you know the assembly language," and "the recompiler itself has absolutely nothing in it from any games and is mainly just information about the MIPS instruction set." (Published 2024-05-17.) [Ars Technica](https://arstechnica.com/gaming/2024/05/how-to-port-any-n64-game-to-the-pc-in-record-time/)
- Public rollout: Zelda 64: Recompiled was released May 2024 (news coverage 2024-05-11) as the first showcase; Digital Foundry covered it the same month. [GBAtemp 2024-05-11](https://gbatemp.net/threads/new-static-recompiler-tool-n64recomp-aims-to-seamlessly-modernize-n64-games.655670/); [Nintendo Life 2024-05-26](https://www.nintendolife.com/news/2024/05/video-digital-foundry-takes-a-look-at-zelda-64-recompiled-majoras-mask)
- Prior art cited by the author: jamulator (NES) and ido-static-recomp (which recompiles the SGI IDO compiler, not a game). [N64Recomp README](https://github.com/N64Recomp/N64Recomp)
- RSP microcode is also statically recompiled by the companion tool RSPRecomp (used in Zelda64Recomp for audio `aspMain` and JPEG `njpgdspMain`). [Zelda64Recomp BUILDING.md](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/dev/BUILDING.md)

### Inferences
- N64Recomp is purely ahead-of-time for CPU code: the README explicitly positions it against "interpreters or dynamic recompilation," and the only runtime resolution is a function-pointer/section lookup table for indirect jumps, which is dispatch, not decoding/executing machine code.
- The generated C preserves MIPS-level semantics (registers, 32-bit wrap/ sign-extension via macros) rather than recovering high-level structure.

### Gaps
- I did not locate a first-party blog post by Mr-Wiseguy beyond the README, the SED podcast, and the GitHub issue #26 answer; the "announcement" appears to have been the May 2024 release plus press coverage, not a standalone technical post.

---

## Q2. How do the recompiled ports handle console hardware, address space, memory, and pointers? Is console RAM a big host allocation?

### Takeaway
The recompiled ports do **not** emulate the console's memory map or hardware registers for the CPU. N64 RAM (RDRAM) is modeled as a contiguous host allocation addressed by a base pointer passed into recompiled functions, and the game's calls into the N64 system library (libultra) are replaced by modern reimplementations rather than emulated.

### Cited Findings
- Author (Mr-Wiseguy), on why address-space emulation is avoided: N64 games "were also built with a system library which is used by game code to control the hardware… game code generally tends to not interact directly with memory-mapped registers since the system library handled that." Therefore "you can avoid implementing costly address space lookup at runtime, and you can instead just convert load/store operations into normal memory accesses via simple pointer arithmetic. This makes performance significantly better than even the best dynamic recompilation implementations, since those still have to deal with the original hardware's memory mappings." [Mr-Wiseguy, N64Recomp issue #26](https://github.com/Mr-Wiseguy/N64Recomp/issues/26)
- Recompiled functions receive a `rdram` pointer plus a `recomp_context` CPU-state struct, e.g. `LOOKUP_FUNC(ctx->r25)(rdram, ctx);`. [N64Recomp README](https://github.com/N64Recomp/N64Recomp)
- Runtime split: `ultramodern` "is a reimplementation of much of the core functionality of libultra" — Threads, Controllers, Audio, Message Queues, Timers, RSP Task Handling, VI timing — with platform I/O via callbacks; `librecomp` "provides wrappers to allow recompiled code to call ultramodern" plus Overlay handling, PI DMA (ROM reads), and EEPROM/SRAM/Flashram saving. [N64ModernRuntime README](https://github.com/N64Recomp/N64ModernRuntime)
- Zelda64Recomp architecture (secondary/AI-generated source): "ultramodern … Implements N64 OS APIs (`osRecvMesg`, `osCreateMesgQueue`, etc.) using modern primitives"; recompiled game code "executes within the `ultramodern` runtime environment"; the app "maintains two distinct memory spaces that are bridged through the `recomp_context` and RDRAM pointer"; special functions are stubbed via `patches/syms.ld` dummy addresses starting at `0x8F000000`. [DeepWiki: Zelda64Recomp Core Architecture](https://deepwiki.com/Zelda64Recomp/Zelda64Recomp/3-core-architecture) *(AI-generated summary of the repo, treat as secondary)*
- RDRAM model (secondary/AI-generated source on a fork): "The runtime simulates the N64's RDRAM by allocating a contiguous block of memory that the recompiled code treats as its physical address space… pointers relative to the base of this allocated RDRAM block to maintain compatibility with original MIPS memory offsets," with `rdram_size` typically 4MB or 8MB. [DeepWiki: drmario64_recomp_plus N64ModernRuntime](https://deepwiki.com/theboy181/drmario64_recomp_plus/9.2-n64modernruntime-(ultramodern-and-librecomp)) *(AI-generated, secondary)*
- Graphics are not run through the N64 RDP/RSP hardware at runtime; the port uses RT64 as the renderer, which converts N64 graphical microcode/display lists into modern API calls (DirectX 12 / Vulkan). [Zelda64Recomp README](https://github.com/Zelda64Recomp/Zelda64Recomp); [Ars Technica](https://arstechnica.com/gaming/2024/05/how-to-port-any-n64-game-to-the-pc-in-record-time/)
- The ROM is still read at runtime for assets via PI DMA ("ROM reads") handled by `librecomp`. [N64ModernRuntime README](https://github.com/N64Recomp/N64ModernRuntime)

### Inferences
- The console RAM is essentially a single flat host buffer used with pointer arithmetic; there is no TLB/memory-mapped-register lookup per access. Hardware device access is factored out into the reimplemented libultra layer, so "hardware" is API-level, not register-level, emulation for the CPU.
- This is why the author claims performance better than even accurate dynamic recompilers: no per-access address translation.

### Gaps
- I could not find a first-party (author-written) document specifying the exact host allocation strategy (size, alignment, pointer-width handling); the flat-RDRAM claim rests on the author's "simple pointer arithmetic" statement plus AI-generated wikis. Exact semantics of TLB-mapped code are noted by the README as future work ("Support for relocations for TLB mapping is coming in the future").

---

## Q3. Do the ports run original machine-code semantics (registers, delay slots) instead of decompiled C? Consequences for correctness and moddability?

### Takeaway
Yes — the recompiled output reproduces MIPS instruction semantics one-to-one (registers in a context struct, delay slots duplicated, explicit 32-bit arithmetic), not reconstructed high-level C. Correctness tracks the binary's actual behavior; moddability comes from *replacing whole functions* (recompiling patched C/asm) rather than editing readable game logic.

### Cited Findings
- Registers live in a context struct and arithmetic goes through runtime-defined macros (e.g. `ADD32`), preserving MIPS 32-bit sign-extension/wrap behavior. [N64Recomp README](https://github.com/N64Recomp/N64Recomp)
- Delay slots: "Branch delay slots are handled by duplicating instructions as necessary." [N64Recomp README](https://github.com/N64Recomp/N64Recomp)
- Author: N64Recomp "doesn't try to match the original binary output when run through a compiler… instead, it's matching the behavior one-to-one." [SED1735 transcript](https://softwareengineeringdaily.com/wp-content/uploads/2024/09/SED1735-N64-Recomp.txt)
- Consequence for readability: generated code is "not very readable unless you know the assembly language used on the N64's CPU," and there is "basically no reason to read the recompiler output code besides debugging." [Ars Technica](https://arstechnica.com/gaming/2024/05/how-to-port-any-n64-game-to-the-pc-in-record-time/)
- Moddability mechanism: "single file output mode" lets you recompile only patched functions; because ordinary linkers pull a symbol only if not already found, "providing the recompiled patches to the linker before providing the original recompiler output will result in the patches taking priority." [N64Recomp README](https://github.com/N64Recomp/N64Recomp)
- Zelda64Recomp implements enhancements by replacing functions with patched versions marked `RECOMP_PATCH` (built into `PatchesLib`, generated as `RecompiledPatches/patches.c`); the symbol file maps original N64 addresses to recompiled implementations. [DeepWiki: Zelda64Recomp Recompilation System](https://deepwiki.com/Zelda64Recomp/Zelda64Recomp/3.1-recompilation-system) *(AI-generated, secondary)*; [DeepWiki: Build System](https://deepwiki.com/Zelda64Recomp/Zelda64Recomp/2-build-system)
- Author: "Most of the enhancements in Zelda64Recomp are provided through this approach, such as the gyro aim and high framerate fixups for cases that RT64's automatic detection messed up"; editing the ROM directly to achieve the same is much harder. [Mr-Wiseguy, issue #26](https://github.com/Mr-Wiseguy/N64Recomp/issues/26)
- The recompiler still depends on decompilation for symbols/headers: Zelda64Recomp credits the "Majora's Mask Decompilation for headers and some function definitions, used for making patches or some enhancements." [Zelda64Recomp README](https://github.com/Zelda64Recomp/Zelda64Recomp)
- Self-modifying code handling: overlays are handled by hooking load points and maintaining a function-address table for indirect jumps; relocatable overlays get extra emitted relocation code; the author says only two cases of self-modifying code were not handled by these techniques, and both were worked around with code replacement. [Mr-Wiseguy, issue #26](https://github.com/Mr-Wiseguy/N64Recomp/issues/26)

### Inferences
- Correctness is "behavioral fidelity to the shipped binary" including compiler artifacts, rather than "matches original source." It is neither emulation (no interpreter) nor decompilation (no recovered structure).
- Modding is powerful but coarse: you replace whole functions; you still must reverse-engineer which function to touch, so an existing decompilation greatly lowers effort.

### Gaps
- No quantitative accuracy/regression study of recompiled ports vs emulators was found; correctness claims are qualitative from the author.

---

## Q4. Documented tradeoffs between static recompilation and decompilation-based ports (author comparisons)?

### Takeaway
Sources explicitly frame them as complementary and at opposite cost points: decompilation yields human-readable, editable C but costs years of reverse-engineering; static recompilation yields literal, unreadable C that is fast to produce but needs decompilation/disassembly for symbols, and it trades readability for automatic coverage.

### Cited Findings
- Author (Mr-Wiseguy): "decompilation and static recompilation go hand-in-hand… I can get the 95% of the game that I don't really care about the details of to run without ever touching those functions. But then that last 5%… I can just focus on those for decompilation." He notes the recompiler "doesn't need to know what [the code] is doing" but recovers "no high-level information." [SED1735 transcript](https://softwareengineeringdaily.com/wp-content/uploads/2024/09/SED1735-N64-Recomp.txt)
- Author's summary: with a manual decompilation "every single portion of the game logic has to be reverse-engineered before you can even build the port"; with recompilation "you only need to reverse-engineer just enough of the game to figure out how to make the mods you want," yielding "fewer opportunities to accidentally introduce bugs." [Ars Technica](https://arstechnica.com/gaming/2024/05/how-to-port-any-n64-game-to-the-pc-in-record-time/)
- Independent summary: "Decompilation produces the metadata; N64Recomp consumes it." [Hysen Labs: N64Recomp](https://hysenlabs.com/projects/n64recomp-n64recomp)
- Digital Foundry framing (via Nintendo Life): it is "not emulating the behaviour of the hardware… nor is it going as far as decompilation in attempting to recreate the original source code." [Nintendo Life, 2024-05-26](https://www.nintendolife.com/news/2024/05/video-digital-foundry-takes-a-look-at-zelda-64-recompiled-majoras-mask)
- libultraship (the decompilation-port platform) states it "is not a recompilation tool"; it "allows developers to build a game's original source code (derived from decompilation) on modern hardware." [libultraship Porting Guide](https://libultraship.com/PORTING.html)
- Author's own TL;DR on feasibility: "Targeting 5th gen (or later) game consoles and requiring the user to provide some info about the layout of code in the ROM makes a lot of the problems normally associated with static recompilation go away… being able to easily replace the system library with modern versions allows games to run without needing to replicate hardware behavior." [Mr-Wiseguy, issue #26](https://github.com/Mr-Wiseguy/N64Recomp/issues/26)

### Inferences
- The two techniques sit at different points: decompilation is high-effort/high-readability/source-true; static recompilation is low-effort/low-readability/binary-true. Most N64 recomp projects still use the game's decompilation as an input, so the techniques are not competitors in practice.

### Gaps
- No peer-reviewed or quantitative "recompilation vs decompilation" survey specific to console ports was found (the search returned general academic decompiler studies, not this comparison). Comparisons are primarily author/press statements.

---

## Q5. What is libultraship, what does it provide, is it reusable, which games use it, and where is the library/game boundary?

### Takeaway
libultraship (LUS) is a reusable MIT-licensed PC platform layer that reimplements the N64 `libultra` SDK and modern graphics/audio/input/resource subsystems, built to compile an **existing game decompilation** natively. It is explicitly not a recompiler; the boundary is: LUS supplies the N64-shaped runtime + renderer + platform services, while the port supplies the decompiled game code.

### Cited Findings
- "libultraship (LUS) is a library meant to provide reimplementations of libultra (n64 sdk) functions that run on modern hardware." [libultraship README](https://github.com/Kenix3/libultraship)
- "It is not a recompilation tool; rather, it allows developers to build a game's original source code (derived from decompilation) on modern hardware by providing a header-compatible wrapper around modern reimplementations of N64 SDK functions. In essence, LUS acts as 'compile-time WINE' for the N64." [libultraship Porting Guide](https://libultraship.com/PORTING.html)
- Reuse requirement: prerequisite is "A Game Decompilation"; the recommended template is the Ghostship project; porting involves replacing `#include <ultra64.h>` with `<libultraship/libultraship.h>` and stubbing/handling N64-specific calls. [libultraship Porting Guide](https://libultraship.com/PORTING.html)
- Architecture (three subsystems): `src/libultraship` = N64 compatibility/bridge layer (reimplemented libultra + bridges to modern backends); `src/ship` = platform-agnostic core (resource/archive management, audio backends SDL/CoreAudio/WASAPI, controllers, config, scripting, events); `src/fast` = the Fast3D display-list renderer (OpenGL, Direct3D 11, Metal). [libultraship Porting Guide](https://libultraship.com/PORTING.html); [DeepWiki: libultraship-switch architecture](https://deepwiki.com/timschneeb/libultraship-switch) *(AI-generated, secondary)*
- Public API surface (Ship namespace): `Context` (root singleton owning components), `Resource` (asset loading/archives/caching), `Window`/`Gui`, `Audio`, `Controller`, `Console`, `Events`, `Scripting`. Platform-specific code is behind compile-time guards (`ENABLE_DX11`, `ENABLE_OPENGL`). [libultraship API: namespaceShip](https://libultraship.com/api/namespaceShip.html)
- Asset system: data is stored separately from the executable in `.otr` (MPQ-compatible) or `.o2r` (ZIP-compatible) archives; "All one needs to do is supply a patch `.otr` or `.o2r` and the system will automatically replace the data," which is designed for mod-friendliness. [libultraship README](https://github.com/Kenix3/libultraship)
- Platform/backend support: Windows (OpenGL, Direct3D 11), macOS (OpenGL, Metal), Linux (OpenGL), iOS, Android (OpenGL ES), OpenBSD. [libultraship Porting Guide](https://libultraship.com/PORTING.html)
- Games/ports using LUS (all decompilation-based, not recompiled): Ship of Harkinian (OoT) — "built atop a custom library dubbed libultraship"; depends on "the source code provided by the OOT decompilation project." [Shipwright README](https://github.com/harbourmasters/shipwright). 2 Ship 2 Harkinian (Majora's Mask) — uses the libultraship submodule. [2ship2harkinian](https://github.com/2ship2harkinian/2ship2harkinian). Starship (Star Fox 64) — engine listed as libultraship, cites SF64 decompilation. [Starship](https://github.com/harbourmasters/starship); [PCGamingWiki](https://www.pcgamingwiki.com/wiki/Star_Fox_64_(Starship)). BattleShip (Super Smash Bros. 64) — built on the ssb-decomp-re decompilation with libultraship for rendering/audio/input. [BattleShip](https://github.com/JRickey/BattleShip). SpaghettiKart (Mario Kart 64) is cited as using the same asset pipeline. [BattleShip README](https://github.com/JRickey/BattleShip)
- Boundary described explicitly by a port (BattleShip), three deliberately separate layers: "decompiled game code (decomp/src/) — Unmodified C produced by the decomp project. Talks to the N64 the same way the original ROM did: GBI display lists, ALSeqPlayer audio, OS threads, OSContPad input." / "port layer (port/) — Modern C++ glue… owns Ship::Context and the resource factories." / "libultraship (libultraship/) — PC-native runtime: Fast3D renderer (OpenGL / Metal / D3D)." [BattleShip README](https://github.com/JRickey/BattleShip)

### Inferences
- LUS is reusable across games (multiple ports share it as a submodule), but reusability is at the platform/runtime level; each game still needs its own decompilation and game-specific glue/asset exporters. The "library vs game" line is the port layer that maps N64 API calls to LUS bridges.
- Because LUS compiles original decompiled C, it is a **source port**, further from emulation than static recompilation: no binary translation and no interpreter at all.

### Gaps
- I did not find an authoritative, dated upstream list of all LUS-based ports; the set above is assembled from individual project READMEs and one port's statement that the pipeline is shared. Exact current maintainer arrangement (HarbourMasters vs Kenix3) is ambiguous: the canonical README lives at Kenix3/libultraship, while ports reference a `libultraship` submodule.

---

## Q6. Static recompilation projects on other consoles, and specifically the Nintendo DS?

### Takeaway
Static recompilation is now a broad ecosystem (NES, SNES, N64, PS1, GBA, Xbox 360, DS, etc.). For the Nintendo DS there ARE static-recompiler projects (ndsrecomp and its variants, targeting ARM9/ARM7), but they are early/alpha and currently fall back to a bounded interpreter tier for code copied into RAM. Nothing I found indicates a mature, public recompiled Pokémon DS release; the Pokémon DS work I found is decompilation (plus one reference to a "pokeblack-recomp" repo that could not be verified).

### Cited Findings
- NES: jamulator is cited as the well-known early example. [N64Recomp README](https://github.com/N64Recomp/N64Recomp)
- Xbox 360: ReXGlue SDK "converts Xbox 360 PowerPC code into portable C++… Rather than interpreting or JIT-compiling PPC instructions at runtime, ReXGlue takes a different path: it generates C++ source code ahead of time," and its codegen is "based on" XenonRecomp and rexdex's recompiler; it is "heavily rooted on the foundations of Xenia." [rexglue-sdk](https://github.com/rexglue/rexglue-sdk). rexdex/recompiler (Xbox360 → Windows, 2017) implemented "96% of PowerPC CPU instructions." [rexdex/recompiler](https://github.com/rexdex/recompiler). Example port: Kameo: RePowered "converts the Xbox 360 PowerPC default.xex into native x86_64 code at build time." [KameoRePowered](https://github.com/birabittoh/KameoRePowered). Another port states "There is no JIT and no instruction interpreter at runtime." [NFSMW-RECOMP README](https://raw.githubusercontent.com/WINDROID-EMU/NFSMW-RECOMP/main/README.md)
- PS1: PSXRecomp "turns a PS1 disc into a native executable — MIPS R3000A translated to C, compiled to x64… Not an emulator: the game becomes a program your CPU runs directly," but it uses "a general-purpose interpreter — as a transient safety net" and a tcc backend to recompile overlays on first load. [PSXRecomp README](https://github.com/mstan/psxrecomp)
- GBA: GBARecomp includes a "Fallback interpreter (emulation)" for missing static coverage and "an embedded jit compiler… to create a shard on disk of native code" for interpreted paths. [1379.tech GBARecomp](https://1379.tech/expanding-the-recomp-ecosystem-with-gbarecomp)
- Ecosystem survey: "Recompiled games: 145 native PC recomps by console" (now listing 174 games) enumerates Xbox 360 (53), N64 (40), PS1 (23), PS2 (11), SNES (9), GBA (8), PSP (5), and others including "Nintendo DS (1)" = Metroid Prime Hunters. [FanGamesDB recomps survey](https://fangamesdb.com/recomps.html) *(aggregator, unverified counts)*
- Xbox 360 note on a SNES library: snesrecomp packages a real emulator hardware backend (LakeSnes) as a linkable library "the same approach used by N64Recomp (which uses parallel-rdp for graphics)… Chop up an emulator, turn the hardware into libraries." [snesrecomp README](https://github.com/sp00nznet/snesrecomp)

**Nintendo DS specifically:**
- mstan/ndsrecomp: "Very early pre-alpha Nintendo DS static recompiler research… lifts guest ARM code to C ahead of time and runs that code natively rather than interpreting the immutable banks." Decodes ARM7TDMI (ARMv4T) and ARM946E-S (ARMv5TE). Critically: "Code copied into RAM by the guest currently uses a bounded interpreter tier." Example title: Metroid Prime Hunters (gameplay described as early footage, not a compatibility claim). [mstan/ndsrecomp](https://github.com/mstan/ndsrecomp); [RetroPortingToolKit/ndsrecomp](https://github.com/RetroPortingToolKit/ndsrecomp)
- A related ndsrecomp README (mirror/alt): "converts a legally dumped Nintendo DS ROM into a standalone C/CMake static-recompilation project" with a "small DS runtime" and "software video renderer"; "It is therefore the original ROM addresses preserved as the runtime's address space"; "If code changes after translation, or execution reaches an address outside the static set, the runtime reads the live opcode and uses its generic ARM/Thumb helper" (i.e., an interpreter fallback). [Myth7x/ndsrecomp](https://github.com/Myth7x/ndsrecomp) *(mirror, verify provenance)*
- The *recomp ecosystem author (1379.tech, 2026-08-03) on DS: "The DS, being similar to the GBA… has the same upsides when it comes to recompilation." Demonstrations: "Super Mario 64 DS with adaptive widescreen WIP" and "Metroid Prime Hunters with Adaptive Widescreen WIP"; "Neither game is publicly released yet." [1379.tech ecosystem update](https://1379.tech/building-enhancing-recomps-ecosystem-updates/)
- Pokémon DS **decompilation** projects (source-level, not recompilation): pret/pokediamond (Diamond/Pearl, created 2020-04-09) [pret/pokediamond](https://github.com/pret/pokediamond); pret/pokeplatinum (Platinum) [pret/pokeplatinum](https://github.com/pret/pokeplatinum); pokemodding/pokeblack (English Black, ROM sha1 26ad0b99…) [pokemodding/pokeblack](https://github.com/pokemodding/pokeblack); Goldoire/pokeblack (matching decompilation of Black, gamecode IRBO) [Goldoire/pokeblack](https://github.com/Goldoire/pokeblack); ds-decomp toolkit (`dsd`) for DS decomp project setup/delinking [AetiasHax/ds-decomp](https://github.com/AetiasHax/ds-decomp). A `pokeblack2` decomp (Black 2/White 2) exists per a mirror. [FieryMewtwo/pokeblack2](https://github.com/FieryMewtwo/pokeblack2)
- A search surfaced a repository described as "Decompilation and static recompilation of English Pokémon Black" at `wize-logic/pokeblack-recomp`, but the GitHub page and raw README returned 404 when fetched. Treat as unverified/likely renamed. [search result reference](https://github.com/wize-logic/pokeblack-recomp) *(could not verify)*

### Inferences
- The DS static-recompiler efforts are real but immature and mostly use an interpreter fallback for RAM-copied/overlay code — meaning they are currently hybrids, not purely static.
- For Pokémon DS, the mature ecosystem is decompilation (pret-family + independent Black/Black2 efforts), which is what a decompilation-based native port such as eidos would build on.

### Gaps
- No public, released, purely-static-recompiled Pokémon DS port was found. I could not verify any dictionary of DS static-recompilation projects beyond ndsrecomp and its variants (several URLs appear to be mirrors; provenance/authoritative canonical repo unclear).
- The FanGamesDB count of "Nintendo DS (1)" (Metroid Prime Hunters) is an aggregator figure and not independently confirmed.

---

## Q7. Where does each technique sit relative to emulation — which interpret machine code at runtime and which do not?

### Takeaway
There is a spectrum. Emulation (with or without a runtime JIT) executes/decodes console machine code at runtime. Static recompilation translates machine code to C/C++ ahead of time and runs native code — no interpreter/JIT for translated code (some ecosystems add transient interpreter/JIT fallbacks). Decompilation-based ports (e.g. libultraship ports) run hand-reconstructed source and never execute the console's machine code at runtime — the furthest from emulation.

### Cited Findings
- Emulator with dynamic recompilation: the author distinguishes "dynamic recompilation, which is what emulators do… done when the game code is being run in the emulator." [SED1735 transcript](https://softwareengineeringdaily.com/wp-content/uploads/2024/09/SED1735-N64-Recomp.txt)
- N64Recomp: "statically recompile N64 binaries into C… simulating behaviors significantly faster than interpreters or dynamic recompilation can." No interpreter/JIT in the tool itself. [N64Recomp README](https://github.com/N64Recomp/N64Recomp)
- Xbox 360: "Rather than interpreting or JIT-compiling PPC instructions at runtime, ReXGlue… generates C++ source code ahead of time." [rexglue-sdk](https://github.com/rexglue/rexglue-sdk); a ReXGlue-based port: "There is no JIT and no instruction interpreter at runtime." [NFSMW-RECOMP](https://raw.githubusercontent.com/WINDROID-EMU/NFSMW-RECOMP/main/README.md)
- PS1: PSXRecomp "Not an emulator: the game becomes a program your CPU runs directly," but retains "a general-purpose interpreter — as a transient safety net," with the goal state "100% native execution." [PSXRecomp README](https://github.com/mstan/psxrecomp)
- GBA: GBARecomp explicitly includes a "Fallback interpreter (emulation)" and an "embedded jit compiler" for uncovered paths. [1379.tech](https://1379.tech/expanding-the-recomp-ecosystem-with-gbarecomp)
- DS: ndsrecomp translates ARM code ahead of time but "Code copied into RAM by the guest currently uses a bounded interpreter tier," and reads live opcodes as a fallback. [mstan/ndsrecomp](https://github.com/mstan/ndsrecomp); [Myth7x/ndsrecomp](https://github.com/Myth7x/ndsrecomp)
- SNES recomp links a real emulator hardware backend (LakeSnes) — the CPU code is recompiled, but hardware peripheral behavior is emulated. [snesrecomp README](https://github.com/sp00nznet/snesrecomp)
- libultraship: "not a recompilation tool"; compiles the game's decompiled original source. [libultraship Porting Guide](https://libultraship.com/PORTING.html)
- Digital Foundry framing: recompilation "is not emulating the behaviour of the hardware… nor is it going as far as decompilation." [Nintendo Life, 2024-05-26](https://www.nintendolife.com/news/2024/05/video-digital-foundry-takes-a-look-at-zelda-64-recompiled-majoras-mask)

### Inferences
- A practical ordering from "most emulatory" to "most native":
  1. **Interpreter emulator** — decodes/executes each console instruction at runtime.
  2. **Dynamic recompiler/JIT emulator** — translates machine code to native at runtime (still emulation).
  3. **Static recompilation** — machine code translated to C/C++ ahead of time; native execution, no runtime decode of translated code. Console peripherals/memory map are usually reimplemented (or partly emulated as libraries). Some ecosystems (PSXRecomp, GBARecomp, ndsrecomp) add a *transient* interpreter/JIT fallback for coverage.
  4. **Decompilation-based port (libultraship)** — no binary translation and no interpreter; original reconstructed C source compiled natively against reimplemented SDK/renderer. Closest to a from-source port.
- So the user's framing holds: N64Recomp sits **between** emulation and decompilation; libultraship ports sit **at the decompilation/native end**. For the DS, existing static-recompilers are hybrids that currently still interpret some code, whereas a falsifiable claim that a decompilation port "emulates the console" is weak, since no console machine code is executed at runtime.

### Gaps
- "No interpreter/JIT at all" is strictly true for N64Recomp-generated CPU code and for ReXGlue/NFSMW, but is NOT universally true for the static-recompilation category (PS1/GBA/DS ecosystems retain interpreter or JIT fallbacks). Any general statement about "static recompilation" must be qualified per project.
- The exact share of runtime that ndsrecomp still interprets (the "bounded interpreter tier") is not quantified in the sources found.

---

## Cross-cutting notes and conflicts
- **AI-generated sources:** Several architectural specifics (RDRAM as one contiguous host allocation, `recomp_context`/two-memory-space bridging, `syms.ld` at `0x8F000000`) come from DeepWiki pages, which are machine-generated summaries of the repos, not primary docs. They are consistent with the author's own statement about "simple pointer arithmetic," but should be treated as secondary. Primary confirmation of the RDRAM-host-buffer design from a first-party N64ModernRuntime/Zelda64Recomp document was not found.
- **Ecosystem is fast-moving:** ndsrecomp and the *recomp (PSX/SNES/GBA) projects carry 2026 dates; some repos found are mirrors with unknown provenance, and one (`wize-logic/pokeblack-recomp`) could not be verified.
- **Legal model shared across techniques:** both recompilation and decompilation-based ports require the user's own game dump; recomp projects state they ship no copyrighted code (e.g. [Zelda64Recomp](https://github.com/Zelda64Recomp/Zelda64Recomp), [KameoRePowered](https://github.com/birabittoh/KameoRePowered), [ndsrecomp](https://github.com/mstan/ndsrecomp)).
