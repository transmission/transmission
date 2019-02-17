AM_CPPFLAGS = -I@top_srcdir@

AM_CFLAGS = \
    @LIBEVENT_CFLAGS@ \
    @LIBCURL_CFLAGS@ \
    @ZLIB_CFLAGS@ \
    @PTHREAD_CFLAGS@

bin_PROGRAMS = \
    transmission-create \
    transmission-edit \
    transmission-remote \
    transmission-show

transmission_create_SOURCES = create.c
transmission_edit_SOURCES = edit.c
transmission_remote_SOURCES = remote.c
transmission_show_SOURCES = show.c

noinst_HEADERS = \
    units.h

dist_man_MANS = \
    transmission-create.1 \
    transmission-edit.1 \
    transmission-remote.1 \
    transmission-show.1

transmission_create_LDADD = \
    $(top_builddir)/libtransmission/libtransmission.a \
    @LIBUPNP_LIBS@ \
    @LIBNATPMP_LIBS@ \
    @INTLLIBS@ \
    @DHT_LIBS@ \
    @LIBB64_LIBS@ \
    @LIBUTP_LIBS@ \
    @LIBEVENT_LIBS@ \
    @LIBCURL_LIBS@ \
    @CRYPTO_LIBS@ \
    @ZLIB_LIBS@ \
    @PTHREAD_LIBS@ \
    ${LIBM}
transmission_edit_LDADD = $(transmission_create_LDADD)
transmission_remote_LDADD = $(transmission_create_LDADD)
transmission_show_LDADD = $(transmission_create_LDADD)
