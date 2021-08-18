include_directories(SYSTEM
    ${THIRD_PARTY_DIR}/googletest/googletest/include
    ${THIRD_PARTY_DIR}/googletest/googletest)

add_subdirectory(gtest)
add_subdirectory(libtransmission)
