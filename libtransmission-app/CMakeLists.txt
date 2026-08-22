include(CheckLibraryExists)
include(CheckSymbolExists)

set(LIBNAME ${TR_NAME}-app)

add_library(${LIBNAME} STATIC)
add_library(transmission::app ALIAS ${LIBNAME})

target_sources(${LIBNAME}
    PRIVATE
        app.cc
        app.h
        converters.cc
        converters.h
        dbus-peer-record.cc
        dbus-peer-record.h
        display-modes.h
        favicon-cache.h
        interop-names.h
        interop.cc
        interop.h
        startup-coordinator.cc
        startup-coordinator.h)

target_include_directories(${LIBNAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_BINARY_DIR}/..)

target_link_libraries(${LIBNAME}
    PUBLIC
        ${TR_NAME}
        fmt::fmt-header-only)
