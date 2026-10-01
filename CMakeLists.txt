#
# CMakeLists.txt -- the native port of the matched game code (eidos).
#
#   cmake -S . -B build -DALETHEIA_ROOT=/path/to/aletheia -DBW_DATA_ROOT=extract/data
#   cmake --build build
#   ctest --test-dir build --output-on-failure
#
# Nothing here compiles the DS build.  The matched sources under
# ${ALETHEIA_ROOT}/src/matched/ are compiled unchanged for the host, through
# a build-tree copy in which tools/dsaddr.py has turned each cast of a fixed
# DS address into a call of bw_ds_ptr (cmake/dsaddr.cmake; a file with no
# such cast is copied byte for byte); the aletheia repository's own DS
# build still compiles the same files with mwccarm through its
# tools/csrc.py.  A matched file that has to change to build here has to be
# re-verified against the ROM in the same commit, in the aletheia
# repository (README.md, "Portability rules").
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
# Forward slashes only: the path is also compiled in as a C string literal
# (EIDOS_DEFAULT_DATA_ROOT in eidos_view and eidos_boot), where a
# Windows-style C:\Users\... reads as escape sequences.  Windows accepts '/'.
file(TO_CMAKE_PATH "${BW_DATA_ROOT_ABS}" BW_DATA_ROOT_ABS)

find_package(Python3 COMPONENTS Interpreter REQUIRED)

# Whether the player's extracted assets are there at all.  The fixture
# generator and the four oracles read them at build time, and seven tests
# read them at run time; without them the build still has to succeed and
# every synthetic test still has to run, so those pieces are switched on
# only when the data root holds a NitroFS tree.  A test that needs data and
# has none is registered DISABLED rather than left out, so ctest names it
# ("Not Run (Disabled)") instead of it silently vanishing from the count.
if(EXISTS "${BW_DATA_ROOT_ABS}/a" AND IS_DIRECTORY "${BW_DATA_ROOT_ABS}/a")
    set(_eidos_data_default ON)
else()
    set(_eidos_data_default OFF)
endif()
option(EIDOS_DATA_TESTS
       "Generate the fixtures and oracles and run the tests that need extracted game data"
       ${_eidos_data_default})
if(EIDOS_DATA_TESTS)
    message(STATUS "Game data: ${BW_DATA_ROOT_ABS}; data-driven tests enabled")
else()
    message(STATUS "Game data: none at ${BW_DATA_ROOT_ABS}; the data-driven tests "
                   "are built where they can be and registered as disabled "
                   "(pass -DBW_DATA_ROOT=<extract/data> to enable them)")
endif()

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
if(EIDOS_ILP32_REQUESTED AND NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR
        "cmake/ilp32-toolchain.cmake was given but the compiler produces "
        "${CMAKE_SIZEOF_VOID_P}-byte pointers.  Is the 32-bit runtime installed "
        "(gcc-multilib on Debian/Ubuntu), or does CMAKE_C_FLAGS override -m32?")
endif()
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

# The same question for the heap's floor.  ON puts the NitroSystem expanded
# heap into the build as written: the heap environment constructor
# (sub_0202FC48), heap creation, the free-list walks and the carve, the
# coalescing free and the child-heap binder, and leaves out the stand-ins
# that take blocks from the host allocator instead.  The expanded heap's
# block header is 0x10 bytes with two pointers in it and every one of these
# files does its address arithmetic in int, so ON needs 32-bit pointers
# exactly as EIDOS_TCBL_MATCHED does.
option(EIDOS_HEAP_MATCHED
       "Compile the expanded heap and the heap constructor as written (needs 32-bit pointers)"
       ${_eidos_tcbl_matched_default})
if(EIDOS_HEAP_MATCHED AND NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR
        "EIDOS_HEAP_MATCHED=ON needs a target with 32-bit pointers; "
        "this one has sizeof(void *) = ${CMAKE_SIZEOF_VOID_P}.  Configure with "
        "-DCMAKE_TOOLCHAIN_FILE=cmake/ilp32-toolchain.cmake, or leave the "
        "option at its default.")
endif()
message(STATUS "Pointer width: ${CMAKE_SIZEOF_VOID_P} bytes; heap floor: "
               "${EIDOS_HEAP_MATCHED} (ON = the matched expanded heap, "
               "OFF = host blocks)")
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
# behaves differently otherwise.  sdk/types.h checks it at compile time.
#
# MSVC (cl.exe and clang-cl) is configured in cmake/msvc.cmake: its plain
# char is already signed, so it gets no flag for it (in particular not /J,
# which makes char unsigned).
#
if(MSVC)
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/msvc.cmake)
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
    ${ALETHEIA_ROOT}/src/matched/arm9/func_02049578.c   # member index -> absolute position
    ${ALETHEIA_ROOT}/src/matched/arm9/func_020495F4.c   # stream read
    ${ALETHEIA_ROOT}/src/matched/arm9/func_0204944C.c   # read a range inside a member
    ${ALETHEIA_ROOT}/src/matched/arm9/func_02048D00.c   # open by id, read whole member, close
    ${ALETHEIA_ROOT}/src/matched/arm9_sdk/func_02048F4C.c   # the two-argument front of it
    ${ALETHEIA_ROOT}/src/matched/arm9/unit_02048DDC.c   # open an archive by id; resolve a member
    ${ALETHEIA_ROOT}/src/matched/arm9/sub_02034734.c    # the assert sink the readers call

    # --- message text ----------------------------------------------------
    ${ALETHEIA_ROOT}/src/matched/arm9/func_02045E3C.c   # the message-bank string cipher

    # --- personal data and the stat formula ------------------------------
    ${ALETHEIA_ROOT}/src/matched/arm9/unit_0201AFD4.c   # species + form -> member index
    ${ALETHEIA_ROOT}/src/matched/arm9/unit_0201ADA4.c   # read the 0x3C-byte record
    ${ALETHEIA_ROOT}/src/matched/arm9/unit_02019C54.c   # one-entry memo over that read
    ${ALETHEIA_ROOT}/src/matched/arm9/unit_02019B04.c   # nature multiplier
    ${ALETHEIA_ROOT}/src/matched/arm9/sub_02018A7C.c    # the six-stat recalculation

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
    ${ALETHEIA_ROOT}/src/matched/arm9/unit_0202FBAC.c   # the four heap-record accessors
    ${ALETHEIA_ROOT}/src/matched/arm9/sub_02030098.c    # the raw allocator
    ${ALETHEIA_ROOT}/src/matched/arm9/sub_02030344.c    # checked heap id -> arena dump
    ${ALETHEIA_ROOT}/src/matched/arm9/sub_02030428.c    # heap id -> arena
    ${ALETHEIA_ROOT}/src/matched/arm9/sub_0203046C.c    # heap id -> live block count
    ${ALETHEIA_ROOT}/src/matched/arm9/sub_020304B8.c    # the block header's heap id
    ${ALETHEIA_ROOT}/src/matched/arm9/sub_020304C0.c    # the block header's provenance record
    ${ALETHEIA_ROOT}/src/matched/arm9/sub_02030734.c    # the allocation entry point
    ${ALETHEIA_ROOT}/src/matched/arm9_sdk/sub_020307B0.c    # the release entry point
    ${ALETHEIA_ROOT}/src/matched/arm9/sub_02030880.c    # stamp the provenance pair
    ${ALETHEIA_ROOT}/src/matched/arm9/sub_020308B8.c    # read it back and validate it
    ${ALETHEIA_ROOT}/src/matched/arm9/sub_02030958.c    # the out-of-memory reporter
    ${ALETHEIA_ROOT}/src/matched/arm9/sub_02030984.c    # the watched-allocation audit
    ${ALETHEIA_ROOT}/src/matched/arm9/sub_020309B8.c    # the free-side audit
    ${ALETHEIA_ROOT}/src/matched/arm9/sub_02030A00.c    # the watch filter
    ${ALETHEIA_ROOT}/src/matched/arm9/sub_02030A2C.c    # the free-side watch filter

    # The expanded heap's front door: round the request and pick the end of
    # the heap by the sign of the alignment.  Arena-independent, so it is in
    # every configuration; what it dispatches to is decided by
    # EIDOS_HEAP_MATCHED below.
    ${ALETHEIA_ROOT}/src/matched/arm9/func_02060CA0.c   # round the request, pick the end

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
    ${ALETHEIA_ROOT}/src/matched/arm9/sub_0203101C.c    # the constructor
    ${ALETHEIA_ROOT}/src/matched/arm9/sub_02030FC8.c    # build the free list
    ${ALETHEIA_ROOT}/src/matched/arm9/sub_020310C4.c    # one pass of the runner

    # --- the interrupt controller's matched half -------------------------
    #
    # Register and table traffic the OS layer's dispatcher agrees with
    # (sdk/os_interrupt.c); sdk/pxi.c's PXI_InitFifo calls both, so every
    # link that reaches bw_port_reset needs them.
    ${ALETHEIA_ROOT}/src/matched/arm9_sdk/OS_SetIrqFunction.c   # OS_IRQTable and the callback slots
    ${ALETHEIA_ROOT}/src/matched/arm9_sdk/OS_EnableIrqMask.c    # IE |= mask, under IME off
)

