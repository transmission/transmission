include(CheckFunctionExists)
include(CheckIncludeFile)

macro(tr_auto_option_changed NAME ACC VAL FIL STK)
    if(NOT ("${VAL}" STREQUAL "AUTO" OR "${VAL}" STREQUAL "ON" OR "${VAL}" STREQUAL "OFF"))
        if("${VAL}" STREQUAL "0" OR "${VAL}" STREQUAL "NO" OR "${VAL}" STREQUAL "FALSE" OR "${VAL}" STREQUAL "N")
            set_property(CACHE ${NAME} PROPERTY VALUE OFF)
        elseif("${VAL}" MATCHES "^[-+]?[0-9]+$" OR "${VAL}" STREQUAL "YES" OR "${VAL}" STREQUAL "TRUE" OR "${VAL}" STREQUAL "Y")
            set_property(CACHE ${NAME} PROPERTY VALUE ON)
        else()
            message(FATAL_ERROR "Option '${NAME}' set to unrecognized value '${VAL}'. Should be boolean or 'AUTO'.")
        endif()
    endif()
endmacro()

macro(tr_auto_option NAME DESC VAL)
    set(${NAME} "${VAL}"
        CACHE STRING "${DESC}")
    set_property(CACHE ${NAME} PROPERTY STRINGS "AUTO;ON;OFF")
    variable_watch(${NAME} tr_auto_option_changed)
endmacro()

macro(tr_fixup_auto_option NAME ISFOUND ISREQ)
    if(${ISFOUND})
        set_property(CACHE ${NAME} PROPERTY VALUE ON)
    elseif(NOT (${ISREQ}))
        set_property(CACHE ${NAME} PROPERTY VALUE OFF)
    endif()
endmacro()

function(tr_list_option_changed NAME ACC VAL FIL STK)
    get_property(VAR_STRINGS CACHE ${NAME} PROPERTY STRINGS)
    string(TOUPPER "${VAL}" VAL_UPCASE)
    foreach(X ${VAR_STRINGS})
        string(TOUPPER "${X}" X_UPCASE)
        if("${VAL_UPCASE}" STREQUAL "${X_UPCASE}")
            if(NOT "${VAL}" STREQUAL "${X}")
                set_property(CACHE ${NAME} PROPERTY VALUE "${X}")
                message(STATUS ">>> (list) ${NAME} -> ${X}")
            endif()
            return()
        endif()
    endforeach()
    string(REPLACE ";" "', '" VAR_STRINGS "${VAR_STRINGS}")
    message(FATAL_ERROR "Option '${NAME}' set to unrecognized value '${VAL}'. Should be one of '${VAR_STRINGS}'.")
endfunction()

macro(tr_list_option NAME DESC VAL)
    set(${NAME} "${VAL}" CACHE STRING "${DESC}")
    set_property(CACHE ${NAME} PROPERTY STRINGS "${VAL};${ARGN}")
    variable_watch(${NAME} tr_list_option_changed)
endmacro()

macro(tr_fixup_list_option NAME FVAL ISFOUND RVAL ISREQ)
    if(${ISFOUND})
        set_property(CACHE ${NAME} PROPERTY VALUE "${FVAL}")
    elseif(NOT (${ISREQ}))
        set_property(CACHE ${NAME} PROPERTY VALUE "${RVAL}")
    endif()
endmacro()

macro(tr_get_required_flag IVAR OVAR)
    set(${OVAR})
    if(${IVAR} AND NOT ${IVAR} STREQUAL "AUTO")
        set(${OVAR} REQUIRED)
    endif()
endmacro()

function(tr_make_id INPUT OVAR)
    string(TOUPPER "${INPUT}" ID)
    string(REGEX REPLACE "[^A-Z0-9]+" "_" ID "${ID}")
    # string(REGEX REPLACE "^_+|_+$" "" ID "${ID}")
    set(${OVAR} "${ID}" PARENT_SCOPE)
