# cmake/wamr/CMakeLists.txt -- the WebAssembly runtime code mods run in.
#
# WAMR's own build script (runtime_lib.cmake) sets compile definitions and
# include folders for the folder it is read in; this folder of its own keeps
# them away from the port's sources.  The result is one static library,
# eidos_wamr, whose public include folder holds wasm_export.h.
#
# The runtime is the interpreter only (the fast one): no AOT, no JIT, no WASI
# and no libc for the module, which reaches the game through the port's
# imports alone (docs/mod-code-wasm.md).  The native calls go through WAMR's
# portable C path (invokeNative_general.c), so no assembler is needed on any
# host.

# The port's directory-wide options (-include gamespell.h, the DS struct
# layout flags of the top-level file) are for the game's code; WAMR is built
# with the host's own layout, the one wasm_export.h is read with in
# sdk/mod_wasm.c.
set_property(DIRECTORY PROPERTY COMPILE_OPTIONS "")

if(WIN32)
    set(WAMR_BUILD_PLATFORM "windows")
elseif(APPLE)
    set(WAMR_BUILD_PLATFORM "darwin")
else()
    set(WAMR_BUILD_PLATFORM "linux")
endif()

string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _eidos_wamr_cpu)
if(_eidos_wamr_cpu MATCHES "arm64|aarch64")
    set(WAMR_BUILD_TARGET "AARCH64")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(WAMR_BUILD_TARGET "X86_32")
else()
    set(WAMR_BUILD_TARGET "X86_64")
endif()

set(WAMR_ROOT_DIR "${EIDOS_WAMR_ROOT}")
set(WAMR_BUILD_INTERP 1)
set(WAMR_BUILD_FAST_INTERP 1)
set(WAMR_BUILD_AOT 0)
set(WAMR_BUILD_JIT 0)
set(WAMR_BUILD_FAST_JIT 0)
set(WAMR_BUILD_LIBC_BUILTIN 0)
set(WAMR_BUILD_LIBC_WASI 0)
set(WAMR_BUILD_MULTI_MODULE 0)
set(WAMR_BUILD_REF_TYPES 0)
set(WAMR_BUILD_SIMD 0)
set(WAMR_BUILD_INVOKE_NATIVE_GENERAL 1)

include(${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake)

add_library(eidos_wamr STATIC ${WAMR_RUNTIME_LIB_SOURCE})
target_include_directories(eidos_wamr PUBLIC ${WAMR_ROOT_DIR}/core/iwasm/include)
# WAMR's own warnings are its own: the port's warning flags stay off it.
target_compile_options(eidos_wamr PRIVATE -w)
if(WIN32)
    # Winsock and the native API the Windows platform layer calls; the
    # POSIX clock MinGW provides lives in winpthread.
    target_link_libraries(eidos_wamr PUBLIC ws2_32 ntdll)
    if(MINGW)
        target_link_libraries(eidos_wamr PUBLIC -Wl,-Bstatic pthread -Wl,-Bdynamic)
    endif()
else()
    find_package(Threads REQUIRED)
    target_link_libraries(eidos_wamr PUBLIC Threads::Threads m)
    if(NOT APPLE)
        target_link_libraries(eidos_wamr PUBLIC dl)
    endif()
endif()
