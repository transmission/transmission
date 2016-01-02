AM_CPPFLAGS = -I@top_srcdir@ -DEMBEDDED

AM_CFLAGS = \
    @LIBEVENT_CFLAGS@ \
    @LIBCURL_CFLAGS@ \
    @SYSTEMD_DAEMON_CFLAGS@ \
    @ZLIB_CFLAGS@ \
    @PTHREAD_CFLAGS@

EXTRA_DIST = \
    transmission-daemon.service

dist_man_MANS = \
    transmission-daemon.1 \
    transmission-remote.1

bin_PROGRAMS = \
    transmission-daemon \
    transmission-remote

LDADD = \
    $(top_builddir)/libtransmission/libtransmission.a \
    @LIBUPNP_LIBS@ \
    @LIBNATPMP_LIBS@ \
    @DHT_LIBS@ \
    @LIBB64_LIBS@ \
    @LIBUTP_LIBS@ \
    @LIBEVENT_LIBS@ \
    @LIBCURL_LIBS@ \
    @CRYPTO_LIBS@ \
    @INTLLIBS@ \
    @SYSTEMD_DAEMON_LIBS@ \
    @ZLIB_LIBS@ \
    @PTHREAD_LIBS@ \
    ${LIBM}

noinst_HEADERS = daemon.h

transmission_daemon_SOURCES = daemon.c
transmission_remote_SOURCES = remote.c

if WIN32
transmission_daemon_SOURCES += daemon-win32.c
else
transmission_daemon_SOURCES += daemon-posix.c
endif
