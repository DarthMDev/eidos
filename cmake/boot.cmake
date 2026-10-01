#
# cmake/boot.cmake -- link the game's own boot into eidos_boot (EIDOS_BOOT=ON).
#
# docs/boot.md, "Linking the boot", is the design.  Included from
# CMakeLists.txt after the eidos library is defined; adds the eidos_boot_game
# object library and links it into eidos_boot.
#
# What happens, in order:
#
#   configure  tools/bootlink.py plan walks tools/closure.py's closure from
#              NitroMain's callees (EIDOS_BOOT_SEEDS), drops the files listed
#              in EIDOS_BOOT_SHIM_REPLACED (checked against what the shims
#              really define: a clash missing from the list fails the
#              configure), drops the mwcc `asm` files (an sdk/asm/<file>.c
#              takes the place of one when it exists) and the files the
#              eidos library already compiles, and writes the source list.
#   build      tools/bootlink.py generate writes one forced-include header
#              per translation unit (renamed spellings, link-time constants,
#              declarations for implicit calls), boot_link.c (the constants'
#              values, the stubs for functions nothing defines) and
#              boot_data_stub.c (TEMPORARY zero-filled ARM9 data objects).
#
# Everything generated lands in ${CMAKE_BINARY_DIR}/boot/; the report of what
# was stubbed is boot/gen/boot_link_report.txt.
#

set(EIDOS_BOOT_DIR ${CMAKE_BINARY_DIR}/boot)
set(EIDOS_BOOT_GEN ${EIDOS_BOOT_DIR}/gen)

# NitroMain's direct matched callees (stubs/reimpl_02005124.c).
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

