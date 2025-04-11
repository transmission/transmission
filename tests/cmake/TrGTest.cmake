include(TrMacros)

tr_auto_option(USE_SYSTEM_GTEST "Use system GTest library" OFF)

tr_add_external_auto_library(GTEST GTest
    SUBPROJECT
    SOURCE_DIR googletest
    CMAKE_ARGS
        -DBUILD_GMOCK=OFF
        -DINSTALL_GTEST=OFF
        -DBUILD_SHARED_LIBS=ON)

# The GTest::gtest_main target is new in CMake 3.20
if(NOT TARGET GTest::gtest_main)
    add_library(GTest::gtest_main ALIAS GTest::Main)
endif()
