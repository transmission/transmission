AM_CPPFLAGS = -I@top_srcdir@ $(LIBEVENT_CPPFLAGS)
AM_CFLAGS = $(OPENSSL_CFLAGS) $(PTHREAD_CFLAGS)

noinst_LIBRARIES = libdaemon.a

dist_man_MANS = \
    transmission-daemon.1 \
    transmission-proxy.1 \
    transmission-remote.1

libdaemon_a_SOURCES = \
    errors.c \
    misc.c

noinst_HEADERS = \
    bsdqueue.h \
    client.h \
    errors.h \
    misc.h \
    server.h \
    torrents.h

bin_PROGRAMS = \
    transmission-daemon \
    transmission-remote \
    transmission-proxy

COMMON_LDADD = \
    ./libdaemon.a \
    $(top_builddir)/libtransmission/libtransmission.a \
    $(top_builddir)/third-party/miniupnp/libminiupnp.a \
    $(top_builddir)/third-party/libnatpmp/libnatpmp.a \
    $(top_builddir)/third-party/libevent/libevent.la \
    $(OPENSSL_LIBS) $(PTHREAD_LIBS) -lm

transmission_daemon_SOURCES = daemon.c server.c torrents.c
transmission_daemon_LDADD = $(COMMON_LDADD)
transmission_remote_SOURCES = client.c remote.c
transmission_remote_LDADD = $(COMMON_LDADD)
transmission_proxy_SOURCES = proxy.c
transmission_proxy_LDADD = $(COMMON_LDADD)
