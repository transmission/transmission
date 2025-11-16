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
find_library(${CMAKE_FIND_PACKAGE_NAME}_LIBRARY
    NAMES fmt fmtd
    HINTS ${_FMT_LIBDIR})

if(${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR)
    if(${CMAKE_FIND_PACKAGE_NAME}_LIBRARY)
        add_library(fmt::fmt INTERFACE IMPORTED)
        target_include_directories(fmt::fmt
            INTERFACE
                ${${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR})
        target_link_libraries(fmt::fmt
            INTERFACE
                ${${CMAKE_FIND_PACKAGE_NAME}_LIBRARY})
        target_compile_features(fmt::fmt INTERFACE cxx_std_11)
        if(MSVC)
            target_compile_options(fmt::fmt INTERFACE $<$<COMPILE_LANGUAGE:CXX>:/utf-8>)
        endif()
    endif()

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

if(_FMT_VERSION)
    set(${CMAKE_FIND_PACKAGE_NAME}_VERSION ${_FMT_VERSION})
elseif(${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR)
    include(TrMacros)
    tr_get_fmt_version(${CMAKE_FIND_PACKAGE_NAME}_VERSION ${${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR})
endif()

set(${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIRS ${${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR})
set(${CMAKE_FIND_PACKAGE_NAME}_LIBRARIES ${${CMAKE_FIND_PACKAGE_NAME}_LIBRARY})

find_package_handle_standard_args(${CMAKE_FIND_PACKAGE_NAME}
    REQUIRED_VARS
        ${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR
    VERSION_VAR ${CMAKE_FIND_PACKAGE_NAME}_VERSION)

mark_as_advanced(${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR ${CMAKE_FIND_PACKAGE_NAME}_LIBRARY)

if(${CMAKE_FIND_PACKAGE_NAME}_PREFER_STATIC_LIB)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ${${CMAKE_FIND_PACKAGE_NAME}_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES})
    unset(${CMAKE_FIND_PACKAGE_NAME}_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES)
endif()
