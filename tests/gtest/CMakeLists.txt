set(GTEST_ROOT_DIR ${TR_THIRD_PARTY_SOURCE_DIR}/googletest/googletest)

add_library(gtestall STATIC)

target_sources(gtestall
    PRIVATE
        ${GTEST_ROOT_DIR}/src/gtest-all.cc
        ${GTEST_ROOT_DIR}/src/gtest_main.cc)

set_property(
    TARGET gtestall
    PROPERTY FOLDER "${TR_THIRD_PARTY_DIR_NAME}")

target_include_directories(gtestall SYSTEM
    PRIVATE
        ${GTEST_ROOT_DIR}
    PUBLIC
        ${GTEST_ROOT_DIR}/include)
