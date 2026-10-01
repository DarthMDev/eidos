#
# tests/expect_panic.cmake -- pass if a test program panics as expected.
#
#   cmake -DCMD=<program> [-DARG1=<a>] [-DARG2=<b>] -DEXPECT=<regex> -P expect_panic.cmake
#
# bw_panic aborts, and CTest counts an abnormal exit as a failure that no
# test property can invert (abort() also ends a process differently on every
# host: SIGABRT, Windows fast-fail 0xC0000409, exit code 3), so a test of a
# panic runs the program from this script and judges its output instead.
# EXE/ARG/PATTERN are accepted as older spellings of CMD/ARG1/EXPECT.
#
if(NOT DEFINED CMD AND DEFINED EXE)
    set(CMD "${EXE}")
endif()
if(NOT DEFINED ARG1 AND DEFINED ARG)
    set(ARG1 "${ARG}")
endif()
if(NOT DEFINED EXPECT AND DEFINED PATTERN)
    set(EXPECT "${PATTERN}")
endif()
execute_process(COMMAND "${CMD}" ${ARG1} ${ARG2}
    RESULT_VARIABLE rc
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err)
set(all "${out}${err}")
if(rc EQUAL 0)
    message(FATAL_ERROR "expected a panic, the program exited normally\n${all}")
endif()
if(NOT all MATCHES "${EXPECT}")
    message(FATAL_ERROR "panic output did not match '${EXPECT}':\n${all}")
endif()
message(STATUS "panicked as expected (${rc})")
