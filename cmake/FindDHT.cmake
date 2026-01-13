if(DHT_PREFER_STATIC_LIB)
    set(DHT_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES})
    if(WIN32)
        set(CMAKE_FIND_LIBRARY_SUFFIXES .a .lib ${CMAKE_FIND_LIBRARY_SUFFIXES})
    else()
        set(CMAKE_FIND_LIBRARY_SUFFIXES .a ${CMAKE_FIND_LIBRARY_SUFFIXES})
    endif()
endif()

if(UNIX)
    find_package(PkgConfig QUIET)
    pkg_check_modules(_DHT QUIET libdht)
endif()

find_path(DHT_INCLUDE_DIR
    NAMES dht/dht.h
    HINTS ${_DHT_INCLUDEDIR})
find_library(DHT_LIBRARY
    NAMES dht
    HINTS ${_DHT_LIBDIR})

set(DHT_INCLUDE_DIRS ${DHT_INCLUDE_DIR})
set(DHT_LIBRARIES ${DHT_LIBRARY})

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(DHT
    REQUIRED_VARS
        DHT_LIBRARY
        DHT_INCLUDE_DIR)

mark_as_advanced(DHT_INCLUDE_DIR DHT_LIBRARY)

if(DHT_PREFER_STATIC_LIB)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ${DHT_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES})
    unset(DHT_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES)
endif()
