# Milestones in detail

## Planned Milestones

- [ ] **Milestone 1**: Portability pass
  - [x] Fix pointer width assumptions (32-bit to 64-bit host): the first
        game-owned structs that store pointers are in the build (the
        task-callback node and its manager), the host layout is written
        down beside the DS one in `sdk/tcbl.h`, the nine matched sources
        that depend on a four-byte pointer are identified and stood in for,
        and the two `u32`-declared pointer fields are answered by wave 26:
        both really are pointers, and two matched declarations of the same
        struct now disagree on an LP64 host. See "Why nine matched files
        are out of the LP64 build"
  - [x] Settle the pointer-width policy: an ILP32 build configuration
        (`cmake/ilp32-toolchain.cmake`) where the nine matched sources
        are compiled as written and the game's structs have the DS's own
        bytes, the LP64 build kept working through one CMake switch keyed
        on `sizeof(void *)`, and the cost of each route measured over the
        whole matched corpus by `tools/closure.py --pointer-audit`. ILP32
        is the reference configuration; `docs/pointer-width.md` records the
        counts, the decision and what would reverse it. Wave 26 is the
        first case the policy *decided* rather than merely survived: two
        EXACT readings of one struct, one target on which they agree
  - [x] Endianness and struct packing compatibility: every read out of
        `extract/` goes through `bw_rd16`/`bw_rd32`, `char` is signed on
        every compiler the build supports, and no matched struct in the
        source list is reached through a byte offset that the host lays
        out differently
  - [ ] Under-declared globals and compiler-specific extensions (two known
        and recorded under "Known type mismatches", two more found in the
        tcbl family; the sweep itself is not done)
  - [x] Warning-free builds on GCC, Clang, and MSVC: warning-free and
        green on the pinned MinGW-w64 GCC at both pointer widths, and on
        MSVC x64, MSVC x86 (the ILP32 reference) and clang-cl x64 at
        `/W4`, all with the real game data, through `tools/check.py` (see
        "Building with MSVC"). GCC and Clang on Linux, and the ASan/UBSan
        configurations, are in `tools/check.py` too but have not been run
        on a real Linux machine yet
- [ ] **Milestone 2**: Platform layer (SDL2)
  - [x] Window creation and input handling: an integer scale of the DS
        layout, stacked or side by side, a configurable SDL scancode table
        for the keypad and the frame upload of two RGBA buffers
        (`engine/platform/`)
  - [x] Audio device initialization and timing: 32768 Hz stereo s16, the DS
        mixer's own rate, with a silent callback until the sequencer exists;
        and the frame pacer on the exact 59.8261 Hz period with the
        remainder accumulated
  - [x] The game's own scheduler runs the frame: the whole of aletheia's
        tcbl.c is in the build (construct, build the free list, register,
        run one pass, destroy one node, destroy all, and the four
        accessors), matched under ILP32, and `demo/eidos_view`'s step
        function is `src/matched/sub_020310C4.c`
  - [x] The heap the scheduler allocates from is matched end to end: the
        allocation entry point, the raw allocator, the provenance stamp and
        both audits, the release entry point and the expanded heap's front
        door. What is left port-side is the arena, which needs the heap
        environment constructor at `0x0202FC48`
  - [x] DS frame emulation loop: task list scheduler running at 60 Hz,
        vblank callbacks. `engine/frame/` is the vblank-driven loop, the
        commit-at-vblank convention and the DS OS tick (`sdk/os_tick.c`
        answers `OS_GetTick`), and the scheduler inside it is the game's
        own: `src/matched/sub_0203101C.c` builds the list on the matched
        heap layer and `src/matched/sub_020310C4.c` is the step function
        `demo/eidos_view` installs
  - [x] The game's own boot runs the frame: NitroMain (aletheia's matched
        `func_02005124.c`) runs natively from power-on with the title-path
        overlays compiled in (ov009, ov010, ov011, ov013, ov017, ov088,
        ov179, ov228): the Nintendo and Pokemon Company logos, the
        copyright screen, the opening cinematic, the title screen and, with
        START, the title menu (NEW GAME / MYSTERY GIFT / NINTENDO WFC
        SETTINGS / MIC TEST), in a window (`eidos_boot --window`) or
        headless; `test_boot_run` asserts the menu. See "Running the
        game's boot" and `docs/boot-handoff.md`