endfunction()

macro(tr_add_external_auto_library ID DIRNAME LIBNAME)
    if(USE_SYSTEM_${ID})
        tr_get_required_flag(USE_SYSTEM_${ID} SYSTEM_${ID}_IS_REQUIRED)
        find_package(${ID} ${${ID}_MINIMUM} ${SYSTEM_${ID}_IS_REQUIRED})
        tr_fixup_auto_option(USE_SYSTEM_${ID} ${ID}_FOUND SYSTEM_${ID}_IS_REQUIRED)
    endif()

    if(USE_SYSTEM_${ID})
        unset(${ID}_UPSTREAM_TARGET)
    else()
        set(${ID}_UPSTREAM_TARGET ${LIBNAME})
        set(${ID}_PREFIX "${CMAKE_BINARY_DIR}/third-party/${${ID}_UPSTREAM_TARGET}")

        set(${ID}_INCLUDE_DIR "${${ID}_PREFIX}/include"
            CACHE INTERNAL "")
        set(${ID}_LIBRARY "${${ID}_PREFIX}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}${LIBNAME}${CMAKE_STATIC_LIBRARY_SUFFIX}"
            CACHE INTERNAL "")

        set(${ID}_INCLUDE_DIRS ${${ID}_INCLUDE_DIR})
        set(${ID}_LIBRARIES ${${ID}_LIBRARY})

        set(${ID}_EXT_PROJ_CMAKE_ARGS)
        if(APPLE)
            string(REPLACE ";" "$<SEMICOLON>" ${ID}_CMAKE_OSX_ARCHITECTURES "${CMAKE_OSX_ARCHITECTURES}")
            list(APPEND ${ID}_EXT_PROJ_CMAKE_ARGS
                "-DCMAKE_OSX_ARCHITECTURES:STRING=${${ID}_CMAKE_OSX_ARCHITECTURES}"
                "-DCMAKE_OSX_DEPLOYMENT_TARGET:STRING=${CMAKE_OSX_DEPLOYMENT_TARGET}"
                "-DCMAKE_OSX_SYSROOT:PATH=${CMAKE_OSX_SYSROOT}")
        endif()

        ExternalProject_Add(
            ${${ID}_UPSTREAM_TARGET}
            URL "${CMAKE_SOURCE_DIR}/third-party/${DIRNAME}"
            ${ARGN}
            PREFIX "${${ID}_PREFIX}"
            CMAKE_ARGS
                -Wno-dev # We don't want to be warned over unused variables
                "-DCMAKE_TOOLCHAIN_FILE:PATH=${CMAKE_TOOLCHAIN_FILE}"
                "-DCMAKE_USER_MAKE_RULES_OVERRIDE=${CMAKE_USER_MAKE_RULES_OVERRIDE}"
                "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
                "-DCMAKE_C_FLAGS:STRING=${CMAKE_C_FLAGS}"
                "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
                "-DCMAKE_CXX_FLAGS:STRING=${CMAKE_CXX_FLAGS}"
                "-DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}"
                "-DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR>"
                "-DCMAKE_INSTALL_LIBDIR:STRING=lib"
                ${${ID}_EXT_PROJ_CMAKE_ARGS}
            BUILD_BYPRODUCTS "${${ID}_LIBRARY}")

        set_property(TARGET ${${ID}_UPSTREAM_TARGET} PROPERTY FOLDER "third-party")
    endif()
endmacro()

function(tr_append_target_property TGT PROP VAL)
    get_target_property(OVAL ${TGT} ${PROP})
    if(OVAL)
        set(VAL "${OVAL} ${VAL}")
    endif()
    set_target_properties(${TGT} PROPERTIES ${PROP} "${VAL}")
endfunction()

