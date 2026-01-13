add_library(fmt::fmt-header-only INTERFACE IMPORTED)

set(${CMAKE_FIND_PACKAGE_NAME}_INCLUDE "${TR_THIRD_PARTY_SOURCE_DIR}/fmt/include")

target_include_directories(fmt::fmt-header-only
    INTERFACE
        ${${CMAKE_FIND_PACKAGE_NAME}_INCLUDE})

set(_FMT_VERSION_H_PATH "${${CMAKE_FIND_PACKAGE_NAME}_INCLUDE}/fmt/base.h")
if(NOT EXISTS "${_FMT_VERSION_H_PATH}")
    # fmt < 11
    set(_FMT_VERSION_H_PATH "${${CMAKE_FIND_PACKAGE_NAME}_INCLUDE}/fmt/core.h")
endif()
file(READ "${_FMT_VERSION_H_PATH}" _FMT_VERSION_H)
if(_FMT_VERSION_H MATCHES "FMT_VERSION ([0-9]+)([0-9][0-9])([0-9][0-9])")
    # Use math to skip leading zeros if any.
    math(EXPR _FMT_VERSION_MAJOR ${CMAKE_MATCH_1})
    math(EXPR _FMT_VERSION_MINOR ${CMAKE_MATCH_2})
    math(EXPR _FMT_VERSION_PATCH ${CMAKE_MATCH_3})
    set(${CMAKE_FIND_PACKAGE_NAME}_VERSION "${_FMT_VERSION_MAJOR}.${_FMT_VERSION_MINOR}.${_FMT_VERSION_PATCH}")
endif()

target_compile_definitions(fmt::fmt-header-only
    INTERFACE
        $<IF:$<VERSION_GREATER_EQUAL:${${CMAKE_FIND_PACKAGE_NAME}_VERSION},11.2.0>,FMT_USE_EXCEPTIONS,FMT_EXCEPTIONS>=0
        FMT_HEADER_ONLY=1)
