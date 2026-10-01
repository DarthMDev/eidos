#
# cmake/armdata.cmake -- generate the ARM9 data tables and the forced-include
# header for a set of matched sources (tools/armdata.py, docs/armdata.md).
#
#   eidos_armdata(<prefix>
#       SOURCES_LIST <file>       the build's sources, one path per line
#       [REGION arm9]             or ovNNN (compiled overlays use cmake/overlays.cmake)
#       [MATCHED <src>...])       the matched sources to compile with the
#                                 header force-included
#
# Writes <build>/armdata/<prefix>.c and <prefix>.h and sets <prefix>_C and
# <prefix>_H in the caller's scope.  The .c goes into the target that links
# the matched sources; the .h is force-included into every one of them
# (`-include` / `/FI`), and each of them depends on it, so it is generated
# before they compile and a change to it recompiles them.
#
# The list file is usually written with file(GENERATE) from the targets'
# SOURCES.  A dsaddr copy in it stands for its original in aletheia's
# src/matched/ (the generator maps it back, so it need not exist yet);
# another source the build generates is skipped until it exists, and the
# generator's own outputs are ignored.
#
# Nothing here reads a game byte: the inputs are aletheia's metadata.
#

set(EIDOS_ARMDATA_TOOL ${CMAKE_CURRENT_LIST_DIR}/../tools/armdata.py)
get_filename_component(EIDOS_ARMDATA_TOOL "${EIDOS_ARMDATA_TOOL}" ABSOLUTE)
set(EIDOS_ARMDATA_EXCLUSIONS ${CMAKE_CURRENT_LIST_DIR}/../tools/armdata_exclusions.txt)
get_filename_component(EIDOS_ARMDATA_EXCLUSIONS "${EIDOS_ARMDATA_EXCLUSIONS}" ABSOLUTE)

function(eidos_armdata prefix)
    cmake_parse_arguments(A "" "SOURCES_LIST;REGION" "MATCHED" ${ARGN})
    if(NOT A_SOURCES_LIST)
        message(FATAL_ERROR "eidos_armdata(${prefix}): SOURCES_LIST is required")
    endif()
    if(NOT A_REGION)
        set(A_REGION arm9)
    endif()
    set(dir ${CMAKE_BINARY_DIR}/armdata)
    file(MAKE_DIRECTORY ${dir})
    set(out_c ${dir}/${prefix}.c)
    set(out_h ${dir}/${prefix}.h)

    # The listings are generated in aletheia and may be absent; the
    # generator falls back to data_objects.json alone.
    if(A_REGION STREQUAL "arm9")
        file(GLOB listings ${ALETHEIA_ROOT}/asm/arm9/*.s)
    else()
        file(GLOB listings ${ALETHEIA_ROOT}/asm/overlays/${A_REGION}.s)
    endif()

    add_custom_command(
        OUTPUT ${out_c} ${out_h}
        COMMAND ${Python3_EXECUTABLE} ${EIDOS_ARMDATA_TOOL}
                --aletheia-root ${ALETHEIA_ROOT}
                --sources ${A_SOURCES_LIST}
                --region ${A_REGION}
                --exclusions ${EIDOS_ARMDATA_EXCLUSIONS}
                --out-c ${out_c} --out-h ${out_h}
        DEPENDS ${EIDOS_ARMDATA_TOOL}
                ${EIDOS_ARMDATA_EXCLUSIONS}
                ${A_SOURCES_LIST}
                ${ALETHEIA_ROOT}/layout/layout.json
                ${ALETHEIA_ROOT}/layout/data_objects.json
                ${ALETHEIA_ROOT}/layout/symbols_sdk.txt
                ${listings}
        COMMENT "Generating ${prefix} (ARM9 data names, relocations) from ${ALETHEIA_ROOT}/layout"
        VERBATIM)
    set_source_files_properties(${out_c} ${out_h} PROPERTIES GENERATED TRUE)

    if(A_MATCHED)
        if(MSVC)
            set(opt "/FI${out_h}")
        else()
            set(opt "-include;${out_h}")
        endif()
        set_property(SOURCE ${A_MATCHED} APPEND PROPERTY COMPILE_OPTIONS ${opt})
        set_property(SOURCE ${A_MATCHED} APPEND PROPERTY OBJECT_DEPENDS ${out_h})
    endif()

    set(${prefix}_C ${out_c} PARENT_SCOPE)
    set(${prefix}_H ${out_h} PARENT_SCOPE)
endfunction()