function(tr_target_compile_definitions_for_headers TGT)
    cmake_parse_arguments(ARG "" "" "PRIVATE;PUBLIC" ${ARGN})
    foreach(VISIBILITY IN ITEMS PRIVATE PUBLIC)
        foreach(H IN LISTS ARG_${VISIBILITY})
            tr_make_id("HAVE_${H}" H_ID)
            check_include_file(${H} ${H_ID})
            target_compile_definitions(${TGT}
                ${VISIBILITY}
                    $<$<BOOL:${${H_ID}}>:${H_ID}>)
        endforeach()
    endforeach()
endfunction()

function(tr_target_compile_definitions_for_functions TGT)
    cmake_parse_arguments(ARG "" "" "PRIVATE;PUBLIC;REQUIRED_LIBS" ${ARGN})
    set(CMAKE_REQUIRED_LIBRARIES "${ARG_REQUIRED_LIBS}")
    foreach(VISIBILITY IN ITEMS PRIVATE PUBLIC)
        foreach(F IN LISTS ARG_${VISIBILITY})
            tr_make_id("HAVE_${F}" F_ID)
            check_function_exists(${F} ${F_ID})
            target_compile_definitions(${TGT}
                ${VISIBILITY}
                    $<$<BOOL:${${F_ID}}>:${F_ID}>)
        endforeach()
    endforeach()
endfunction()

function(tr_disable_source_files_compile)
    set_source_files_properties(
        ${ARGN}
        PROPERTIES
            HEADER_FILE_ONLY ON)
endfunction()

function(tr_win32_app_info TGT DESCR INTNAME ORIGFNAME)
    if(NOT WIN32)
        return()
    endif()

    set(TR_FILE_DESCRIPTION "${DESCR}")
    set(TR_INTERNAL_NAME "${INTNAME}")
    set(TR_ORIGINAL_FILENAME "${ORIGFNAME}")
    if(ARGN)
        set(TR_MAIN_ICON "${ARGN}")
    endif()

    configure_file("${CMAKE_SOURCE_DIR}/cmake/Transmission.rc.in" "${INTNAME}-app-info.rc")

    target_sources(${TGT}
        PRIVATE
            "${CMAKE_CURRENT_BINARY_DIR}/${INTNAME}-app-info.rc")
endfunction()

function(tr_select_library LIBNAMES FUNCNAME DIRS OVAR)
    set(LIBNAME)
    foreach(X ${LIBNAMES})
        set(VAR_NAME "HAVE_${FUNCNAME}_IN_LIB${X}")
        string(TOUPPER "${VAR_NAME}" VAR_NAME)
        check_library_exists("${X}" "${FUNCNAME}" "${DIRS}" ${VAR_NAME})
        if(${VAR_NAME})
            set(LIBNAME "${X}")
            break()
        endif()
    endforeach()
    set(${OVAR} "${LIBNAME}" PARENT_SCOPE)
endfunction()

