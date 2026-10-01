/*
 * tests/msvc_no_attribute.h -- forced into one matched CARD source under
 * cl.exe only (test_backup's CMake entry).
 *
 * func_02076540.c declares the bounce buffer with GCC's
 * `__attribute__((aligned(32)))`, the DS's cache-line alignment, which
 * mwccarm and every other compiler the port uses accept and cl.exe does
 * not.  The alignment means nothing on a host without the DS's cache, so
 * the attribute is dropped.  A function-like macro cannot be passed as a
 * CMake compile definition, hence a header.
 */

#ifndef BW_TEST_MSVC_NO_ATTRIBUTE_H
#define BW_TEST_MSVC_NO_ATTRIBUTE_H

#define __attribute__(x)

#endif
