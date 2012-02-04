SUBDIRS = icons

if TR_UNSTABLE
GTK_EXTRA_CPPFLAGS=-DG_DISABLE_DEPRECATED -DGDK_PIXBUF_DISABLE_DEPRECATED -DGDK_DISABLE_DEPRECATED -DGTK_DISABLE_DEPRECATED -DPANGO_DISABLE_DEPRECATED
endif

# these should go in GTK_EXTRA_CPPFLAGS at some point, but not yet because it breaks libnotify's headers
# -DG_DISABLE_SINGLE_INCLUDES -DGTK_DISABLE_SINGLE_INCLUDES

AM_CPPFLAGS = \
    -I$(top_srcdir) \
    -DTRANSMISSIONLOCALEDIR=\""$(transmissionlocaledir)"\" \
    $(GTK_EXTRA_CPPFLAGS)

AM_CFLAGS = \
    @LIBAPPINDICATOR_CFLAGS@ \
    @LIBEVENT_CFLAGS@ \
    @GTK_CFLAGS@ \
    @LIBCURL_CFLAGS@ \
    @OPENSSL_CFLAGS@ \
    @ZLIB_CFLAGS@ \
    @PTHREAD_CFLAGS@

AM_LDFLAGS = \
    @ZLIB_LDFLAGS@

noinst_HEADERS = \
    actions.h \
    conf.h \
    details.h \
    dialogs.h \
    favicon.h \
    file-list.h \
    filter.h \
    hig.h \
    icons.h \
    icon-lock.h \
    icon-logo-24.h \
    icon-logo-48.h \
    icon-ratio.h \
    icon-turtle.h \
    icon-utilities.h \
    makemeta-ui.h \
    msgwin.h \
    notify.h \
    open-dialog.h \
    relocate.h \
    stats.h \
    torrent-cell-renderer.h \
    tr-core.h \
    tr-icon.h \
    tr-prefs.h \
    tr-window.h \
    ui.h \
    util.h

bin_PROGRAMS = transmission-gtk

transmission_gtk_SOURCES = \
    actions.c \
    conf.c \
    details.c \
    dialogs.c \
    favicon.c \
    file-list.c \
    filter.c \
    hig.c \
    icons.c \
    main.c \
    makemeta-ui.c \
    msgwin.c \
    notify.c \
    open-dialog.c \
    relocate.c \
    stats.c \
    torrent-cell-renderer.c \
    tr-core.c \
    tr-icon.c \
    tr-prefs.c \
    tr-window.c \
    util.c

dist_man_MANS = transmission-gtk.1

transmission_gtk_LDADD = \
    $(top_builddir)/libtransmission/libtransmission.a \
    @LIBUPNP_LIBS@ \
    @LIBNATPMP_LIBS@ \
    @DHT_LIBS@ \
    @LIBUTP_LIBS@ \
    @GTK_LIBS@ \
    @LIBAPPINDICATOR_LIBS@ \
    @LIBEVENT_LIBS@ \
    @LIBCURL_LIBS@ \
    @OPENSSL_LIBS@ \
    @ZLIB_LIBS@ \
    @PTHREAD_LIBS@

DESKTOP_IN_FILES=transmission-gtk.desktop.in
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
    transmission-gtk.desktop

if WIN32

transmission.res: transmission.rc
	$(WINDRES) -J rc -i transmission.rc -O coff -o transmission.res

BUILT_SOURCES = \
	setransmission.res

CLEANFILES = \
	transmission.res

transmission_gtk_LDADD += \
	transmission.res

transmission_gtk_LDFLAGS = \
	-mwindows

endif
