SUBDIRS = icons

if TR_UNSTABLE
GTK_EXTRA_CPPFLAGS="-DG_DISABLE_DEPRECATED -DGDK_PIXBUF_DISABLE_DEPRECATED -DGDK_DISABLE_DEPRECATED -DGTK_DISABLE_DEPRECATED -DPANGO_DISABLE_DEPRECATED"
endif

AM_CPPFLAGS = \
    -I$(top_srcdir) \
    -I$(top_srcdir)/third-party/ \
    $(LIBEVENT_CPPFLAGS) \
    -DTRANSMISSIONLOCALEDIR=\""$(transmissionlocaledir)"\" \
    $(GTK_EXTRA_CPPFLAGS)

AM_CFLAGS = \
    $(GTK_CFLAGS) \
    $(OPENSSL_CFLAGS) \
    $(LIBCURL_CFLAGS) \
    $(PTHREAD_CFLAGS) \
    $(GIO_CFLAGS) \
    $(DBUS_GLIB_CFLAGS) \
    $(LIBNOTIFY_CFLAGS)

noinst_HEADERS = \
    actions.h \
    add-dialog.h \
    conf.h \
    details.h \
    dialogs.h \
    hig.h \
    file-list.h \
    lock.h \
    logo.h \
    makemeta-ui.h \
    msgwin.h \
    notify.h \
    stats.h \
    sexy-icon-entry.h \
    torrent-cell-renderer.h \
    tracker-list.h \
    tr-core.h \
    tr-core-dbus.h \
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
    add-dialog.c \
    conf.c \
    details.c \
    dialogs.c \
    file-list.c \
    hig.c \
    main.c \
    makemeta-ui.c \
    msgwin.c \
    notify.c \
    sexy-icon-entry.c \
    stats.c \
    torrent-cell-renderer.c \
    tracker-list.c \
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
    $(top_builddir)/third-party/libevent/libevent_core.la \
    $(top_builddir)/third-party/miniupnp/libminiupnp.a \
    $(top_builddir)/third-party/libnatpmp/libnatpmp.a \
    $(top_builddir)/third-party/shttpd/libshttpd.a \
    $(GTK_LIBS) \
    $(GIO_LIBS) \
    $(LIBNOTIFY_LIBS) \
    $(DBUS_GLIB_LIBS) \
    $(OPENSSL_LIBS) \
    $(LIBCURL_LIBS) \
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

