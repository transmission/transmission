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
  NEWS \
  AUTHORS \
  COPYING \
  README \
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
  third-party/b64-01-newline.patch \
  third-party/b64.cmake \
  third-party/dht.cmake \
  third-party/event2.cmake \
  third-party/natpmp.cmake \
  third-party/utp.cmake \
  third-party/utp_config.h \
  utils/CMakeLists.txt

dist-hook:
	rm -rf `find $(distdir)/qt -name .svn`


DISTCLEANFILES = \
  intltool-extract \
  intltool-merge \
  intltool-update 