# ---------------------------------------------------------------------------
# The one list of matched files a port shim replaces
# ---------------------------------------------------------------------------
#
# A matched file is left out of the boot when a shim under sdk/ stands in for
# it: the shim defines the same names (directly, or the SDK name at the
# file's address), so both in one link is a duplicate symbol, and the shim is
# the one that is right on a host (docs/boot.md, "Three layers").  Paths are
# relative to the aletheia checkout.  bootlink.py plan fails the configure
# if a file in the closure defines a shim's name and is not listed here, and
# names every listed file that has no clash (those are kept out by the
# owning lane's decision: they manipulate state the shim keeps differently).
set(EIDOS_BOOT_SHIM_REPLACED
    # sdk/os_thread.c: threads on host threads under one run token
    src/matched/arm9_sdk/func_02085264.c   # OS_InitThread
    src/matched/arm9_sdk/func_020853B0.c   # OS_IsThreadAvailable
    src/matched/arm9_sdk/func_020853C0.c   # OS_CreateThread
    src/matched/arm9_sdk/func_020854D0.c
    src/matched/arm9_sdk/func_02085528.c
    src/matched/arm9_sdk/func_02085560.c
    src/matched/arm9_sdk/func_020855BC.c   # OS_DestroyThread
    src/matched/arm9_sdk/func_0208574C.c   # OS_IsThreadTerminated
    src/matched/arm9_sdk/func_0208565C.c
    src/matched/arm9_sdk/func_02085710.c
    src/matched/arm9_sdk/func_02085760.c   # OS_SleepThreadDirect
    src/matched/arm9_sdk/func_02085868.c   # OS_WakeupThreadDirect
    src/matched/arm9_sdk/func_020858BC.c   # OS_RescheduleThread
    src/matched/arm9_sdk/func_020858D8.c
    src/matched/arm9_sdk/func_02085988.c   # OS_SetThreadPriority
    src/matched/arm9_sdk/func_02085A38.c   # OS_Sleep
    src/matched/arm9_sdk/func_02085AD8.c
    src/matched/arm9_sdk/func_02085AF8.c   # OS_SetSwitchThreadCallback
    src/matched/arm9_sdk/func_02085B20.c   # OSi_IdleThreadProc
    src/matched/arm9_sdk/func_02085B30.c   # OS_DisableScheduler
    src/matched/arm9_sdk/func_02085B60.c   # OS_EnableScheduler
    src/matched/arm9_sdk/func_020850CC.c   # OSi_RemoveMutexLinkFromQueue
    src/matched/arm9_sdk/func_02085078.c
    src/matched/arm9_sdk/func_020850FC.c
    src/matched/arm9_sdk/func_02085158.c
    src/matched/arm9_sdk/func_02084FB4.c
    src/matched/arm9_sdk/OS_SleepThread.c
    src/matched/arm9_sdk/OS_WakeupThread.c
    src/matched/arm9_sdk/OS_SelectThread.c
    src/matched/arm9_sdk/OSi_RescheduleThread.c
    src/matched/arm9_sdk/OSi_InsertLinkToQueue.c
    src/matched/arm9_sdk/OSi_RemoveLinkFromQueue.c
    src/matched/arm9_sdk/OS_ExitThread.c
    # sdk/os_alarm.c: alarms on the tick
    src/matched/arm9_sdk/func_02087208.c   # OS_InitAlarm
    src/matched/arm9_sdk/func_02087244.c   # OS_IsAlarmAvailable
    src/matched/arm9_sdk/func_02087254.c   # OS_CreateAlarm
    src/matched/arm9_sdk/func_020873F8.c   # OS_CancelAlarm
    src/matched/arm9_sdk/func_0208748C.c
    src/matched/arm9_sdk/OS_SetAlarm.c
    src/matched/arm9_sdk/OSi_InsertAlarm.c
    # sdk/os_tick.c: OS_GetTick on the frame loop
    src/matched/arm9_sdk/func_02086FE8.c   # OS_InitTick
    src/matched/arm9_sdk/func_02087058.c   # OS_IsTickAvailable
    src/matched/arm9_sdk/func_02087068.c
    src/matched/arm9_sdk/func_02087170.c   # OS_GetTickLo
    src/matched/arm9_sdk/OS_GetTick.c
    # sdk/os_interrupt.c: the waits on the IRQ check flag, the IF latch
    src/matched/arm9_sdk/func_0208459C.c   # OS_WaitIrq
    src/matched/arm9_sdk/func_02087A3C.c
    src/matched/arm9_sdk/OS_Halt.c
    src/matched/arm9_sdk/OS_ResetRequestIrqMask.c
    # sdk/os_irq.c: the CPSR
    src/matched/arm9_sdk/OS_DisableInterrupts.c
    src/matched/arm9_sdk/OS_EnableInterrupts.c
    src/matched/arm9_sdk/OS_RestoreInterrupts.c
    src/matched/arm9_sdk/OS_DisableInterrupts_IrqAndFiq.c
    src/matched/arm9_sdk/OS_RestoreInterrupts_IrqAndFiq.c
    src/matched/arm9_sdk/OS_GetCpsrIrq.c
    src/matched/arm9_sdk/OS_GetProcMode.c
    src/matched/arm9_sdk/OS_Terminate.c
    # sdk/os_system.c: console and boot type, MPU, the ARM7 handshake
    src/matched/arm9_sdk/func_02085CBC.c   # OS_GetConsoleType
    src/matched/arm9_sdk/func_02086DB0.c   # OS_SetProtectionRegionEx
    src/matched/arm9_sdk/func_02086348.c   # OSi_WaitVCount0
    src/matched/arm9_sdk/func_02084AB0.c   # OSi_SyncWithOtherProc
    src/matched/arm9_sdk/func_02087B04.c   # OS_GetBootType
    src/matched/arm9_sdk/OS_SetProtectionRegionParam.c
    # sdk/os_argument.c: the deliver argument
    src/matched/arm9_sdk/func_02087F60.c   # OS_InitDeliverArgInfo
    src/matched/arm9_sdk/func_02087FBC.c   # OS_EncodeDeliverArg
    src/matched/arm9_sdk/func_0208806C.c   # OS_DecodeDeliverArg
    src/matched/arm9_sdk/func_02088118.c   # OS_SetDeliverArgStateInvalid
    src/matched/arm9_sdk/func_0208812C.c   # OS_GetTitleIdFromDeliverArg
    # sdk/os_alloc.c: the OS heap on malloc
    src/calib/func_020868E0.c              # OS_AllocFromHeap
    # sdk/pxi.c: the FIFO model
    src/matched/arm9_sdk/func_020884A4.c   # PXI_Init's FIFO set-up
    src/matched/arm9_sdk/func_02088688.c   # PXIi_HandlerRecvFifoNotEmpty
    # sdk/mi.c: MI_CpuCopy/Fill on memcpy/memset
    src/matched/arm9_sdk/MI_CpuCopy8.c
    src/matched/arm9_sdk/MI_CpuFill8.c
    src/matched/arm9_sdk/MIi_CpuClear32.c
    src/matched/arm9_sdk/func_02082AC0.c
    # sdk/fs.c: the ROM file system on the extract
    src/matched/arm9_sdk/func_0207AD04.c   # FS_Init
    src/matched/arm9_sdk/func_0207AC64.c   # FS_SetDefaultDMA
    src/matched/arm9_sdk/FS_LoadArchiveTables.c
    src/matched/arm9_sdk/FS_InitFile.c
    src/matched/arm9_sdk/FS_CloseFile.c
    src/matched/arm9_sdk/func_020788D8.c   # FS_ConvertPathToFileID
    src/matched/arm9_sdk/func_02078964.c   # FS_OpenFileDirect
    src/matched/arm9_sdk/func_020789A4.c   # FS_OpenFileFast
    src/matched/arm9_sdk/func_020789FC.c   # FS_OpenFileEx
    src/matched/arm9_sdk/func_02078A94.c   # FS_GetFileLength
    src/matched/arm9_sdk/func_02078B44.c   # FS_SeekFile
    src/matched/arm9_sdk/func_02078B70.c   # FS_ReadFile
    src/matched/arm9/func_02078C28.c
    src/matched/arm9_sdk/func_0207A940.c   # FS_CreateFileFromMemory
    src/matched/arm9_sdk/CRYPTOi__BN_lshift1.c
    # sdk/overlay.c: overlays compiled in, checked on load
    src/matched/arm9_sdk/func_02078C40.c   # FSi_InitOverlay
    src/matched/arm9_sdk/func_02078D34.c   # FS_LoadOverlayInfo
    src/matched/arm9_sdk/func_02078EB0.c   # FS_LoadOverlayImage
    src/matched/arm9_sdk/func_0207902C.c   # FS_StartOverlay
    src/matched/arm9_sdk/unit_02079164.c   # FS_EndOverlay, FS_UnloadOverlayImage
    src/matched/arm9_sdk/func_02079264.c   # FS_LoadOverlay
    src/matched/arm9_sdk/func_020792B8.c   # FS_UnloadOverlay
    # stubs/reimpl_02045A98.c: the end-of-string code unit, already in the
    # eidos library (written before aletheia matched it)
    src/matched/arm9_sdk/func_02045A98.c
)

