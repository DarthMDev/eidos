# host/CMakeLists.txt -- eidos's 64-bit program for the wasm32 build
# (docs/lp64.md, "The wasm32 configuration's architecture").
#
# Stage one is eidos's own CMake project configured for wasm32 (wasi-sdk's
# toolchain file): it builds eidos_boot.wasm.  This project is stage two,
# with the platform's own compiler: wasm2c turns the module into C (game.h
# and game_*.c, split so they compile in parallel), and that C, wasm2c's
# runtime, host/ and SDL3 make the program.
#
#   cmake -S host -B build-host -DEIDOS_WASM_MODULE=<stage one>/eidos_boot.wasm
#         -DWABT_ROOT=<wabt source tree> -DWASM2C=<wasm2c> -DSDL3_PREFIX=<SDL3>
#         -DWASM_OPT=<wasm-opt> [-DEIDOS_HOST_OPT=-O2]

cmake_minimum_required(VERSION 3.16)
project(eidos_host C)

set(EIDOS_WASM_MODULE "" CACHE FILEPATH "the stage one module (eidos_boot.wasm)")
set(WABT_ROOT "" CACHE PATH "wabt's source tree (its wasm2c/ runtime)")
set(WASM2C "" CACHE FILEPATH "wasm2c")
set(WASM_OPT "" CACHE FILEPATH "binaryen's wasm-opt")
set(EIDOS_HOST_OPT "-O2" CACHE STRING "wasm-opt's optimisation level (-O0 to debug)")
option(EIDOS_HOST_FPCAST
       "--fpcast-emu (OFF: a diagnostic build that logs calls through pointers of another type)" ON)
set(SDL3_PREFIX "" CACHE PATH "SDL3: a prefix with include/SDL3 and the library")
set(EIDOS_HOST_PARTS 32 CACHE STRING "how many C files wasm2c writes")
option(EIDOS_HOST_WINDOWED "Windows: a windowed program with no console (a release)" OFF)

foreach(_v EIDOS_WASM_MODULE WASM2C WASM_OPT)
    if(NOT EXISTS "${${_v}}")
        message(FATAL_ERROR "${_v}: nothing at '${${_v}}'")
    endif()
endforeach()
if(NOT EXISTS "${WABT_ROOT}/wasm2c/wasm-rt-impl.c")
    message(FATAL_ERROR "WABT_ROOT: no wasm2c runtime under '${WABT_ROOT}'")
endif()

set(_root ${CMAKE_CURRENT_SOURCE_DIR}/..)
find_package(Python3 COMPONENTS Interpreter REQUIRED)

# wasm-opt, then wasm2c.  The game calls functions through pointers of
# other prototypes (a handler stored as one type, called as another), which
# the DS allows and a wasm call_indirect traps on; --fpcast-emu makes every
# indirect call take the same 20 64-bit arguments, so a callee ignores the
# extra ones and a missing one is 0 (docs/lp64.md).  The module's widest
# function has 17 parameters.  -g keeps the function names, which become
# the C functions' names and so a crash report's.
if(EIDOS_HOST_FPCAST)
    set(_eidos_fpcast --fpcast-emu --pass-arg=max-func-params@20)
else()
    set(_eidos_fpcast)
endif()
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/game.opt.wasm
    COMMAND ${WASM_OPT} ${EIDOS_WASM_MODULE} ${_eidos_fpcast} ${EIDOS_HOST_OPT} -g
            -o ${CMAKE_BINARY_DIR}/game.opt.wasm
    DEPENDS ${EIDOS_WASM_MODULE}
    COMMENT "wasm-opt --fpcast-emu ${EIDOS_HOST_OPT}")
set(_mod "${CMAKE_BINARY_DIR}/game.opt.wasm")
set(_parts)
math(EXPR _last "${EIDOS_HOST_PARTS} - 1")
foreach(_i RANGE 0 ${_last})
    list(APPEND _parts ${CMAKE_BINARY_DIR}/game_${_i}.c)
endforeach()
# Then its one patch (tools/wasm2c_patch.py: a call to a DS code address).
add_custom_command(
    OUTPUT ${_parts} ${CMAKE_BINARY_DIR}/game.h ${CMAKE_BINARY_DIR}/game-impl.h
    COMMAND ${WASM2C} ${_mod} -n game --num-outputs=${EIDOS_HOST_PARTS}
            -o ${CMAKE_BINARY_DIR}/game.c
    COMMAND ${Python3_EXECUTABLE} ${_root}/tools/wasm2c_patch.py
            ${CMAKE_BINARY_DIR}/game-impl.h
    DEPENDS ${_mod} ${_root}/tools/wasm2c_patch.py
    COMMENT "wasm2c")

