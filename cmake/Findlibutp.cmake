if(${CMAKE_FIND_PACKAGE_NAME}_PREFER_STATIC_LIB)
    set(${CMAKE_FIND_PACKAGE_NAME}_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES})
    if(WIN32)
        set(CMAKE_FIND_LIBRARY_SUFFIXES .a .lib ${CMAKE_FIND_LIBRARY_SUFFIXES})
    else()
        set(CMAKE_FIND_LIBRARY_SUFFIXES .a ${CMAKE_FIND_LIBRARY_SUFFIXES})
    endif()
endif()

if(UNIX)
    find_package(PkgConfig QUIET)
    pkg_check_modules(_UTP QUIET libutp)
endif()

find_path(${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR
    NAMES libutp/utp.h
    HINTS ${_UTP_INCLUDEDIR})
find_library(${CMAKE_FIND_PACKAGE_NAME}_LIBRARY
    NAMES utp
    HINTS ${_UTP_LIBDIR})

if(${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR AND ${CMAKE_FIND_PACKAGE_NAME}_LIBRARY)
    include(CheckCXXSymbolExists)

    set(_UTP_FUNCS
        utp_check_timeouts
        utp_close
        utp_connect
        utp_context_get_userdata
        utp_context_set_option
        utp_context_set_userdata
        utp_create_socket
        utp_destroy
        utp_getpeername
        utp_get_userdata
        utp_init
        utp_issue_deferred_acks
        utp_process_udp
        utp_read_drained
        utp_set_callback
        utp_set_userdata
        utp_write
        utp_writev)

    set(_UTP_OLD_CMAKE_REQUIRED_INCLUDES "${CMAKE_REQUIRED_INCLUDES}")
    set(_UTP_OLD_CMAKE_REQUIRED_LIBRARIES "${CMAKE_REQUIRED_LIBRARIES}")
    set(_UTP_OLD_CMAKE_REQUIRED_QUIET "${CMAKE_REQUIRED_QUIET}")

    set(CMAKE_REQUIRED_INCLUDES "${${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR}")
    set(CMAKE_REQUIRED_LIBRARIES "${${CMAKE_FIND_PACKAGE_NAME}_LIBRARY}")
    set(CMAKE_REQUIRED_QUIET ON)

    foreach(_UTP_FUNC IN LISTS _UTP_FUNCS)
        string(MAKE_C_IDENTIFIER "HAVE_${_UTP_FUNC}" _UTP_FUNC_VAR)
        string(TOUPPER "${_UTP_FUNC_VAR}" _UTP_FUNC_VAR)
        check_cxx_symbol_exists(${_UTP_FUNC} libutp/utp.h ${_UTP_FUNC_VAR})
        if(NOT ${_UTP_FUNC_VAR})
            unset(${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR CACHE)
            unset(${CMAKE_FIND_PACKAGE_NAME}_LIBRARY CACHE)
            break()
        endif()
    endforeach()

    set(CMAKE_REQUIRED_INCLUDES "${_UTP_OLD_CMAKE_REQUIRED_INCLUDES}")
    set(CMAKE_REQUIRED_LIBRARIES "${_UTP_OLD_CMAKE_REQUIRED_LIBRARIES}")
    set(CMAKE_REQUIRED_QUIET "${_UTP_OLD_CMAKE_REQUIRED_QUIET}")
endif()

set(${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIRS ${${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR})
set(${CMAKE_FIND_PACKAGE_NAME}_LIBRARIES ${${CMAKE_FIND_PACKAGE_NAME}_LIBRARY})

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(${CMAKE_FIND_PACKAGE_NAME}
    REQUIRED_VARS
        ${CMAKE_FIND_PACKAGE_NAME}_LIBRARY
        ${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR)

mark_as_advanced(${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR ${CMAKE_FIND_PACKAGE_NAME}_LIBRARY)

if(${CMAKE_FIND_PACKAGE_NAME}_PREFER_STATIC_LIB)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ${${CMAKE_FIND_PACKAGE_NAME}_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES})
    unset(${CMAKE_FIND_PACKAGE_NAME}_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES)
endif()
