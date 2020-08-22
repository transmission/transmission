check_LTLIBRARIES = \
  libgtest.la \
  libgtest_main.la

GTEST_DIR = $(top_srcdir)/third-party/googletest

GOOGLE_TEST_INCLUDES = \
  -I$(top_srcdir)/third-party/googletest/googletest/include \
  -I$(top_srcdir)/third-party/googletest/googletest

libgtest_la_SOURCES = \
  $(GTEST_DIR)/googletest/src/gtest-all.cc

libgtest_la_LDFLAGS = \
  -pthread

libgtest_la_CPPFLAGS = \
  $(GOOGLE_TEST_INCLUDES)

libgtest_main_la_SOURCES = \
  $(GTEST_DIR)/googletest/src/gtest_main.cc

libgtest_main_la_CPPFLAGS = \
  $(GOOGLE_TEST_INCLUDES)

libgtest_main_la_LIBADD = \
  $(top_builddir)/tests/gtest/libgtest.la

EXTRA_DIST = \
  CMakeLists.txt
