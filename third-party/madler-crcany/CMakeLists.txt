add_library(MadlerCrcany STATIC
    crc32iscsi.c
    crc32iscsi.h)

target_include_directories(MadlerCrcany
    PUBLIC
        ${CMAKE_CURRENT_LIST_DIR})
