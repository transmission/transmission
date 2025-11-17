find_package(${CMAKE_FIND_PACKAGE_NAME} QUIET NO_MODULE)

include(FindPackageHandleStandardArgs)
if(${CMAKE_FIND_PACKAGE_NAME}_FOUND)
    find_package_handle_standard_args(${CMAKE_FIND_PACKAGE_NAME} CONFIG_MODE)
    return()
endif()

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
    pkg_check_modules(_MINIUPNPC QUIET libminiupnpc)
endif()

find_path(${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR
    NAMES miniupnpc/miniupnpc.h
    HINTS ${_MINIUPNPC_INCLUDEDIR})
find_library(${CMAKE_FIND_PACKAGE_NAME}_LIBRARY
    NAMES
        miniupnpc
        libminiupnpc
    HINTS ${_MINIUPNPC_LIBDIR})

if(_MINIUPNPC_VERSION)
    set(${CMAKE_FIND_PACKAGE_NAME}_VERSION ${_MINIUPNPC_VERSION})
elseif(${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR)
    file(STRINGS "${${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR}/miniupnpc/miniupnpc.h" _MINIUPNPC_VERSION_STR
        REGEX "^#define[\t ]+MINIUPNPC_VERSION[\t ]+\"[^\"]+\"")
    if(_MINIUPNPC_VERSION_STR MATCHES "\"([^\"]+)\"")
        set(${CMAKE_FIND_PACKAGE_NAME}_VERSION "${CMAKE_MATCH_1}")
    endif()
endif()

find_package_handle_standard_args(${CMAKE_FIND_PACKAGE_NAME}
    REQUIRED_VARS
        ${CMAKE_FIND_PACKAGE_NAME}_LIBRARY
        ${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR
    VERSION_VAR ${CMAKE_FIND_PACKAGE_NAME}_VERSION)

if(${CMAKE_FIND_PACKAGE_NAME}_FOUND)
    set(${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIRS ${${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR})
    set(${CMAKE_FIND_PACKAGE_NAME}_LIBRARIES ${${CMAKE_FIND_PACKAGE_NAME}_LIBRARY})

    if(NOT TARGET miniupnpc::miniupnpc)
        add_library(miniupnpc::miniupnpc INTERFACE IMPORTED)
        target_include_directories(miniupnpc::miniupnpc
            INTERFACE
                ${${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR})
        target_link_libraries(miniupnpc::miniupnpc
            INTERFACE
                ${${CMAKE_FIND_PACKAGE_NAME}_LIBRARY})
    endif()
endif()

mark_as_advanced(${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR ${CMAKE_FIND_PACKAGE_NAME}_LIBRARY)

if(${CMAKE_FIND_PACKAGE_NAME}_PREFER_STATIC_LIB)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ${${CMAKE_FIND_PACKAGE_NAME}_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES})
    unset(${CMAKE_FIND_PACKAGE_NAME}_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES)
endif()
