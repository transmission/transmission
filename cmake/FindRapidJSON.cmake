find_package(${CMAKE_FIND_PACKAGE_NAME} QUIET NO_MODULE)

include(FindPackageHandleStandardArgs)
if(${CMAKE_FIND_PACKAGE_NAME}_FOUND)
    find_package_handle_standard_args(${CMAKE_FIND_PACKAGE_NAME} CONFIG_MODE)
    return()
endif()

if(UNIX)
    find_package(PkgConfig QUIET)
    pkg_check_modules(_RAPIDJSON QUIET RapidJSON)
endif()

find_path(${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR
    NAMES rapidjson/rapidjson.h
    HINTS ${_RAPIDJSON_INCLUDEDIR})

if (${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR)
    add_library(RapidJSON INTERFACE IMPORTED)

    target_include_directories(RapidJSON
        INTERFACE
            ${${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR})
endif()

if(_RAPIDJSON_VERSION)
    set(${CMAKE_FIND_PACKAGE_NAME}_VERSION ${_RAPIDJSON_VERSION})
endif()

set(${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIRS ${${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR})

find_package_handle_standard_args(${CMAKE_FIND_PACKAGE_NAME}
    REQUIRED_VARS ${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR
    VERSION_VAR ${CMAKE_FIND_PACKAGE_NAME}_VERSION)

mark_as_advanced(${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR)
