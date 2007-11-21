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
    tracker.c \
    transmission.c \
    trevent.c \
    upnp.c \
    utils.c

noinst_HEADERS = \
    bencode.h \
    bsdtree.h \
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
    tracker.h \
    transmission.h \
    trcompat.h \
    trevent.h \
    upnp.h \
    utils.h

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