# ---------------------------------------------------------------------------
# The shim and the stubs
# ---------------------------------------------------------------------------

# The overlay registry's table (sdk/overlay.h), generated from aletheia's
# layout.json and manifest shards and this build's own source list by
# tools/overlay_table.py.  Generated rather than committed because its
# coverage columns move with every aletheia wave and every source added
# here.  It is regenerated when the script, layout.json, symbols_sdk.txt,
# any overlay shard present at configure time, or the build's source list
# changes; a shard added by a new wave is picked up at the next configure.
set(BW_OVERLAY_TABLE ${CMAKE_CURRENT_BINARY_DIR}/overlay_table.c)
set(BW_BUILD_SOURCE_LIST ${CMAKE_CURRENT_BINARY_DIR}/eidos_sources.txt)
file(GLOB BW_OVERLAY_SHARDS ${ALETHEIA_ROOT}/layout/matched/ov*/*.json)

# The IPC FIFO model and the ARM7 behind it (sdk/pxi.h, sdk/arm7/arm7.h).
set(BW_PXI_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/pxi.c           # FIFO pair, IPCSYNC, PXI_InitFifo, the IRQ 18 handler
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/arm7/arm7.c     # ARM7 power-on and step, OS tag 12, OS lock sync
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/arm7/pm.c       # power manager, tag 8
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/arm7/rtc.c      # real-time clock, tag 5
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/arm7/ctrdg.c    # Slot-2, tag 13
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/arm7/card.c     # Slot-1 pull-out, tag 14
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/arm7/fs.c       # Slot-1 backup requests, tag 11
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/arm7/snd.c      # sound command lists, tag 7
)

set(BW_SDK_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/fs.c            # the ROM file system onto the extract
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/overlay.c       # overlays: compiled in, checked on load
    ${BW_OVERLAY_TABLE}                             # generated, see below
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/mi.c            # MI_CpuCopy/Fill family
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/os_alloc.c      # OS heap onto malloc
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/os_arena.c      # OS_AllocFromArenaLo, the heaps' arena
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/os_irq.c        # OS_DisableInterrupts/Restore/Terminate
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/gx.c            # the one GX name the heap layer reaches
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/g3_fifo.c       # GX_SendFifo48B/64B, GXi_NopClearFifo128_ onto hw3d
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/mi_gx.c         # MI copies and GX command DMA onto hw3d
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/tcbl.c          # the port's helper over tcbl.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/vfs.c           # data root, mod list, path resolution
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/vfs_narc.c      # member-level NARC overrides, the merged archive
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/plat_dir.c      # path kind and directory listing, Win32 and POSIX
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/narc.c          # NARC framing, ARM9 rodata
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/game_globals.c  # the ARM9 globals the matched code names
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/aliases.c       # sub_/func_ spelling forwarders
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/dsmem.c         # the DS address space: registry, regions, I/O, VRAM
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/host_vm.c       # reserve/commit for main RAM (the one OS-specific file)
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/os_tick.c       # OS_GetTick onto the frame loop, OS_InitTick
    # The OS layer the boot runs on (docs/boot.md, "Threads, interrupts and
    # the frame").  os_interrupt/os_alarm/os_system read the register file,
    # so they need sdk/dsmem.c; nothing in the library pulls them in until a
    # caller of theirs is linked.
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/plat_thread.c   # host mutex/condvar/thread, Win32 and POSIX
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/os_thread.c     # OSThread on host threads, one run token
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/os_interrupt.c  # IE/IF/IME, the IRQ handler, OS_Halt, OS_WaitIrq
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/os_alarm.c      # alarms on the tick
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/os_system.c     # console/boot type, MPU, ARM7 sync
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/os_argument.c   # the deliver argument
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/panic.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/port_reset.c    # bw_port_reset: dsmem, OS, PXI power-on and their joins
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/armdata.c       # the ARM9 image's data: copy, relocate, bind names
    ${BW_PXI_SOURCES}
)

# sdk/asm/: portable C for the matched SDK and runtime functions whose bodies
# are mwcc `asm` (sdk/asm/asmsdk.h).  One file per matched source; each
# names the source it replaces.  These need nothing beyond this library, so
# they are in it.
set(BW_ASMSDK_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/MI_CpuCopy8.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/MI_CpuFill8.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/MIi_CpuClear16.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/MIi_CpuClear32.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/MIi_CpuCopy16.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/MIi_CpuCopy32.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/func_020834C0.c   # MI_UncompressLZ8
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/MTX_Rot22_.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/s32_div_f.c       # _s32_div_f, _u32_div_f
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/func_02084EB8.c   # OS_GetLockID
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/func_02084F10.c   # OS_ReleaseLockID
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/OS_GetMacAddress.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/CP_SaveContext.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/CPi_RestoreContext.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/CTRDG_IsExistedAtInit.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/CTRDGi_IsTaskAvailable_02010054.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/func_02034F74.c
)
list(APPEND BW_SDK_SOURCES ${BW_ASMSDK_SOURCES})

# The rest call matched game code or name game globals, so they link with
# the boot closure (EIDOS_BOOT), in place of the matched asm sources they
# replace; tests/test_asmsdk_glue.c checks them against recording doubles.
set(BW_ASMSDK_BOOT_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/func_02084D98.c   # OS_UnLockCartridge
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/ArrayDeleteAt.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/NHTTPi_GetConnection.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/OS_GetInitArenaLo.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/OS_GetInitArenaHi.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/SNDi_IncAlarmId.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/func_02030BA4.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/func_0203129C.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/func_02034F84.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/func_0203516C.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/func_020351D0.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/func_02049238.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/func_0208C86C.c
    # TWL-only targets: panic if a NITRO boot ever reaches them.
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/func_020053E4.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/func_02005420.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/func_02005428.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/func_02005454.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/func_02005B6C.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/func_027312B4.c   # OSi_InitArenaEx_ltdmain
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/func_0273131C.c   # OSi_SearchTitleIdFromList
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/func_027313E8.c   # PMi_PreSleepForNdma
)

# One symbol per file, on purpose: a static-library member is only pulled in
# when nothing else defines its symbol, which is how tests/test_stats.c
# supplies its own record accessors without displacing anything else.
set(BW_STUB_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/reimpl_02045A98.c  # end-of-string code unit
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/reimpl_0204960C.c  # archive stream seek
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/stub_02017E1C.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/stub_02017E40.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/stub_02018598.c

    # The out-of-memory report.  EXACT in aletheia, and its closure reaches
    # the game's debug print (func_0209128C, open) and a module of report
    # strings; it runs one call before OS_Terminate, after an allocation has
    # already failed, so it stays a stub.
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/stub_02030B50.c
)

# --- the heap layer's floor ----------------------------------------------
#
# python tools/closure.py --seed-func sub_0202FC48
# python tools/closure.py --seed-func sub_0202FDC4
# python tools/closure.py --seed-func func_02030164
#
# Exactly one of the two lists enters the library, by EIDOS_HEAP_MATCHED.
#
# Two EXACT files in this chain are not compiled as written even under
# ILP32: func_02060888.c (the carve) and func_02060E30.c (the free) let
# func_02060764 write a two-word region starting at one scalar local and
# then read the second word through the *next* declared local (`end`, and
# `&end - 1`), which is right under mwccarm's frame layout and nowhere
# else.  stubs/reimpl_02060888.c and stubs/reimpl_02060E30.c are their
# bodies with the pair declared as an array; README.md, "The heap
# environment", has the finding.
set(BW_HEAP_MATCHED
    ${ALETHEIA_ROOT}/src/matched/arm9/sub_0202FC48.c    # the heap environment constructor
    ${ALETHEIA_ROOT}/src/matched/arm9/sub_0202FDC4.c    # carve a child heap from a parent
    ${ALETHEIA_ROOT}/src/matched/arm9/func_0202FED8.c   # bind it to a spare entry
    ${ALETHEIA_ROOT}/src/matched/arm9/func_02060C5C.c   # create an expanded heap over a region
    ${ALETHEIA_ROOT}/src/matched/arm9/func_02060814.c   # the expanded heap's head
    ${ALETHEIA_ROOT}/src/matched/arm9/func_020606D0.c   # the common heap head, root list
    ${ALETHEIA_ROOT}/src/matched/arm9/func_020606A8.c   # the list that contains a heap
    ${ALETHEIA_ROOT}/src/matched/arm9/unit_02060614.c   # list walk, find by address
    ${ALETHEIA_ROOT}/src/matched/arm9/func_02060468.c   # init a list
    ${ALETHEIA_ROOT}/src/matched/arm9/func_02060480.c   # first append
    ${ALETHEIA_ROOT}/src/matched/arm9/func_020604AC.c   # append
    ${ALETHEIA_ROOT}/src/matched/arm9/func_020607E8.c   # write a block header
    ${ALETHEIA_ROOT}/src/matched/arm9/func_02060764.c   # a block's extent
    ${ALETHEIA_ROOT}/src/matched/arm9/func_02060790.c   # unlink a block
    ${ALETHEIA_ROOT}/src/matched/arm9/func_020607B8.c   # link a block after another
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/reimpl_02060888.c  # the carve (see above)
    ${ALETHEIA_ROOT}/src/matched/arm9/func_02060A08.c   # the head-end walk
    ${ALETHEIA_ROOT}/src/matched/arm9/func_02060ABC.c   # the tail-end walk
    ${ALETHEIA_ROOT}/src/matched/arm9/func_02030164.c   # the raw free
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/reimpl_02060E30.c  # the expanded heap's free (see above)
    ${ALETHEIA_ROOT}/src/matched/arm9/func_02060B6C.c   # return a region, coalescing
    ${ALETHEIA_ROOT}/src/matched/arm9/func_02060E6C.c   # total free bytes
    ${ALETHEIA_ROOT}/src/matched/arm9/func_02060E94.c   # largest allocatable block
    ${ALETHEIA_ROOT}/src/matched/arm9/func_02091588.c   # abs, under the above
    ${ALETHEIA_ROOT}/src/matched/arm9/func_02060F90.c   # an allocated block's size
)
set(BW_HEAP_REIMPL
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/reimpl_02060A08.c  # the head-end walk
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/reimpl_02060ABC.c  # the tail-end walk
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/reimpl_02030164.c  # the raw free
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/stub_02060E6C.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/stub_02060E94.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/stub_02060F90.c
)
if(EIDOS_HEAP_MATCHED)
    list(APPEND BW_MATCHED_SOURCES ${BW_HEAP_MATCHED})
else()
    list(APPEND BW_STUB_SOURCES ${BW_HEAP_REIMPL})
endif()

# The nine files the pointer width decides between.  Exactly one of the two
# lists enters the library; the symbols are the same either way, so nothing
# above or below this line knows which it got.
set(BW_TCBL_MATCHED
    ${ALETHEIA_ROOT}/src/matched/arm9/sub_02030EF0.c    # row address
    ${ALETHEIA_ROOT}/src/matched/arm9/sub_02030F34.c    # free-list push
    ${ALETHEIA_ROOT}/src/matched/arm9/func_02030F04.c   # free-list pop
    ${ALETHEIA_ROOT}/src/matched/arm9/sub_02030FB4.c    # the work accessor
    ${ALETHEIA_ROOT}/src/matched/arm9/sub_02030F50.c    # reset a node
    ${ALETHEIA_ROOT}/src/matched/arm9/sub_02030F70.c    # unlink one and free it
    ${ALETHEIA_ROOT}/src/matched/arm9/sub_020311B8.c    # register a callback
    ${ALETHEIA_ROOT}/src/matched/arm9/sub_02031194.c    # destroy one node
    ${ALETHEIA_ROOT}/src/matched/arm9/sub_0203115C.c    # destroy every node
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

# --- fixed DS addresses ----------------------------------------------------
# Every matched source is compiled from a build-tree copy that
# tools/dsaddr.py has rewritten (docs/boot.md, "Fixed addresses").  From
# here on BW_MATCHED_SOURCES names the copies, and a per-source property on
# a matched file is set on eidos_dsaddr_path() of it.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/dsaddr.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/armdata.cmake)
set(BW_MATCHED_ORIGINALS ${BW_MATCHED_SOURCES})
eidos_dsaddr(BW_MATCHED_SOURCES ${BW_MATCHED_SOURCES})

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
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/g2d/hwcompose.c  # both screens from the hardware state
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
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/g3d/hw3d.c       # the 3D hardware: FIFO, ports, registers, swap

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

    # The ARM7's sound driver over engine/audio: SND commands from tag 7,
    # the shared work, the alarms (sdk/arm7/snd.h).  Not in BW_PXI_SOURCES:
    # the transport (sdk/arm7/snd.c) links without it, and test_pxi does.
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/arm7/snd_driver.c
)

add_library(eidos STATIC
    ${BW_MATCHED_SOURCES}
    ${BW_SDK_SOURCES}
    ${BW_STUB_SOURCES}
    ${BW_ENGINE_SOURCES}
)
# The source list the overlay table reads: every source of the library, as
# the generator expression sees it once all target_sources calls are in.
file(GENERATE OUTPUT ${BW_BUILD_SOURCE_LIST}
     CONTENT "$<JOIN:$<TARGET_PROPERTY:eidos,SOURCES>,\n>\n")
add_custom_command(
    OUTPUT ${BW_OVERLAY_TABLE}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/overlay_table.py
            --aletheia-root ${ALETHEIA_ROOT}
            --sources ${BW_BUILD_SOURCE_LIST}
            --out ${BW_OVERLAY_TABLE}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/overlay_table.py
            ${ALETHEIA_ROOT}/layout/layout.json
            ${ALETHEIA_ROOT}/layout/symbols_sdk.txt
            ${BW_OVERLAY_SHARDS}
            ${BW_BUILD_SOURCE_LIST}
    COMMENT "Generating the overlay table from ${ALETHEIA_ROOT}/layout"
    VERBATIM)

# Port code that has to know which heap floor it is sitting on (the
# initialiser, the forwarders, the tests) reads this.
# In the library the sound transport installs the engine/audio driver at
# ARM7 power-on (sdk/arm7/snd.c); test_pxi compiles the transport alone.
target_compile_definitions(eidos PRIVATE BW_ARM7_SND_DRIVER=1)

if(EIDOS_HEAP_MATCHED)
    target_compile_definitions(eidos PUBLIC EIDOS_HEAP_MATCHED=1)
else()
    target_compile_definitions(eidos PUBLIC EIDOS_HEAP_MATCHED=0)
endif()
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
#
# find_library is not the right question: it searches the host's default
# library directories whatever the target ABI is, so under -m32 on a 64-bit
# Linux host it answers for the wrong libm (or for none at all, and the link
# fails on pow/round/ceil).  Asking the compiler whether `-lm` links a call
# to pow() answers it for the target being built.
include(CheckLibraryExists)
check_library_exists(m pow "" BW_HAVE_LIBM)
if(BW_HAVE_LIBM)
    target_link_libraries(eidos PUBLIC m)
endif()

# sdk/plat_thread.c: pthreads off Windows (Threads::Threads is empty where
# the C library already has them, and on Win32).
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(eidos PUBLIC Threads::Threads)

# engine/audio is required to agree with its Python oracle *exactly* (see
# engine/audio/audio.h and tests/test_audio.c), and on 32-bit x86 the
# default floating point unit is x87, which evaluates double expressions in
# 80-bit registers and rounds to 64 bits only when a value is spilled.  That
# is not IEEE binary64 arithmetic, and it made every ILP32 render differ
# from the oracle's in the last ulp of a gain or a note-off time.  Asking
# for SSE2 instead gives the same 64-bit rounding at every step that every
# other target already has.  It is applied to the audio sources only: the
# rest of the engine is integer arithmetic and does not care.
#
# MSVC needs nothing: its 32-bit x86 compiler has defaulted to /arch:SSE2
# since Visual Studio 2012, so double arithmetic is already done in SSE2.
include(CheckCCompilerFlag)
set(BW_AUDIO_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/audio/sdat.c
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/audio/sbnk.c
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/audio/swav.c
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/audio/sseq.c
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/audio/mixer.c
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/audio/player.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/arm7/snd_driver.c
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
# Needed only by engine/platform/ and the eidos_view demo.  Where it is
# looked for, the opt-in configure-time fetch (EIDOS_SDL_FETCH) and the link
# probe are in cmake/sdl2.cmake.
#
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/sdl2.cmake)

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
# for the same function differ between translation units by design, and a
# local that is never read is sometimes what fixes the register allocation
# (src/matched/func_02060814.c's `feature`).
# Under MSVC the same three are spelled in cmake/msvc.cmake.
if(MSVC)
    set_source_files_properties(${BW_MATCHED_SOURCES} PROPERTIES
        COMPILE_OPTIONS "${EIDOS_MATCHED_COMPILE_OPTIONS}")
else()
    set_source_files_properties(${BW_MATCHED_SOURCES} PROPERTIES
        COMPILE_OPTIONS "-Wno-unused-parameter;-Wno-unused-variable;-Wno-missing-prototypes")
endif()

# Two matched files name one DS object under two different names: the DS
# build links both relocations to one address, a host linker would make two
# objects.  Each is compiled with its spelling defined to the other file's,
# which renames the reference at build time and leaves the file itself
# untouched (portability rule 1).  closure.py lists both pairs as `data`
# names at the same address.
#
#   0x02146B64  sRootList (func_020606D0.c)   g_02146B64 (func_020606A8.c)
#   0x02146954  g_EnvModule (everywhere)      g_02146954 (func_0202FED8.c)
if(EIDOS_HEAP_MATCHED)
    eidos_dsaddr_path(_src ${ALETHEIA_ROOT}/src/matched/arm9/func_020606A8.c)
    set_source_files_properties(${_src}
        PROPERTIES COMPILE_DEFINITIONS "g_02146B64=sRootList")
    eidos_dsaddr_path(_src ${ALETHEIA_ROOT}/src/matched/arm9/func_0202FED8.c)
    set_source_files_properties(${_src}
        PROPERTIES COMPILE_DEFINITIONS "g_02146954=g_EnvModule")
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
# The installer core: identify and extract the player's ROM (install/)
# ---------------------------------------------------------------------------
#
# Plain C99 and the standard library, no SDL and nothing of the game: the
# part of the first-run wizard that is the same on every platform.
add_library(eidos_install STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/install/sha1.c
    ${CMAKE_CURRENT_SOURCE_DIR}/install/blz.c
    ${CMAKE_CURRENT_SOURCE_DIR}/install/ndsrom.c
    ${CMAKE_CURRENT_SOURCE_DIR}/install/fsutil.c
)
target_include_directories(eidos_install PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/install
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk)
add_executable(eidos_extract ${CMAKE_CURRENT_SOURCE_DIR}/install/extract_main.c)
target_link_libraries(eidos_extract PRIVATE eidos_install)
if(WIN32)
    # CommandLineToArgvW, for UTF-8 paths on the command line.
    target_link_libraries(eidos_extract PRIVATE shell32)
endif()

# First run without a window (install/setup.c): where the data root lives
# on each platform, whether the one there is playable, the config file,
# --rom/--reset-data/--move-data; and the platform's own file dialog
# (install/picker.c, NSOpenPanel in install/picker_mac.m on macOS).  No SDL,
# so every configuration builds and tests it, MSVC included; the SDL
# wizard on top of it is with the platform layer below (eidos_wizard).
add_library(eidos_setup STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/install/setup.c
    ${CMAKE_CURRENT_SOURCE_DIR}/install/location.c
    ${CMAKE_CURRENT_SOURCE_DIR}/install/picker.c
)
target_link_libraries(eidos_setup PUBLIC eidos_install)
if(WIN32)
    # SHGetKnownFolderPath and CommandLineToArgvW (shell32), CoTaskMemFree
    # and the COM dialog (ole32), GetOpenFileNameW (comdlg32).
    target_link_libraries(eidos_setup PUBLIC shell32 ole32 comdlg32)
elseif(APPLE)
    enable_language(OBJC)
    target_sources(eidos_setup PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/install/picker_mac.m)
    target_link_libraries(eidos_setup PUBLIC "-framework Cocoa")
else()
    # dlopen of libdbus-1 for the desktop portal; no link-time D-Bus.
    target_link_libraries(eidos_setup PUBLIC ${CMAKE_DL_LIBS})
endif()

# A synthetic cartridge and aletheia's own extraction of it, generated from a
# fixed seed (no game data): tools/synth_rom.py.
set(BW_SYNTH_DIR ${CMAKE_CURRENT_BINARY_DIR}/synth)
add_custom_command(
    OUTPUT ${BW_SYNTH_DIR}/synth.nds ${BW_SYNTH_DIR}/oracle_files.txt
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/synth_rom.py
            --aletheia-root ${ALETHEIA_ROOT} --out ${BW_SYNTH_DIR}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/synth_rom.py
            ${ALETHEIA_ROOT}/tools/ndsrom.py
            ${ALETHEIA_ROOT}/tools/blz.py
    COMMENT "Generating the synthetic cartridge and its ndsrom.py extraction"
    VERBATIM
)
add_custom_target(bw_synth DEPENDS ${BW_SYNTH_DIR}/synth.nds)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

enable_testing()

# The two fixture tests compile the generated header in, so without game
# data there is nothing to build them against; they are still registered, as
# disabled, so the count stays honest.
foreach(t test_text test_stats)
    if(EIDOS_DATA_TESTS)
        add_executable(${t} ${CMAKE_CURRENT_SOURCE_DIR}/tests/${t}.c ${BW_FIXTURES})
        add_dependencies(${t} bw_fixtures)
        target_include_directories(${t} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
        target_link_libraries(${t} PRIVATE eidos)
        add_test(NAME ${t} COMMAND ${t} ${BW_DATA_ROOT_ABS})
    else()
        add_test(NAME ${t} COMMAND ${CMAKE_COMMAND} -E false)
        set_tests_properties(${t} PROPERTIES DISABLED TRUE)
    endif()
endforeach()

# The oracle tests read their oracle at run time rather than compiling it
# in, so they are built in every configuration (a compile error in one is
# still caught without game data) and only their oracle and their ctest
# entry depend on the data being there.
function(eidos_data_test name)
    if(EIDOS_DATA_TESTS)
        add_dependencies(${name} ${ARGV1})
    else()
        set_tests_properties(${name} PROPERTIES DISABLED TRUE)
    endif()
endfunction()

# The 2D engine test needs the oracle rather than the fixture header, and
# takes the oracle directory as its second argument.
add_executable(test_g2d ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_g2d.c)
target_include_directories(test_g2d PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(test_g2d PRIVATE eidos)
add_test(NAME test_g2d COMMAND test_g2d ${BW_DATA_ROOT_ABS} ${BW_G2D_ORACLE_DIR})
eidos_data_test(test_g2d bw_g2d_oracle)

# The BG test reads its screen list out of the oracle's own manifest, so the
# set of screens is changed in one place (BW_BG_SCREENS above).
add_executable(test_bg ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_bg.c)
target_include_directories(test_bg PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(test_bg PRIVATE eidos)
add_test(NAME test_bg COMMAND test_bg ${BW_DATA_ROOT_ABS} ${BW_BG_ORACLE_DIR})
eidos_data_test(test_bg bw_bg_oracle)

# The 3D engine test has two halves.  The geometry half reads the OBJ and
# the manifest the oracle wrote and compares every vertex and every
# texture coordinate of two real map models against this engine's own
# display-list decode.  The raster half renders one of them from a fixed
# camera into a BMP in the build directory and asserts non-trivial
# content, plus a set of synthetic checks whose expected pixels are
# computed on paper.  There is no DS frame to compare a picture against
# and the test does not pretend otherwise.
add_executable(test_g3d ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_g3d.c)
target_include_directories(test_g3d PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(test_g3d PRIVATE eidos)
add_test(NAME test_g3d COMMAND test_g3d ${BW_DATA_ROOT_ABS} ${BW_G3D_ORACLE_DIR}
                               ${CMAKE_CURRENT_BINARY_DIR}/g3d_house.bmp)
eidos_data_test(test_g3d bw_g3d_oracle)

# The audio test reads its sequence list out of the audio oracle's own
# manifest, so the set of sequences is changed in one place
# (BW_AUDIO_SEQS above), and writes its own note dump next to the oracle's
# so a failure leaves both files on disk to diff by hand.
add_executable(test_audio ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_audio.c)
target_include_directories(test_audio PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(test_audio PRIVATE eidos)
add_test(NAME test_audio COMMAND test_audio ${BW_DATA_ROOT_ABS} ${BW_AUDIO_ORACLE_DIR})
eidos_data_test(test_audio bw_audio_oracle)

# The ARM7 sound driver through the PXI wire (sdk/arm7/snd.h): synthetic
# command lists against engine/audio driven directly, needing no data; and
# the same with real sequences copied into main RAM from the SDAT, which
# needs the data root.
add_executable(test_snd ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_snd.c)
target_include_directories(test_snd PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(test_snd PRIVATE eidos)
add_test(NAME test_snd COMMAND test_snd)
add_test(NAME test_snd_data COMMAND test_snd ${BW_DATA_ROOT_ABS})
if(NOT EIDOS_DATA_TESTS)
    set_tests_properties(test_snd_data PROPERTIES DISABLED TRUE)
endif()

# The game's own SND library (aletheia's matched snd_*.o functions) driving
# the same driver.  4-byte pointers only: the matched files' views of
# snd_command.o's .bss agree with each other only there (the test's header
# says how).  Each file defines its address spelling and calls the SDK
# spelling of its neighbours, so the SDK names are defined to the address
# names on the command line, which leaves the files untouched.
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(BW_SND_TEST_MATCHED
        arm9_sdk/PXI_SendWordByFifo.c
        arm9_sdk/PXI_SetFifoRecvCallback.c
        arm9_sdk/func_020885EC.c            # PXI_IsCallbackReady
        arm9_sdk/func_020898A8.c            # SND_CommandInit
        arm9_sdk/func_02089978.c            # SND_RecvCommandReply
        arm9_sdk/func_02089A78.c            # SND_AllocCommand
        arm9_sdk/func_02089B00.c            # SND_PushCommand
        arm9_sdk/SND_FlushCommand.c
        arm9_sdk/func_02089CD4.c            # SND_WaitForCommandProc
        arm9_sdk/func_02089D40.c            # SND_GetCurrentCommandTag
        arm9_sdk/func_02089D6C.c            # SND_IsFinishedCommandTag
        arm9_sdk/func_02089DBC.c            # SND_CountFreeCommand
        arm9_sdk/func_02089DF8.c            # SND_CountReservedCommand
        arm9_sdk/func_02089E34.c            # SND_CountWaitingCommand
        arm9_sdk/func_02089E50.c            # PxiFifoCallback
        arm9_sdk/func_02089E74.c            # InitPXI
        arm9_sdk/func_02089ED8.c            # RequestCommandProc
        arm9_sdk/func_02089F00.c            # AllocCommand
        arm9_sdk/func_02089F48.c            # IsCommandAvailable
        arm9_sdk/func_02089F88.c            # SND_AlarmInit
        arm9_sdk/func_02089FE0.c            # SNDi_SetAlarmHandler
        arm9_sdk/func_0208A010.c            # SNDi_CallAlarmHandler
        arm9_sdk/func_0208A07C.c            # SND_GetChannelStatus
        arm9_sdk/func_0208A0A4.c            # SND_ReadTrackInfo
        arm9_sdk/func_0208A1AC.c            # SNDi_GetFinishedCommandTag
        arm9_sdk/func_0208A1D0.c            # SNDi_InitSharedWork
        arm9_sdk/func_0208A2BC.c            # SND_AssignWaveArc
        arm9_sdk/func_02089804.c            # PushCommand_impl
        arm9_sdk/func_02089350.c            # SND_PrepareSeq
        arm9_sdk/func_02089330.c            # SND_StopSeq
        arm9_sdk/func_020897B4.c            # SNDi_SetPlayerParam
        arm9_sdk/func_0208970C.c            # SND_SetMasterVolume
        arm9_sdk/func_02089540.c            # SND_SetupAlarm
        arm9_sdk/func_020894B4.c            # SND_StartTimer
        arm9_sdk/func_02089794.c            # SND_ReadDriverInfo
    )
    set(_bw_snd_matched)
    foreach(_rel ${BW_SND_TEST_MATCHED})
        list(APPEND _bw_snd_matched ${ALETHEIA_ROOT}/src/matched/${_rel})
    endforeach()
    # The library's own rewrite of these, if a later wave adds them to it,
    # would collide; until then the test owns the copies.
    eidos_dsaddr(_bw_snd_matched ${_bw_snd_matched})
    if(NOT MSVC)
        set_source_files_properties(${_bw_snd_matched} PROPERTIES
            COMPILE_OPTIONS "-Wno-unused-parameter;-Wno-unused-variable;-Wno-missing-prototypes")
    else()
        set_source_files_properties(${_bw_snd_matched} PROPERTIES
            COMPILE_OPTIONS "${EIDOS_MATCHED_COMPILE_OPTIONS}")
    endif()
    set_source_files_properties(${_bw_snd_matched} PROPERTIES COMPILE_DEFINITIONS
        "InitPXI=func_02089E74;SNDi_InitSharedWork=func_0208A1D0;SND_AllocCommand=func_02089A78;SND_PushCommand=func_02089B00;SND_RecvCommandReply=func_02089978;SNDi_GetFinishedCommandTag=func_0208A1AC;IsCommandAvailable=func_02089F48;AllocCommand=func_02089F00;SND_CountWaitingCommand=func_02089E34;SND_CountFreeCommand=func_02089DBC;SND_CountReservedCommand=func_02089DF8;RequestCommandProc=func_02089ED8;SND_IsFinishedCommandTag=func_02089D6C;PxiFifoCallback=func_02089E50;SNDi_CallAlarmHandler=func_0208A010;PushCommand_impl=func_02089804;SNDi_SetAlarmHandler=func_02089FE0;PXI_IsCallbackReady=func_020885EC;sCommandBss=sFreeList")
    add_executable(test_snd_matched
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_snd_matched.c
        ${_bw_snd_matched})
    target_include_directories(test_snd_matched PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(test_snd_matched PRIVATE eidos)
    add_test(NAME test_snd_matched COMMAND test_snd_matched)
    set_tests_properties(test_snd_matched PROPERTIES TIMEOUT 60)
endif()

# The synthetic engine tests need neither game data nor an oracle: every
# expected pixel in them is computed on paper from the hardware rule under
# test.  They are the counterweight to the oracle comparisons, which can
# only ever show that two implementations agree.
foreach(t test_affine test_bg_affine test_window test_frame test_tcbl test_heap test_vfs test_arcid test_compose3d test_dsmem)
    add_executable(${t} ${CMAKE_CURRENT_SOURCE_DIR}/tests/${t}.c)
    target_include_directories(${t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(${t} PRIVATE eidos)
    add_test(NAME ${t} COMMAND ${t})
endforeach()

# The registry's overlap check panics, and a panic aborts the process, so it
# is exercised in a run of its own that passes only if the panic happened.
add_test(NAME test_dsmem_overlap COMMAND ${CMAKE_COMMAND}
    -DEXE=$<TARGET_FILE:test_dsmem> -DARG=--overlap
    "-DPATTERN=PANIC in bw_ds_register at 0x02300010"
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/expect_panic.cmake)

# The hardware-state compositor is checked two ways: synthetic register and
# VRAM states whose expected pixels are computed on paper, and, with game
# data, the title screen and a rig pose loaded into VRAM, palette and OAM
# the way the SDK would and required to come out pixel-identical to the
# asset path.  The states are written into the real address space
# (sdk/dsmem.c); without game data the second half reports itself skipped.
add_executable(test_hwcompose ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_hwcompose.c)
target_include_directories(test_hwcompose PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(test_hwcompose PRIVATE eidos)
add_test(NAME test_hwcompose COMMAND test_hwcompose ${BW_DATA_ROOT_ABS})

# The 3D hardware interface (engine/g3d/hw3d.c) over the real sdk/dsmem.c:
# one display list through the FIFO, the ports and the SDK's sender shims,
# pixel-identical to the interpreter's own rendering, plus GXSTAT, the test
# results and the swap.  Synthetic, no game data.
add_executable(test_g3dhw ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_g3dhw.c)
target_include_directories(test_g3dhw PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(test_g3dhw PRIVATE eidos)
add_test(NAME test_g3dhw COMMAND test_g3dhw)

# The FS shim, the overlay registry and member-level mod overrides:
# synthetic checks always, and the real-data half when the data root's
# parent holds the extract's tables.
foreach(t test_fs test_overlay test_vfs_members)
    add_executable(${t} ${CMAKE_CURRENT_SOURCE_DIR}/tests/${t}.c)
    target_include_directories(${t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(${t} PRIVATE eidos)
    add_test(NAME ${t} COMMAND ${t} ${BW_DATA_ROOT_ABS})
endforeach()
# Loading an overlay whose code is not in the build must stop at the load and
# say which overlay and which function.  The process aborts, so the check
# runs through tests/expect_panic.cmake (see its header for why).
add_test(NAME test_overlay_refuses
    COMMAND ${CMAKE_COMMAND} -DCMD=$<TARGET_FILE:test_overlay> -DARG1=--load -DARG2=9
            "-DEXPECT=PANIC in FS_LoadOverlay at 0x02155160: overlay 9 [(]0x02155160[.][.]0x021A8260[)] is not in this build: first missing function [A-Za-z_0-9]+ at 0x021"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/expect_panic.cmake)

# --- PXI and the ARM7 -------------------------------------------------------
#
# test_pxi drives the FIFO model and every ARM7 responder from the game's own
# matched ARM9 SDK code, on the port's real address space and OS layer
# (bw_port_reset), so it compiles a set of aletheia's matched sources itself
# and links the eidos library for everything else.  The sources go through
# the same tools/dsaddr.py rewrite as the library's (cmake/dsaddr.cmake); a
# source the library already compiles is left to the library, so no file
# gets two rewrite rules.  tests/pxi_doubles.c holds only the ARM9 data
# objects and spelling aliases those files name that the library does not
# define yet.
set(BW_PXI_TEST_MATCHED
    arm9/func_02088498.c                # PXI_Init
    arm9_sdk/PXI_SendWordByFifo.c
    arm9_sdk/PXI_SetFifoRecvCallback.c
    arm9_sdk/func_020885EC.c            # PXI_IsCallbackReady
    arm9_sdk/OS_SetIrqFunction.c
    arm9_sdk/OS_EnableIrqMask.c
    # OS_ResetRequestIrqMask and OSi_SyncWithOtherProc (func_02084AB0) are
    # the OS layer's shims (sdk/os_interrupt.c, sdk/os_system.c), as in the
    # boot, so their matched files are not compiled here.
    arm9_sdk/func_02087B78.c            # the OS's PXI init
    arm9_sdk/OSi_SendToPxi.c
    arm9_sdk/func_02084A38.c            # OSi_CommonCallback
    arm9_sdk/func_02084B90.c            # OS_InitLock
    arm9_sdk/PMi_SendPxiData.c
    arm9_sdk/PMi_TryToSendPxiData.c
    arm9_sdk/PM_SendUtilityCommandAsync.c
    arm9_sdk/PMi_CallCallbackAndUnlock.c
    arm9_sdk/func_0208B840.c            # PMi_DummyCallback
    arm9_sdk/func_02088788.c            # RTC_Init
    arm9_sdk/RtcCommonCallback.c
    arm9_sdk/RtcBCD2HEX.c
    arm9_sdk/RTC_GetDayOfWeek.c
    arm9_sdk/RTCi_WriteRawStatus2Async.c
    arm9_sdk/func_02088F2C.c            # RtcSendPxiCommand
    arm9_sdk/func_02088944.c            # RTC_GetDateTimeAsync
    arm9_sdk/func_020911A0.c            # CTRDG_DummyInit
    arm9_sdk/func_02091150.c            # CTRDGi_SendtoPxi
    arm9_sdk/func_020911D4.c            # CTRDGi_DummyCallback
    arm9_sdk/func_020763BC.c            # CARDi_OnFifoRecv
    arm9_sdk/func_02089ED8.c            # SND RequestCommandProc
)
# pm.o's init and callback view its .bss through 4-byte-pointer offsets
# (and under a second name, s_02153000), so they join only on ILP32.
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    list(APPEND BW_PXI_TEST_MATCHED
        arm9_sdk/func_0208B8B0.c        # PM_Init
        arm9_sdk/func_0208B950.c        # PMi_CommonCallback
        arm9_sdk/PMi_WaitBusy.c
        arm9_sdk/PM_SendUtilityCommand.c
        arm9_sdk/PM_GetAmp.c)
    set(_bw_pxi_pm_matched 1)
else()
    set(_bw_pxi_pm_matched 0)
endif()
set(_bw_pxi_test_originals)
foreach(_rel ${BW_PXI_TEST_MATCHED})
    set(_src ${ALETHEIA_ROOT}/src/matched/${_rel})
    if(NOT _src IN_LIST BW_MATCHED_ORIGINALS)
        list(APPEND _bw_pxi_test_originals ${_src})
    endif()
endforeach()
eidos_dsaddr(_bw_pxi_test_sources ${_bw_pxi_test_originals})
if(NOT MSVC)
    set_source_files_properties(${_bw_pxi_test_sources} PROPERTIES
        COMPILE_OPTIONS "-Wno-unused-parameter;-Wno-unused-variable;-Wno-missing-prototypes;-Wno-unknown-pragmas")
else()
    set_source_files_properties(${_bw_pxi_test_sources} PROPERTIES
        COMPILE_OPTIONS "${EIDOS_MATCHED_COMPILE_OPTIONS};${EIDOS_MATCHED_PRAGMA_OPTION}")
endif()
add_executable(test_pxi
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pxi.c
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/pxi_doubles.c
    ${_bw_pxi_test_sources})
target_include_directories(test_pxi PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_compile_definitions(test_pxi PRIVATE BW_TEST_PM_MATCHED=${_bw_pxi_pm_matched})
target_link_libraries(test_pxi PRIVATE eidos)
if(_bw_pxi_pm_matched)
    # One object, two names (see "Two matched files name one DS object").
    eidos_dsaddr_path(_a ${ALETHEIA_ROOT}/src/matched/arm9_sdk/func_0208B8B0.c)
    eidos_dsaddr_path(_b ${ALETHEIA_ROOT}/src/matched/arm9_sdk/func_0208B950.c)
    set_source_files_properties(${_a} ${_b}
        PROPERTIES COMPILE_DEFINITIONS "s_02153000=g_02153000")
endif()
add_test(NAME test_pxi COMMAND test_pxi)
set_tests_properties(test_pxi PROPERTIES TIMEOUT 30)

# The OS layer's tests, on the real address space (sdk/dsmem.c) and the
# port's power-on (bw_port_reset).  test_os_irq also compiles aletheia's
# matched OS_GetIrqFunction.c as written, the table reader beside the
# library's OS_SetIrqFunction.c that the shim's dispatcher has to agree
# with.
foreach(t test_os_thread test_os_irq test_os_alarm)
    add_executable(${t} ${CMAKE_CURRENT_SOURCE_DIR}/tests/${t}.c)
    target_include_directories(${t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(${t} PRIVATE eidos)
    add_test(NAME ${t} COMMAND ${t})
    set_tests_properties(${t} PROPERTIES TIMEOUT 60)
endforeach()
target_sources(test_os_irq PRIVATE
    ${ALETHEIA_ROOT}/src/matched/arm9_sdk/OS_GetIrqFunction.c)

# sdk/asm/: the asm functions' C.  test_asmsdk checks the half in the
# library on the real address space (and, with game data, MI_UncompressLZ8
# on real archive members); test_asmsdk_glue links the boot half alone
# against recording doubles of the game code it calls.
add_executable(test_asmsdk ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_asmsdk.c)
target_include_directories(test_asmsdk PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(test_asmsdk PRIVATE eidos)
add_test(NAME test_asmsdk COMMAND test_asmsdk ${BW_DATA_ROOT_ABS})
add_executable(test_asmsdk_glue
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_asmsdk_glue.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/func_02034F74.c
    ${BW_ASMSDK_BOOT_SOURCES})
target_include_directories(test_asmsdk_glue PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk ${CMAKE_CURRENT_SOURCE_DIR}/tests)
add_test(NAME test_asmsdk_glue COMMAND test_asmsdk_glue)

add_executable(test_extract ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_extract.c)
target_include_directories(test_extract PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(test_extract PRIVATE eidos_install)
add_dependencies(test_extract bw_synth)
add_test(NAME test_extract COMMAND test_extract ${BW_SYNTH_DIR})

# The data root's location, check, config file, reset and move, and the
# headless first run, on synthetic trees and the synthetic cartridge.  The
# test sets its own eidos home, so it never reads or writes the real one;
# BW_DATA_ROOT is cleared because it outranks everything the test sets up.
# Not named test_setup: 32-bit Windows treats an executable with "setup"
# (or "install", "update") in its name as an installer and demands
# elevation before it will start it (UAC installer detection).
add_executable(test_dataroot ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_dataroot.c)
target_include_directories(test_dataroot PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(test_dataroot PRIVATE eidos_setup)
add_dependencies(test_dataroot bw_synth)
add_test(NAME test_dataroot
         COMMAND test_dataroot ${BW_SYNTH_DIR} ${CMAKE_CURRENT_BINARY_DIR}/setup_work)
set_tests_properties(test_dataroot PROPERTIES ENVIRONMENT "BW_DATA_ROOT=;EIDOS_HOME=")

# The native file dialog opened for real and answered by a helper thread
# (Windows).  It shows a window on the desktop, so it runs only with
# EIDOS_TEST_DIALOGS=1 and is skipped otherwise.
add_executable(test_picker ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_picker.c)
target_include_directories(test_picker PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(test_picker PRIVATE eidos_setup)
add_test(NAME test_picker
         COMMAND test_picker ${CMAKE_CURRENT_BINARY_DIR}/picker_work)
set_tests_properties(test_picker PROPERTIES SKIP_REGULAR_EXPRESSION "SKIPPED:" TIMEOUT 120)

# The extractor on the player's real ROM, named by BW_ROM at test time,
# checked file for file against the extract/ tree that holds BW_DATA_ROOT
# (aletheia's own ndsrom.py + delink.py output).  Skipped, not failed,
# without BW_ROM or without that tree; see tests/extract_rom.cmake.
get_filename_component(BW_EXTRACT_ROOT "${BW_DATA_ROOT_ABS}" DIRECTORY)
add_test(NAME test_extract_rom
         COMMAND ${CMAKE_COMMAND} -DEXTRACT=$<TARGET_FILE:eidos_extract>
                 -DREFERENCE=${BW_EXTRACT_ROOT}
                 -DWORK=${CMAKE_CURRENT_BINARY_DIR}/rom_extract
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/extract_rom.cmake)
set_tests_properties(test_extract_rom PROPERTIES
    SKIP_REGULAR_EXPRESSION "SKIPPED:" TIMEOUT 900)

# --- the ARM9 image's data (sdk/armdata.c, tools/armdata.py) ----------------
#
# test_armdata_gen: the generator against a synthetic aletheia tree written
# by the test itself (no game data, no real metadata).
add_test(NAME test_armdata_gen
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_armdata.py
            --tool=${CMAKE_CURRENT_SOURCE_DIR}/tools/armdata.py)

# test_armdata: the runtime against a synthetic image and hand-written
# tables (copy, zero, bind, relocate, the mismatch and LP64 refusals).
add_executable(test_armdata ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_armdata.c)
target_include_directories(test_armdata PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(test_armdata PRIVATE eidos)
add_test(NAME test_armdata COMMAND test_armdata)
add_test(NAME test_armdata_trap COMMAND ${CMAKE_COMMAND}
    -DCMD=$<TARGET_FILE:test_armdata> -DARG1=--trap
    "-DEXPECT=PANIC in armdata at 0x02012344: called through a pointer"
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/expect_panic.cmake)

# test_armdata_real: tables generated from aletheia's real metadata for a
# handful of matched sources, four of which are compiled here with the
# forced-include header, then loaded from the player's own arm9_dec.bin.
# The metadata is always there, so it always builds; without the image the
# run reports itself skipped.
set(BW_ARMDATA_REAL_MATCHED
    ${ALETHEIA_ROOT}/src/matched/arm9/func_0202F9F4.c   # the three entries of
    ${ALETHEIA_ROOT}/src/matched/arm9/func_0202F9F8.c   # the proc table at
    ${ALETHEIA_ROOT}/src/matched/arm9/func_0202FAB0.c   # 0x020A0480
    ${ALETHEIA_ROOT}/src/matched/arm9/func_02005440.c   # stores to g_020A9E80
)
eidos_dsaddr(BW_ARMDATA_REAL_COPIES ${BW_ARMDATA_REAL_MATCHED})
set(BW_ARMDATA_REAL_NAMED                               # names only, not compiled
    ${ALETHEIA_ROOT}/src/matched/arm9/sub_0200545C.c     # d_020A6C58
    ${ALETHEIA_ROOT}/src/matched/arm9/unit_0200560C.c    # sub_02005610_Sys  } one address
    ${ALETHEIA_ROOT}/src/matched/arm9/unit_020056A0.c    # sub_020056A0_Sys  }
    ${ALETHEIA_ROOT}/src/matched/arm9_sdk/func_0208459C.c   # SDK_AUTOLOAD_DTCM_START
    ${ALETHEIA_ROOT}/src/matched/arm9_sdk/func_02085264.c   # SDK_IRQ_STACKSIZE, port threads
    ${ALETHEIA_ROOT}/src/matched/arm9_sdk/func_02030524.c   # d_02146954 = g_EnvModule
)
string(REPLACE ";" "
" _armdata_real "${BW_ARMDATA_REAL_COPIES};${BW_ARMDATA_REAL_NAMED}")
file(GENERATE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/test_armdata_real_sources.txt
     CONTENT "$<JOIN:$<TARGET_PROPERTY:eidos,SOURCES>,
>
${_armdata_real}
")
eidos_armdata(test_armdata_real
    SOURCES_LIST ${CMAKE_CURRENT_BINARY_DIR}/test_armdata_real_sources.txt
    MATCHED ${BW_ARMDATA_REAL_COPIES})
if(NOT MSVC)
    set_property(SOURCE ${BW_ARMDATA_REAL_COPIES} APPEND PROPERTY COMPILE_OPTIONS
        "-Wno-unused-parameter;-Wno-unused-variable;-Wno-missing-prototypes")
else()
    set_property(SOURCE ${BW_ARMDATA_REAL_COPIES} APPEND PROPERTY COMPILE_OPTIONS
        "${EIDOS_MATCHED_COMPILE_OPTIONS}")
endif()
add_executable(test_armdata_real
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_armdata_real.c
    ${BW_ARMDATA_REAL_COPIES}
    ${test_armdata_real_C})
target_include_directories(test_armdata_real PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(test_armdata_real PRIVATE eidos)
add_test(NAME test_armdata_real COMMAND test_armdata_real ${BW_DATA_ROOT_ABS})

# tools/dsaddr.py: every cast form, the prelude against sdk/dsmem.h on this
# compiler, every matched source in this build parsed after its rewrite,
# and a dry run over the whole of aletheia's src/matched (idempotent, no
# line moves).  The source list is the one this configuration compiles.
string(REPLACE ";" "\n" _dsaddr_list "${BW_MATCHED_ORIGINALS}")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/dsaddr_sources.txt "${_dsaddr_list}\n")
if(MSVC)
    set(_dsaddr_cc_id MSVC)
else()
    set(_dsaddr_cc_id GNU)
endif()
add_test(NAME test_dsaddr
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_dsaddr.py
            --aletheia-root=${ALETHEIA_ROOT}
            --sources=${CMAKE_CURRENT_BINARY_DIR}/dsaddr_sources.txt
            --cc=${CMAKE_C_COMPILER}
            --cc-id=${_dsaddr_cc_id}
            "--cflags=${CMAKE_C_FLAGS}")

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
    # The first-run setup window (engine/platform/wizard.c) and its bitmap
    # font, over install/setup.h.  eidos_view finds its data the way the
    # game will, through eidos_firstrun: --data-root, BW_DATA_ROOT, the
    # saved setting, the platform default, else this window.
    add_library(eidos_wizard STATIC
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/platform/wizard.c
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/platform/font.c
    )
    target_include_directories(eidos_wizard PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/platform)
    target_link_libraries(eidos_wizard PUBLIC eidos_setup eidos_sdl2)

    add_executable(eidos_view ${CMAKE_CURRENT_SOURCE_DIR}/demo/eidos_view.c)
    target_link_libraries(eidos_view PRIVATE eidos_platform eidos_wizard)

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
    if(NOT EIDOS_DATA_TESTS)
        set_tests_properties(test_view PROPERTIES DISABLED TRUE)
    endif()

    # The setup window under the dummy video driver, driven by scripted
    # events: an archive dropped and refused, a mouse click on "Choose
    # another file", the synthetic cartridge refused as not the original
    # game, then installed with the test-only override, Start; Escape and
    # closing the window.  With BW_ROM set it also installs the real ROM
    # through a drop, with no override.
    add_executable(test_wizard ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_wizard.c)
    target_include_directories(test_wizard PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(test_wizard PRIVATE eidos_wizard)
    add_dependencies(test_wizard bw_synth eidos_view)
    add_test(NAME test_wizard
             COMMAND test_wizard ${BW_SYNTH_DIR} ${CMAKE_CURRENT_BINARY_DIR}/wizard_work)
    set_tests_properties(test_wizard PROPERTIES TIMEOUT 600
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;BW_DATA_ROOT=;EIDOS_HOME=")

    # The whole first run on the player's real ROM, without a window:
    # eidos_view --rom makes the data in a scratch eidos home and draws
    # from it; the next start finds it through the saved setting;
    # --move-data and --reset-data.  Skipped without BW_ROM.
    add_test(NAME test_firstrun_rom
             COMMAND ${CMAKE_COMMAND} -DVIEW=$<TARGET_FILE:eidos_view>
                     -DWORK=${CMAKE_CURRENT_BINARY_DIR}/firstrun_rom
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/firstrun_rom.cmake)
    set_tests_properties(test_firstrun_rom PROPERTIES
        SKIP_REGULAR_EXPRESSION "SKIPPED:" TIMEOUT 900
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;BW_DATA_ROOT=")
endif()

# ---------------------------------------------------------------------------
# eidos_boot -- the game's own ARM9 boot (docs/boot.md)
# ---------------------------------------------------------------------------
#
# demo/eidos_boot.c runs NitroMain (stubs/reimpl_02005124.c) under the frame
# loop, headless.  It is always built; what it links depends on EIDOS_BOOT:
#
#   OFF (default)  only the parts that link today.  The executable reports
#                  that the boot is not linked and exits 0.
#   ON             NitroMain, the one unmatched callee it needs
#                  (stubs/reimpl_02047990.c), and the closure of NitroMain's
#                  matched callees as tools/closure.py computes it.  This
#                  needs the boot lanes' shims (sdk/dsmem.c, the OS thread and
#                  interrupt layer, the ARM7 replies, the overlay loader) and
#                  does not link until they have landed.
#
# The closure is computed once at configure time into the build tree
# (eidos_boot_closure.txt; delete it to recompute) and compiled into its own
# static library, minus the sources the eidos library already carries, so
# nothing is compiled twice.  The seeds are NitroMain's direct matched
# callees, func_02005328.c (the system initialiser) among them.
option(EIDOS_BOOT
       "Link the game's own boot (NitroMain and its closure) into eidos_boot" OFF)

add_executable(eidos_boot ${CMAKE_CURRENT_SOURCE_DIR}/demo/eidos_boot.c)
target_link_libraries(eidos_boot PRIVATE eidos)
target_compile_definitions(eidos_boot PRIVATE
    EIDOS_DEFAULT_DATA_ROOT="${BW_DATA_ROOT_ABS}")

if(EIDOS_BOOT)
    set(EIDOS_BOOT_SEEDS
        arm9/func_02005328.c      # system init
        arm9/sub_0200545C.c       # gfl_use.c: heaps, system record
        arm9/unit_020051D8.c      # func_020051D8, func_02005204 (hblank)
        arm9/func_0200520C.c      # vblank handler
        arm9/func_0200523C.c      # boot sequence, overlays
        arm9/func_020053EC.c
        arm9/func_020055F8.c
        arm9/func_020052EC.c
        arm9/unit_0200560C.c      # sub_0200560C
        arm9/func_02005230.c
        arm9_sdk/OS_SetIrqFunction.c
        arm9_sdk/OS_EnableIrqMask.c
        arm9_sdk/func_0207D3AC.c  # GX_HBlankIntr
        arm9_sdk/func_0207D3E4.c  # GX_VBlankIntr
        arm9_sdk/func_0208574C.c  # OS_IsThreadTerminated
        arm9_sdk/func_02085760.c  # OS_SleepThreadDirect
        arm9_sdk/func_02085868.c  # OS_WakeupThreadDirect
        arm9_sdk/func_0208459C.c  # OS_WaitIrq
    )
    set(_boot_list ${CMAKE_CURRENT_BINARY_DIR}/eidos_boot_closure.txt)
    if(NOT EXISTS ${_boot_list})
        set(_seed_args)
        foreach(s ${EIDOS_BOOT_SEEDS})
            list(APPEND _seed_args --seed src/matched/${s})
        endforeach()
        message(STATUS "EIDOS_BOOT: computing NitroMain's closure (tools/closure.py)")
        execute_process(
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/closure.py
                    --aletheia-root ${ALETHEIA_ROOT} ${_seed_args} --cmake
            OUTPUT_FILE ${_boot_list}
            RESULT_VARIABLE _boot_rc)
        if(NOT _boot_rc EQUAL 0)
            file(REMOVE ${_boot_list})
            message(FATAL_ERROR "EIDOS_BOOT: tools/closure.py failed (${_boot_rc})")
        endif()
    endif()
    file(STRINGS ${_boot_list} _boot_lines)
    set(EIDOS_BOOT_MATCHED)
    foreach(line ${_boot_lines})
        string(STRIP "${line}" line)
        string(REPLACE "\${BW_ROOT}" "${ALETHEIA_ROOT}" line "${line}")
        if(line)
            list(APPEND EIDOS_BOOT_MATCHED "${line}")
        endif()
    endforeach()
    if(BW_MATCHED_SOURCES)
        list(REMOVE_ITEM EIDOS_BOOT_MATCHED ${BW_MATCHED_SOURCES})
    endif()
    list(LENGTH EIDOS_BOOT_MATCHED _boot_n)
    message(STATUS "EIDOS_BOOT: ON, ${_boot_n} matched sources beyond the eidos library")

    add_library(eidos_boot_game STATIC
        ${EIDOS_BOOT_MATCHED}
        ${CMAKE_CURRENT_SOURCE_DIR}/stubs/reimpl_02005124.c
        ${CMAKE_CURRENT_SOURCE_DIR}/stubs/reimpl_02047990.c
    )
    target_link_libraries(eidos_boot_game PUBLIC eidos)
    if(NOT MSVC)
        set_source_files_properties(${EIDOS_BOOT_MATCHED} PROPERTIES
            COMPILE_OPTIONS "-Wno-unused-parameter;-Wno-unused-variable;-Wno-missing-prototypes")
    else()
        set_source_files_properties(${EIDOS_BOOT_MATCHED} PROPERTIES
            COMPILE_OPTIONS "${EIDOS_MATCHED_COMPILE_OPTIONS}")
    endif()
    target_link_libraries(eidos_boot PRIVATE eidos_boot_game)
    target_compile_definitions(eidos_boot PRIVATE EIDOS_BOOT=1)

    # The ARM9 image's data for everything this build compiles
    # (docs/armdata.md): the names the boot's matched sources use, bound by
    # the forced-include header, and the image's pointers relocated.  The
    # program installs it with bw_data_install(&bw_arm9_data, 0) before its
    # first bw_ds_reset.
    file(GENERATE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/eidos_boot_sources.txt
         CONTENT "$<JOIN:$<TARGET_PROPERTY:eidos,SOURCES>,
>
$<JOIN:$<TARGET_PROPERTY:eidos_boot_game,SOURCES>,
>
")
    eidos_armdata(bw_arm9_data
        SOURCES_LIST ${CMAKE_CURRENT_BINARY_DIR}/eidos_boot_sources.txt
        MATCHED ${EIDOS_BOOT_MATCHED})
    target_sources(eidos_boot_game PRIVATE ${bw_arm9_data_C})
endif()

# --- No modal dialogs under MSVC ---------------------------------------------
#
# The Microsoft runtime answers abort(), a failed assert() and a debug-CRT
# report with a window that waits for a click, which stalls an unattended
# test run (several tests abort on purpose: tests/expect_panic.cmake).
# sdk/plat_nodialog.c sends every such report to stderr from a CRT
# initialiser; it goes into each executable as an object of its own, because
# nothing references it and a library member would never be linked.  This
# block stays last so it sees every executable defined above.
option(EIDOS_NO_CRT_DIALOGS
       "MSVC: send CRT error reports to stderr instead of modal dialogs" ON)
if(MSVC AND EIDOS_NO_CRT_DIALOGS)
    get_property(_eidos_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_t ${_eidos_targets})
        get_target_property(_type ${_t} TYPE)
        if(_type STREQUAL "EXECUTABLE")
            target_sources(${_t} PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/sdk/plat_nodialog.c)
        endif()
    endforeach()
endif()
