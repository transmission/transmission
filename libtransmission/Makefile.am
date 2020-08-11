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
  jsonsl.c \
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
  session-id.c \
  stats.c \
  torrent.c \
  torrent-ctor.c \
  torrent-magnet.c \
  tr-assert.c \
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
libtransmission_a_SOURCES += file-win32.c subprocess-win32.c watchdir-win32.c
else
libtransmission_a_SOURCES += file-posix.c subprocess-posix.c
endif

if CRYPTO_USE_OPENSSL
libtransmission_a_SOURCES += crypto-utils-openssl.c
endif
if CRYPTO_USE_CYASSL
libtransmission_a_SOURCES += crypto-utils-cyassl.c
if CYASSL_IS_WOLFSSL
AM_CPPFLAGS += -DCYASSL_IS_WOLFSSL
endif
endif
if CRYPTO_USE_POLARSSL
libtransmission_a_SOURCES += crypto-utils-polarssl.c
if POLARSSL_IS_MBEDTLS
AM_CPPFLAGS += -DPOLARSSL_IS_MBEDTLS
endif
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
  peer-socket.h \
  platform.h \
  platform-quota.h \
  port-forwarding.h \
  ptrarray.h \
  quark.h \
  resume.h \
  rpcimpl.h \
  rpc-server.h \
  session.h \
  session-id.h \
  stats.h \
  subprocess.h \
  torrent.h \
  torrent-magnet.h \
  tr-getopt.h \
  transmission.h \
  tr-assert.h \
  tr-dht.h \
  tr-macros.h \
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

