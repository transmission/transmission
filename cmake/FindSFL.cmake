add_library(sfl::sfl INTERFACE IMPORTED)

target_include_directories(sfl::sfl
    INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/../third-party/sfl/include)

target_compile_definitions(sfl::sfl
    INTERFACE
        SFL_NO_EXCEPTIONS=1)
