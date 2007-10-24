AM_CPPFLAGS = -I. -I$(top_srcdir) -D__TRANSMISSION__ $(LIBEVENT_CPPFLAGS)

AM_CFLAGS = $(PTHREAD_CFLAGS)

noinst_LIBRARIES = libtransmission.a

libtransmission_a_SOURCES = \
    basename.c \
    bencode.c \
    clients.c \
    completion.c \
    crypto.c \
    dirname.c \
    fastresume.c \
    fdlimit.c \
    handshake.c \
    http.c \
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
    strlcat.c \
    strlcpy.c \
    torrent.c \
    tracker.c \
    transmission.c \
    trevent.c \
    upnp.c \
    utils.c \
    xml.c

noinst_HEADERS = \
    bencode.h \
    bsdtree.h \
    clients.h \
    crypto.h \
    completion.h \
    fastresume.h \
    fdlimit.h \
    handshake.h \
    http.h \
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
    utils.h \
    xml.h

BUILT_SOURCES = version.h

EXTRA_DIST = \
	version.h \
	version.h.in

do_subst = sed \
	-e 's,[@]PEERID_PREFIX[@],$(PEERID_PREFIX),g' \
	-e 's,[@]USERAGENT_PREFIX[@],$(USERAGENT_PREFIX),g' \
	-e 's,[@]SVN_REVISION[@],$(SVN_REVISION),g' \
	-e 's,[@]SHORT_VERSION_STRING[@],$(SHORT_VERSION_STRING),g' \
	-e 's,[@]LONG_VERSION_STRING[@],$(LONG_VERSION_STRING),g' 

version.h: version.h.in
	$(do_subst) < $(srcdir)/version.h.in > version.h

CLEANFILES = \
	version.h
