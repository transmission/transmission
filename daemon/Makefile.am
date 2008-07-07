AM_CPPFLAGS = -I@top_srcdir@ $(LIBEVENT_CPPFLAGS) -I$(top_srcdir)/third-party/ -DEMBEDDED
AM_CFLAGS = $(OPENSSL_CFLAGS) $(LIBCURL_CFLAGS) $(PTHREAD_CFLAGS)

dist_man_MANS = \
    transmission-daemon.1 \
    transmission-remote.1

bin_PROGRAMS = \
    transmission-daemon \
    transmission-remote

LDADD = \
    $(top_builddir)/libtransmission/libtransmission.a \
    $(top_builddir)/third-party/shttpd/libshttpd.a \
    $(top_builddir)/third-party/miniupnp/libminiupnp.a \
    $(top_builddir)/third-party/libnatpmp/libnatpmp.a \
    $(top_builddir)/third-party/libevent/libevent_core.la \
    $(INTLLIBS) \
    $(OPENSSL_LIBS) \
    $(LIBCURL_LIBS) \
    $(PTHREAD_LIBS) -lm

noinst_HEADERS = getopts.h

transmission_daemon_SOURCES = daemon.c
transmission_remote_SOURCES = remote.c getopts.c
