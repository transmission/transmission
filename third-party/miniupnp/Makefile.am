noinst_LIBRARIES = libminiupnp.a

AM_CFLAGS = @PTHREAD_CFLAGS@ -DNDEBUG

libminiupnp_a_SOURCES = \
    igd_desc_parse.c \
    minisoap.c \
    minissdpc.c \
    miniupnpc.c \
    miniwget.c \
    minixml.c \
    upnpcommands.c \
    upnpreplyparse.c

noinst_HEADERS = \
    bsdqueue.h \
    codelength.h \
    declspec.h \
    igd_desc_parse.h \
    minisoap.h \
    minissdpc.h \
    miniupnpc.h \
    miniwget.h \
    minixml.h \
    upnpcommands.h \
    upnpreplyparse.h

EXTRA_DIST = \
    README \
    LICENCE \
    miniupnpcstrings.h.in \
    updateminiupnpcstrings.sh

BUILT_SOURCES = \
    miniupnpcstrings.h

miniupnpcstrings.h: Makefile
	$(srcdir)/updateminiupnpcstrings.sh $(srcdir)/miniupnpcstrings.h.in $@

DISTCLEANFILES = $(builddir)/miniupnpcstrings.h
