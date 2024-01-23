add_library(small::small INTERFACE IMPORTED)

target_include_directories(small::small
    INTERFACE
        ${TR_THIRD_PARTY_SOURCE_DIR}/small/include)

target_compile_definitions(small::small
    INTERFACE
        SMALL_DISABLE_EXCEPTIONS=1)
