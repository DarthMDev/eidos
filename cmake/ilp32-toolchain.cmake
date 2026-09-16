#
# cmake/ilp32-toolchain.cmake -- build eidos with 32-bit pointers.
#
# The DS is LP32: `sizeof(void *)` is 4, and every matched struct the game
# owns was laid out by a compiler that knew that.  Configuring eidos with
# this toolchain file reproduces that pointer width on the host, which makes
# every game-owned struct in the build byte-identical in layout to the DS's
# and lets the four EXACT tcbl accessors that reach a field through a DS byte
# offset be compiled AS WRITTEN (see docs/pointer-width.md and the
# EIDOS_TCBL_MATCHED_ACCESSORS option in CMakeLists.txt, which defaults to ON
# exactly when this file, or anything else, has produced a 32-bit target).
#
#   cmake -S . -B build32 -G Ninja \
#       -DCMAKE_TOOLCHAIN_FILE=cmake/ilp32-toolchain.cmake \
#       -DALETHEIA_ROOT=../aletheia
#
# Two ways of getting a 32-bit target are tried, in this order:
#
#   1. A dedicated i686 compiler driver.  On Windows that is
#      i686-w64-mingw32-gcc, which the MinGW-w64 archive in aletheia's
#      tools/gcc-host/ ships next to the x86_64 one; on Linux it is whatever
#      i686-linux-gnu-gcc the distribution installs.  Pass -DALETHEIA_ROOT
#      on the configure line (as the command above does) and this file finds
#      the bundled one without anything being on PATH.
#   2. The generic -m32 flag on whatever compiler is the default.  GCC and
#      Clang both take it; it needs the 32-bit runtime libraries installed,
#      which is a separate package on most Linux distributions.
#
# Setting CMAKE_C_COMPILER explicitly on the command line overrides both.
#
# CMAKE_SYSTEM_NAME is deliberately not set: this is a same-OS, different-ABI
# build, not a cross build, and the resulting executables run and are tested
# on the build machine.  CMake discovers sizeof(void *) == 4 from the
# compiler ABI check before the project's own CMakeLists.txt runs, so
# CMAKE_SIZEOF_VOID_P is already correct where the pointer-width switch
# reads it.
#

if(NOT CMAKE_C_COMPILER)
    set(_ilp32_hints "")
    foreach(_root
            "$ENV{ALETHEIA_ROOT}"
            "${ALETHEIA_ROOT}"
            "${CMAKE_CURRENT_LIST_DIR}/../../aletheia"
            "${CMAKE_CURRENT_LIST_DIR}/../../bw_decomp")
        if(_root)
            file(GLOB _bins "${_root}/tools/gcc-host/*/bin")
            list(APPEND _ilp32_hints ${_bins})
        endif()
    endforeach()

    find_program(EIDOS_ILP32_C_COMPILER
        NAMES i686-w64-mingw32-gcc i686-linux-gnu-gcc i586-linux-gnu-gcc
        HINTS ${_ilp32_hints}
        DOC "A C compiler driver whose default target has 32-bit pointers")

    if(EIDOS_ILP32_C_COMPILER)
        set(CMAKE_C_COMPILER "${EIDOS_ILP32_C_COMPILER}")
        # The same archive ships the matching resource compiler; CMake looks
        # for one on Windows even for a C-only project.
        get_filename_component(_ilp32_bin "${EIDOS_ILP32_C_COMPILER}" DIRECTORY)
        find_program(EIDOS_ILP32_RC_COMPILER
            NAMES i686-w64-mingw32-windres
            HINTS "${_ilp32_bin}")
        if(EIDOS_ILP32_RC_COMPILER)
            set(CMAKE_RC_COMPILER "${EIDOS_ILP32_RC_COMPILER}")
        endif()
    else()
        # No i686 driver: ask the default compiler for the 32-bit ABI.  The
        # flag has to reach the link line too, or the ABI check links 64-bit
        # objects against a 32-bit runtime and fails in a way that looks like
        # a missing compiler rather than a missing multilib.
        set(CMAKE_C_FLAGS_INIT "-m32")
        set(CMAKE_EXE_LINKER_FLAGS_INIT "-m32")
        set(CMAKE_SHARED_LINKER_FLAGS_INIT "-m32")
        set(CMAKE_MODULE_LINKER_FLAGS_INIT "-m32")
    endif()
endif()
