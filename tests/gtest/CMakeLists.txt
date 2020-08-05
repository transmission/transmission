add_library(
  gtestall
  SHARED
  ${CMAKE_SOURCE_DIR}/third-party/googletest/googletest/src/gtest-all.cc
  ${CMAKE_SOURCE_DIR}/third-party/googletest/googletest/src/gtest_main.cc
)
