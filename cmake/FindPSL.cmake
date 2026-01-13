if(PSL_PREFER_STATIC_LIB)
    set(PSL_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES})
    if(WIN32)
        set(CMAKE_FIND_LIBRARY_SUFFIXES .a .lib ${CMAKE_FIND_LIBRARY_SUFFIXES})
    else()
        set(CMAKE_FIND_LIBRARY_SUFFIXES .a ${CMAKE_FIND_LIBRARY_SUFFIXES})
    endif()
endif()

if(UNIX)
    find_package(PkgConfig QUIET)
    pkg_check_modules(_PSL QUIET libpsl)
endif()

find_path(PSL_INCLUDE_DIR
    NAMES libpsl.h
    HINTS ${_PSL_INCLUDEDIR})
find_library(PSL_LIBRARY
    NAMES psl
    HINTS ${_PSL_LIBDIR})

set(PSL_INCLUDE_DIRS ${PSL_INCLUDE_DIR})
set(PSL_LIBRARIES ${PSL_LIBRARY})

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(PSL
    REQUIRED_VARS
        PSL_LIBRARY
        PSL_INCLUDE_DIR)

mark_as_advanced(PSL_INCLUDE_DIR PSL_LIBRARY)

if(PSL_PREFER_STATIC_LIB)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ${PSL_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES})
    unset(PSL_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES)
endif()
