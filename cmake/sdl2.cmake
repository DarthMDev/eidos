#
# cmake/sdl2.cmake -- find SDL2 for the platform layer, on every toolchain.
#
# Included from CMakeLists.txt.  SDL2 is needed only by engine/platform/ and
# by the eidos_view demo; the engine library, the shim, the stubs and every
# other test build and run without it, so it is looked for rather than
# required, and everything that depends on it is skipped when it is absent.
#
# Out of this file come:
#
#   eidos_sdl2           an INTERFACE target carrying SDL2's include
#                        directory and library, when found
#   EIDOS_SDL_FOUND      ON when the platform layer can be built
#   EIDOS_SDL_ORIGIN     where it came from, for the configure log
#   EIDOS_SDL_RUNTIME    the SDL2.dll to copy next to eidos_view (Windows)
#
# The places tried, in this order:
#
#   1. the per-user cache tools/fetch_sdl.py fills, outside the repository
#      (%LOCALAPPDATA%\eidos\sdl on Windows; see the script).  The flavour is
#      the one this toolchain links: `vc` for cl.exe and clang-cl, `mingw`
#      for MinGW-w64, `source` (a build of the source release made with this
#      very compiler) for everything else.  Asking costs nothing and never
#      touches the network.
#   2. third_party/SDL2-<version>/<triplet> inside the checkout, where an
#      earlier fetch_sdl.py unpacked the MinGW package.  Kept so a checkout
#      that already has it keeps building; new fetches go to the cache.
#   3. an SDL2 CMake package (find_package), which is what a system package
#      manager, Homebrew or vcpkg installs, or -DSDL2_DIR names.
#   4. pkg-config's sdl2.pc.
#   5. with -DEIDOS_SDL_FETCH=ON only: run tools/fetch_sdl.py now (download,
#      verify, and for the `source` flavour build), then step 1 again.
#
# Why step 5 is opt-in rather than automatic.  A configure that silently
# reaches the network is wrong for the cases that matter most for
# reproducibility: distribution packaging and CI sandboxes forbid it, an
# offline machine would stall on a five-minute timeout, and a build should
# not change what it links because a download happened to succeed.  The
# explicit fetch is one command, and once it has run, step 1 finds the
# result automatically from every checkout and worktree on the machine.
# tools/check.py passes -DEIDOS_SDL_FETCH=ON for the Windows configurations,
# so the full matrix is one command on a fresh machine.
#
# Whatever is found is then compiled and linked against once, at this
# target's pointer width, before it is trusted (the probe at the end).
#

option(EIDOS_SDL "Build the SDL2 platform layer and the eidos_view demo" ON)
option(EIDOS_SDL_FETCH
       "Download (and for the source flavour build) SDL2 at configure time when it is not found"
       OFF)
set(EIDOS_SDL_FLAVOR "" CACHE STRING
    "Which tools/fetch_sdl.py flavour to use: vc, mingw or source (empty: by toolchain)")
set_property(CACHE EIDOS_SDL_FLAVOR PROPERTY STRINGS "" vc mingw source)

set(EIDOS_SDL_FOUND OFF)
set(EIDOS_SDL_ORIGIN "")
set(EIDOS_SDL_RUNTIME "")

set(_sdl_fetch ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/fetch_sdl.py)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(_sdl_arch x64)
    set(_sdl_triplet x86_64-w64-mingw32)
else()
    set(_sdl_arch x86)
    set(_sdl_triplet i686-w64-mingw32)
endif()

if(EIDOS_SDL_FLAVOR)
    set(_sdl_flavor "${EIDOS_SDL_FLAVOR}")
elseif(MSVC)
    set(_sdl_flavor vc)
elseif(WIN32 AND CMAKE_C_COMPILER_ID STREQUAL "GNU")
    set(_sdl_flavor mingw)
else()
    set(_sdl_flavor source)
endif()

# A source build is only good for the compiler, flags and ABI it was built
# with, so its install prefix is named after them.
string(SHA1 _sdl_hash "${CMAKE_C_COMPILER}|${CMAKE_TOOLCHAIN_FILE}|${CMAKE_C_FLAGS}|${CMAKE_SIZEOF_VOID_P}")
string(SUBSTRING "${_sdl_hash}" 0 10 _sdl_hash)
string(TOLOWER "${CMAKE_SYSTEM_NAME}-${CMAKE_C_COMPILER_ID}-${_sdl_arch}-${_sdl_hash}" _sdl_tag)

