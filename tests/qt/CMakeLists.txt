set_directory_properties(
  PROPERTIES
  FOLDER "UnitTests"
  MSVC_RUNTIME_LIBRARY "MultiThreaded"
)

add_compile_options(
  ${CXX_WARNING_FLAGS}
)

include_directories(
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_BINARY_DIR}
)

include_directories(
  SYSTEM
  ${EVENT2_INCLUDE_DIRS}
)

foreach(T prefs)
  set(TP trqt-test-${T})
  add_executable(${TP} ${T}-test.cc)
  target_link_libraries(${TP} gtestall trqt)
  add_test(NAME ${TP} COMMAND ${TP})
endforeach()
