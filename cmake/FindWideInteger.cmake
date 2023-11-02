add_library(WideInteger::WideInteger INTERFACE IMPORTED)

target_include_directories(WideInteger::WideInteger
    INTERFACE
        ${TR_THIRD_PARTY_SOURCE_DIR}/wide-integer)