# Use one prefix out of the cache: the flavour's files become eidos_sdl2.
function(_eidos_sdl_use_prefix pre origin)
    if(_sdl_flavor STREQUAL "source")
        find_package(SDL2 CONFIG QUIET PATHS "${pre}" NO_DEFAULT_PATH)
        if(NOT TARGET SDL2::SDL2)
            return()
        endif()
        add_library(eidos_sdl2 INTERFACE)
        target_link_libraries(eidos_sdl2 INTERFACE SDL2::SDL2)
        set(_sdl_probe_incs "" PARENT_SCOPE)
        set(_sdl_probe_libs SDL2::SDL2 PARENT_SCOPE)
        if(EXISTS "${pre}/bin/SDL2.dll")
            set(EIDOS_SDL_RUNTIME "${pre}/bin/SDL2.dll" PARENT_SCOPE)
        endif()
    else()
        if(_sdl_flavor STREQUAL "vc")
            set(inc "${pre}/include")
            set(lib "${pre}/lib/${_sdl_arch}/SDL2.lib")
            set(dll "${pre}/lib/${_sdl_arch}/SDL2.dll")
        else()
            set(inc "${pre}/include/SDL2")
            set(lib "${pre}/lib/libSDL2.dll.a")
            set(dll "${pre}/bin/SDL2.dll")
        endif()
        add_library(eidos_sdl2 INTERFACE)
        target_include_directories(eidos_sdl2 INTERFACE "${inc}")
        # Only SDL2 itself: engine/platform/platform.c defines
        # SDL_MAIN_HANDLED and calls SDL_SetMainReady, so eidos keeps its own
        # main() and needs neither SDL2main nor the mingw32 stub.
        target_link_libraries(eidos_sdl2 INTERFACE "${lib}")
        set(_sdl_probe_incs "${inc}" PARENT_SCOPE)
        set(_sdl_probe_libs "${lib}" PARENT_SCOPE)
        set(EIDOS_SDL_RUNTIME "${dll}" PARENT_SCOPE)
    endif()
    set(EIDOS_SDL_FOUND ON PARENT_SCOPE)
    set(EIDOS_SDL_ORIGIN "${origin}" PARENT_SCOPE)
endfunction()

