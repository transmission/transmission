AM_CPPFLAGS = -I. -I$(top_srcdir) -I$(top_srcdir)/third-party/ -D__TRANSMISSION__ $(LIBEVENT_CPPFLAGS)
AM_CFLAGS = $(OPENSSL_CFLAGS) $(PTHREAD_CFLAGS)

noinst_LIBRARIES = libtransmission.a

libtransmission_a_SOURCES = \
    bencode.c \
    clients.c \
    completion.c \
    crypto.c \
    fastresume.c \
    fdlimit.c \
    handshake.c \
    inout.c \
    ipcparse.c \
    list.c \
    makemeta.c \
    metainfo.c \
    natpmp.c \
    net.c \
    peer-io.c \
    peer-mgr.c \
    peer-msgs.c \
    platform.c \
    ptrarray.c \
    publish.c \
    ratecontrol.c \
    shared.c \
    stats.c \
    torrent.c \
    torrent-ctor.c \
    tracker.c \
    transmission.c \
    trevent.c \
    upnp.c \
    utils.c \
    verify.c

noinst_HEADERS = \
    bencode.h \
    clients.h \
    crypto.h \
    completion.h \
    fastresume.h \
    fdlimit.h \
    handshake.h \
    inout.h \
    internal.h \
    ipcparse.h \
    list.h \
    makemeta.h \
    metainfo.h \
    natpmp.h \
    net.h \
    peer-io.h \
    peer-mgr.h \
    peer-mgr-private.h \
    peer-msgs.h \
    platform.h \
    ptrarray.h \
    publish.h \
    ratecontrol.h \
    shared.h \
    stats.h \
    torrent.h \
    tracker.h \
    transmission.h \
    trcompat.h \
    trevent.h \
    upnp.h \
    utils.h \
    verify.h

bin_PROGRAMS = benc2php

TESTS = \
    bencode-test \
    test-fastset \
    test-peer-id

noinst_PROGRAMS = $(TESTS)

APPS_LDADD = \
    ./libtransmission.a  \
    $(top_builddir)/third-party/miniupnp/libminiupnp.a \
    $(top_builddir)/third-party/libnatpmp/libnatpmp.a \
    $(top_builddir)/third-party/libevent/libevent.la \
    $(OPENSSL_LIBS) $(PTHREAD_LIBS) -lm

benc2php_SOURCES = benc2php.c
benc2php_LDADD = $(APPS_LDADD)
bencode_test_SOURCES = bencode-test.c
bencode_test_LDADD = $(APPS_LDADD)
test_fastset_SOURCES = test-fastset.c
test_fastset_LDADD = $(APPS_LDADD)
test_peer_id_SOURCES = test-peer-id.c
test_peer_id_LDADD = $(APPS_LDADD)



EXTRA_libtransmission_a_SOURCES = \
    version.h

BUILT_SOURCES = \
    version.h

version.h:
	echo '#define PEERID_PREFIX         "'@PEERID_PREFIX@'"' > version.h
	echo '#define USERAGENT_PREFIX      "'@USERAGENT_PREFIX@'"' >> version.h
	echo '#define SVN_REVISION          "'`svn info | grep "Revision" | awk -F': ' '{print $$2}'`'"' >> version.h
	echo '#define SHORT_VERSION_STRING  "'@USERAGENT_PREFIX@'"' >> version.h
	echo '#define LONG_VERSION_STRING   "'@USERAGENT_PREFIX@' ('`svn info | grep "Revision" | awk -F': ' '{print $$2}'`')"' >> version.h
