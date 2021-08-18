add_executable(libtransmission-test
    bitfield-test.cc
    blocklist-test.cc
    clients-test.cc
    copy-test.cc
    crypto-test-ref.h
    crypto-test.cc
    error-test.cc
    file-test.cc
    getopt-test.cc
    history-test.cc
    json-test.cc
    magnet-test.cc
    makemeta-test.cc
    metainfo-test.cc
    move-test.cc
    peer-msgs-test.cc
    quark-test.cc
    rename-test.cc
    rpc-test.cc
    session-test.cc
    subprocess-test-script.cmd
    subprocess-test.cc
    test-fixtures.h
    utils-test.cc
    variant-test.cc
    watchdir-test.cc)

target_compile_definitions(libtransmission-test
    PRIVATE
        __TRANSMISSION__)

target_include_directories(libtransmission-test
    PRIVATE
        ${CMAKE_SOURCE_DIR}/libtransmission
        ${CMAKE_BINARY_DIR}/libtransmission)

target_include_directories(libtransmission-test SYSTEM
    PRIVATE
        ${CURL_INCLUDE_DIRS}
        ${EVENT2_INCLUDE_DIRS})

target_compile_options(libtransmission-test
    PRIVATE
        ${CXX_WARNING_FLAGS}
        $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-Wno-sign-compare>) # patches welcomed

target_link_libraries(libtransmission-test
    PRIVATE
        ${TR_NAME}
        gtestall)

add_test(
    NAME libtransmission-test
    COMMAND libtransmission-test)

add_custom_command(
    TARGET libtransmission-test
    PRE_BUILD
    COMMAND
        ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/subprocess-test-script.cmd
            $<TARGET_FILE_DIR:libtransmission-test>/subprocess-test.cmd)

add_executable(subprocess-test
    subprocess-test-program.cc)

target_include_directories(subprocess-test
    PRIVATE
        ${CMAKE_SOURCE_DIR}/libtransmission)

target_link_libraries(subprocess-test
    PRIVATE
        ${TR_NAME})

add_dependencies(libtransmission-test
    subprocess-test)
