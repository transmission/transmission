if WITH_GTK
  GTK_DIR = gtk
endif
if HAVE_BEOS
  BEOS_DIR = beos
endif
if HAVE_DARWIN
  MAC_DIR = macosx
endif
if HAVE_WX
  WX_DIR = wx
endif

SUBDIRS = third-party libtransmission daemon cli $(GTK_DIR) $(BEOS_DIR) $(MAC_DIR) $(WX_DIR)

DISTCLEANFILES = \
 Transmission.desktop

EXTRA_DIST = \
 NEWS \
 AUTHORS \
 LICENSE \
 README \
 third-party