# Step 1, and step 5's second look.
function(_eidos_sdl_from_cache origin)
    if(_sdl_flavor STREQUAL "source")
        set(args --locate source --tag ${_sdl_tag})
    else()
        set(args --locate ${_sdl_flavor} --arch ${_sdl_arch})
    endif()
    execute_process(COMMAND ${_sdl_fetch} ${args}
                    RESULT_VARIABLE rc OUTPUT_VARIABLE pre ERROR_QUIET
                    OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(rc EQUAL 0 AND pre)
        _eidos_sdl_use_prefix("${pre}" "${origin} (${_sdl_flavor}, ${pre})")
        set(EIDOS_SDL_FOUND "${EIDOS_SDL_FOUND}" PARENT_SCOPE)
        set(EIDOS_SDL_ORIGIN "${EIDOS_SDL_ORIGIN}" PARENT_SCOPE)
        set(EIDOS_SDL_RUNTIME "${EIDOS_SDL_RUNTIME}" PARENT_SCOPE)
        set(_sdl_probe_incs "${_sdl_probe_incs}" PARENT_SCOPE)
        set(_sdl_probe_libs "${_sdl_probe_libs}" PARENT_SCOPE)
    endif()
endfunction()

if(EIDOS_SDL)
    # 1. the per-user cache
    _eidos_sdl_from_cache("fetch_sdl.py cache")

    # 2. an older fetch_sdl.py's tree inside the checkout (MinGW only)
    if(NOT EIDOS_SDL_FOUND AND _sdl_flavor STREQUAL "mingw")
        file(GLOB _sdl_trees "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL2-*")
        foreach(_tree ${_sdl_trees})
            set(_cand "${_tree}/${_sdl_triplet}")
            if(NOT EIDOS_SDL_FOUND
               AND EXISTS "${_cand}/include/SDL2/SDL.h"
               AND EXISTS "${_cand}/lib/libSDL2.dll.a"
               AND EXISTS "${_cand}/bin/SDL2.dll")
                _eidos_sdl_use_prefix("${_cand}" "${_cand}")
            endif()
        endforeach()
    endif()

    # 3. a CMake package: system, Homebrew, vcpkg, or -DSDL2_DIR
    if(NOT EIDOS_SDL_FOUND)
        find_package(SDL2 QUIET)
        if(SDL2_FOUND)
            add_library(eidos_sdl2 INTERFACE)
            if(TARGET SDL2::SDL2)
                target_link_libraries(eidos_sdl2 INTERFACE SDL2::SDL2)
                set(_sdl_probe_incs "")
                set(_sdl_probe_libs SDL2::SDL2)
            else()
                target_include_directories(eidos_sdl2 INTERFACE ${SDL2_INCLUDE_DIRS})
                target_link_libraries(eidos_sdl2 INTERFACE ${SDL2_LIBRARIES})
                set(_sdl_probe_incs ${SDL2_INCLUDE_DIRS})
                set(_sdl_probe_libs ${SDL2_LIBRARIES})
            endif()
            set(EIDOS_SDL_FOUND ON)
            set(EIDOS_SDL_ORIGIN "CMake package ${SDL2_DIR}")
        endif()
    endif()

    # 4. pkg-config
    if(NOT EIDOS_SDL_FOUND)
        find_package(PkgConfig QUIET)
        if(PKG_CONFIG_FOUND)
            pkg_check_modules(SDL2PC QUIET sdl2)
            if(SDL2PC_FOUND)
                add_library(eidos_sdl2 INTERFACE)
                target_include_directories(eidos_sdl2 INTERFACE ${SDL2PC_INCLUDE_DIRS})
                target_link_libraries(eidos_sdl2 INTERFACE ${SDL2PC_LIBRARIES})
                target_link_directories(eidos_sdl2 INTERFACE ${SDL2PC_LIBRARY_DIRS})
                set(_sdl_probe_incs ${SDL2PC_INCLUDE_DIRS})
                set(_sdl_probe_libs ${SDL2PC_LINK_LIBRARIES})
                set(EIDOS_SDL_FOUND ON)
                set(EIDOS_SDL_ORIGIN "pkg-config sdl2")
            endif()
        endif()
    endif()

    # 5. fetch now, when asked to
    if(NOT EIDOS_SDL_FOUND AND EIDOS_SDL_FETCH)
        if(_sdl_flavor STREQUAL "source")
            # The same compiler, toolchain file, flags and generator as this
            # build, so the library has this build's ABI.
            set(_sdl_cmake_args "--cmake-arg=-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}")
            if(CMAKE_TOOLCHAIN_FILE)
                list(APPEND _sdl_cmake_args
                     "--cmake-arg=-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}"
                     "--cmake-arg=-DALETHEIA_ROOT=${ALETHEIA_ROOT}")
            endif()
            if(CMAKE_C_FLAGS)
                list(APPEND _sdl_cmake_args
                     "--cmake-arg=-DCMAKE_C_FLAGS=${CMAKE_C_FLAGS}")
            endif()
            if(CMAKE_MAKE_PROGRAM)
                list(APPEND _sdl_cmake_args
                     "--cmake-arg=-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}")
            endif()
            set(_sdl_fetch_args --flavor source --build --tag ${_sdl_tag}
                --generator ${CMAKE_GENERATOR} ${_sdl_cmake_args})
        else()
            set(_sdl_fetch_args --flavor ${_sdl_flavor})
        endif()
        message(STATUS "SDL2: not found, running tools/fetch_sdl.py "
                       "(EIDOS_SDL_FETCH=ON)")
        execute_process(COMMAND ${_sdl_fetch} ${_sdl_fetch_args}
                        RESULT_VARIABLE _sdl_rc)
        if(_sdl_rc EQUAL 0)
            _eidos_sdl_from_cache("fetched by fetch_sdl.py")
        else()
            message(STATUS "SDL2: tools/fetch_sdl.py failed (${_sdl_rc})")
        endif()
    endif()
endif()

# A package that was found is not necessarily one this target can link: on
# a 64-bit Linux host configured with cmake/ilp32-toolchain.cmake, pkg-config
# happily reports the 64-bit SDL2, whose headers then fail to compile under
# -m32 (the multiarch SDL_config.h forwards to a per-ABI header that is not
# installed) or whose library fails to link; the x64 VC library in an x86
# developer prompt fails the same way.  Try it, at this target's pointer
# width, before committing to it.
if(EIDOS_SDL_FOUND)
    include(CheckCSourceCompiles)
    # The answer is cached under this name; drop it so a different SDL2 (a
    # fetch since the last configure) is probed afresh.
    unset(EIDOS_SDL_LINKS CACHE)
    # try_compile takes imported targets and plain paths, not the ordinary
    # INTERFACE target above, so the probe is handed what went into it.
    set(CMAKE_REQUIRED_INCLUDES ${_sdl_probe_incs})
    set(CMAKE_REQUIRED_LIBRARIES ${_sdl_probe_libs})
    set(CMAKE_REQUIRED_QUIET ON)
    check_c_source_compiles("
#define SDL_MAIN_HANDLED
#include <SDL.h>
int main(void) { SDL_SetMainReady(); return SDL_Init(0) == 0 ? 0 : 1; }
" EIDOS_SDL_LINKS)
    unset(CMAKE_REQUIRED_INCLUDES)
    unset(CMAKE_REQUIRED_LIBRARIES)
    unset(CMAKE_REQUIRED_QUIET)
    if(NOT EIDOS_SDL_LINKS)
        message(STATUS "SDL2: ${EIDOS_SDL_ORIGIN} found, but a test program "
                       "does not compile and link against it for this "
                       "target (${CMAKE_SIZEOF_VOID_P}-byte pointers)")
        set(EIDOS_SDL_FOUND OFF)
        set(EIDOS_SDL_ORIGIN "")
        set(EIDOS_SDL_RUNTIME "")
    endif()
endif()

if(EIDOS_SDL_FOUND)
    message(STATUS "SDL2: ${EIDOS_SDL_ORIGIN}")
elseif(EIDOS_SDL)
    message(STATUS "SDL2: not found; the platform layer and eidos_view are "
                   "skipped (python tools/fetch_sdl.py --flavor ${_sdl_flavor}, "
                   "-DEIDOS_SDL_FETCH=ON, or install SDL2)")
else()
    message(STATUS "SDL2: off (EIDOS_SDL=OFF)")
endif()
