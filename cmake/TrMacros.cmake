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
    set(${NAME} "${VAL}" CACHE STRING "${DESC}")
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
    if (${IVAR} AND NOT ${IVAR} STREQUAL "AUTO")
        set(${OVAR} REQUIRED)
    endif()
endmacro()

function(tr_make_id INPUT OVAR)
    string(TOUPPER "${INPUT}" ID)
    string(REGEX REPLACE "[^A-Z0-9]+" "_" ID "${ID}")
    # string(REGEX REPLACE "^_+|_+$" "" ID "${ID}")
    set(${OVAR} "${ID}" PARENT_SCOPE)
endfunction()

macro(tr_github_upstream ID REPOID RELID RELMD5)
    set(${ID}_RELEASE "${RELID}")
    set(${ID}_UPSTREAM URL "https://github.com/${REPOID}/archive/${RELID}.tar.gz")
    if(NOT SKIP_UPSTREAM_CHECKSUM)
        list(APPEND ${ID}_UPSTREAM URL_MD5 "${RELMD5}")
    endif()
endmacro()

macro(tr_add_external_auto_library ID LIBNAME)
    if(USE_SYSTEM_${ID})
        tr_get_required_flag(USE_SYSTEM_${ID} SYSTEM_${ID}_IS_REQUIRED)
        find_package(${ID} ${${ID}_MINIMUM} ${SYSTEM_${ID}_IS_REQUIRED})
        tr_fixup_auto_option(USE_SYSTEM_${ID} ${ID}_FOUND SYSTEM_${ID}_IS_REQUIRED)
    endif()

    if(USE_SYSTEM_${ID})
        unset(${ID}_UPSTREAM_TARGET)
    else()
        set(${ID}_UPSTREAM_TARGET ${LIBNAME}-${${ID}_RELEASE})
        set(${ID}_PREFIX "${CMAKE_BINARY_DIR}/third-party/${${ID}_UPSTREAM_TARGET}")

        ExternalProject_Add(
            ${${ID}_UPSTREAM_TARGET}
            ${${ID}_UPSTREAM}
            ${ARGN}
            PREFIX "${${ID}_PREFIX}"
            CMAKE_ARGS
                -Wno-dev # We don't want to be warned over unused variables
                "-DCMAKE_TOOLCHAIN_FILE:PATH=${CMAKE_TOOLCHAIN_FILE}"
                "-DCMAKE_C_FLAGS:STRING=${CMAKE_C_FLAGS}"
                "-DCMAKE_CXX_FLAGS:STRING=${CMAKE_CXX_FLAGS}"
                "-DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}"
                "-DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR>"
        )

        set_property(TARGET ${${ID}_UPSTREAM_TARGET} PROPERTY FOLDER "ThirdParty")

        set(${ID}_INCLUDE_DIR "${${ID}_PREFIX}/include" CACHE INTERNAL "")
        set(${ID}_LIBRARY "${${ID}_PREFIX}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}${LIBNAME}${CMAKE_STATIC_LIBRARY_SUFFIX}" CACHE INTERNAL "")

        set(${ID}_INCLUDE_DIRS ${${ID}_INCLUDE_DIR})
        set(${ID}_LIBRARIES ${${ID}_LIBRARY})
    endif()
endmacro()

function(tr_append_target_property TGT PROP VAL)
    get_target_property(OVAL ${TGT} ${PROP})
    if(OVAL)
        set(VAL "${OVAL} ${VAL}")
    endif()
    set_target_properties(${TGT} PROPERTIES ${PROP} "${VAL}")
endfunction()

function(tr_win32_app_info OVAR DESCR INTNAME ORIGFNAME)
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

    set(${OVAR} "${CMAKE_CURRENT_BINARY_DIR}/${INTNAME}-app-info.rc" PARENT_SCOPE)
endfunction()
