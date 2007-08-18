AM_CPPFLAGS = -I. -I$(top_srcdir) -D__TRANSMISSION__ $(LIBEVENT_CPPFLAGS)

AM_CFLAGS = $(PTHREAD_CFLAGS)

noinst_LIBRARIES = libtransmission.a

EXTRA_DIST = version.h.in

libtransmission_a_SOURCES = \
    basename.c \
    bencode.c \
    choking.c \
    clients.c \
    completion.c \
    dirname.c \
    fastresume.c \
    fdlimit.c \
    http.c \
    inout.c \
    ipcparse.c \
    list.c \
    makemeta.c \
    metainfo.c \
    natpmp.c \
    net.c \
    peer.c \
    platform.c \
    ptrarray.c \
    publish.c \
    ratecontrol.c \
    sha1.c \
    shared.c \
    strlcat.c \
    strlcpy.c \
    timer.c \
    torrent.c \
    tracker.c \
    transmission.c \
    trevent.c \
    upnp.c \
    utils.c \
    xml.c

noinst_HEADERS = \
    bencode.h \
    bsdqueue.h \
    bsdtree.h \
    choking.h \
    clients.h \
    completion.h \
    fastresume.h \
    fdlimit.h \
    http.h \
    inout.h \
    internal.h \
    ipcparse.h \
    list.h \
    makemeta.h \
    metainfo.h \
    natpmp.h \
    net.h \
    peeraz.h \
    peerext.h \
    peer.h \
    peermessages.h \
    peerparse.h \
    peertree.h \
    peerutils.h \
    platform.h \
    ptrarray.h \
    publish.h \
    ratecontrol.h \
    sha1.h \
    shared.h \
    timer.h \
    tracker.h \
    transmission.h \
    trcompat.h \
    trevent.h \
    upnp.h \
    utils.h \
    xml.h
