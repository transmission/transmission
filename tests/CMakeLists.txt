#set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

include(GoogleTest)

include_directories(
  SYSTEM
  ${CMAKE_SOURCE_DIR}/third-party/googletest/googletest/include
  ${CMAKE_SOURCE_DIR}/third-party/googletest/googletest
)

add_subdirectory(gtest)
add_subdirectory(libtransmission)