# The pointer-width switches (CMakeLists.txt, "Pointer width") put a
# stubs/reimpl_ file in the eidos library in place of a matched one; the
# boot leaves out the matched file the library's choice replaces.
list(APPEND EIDOS_BOOT_SHIM_REPLACED
    # stubs/reimpl_02060888.c, reimpl_02060E30.c: the carve and the free read
    # a local through its neighbour's address (mwcc's frame layout), so they
    # are never compiled as written, at either width
    src/matched/arm9/func_02060888.c
    src/matched/arm9/func_02060E30.c)
if(NOT EIDOS_HEAP_MATCHED)
    list(APPEND EIDOS_BOOT_SHIM_REPLACED
        # stubs/reimpl_02060A08.c, reimpl_02060ABC.c: the expanded heap's
        # walks on host blocks (the LP64 heap floor)
        src/matched/arm9/func_02060A08.c
        src/matched/arm9/func_02060ABC.c)
endif()

# The data lane's definitions of the ARM9 data objects.  Empty until it
# lands; every name a file listed here defines leaves boot_data_stub.c.
set(EIDOS_BOOT_DATA_SOURCES "" CACHE STRING
    "C files defining ARM9 data objects for the boot (replace boot_data_stub.c)")

# Port code compiled into the boot beside the matched closure.
set(EIDOS_BOOT_PORT_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/reimpl_02005124.c   # NitroMain
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/reimpl_02047990.c   # the deferred buffer swap
)
if(NOT EIDOS_HEAP_MATCHED)
    # The eidos library carries these two only with the matched heap floor;
    # the boot reaches the carve and the free at both widths.
    list(APPEND EIDOS_BOOT_PORT_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/stubs/reimpl_02060888.c
        ${CMAKE_CURRENT_SOURCE_DIR}/stubs/reimpl_02060E30.c)
endif()

# eidos_force_include(<source> <header>): compile <source> as if it began
# with #include "<header>".  -include for GCC and Clang, /FI for MSVC and
# clang-cl.
function(eidos_force_include src header)
    if(MSVC)
        set_property(SOURCE ${src} APPEND PROPERTY COMPILE_OPTIONS "/FI${header}")
    else()
        set_property(SOURCE ${src} APPEND PROPERTY COMPILE_OPTIONS "-include" "${header}")
    endif()
endfunction()

# --- configure: the plan ----------------------------------------------------

