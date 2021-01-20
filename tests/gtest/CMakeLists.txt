add_library(
  gtestall
  STATIC
  ${CMAKE_SOURCE_DIR}/third-party/googletest/googletest/src/gtest-all.cc
  ${CMAKE_SOURCE_DIR}/third-party/googletest/googletest/src/gtest_main.cc
)
