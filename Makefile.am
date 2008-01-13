ACLOCAL_AMFLAGS = -I m4

if BUILD_BEOS
  BEOS_DIR = beos
endif
if BUILD_CLI
  CLI_DIR = cli
endif
if BUILD_DAEMON
  DAEMON_DIR = daemon
endif
if BUILD_DARWIN
  MAC_DIR = macosx
endif
if BUILD_GTK
  GTK_DIR = gtk po
endif
if BUILD_WX
  WX_DIR = wx
endif

SUBDIRS = \
  third-party \
  libtransmission \
  $(DAEMON_DIR) \
  $(CLI_DIR) \
  $(BEOS_DIR) \
  $(GTK_DIR) \
  $(MAC_DIR) \
  $(WX_DIR)

EXTRA_DIST = \
  NEWS \
  AUTHORS \
  LICENSE \
  README \
  Transmission.xcodeproj/project.pbxproj \
  intltool-extract.in \
  intltool-merge.in \
  intltool-update.in 

DISTCLEANFILES = \
  intltool-extract \
  intltool-merge \
  intltool-update 

