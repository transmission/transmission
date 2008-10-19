ACLOCAL_AMFLAGS = -I m4

if BUILD_BEOS
  BEOS_DIR = beos
endif
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
if BUILD_WX
  WX_DIR = wx
endif

SUBDIRS = \
  doc \
  third-party \
  libtransmission \
  $(DAEMON_DIR) \
  $(CLI_DIR) \
  $(BEOS_DIR) \
  $(GTK_DIR) \
  $(MAC_DIR) \
  $(WX_DIR)

EXTRA_DIST = \
  web \
  NEWS \
  AUTHORS \
  COPYING \
  README \
  autogen.sh \
  Transmission.xcodeproj/project.pbxproj \
  intltool-extract.in \
  intltool-merge.in \
  intltool-update.in

clutchdir = $(datadir)/transmission/web
clutch_DATA = \
  web/index.html \
  web/LICENSE

clutch_cssdir = $(clutchdir)/stylesheets
clutch_css_DATA = \
  web/stylesheets/iphone.css \
  web/stylesheets/common.css

clutch_jsdir = $(clutchdir)/javascript
clutch_js_DATA = \
  web/javascript/menu.js \
  web/javascript/dialog.js \
  web/javascript/transmission.js \
  web/javascript/transmission.remote.js \
  web/javascript/common.js \
  web/javascript/torrent.js

clutch_jquerydir = $(clutch_jsdir)/jquery
clutch_jquery_DATA = \
  web/javascript/jquery/json.min.js \
  web/javascript/jquery/jquery.contextmenu.min.js \
  web/javascript/jquery/jquery.min.js \
  web/javascript/jquery/jquery.form.min.js \
  web/javascript/jquery/jquery.transmenu.min.js \
  web/javascript/jquery/jquery.dimensions.min.js

clutch_imagesdir = $(clutchdir)/images
clutch_images_DATA = \
  web/images/favicon.ico \
  web/images/favicon.png \
  web/images/webclip-icon.png

clutch_graphicsdir = $(clutch_imagesdir)/graphics
clutch_graphics_DATA = \
  web/images/graphics/browser_firefox.gif \
  web/images/graphics/logo.png \
  web/images/graphics/chrome.png \
  web/images/graphics/iphone_chrome.png \
  web/images/graphics/browser_opera.gif \
  web/images/graphics/filter_bar.png \
  web/images/graphics/filter_icon.png \
  web/images/graphics/transfer_arrows.png \
  web/images/graphics/browser_safari.gif

clutch_progressdir = $(clutch_imagesdir)/progress
clutch_progress_DATA = \
  web/images/progress/progress.png

clutch_buttonsdir = $(clutch_imagesdir)/buttons
clutch_buttons_DATA = \
  web/images/buttons/tab_backgrounds.png \
  web/images/buttons/toolbar_buttons.png \
  web/images/buttons/info_general.png \
  web/images/buttons/torrent_buttons.png \
  web/images/buttons/info_activity.png

DISTCLEANFILES = \
  intltool-extract \
  intltool-merge \
  intltool-update 
