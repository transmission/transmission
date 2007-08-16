AM_CPPFLAGS = \
    -I$(top_srcdir) \
    $(LIBEVENT_CPPFLAGS) \
    $(GTK_CFLAGS) \
    -DLOCALEDIR=\""$(transmissionlocaledir)"\"

AM_CFLAGS = $(PTHREAD_CFLAGS)

noinst_HEADERS = \
    actions.h \
    conf.h \
    dialogs.h \
    dot-icons.h \
    hig.h \
    img_icon_full.h \
    io.h \
    ipc.h \
    makemeta-ui.h \
    msgwin.h \
    torrent-inspector.h \
    tr_cell_renderer_progress.h \
    tr_core.h \
    tr_icon.h \
    tr_prefs.h \
    tr_torrent.h \
    tr_window.h \
    ui.h \
    util.h

bin_PROGRAMS = transmission-gtk

transmission_gtk_SOURCES = \
    actions.c \
    conf.c \
    dialogs.c \
    hig.c \
    io.c \
    ipc.c \
    main.c \
    makemeta-ui.c \
    msgwin.c \
    torrent-inspector.c \
    tr_cell_renderer_progress.c \
    tr_core.c \
    tr_icon.c \
    tr_prefs.c \
    tr_torrent.c \
    tr_window.c \
    util.c

transmission_gtk_LDADD = \
    $(top_builddir)/libtransmission/libtransmission.a \
    $(top_builddir)/third-party/libevent/libevent.la \
    $(GTK_LIBS) \
    $(PTHREAD_LIBS) -lm
