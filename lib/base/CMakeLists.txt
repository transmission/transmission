add_library(trbase STATIC)

target_sources(trbase
    PRIVATE
        string-utils.cc
        string-utils.h
        $<$<BOOL:${IS_APPLE_CLANG}>:string-utils.mm>
        tr-assert.cc
        tr-assert.h
        tr-macros.h)

target_include_directories(trbase
    PUBLIC
        ${CMAKE_SOURCE_DIR})

target_link_libraries(trbase
    PUBLIC
        transmission::fmt-header-only
        utf8::cpp
        wildmat)

set_property(
    TARGET trbase
    PROPERTY FOLDER "lib")
