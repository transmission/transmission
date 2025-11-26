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

# Always fails
find_package_handle_standard_args(${CMAKE_FIND_PACKAGE_NAME}
    REQUIRED_VARS
        ${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR
    VERSION_VAR ${CMAKE_FIND_PACKAGE_NAME}_VERSION)
