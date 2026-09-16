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
)

# ---------------------------------------------------------------------------
# The shim and the stubs
# ---------------------------------------------------------------------------

set(BW_SDK_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/fs.c            # FS_SeekFile/ReadFile/CloseFile
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/mi.c            # MI_CpuCopy/Fill family
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/os_alloc.c      # OS heap onto malloc
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/narc.c          # data root, NARC framing, ARM9 rodata
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/game_globals.c  # the ARM9 globals the matched code names
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/aliases.c       # sub_/func_ spelling forwarders
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
)

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
)

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

# The synthetic engine tests need neither game data nor an oracle: every
# expected pixel in them is computed on paper from the hardware rule under
# test.  They are the counterweight to the oracle comparisons, which can
# only ever show that two implementations agree.
foreach(t test_affine test_bg_affine)
    add_executable(${t} ${CMAKE_CURRENT_SOURCE_DIR}/tests/${t}.c)
    target_include_directories(${t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(${t} PRIVATE eidos)
    add_test(NAME ${t} COMMAND ${t})
endforeach()
