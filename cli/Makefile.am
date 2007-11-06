AM_CPPFLAGS = -I$(top_srcdir) $(LIBEVENT_CPPFLAGS)
AM_CFLAGS = $(OPENSSL_CFLAGS) $(PTHREAD_CFLAGS)

bin_PROGRAMS = transmission-cli

dist_man_MANS = transmissioncli.1

transmission_cli_SOURCES = transmissioncli.c

transmission_cli_LDADD = \
    $(top_builddir)/libtransmission/libtransmission.a \
    $(top_builddir)/third-party/libevent/libevent.la \
    $(top_builddir)/third-party/miniupnp/libminiupnp.a \
    $(OPENSSL_LIBS) $(PTHREAD_LIBS) -lm

