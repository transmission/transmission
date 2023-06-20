add_library(span-lite::span-lite INTERFACE IMPORTED)

target_include_directories(span-lite::span-lite
    INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/../third-party/span-lite/include)

target_compile_definitions(span-lite::span-lite
    INTERFACE
        span_FEATURE_WITH_CONTAINER=1)
