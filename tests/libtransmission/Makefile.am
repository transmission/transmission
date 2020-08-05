#check_PROGRAMS = gtest
#
#gtest_SOURCES = gtest.cpp
#gtest_LDADD = libgtest.la
#gtest_LDFLAGS = -pthread
#
#gtest_CPPFLAGS = \
#  -I$(top_srcdir)/googletest/googletest/include \
#  -I$(top_srcdir)/googletest/googletest \
#  -pthread

AM_CPPFLAGS = \
  -std=gnu++17 \
  -D__TRANSMISSION__ \
  @LIBEVENT_CFLAGS@ \
  @LIBCURL_CFLAGS@ \
  -I$(top_srcdir)/third-party/googletest/googletest/include \
  -I$(top_srcdir)/libtransmission \
  -I$(srcdir) \
  -pthread

LDADD = \
  $(top_builddir)/tests/gtest/libgtest_main.la \
  $(top_builddir)/tests/gtest/libgtest.la \
  $(top_builddir)/libtransmission/libtransmission.a \
  @LIBUPNP_LIBS@ \
  @LIBNATPMP_LIBS@ \
  @DHT_LIBS@ \
  @LIBB64_LIBS@ \
  @LIBUTP_LIBS@ \
  @GTK_LIBS@ \
  @LIBAPPINDICATOR_LIBS@ \
  @LIBEVENT_LIBS@ \
  @LIBCURL_LIBS@ \
  @CRYPTO_LIBS@ \
  @ZLIB_LIBS@ \
  @PTHREAD_LIBS@ \
  ${LIBM}

TESTS = \
  test_bitfield \
  test_blocklist \
  test_clients \
  test_crypto \
  test_error \
  test_file \
  test_getopt \
  test_history \
  test_json \
  test_magnet \
  test_makemeta \
  test_metainfo \
  test_move \
  test_peer_msgs \
  test_quark \
  test_rename \
  test_rpc \
  test_session \
  test_subprocess \
  test_utils \
  test_variant \
  test_watchdir

check_PROGRAMS = $(TESTS)

test_bitfield_SOURCES = bitfield-test.cc
test_blocklist_SOURCES = blocklist-test.cc
test_clients_SOURCES = clients-test.cc
test_crypto_SOURCES = crypto-test.cc
test_error_SOURCES = error-test.cc
test_file_SOURCES = file-test.cc
test_getopt_SOURCES = getopt-test.cc
test_history_SOURCES = history-test.cc
test_json_SOURCES = json-test.cc
test_magnet_SOURCES = magnet-test.cc
test_makemeta_SOURCES = makemeta-test.cc
test_metainfo_SOURCES = metainfo-test.cc
test_move_SOURCES = move-test.cc
test_peer_msgs_SOURCES = peer-msgs-test.cc
test_quark_SOURCES = quark-test.cc
test_rename_SOURCES = rename-test.cc
test_rpc_SOURCES = rpc-test.cc
test_session_SOURCES = session-test.cc
test_subprocess_SOURCES = subprocess-test.cc
test_utils_SOURCES = utils-test.cc
test_variant_SOURCES = variant-test.cc
test_watchdir_SOURCES = watchdir-test.cc

EXTRA_DIST = \
  CMakeLists.txt \
  subprocess-test.cmd \
  crypto-test-ref.h \
  test-fixtures.h
