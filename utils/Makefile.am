AM_CPPFLAGS = -I@top_srcdir@

AM_CFLAGS = \
    @LIBEVENT_CFLAGS@ \
    @LIBCURL_CFLAGS@ \
    @OPENSSL_CFLAGS@ \
    @ZLIB_CFLAGS@ \
    @PTHREAD_CFLAGS@

AM_LDFLAGS = \
    @ZLIB_LDFLAGS@

bin_PROGRAMS = \
    transmission-create \
    transmission-edit \
    transmission-show

transmission_create_SOURCES = create.c
transmission_edit_SOURCES = edit.c
transmission_show_SOURCES = show.c

dist_man_MANS = \
    transmission-create.1 \
    transmission-edit.1 \
    transmission-show.1

transmission_create_LDADD = \
    $(top_builddir)/libtransmission/libtransmission.a \
    $(top_builddir)/third-party/miniupnp/libminiupnp.a \
    $(top_builddir)/third-party/libnatpmp/libnatpmp.a \
    @INTLLIBS@ \
    @DHT_LIBS@ \
    @LIBUTP_LIBS@ \
    @LIBEVENT_LIBS@ \
    @LIBCURL_LIBS@ \
    @OPENSSL_LIBS@ \
    @ZLIB_LIBS@ \
    @PTHREAD_LIBS@
transmission_edit_LDADD = $(transmission_create_LDADD)
transmission_show_LDADD = $(transmission_create_LDADD)
