add_library(jsonsl STATIC
    jsonsl.c
    jsonsl.h)

target_include_directories(jsonsl
    PUBLIC
        ${CMAKE_CURRENT_LIST_DIR})
