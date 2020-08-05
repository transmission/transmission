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

foreach(T bitfield blocklist clients crypto error file getopt
          history json magnet makemeta metainfo move peer-msgs
          quark rename rpc session subprocess utils variant watchdir)
  set(TP ${TR_NAME}-test-${T})
  if(T MATCHES "^([^@]+)@.+$")
    string(REPLACE "@" "-" TP "${TP}")
    string(REPLACE "@" "-" T_NAME "${T}")
    set(${TP}_TEST_BASENAME "${CMAKE_MATCH_1}")
  else()
    set(T_NAME "${T}")
    set(${TP}_TEST_BASENAME "${T}")
  endif()
  add_executable(${TP} ${${TP}_TEST_BASENAME}-test.cc ${${T}-test_ADD_SOURCES})
  target_link_libraries(${TP} gtestall ${TR_NAME})
  add_test(NAME ${T_NAME}-test COMMAND ${TP})
  set_property(TARGET ${TP} PROPERTY FOLDER "UnitTests")
endforeach()

if(WIN32)
  add_custom_command(TARGET ${TR_NAME}-test-subprocess PRE_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PROJECT_SOURCE_DIR}/libtransmission/subprocess-test.cmd
            $<TARGET_FILE_DIR:${TR_NAME}-test-subprocess>/${TR_NAME}-test-subprocess.cmd)
endif()
