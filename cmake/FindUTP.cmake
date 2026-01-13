if(UTP_PREFER_STATIC_LIB)
    set(UTP_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES})
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

find_path(UTP_INCLUDE_DIR
    NAMES libutp/utp.h
    HINTS ${_UTP_INCLUDEDIR})
find_library(UTP_LIBRARY
    NAMES utp
    HINTS ${_UTP_LIBDIR})

if(UTP_INCLUDE_DIR AND UTP_LIBRARY)
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

    set(CMAKE_REQUIRED_INCLUDES "${UTP_INCLUDE_DIR}")
    set(CMAKE_REQUIRED_LIBRARIES "${UTP_LIBRARY}")
    set(CMAKE_REQUIRED_QUIET ON)

    foreach(_UTP_FUNC IN LISTS _UTP_FUNCS)
        string(MAKE_C_IDENTIFIER "HAVE_${_UTP_FUNC}" _UTP_FUNC_VAR)
        string(TOUPPER "${_UTP_FUNC_VAR}" _UTP_FUNC_VAR)
        check_cxx_symbol_exists(${_UTP_FUNC} libutp/utp.h ${_UTP_FUNC_VAR})
        if(NOT ${_UTP_FUNC_VAR})
            unset(UTP_INCLUDE_DIR CACHE)
            unset(UTP_LIBRARY CACHE)
            break()
        endif()
    endforeach()

    set(CMAKE_REQUIRED_INCLUDES "${_UTP_OLD_CMAKE_REQUIRED_INCLUDES}")
    set(CMAKE_REQUIRED_LIBRARIES "${_UTP_OLD_CMAKE_REQUIRED_LIBRARIES}")
    set(CMAKE_REQUIRED_QUIET "${_UTP_OLD_CMAKE_REQUIRED_QUIET}")
endif()

set(UTP_INCLUDE_DIRS ${UTP_INCLUDE_DIR})
set(UTP_LIBRARIES ${UTP_LIBRARY})

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(UTP
    REQUIRED_VARS
        UTP_LIBRARY
        UTP_INCLUDE_DIR)

mark_as_advanced(UTP_INCLUDE_DIR UTP_LIBRARY)

if(UTP_PREFER_STATIC_LIB)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ${UTP_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES})
    unset(UTP_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES)
endif()
