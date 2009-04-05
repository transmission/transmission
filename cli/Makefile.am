AM_CPPFLAGS = -I$(top_srcdir) $(LIBEVENT_CPPFLAGS)

AM_CFLAGS = \
    $(OPENSSL_CFLAGS) \
    $(LIBCURL_CFLAGS) \
    $(ZLIB_CFLAGS) \
    $(PTHREAD_CFLAGS)

AM_LDFLAGS = \
    $(ZLIB_LDFLAGS)

bin_PROGRAMS = transmissioncli

dist_man_MANS = transmissioncli.1

transmissioncli_SOURCES = cli.c

transmissioncli_LDADD = \
    $(top_builddir)/libtransmission/libtransmission.a \
    $(top_builddir)/third-party/libevent/libevent.la \
    $(top_builddir)/third-party/libnatpmp/libnatpmp.a \
    $(top_builddir)/third-party/miniupnp/libminiupnp.a \
    $(INTLLIBS) \
    $(LIBCURL_LIBS) \
    $(ZLIB_LIBS) \
    $(OPENSSL_LIBS) \
    $(PTHREAD_LIBS) \
    -lm

