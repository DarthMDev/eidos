#
# tests/boot_run.cmake -- run the game's own boot headless and judge where it
# stops (docs/boot.md, "Running the boot").
#
#   cmake -DBOOT=<eidos_boot> -DDATA=<BW_DATA_ROOT> [-DFRAMES=<n>]
#         [-DWATCHDOG=<seconds>] -P boot_run.cmake
#
# The boot needs the player's game data: the file tree under DATA and the
# ARM9 image beside it (DATA/../arm9_dec.bin, docs/armdata.md).  Without
# them the test prints SKIPPED and CTest counts it as skipped
# (SKIP_REGULAR_EXPRESSION).
#
# Pass when the run ends in one of the two ways a correct boot ends today:
#   - the overlay layer's own stop, "PANIC in FS_LoadOverlay ... overlay N
#     (...) is not in this build": the boot got as far as the first overlay
#     the build does not compile (sdk/overlay.c), which is the boundary of
#     the port's code, not a port bug;
#   - all FRAMES frames ran ("eidos_boot: ran after N frames");
#   - the stop the build documents as today's (-DKNOWN_STOP=<regex>, set by
#     cmake/boot.cmake when overlays are compiled in).
# With 8-byte pointers (-DLP64=1) the boot cannot run (docs/armdata.md,
# "Pointer width"); the test then checks that it refuses with the data
# layer's own message rather than crashing.
# Anything else fails with the run's output: a stub reached, a crash, a hang
# (the watchdog's report), another panic.  When the overlays a stop names
# join the build, the boot runs further and this test keeps passing as long
# as it ends in one of the two ways above.
#
if(NOT DEFINED FRAMES)
    set(FRAMES 120)
endif()
if(NOT DEFINED WATCHDOG)
    set(WATCHDOG 60)
endif()
if(NOT EXISTS "${DATA}/a" OR NOT EXISTS "${DATA}/../arm9_dec.bin")
    message("SKIPPED: no game data at ${DATA} (or no arm9_dec.bin beside it)")
    return()
endif()
execute_process(COMMAND "${BOOT}" --data-root "${DATA}" --frames ${FRAMES}
                        --watchdog ${WATCHDOG}
    RESULT_VARIABLE rc
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
    TIMEOUT 600)
set(all "${out}${err}")
if(LP64)
    if(all MATCHES "the boot needs an ILP32 build")
        message(STATUS "the LP64 boot refused as designed")
        return()
    endif()
    message(FATAL_ERROR "the LP64 boot did not refuse cleanly (exit ${rc}):\n${all}")
endif()
if(all MATCHES "PANIC in FS_LoadOverlay at 0x[0-9A-F]+: overlay [0-9]+ \\([^)]*\\) is not in this build")
    string(REGEX MATCH "overlay [0-9]+ \\([^)]*\\) is not in this build" stop "${all}")
    message(STATUS "the boot stopped at the overlay boundary: ${stop}")
    return()
endif()
# The stop the build expects today (cmake/boot.cmake, KNOWN_STOP): where
# the run ends once the overlays it loads are compiled in, documented there.
if(NOT "${KNOWN_STOP}" STREQUAL "" AND all MATCHES "${KNOWN_STOP}")
    message(STATUS "the boot stopped at the known stop: ${KNOWN_STOP}")
    return()
endif()
if(rc EQUAL 0 AND all MATCHES "eidos_boot: ran after ${FRAMES} frames")
    message(STATUS "the boot ran ${FRAMES} frames")
    return()
endif()
message(FATAL_ERROR "the boot stopped somewhere else (exit ${rc}):\n${all}")
