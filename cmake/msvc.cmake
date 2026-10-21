#
# cmake/msvc.cmake -- compiler settings for MSVC's cl.exe and for clang-cl.
#
# Included from CMakeLists.txt when MSVC is true, which CMake sets for both
# cl.exe and clang-cl (clang-cl takes cl's command-line syntax).  The goal is
# the same contract the GCC and Clang branch has: one warning set for the
# port's own code, the same set minus the warnings that are inherent to
# reproducing mwccarm's output for aletheia's matched sources, and the same
# meaning of plain `char`.
#
# There is no toolchain file for the pointer width here.  Under MSVC the
# target architecture is chosen by the developer environment, not by a
# compiler flag: a shell set up with `vcvarsall.bat x64` builds LP64 (LLP64,
# strictly: long stays four bytes), one set up with `vcvarsall.bat x64_x86`
# (or `x86`) builds ILP32, and CMakeLists.txt reads CMAKE_SIZEOF_VOID_P either
# way.  Do not pass cmake/ilp32-toolchain.cmake with cl.exe.
#
# Plain `char`.  The DS compiler is pinned to `-char signed` and MSVC's plain
# char is already signed, so nothing is passed.  In particular /J is NOT
# passed: /J makes plain char *unsigned*, the opposite of what the port needs
# (an earlier revision of CMakeLists.txt passed it for exactly the wrong
# reason).  sdk/types.h asserts the signedness at compile time, so a flag
# that flips it fails the build instead of changing behaviour.
#

if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
    set(EIDOS_CLANG_CL ON)
else()
    set(EIDOS_CLANG_CL OFF)
endif()

# /W4 is MSVC's equivalent of -Wall -Wextra (clang-cl maps it to exactly
# that).  /utf-8 makes the source and execution character sets UTF-8, which
# is what every other compiler already assumes; without it cl.exe reads the
# sources in the system code page.
add_compile_options(/W4 /utf-8)

# The CRT's "deprecated" family (C4996): fopen, strcpy, sprintf and the POSIX
# spellings (fileno, strdup) are standard C or POSIX and are what the port
# uses on every platform.  The _s replacements are Annex K, which no other
# libc this port targets implements.  Defining these turns the deprecation
# off at its source rather than hiding every C4996 with /wd.
add_compile_definitions(_CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS)

# C4324, "structure was padded due to alignment specifier": a report, not a
# defect.  A struct that holds a jmp_buf (16-byte aligned on x64) is padded
# to that alignment, which is what the alignment requires; GCC and Clang have
# no warning for it at all.  clang-cl does not emit it.
add_compile_options(/wd4324)

if(NOT EIDOS_CLANG_CL)
    # Implicit narrowing on assignment (C4244, C4242) and size_t to a smaller
    # integer (C4267).  GCC and Clang only report these under -Wconversion,
    # which the GCC/Clang branch does not enable: the port keeps one warning
    # set on every compiler, so these stay off here too rather than making
    # MSVC alone demand casts the other compilers do not.
    add_compile_options(/wd4244 /wd4242 /wd4267)
endif()

# The matched sources' suppressions, the same three as the GCC/Clang branch
# (see the comment above that branch in CMakeLists.txt): every extern is
# declared locally with the prototype that call site needs, a parameter the
# body does not use keeps the DS calling convention, and a local that is never
# read is sometimes what fixed mwccarm's register allocation.  The matched
# files themselves are never edited (docs/building.md, "Portability rules").
if(EIDOS_CLANG_CL)
    set(EIDOS_MATCHED_COMPILE_OPTIONS
        "-Wno-unused-parameter;-Wno-unused-variable;-Wno-missing-prototypes")
else()
    #   C4100  unreferenced formal parameter       (-Wunused-parameter)
    #   C4101  unreferenced local variable         (-Wunused-variable)
    #   C4189  local initialized but not referenced (-Wunused-variable)
    # and two that only cl.exe reports for these files, both for something
    # mwccarm accepts and the DS build depends on:
    #   C4200  zero-sized array in a struct (`u8 data[0]`, the pre-C99
    #          spelling of a flexible array member; GCC and Clang accept it
    #          silently without -pedantic)
    #   C4245  a negative constant initialising an unsigned object (`u32 x =
    #          -1`); GCC and Clang report it only under -Wsign-conversion,
    #          which no configuration enables
    set(EIDOS_MATCHED_COMPILE_OPTIONS "/wd4100;/wd4101;/wd4189;/wd4200;/wd4245")
endif()

# A few matched SDK files carry mwccarm's `#pragma unused(x)`, which no host
# compiler knows (C4068 under cl.exe, -Wunknown-pragmas under clang-cl, the
# same flag the GCC/Clang branch passes for those files).  The pragma only
# silences an unused-parameter report that the options above already
# silence, so ignoring it changes nothing.
if(EIDOS_CLANG_CL)
    set(EIDOS_MATCHED_PRAGMA_OPTION "-Wno-unknown-pragmas")
else()
    set(EIDOS_MATCHED_PRAGMA_OPTION "/wd4068")
endif()
