AM_CPPFLAGS = -I@top_srcdir@

AM_CFLAGS = \
    @LIBEVENT_CFLAGS@ \
    @LIBCURL_CFLAGS@ \
    @ZLIB_CFLAGS@ \
    @PTHREAD_CFLAGS@

bin_PROGRAMS = transmission-cli

dist_man_MANS = transmission-cli.1

transmission_cli_SOURCES = cli.c

transmission_cli_LDADD = \
    $(top_builddir)/libtransmission/libtransmission.a \
    @LIBNATPMP_LIBS@ \
    @LIBUPNP_LIBS@ \
    @DHT_LIBS@ \
    @LIBB64_LIBS@ \
    @LIBUTP_LIBS@ \
    @LIBEVENT_LIBS@ \
    @LIBCURL_LIBS@ \
    @CRYPTO_LIBS@ \
    @INTLLIBS@ \
    @ZLIB_LIBS@ \
    @PTHREAD_LIBS@ \
    ${LIBM}