- [ ] **Milestone 3**: 2D engine shim
  - [x] BG layers from tile and palette memory: NSCR screens, all four
        text screen sizes, 16- and 256-colour modes, scroll, priority and
        the BG/OBJ frame compositor (`engine/g2d/bg.c`, oracle-tested),
        plus the rotation/scaling layers for BG2 and BG3: one-byte map
        entries, 8bpp tiles, the four square sizes from 128 to 1024,
        wraparound and the 20.8 reference point
        (`engine/g2d/bg_affine.c`)
  - [x] OAM sprites: priorities, flips, cell banks and multi-cell poses
        (`engine/g2d/`, oracle-tested), with the affine path in the
        hardware's fixed point: an fx32 sine table, the 8.8 PA/PB/PC/PD
        parameters, the double-size reference point and the per-pixel
        inverse-mapped texel fetch (`engine/g2d/affine.c`)
  - [x] Windowing and alpha blending: WIN0/WIN1, the OBJ window,
        WININ/WINOUT layer masks, and BLDCNT's alpha blend and the two
        BLDY brightness effects, under the DS rule that an effect runs
        only on the topmost pixel and blends only with the one directly
        beneath it (`engine/g2d/window.c`)
  - [x] Dual-screen rendering layout: two 256x192 screens, stacked or side
        by side, at an integer scale, each composed independently by
        `g2d_frame_compose_desc` and uploaded as its own texture
        (`engine/platform/`, `demo/eidos_view.c`)
  - [ ] Opening cinematic and title screen render correctly (both now
        run from the game's own code and are drawn from the hardware state
        it writes: logos, copyright, the opening's painted scenes and 3D,
        the title and its menu. Open: no pixel comparison against a DS
        capture yet, and the documented compositor approximations)
