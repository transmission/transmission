# include(GoogleTest)
# add_subdirectory("${CMAKE_SOURCE_DIR}/third-party/googletest" "third-party/googletest")

set(crypto-test_ADD_SOURCES crypto-test-ref.h)
set(subprocess-test_ADD_SOURCES subprocess-test.cmd)

include_directories(
    ${CMAKE_SOURCE_DIR}/libtransmission
    ${CMAKE_BINARY_DIR}/libtransmission
)

add_definitions(
    -D__TRANSMISSION__
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
    target_link_libraries(${TP} gtest gtest_main ${TR_NAME})
    add_test(NAME ${T_NAME}-test COMMAND ${TP})
    set_property(TARGET ${TP} PROPERTY FOLDER "UnitTests")
endforeach()

if(WIN32)
    add_custom_command(TARGET ${TR_NAME}-test-subprocess PRE_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PROJECT_SOURCE_DIR}/subprocess-test.cmd
            $<TARGET_FILE_DIR:${TR_NAME}-test-subprocess>/${TR_NAME}-test-subprocess.cmd)
endif()
