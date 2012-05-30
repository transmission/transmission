noinst_LIBRARIES = libminiupnp.a

AM_CFLAGS = @PTHREAD_CFLAGS@ -DNDEBUG -D_GNU_SOURCE

libminiupnp_a_SOURCES = \
    connecthostport.c \
    igd_desc_parse.c \
    minisoap.c \
    minissdpc.c \
    miniupnpc.c \
    miniwget.c \
    minixml.c \
    portlistingparse.c \
    receivedata.c \
    upnpcommands.c \
    upnpreplyparse.c

noinst_HEADERS = \
    bsdqueue.h \
    codelength.h \
    connecthostport.h \
    declspec.h \
    igd_desc_parse.h \
    minisoap.h \
    minissdpc.h \
    miniupnpc.h \
    miniupnpctypes.h \
    miniwget.h \
    minixml.h \
    portlistingparse.h \
    receivedata.h \
    upnpcommands.h \
    upnpreplyparse.h

EXTRA_DIST = \
    README \
    LICENSE \
    miniupnpcstrings.h.in \
    updateminiupnpcstrings.sh

BUILT_SOURCES = \
    miniupnpcstrings.h

miniupnpcstrings.h: Makefile
	$(srcdir)/updateminiupnpcstrings.sh $(srcdir)/VERSION $(srcdir)/miniupnpcstrings.h.in $@

DISTCLEANFILES = $(builddir)/miniupnpcstrings.h
