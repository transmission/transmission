add_library(utf8::cpp INTERFACE IMPORTED)

target_include_directories(utf8::cpp
    INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/../third-party/utfcpp/source)
