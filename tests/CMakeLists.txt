set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

include(GoogleTest)

add_subdirectory(
    "${CMAKE_SOURCE_DIR}/third-party/googletest"
    "${CMAKE_BINARY_DIR}/third-party/googletest"
)

add_subdirectory(
    libtransmission
)
