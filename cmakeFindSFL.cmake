add_library(sfl-library INTERFACE IMPORTED)

target_include_directories(sfl-library
    INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/../third-party/sfl-library/include)

target_compile_definitions(sfl-library
    INTERFACE
        SFL_NO_EXCEPTIONS=1)
