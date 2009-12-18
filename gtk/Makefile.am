SUBDIRS = icons

if TR_UNSTABLE
GTK_EXTRA_CPPFLAGS=-DG_DISABLE_DEPRECATED -DGDK_PIXBUF_DISABLE_DEPRECATED -DGDK_DISABLE_DEPRECATED -DGTK_DISABLE_DEPRECATED -DPANGO_DISABLE_DEPRECATED
endif

# these should go in GTK_EXTRA_CPPFLAGS at some point, but not yet because it breaks libnotify's headers
# -DG_DISABLE_SINGLE_INCLUDES -DGTK_DISABLE_SINGLE_INCLUDES

sexy-marshal.h:	marshal.list
	glib-genmarshal --prefix=sexy_marshal ./marshal.list --header > sexy-marshal.h

sexy-marshal.c:	marshal.list
	echo '#include "sexy-marshal.h"' > sexy-marshal.c
	glib-genmarshal --prefix=sexy_marshal ./marshal.list --body >> sexy-marshal.c

AM_CPPFLAGS = \
    -I$(top_srcdir) \
    -DTRANSMISSIONLOCALEDIR=\""$(transmissionlocaledir)"\" \
    $(GTK_EXTRA_CPPFLAGS)

AM_CFLAGS = \
    @LIBEVENT_CFLAGS@ \
    @LIBCANBERRA_CFLAGS@ \
    @LIBGCONF_CFLAGS@ \
    @GTK_CFLAGS@ \
    @LIBCURL_CFLAGS@ \
    @GIO_CFLAGS@ \
    @LIBNOTIFY_CFLAGS@ \
    @DBUS_GLIB_CFLAGS@ \
    @OPENSSL_CFLAGS@ \
    @ZLIB_CFLAGS@ \
    @PTHREAD_CFLAGS@

AM_LDFLAGS = \
    @ZLIB_LDFLAGS@

noinst_HEADERS = \
    actions.h \
    add-dialog.h \
    conf.h \
    details.h \
    dialogs.h \
    hig.h \
    icons.h \
    file-list.h \
    lock.h \
    logo.h \
    makemeta-ui.h \
    msgwin.h \
    notify.h \
    options-icon.h \
    ratio-icon.h \
    relocate.h \
    stats.h \
    sexy-icon-entry.h \
    sexy-marshal.h \
    torrent-cell-renderer.h \
    tracker-list.h \
    tr-core.h \
    tr-core-dbus.h \
    tr-icon.h \
    tr-prefs.h \
    tr-torrent.h \
    tr-window.h \
    turtles.h \
    ui.h \
    util.h

bin_PROGRAMS = transmission

dbus_generated_sources = tr-core-dbus.h

transmission_SOURCES = \
    actions.c \
    add-dialog.c \
    conf.c \
    details.c \
    dialogs.c \
    file-list.c \
    hig.c \
    icons.c \
    main.c \
    makemeta-ui.c \
    msgwin.c \
    notify.c \
    relocate.c \
    sexy-icon-entry.c \
    sexy-marshal.c \
    stats.c \
    torrent-cell-renderer.c \
    tracker-list.c \
    tr-core.c \
    tr-icon.c \
    tr-prefs.c \
    tr-torrent.c \
    tr-window.c \
    util.c \
    $(dbus_generated_sources)

dist_man_MANS = transmission.1

transmission_LDADD = \
    $(top_builddir)/libtransmission/libtransmission.a \
    $(top_builddir)/third-party/miniupnp/libminiupnp.a \
    $(top_builddir)/third-party/libnatpmp/libnatpmp.a \
    @LIBCANBERRA_LIBS@ \
    @LIBGCONF_LIBS@ \
    @DHT_LIBS@ \
    @GTK_LIBS@ \
    @GIO_LIBS@ \
    @LIBNOTIFY_LIBS@ \
    @LIBEVENT_LIBS@ \
    @DBUS_GLIB_LIBS@ \
    @LIBCURL_LIBS@ \
    @OPENSSL_LIBS@ \
    @ZLIB_LIBS@ \
    @PTHREAD_LIBS@

DESKTOP_IN_FILES=transmission.desktop.in
DESKTOP_FILES=$(DESKTOP_IN_FILES:.desktop.in=.desktop)
@INTLTOOL_DESKTOP_RULE@

Productivitydir = $(datadir)/applications
Productivity_DATA = $(DESKTOP_FILES)

icon_DATA = transmission.png
icondir = $(datadir)/pixmaps

EXTRA_DIST = \
    $(DESKTOP_IN_FILES) \
    marshal.list \
    tr-core-dbus.xml \
    transmission.png

DISTCLEANFILES = \
    transmission.desktop

CLEANFILES = $(dbus_generated_sources)

$(srcdir)/tr-core.c: tr-core-dbus.h

tr-core-dbus.h: $(srcdir)/tr-core-dbus.xml
	$(DBUS_BINDING_TOOL) --mode=glib-server --prefix=tr_core $< > $(@F)

if WIN32

transmission.res: transmission.rc
	$(WINDRES) -J rc -i transmission.rc -O coff -o transmission.res

BUILT_SOURCES = \
	sexy-marshal.h \
	sexy-marshal.c \
	setransmission.res

CLEANFILES += \
	transmission.res

transmission_LDADD += \
	transmission.res

transmission_LDFLAGS = \
	-mwindows

else

BUILT_SOURCES = \
	sexy-marshal.h \
	sexy-marshal.c

endif
