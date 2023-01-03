add_library(FastFloat::fast_float INTERFACE IMPORTED)

target_include_directories(FastFloat::fast_float
    INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/../third-party/fast_float/include)
