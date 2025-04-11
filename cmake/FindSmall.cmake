find_package(${CMAKE_FIND_PACKAGE_NAME} QUIET NO_MODULE)

include(FindPackageHandleStandardArgs)
if(${CMAKE_FIND_PACKAGE_NAME}_FOUND)
    find_package_handle_standard_args(${CMAKE_FIND_PACKAGE_NAME} CONFIG_MODE)
    return()
endif()

find_path(${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR
    NAMES small/vector.hpp)

if (${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR)
    add_library(small::small INTERFACE IMPORTED)

    target_include_directories(small::small
        INTERFACE
            ${${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR})
endif()

set(${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIRS ${${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR})

find_package_handle_standard_args(${CMAKE_FIND_PACKAGE_NAME}
    REQUIRED_VARS ${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR)

mark_as_advanced(${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR)