file(MAKE_DIRECTORY ${EIDOS_BOOT_DIR})
string(REPLACE ";" "\n" _boot_excl "${EIDOS_BOOT_SHIM_REPLACED}")
file(WRITE ${EIDOS_BOOT_DIR}/shim_replaced.txt "${_boot_excl}\n")
# The shims: every port source of the eidos library.  Its matched sources
# are passed separately (they are subtracted from the closure).
set(_boot_shims ${BW_SDK_SOURCES} ${BW_STUB_SOURCES})
list(REMOVE_ITEM _boot_shims ${BW_OVERLAY_TABLE})
string(REPLACE ";" "\n" _boot_shim_txt "${_boot_shims}")
file(WRITE ${EIDOS_BOOT_DIR}/shim_sources.txt "${_boot_shim_txt}\n")
string(REPLACE ";" "\n" _boot_lib_txt "${BW_MATCHED_ORIGINALS}")
file(WRITE ${EIDOS_BOOT_DIR}/library_matched.txt "${_boot_lib_txt}\n")
string(REPLACE ";" "\n" _boot_port_txt "${EIDOS_BOOT_PORT_SOURCES}")
file(WRITE ${EIDOS_BOOT_DIR}/port_sources.txt "${_boot_port_txt}\n")
string(REPLACE ";" "\n" _boot_data_txt "${EIDOS_BOOT_DATA_SOURCES}")
file(WRITE ${EIDOS_BOOT_DIR}/data_sources.txt "${_boot_data_txt}\n")

set(_seed_args)
foreach(s ${EIDOS_BOOT_SEEDS})
    list(APPEND _seed_args --seed src/matched/${s})
endforeach()
message(STATUS "EIDOS_BOOT: planning the boot (tools/bootlink.py plan)")
execute_process(
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/bootlink.py plan
            --aletheia-root ${ALETHEIA_ROOT} ${_seed_args}
            --exclude-file ${EIDOS_BOOT_DIR}/shim_replaced.txt
            --shim-sources ${EIDOS_BOOT_DIR}/shim_sources.txt
            --shim-sources ${EIDOS_BOOT_DIR}/port_sources.txt
            --library-matched ${EIDOS_BOOT_DIR}/library_matched.txt
            --asm-dir ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm
            --out-dir ${EIDOS_BOOT_DIR}
    RESULT_VARIABLE _boot_rc
    OUTPUT_VARIABLE _boot_out
    ERROR_VARIABLE _boot_err)
