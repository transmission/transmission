add_library(wildmat STATIC
    wildmat.c
    wildmat.h)

target_include_directories(wildmat
    PUBLIC
        ${CMAKE_CURRENT_LIST_DIR})
