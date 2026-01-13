add_library(utf8::cpp INTERFACE IMPORTED)

target_include_directories(utf8::cpp
    INTERFACE
        ${TR_THIRD_PARTY_SOURCE_DIR}/utfcpp/source)
