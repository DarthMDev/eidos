#
# tests/boot_run.cmake -- run the game's own boot and judge how far it gets
# (docs/boot.md, "Running the boot").
#
#   cmake -DBOOT=<eidos_boot> -DDATA=<BW_DATA_ROOT> [-DFRAMES=<n>]
#         [-DMIN_FRAMES=<n>] [-DKNOWN_STOP=<regex>] [-DRESIDENT=<ids>]
#         [-DWINDOW=1] [-DWATCHDOG=<seconds>] [-DLP64=1] -P boot_run.cmake
#
# The boot needs the player's game data: the file tree under DATA and the
# ARM9 image beside it (DATA/../arm9_dec.bin, docs/armdata.md).  Without
# them the test prints SKIPPED and CTest counts it as skipped
# (SKIP_REGULAR_EXPRESSION).
#
# The run asks for FRAMES frames.  It passes when
#   - it runs them all ("eidos_boot: ran after FRAMES frames"), and, when
#     RESIDENT is given, the overlays resident at the end are exactly those
#     (ids separated by spaces, as eidos_boot prints them); or
#   - it stops at the build's documented next stop: the output matches
#     KNOWN_STOP and the report says it got at least MIN_FRAMES frames in
#     ("eidos_boot: stopped at frame N", which eidos_boot prints with every
#     crash, panic or watchdog report).  MIN_FRAMES is the furthest point
#     the boot is known to reach, so a stop before it is a regression even
#     when the message matches;
#   - without overlays compiled in (no KNOWN_STOP), it stops at the overlay
#     layer's own boundary: "PANIC in FS_LoadOverlay ... overlay N (...) is
#     not in this build".
# With 8-byte pointers (-DLP64=1) the boot cannot run (docs/armdata.md,
# "Pointer width"); the test then checks that it refuses with the data
# layer's own message rather than crashing.
# WINDOW=1 runs it with --window (the caller sets SDL_VIDEODRIVER and
# SDL_AUDIODRIVER to dummy): the window and the audio queue live on the
# frame thread, and a run that stalls there fails on the watchdog.
# Anything else fails with the run's output.
#
if(NOT DEFINED FRAMES)
    set(FRAMES 120)
endif()
if(NOT DEFINED MIN_FRAMES)
    set(MIN_FRAMES 0)
endif()
if(NOT DEFINED WATCHDOG)
    set(WATCHDOG 60)
endif()
if(NOT EXISTS "${DATA}/a" OR NOT EXISTS "${DATA}/../arm9_dec.bin")
    message("SKIPPED: no game data at ${DATA} (or no arm9_dec.bin beside it)")
    return()
endif()
set(window_arg)
if(WINDOW)
    set(window_arg --window)
endif()
execute_process(COMMAND "${BOOT}" --data-root "${DATA}" --frames ${FRAMES}
                        --watchdog ${WATCHDOG} ${window_arg}
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

string(REGEX MATCH "resident overlays:([ 0-9]*)" _res "${all}")
set(resident "${CMAKE_MATCH_1}")
string(STRIP "${resident}" resident)

if(rc EQUAL 0 AND all MATCHES "eidos_boot: ran after ${FRAMES} frames")
    if(DEFINED RESIDENT AND NOT "${RESIDENT}" STREQUAL ""
       AND NOT "${resident}" STREQUAL "${RESIDENT}")
        message(FATAL_ERROR "the boot ran ${FRAMES} frames, but the resident "
                            "overlays are '${resident}', not '${RESIDENT}':\n${all}")
    endif()
    message(STATUS "the boot ran ${FRAMES} frames; resident overlays: ${resident}")
    return()
endif()

if(NOT "${KNOWN_STOP}" STREQUAL "" AND all MATCHES "${KNOWN_STOP}")
    string(REGEX MATCH "stopped at frame ([0-9]+)" _at "${all}")
    set(at "${CMAKE_MATCH_1}")
    if("${at}" STREQUAL "")
        set(at 0)
    endif()
    if(at LESS MIN_FRAMES)
        message(FATAL_ERROR "the boot stopped at the known stop, but at frame "
                            "${at}, before ${MIN_FRAMES}:\n${all}")
    endif()
    message(STATUS "the boot reached frame ${at} and stopped at the known "
                   "stop: ${KNOWN_STOP}")
    return()
endif()

if("${KNOWN_STOP}" STREQUAL ""
   AND all MATCHES "PANIC in FS_LoadOverlay at 0x[0-9A-F]+: overlay [0-9]+ \\([^)]*\\) is not in this build")
    string(REGEX MATCH "overlay [0-9]+ \\([^)]*\\) is not in this build" stop "${all}")
    message(STATUS "the boot stopped at the overlay boundary: ${stop}")
    return()
endif()
message(FATAL_ERROR "the boot stopped somewhere else (exit ${rc}):\n${all}")