string(STRIP "${_boot_out}" _boot_out)
string(REPLACE "\n" "\n   " _boot_out "${_boot_out}")
message(STATUS "EIDOS_BOOT:\n   ${_boot_out}")
if(NOT _boot_rc EQUAL 0)
    message(FATAL_ERROR "EIDOS_BOOT: tools/bootlink.py plan failed (${_boot_rc}): "
                        "fix EIDOS_BOOT_SHIM_REPLACED in cmake/boot.cmake\n${_boot_err}")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/bootlink.py
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/closure.py)
# A new sdk/asm/ file changes the plan: re-run it when the set changes.
file(GLOB _boot_asm_present CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/sdk/asm/*.c)

file(STRINGS ${EIDOS_BOOT_DIR}/boot_matched.txt EIDOS_BOOT_MATCHED)
file(STRINGS ${EIDOS_BOOT_DIR}/boot_asm_subst.txt EIDOS_BOOT_ASM_SUBST)

# --- build: the generated headers and C ------------------------------------

set(_boot_headers)
set(_boot_units ${EIDOS_BOOT_MATCHED} ${EIDOS_BOOT_PORT_SOURCES} ${EIDOS_BOOT_ASM_SUBST})
foreach(src ${_boot_units})
    file(RELATIVE_PATH _rel "${ALETHEIA_ROOT}" "${src}")
    if(_rel MATCHES "^\\.\\./" OR IS_ABSOLUTE "${_rel}")
        get_filename_component(_rel "${src}" NAME)
        set(_rel "port/${_rel}")
    endif()
    string(REGEX REPLACE "\\.c$" "" _rel "${_rel}")
    string(REGEX REPLACE "[^A-Za-z0-9_]" "_" _hname "${_rel}")
    set(_hdr ${EIDOS_BOOT_GEN}/include/${_hname}.h)
    list(APPEND _boot_headers ${_hdr})
    set(_boot_header_of_${_hname} ${_hdr})
endforeach()

set(_boot_gen_c ${EIDOS_BOOT_GEN}/boot_link.c ${EIDOS_BOOT_GEN}/boot_data_stub.c)
# sdk/ decides some names with `#if EIDOS_HEAP_MATCHED`; the generator is
# told the value this build compiles them with.
if(EIDOS_HEAP_MATCHED)
    set(_boot_heap_matched 1)
else()
    set(_boot_heap_matched 0)
endif()
add_custom_command(
    OUTPUT ${_boot_headers} ${_boot_gen_c} ${EIDOS_BOOT_GEN}/boot_link_report.txt
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/bootlink.py generate
            --aletheia-root ${ALETHEIA_ROOT}
            --plan-dir ${EIDOS_BOOT_DIR}
            --shim-sources ${EIDOS_BOOT_DIR}/shim_sources.txt
            --port-sources ${EIDOS_BOOT_DIR}/port_sources.txt
            --data-sources ${EIDOS_BOOT_DIR}/data_sources.txt
            --define EIDOS_HEAP_MATCHED=${_boot_heap_matched}
            --out-dir ${EIDOS_BOOT_GEN}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/bootlink.py
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/closure.py
            ${EIDOS_BOOT_DIR}/boot_matched.txt
            ${_boot_shims} ${EIDOS_BOOT_PORT_SOURCES} ${EIDOS_BOOT_DATA_SOURCES}
    COMMENT "Generating the boot's link headers, constants and stubs"
    VERBATIM)

# --- the library ------------------------------------------------------------

list(LENGTH EIDOS_BOOT_MATCHED _boot_n)
message(STATUS "EIDOS_BOOT: ON, ${_boot_n} matched sources beyond the eidos library")

eidos_dsaddr(EIDOS_BOOT_COPIES ${EIDOS_BOOT_MATCHED})

# An object library: every boot object is in the link, so the link checks
# the whole closure, not only what NitroMain happens to reach first.
add_library(eidos_boot_game OBJECT
    ${EIDOS_BOOT_COPIES}
    ${EIDOS_BOOT_PORT_SOURCES}
    ${EIDOS_BOOT_ASM_SUBST}
    ${EIDOS_BOOT_DATA_SOURCES}
    ${_boot_gen_c}
)
target_include_directories(eidos_boot_game PRIVATE
    $<TARGET_PROPERTY:eidos,INTERFACE_INCLUDE_DIRECTORIES>
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs)
target_compile_definitions(eidos_boot_game PRIVATE
    $<TARGET_PROPERTY:eidos,INTERFACE_COMPILE_DEFINITIONS>)

# The forced-include header of each translation unit.
foreach(src ${_boot_units})
    file(RELATIVE_PATH _rel "${ALETHEIA_ROOT}" "${src}")
    if(_rel MATCHES "^\\.\\./" OR IS_ABSOLUTE "${_rel}")
        get_filename_component(_rel "${src}" NAME)
        set(_rel "port/${_rel}")
        set(_compiled ${src})
    else()
        eidos_dsaddr_path(_compiled ${src})
    endif()
    string(REGEX REPLACE "\\.c$" "" _rel "${_rel}")
    string(REGEX REPLACE "[^A-Za-z0-9_]" "_" _hname "${_rel}")
    eidos_force_include(${_compiled} ${_boot_header_of_${_hname}})
    set_property(SOURCE ${_compiled} APPEND PROPERTY OBJECT_DEPENDS
        ${_boot_header_of_${_hname}})
endforeach()

# The matched files keep the warning exemptions the eidos library gives
# them (CMakeLists.txt), plus the ones the boot's 1100 files raise under
# GCC 14 and Clang that only an edit of the matched source could silence:
# a switch case that falls through, a variable kept for its register
# allocation, an unused static helper or label, `#pragma unused`, a
# signed/unsigned comparison or precedence the DS build relies on, a
# function that ends without a return on a path mwcc never takes.  Each is
# reproduced exactly from the DS code; none is a host porting problem.
if(MSVC)
    set(_boot_matched_opts ${EIDOS_MATCHED_COMPILE_OPTIONS})
else()
    set(_boot_matched_opts
        -Wno-unused-parameter -Wno-unused-variable -Wno-missing-prototypes
        -Wno-unused-but-set-variable -Wno-implicit-fallthrough
        -Wno-sign-compare -Wno-parentheses -Wno-unused-function
        -Wno-unused-label -Wno-unknown-pragmas -Wno-switch -Wno-return-type
        -Wno-type-limits -Wno-cast-function-type)
    # With 8-byte pointers, GCC flags every cast between a pointer and a
    # 32-bit integer: 532 sites in the boot's matched files, the
    # pointer-width question docs/pointer-width.md measures (signal C).  They
    # are known, counted there, and why the LP64 boot links but is not
    # expected to run; the reference boot is ILP32, where none of them fires.
    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
        list(APPEND _boot_matched_opts
            -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast)
    endif()
endif()
foreach(src ${EIDOS_BOOT_COPIES})
    set_property(SOURCE ${src} APPEND PROPERTY COMPILE_OPTIONS ${_boot_matched_opts})
endforeach()

target_sources(eidos_boot PRIVATE $<TARGET_OBJECTS:eidos_boot_game>)
target_compile_definitions(eidos_boot PRIVATE EIDOS_BOOT=1)
