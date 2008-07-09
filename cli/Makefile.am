AM_CPPFLAGS = -I$(top_srcdir) $(LIBEVENT_CPPFLAGS)
AM_CFLAGS = $(OPENSSL_CFLAGS) $(LIBCURL_CFLAGS) $(PTHREAD_CFLAGS)

bin_PROGRAMS = transmissioncli

dist_man_MANS = transmissioncli.1

transmissioncli_SOURCES = cli.c

transmissioncli_LDADD = \
  $(top_builddir)/libtransmission/libtransmission.a \
  $(top_builddir)/third-party/libevent/libevent_core.la \
  $(top_builddir)/third-party/libnatpmp/libnatpmp.a \
  $(top_builddir)/third-party/miniupnp/libminiupnp.a \
  $(top_builddir)/third-party/shttpd/libshttpd.a \
  $(INTLLIBS) \
  $(OPENSSL_LIBS) \
  $(LIBCURL_LIBS) \
  $(PTHREAD_LIBS) \
  -lm

