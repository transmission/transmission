include(GoogleTest)

add_executable(libtransmission-app-test)

target_sources(libtransmission-app-test
    PRIVATE
        dbus-peer-record-test.cc
        interop-test.cc)

set_property(
    TARGET libtransmission-app-test
    PROPERTY FOLDER "tests")

target_compile_definitions(libtransmission-app-test
    PRIVATE
        -DLIBTRANSMISSION_TEST_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../libtransmission/assets"
        __TRANSMISSION__)

target_include_directories(libtransmission-app-test
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../libtransmission)

target_link_libraries(libtransmission-app-test
    PRIVATE
        transmission::app
        ${TR_NAME}
        gtestall
        dht::dht
        fmt::fmt-header-only
        libevent::event)

if(NOT CMAKE_CROSSCOMPILING OR CMAKE_CROSSCOMPILING_EMULATOR)
    gtest_discover_tests(libtransmission-app-test
        TEST_PREFIX "LTA.")
else()
    add_test(
        NAME libtransmission-app-test
        COMMAND libtransmission-app-test)
endif()
