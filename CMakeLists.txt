#
# CMakeLists.txt -- the native port of the matched game code (eidos).
#
#   cmake -S . -B build -DALETHEIA_ROOT=/path/to/aletheia -DBW_DATA_ROOT=extract/data
#   cmake --build build
#   ctest --test-dir build --output-on-failure
#
# Nothing here compiles the DS build.  The matched sources under
# ${ALETHEIA_ROOT}/src/matched/ are compiled *in place*, unchanged, for the
# host; the aletheia repository's own DS build still compiles the same files
# with mwccarm through its tools/csrc.py.  A matched file that has to change
# to build here has to be re-verified against the ROM in the same commit, in
# the aletheia repository (README.md, "Portability rules").
#

cmake_minimum_required(VERSION 3.16)
project(eidos C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Where the aletheia decompilation checkout lives.  eidos consumes matched
# sources, layout/ manifests and tools/assets/ scripts from it as an external
# checkout; nothing from aletheia is copied into this repository.  The
# default assumes the two repositories are cloned as siblings; pass this
# explicitly when they are not (see README.md).
# The sibling checkout may keep the repository's original folder name
# (bw_decomp); both spellings are tried before giving up.
set(_aletheia_default "${CMAKE_CURRENT_SOURCE_DIR}/../aletheia")
if(NOT EXISTS "${_aletheia_default}/src/matched"
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../bw_decomp/src/matched")
    set(_aletheia_default "${CMAKE_CURRENT_SOURCE_DIR}/../bw_decomp")
endif()
set(ALETHEIA_ROOT "${_aletheia_default}" CACHE PATH
    "Path to the aletheia decompilation repository checkout")
get_filename_component(ALETHEIA_ROOT "${ALETHEIA_ROOT}" ABSOLUTE)
if(NOT EXISTS "${ALETHEIA_ROOT}/src/matched")
    message(FATAL_ERROR "ALETHEIA_ROOT=${ALETHEIA_ROOT} does not contain src/matched; pass -DALETHEIA_ROOT=<path to the aletheia checkout>")
endif()

# Where the player's extracted assets live.  Relative paths are taken against
# the aletheia checkout, so -DBW_DATA_ROOT=extract/data does the obvious
# thing when aletheia is where the assets were extracted.
set(BW_DATA_ROOT "${ALETHEIA_ROOT}/extract/data" CACHE STRING
    "Directory holding the extracted NitroFS tree (the one with a/0/0/2)")
if(IS_ABSOLUTE "${BW_DATA_ROOT}")
    set(BW_DATA_ROOT_ABS "${BW_DATA_ROOT}")
else()
    set(BW_DATA_ROOT_ABS "${ALETHEIA_ROOT}/${BW_DATA_ROOT}")
endif()

find_package(Python3 COMPONENTS Interpreter REQUIRED)

# ---------------------------------------------------------------------------
# Pointer width
# ---------------------------------------------------------------------------
#
# The DS is LP32.  Every struct the game's own layout owns was laid out by a
# compiler with 4-byte pointers, and a matched source that reaches a field of
# such a struct through an explicit byte offset is only correct on a target
# that agrees.  docs/pointer-width.md measures how much matched code that is
# and records the policy; the mechanism is this one switch.
#
# ON:  the nine EXACT tcbl sources that depend on a four-byte pointer
#      (sub_02030EF0, sub_02030F34, func_02030F04, sub_02030FB4, and the
#      wave-26 five sub_02030F50, sub_02030F70, sub_020311B8, sub_02031194,
#      sub_0203115C) are compiled AS WRITTEN, straight out of aletheia's
#      src/matched/, and the stubs/reimpl_ stand-ins are left out of the
#      library.  Correct only where sizeof(void *) == 4.
# OFF: the stand-ins are compiled and the matched nine stay out, which is the
#      state README.md's "Why nine matched files are out of the LP64 build"
#      describes.
#
# The first four reach a field through a DS byte offset.  The other five
# declare the node's +0x00 and +0x10 as pointers where sub_0203101C and
# sub_020310C4, which are in the build unconditionally, declare them as u32:
# the same struct on the DS and on any ILP32 target, a different one on LP64.
#
# The default is decided at configure time from the target's pointer width,
# so `-DCMAKE_TOOLCHAIN_FILE=cmake/ilp32-toolchain.cmake` is the whole of
# what an ILP32 build has to say.
#
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(_eidos_tcbl_matched_default ON)
else()
    set(_eidos_tcbl_matched_default OFF)
endif()
option(EIDOS_TCBL_MATCHED
       "Compile the nine pointer-width-dependent tcbl sources as written (needs 32-bit pointers)"
       ${_eidos_tcbl_matched_default})
if(EIDOS_TCBL_MATCHED AND NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR
        "EIDOS_TCBL_MATCHED=ON needs a target with 32-bit pointers; "
        "this one has sizeof(void *) = ${CMAKE_SIZEOF_VOID_P}.  Configure with "
        "-DCMAKE_TOOLCHAIN_FILE=cmake/ilp32-toolchain.cmake, or leave the "
        "option at its default.")
endif()
message(STATUS "Pointer width: ${CMAKE_SIZEOF_VOID_P} bytes; tcbl sources: "
               "${EIDOS_TCBL_MATCHED} (ON = the matched nine, "
               "OFF = stubs/reimpl_)")

# ---------------------------------------------------------------------------
# Compiler settings
# ---------------------------------------------------------------------------
#
# `-char signed` is part of the pinned DS compiler configuration (see
# aletheia's docs/AGENT-BRIEF.md); the host must agree, because matched code
# that stores into a plain `char` and compares it against a negative value
# behaves differently otherwise.
#
if(MSVC)
    add_compile_options(/W3 /J /wd4996)
else()
    add_compile_options(-Wall -Wextra -fsigned-char)
    # The matched sources are written to reproduce mwccarm's output, not to
    # please a modern warning set.  They are compiled with the same warnings
    # as the port's own code, but the ones that are inherent to the style are
    # turned off for them specifically, further down.
endif()

# ---------------------------------------------------------------------------
# The curated list of matched translation units
# ---------------------------------------------------------------------------
#
# Grown mechanically, never by hand-guessing:
#
#     python tools/closure.py --seed-func func_02018A7C
#     python tools/closure.py --seed-func func_02018A7C --cmake
#
# prints the transitive closure through matched files and, separately, every
# name that is still open (needs a stubs/ entry) or is a global object
# (needs a sdk/game_globals.c entry).  Add a file here only when the closure
# report says what it needs and that need is met.
#
set(BW_MATCHED_SOURCES
    # --- archive reading -------------------------------------------------
    ${ALETHEIA_ROOT}/src/matched/func_02049578.c   # member index -> absolute position
    ${ALETHEIA_ROOT}/src/matched/func_020495F4.c   # stream read
    ${ALETHEIA_ROOT}/src/matched/func_0204944C.c   # read a range inside a member
    ${ALETHEIA_ROOT}/src/matched/func_02048D00.c   # open by id, read whole member, close
    ${ALETHEIA_ROOT}/src/matched/func_02048F4C.c   # the two-argument front of it
    ${ALETHEIA_ROOT}/src/matched/sub_02034734.c    # the assert sink the readers call

    # --- message text ----------------------------------------------------
    ${ALETHEIA_ROOT}/src/matched/func_02045E3C.c   # the message-bank string cipher

    # --- personal data and the stat formula ------------------------------
    ${ALETHEIA_ROOT}/src/matched/unit_0201AFD4.c   # species + form -> member index
    ${ALETHEIA_ROOT}/src/matched/unit_0201ADA4.c   # read the 0x3C-byte record
    ${ALETHEIA_ROOT}/src/matched/unit_02019C54.c   # one-entry memo over that read
    ${ALETHEIA_ROOT}/src/matched/unit_02019B04.c   # nature multiplier
    ${ALETHEIA_ROOT}/src/matched/sub_02018A7C.c    # the six-stat recalculation

    # --- the heap layer --------------------------------------------------
    #
    # python tools/closure.py --seed-func sub_0203101C
    #
    # The whole chain from the game's allocation entry point down to the
    # expanded-heap allocator it bottoms out in, and the release side beside
    # it.  Every one of these is LP64-clean: none of them reaches a field of
    # a pointer-bearing struct through a DS byte offset, and the one 0x1c
    # that does appear is a block header with no pointers in it, so it is
    # 0x1c on any target.
    ${ALETHEIA_ROOT}/src/matched/unit_0202FBAC.c   # the four heap-record accessors
    ${ALETHEIA_ROOT}/src/matched/sub_02030098.c    # the raw allocator
    ${ALETHEIA_ROOT}/src/matched/sub_02030344.c    # checked heap id -> arena dump
    ${ALETHEIA_ROOT}/src/matched/sub_02030428.c    # heap id -> arena
    ${ALETHEIA_ROOT}/src/matched/sub_0203046C.c    # heap id -> live block count
    ${ALETHEIA_ROOT}/src/matched/sub_020304B8.c    # the block header's heap id
    ${ALETHEIA_ROOT}/src/matched/sub_020304C0.c    # the block header's provenance record
    ${ALETHEIA_ROOT}/src/matched/sub_02030734.c    # the allocation entry point
    ${ALETHEIA_ROOT}/src/matched/sub_020307B0.c    # the release entry point
    ${ALETHEIA_ROOT}/src/matched/sub_02030880.c    # stamp the provenance pair
    ${ALETHEIA_ROOT}/src/matched/sub_020308B8.c    # read it back and validate it
    ${ALETHEIA_ROOT}/src/matched/sub_02030958.c    # the out-of-memory reporter
    ${ALETHEIA_ROOT}/src/matched/sub_02030984.c    # the watched-allocation audit
    ${ALETHEIA_ROOT}/src/matched/sub_020309B8.c    # the free-side audit
    ${ALETHEIA_ROOT}/src/matched/sub_02030A00.c    # the watch filter
    ${ALETHEIA_ROOT}/src/matched/sub_02030A2C.c    # the free-side watch filter

    # The expanded heap's front door.  Its two callees (func_02060A08 and
    # func_02060ABC) are EXACT in aletheia too, and they stay out: both walk
    # an arena's free list and hand the winner to the still-open
    # func_02060888, and the port has no arena, because the heap environment
    # constructor at 0x0202FC48 is still open.  What this file contributes on
    # its own is the request rounding and the sign-of-the-alignment
    # direction convention, which is arena-independent.
    ${ALETHEIA_ROOT}/src/matched/func_02060CA0.c   # round the request, pick the end

    # --- tcbl.c, the game's task-callback list ---------------------------
    #
    # The scheduler engine/frame/ runs as the frame loop's step.  These three
    # are in every configuration, and on an LP64 target they are also what
    # decides the host layout of the node and the manager: the constructor
    # allocates sizeof(struct Tcbl) as its own compiler sees it and the
    # runner walks the node with its own offsets.
    #
    # Nine other EXACT files of this family are not in this list.  Four
    # (sub_02030EF0, sub_02030F34, func_02030F04, sub_02030FB4) reach a field
    # of the node or the manager through a DS byte offset.  Five more, all
    # matched in wave 26 (sub_02030F50, sub_02030F70, sub_020311B8,
    # sub_02031194, sub_0203115C), declare the node's +0x00 and +0x10 as
    # pointers where the three above declare them as u32, which is the same
    # struct on the DS and on any ILP32 target and a different one on LP64.
    # All nine are added below, or replaced by stubs/reimpl_ stand-ins, by
    # EIDOS_TCBL_MATCHED.  README.md, "Why nine matched files are out of the
    # LP64 build", records the reason for each.
    ${ALETHEIA_ROOT}/src/matched/sub_0203101C.c    # the constructor
    ${ALETHEIA_ROOT}/src/matched/sub_02030FC8.c    # build the free list
    ${ALETHEIA_ROOT}/src/matched/sub_020310C4.c    # one pass of the runner
)

# ---------------------------------------------------------------------------
# The shim and the stubs
# ---------------------------------------------------------------------------

set(BW_SDK_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/fs.c            # FS_SeekFile/ReadFile/CloseFile
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/mi.c            # MI_CpuCopy/Fill family
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/os_alloc.c      # OS heap onto malloc
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/os_irq.c        # OS_DisableInterrupts/Restore/Terminate
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/gx.c            # the one GX name the heap layer reaches
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/tcbl.c          # the port's helper over tcbl.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/vfs.c           # data root, mod overlay, path resolution
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/narc.c          # NARC framing, ARM9 rodata
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/game_globals.c  # the ARM9 globals the matched code names
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/aliases.c       # sub_/func_ spelling forwarders
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/os_tick.c       # OS_GetTick onto the frame loop
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/panic.c
)

# One symbol per file, on purpose: a static-library member is only pulled in
# when nothing else defines its symbol, which is how tests/test_stats.c
# supplies its own record accessors without displacing anything else.
set(BW_STUB_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/reimpl_02045A98.c  # end-of-string code unit
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/reimpl_0204960C.c  # archive stream seek
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/stub_02048DDC.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/stub_02048E34.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/stub_02017E1C.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/stub_02017E40.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/stub_02018598.c

    # --- the heap layer's floor ------------------------------------------
    #
    # Everything above the arena is matched now.  What is left here is the
    # arena itself, and it stays the port's until the heap environment
    # constructor at 0x0202FC48 is decompiled: without it there is no region
    # for a free-list walk to walk and no block header for a free to read.
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/reimpl_02060A08.c  # the head-end walk
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/reimpl_02060ABC.c  # the tail-end walk
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/reimpl_02030164.c  # the raw free
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/stub_02030B50.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/stub_02060E6C.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/stub_02060E94.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/stub_02060F90.c
)

# The nine files the pointer width decides between.  Exactly one of the two
# lists enters the library; the symbols are the same either way, so nothing
# above or below this line knows which it got.
set(BW_TCBL_MATCHED
    ${ALETHEIA_ROOT}/src/matched/sub_02030EF0.c    # row address
    ${ALETHEIA_ROOT}/src/matched/sub_02030F34.c    # free-list push
    ${ALETHEIA_ROOT}/src/matched/func_02030F04.c   # free-list pop
    ${ALETHEIA_ROOT}/src/matched/sub_02030FB4.c    # the work accessor
    ${ALETHEIA_ROOT}/src/matched/sub_02030F50.c    # reset a node
    ${ALETHEIA_ROOT}/src/matched/sub_02030F70.c    # unlink one and free it
    ${ALETHEIA_ROOT}/src/matched/sub_020311B8.c    # register a callback
    ${ALETHEIA_ROOT}/src/matched/sub_02031194.c    # destroy one node
    ${ALETHEIA_ROOT}/src/matched/sub_0203115C.c    # destroy every node
)
set(BW_TCBL_REIMPL
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/reimpl_02030EF0.c  # row address
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/reimpl_02030F34.c  # free-list push
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/reimpl_02030F04.c  # free-list pop
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/reimpl_02030FB4.c  # the work accessor
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/reimpl_02030F50.c  # reset a node
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/reimpl_02030F70.c  # unlink one and free it
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/reimpl_020311B8.c  # register a callback
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/reimpl_02031194.c  # destroy one node
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/reimpl_0203115C.c  # destroy every node
)

if(EIDOS_TCBL_MATCHED)
    list(APPEND BW_MATCHED_SOURCES ${BW_TCBL_MATCHED})
else()
    list(APPEND BW_STUB_SOURCES ${BW_TCBL_REIMPL})
endif()

# ---------------------------------------------------------------------------
# The engine
# ---------------------------------------------------------------------------
#
# eidos's own code, not a shim over an SDK name and not game code: the
# host-side reimplementation of hardware the DS had and a PC does not.
# engine/g2d/ is the 2D (OBJ/OAM) software compositor; every rule it
# implements is attested by a matched source, by aletheia's docs/ASSETS.md
# or by public NDS hardware documentation, and the file headers cite which.
# No SDL, no third-party code, standard library only.
#
set(BW_ENGINE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/g2d/lzss.c       # LZ77/LZ11 containers
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/g2d/resources.c  # NCGR/NCLR/NCER/NANR/NMCR
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/g2d/affine.c     # PA..PD, the per-pixel resample
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/g2d/compose.c    # the OBJ compositor
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/g2d/rig.c        # rig block loading
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/g2d/bg.c         # BG layers, the frame compositor
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/g2d/bg_affine.c  # rotation/scaling BG layers
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/g2d/window.c     # windows, alpha and brightness
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/frame/frame.c    # the vblank frame loop and the OS tick

    # engine/g3d/ -- the geometry engine and the software rasteriser, the
    # host's stand-in for the DS 3D hardware.  No SDL here either: the
    # parsing and the vertex pipeline are checked against aletheia's own
    # OBJ export by tests/test_g3d.c, and the rasteriser writes an RGBA
    # buffer in exactly the shape g2d_frame_compose does.
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/g3d/mtx.c        # 20.12 fixed point, the matrix stack
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/g3d/texture.c    # the seven GX texture formats
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/g3d/nsbmd.c      # NSBMD/NSBTX, dictionaries, shapes
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/g3d/gx.c         # the display-list interpreter
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/g3d/raster.c     # the software rasteriser

    # engine/audio/ -- the SDAT sequencer and mixer, the host's stand-in for
    # the ARM7 sound core.  No SDL here either: the platform layer's audio
    # callback calls into this, and tests/test_audio.c drives it headless
    # against aletheia's tools/assets/sseqsynth.py.
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/audio/sdat.c     # SYMB/INFO/FAT/FILE
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/audio/sbnk.c     # instrument tables
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/audio/swav.c     # SWAR/SWAV, PCM8/PCM16/ADPCM
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/audio/sseq.c     # the SSEQ interpreter
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/audio/mixer.c    # one voice: resample, ADSR, PSG
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/audio/player.c   # render and stream a sequence
)

add_library(eidos STATIC
    ${BW_MATCHED_SOURCES}
    ${BW_SDK_SOURCES}
    ${BW_STUB_SOURCES}
    ${BW_ENGINE_SOURCES}
)
target_include_directories(eidos PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/g2d
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/g3d
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/frame
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/audio
)

# engine/audio is the only part of the engine that needs libm (pow for the
# semitone ratio, fmod for the square's phase, ceil/round/fabs).  On a
# toolchain that keeps the maths functions in a separate library it has to
# be named; where they are in libc, find_library simply finds nothing and
# this is a no-op.
find_library(BW_LIBM m)
if(BW_LIBM)
    target_link_libraries(eidos PUBLIC ${BW_LIBM})
endif()

# engine/audio is required to agree with its Python oracle *exactly* (see
# engine/audio/audio.h and tests/test_audio.c), and on 32-bit x86 the
# default floating point unit is x87, which evaluates double expressions in
# 80-bit registers and rounds to 64 bits only when a value is spilled.  That
# is not IEEE binary64 arithmetic, and it made every ILP32 render differ
# from the oracle's in the last ulp of a gain or a note-off time.  Asking
# for SSE2 instead gives the same 64-bit rounding at every step that every
# other target already has.  It is applied to the audio sources only: the
# rest of the engine is integer arithmetic and does not care.
include(CheckCCompilerFlag)
set(BW_AUDIO_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/audio/sdat.c
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/audio/sbnk.c
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/audio/swav.c
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/audio/sseq.c
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/audio/mixer.c
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/audio/player.c
)
if(CMAKE_SIZEOF_VOID_P EQUAL 4 AND NOT MSVC)
    check_c_compiler_flag("-msse2 -mfpmath=sse" BW_HAVE_SSE2_MATH)
    if(BW_HAVE_SSE2_MATH)
        set_source_files_properties(${BW_AUDIO_SOURCES} PROPERTIES
            COMPILE_OPTIONS "-msse2;-mfpmath=sse")
        message(STATUS "engine/audio: SSE2 double arithmetic (x87 excess "
                       "precision would break the oracle comparison)")
    else()
        message(WARNING "engine/audio: no -msse2 on this 32-bit compiler; "
                        "test_audio may differ from the oracle in the last ulp")
    endif()
endif()

# ---------------------------------------------------------------------------
# SDL2, optional
# ---------------------------------------------------------------------------
#
# SDL2 is needed only by engine/platform/ and by the eidos_view demo.  The
# engine library, the shim, the stubs and every other test build and run
# without it, so it is looked for rather than required, and everything that
# depends on it is skipped when it is not there.
#
# Three places are tried, in this order:
#
#   1. third_party/SDL2-<version>/<triplet>, which `python tools/fetch_sdl.py`
#      downloads from the official libsdl-org release and verifies against a
#      recorded SHA-256.  This is the no-installer path on Windows and is the
#      same pattern as the toolchain archives in aletheia's tools/.
#   2. an SDL2 CMake package (find_package), which is what a system package
#      manager installs.
#   3. pkg-config's sdl2.pc.
#
option(EIDOS_SDL "Build the SDL2 platform layer and the eidos_view demo" ON)

set(EIDOS_SDL_FOUND OFF)
set(EIDOS_SDL_ORIGIN "")
set(EIDOS_SDL_RUNTIME "")

if(EIDOS_SDL)
    # 1. the fetched tree
    file(GLOB _sdl_trees "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL2-*")
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(_sdl_triplet "x86_64-w64-mingw32")
    else()
        set(_sdl_triplet "i686-w64-mingw32")
    endif()
    foreach(_tree ${_sdl_trees})
        set(_cand "${_tree}/${_sdl_triplet}")
        if(EXISTS "${_cand}/include/SDL2/SDL.h"
           AND EXISTS "${_cand}/lib/libSDL2.dll.a"
           AND NOT EIDOS_SDL_FOUND)
            add_library(eidos_sdl2 INTERFACE)
            target_include_directories(eidos_sdl2 INTERFACE "${_cand}/include/SDL2")
            # Only libSDL2 itself: engine/platform/platform.c defines
            # SDL_MAIN_HANDLED and calls SDL_SetMainReady, so eidos keeps its
            # own main() and needs neither SDL2main nor the mingw32 stub.
            target_link_libraries(eidos_sdl2 INTERFACE "${_cand}/lib/libSDL2.dll.a")
            set(EIDOS_SDL_FOUND ON)
            set(EIDOS_SDL_ORIGIN "${_cand}")
            if(EXISTS "${_cand}/bin/SDL2.dll")
                set(EIDOS_SDL_RUNTIME "${_cand}/bin/SDL2.dll")
            endif()
        endif()
    endforeach()

    # 2. a system CMake package
    if(NOT EIDOS_SDL_FOUND)
        find_package(SDL2 QUIET)
        if(SDL2_FOUND)
            add_library(eidos_sdl2 INTERFACE)
            if(TARGET SDL2::SDL2)
                target_link_libraries(eidos_sdl2 INTERFACE SDL2::SDL2)
            else()
                target_include_directories(eidos_sdl2 INTERFACE ${SDL2_INCLUDE_DIRS})
                target_link_libraries(eidos_sdl2 INTERFACE ${SDL2_LIBRARIES})
            endif()
            set(EIDOS_SDL_FOUND ON)
            set(EIDOS_SDL_ORIGIN "system package")
        endif()
    endif()

    # 3. pkg-config
    if(NOT EIDOS_SDL_FOUND)
        find_package(PkgConfig QUIET)
        if(PKG_CONFIG_FOUND)
            pkg_check_modules(SDL2PC QUIET sdl2)
            if(SDL2PC_FOUND)
                add_library(eidos_sdl2 INTERFACE)
                target_include_directories(eidos_sdl2 INTERFACE ${SDL2PC_INCLUDE_DIRS})
                target_link_libraries(eidos_sdl2 INTERFACE ${SDL2PC_LIBRARIES})
                target_link_directories(eidos_sdl2 INTERFACE ${SDL2PC_LIBRARY_DIRS})
                set(EIDOS_SDL_FOUND ON)
                set(EIDOS_SDL_ORIGIN "pkg-config sdl2")
            endif()
        endif()
    endif()
endif()

if(EIDOS_SDL_FOUND)
    message(STATUS "SDL2: ${EIDOS_SDL_ORIGIN}")
else()
    message(STATUS "SDL2: not found; the platform layer and eidos_view are "
                   "skipped (run python tools/fetch_sdl.py, or install SDL2)")
endif()

# ---------------------------------------------------------------------------
# The platform layer
# ---------------------------------------------------------------------------

if(EIDOS_SDL_FOUND)
    add_library(eidos_platform STATIC
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/platform/platform.c
    )
    target_include_directories(eidos_platform PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/platform
    )
    target_link_libraries(eidos_platform PUBLIC eidos eidos_sdl2)
endif()

# The matched files are compiled with the port's warning set but without the
# ones that are inherent to reproducing a 2010 compiler's output: they declare
# each extern locally with the prototype that call site needs, so prototypes
# for the same function differ between translation units by design.
if(NOT MSVC)
    set_source_files_properties(${BW_MATCHED_SOURCES} PROPERTIES
        COMPILE_OPTIONS "-Wno-unused-parameter;-Wno-missing-prototypes")
endif()

# ---------------------------------------------------------------------------
# Generated fixtures
# ---------------------------------------------------------------------------

set(BW_FIXTURES ${CMAKE_CURRENT_BINARY_DIR}/bw_fixtures.h)

add_custom_command(
    OUTPUT ${BW_FIXTURES}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/fixtures.py
            --aletheia-root ${ALETHEIA_ROOT}
            --data-root ${BW_DATA_ROOT_ABS}
            --out ${BW_FIXTURES}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/fixtures.py
            ${ALETHEIA_ROOT}/tools/assets/text.py
            ${ALETHEIA_ROOT}/tools/assets/narc.py
    COMMENT "Generating port test fixtures from ${BW_DATA_ROOT_ABS}"
    VERBATIM
)
add_custom_target(bw_fixtures DEPENDS ${BW_FIXTURES})

# ---------------------------------------------------------------------------
# The 2D engine's pixel oracle
# ---------------------------------------------------------------------------
#
# tools/rig_oracle.py imports aletheia's tools/assets/rig.py (nothing is
# copied here, and aletheia is not modified) and dumps the rest pose of
# each species as a raw RGBA file next to the PNG rig.py itself writes.
# tests/test_g2d.c composes the same poses through engine/g2d and compares
# them pixel for pixel.  The dumps are game data derived from the player's
# own ROM: they are written into the build directory only, never here.
#
# The four reference species are identity poses at t=0 and are compared
# pixel for pixel.  Species 50 and 88 are the two genuinely non-identity SRT
# poses the asset lane's sweep found (a half-height scale and a rotation);
# the test renders them through the affine path and reports the divergence
# from the oracle rather than demanding equality, because the engine
# transforms in the hardware's fixed point and rig.py transforms in doubles
# (README.md, "the fixed-point question").
set(BW_G2D_SPECIES 25 6 94 150)
set(BW_G2D_AFFINE_SPECIES 50 88)
set(BW_G2D_ORACLE_DIR ${CMAKE_CURRENT_BINARY_DIR}/oracle/rig)

set(BW_G2D_ORACLE_FILES "")
foreach(sp ${BW_G2D_SPECIES} ${BW_G2D_AFFINE_SPECIES})
    # rig_oracle.py names its output with a four-digit species number.
    string(LENGTH "${sp}" _len)
    math(EXPR _pad "4 - ${_len}")
    set(_name "${sp}")
    while(_pad GREATER 0)
        set(_name "0${_name}")
        math(EXPR _pad "${_pad} - 1")
    endwhile()
    list(APPEND BW_G2D_ORACLE_FILES ${BW_G2D_ORACLE_DIR}/${_name}_pose.rgba)
endforeach()

add_custom_command(
    OUTPUT ${BW_G2D_ORACLE_FILES}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/rig_oracle.py
            --aletheia-root ${ALETHEIA_ROOT}
            --data-root ${BW_DATA_ROOT_ABS}
            --out ${BW_G2D_ORACLE_DIR}
            ${BW_G2D_SPECIES} ${BW_G2D_AFFINE_SPECIES}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/rig_oracle.py
            ${ALETHEIA_ROOT}/tools/assets/rig.py
            ${ALETHEIA_ROOT}/tools/assets/narc.py
            ${ALETHEIA_ROOT}/tools/assets/battle_sprite.py
    COMMENT "Generating the g2d pixel oracle with aletheia's rig.py"
    VERBATIM
)
add_custom_target(bw_g2d_oracle DEPENDS ${BW_G2D_ORACLE_FILES})

# ---------------------------------------------------------------------------
# The BG layer's pixel oracle
# ---------------------------------------------------------------------------
#
# tools/bg_oracle.py imports aletheia's tools/assets/background.py the same
# way, renders each screen below and writes the PNG, a raw RGBA dump and
# screens.txt, the manifest naming the NCGR/NCLR sub-file its coverage rule
# paired with each NSCR.  tests/test_bg.c renders the same three sub-files
# through engine/g2d/bg.c and compares pixel for pixel.
#
# The eight screens are picked for coverage rather than for looks:
#
#   titledemo.narc#0   256x256  8bpp  the Diamond/Pearl title logo the asset
#                                     lane identified; 255 distinct colours
#   a/1/4/3#22         512x192  4bpp  the battle-background terrain layer
#   a/1/3/7#26         256x256  4bpp  the "VS. RECORDER" UI banner
#   a/0/4/6#11         256x512  4bpp  the menu window frame, the tall size
#   a/0/3/4#17         512x256  8bpp  the wide size, in 256-colour mode
#   a/1/5/9#108        512x512  4bpp  the largest screen size
#   a/1/2/0#4          256x192  4bpp  55 distinct colours, the widest 4bpp
#                                     palette spread found in the scan
#   a/1/9/2#2          256x256  4bpp  29 distinct colours, the Wi-Fi grid
#
# so that all four hardware screen sizes, both colour modes and two
# authoring sizes that are not hardware sizes at all are covered.
#
# The entries are quoted because an unquoted '#' starts a CMake comment.
set(BW_BG_SCREENS
    "titledemo.narc#0"
    "a/1/4/3#22"
    "a/1/3/7#26"
    "a/0/4/6#11"
    "a/0/3/4#17"
    "a/1/5/9#108"
    "a/1/2/0#4"
    "a/1/9/2#2"
)
set(BW_BG_ORACLE_DIR ${CMAKE_CURRENT_BINARY_DIR}/oracle/bg)

set(BW_BG_ORACLE_FILES ${BW_BG_ORACLE_DIR}/screens.txt)
foreach(scr ${BW_BG_SCREENS})
    string(REPLACE ".narc" "" _n "${scr}")
    string(REPLACE "/" "_" _n "${_n}")
    string(REPLACE "#" "_" _n "${_n}")
    list(APPEND BW_BG_ORACLE_FILES ${BW_BG_ORACLE_DIR}/${_n}.rgba)
endforeach()

add_custom_command(
    OUTPUT ${BW_BG_ORACLE_FILES}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/bg_oracle.py
            --aletheia-root ${ALETHEIA_ROOT}
            --data-root ${BW_DATA_ROOT_ABS}
            --out ${BW_BG_ORACLE_DIR}
            ${BW_BG_SCREENS}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/bg_oracle.py
            ${ALETHEIA_ROOT}/tools/assets/background.py
            ${ALETHEIA_ROOT}/tools/assets/narc.py
            ${ALETHEIA_ROOT}/tools/assets/battle_sprite.py
    COMMENT "Generating the BG pixel oracle with aletheia's background.py"
    VERBATIM
)
add_custom_target(bw_bg_oracle DEPENDS ${BW_BG_ORACLE_FILES})

# ---------------------------------------------------------------------------
# The 3D engine's geometry oracle
# ---------------------------------------------------------------------------
#
# tools/g3d_oracle.py imports aletheia's tools/assets/nsbmd.py and
# tools/assets/map_shapes.py (nothing is copied here, and aletheia is not
# modified) and writes, per model, the per-shape textured OBJ that lane
# exports, plus models.txt: the manifest naming each shape's texture and
# the (width, height) the OBJ's `vt` lines were divided by, which is what
# lets tests/test_g3d.c compare texture coordinates in the sixteenths of
# a texel the TEXCOORD command actually carries.
#
# Two models, picked for coverage rather than for looks:
#
#   pl_00_00   a/0/0/8#120   an interior, 40 shapes, the model the asset
#                            lane's own shape-dictionary crack was
#                            confirmed against
#   map24_23   a/0/0/8#0     an outdoor terrain chunk, 26 shapes
#
# The manifest also carries the texture-format survey over a/0/1/4, the
# shared archive every map model draws from, which is what README.md's
# engine/g3d section quotes for which formats actually occur.
set(BW_G3D_MODELS pl_00_00:120 map24_23:0)
set(BW_G3D_ORACLE_DIR ${CMAKE_CURRENT_BINARY_DIR}/oracle/g3d)
set(BW_G3D_ORACLE_FILES
    ${BW_G3D_ORACLE_DIR}/models.txt
    ${BW_G3D_ORACLE_DIR}/pl_00_00.obj
    ${BW_G3D_ORACLE_DIR}/map24_23.obj
)

add_custom_command(
    OUTPUT ${BW_G3D_ORACLE_FILES}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/g3d_oracle.py
            --aletheia-root ${ALETHEIA_ROOT}
            --data-root ${BW_DATA_ROOT_ABS}
            --out ${BW_G3D_ORACLE_DIR}
            ${BW_G3D_MODELS}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/g3d_oracle.py
            ${ALETHEIA_ROOT}/tools/assets/nsbmd.py
            ${ALETHEIA_ROOT}/tools/assets/map_shapes.py
            ${ALETHEIA_ROOT}/tools/assets/narc.py
    COMMENT "Generating the g3d geometry oracle with aletheia's map_shapes.py"
    VERBATIM
)
add_custom_target(bw_g3d_oracle DEPENDS ${BW_G3D_ORACLE_FILES})

# ---------------------------------------------------------------------------
# The audio engine's oracle
# ---------------------------------------------------------------------------
#
# tools/audio_oracle.py imports aletheia's tools/assets/sseqsynth.py, the
# sample-based SSEQ renderer that lane wrote against the ROM's own SDAT, and
# writes one .wav and one .notes.txt per sequence plus seqs.txt, the
# manifest tests/test_audio.c reads.  Nothing is copied here and aletheia is
# not modified; the renders are game data and are written into the build
# directory only.
#
# The three sequences cover the three channel kinds the mixer implements:
#
#   1062  SEQ_BGM_GYM          PCM waves out of a real SWAR (the gym theme)
#   1146  SEQ_BGM_BATTLEPINCH  PSG square, alongside PCM on other tracks
#   1379  SEQ_SE_KOUKA_H       PSG noise
#
# The window is deliberately short.  It is the same window on both sides, it
# is long enough to reach several tempo segments and dozens of note-ons, and
# the oracle is a pure-Python mixer: a full-length render would put minutes
# into every build for no extra coverage.
set(BW_AUDIO_SEQS 1062 1146 1379)
set(BW_AUDIO_SECONDS 10.0)
set(BW_AUDIO_ORACLE_DIR ${CMAKE_CURRENT_BINARY_DIR}/oracle/audio)

set(BW_AUDIO_ORACLE_FILES ${BW_AUDIO_ORACLE_DIR}/seqs.txt)
foreach(sq ${BW_AUDIO_SEQS})
    list(APPEND BW_AUDIO_ORACLE_FILES ${BW_AUDIO_ORACLE_DIR}/seq${sq}.wav)
endforeach()

add_custom_command(
    OUTPUT ${BW_AUDIO_ORACLE_FILES}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/audio_oracle.py
            --aletheia-root ${ALETHEIA_ROOT}
            --data-root ${BW_DATA_ROOT_ABS}
            --out ${BW_AUDIO_ORACLE_DIR}
            --max-seconds ${BW_AUDIO_SECONDS}
            ${BW_AUDIO_SEQS}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/audio_oracle.py
            ${ALETHEIA_ROOT}/tools/assets/sseqsynth.py
            ${ALETHEIA_ROOT}/tools/assets/sdat.py
            ${ALETHEIA_ROOT}/tools/assets/sbnk.py
            ${ALETHEIA_ROOT}/tools/assets/sseq.py
            ${ALETHEIA_ROOT}/tools/assets/swav.py
            ${ALETHEIA_ROOT}/tools/assets/adpcm.py
    COMMENT "Generating the audio oracle with aletheia's sseqsynth.py"
    VERBATIM
)
add_custom_target(bw_audio_oracle DEPENDS ${BW_AUDIO_ORACLE_FILES})

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

enable_testing()

foreach(t test_text test_stats)
    add_executable(${t} ${CMAKE_CURRENT_SOURCE_DIR}/tests/${t}.c ${BW_FIXTURES})
    add_dependencies(${t} bw_fixtures)
    target_include_directories(${t} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
    target_link_libraries(${t} PRIVATE eidos)
    add_test(NAME ${t} COMMAND ${t} ${BW_DATA_ROOT_ABS})
endforeach()

# The 2D engine test needs the oracle rather than the fixture header, and
# takes the oracle directory as its second argument.
add_executable(test_g2d ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_g2d.c)
add_dependencies(test_g2d bw_g2d_oracle)
target_include_directories(test_g2d PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(test_g2d PRIVATE eidos)
add_test(NAME test_g2d COMMAND test_g2d ${BW_DATA_ROOT_ABS} ${BW_G2D_ORACLE_DIR})

# The BG test reads its screen list out of the oracle's own manifest, so the
# set of screens is changed in one place (BW_BG_SCREENS above).
add_executable(test_bg ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_bg.c)
add_dependencies(test_bg bw_bg_oracle)
target_include_directories(test_bg PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(test_bg PRIVATE eidos)
add_test(NAME test_bg COMMAND test_bg ${BW_DATA_ROOT_ABS} ${BW_BG_ORACLE_DIR})

# The 3D engine test has two halves.  The geometry half reads the OBJ and
# the manifest the oracle wrote and compares every vertex and every
# texture coordinate of two real map models against this engine's own
# display-list decode.  The raster half renders one of them from a fixed
# camera into a BMP in the build directory and asserts non-trivial
# content, plus a set of synthetic checks whose expected pixels are
# computed on paper.  There is no DS frame to compare a picture against
# and the test does not pretend otherwise.
add_executable(test_g3d ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_g3d.c)
add_dependencies(test_g3d bw_g3d_oracle)
target_include_directories(test_g3d PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(test_g3d PRIVATE eidos)
add_test(NAME test_g3d COMMAND test_g3d ${BW_DATA_ROOT_ABS} ${BW_G3D_ORACLE_DIR}
                               ${CMAKE_CURRENT_BINARY_DIR}/g3d_house.bmp)

# The audio test reads its sequence list out of the audio oracle's own
# manifest, so the set of sequences is changed in one place
# (BW_AUDIO_SEQS above), and writes its own note dump next to the oracle's
# so a failure leaves both files on disk to diff by hand.
add_executable(test_audio ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_audio.c)
add_dependencies(test_audio bw_audio_oracle)
target_include_directories(test_audio PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(test_audio PRIVATE eidos)
add_test(NAME test_audio COMMAND test_audio ${BW_DATA_ROOT_ABS} ${BW_AUDIO_ORACLE_DIR})

# The synthetic engine tests need neither game data nor an oracle: every
# expected pixel in them is computed on paper from the hardware rule under
# test.  They are the counterweight to the oracle comparisons, which can
# only ever show that two implementations agree.
foreach(t test_affine test_bg_affine test_window test_frame test_tcbl test_vfs)
    add_executable(${t} ${CMAKE_CURRENT_SOURCE_DIR}/tests/${t}.c)
    target_include_directories(${t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(${t} PRIVATE eidos)
    add_test(NAME ${t} COMMAND ${t})
endforeach()

# ---------------------------------------------------------------------------
# eidos_view -- the demo, and the first visible output
# ---------------------------------------------------------------------------
#
# The top screen is titledemo.narc member 0 over the NCGR/NCLR pair
# background.py's coverage rule paired with it (members 1 and 2, the same
# triple tests/test_bg.c checks pixel for pixel); the bottom screen is one
# composed rig pose out of a/0/0/4.  Both come from the player's own extract/;
# nothing is bundled.
#
# test_view runs it with SDL_VIDEODRIVER=dummy and SDL_AUDIODRIVER=dummy, so
# ctest checks the picture on a machine with no display and no sound card.
#
if(EIDOS_SDL_FOUND)
    add_executable(eidos_view ${CMAKE_CURRENT_SOURCE_DIR}/demo/eidos_view.c)
    target_link_libraries(eidos_view PRIVATE eidos_platform)
    target_compile_definitions(eidos_view PRIVATE
        EIDOS_DEFAULT_DATA_ROOT="${BW_DATA_ROOT_ABS}")

    # SDL2.dll has to sit next to the executable on Windows: there is no
    # installer and nothing on PATH.
    if(EIDOS_SDL_RUNTIME)
        add_custom_command(TARGET eidos_view POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${EIDOS_SDL_RUNTIME}" "$<TARGET_FILE_DIR:eidos_view>"
            COMMENT "Copying SDL2.dll next to eidos_view")
    endif()

    add_executable(test_view ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_view.c)
    target_include_directories(test_view PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(test_view PRIVATE eidos)
    add_dependencies(test_view eidos_view)
    add_test(NAME test_view
             COMMAND test_view $<TARGET_FILE:eidos_view> ${BW_DATA_ROOT_ABS}
                     ${CMAKE_CURRENT_BINARY_DIR}/eidos_view.bmp)
    set_tests_properties(test_view PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
endif()
