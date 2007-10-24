ACLOCAL_AMFLAGS = -I m4

if HAVE_BEOS
  BEOS_DIR = beos
endif
if HAVE_DARWIN
  MAC_DIR = macosx
endif
if WITH_GTK
  GTK_DIR = gtk po
endif
if HAVE_WX
  WX_DIR = wx
endif

SUBDIRS = \
  third-party \
  libtransmission \
  daemon cli \
  $(BEOS_DIR) \
  $(GTK_DIR) \
  $(MAC_DIR) \
  $(WX_DIR)

EXTRA_DIST = \
  NEWS \
  AUTHORS \
  LICENSE \
  README \
  mk \
  intltool-extract.in \
  intltool-merge.in \
  intltool-update.in 

DISTCLEANFILES = \
  intltool-extract \
  intltool-merge \
  intltool-update 

