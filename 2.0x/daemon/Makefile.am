AM_CPPFLAGS = -I@top_srcdir@ -DEMBEDDED

AM_CFLAGS = \
    @LIBEVENT_CFLAGS@ \
    @OPENSSL_CFLAGS@ \
    @LIBCURL_CFLAGS@ \
    @ZLIB_CFLAGS@ \
    @PTHREAD_CFLAGS@

AM_LDFLAGS = \
    @ZLIB_LDFLAGS@

dist_man_MANS = \
    transmission-daemon.1 \
    transmission-remote.1

bin_PROGRAMS = \
    transmission-daemon \
    transmission-remote

LDADD = \
    $(top_builddir)/libtransmission/libtransmission.a \
    $(top_builddir)/third-party/miniupnp/libminiupnp.a \
    $(top_builddir)/third-party/libnatpmp/libnatpmp.a \
    @DHT_LIBS@ \
    @LIBEVENT_LIBS@ \
    @LIBCURL_LIBS@ \
    @OPENSSL_LIBS@ \
    @INTLLIBS@ \
    @ZLIB_LIBS@ \
    @PTHREAD_LIBS@

noinst_HEADERS = \
    watch.h

transmission_daemon_SOURCES = daemon.c watch.c
transmission_remote_SOURCES = remote.c
