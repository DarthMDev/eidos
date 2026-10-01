# tests/firstrun_rom.cmake -- first run on the player's real ROM, no window.
#
#     cmake -DVIEW=<eidos_view> -DWORK=<dir> -P tests/firstrun_rom.cmake
#
# The ROM is named by the BW_ROM environment variable at test time; without
# it the test reports itself skipped.  The eidos home is a scratch folder
# (EIDOS_HOME), so the player's own settings and data are never touched.
#
#   1. eidos_view --rom <ROM> --no-window makes the game data in the default
#      place under that home and then draws from it (two frames, a
#      screenshot);
#   2. a plain start finds it through the saved setting;
#   3. --move-data moves it, and the next plain start finds it there;
#   4. --reset-data deletes it, and the next start without a window says
#      what is missing and fails.
# On success the scratch folder is deleted.

foreach(v VIEW WORK)
    if(NOT DEFINED ${v})
        message(FATAL_ERROR "firstrun_rom.cmake: -D${v}= is required")
    endif()
endforeach()

set(ROM "$ENV{BW_ROM}")
if(ROM STREQUAL "")
    message("SKIPPED: set BW_ROM to the path of the Pokemon Black ROM to run this test")
    return()
endif()
if(NOT EXISTS "${ROM}")
    message(FATAL_ERROR "BW_ROM is set but ${ROM} does not exist")
endif()

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
set(ENV{EIDOS_HOME} "${WORK}/home")
set(ENV{BW_DATA_ROOT} "")

function(run_view expect_ok label)
    execute_process(COMMAND "${VIEW}" ${ARGN}
                    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(expect_ok AND NOT rc EQUAL 0)
        message(FATAL_ERROR "${label}: exit ${rc}\n${out}\n${err}")
    endif()
    if(NOT expect_ok AND rc EQUAL 0)
        message(FATAL_ERROR "${label}: succeeded, should have failed\n${out}\n${err}")
    endif()
    set(last_out "${out}${err}" PARENT_SCOPE)
    message(STATUS "${label}: exit ${rc}")
endfunction()

set(FRAMES --no-window --no-audio --frames 2)

# 1. --rom
run_view(TRUE "install with --rom" --rom "${ROM}" ${FRAMES} --shot "${WORK}/shot.bmp")
if(NOT EXISTS "${WORK}/home/extract/eidos_manifest.json")
    message(FATAL_ERROR "no extraction in ${WORK}/home/extract")
endif()
if(NOT EXISTS "${WORK}/shot.bmp")
    message(FATAL_ERROR "eidos_view drew nothing from the new data")
endif()
file(READ "${WORK}/home/config.txt" cfg)
if(NOT cfg MATCHES "rom=")
    message(FATAL_ERROR "the ROM was not remembered:\n${cfg}")
endif()

# 2. found again, no flags
run_view(TRUE "start from the saved setting" ${FRAMES})

# 3. move
run_view(TRUE "move" --move-data "${WORK}/moved data")
if(EXISTS "${WORK}/home/extract" OR NOT EXISTS "${WORK}/moved data/eidos_manifest.json")
    message(FATAL_ERROR "--move-data did not move the extraction")
endif()
run_view(TRUE "start after the move" ${FRAMES})

# 4. reset
run_view(TRUE "reset" --reset-data)
if(EXISTS "${WORK}/moved data")
    message(FATAL_ERROR "--reset-data left the extraction")
endif()
run_view(FALSE "start with no data and no window" ${FRAMES})
if(NOT last_out MATCHES "no playable game data")
    message(FATAL_ERROR "the headless message is missing:\n${last_out}")
endif()

file(REMOVE_RECURSE "${WORK}")
message(STATUS "first run on the real ROM: install, reuse, move, reset all good")
