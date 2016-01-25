AM_CPPFLAGS = \
  -I$(top_srcdir) \
  -D__TRANSMISSION__ \
  -DPACKAGE_DATA_DIR=\""$(datadir)"\"

AM_CFLAGS = \
  @DHT_CFLAGS@ \
  @LIBB64_CFLAGS@ \
  @LIBUTP_CFLAGS@ \
  @LIBUPNP_CFLAGS@ \
  @LIBNATPMP_CFLAGS@ \
  @LIBEVENT_CFLAGS@ \
  @LIBCURL_CFLAGS@ \
  @CRYPTO_CFLAGS@ \
  @PTHREAD_CFLAGS@ \
  @ZLIB_CFLAGS@

noinst_LIBRARIES = libtransmission.a

libtransmission_a_SOURCES = \
  announcer.c \
  announcer-http.c \
  announcer-udp.c \
  bandwidth.c \
  bitfield.c \
  blocklist.c \
  cache.c \
  clients.c \
  completion.c \
  ConvertUTF.c \
  crypto.c \
  crypto-utils.c \
  crypto-utils-fallback.c \
  error.c \
  fdlimit.c \
  file.c \
  handshake.c \
  history.c \
  inout.c \
  list.c \
  log.c \
  magnet.c \
  makemeta.c \
  metainfo.c \
  natpmp.c \
  net.c \
  peer-io.c \
  peer-mgr.c \
  peer-msgs.c \
  platform.c \
  platform-quota.c \
  port-forwarding.c \
  ptrarray.c \
  quark.c \
  resume.c \
  rpcimpl.c \
  rpc-server.c \
  session.c \
  stats.c \
  torrent.c \
  torrent-ctor.c \
  torrent-magnet.c \
  tr-dht.c \
  tr-lpd.c \
  tr-udp.c \
  tr-utp.c \
  tr-getopt.c \
  trevent.c \
  upnp.c \
  utils.c \
  variant.c \
  variant-benc.c \
  variant-json.c \
  verify.c \
  watchdir.c \
  watchdir-generic.c \
  web.c \
  webseed.c \
  wildmat.c

if USE_INOTIFY
libtransmission_a_SOURCES += watchdir-inotify.c
AM_CPPFLAGS += -DWITH_INOTIFY
endif

if USE_KQUEUE
libtransmission_a_SOURCES += watchdir-kqueue.c
AM_CPPFLAGS += -DWITH_KQUEUE
endif

if WIN32
libtransmission_a_SOURCES += file-win32.c watchdir-win32.c
else
libtransmission_a_SOURCES += file-posix.c
endif

if CRYPTO_USE_OPENSSL
libtransmission_a_SOURCES += crypto-utils-openssl.c
endif
if CRYPTO_USE_CYASSL
libtransmission_a_SOURCES += crypto-utils-cyassl.c
endif
if CRYPTO_USE_POLARSSL
libtransmission_a_SOURCES += crypto-utils-polarssl.c
endif

noinst_HEADERS = \
  announcer.h \
  announcer-common.h \
  bandwidth.h \
  bitfield.h \
  blocklist.h \
  cache.h \
  clients.h \
  ConvertUTF.h \
  crypto.h \
  crypto-utils.h \
  completion.h \
  error.h \
  error-types.h \
  fdlimit.h \
  file.h \
  handshake.h \
  history.h \
  inout.h \
  jsonsl.c \
  jsonsl.h \
  libtransmission-test.h \
  list.h \
  log.h \
  magnet.h \
  makemeta.h \
  metainfo.h \
  natpmp_local.h \
  net.h \
  peer-common.h \
  peer-io.h \
  peer-mgr.h \
  peer-msgs.h \
  platform.h \
  platform-quota.h \
  port-forwarding.h \
  ptrarray.h \
  quark.h \
  resume.h \
  rpcimpl.h \
  rpc-server.h \
  session.h \
  stats.h \
  torrent.h \
  torrent-magnet.h \
  tr-getopt.h \
  transmission.h \
  tr-dht.h \
  tr-udp.h \
  tr-utp.h \
  tr-lpd.h \
  trevent.h \
  upnp.h \
  utils.h \
  variant.h \
  variant-common.h \
  verify.h \
  version.h \
  watchdir.h \
  watchdir-common.h \
  web.h \
  webseed.h

TESTS = \
  bitfield-test \
  blocklist-test \
  clients-test \
  crypto-test \
  error-test \
  file-test \
  history-test \
  json-test \
  magnet-test \
  makemeta-test \
  metainfo-test \
  move-test \
  peer-msgs-test \
  quark-test \
  rename-test \
  rpc-test \
  session-test \
  tr-getopt-test \
  utils-test \
  variant-test \
  watchdir-test \
  watchdir-generic-test

noinst_PROGRAMS = $(TESTS)

