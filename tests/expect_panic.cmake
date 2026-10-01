# tests/expect_panic.cmake -- run a test binary that is expected to stop in
# bw_panic, and pass when it did not exit normally and its output matches
# EXPECT.  ctest's own PASS_REGULAR_EXPRESSION cannot express this: it
# counts a process that ends in abort() as a failure whatever it printed,
# and abort() ends a process differently on every host (SIGABRT, Windows
# fast-fail 0xC0000409, exit code 3).
#
#   cmake -DCMD=<binary> [-DARG1=<a>] [-DARG2=<b>] -DEXPECT=<regex> -P expect_panic.cmake

execute_process(
    COMMAND ${CMD} ${ARG1} ${ARG2}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err)
set(all "${out}${err}")
message("${all}")
if(result EQUAL 0)
    message(FATAL_ERROR "expected a panic, the process exited normally")
endif()
if(NOT all MATCHES "${EXPECT}")
    message(FATAL_ERROR "the output does not match: ${EXPECT}")
endif()
message("panicked as expected (${result})")