# The OpenGL imports, from the module's own list of them.
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/gl_names.txt
    COMMAND ${Python3_EXECUTABLE} ${_root}/tools/wasm_imports.py ${_mod} env gl
            -o ${CMAKE_BINARY_DIR}/gl_names.txt
    DEPENDS ${_mod} ${_root}/tools/wasm_imports.py)
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/gl_imports.c
    COMMAND ${Python3_EXECUTABLE} ${_root}/tools/wasm_gl_imports.py
            --sdl-include ${SDL3_PREFIX}/include --names ${CMAKE_BINARY_DIR}/gl_names.txt
            -o ${CMAKE_BINARY_DIR}/gl_imports.c
    DEPENDS ${CMAKE_BINARY_DIR}/gl_names.txt ${_root}/tools/wasm_gl_imports.py)

add_executable(eidos
    ${_parts}
    ${CMAKE_BINARY_DIR}/gl_imports.c
    ${WABT_ROOT}/wasm2c/wasm-rt-impl.c
    ${WABT_ROOT}/wasm2c/wasm-rt-mem-impl.c
    ${WABT_ROOT}/wasm2c/wasm-rt-exceptions-impl.c
    ${CMAKE_CURRENT_SOURCE_DIR}/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/fiber.c
    ${CMAKE_CURRENT_SOURCE_DIR}/par.c
    ${CMAKE_CURRENT_SOURCE_DIR}/net.c
    ${CMAKE_CURRENT_SOURCE_DIR}/wasi.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdl.c
    ${_root}/install/picker.c
    ${_root}/install/location.c
    ${_root}/sdk/plat_save.c
    ${_root}/sdk/plat_dir.c)
target_include_directories(eidos PRIVATE
    ${CMAKE_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_root}/install
    ${_root}/sdk
    ${WABT_ROOT}/wasm2c
    ${SDL3_PREFIX}/include)
# Each fiber has its own native stack, so wasm2c's stack-depth count (one
# per thread) does not apply; a trap ends the program (no recovery across
# fibers), and guard pages check the memory, which then never moves (the
# SDL surfaces made from module pixels rely on it, host/sdl.c).
if(NOT EIDOS_HOST_FPCAST)
    target_compile_definitions(eidos PRIVATE HOST_INDIRECT_LOG=1)
endif()
target_compile_definitions(eidos PRIVATE
    WASM_RT_NONCONFORMING_UNCHECKED_STACK_EXHAUSTION=1
    WASM_RT_SKIP_SIGNAL_RECOVERY=1
    WASM_RT_TRAP_HANDLER=host_trap)
# The generated files are large; GCC's assembly between its stages would
# otherwise go through the temporary folder (hundreds of MB per file).
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(eidos PRIVATE -pipe)
endif()
if(WIN32)
    # The icon (assets/eidos.rc), and for a release no console window:
    # `eidos --console` opens one (host/main.c, console_open).
    enable_language(RC)
    target_sources(eidos PRIVATE ${_root}/assets/eidos.rc)
    if(EIDOS_HOST_WINDOWED)
        target_link_options(eidos PRIVATE -mwindows)
    endif()
    find_library(_sdl3 NAMES SDL3 libSDL3.dll.a PATHS ${SDL3_PREFIX}/lib NO_DEFAULT_PATH)
    target_link_libraries(eidos PRIVATE ${_sdl3} bcrypt ole32 shell32 uuid comdlg32 ws2_32 dbghelp)
    # WSAPoll (host/net.c) is Vista's
    set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/net.c PROPERTIES
        COMPILE_DEFINITIONS _WIN32_WINNT=0x0601)
    add_custom_command(TARGET eidos POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SDL3_PREFIX}/bin/SDL3.dll $<TARGET_FILE_DIR:eidos>)
else()
    find_library(_sdl3 NAMES SDL3 PATHS ${SDL3_PREFIX}/lib ${SDL3_PREFIX}/lib64)
    target_link_libraries(eidos PRIVATE ${_sdl3} m dl)
endif()