apps_ldadd = \
  ./libtransmission.a  \
  @LIBUPNP_LIBS@ \
  @LIBNATPMP_LIBS@ \
  @INTLLIBS@ \
  @DHT_LIBS@ \
  @LIBB64_LIBS@ \
  @LIBUTP_LIBS@ \
  @LIBCURL_LIBS@ \
  @LIBEVENT_LIBS@ \
  @CRYPTO_LIBS@ \
  @PTHREAD_LIBS@ \
  @ZLIB_LIBS@ \
  ${LIBM}

TEST_SOURCES = libtransmission-test.c

bitfield_test_SOURCES = bitfield-test.c $(TEST_SOURCES)
bitfield_test_LDADD = ${apps_ldadd}
bitfield_test_LDFLAGS = ${apps_ldflags}

blocklist_test_SOURCES = blocklist-test.c $(TEST_SOURCES)
blocklist_test_LDADD = ${apps_ldadd}
blocklist_test_LDFLAGS = ${apps_ldflags}

clients_test_SOURCES = clients-test.c $(TEST_SOURCES)
clients_test_LDADD = ${apps_ldadd}
clients_test_LDFLAGS = ${apps_ldflags}

crypto_test_SOURCES = crypto-test.c crypto-test-ref.h $(TEST_SOURCES)
crypto_test_LDADD = ${apps_ldadd}
crypto_test_LDFLAGS = ${apps_ldflags}

error_test_SOURCES = error-test.c $(TEST_SOURCES)
error_test_LDADD = ${apps_ldadd}
error_test_LDFLAGS = ${apps_ldflags}

file_test_SOURCES = file-test.c $(TEST_SOURCES)
file_test_LDADD = ${apps_ldadd}
file_test_LDFLAGS = ${apps_ldflags}

history_test_SOURCES = history-test.c $(TEST_SOURCES)
history_test_LDADD = ${apps_ldadd}
history_test_LDFLAGS = ${apps_ldflags}

json_test_SOURCES = json-test.c $(TEST_SOURCES)
json_test_LDADD = ${apps_ldadd}
json_test_LDFLAGS = ${apps_ldflags}

quark_test_SOURCES = quark-test.c $(TEST_SOURCES)
quark_test_LDADD = ${apps_ldadd}
quark_test_LDFLAGS = ${apps_ldflags}

magnet_test_SOURCES = magnet-test.c $(TEST_SOURCES)
magnet_test_LDADD = ${apps_ldadd}
magnet_test_LDFLAGS = ${apps_ldflags}

metainfo_test_SOURCES = metainfo-test.c $(TEST_SOURCES)
metainfo_test_LDADD = ${apps_ldadd}
metainfo_test_LDFLAGS = ${apps_ldflags}

makemeta_test_SOURCES = makemeta-test.c $(TEST_SOURCES)
makemeta_test_LDADD = ${apps_ldadd}
makemeta_test_LDFLAGS = ${apps_ldflags}

move_test_SOURCES = move-test.c $(TEST_SOURCES)
move_test_LDADD = ${apps_ldadd}
move_test_LDFLAGS = ${apps_ldflags}

peer_msgs_test_SOURCES = peer-msgs-test.c $(TEST_SOURCES)
peer_msgs_test_LDADD = ${apps_ldadd}
peer_msgs_test_LDFLAGS = ${apps_ldflags}

rpc_test_SOURCES = rpc-test.c $(TEST_SOURCES)
rpc_test_LDADD = ${apps_ldadd}
rpc_test_LDFLAGS = ${apps_ldflags}

session_test_SOURCES = session-test.c $(TEST_SOURCES)
session_test_LDADD = ${apps_ldadd}
session_test_LDFLAGS = ${apps_ldflags}

tr_getopt_test_SOURCES = tr-getopt-test.c $(TEST_SOURCES)
tr_getopt_test_LDADD = ${apps_ldadd}
tr_getopt_test_LDFLAGS = ${apps_ldflags}

utils_test_SOURCES = utils-test.c $(TEST_SOURCES)
utils_test_LDADD = ${apps_ldadd}
utils_test_LDFLAGS = ${apps_ldflags}

variant_test_SOURCES = variant-test.c $(TEST_SOURCES)
variant_test_LDADD = ${apps_ldadd}
variant_test_LDFLAGS = ${apps_ldflags}

watchdir_test_SOURCES = watchdir-test.c $(TEST_SOURCES)
watchdir_test_LDADD = ${apps_ldadd}
watchdir_test_LDFLAGS = ${apps_ldflags}

watchdir_generic_test_SOURCES = watchdir-test.c $(TEST_SOURCES)
watchdir_generic_test_LDADD = ${apps_ldadd}
watchdir_generic_test_LDFLAGS = ${apps_ldflags}
watchdir_generic_test_CPPFLAGS = -DWATCHDIR_TEST_FORCE_GENERIC $(AM_CPPFLAGS)

rename_test_SOURCES = rename-test.c $(TEST_SOURCES)
rename_test_LDADD = ${apps_ldadd}
rename_test_LDFLAGS = ${apps_ldflags}
