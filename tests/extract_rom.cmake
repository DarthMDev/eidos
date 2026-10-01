# tests/extract_rom.cmake -- eidos_extract on the player's real ROM.
#
#     cmake -DEXTRACT=<eidos_extract> -DREFERENCE=<dir> -DWORK=<dir>
#           -P tests/extract_rom.cmake
#
# The ROM is named by the BW_ROM environment variable at test time; without
# it (or without the reference tree) the test reports itself skipped.
# REFERENCE is the parent of BW_DATA_ROOT: aletheia's own extract/, written
# by tools/ndsrom.py extract and tools/delink.py from the same ROM.  The
# expected hashes are therefore never written down anywhere in the repo:
# they are computed here, from that tree, each run.
#
# Checks:
#   1. eidos_extract identifies the ROM as the known good dump and extracts
#      it into a folder whose name is outside ASCII (the wide-character
#      path on Windows, UTF-8 elsewhere);
#   2. every file it wrote, except its own eidos_manifest.json, exists in
#      REFERENCE with the same size and SHA-1;
#   3. every REFERENCE file it is meant to write is there: all of them but
#      ndsrom.py's rebuild bookkeeping (manifest.json, residue/) and what
#      delink.py derives with a DSi key or for the disassembler
#      (arm9i_dec.bin, ov230d.bin), see docs/installer.md;
#   4. a second run finds the finished extraction and writes nothing;
#   5. no staging directory is left.
# On success the extraction is deleted; on failure it is kept for a look.

foreach(v EXTRACT REFERENCE WORK)
    if(NOT DEFINED ${v})
        message(FATAL_ERROR "extract_rom.cmake: -D${v}= is required")
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
if(NOT EXISTS "${REFERENCE}/header.bin" OR NOT IS_DIRECTORY "${REFERENCE}/data")
    message("SKIPPED: no reference extraction at ${REFERENCE} (the parent of BW_DATA_ROOT)")
    return()
endif()

# "extract é€ ü": spaces and characters outside ASCII and outside Latin-1.
set(OUT "${WORK}/extract é€ ü")
file(REMOVE_RECURSE "${OUT}" "${OUT}.partial" "${OUT}.old")
file(MAKE_DIRECTORY "${WORK}")

# --- 1. extract ------------------------------------------------------------
string(TIMESTAMP t0 "%s")
execute_process(COMMAND "${EXTRACT}" "${ROM}" "${OUT}"
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
string(TIMESTAMP t1 "%s")
math(EXPR secs "${t1} - ${t0}")
message("${out}")
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "eidos_extract failed (${rc}):\n${err}")
endif()
if(NOT out MATCHES "verdict   Pokemon Black, the known good dump")
    message(FATAL_ERROR "the ROM was not identified as the known good dump")
endif()
message("identify + extract: ${secs} s")

# --- 2. everything written matches the reference ---------------------------
file(GLOB_RECURSE written RELATIVE "${OUT}" "${OUT}/*")
list(REMOVE_ITEM written "eidos_manifest.json")
set(bad 0)
set(n 0)
foreach(rel IN LISTS written)
    math(EXPR n "${n} + 1")
    if(NOT EXISTS "${REFERENCE}/${rel}")
        message("  not in the reference: ${rel}")
        math(EXPR bad "${bad} + 1")
        continue()
    endif()
    file(SIZE "${OUT}/${rel}" s1)
    file(SIZE "${REFERENCE}/${rel}" s2)
    if(NOT s1 EQUAL s2)
        message("  size differs: ${rel} (${s1}, reference ${s2})")
        math(EXPR bad "${bad} + 1")
        continue()
    endif()
    file(SHA1 "${OUT}/${rel}" h1)
    file(SHA1 "${REFERENCE}/${rel}" h2)
    if(NOT h1 STREQUAL h2)
        message("  content differs: ${rel}")
        math(EXPR bad "${bad} + 1")
    endif()
endforeach()
message("${n} files written, ${bad} differing from the reference")

# --- 3. nothing the extractor should write is missing ---------------------
file(GLOB_RECURSE expected RELATIVE "${REFERENCE}" "${REFERENCE}/*")
set(missing 0)
foreach(rel IN LISTS expected)
    if(rel STREQUAL "manifest.json" OR rel MATCHES "^residue/"
       OR rel STREQUAL "arm9i_dec.bin" OR rel STREQUAL "ov230d.bin")
        continue()
    endif()
    if(NOT EXISTS "${OUT}/${rel}")
        message("  missing: ${rel}")
        math(EXPR missing "${missing} + 1")
    endif()
endforeach()
message("${missing} reference files missing from the extraction")

# --- 4. a second run is a no-op ---------------------------------------------
execute_process(COMMAND "${EXTRACT}" "${ROM}" "${OUT}"
                RESULT_VARIABLE rc2 OUTPUT_VARIABLE out2 ERROR_VARIABLE err2)
if(NOT rc2 EQUAL 0 OR NOT out2 MATCHES "already extracted")
    message("  second run: ${rc2}\n${out2}\n${err2}")
    math(EXPR bad "${bad} + 1")
endif()

# --- 5. no staging left ------------------------------------------------------
if(EXISTS "${OUT}.partial" OR EXISTS "${OUT}.old")
    message("  a staging or set-aside directory was left behind")
    math(EXPR bad "${bad} + 1")
endif()

if(NOT bad EQUAL 0 OR NOT missing EQUAL 0 OR n EQUAL 0)
    message(FATAL_ERROR "test_extract_rom: FAILED (output kept at ${OUT})")
endif()
file(REMOVE_RECURSE "${OUT}")
message("test_extract_rom: ${n} files identical to the reference, none missing")
