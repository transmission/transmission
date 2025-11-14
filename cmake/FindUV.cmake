if(UV_PREFER_STATIC_LIB)
    set(UV_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES})
    if(WIN32)
        set(CMAKE_FIND_LIBRARY_SUFFIXES .a .lib ${CMAKE_FIND_LIBRARY_SUFFIXES})
    else()
        set(CMAKE_FIND_LIBRARY_SUFFIXES .a ${CMAKE_FIND_LIBRARY_SUFFIXES})
    endif()
endif()

if(UNIX)
    find_package(PkgConfig QUIET)
    pkg_check_modules(_UV QUIET libuv)
endif()

find_path(UV_INCLUDE_DIR
    NAMES uv.h
    HINTS ${_UV_INCLUDEDIR})
find_library(UV_LIBRARY
    NAMES uv
    HINTS ${_UV_LIBDIR})

set(UV_INCLUDE_DIRS ${UV_INCLUDE_DIR})
set(UV_LIBRARIES ${UV_LIBRARY})

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(UV
    REQUIRED_VARS
        UV_LIBRARY
        UV_INCLUDE_DIR)

mark_as_advanced(UV_INCLUDE_DIR UV_LIBRARY)

if(UV_PREFER_STATIC_LIB)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ${UV_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES})
    unset(UV_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES)
endif()
