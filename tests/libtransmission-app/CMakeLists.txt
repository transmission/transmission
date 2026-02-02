include(GoogleTest)

add_executable(libtransmission-app-test)

target_sources(libtransmission-app-test
	PRIVATE
		display-mode-tests.cc
		test-fixtures.h)

set_property(
	TARGET libtransmission-app-test
	PROPERTY FOLDER "tests")

target_link_libraries(libtransmission-app-test
	PRIVATE
		${TR_NAME}-app
		${TR_NAME}
		GTest::gtest_main)

if(NOT CMAKE_CROSSCOMPILING OR CMAKE_CROSSCOMPILING_EMULATOR)
	gtest_discover_tests(libtransmission-app-test
		TEST_PREFIX "LTA.")
else()
	add_test(
		NAME libtransmission-app-test
		COMMAND libtransmission-app-test)
endif()