function(tr_fixup_bundle_item BUNDLE_DIR BUNDLE_ITEMS DEP_DIRS)
    while(BUNDLE_ITEMS)
        list(GET BUNDLE_ITEMS 0 ITEM)
        list(REMOVE_AT BUNDLE_ITEMS 0)

        set(ITEM_FULL_BUNDLE_PATH "${BUNDLE_DIR}/${ITEM}")
        get_filename_component(ITEM_FULL_BUNDLE_DIR "${ITEM_FULL_BUNDLE_PATH}" PATH)

        unset(ITEM_DEPS)
        get_prerequisites("${ITEM_FULL_BUNDLE_PATH}" ITEM_DEPS 1 0 "${ITEM_FULL_BUNDLE_PATH}" "${DEP_DIRS}")

        foreach(DEP IN LISTS ITEM_DEPS)
            gp_resolve_item("${ITEM_FULL_BUNDLE_PATH}" "${DEP}" "${ITEM_FULL_BUNDLE_DIR}" "${DEP_DIRS}" DEP_FULL_PATH)

            if(DEP_FULL_PATH MATCHES "[.]dylib$")
                get_filename_component(DEP_NAME "${DEP_FULL_PATH}" NAME)
                file(COPY "${DEP_FULL_PATH}" DESTINATION "${BUNDLE_DIR}/Contents/MacOS/")
                set(DEP_BUNDLE_PATH "Contents/MacOS/${DEP_NAME}")
            elseif(DEP_FULL_PATH MATCHES "^(.+)/(([^/]+[.]framework)/.+)$")
                set(DEP_NAME "${CMAKE_MATCH_2}")
                file(
                    COPY "${CMAKE_MATCH_1}/${CMAKE_MATCH_3}"
                    DESTINATION "${BUNDLE_DIR}/Contents/Frameworks/"
                    PATTERN "Headers" EXCLUDE)
                set(DEP_BUNDLE_PATH "Contents/Frameworks/${DEP_NAME}")
            else()
                message(FATAL_ERROR "Don't know how to fixup '${DEP_FULL_PATH}'")
            endif()

            execute_process(COMMAND install_name_tool -change "${DEP}" "@rpath/${DEP_NAME}" "${ITEM_FULL_BUNDLE_PATH}")

            set(DEP_FULL_BUNDLE_PATH "${BUNDLE_DIR}/${DEP_BUNDLE_PATH}")
            execute_process(COMMAND chmod u+w "${DEP_FULL_BUNDLE_PATH}")
            execute_process(COMMAND install_name_tool -id "@rpath/${DEP_NAME}" "${DEP_FULL_BUNDLE_PATH}")

            list(REMOVE_ITEM BUNDLE_ITEMS "${DEP_BUNDLE_PATH}")
            list(APPEND BUNDLE_ITEMS "${DEP_BUNDLE_PATH}")
        endforeach()
    endwhile()
endfunction()

function(tr_glib_compile_resources TGT NAME INPUT_DIR INPUT_FILE OUTPUT_FILE_BASE)
    if(NOT GLIB_COMPILE_RESOURCES_EXECUTABLE)
        execute_process(
            COMMAND ${PKG_CONFIG_EXECUTABLE} gio-2.0 --variable glib_compile_resources
            OUTPUT_VARIABLE GLIB_COMPILE_RESOURCES_EXECUTABLE
            OUTPUT_STRIP_TRAILING_WHITESPACE)

        if(NOT GLIB_COMPILE_RESOURCES_EXECUTABLE)
            message(SEND_ERROR "Unable to find glib-compile-resources executable")
        endif()

        set(GLIB_COMPILE_RESOURCES_EXECUTABLE "${GLIB_COMPILE_RESOURCES_EXECUTABLE}"
            CACHE STRING "glib-compile-resources executable")
    endif()

    set_property(
        DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
            "${INPUT_DIR}/${INPUT_FILE}")

    file(STRINGS "${INPUT_DIR}/${INPUT_FILE}" INPUT_LINES)
    set(OUTPUT_DEPENDS)
    foreach(INPUT_LINE IN LISTS INPUT_LINES)
        if(INPUT_LINE MATCHES ">([^<]+)</file>")
            list(APPEND OUTPUT_DEPENDS "${INPUT_DIR}/${CMAKE_MATCH_1}")
        endif()
    endforeach()

    add_custom_command(
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${OUTPUT_FILE_BASE}.c"
        COMMAND
            "${GLIB_COMPILE_RESOURCES_EXECUTABLE}"
            "--target=${CMAKE_CURRENT_BINARY_DIR}/${OUTPUT_FILE_BASE}.c"
            "--sourcedir=${INPUT_DIR}"
            --generate-source
            --c-name "${NAME}"
            "${INPUT_DIR}/${INPUT_FILE}"
        DEPENDS
            "${INPUT_DIR}/${INPUT_FILE}"
            ${OUTPUT_DEPENDS}
        WORKING_DIRECTORY "${INPUT_DIR}")

    add_custom_command(
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${OUTPUT_FILE_BASE}.h"
        COMMAND
            "${GLIB_COMPILE_RESOURCES_EXECUTABLE}"
            "--target=${CMAKE_CURRENT_BINARY_DIR}/${OUTPUT_FILE_BASE}.h"
            "--sourcedir=${INPUT_DIR}"
            --generate-header
            --c-name "${NAME}"
            "${INPUT_DIR}/${INPUT_FILE}"
        DEPENDS
            "${INPUT_DIR}/${INPUT_FILE}"
            ${OUTPUT_DEPENDS}
        WORKING_DIRECTORY "${INPUT_DIR}")

    target_sources(${TGT}
        PRIVATE
            "${INPUT_DIR}/${INPUT_FILE}"
            "${CMAKE_CURRENT_BINARY_DIR}/${OUTPUT_FILE_BASE}.c"
            "${CMAKE_CURRENT_BINARY_DIR}/${OUTPUT_FILE_BASE}.h")

    source_group("Generated Files"
        FILES
            "${CMAKE_CURRENT_BINARY_DIR}/${OUTPUT_FILE_BASE}.c"
            "${CMAKE_CURRENT_BINARY_DIR}/${OUTPUT_FILE_BASE}.h")
