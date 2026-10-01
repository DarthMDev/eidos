#
# tests/expect_panic.cmake -- pass if a test program panics as expected.
#
#   cmake -DEXE=<program> -DARG=<argument> -DPATTERN=<regex> -P expect_panic.cmake
#
# bw_panic aborts, and CTest counts an abnormal exit as a failure that no
# test property can invert, so a test of a panic runs the program from this
# script and judges its stderr instead.
#
execute_process(COMMAND "${EXE}" ${ARG}
    RESULT_VARIABLE rc
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err)
if(rc EQUAL 0)
    message(FATAL_ERROR "expected a panic, the program exited normally\n${out}${err}")
endif()
if(NOT err MATCHES "${PATTERN}")
    message(FATAL_ERROR "panic message did not match '${PATTERN}':\n${err}")
endif()
message(STATUS "panicked as expected: ${err}")
