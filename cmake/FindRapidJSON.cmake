add_library(RapidJSON INTERFACE IMPORTED)

target_include_directories(RapidJSON
    INTERFACE
        ${TR_THIRD_PARTY_SOURCE_DIR}/rapidjson/include)

target_compile_definitions(RapidJSON
    INTERFACE
        RAPIDJSON_HAS_STDSTRING=1)
