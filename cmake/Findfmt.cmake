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
    pkg_check_modules(_FMT QUIET fmt)
endif()

find_path(${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR
    NAMES fmt/core.h
    HINTS ${_FMT_INCLUDEDIR})

if(_FMT_VERSION)
    set(${CMAKE_FIND_PACKAGE_NAME}_VERSION ${_FMT_VERSION})
elseif(${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR)
    set(_FMT_VERSION_H_PATH "${${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR}/fmt/base.h")
    if(NOT EXISTS "${_FMT_VERSION_H_PATH}")
        # fmt < 11
        set(_FMT_VERSION_H_PATH "${${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR}/fmt/core.h")
    endif()
    file(READ "${_FMT_VERSION_H_PATH}" _FMT_VERSION_H)
    if(_FMT_VERSION_H MATCHES "FMT_VERSION ([0-9]+)([0-9][0-9])([0-9][0-9])")
        # Use math to skip leading zeros if any.
        math(EXPR _FMT_VERSION_MAJOR ${CMAKE_MATCH_1})
        math(EXPR _FMT_VERSION_MINOR ${CMAKE_MATCH_2})
        math(EXPR _FMT_VERSION_PATCH ${CMAKE_MATCH_3})
        set(${CMAKE_FIND_PACKAGE_NAME}_VERSION "${_FMT_VERSION_MAJOR}.${_FMT_VERSION_MINOR}.${_FMT_VERSION_PATCH}")
    endif()
endif()

find_package_handle_standard_args(${CMAKE_FIND_PACKAGE_NAME}
    REQUIRED_VARS
        ${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR
    VERSION_VAR ${CMAKE_FIND_PACKAGE_NAME}_VERSION)

if(${CMAKE_FIND_PACKAGE_NAME}_FOUND)
    set(${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIRS ${${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR})

    if(NOT TARGET fmt::fmt-header-only)
        add_library(fmt::fmt-header-only INTERFACE IMPORTED)
        target_compile_definitions(fmt::fmt-header-only-only INTERFACE FMT_HEADER_ONLY=1)
        target_compile_features(fmt::fmt-header-only-only INTERFACE cxx_std_11)
        target_include_directories(fmt::fmt-header-only
            INTERFACE
                ${${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR})
        if(MSVC)
            target_compile_options(fmt::fmt-header-only INTERFACE $<$<COMPILE_LANGUAGE:CXX>:/utf-8>)
        endif()
    endif()
endif()

mark_as_advanced(${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR ${CMAKE_FIND_PACKAGE_NAME}_LIBRARY)

if(${CMAKE_FIND_PACKAGE_NAME}_PREFER_STATIC_LIB)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ${${CMAKE_FIND_PACKAGE_NAME}_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES})
    unset(${CMAKE_FIND_PACKAGE_NAME}_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES)
endif()
