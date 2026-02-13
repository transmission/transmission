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
        display-modes.h
        favicon-cache.h)

target_include_directories(${LIBNAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
        ${CMAKE_CURRENT_BINARY_DIR}/../..)

target_link_libraries(${LIBNAME}
    PUBLIC
        transmission::fmt-header-only)
