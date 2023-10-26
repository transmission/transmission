add_library(small::small INTERFACE IMPORTED)

target_include_directories(small::small
    INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/../third-party/small/include)


target_compile_definitions(small::small
    INTERFACE
        SMALL_DISABLE_EXCEPTIONS=1)
