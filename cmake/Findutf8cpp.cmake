find_package(${CMAKE_FIND_PACKAGE_NAME} QUIET NO_MODULE)

include(FindPackageHandleStandardArgs)
if(${CMAKE_FIND_PACKAGE_NAME}_FOUND)
    if(${CMAKE_FIND_PACKAGE_NAME}_VERSION VERSION_LESS 4.0.0)
        # Before 4.0.0, some compiler options from their tests leaked into the
        # main target. We workaround by clearing them here.
        set_property(TARGET utf8cpp PROPERTY INTERFACE_COMPILE_OPTIONS)
    endif()

    find_package_handle_standard_args(${CMAKE_FIND_PACKAGE_NAME} CONFIG_MODE)
    return()
endif()

find_path(${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR
    NAMES utf8.h
    PATH_SUFFIXES utf8cpp)

if (${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR)
    add_library(utf8::cpp INTERFACE IMPORTED)

    target_include_directories(utf8::cpp
        INTERFACE
            ${${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR})
endif()

set(${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIRS ${${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR})

find_package_handle_standard_args(${CMAKE_FIND_PACKAGE_NAME}
    REQUIRED_VARS ${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR)

mark_as_advanced(${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR)
