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

add_library(eidos STATIC
    ${BW_MATCHED_SOURCES}
    ${BW_SDK_SOURCES}
    ${BW_STUB_SOURCES}
)
target_include_directories(eidos PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/sdk)

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
