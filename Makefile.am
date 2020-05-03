ACLOCAL_AMFLAGS = -I m4

if BUILD_CLI
  CLI_DIR = cli
endif
if BUILD_DAEMON
if !WIN32
  DAEMON_DIR = daemon
endif
endif
if BUILD_GTK
  GTK_DIR = gtk po
endif
if BUILD_MAC
  MAC_DIR = macosx
endif

SUBDIRS = \
  extras \
  third-party \
  libtransmission \
  utils \
  $(DAEMON_DIR) \
  $(CLI_DIR) \
  $(GTK_DIR) \
  $(MAC_DIR) \
  web

EXTRA_DIST = \
  qt \
  NEWS.md \
  AUTHORS \
  COPYING \
  README.md \
  autogen.sh \
  update-version-h.sh \
  Transmission.xcodeproj/project.pbxproj

EXTRA_DIST += \
  CMakeLists.txt \
  cmake \
  cli/CMakeLists.txt \
  daemon/CMakeLists.txt \
  gtk/CMakeLists.txt \
  libtransmission/CMakeLists.txt \
  libtransmission/version.h.in \
  po/CMakeLists.txt \
  qt/CMakeLists.txt \
  utils/CMakeLists.txt

if HAVE_REVISION_FILE
  EXTRA_DIST += REVISION
endif

dist-hook:
	find "$(distdir)/qt" "$(distdir)/cmake" -name .svn -exec rm -rf '{}' '+' -prune

DISTCLEANFILES = \
  intltool-extract \
  intltool-merge \
  intltool-update 
