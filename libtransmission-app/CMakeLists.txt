include(CheckLibraryExists)
include(CheckSymbolExists)

set(LIBNAME ${TR_NAME}-app)

add_library(${LIBNAME} STATIC)

target_sources(${LIBNAME}
    PRIVATE
        app.cc
        app.h
        favicon-cache.h)

target_include_directories(${LIBNAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_BINARY_DIR}/..)

target_link_libraries(${TR_NAME}
    PUBLIC
        fmt::fmt-header-only)

if(INSTALL_LIB)
    install(
        TARGETS ${LIBNAME}
        DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(
        FILES
            favicon-cache.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${LIBNAME})
endif()
