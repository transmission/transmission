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

tr_setup_gtest_target(libtransmission-app-test "LTA.")
