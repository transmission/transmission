SUBDIRS = icons

AM_CPPFLAGS = \
    -I$(top_srcdir) \
    -I$(top_srcdir)/third-party/ \
    $(LIBEVENT_CPPFLAGS) \
    -DTRANSMISSIONLOCALEDIR=\""$(transmissionlocaledir)"\"

AM_CFLAGS = \
    $(GTK_CFLAGS) \
    $(OPENSSL_CFLAGS) \
    $(PTHREAD_CFLAGS) \
    $(GIO_CFLAGS) \
    $(LIBNOTIFY_CFLAGS)

noinst_HEADERS = \
    actions.h \
    conf.h \
    details.h \
    dialogs.h \
    hig.h \
    ipc.h \
    file-list.h \
    lock.h \
    logo.h \
    makemeta-ui.h \
    msgwin.h \
    open-dialog.h \
    stats.h \
    sexy-icon-entry.h \
    torrent-cell-renderer.h \
    tr-core.h \
    tr-icon.h \
    tr-io.h \
    tr-prefs.h \
    tr-torrent.h \
    tr-window.h \
    ui.h \
    util.h

bin_PROGRAMS = transmission

transmission_SOURCES = \
    actions.c \
    conf.c \
    details.c \
    dialogs.c \
    file-list.c \
    hig.c \
    ipc.c \
    main.c \
    makemeta-ui.c \
    msgwin.c \
    notify.c \
    open-dialog.c \
    sexy-icon-entry.c \
    stats.c \
    torrent-cell-renderer.c \
    tr-core.c \
    tr-icon.c \
    tr-io.c \
    tr-prefs.c \
    tr-torrent.c \
    tr-window.c \
    util.c

dist_man_MANS = transmission.1

transmission_LDADD = \
    $(top_builddir)/libtransmission/libtransmission.a \
    $(top_builddir)/third-party/libevent/libevent.la \
    $(top_builddir)/third-party/miniupnp/libminiupnp.a \
    $(top_builddir)/third-party/libnatpmp/libnatpmp.a \
    $(GTK_LIBS) \
    $(GIO_LIBS) \
    $(LIBNOTIFY_LIBS) \
    $(OPENSSL_LIBS) \
    $(PTHREAD_LIBS) -lm

DESKTOP_IN_FILES=transmission.desktop.in
DESKTOP_FILES=$(DESKTOP_IN_FILES:.desktop.in=.desktop)
@INTLTOOL_DESKTOP_RULE@

Productivitydir = $(datadir)/applications
Productivity_DATA = $(DESKTOP_FILES)

icon_DATA = transmission.png
icondir = $(datadir)/pixmaps

EXTRA_DIST = \
    $(DESKTOP_IN_FILES) \
    transmission.png

DISTCLEANFILES = \
    transmission.desktop

