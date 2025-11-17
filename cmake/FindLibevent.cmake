if(${CMAKE_FIND_PACKAGE_NAME}_PREFER_STATIC_LIB)
    set(LIBEVENT_STATIC_LINK TRUE)
endif()
find_package(${CMAKE_FIND_PACKAGE_NAME} QUIET NO_MODULE)

include(FindPackageHandleStandardArgs)
if(${CMAKE_FIND_PACKAGE_NAME}_FOUND)
    find_package_handle_standard_args(${CMAKE_FIND_PACKAGE_NAME} CONFIG_MODE HANDLE_COMPONENTS)
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
endif()

set(${CMAKE_FIND_PACKAGE_NAME}_LIBRARIES)
foreach(_comp IN LISTS ${CMAKE_FIND_PACKAGE_NAME}_FIND_COMPONENTS)
    if(UNIX)
        pkg_check_modules(_EVENT2_${_comp} QUIET libevent-${_comp})

        if(_EVENT2_${_comp}_VERSION AND NOT ${CMAKE_FIND_PACKAGE_NAME}_VERSION)
            set(${CMAKE_FIND_PACKAGE_NAME}_VERSION ${_EVENT2_${_comp}_VERSION})
        endif()
    endif()

    # All components share the same include directory
    find_path(${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR
        NAMES event2/event.h
        HINTS ${_EVENT2_${_comp}_INCLUDEDIR})

    find_library(${CMAKE_FIND_PACKAGE_NAME}_${_comp}_LIBRARY
        NAMES "event_${_comp}"
        HINTS ${_EVENT2_${_comp}_LIBDIR})

    mark_as_advanced(${CMAKE_FIND_PACKAGE_NAME}_${_comp}_LIBRARY)

    set(${CMAKE_FIND_PACKAGE_NAME}_${_comp}_FOUND FALSE)
    if(${CMAKE_FIND_PACKAGE_NAME}_${_comp}_LIBRARY)
        set(_target "libevent::${_comp}")

        add_library(libevent::${_comp} INTERFACE IMPORTED)
        target_include_directories(libevent::${_comp}
            INTERFACE
                ${${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR})
        target_link_libraries(libevent::${_comp}
            INTERFACE
                ${${CMAKE_FIND_PACKAGE_NAME}_${_comp}_LIBRARY})

        list(APPEND ${CMAKE_FIND_PACKAGE_NAME}_LIBRARIES libevent::${_comp})

        set(${CMAKE_FIND_PACKAGE_NAME}_${_comp}_FOUND TRUE)
    endif()
endforeach()

if(NOT ${CMAKE_FIND_PACKAGE_NAME}_VERSION AND ${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR)
    file(STRINGS "${${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR}/event2/event-config.h" _EVENT_VERSION_STR
        REGEX "^#define[\t ]+EVENT__VERSION[\t ]+\"[^\"]+\"")
    if(_EVENT_VERSION_STR MATCHES "\"([^\"]+)\"")
        set(${CMAKE_FIND_PACKAGE_NAME}_VERSION "${CMAKE_MATCH_1}")
    endif()
endif()

set(${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIRS ${${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR})

find_package_handle_standard_args(${CMAKE_FIND_PACKAGE_NAME}
    HANDLE_COMPONENTS
    VERSION_VAR ${CMAKE_FIND_PACKAGE_NAME}_VERSION)

mark_as_advanced(${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR)

if(${CMAKE_FIND_PACKAGE_NAME}_PREFER_STATIC_LIB)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ${${CMAKE_FIND_PACKAGE_NAME}_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES})
    unset(${CMAKE_FIND_PACKAGE_NAME}_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES)
endif()