endfunction()

function(tr_target_glib_resources TGT)
    foreach(ARG IN LISTS ARGN)
        get_filename_component(ARG_PATH "${ARG}" ABSOLUTE)
        string(SHA1 ARG_HASH "${ARG_PATH}")
        string(SUBSTRING "${ARG_HASH}" 0 10 ARG_HASH)

        get_filename_component(ARG_NAME_WE "${ARG}" NAME_WE)
        string(MAKE_C_IDENTIFIER "${ARG_NAME_WE}" ARG_ID)

        get_filename_component(ARG_DIR "${ARG_PATH}" DIRECTORY)
        get_filename_component(ARG_NAME "${ARG}" NAME)

        tr_glib_compile_resources(${TGT}
            "${ARG_ID}_${ARG_HASH}"
            "${ARG_DIR}"
            "${ARG_NAME}"
            "${ARG_ID}-${ARG_HASH}")
    endforeach()
endfunction()

function(tr_gettext_msgfmt TGT OUTPUT_FILE INPUT_FILE)
    get_filename_component(OUTPUT_FILE_EXT "${OUTPUT_FILE}" LAST_EXT)
    if(OUTPUT_FILE_EXT STREQUAL ".desktop")
        set(MODE_ARG "--desktop")
    elseif(OUTPUT_FILE_EXT STREQUAL ".xml")
        set(MODE_ARG "--xml")
    else()
        message(FATAL_ERROR "Unsupported output file extension: '${OUTPUT_FILE_EXT}'")
    endif()

    add_custom_command(
        OUTPUT ${OUTPUT_FILE}
        COMMAND ${GETTEXT_MSGFMT_EXECUTABLE} ${MODE_ARG} -d ${CMAKE_SOURCE_DIR}/po --template ${INPUT_FILE} -o ${OUTPUT_FILE}
        DEPENDS ${INPUT_FILE}
        VERBATIM)

    target_sources(${TGT}
        PRIVATE
            "${INPUT_FILE}"
            "${OUTPUT_FILE}")

    source_group("Generated Files"
        FILES
            "${OUTPUT_FILE}")
endfunction()

macro(tr_qt_add_resources)
    if(Qt_VERSION_MAJOR EQUAL 6)
        qt6_add_resources(${ARGN})
    else()
        qt5_add_resources(${ARGN})
    endif()
endmacro()

macro(tr_qt_add_translation)
    if(Qt_VERSION_MAJOR EQUAL 6)
        qt6_add_translation(${ARGN} OPTIONS -silent)
    elseif(Qt_VERSION GREATER_EQUAL 5.11)
        qt5_add_translation(${ARGN} OPTIONS -silent)
    else()
        qt5_add_translation(${ARGN})
    endif()
endmacro()
