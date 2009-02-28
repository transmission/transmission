AM_CPPFLAGS = -I@top_srcdir@ $(LIBEVENT_CPPFLAGS) -I$(top_srcdir)/third-party/ -DEMBEDDED

AM_CFLAGS = \
    $(OPENSSL_CFLAGS) \
    $(LIBCURL_CFLAGS) \
    $(ZLIB_CFLAGS) \
    $(PTHREAD_CFLAGS)

AM_LDFLAGS = \
    $(ZLIB_LDFLAGS)

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
    $(top_builddir)/third-party/libevent/libevent.la \
    $(INTLLIBS) \
    $(LIBCURL_LIBS) \
    $(ZLIB_LIBS) \
    $(OPENSSL_LIBS) \
    $(PTHREAD_LIBS) -lm

transmission_daemon_SOURCES = daemon.c
transmission_remote_SOURCES = remote.c
