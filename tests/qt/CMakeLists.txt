# A Qt built without testlib can still build the client, so its absence skips these
# rather than failing the configure. A missing dbus-run-session or
# desktop-file-validate skips the tests that need one the same way.
find_package(Qt${Qt_VERSION_MAJOR}Test ${QT_MINIMUM} QUIET)
if(NOT Qt${Qt_VERSION_MAJOR}Test_FOUND)
    message(STATUS "Qt${Qt_VERSION_MAJOR}Test not found - skipping the Qt client tests")
    add_custom_target(qt-tests)
    set_property(TARGET qt-tests PROPERTY FOLDER "tests")
    return()
endif()

set_property(DIRECTORY PROPERTY INCLUDE_DIRECTORIES
    ${CMAKE_BINARY_DIR}/qt/${TR_NAME}-qt-lib_autogen/include)
set_property(DIRECTORY PROPERTY LINK_LIBRARIES
    ${TR_NAME}-qt-lib
    Qt${Qt_VERSION_MAJOR}::Test)

# Arguments after the source name run ahead of the test executable.
function(add_trqt_test source)
    get_filename_component(name_no_ext ${source} NAME_WE)
    string(REGEX REPLACE "-test$" "" test_name ${name_no_ext})
    set(target qt-test-${test_name})
    add_executable(${target} ${source} ${CMAKE_CURRENT_SOURCE_DIR}/../../qt/application.qrc)
    set_property(TARGET ${target} PROPERTY AUTOMOC ON)
    set_property(TARGET ${target} PROPERTY AUTORCC ON)

    add_test(NAME QT.${test_name} COMMAND ${ARGN} $<TARGET_FILE:${target}>)
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} APPEND PROPERTY QT_TEST_TARGETS ${target})

    if(UNIX AND NOT APPLE)
        set_tests_properties(
            QT.${test_name}
            PROPERTIES
                ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()

    # Qt is built shared here, and a test executable is not deployed beside
    # its DLLs the way the installed client is. Without Qt's bin dir the
    # process cannot start, and dies without reaching main() to say so.
    if(WIN32)
        set_tests_properties(
            QT.${test_name}
            PROPERTIES
                ENVIRONMENT_MODIFICATION
                    "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt${Qt_VERSION_MAJOR}::Core>")
    endif()
endfunction()


add_trqt_test(add-data-test.cc)

# Builds a window, so it asks for a platform that needs no display on every host.
add_trqt_test(main-window-test.cc)
set_tests_properties(QT.main-window
    PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

if(ENABLE_QT_COM_INTEROP)
    add_trqt_test(com-interop-test.cc)

    # The test stands in for a client on both ends of the round trip, so it
    # needs the type library a client embeds. Without it the caller's QAxBase
    # has no interface information and cannot resolve a method by name.
    tr_target_idl_files(qt-test-com-interop
        ../../qt/transmission-qt.idl)
endif()

# The D-Bus test puts several clients on a bus, so give it one of its own rather
# than whichever bus the machine running the tests happens to have. Without
# dbus-run-session it falls back to whatever bus is in the environment.
if(ENABLE_QT_DBUS_INTEROP)
    find_program(DBUS_RUN_SESSION dbus-run-session)
    if(DBUS_RUN_SESSION AND NOT APPLE)
        add_trqt_test(dbus-interop-test.cc ${DBUS_RUN_SESSION} --)
    else()
        add_trqt_test(dbus-interop-test.cc)
    endif()

    # QTest exits 0 on QSKIP; without this, a busless machine would report the
    # whole suite as passed rather than skipped.
    set_tests_properties(QT.dbus-interop
        PROPERTIES
            SKIP_REGULAR_EXPRESSION "no session bus;another Transmission owns this bus")
endif()

get_property(QT_TEST_TARGETS DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY QT_TEST_TARGETS)

add_custom_target(qt-tests
    DEPENDS
        ${QT_TEST_TARGETS})

set_property(
    TARGET qt-tests
    PROPERTY FOLDER "tests")
