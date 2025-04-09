find_package(${CMAKE_FIND_PACKAGE_NAME} QUIET NO_MODULE)

include(FindPackageHandleStandardArgs)
if(${CMAKE_FIND_PACKAGE_NAME}_FOUND)
    if(TARGET libdeflate::libdeflate_static AND (${CMAKE_FIND_PACKAGE_NAME}_PREFER_STATIC_LIB OR NOT TARGET libdeflate::libdeflate_shared))
        add_library(libdeflate::libdeflate ALIAS libdeflate::libdeflate_static)
    else()
        add_library(libdeflate::libdeflate ALIAS libdeflate::libdeflate_shared)
    endif()

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
    # pkg-config support added in libdeflate v1.9
    pkg_check_modules(_DEFLATE QUIET libdeflate)
endif()

find_path(${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR
    NAMES libdeflate.h
    HINTS ${_DEFLATE_INCLUDEDIR})
find_library(${CMAKE_FIND_PACKAGE_NAME}_LIBRARY
    NAMES deflate
    HINTS ${_DEFLATE_LIBDIR})

if(${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR AND ${CMAKE_FIND_PACKAGE_NAME}_LIBRARY)
    add_library(libdeflate::libdeflate INTERFACE IMPORTED)
    target_include_directories(libdeflate::libdeflate
        INTERFACE
            ${${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR})
    target_link_libraries(libdeflate::libdeflate
        INTERFACE
            ${${CMAKE_FIND_PACKAGE_NAME}_LIBRARY})
endif()

set(${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIRS ${${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR})
set(${CMAKE_FIND_PACKAGE_NAME}_LIBRARIES ${${CMAKE_FIND_PACKAGE_NAME}_LIBRARY})

if(_DEFLATE_VERSION)
    set(${CMAKE_FIND_PACKAGE_NAME}_VERSION ${_DEFLATE_VERSION})
elseif(${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR)
    file(STRINGS "${${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR}/libdeflate.h" ${CMAKE_FIND_PACKAGE_NAME}_VERSION_STR
        REGEX "^#define[\t ]+LIBDEFLATE_VERSION_STRING[\t ]+\"[^\"]+\"")
    if(${CMAKE_FIND_PACKAGE_NAME}_VERSION_STR MATCHES "\"([^\"]+)\"")
        set(${CMAKE_FIND_PACKAGE_NAME}_VERSION "${CMAKE_MATCH_1}")
    endif()
endif()

find_package_handle_standard_args(${CMAKE_FIND_PACKAGE_NAME}
    REQUIRED_VARS
        ${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR
        ${CMAKE_FIND_PACKAGE_NAME}_LIBRARY
    VERSION_VAR ${CMAKE_FIND_PACKAGE_NAME}_VERSION)

mark_as_advanced(${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR ${CMAKE_FIND_PACKAGE_NAME}_LIBRARY)

if(${CMAKE_FIND_PACKAGE_NAME}_PREFER_STATIC_LIB)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ${${CMAKE_FIND_PACKAGE_NAME}_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES})
    unset(${CMAKE_FIND_PACKAGE_NAME}_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES)
endif()
