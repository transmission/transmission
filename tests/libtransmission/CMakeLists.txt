set(crypto-test_ADD_SOURCES crypto-test-ref.h)
set(subprocess-test_ADD_SOURCES subprocess-test.cmd)

add_compile_options(
  ${CXX_WARNING_FLAGS}
)

if(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang")
  # patches welcomed
  add_compile_options(-Wno-sign-compare)
endif()

add_definitions(
  -D__TRANSMISSION__
)

include_directories(
  ${CMAKE_SOURCE_DIR}/libtransmission
  ${CMAKE_SOURCE_DIR}/tests/libtransmission
  ${CMAKE_BINARY_DIR}/libtransmission
)
include_directories(
  SYSTEM
  ${CURL_INCLUDE_DIRS}
  ${EVENT2_INCLUDE_DIRS}
)

set_property(DIRECTORY PROPERTY FOLDER "UnitTests")
set_property(DIRECTORY PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")

foreach(T bitfield blocklist clients crypto error file getopt
          history json magnet makemeta metainfo move peer-msgs
          quark rename rpc session subprocess utils variant watchdir)
  set(TP libtransmission-test-${T})
  add_executable(${TP} ${T}-test.cc)
  target_link_libraries(${TP} gtestall ${TR_NAME})
  add_test(NAME ${TP} COMMAND ${TP})
endforeach()

add_custom_command(
  TARGET libtransmission-test-subprocess
  PRE_BUILD
  COMMAND
    ${CMAKE_COMMAND} -E copy_if_different
      ${PROJECT_SOURCE_DIR}/tests/libtransmission/subprocess-test.cmd
      $<TARGET_FILE_DIR:libtransmission-test-subprocess>/libtransmission-test-subprocess.cmd
)
