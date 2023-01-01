include(CMakeParseArguments)

function(find_msvc_crt_msm OUTPUT_VAR)
    if(${OUTPUT_VAR})
        return()
    endif()

    message(STATUS "Looking for a CRT MSM:")

    set(MSM_FILE "Microsoft_VC${MSVC_TOOLSET_VERSION}_CRT_${ARCH}.msm")
    message(STATUS "  * File name: ${MSM_FILE}")

    set(VC_DIR "${CMAKE_CXX_COMPILER}")
    while(VC_DIR AND NOT VC_DIR MATCHES "/VC$")
        get_filename_component(VC_DIR "${VC_DIR}" DIRECTORY)
    endwhile()
    message(STATUS "  * VC directory: ${VC_DIR}")

    file(GLOB VC_VER_DIRS "${VC_DIR}/Redist/MSVC/*")
    message(STATUS "  * Redist directories: ${VC_VER_DIRS}")

    set(CMN_PF_DIR "CommonProgramFiles(x86)")
    find_file(${OUTPUT_VAR}
        NAMES "${MSM_FILE}"
        PATHS
            ${VC_VER_DIRS}
            $ENV{${CMN_PF_DIR}}
        PATH_SUFFIXES
            "MergeModules"
            "Merge Modules")
    message(STATUS "  * Result: ${${OUTPUT_VAR}}")

    set(${OUTPUT_VAR} "${${OUTPUT_VAR}}" PARENT_SCOPE)
endfunction()

function(wix_heat OUTPUT_FILE SOURCE_DIR CG_NAME DR_NAME VAR_NAME)
    cmake_parse_arguments(HEAT "" "XSL_TRANSFORM" "" ${ARGN})

    if(NOT IS_ABSOLUTE "${OUTPUT_FILE}")
        set(OUTPUT_FILE "${CMAKE_CURRENT_BINARY_DIR}/${OUTPUT_FILE}")
    endif()

    set(OPTIONS -nologo -sfrag -ag -srd -indent 2 -cg "${CG_NAME}" -dr "${DR_NAME}" -var "${VAR_NAME}")
    set(DEPENDS "${SOURCE_DIR}")
    if(HEAT_XSL_TRANSFORM)
        if(NOT IS_ABSOLUTE "${HEAT_XSL_TRANSFORM}")
            set(HEAT_XSL_TRANSFORM "${CMAKE_CURRENT_SOURCE_DIR}/${HEAT_XSL_TRANSFORM}")
        endif()
        list(APPEND OPTIONS -t "${HEAT_XSL_TRANSFORM}")
        list(APPEND DEPENDS "${HEAT_XSL_TRANSFORM}")
    endif()

    add_custom_command(
        OUTPUT "${OUTPUT_FILE}"
        COMMAND heat dir "${SOURCE_DIR}" ${OPTIONS} -out "${OUTPUT_FILE}"
        DEPENDS ${DEPENDS})

    list(APPEND ${OUTPUT_VAR} "${OUTPUT_FILE}")
    set(${OUTPUT_VAR} "${${OUTPUT_VAR}}" PARENT_SCOPE)
endfunction()

function(wix_candle OUTPUT_VAR)
    cmake_parse_arguments(CANDLE "" "ARCHITECTURE" "SOURCES;EXTENSIONS;DEFINITIONS;EXTRA_DEPENDS" ${ARGN})

    set(OPTIONS -nologo -pedantic -arch "${CANDLE_ARCHITECTURE}" "-I${CMAKE_CURRENT_BINARY_DIR}")
    foreach(X ${CANDLE_EXTENSIONS})
        list(APPEND OPTIONS -ext "${X}")
    endforeach()
    foreach(X ${CANDLE_DEFINITIONS})
        list(APPEND OPTIONS "-d${X}")
    endforeach()

    foreach(F ${CANDLE_SOURCES})
        if(NOT IS_ABSOLUTE "${F}")
            set(F "${CMAKE_CURRENT_SOURCE_DIR}/${F}")
        endif()
        get_filename_component(F_NAME "${F}" NAME)
        string(REGEX REPLACE "[.]wxs$" "" F_NAME "${F_NAME}")
        set(CANDLE_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${F_NAME}.wixobj")
        add_custom_command(
            OUTPUT "${CANDLE_OUTPUT}"
            COMMAND candle ${OPTIONS} "${F}" -out "${CANDLE_OUTPUT}"
            DEPENDS
                "${F}"
                ${CANDLE_EXTRA_DEPENDS})
        list(APPEND ${OUTPUT_VAR} "${CANDLE_OUTPUT}")
    endforeach()

    set(${OUTPUT_VAR} "${${OUTPUT_VAR}}" PARENT_SCOPE)
endfunction()

function(wix_light OUTPUT_VAR)
    cmake_parse_arguments(LIGHT "" "NAME" "OBJECTS;EXTENSIONS;EXTRA_DEPENDS" ${ARGN})

    set(OPTIONS -nologo -pedantic -sw1076)
    foreach(X ${LIGHT_EXTENSIONS})
        list(APPEND OPTIONS -ext "${X}")
    endforeach()

    set(LIGHT_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${LIGHT_NAME}.msi")
    add_custom_command(
        OUTPUT ${LIGHT_OUTPUT}
        BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/${LIGHT_NAME}.wixpdb"
        COMMAND light ${OPTIONS} -out "${CMAKE_CURRENT_BINARY_DIR}/${LIGHT_NAME}.msi" ${LIGHT_OBJECTS}
        DEPENDS
            ${LIGHT_OBJECTS}
            ${LIGHT_EXTRA_DEPENDS})

    list(APPEND ${OUTPUT_VAR} ${LIGHT_OUTPUT})
    set(${OUTPUT_VAR} "${${OUTPUT_VAR}}" PARENT_SCOPE)
endfunction()
