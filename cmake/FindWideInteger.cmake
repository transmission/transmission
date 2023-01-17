add_library(WideInteger::WideInteger INTERFACE IMPORTED)

target_include_directories(WideInteger::WideInteger
    INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/../third-party/wide-integer)
