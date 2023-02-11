add_library(gtestall STATIC
    ${THIRD_PARTY_DIR}/googletest/googletest/src/gtest-all.cc
    ${THIRD_PARTY_DIR}/googletest/googletest/src/gtest_main.cc)

set_property(TARGET gtestall PROPERTY FOLDER "third-party")
