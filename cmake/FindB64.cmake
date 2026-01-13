if(B64_PREFER_STATIC_LIB)
    set(B64_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES})
    if(WIN32)
        set(CMAKE_FIND_LIBRARY_SUFFIXES .a .lib ${CMAKE_FIND_LIBRARY_SUFFIXES})
    else()
        set(CMAKE_FIND_LIBRARY_SUFFIXES .a ${CMAKE_FIND_LIBRARY_SUFFIXES})
    endif()
endif()

if(UNIX)
    find_package(PkgConfig QUIET)
    pkg_check_modules(_B64 QUIET libb64)
endif()

find_path(B64_INCLUDE_DIR
    NAMES
        b64/cdecode.h
        b64/cencode.h
    HINTS ${_B64_INCLUDEDIR})
find_library(B64_LIBRARY
    NAMES b64
    HINTS ${_B64_LIBDIR})

set(B64_INCLUDE_DIRS ${B64_INCLUDE_DIR})
set(B64_LIBRARIES ${B64_LIBRARY})

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(B64
    REQUIRED_VARS
        B64_LIBRARY
        B64_INCLUDE_DIR)

mark_as_advanced(B64_INCLUDE_DIR B64_LIBRARY)

if(B64_PREFER_STATIC_LIB)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ${B64_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES})
    unset(B64_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES)
endif()
