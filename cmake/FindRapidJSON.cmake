add_library(RapidJSON INTERFACE IMPORTED)

target_include_directories(RapidJSON
    INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/../third-party/rapidjson/include)

target_compile_definitions(RapidJSON
    INTERFACE
        RAPIDJSON_HAS_STDSTRING=1)
