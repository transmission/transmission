include_directories(SYSTEM
    ${LIBFMT_INCLUDE_DIRS}
    ${THIRD_PARTY_DIR}/googletest/googletest/include
    ${THIRD_PARTY_DIR}/googletest/googletest)

add_subdirectory(gtest)
add_subdirectory(libtransmission)
add_subdirectory(utils)