- [ ] **Milestone 4**: 3D engine shim
  - [x] DS fixed-point geometry engine emulation: the whole pipeline is
        20.12 integer arithmetic with 64-bit intermediates and no float
        and no `math.h` anywhere in `engine/g3d/`, including the
        perspective divide, the viewport transform and the camera
        helpers, which take the sine and cosine from `engine/g2d`'s
        existing table rather than adding a second one
  - [x] Matrix stack and vertex pipeline: the four matrix modes, the
        one-deep projection and texture saves, the 31-entry
        position/vector stack with its sticky over and underflow flag,
        load/mult in the 4x4, 4x3 and 3x3 forms, translate and scale,
        and a display-list interpreter covering the whole opcode table
        with all six vertex encodings, the current-vertex register,
        texcoord, colour, normal, polygon attributes and strip assembly
        (`engine/g3d/mtx.c`, `engine/g3d/gx.c`). Checked against
        aletheia's own OBJ export: 5678 of 5678 vertices and 1782 of
        1782 faces on one model, 2120 / 2120 and 572 / 572 on another,
        worst deviation 5.000e-07 against a 1e-6 tolerance
  - [x] Texture formats (A3I5, A5I3, 4x4 texel compressed, etc.): all
        seven decode (`engine/g3d/texture.c`), and the five that occur
        in `a/0/1/4`'s 21780 textures are surveyed and listed above.
        Format 5 is implemented from the public block encoding and has
        no real instance in this ROM; format 7 likewise does not occur
  - [ ] DS rasterizer quirks (perspective handling, edge rules): there
        is a rasteriser, it is documented rule by rule as hardware
        behaviour, approximation or port decision, and it has synthetic
        tests. It is **not** checked against a DS frame, and the two
        items in this line's name are precisely the two that are
        approximations: texture coordinates are interpolated affinely
        per span, and the fill rule is a conventional top-left one
        rather than the hardware's own edge inclusion. This box stays
        open until there is a frame to compare against
  - [ ] Overworld and map chunks render correctly: a map chunk renders,
        textured, from the player's own archives, and `eidos_view
        --map N` orbits it. The node dictionary's full SRT, its
        parent/child tree and the per-shape node binding are decoded and
        applied, so a multi-node model's parts sit at the positions and
        orientations its joint matrices put them at
        (`docs/g3d-nodes.md`); lighting, fog, edge marking,
        anti-aliasing and the w-buffer are done on the hardware path
        (`engine/g3d/light.c`, `engine/g3d/raster.c`); what is still
        missing is a pixel oracle
- [ ] **Milestone 5**: Audio
  - [x] SDAT sequencer and mixer (replacing ARM7 audio core): the SDAT
        container, SBNK instrument tables, SWAR/SWAV waves, the SSEQ
        bytecode interpreter and a 16-channel mixer at 32768 Hz, in pure C
        with no SDL (`engine/audio/`), checked sample for sample against
        aletheia's `sseqsynth.py` and wired into the platform layer's audio
        callback (`eidos_view --music`)
  - [x] PCM and ADPCM channels: PCM8, PCM16 and IMA-ADPCM decode with loop
        points, semitone-ratio resampling and the linear ADSR, bit-identical
        to the oracle on `SEQ_BGM_GYM`
  - [x] PSG square wave and noise channels: the eight duty settings, and the
        15-bit Galois LFSR clocked at the SND timer rate, bit-identical to
        the oracle on `SEQ_BGM_BATTLEPINCH` and `SEQ_SE_KOUKA_H`
  - [ ] Music, sound effects, and Pokémon cries play accurately (the game's
        SND commands play on the ARM7 driver, `sdk/arm7/snd_seq.c` and
        `snd_hw.c`: SDK-shaped players, tracks and shared channels, the
        SDK's own timer and volume arithmetic, Tie, portamento, the LFO,
        the pitch wheel, loop points and direct-channel streams; its ARM7
        side follows public driver documentation and has not been compared
        with the DS's own output; `engine/audio` keeps the asset lane's
        model for the oracle comparison)
  - [ ] The game's own sound calls reach it: the matched ARM9 code calls
        the SND library, but the boot does not run it yet, so the SEQ id is
        still chosen by the demo
- [ ] **Milestone 6**: Data pipeline & asset extraction
  - [x] First-run asset extractor from legally dumped ROM: `install/`
        identifies the dump, refuses a wrong game or a bad dump with a
        message, and extracts it atomically into ndsrom.py's layout plus
        `arm9_dec.bin` and `overlays_dec/` (726 of 726 files identical to
        aletheia's own extract, `test_extract_rom`); the setup window
        (`engine/platform/wizard.c`, one SDL implementation for every
        platform) opens when the data root is missing or not playable,
        takes the ROM from the native file dialog or a drop, extracts it
        with a progress bar and Cancel, and says why a file was refused;
        `--rom`, `--no-window`, `--reset-data`, `--move-data`, and a data
        location per platform (see "First run: the setup window").
        Verified on Windows: the window driven by scripted events under the
        dummy and the real Windows video driver, the real ROM installed
        through it and through `eidos_view --rom`, the real
        `IFileOpenDialog` opened and answered (`test_picker`), MinGW at
        both widths, `install/setup.c` and the picker under MSVC x64, x86
        and clang-cl. The Linux (portal, zenity, kdialog), macOS and
        Android branches of the C files are syntax-checked with clang
        against stand-in system headers only; `install/picker_mac.m` is not
        compiled (no macOS SDK here); the Android and iOS pickers are
        deferred with a plan in `docs/installer.md`
  - [ ] Readable-format conversion of the extracted assets (not started;
        split out of the box above, where it was listed as not started)
  - [x] File layer: VFS with mod overlay support (`sdk/vfs.c`: the data
        root and several mods in a manifest-ordered load order, whole-file
        overrides; `sdk/vfs_narc.c`: member-level NARC overrides that
        replace or add members, served to every reader, matched ones
        included, as one merged archive; `test_vfs`, `test_vfs_members`,
        see "The file layer")
  - [x] Engine boots cleanly from extracted assets: the game's own boot
        reaches the title menu from the player's extract (main RAM is the
        player's `arm9_dec.bin`, overlays come from `overlays_dec/`). Past
        the menu, NEW GAME loads ov204 (new-game intro) and ov194 (name
        entry); both are matched now (123 and 148 functions), and neither
        is compiled into the boot yet
- [ ] **Milestone 7**: Gameplay integration
  - [ ] Field system: player movement, warps, script VM, encounters
  - [ ] Battle system
  - [ ] Menus, UI, and save file management
  - [ ] Fully playable from new game to credits
- [ ] **Milestone 8**: Modding & enhancements
  - [ ] Scripting support and documented data formats
  - [ ] Debug and configuration overlay
  - [ ] High-resolution rendering / widescreen support
  - [ ] Stable plugin / mod API
- [ ] **Milestone 9**: Packaging & platforms
  - [ ] Windows, macOS, and Linux standalone builds
  - [ ] Mobile targets (Android / iOS)
- [ ] **Milestone 10**: Pokémon White support
  - [ ] Support for White version assets and script variations
