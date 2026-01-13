add_library(FastFloat::fast_float INTERFACE IMPORTED)

target_include_directories(FastFloat::fast_float
    INTERFACE
        ${TR_THIRD_PARTY_SOURCE_DIR}/fast_float/include)
