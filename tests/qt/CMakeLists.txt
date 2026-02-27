find_package(Qt${Qt_VERSION_MAJOR}Test ${QT_MINIMUM} REQUIRED)

set_property(DIRECTORY PROPERTY INCLUDE_DIRECTORIES
    ${CMAKE_BINARY_DIR}/qt/${TR_NAME}-qt-lib_autogen/include)
set_property(DIRECTORY PROPERTY LINK_LIBRARIES
    ${TR_NAME}-qt-lib
    Qt${Qt_VERSION_MAJOR}::Test)

function(add_trqt_test source)
    get_filename_component(name_no_ext ${source} NAME_WE)
    string(REGEX REPLACE "-test$" "" test_name ${name_no_ext})
    set(target qt-test-${test_name})
    add_executable(${target} ${source} ${CMAKE_CURRENT_SOURCE_DIR}/../../qt/application.qrc)
    set_property(TARGET ${target} PROPERTY AUTOMOC ON)
    set_property(TARGET ${target} PROPERTY AUTORCC ON)
    add_test(NAME QT.${test_name} COMMAND ${target})

    if(UNIX AND NOT APPLE)
        set_tests_properties(
            QT.${test_name}
            PROPERTIES
                ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()
endfunction()

add_trqt_test(prefs-test.cc)
add_trqt_test(rpcclient-test.cc)
add_trqt_test(session-test.cc)
add_trqt_test(torrent-memory-test.cc)

add_custom_target(qt-tests
    DEPENDS
        qt-test-prefs
        qt-test-rpcclient
        qt-test-session
        qt-test-torrent-memory)

set_property(
    TARGET qt-tests
    PROPERTY FOLDER "tests")
