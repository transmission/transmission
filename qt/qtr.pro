TARGET = qtr
NAME = "Transmission"
DESCRIPTION = "Transmission: a fast, easy, and free BitTorrent client"
VERSION = 1.60
LICENSE = "GPL"

target.path = /bin
INSTALLS += target

CONFIG += qt thread debug link_pkgconfig
QT += network
PKGCONFIG = fontconfig libcurl openssl

TRANSMISSION_TOP = ..
INCLUDEPATH += $${TRANSMISSION_TOP}
LIBS += $${TRANSMISSION_TOP}/libtransmission/libtransmission.a
LIBS += $${TRANSMISSION_TOP}/third-party/dht/libdht.a
LIBS += $${TRANSMISSION_TOP}/third-party/miniupnp/libminiupnp.a
LIBS += $${TRANSMISSION_TOP}/third-party/libnatpmp/libnatpmp.a
LIBS += $${TRANSMISSION_TOP}/third-party/libevent/.libs/libevent.a

TRANSLATIONS += transmission_en.ts transmission_ru.ts

FORMS += mainwin.ui
RESOURCES += application.qrc
SOURCES += about.cc app.cc details.cc file-tree.cc filters.cc hig.cc \
           license.cc mainwin.cc make-dialog.cc options.cc prefs.cc \
           prefs-dialog.cc qticonloader.cc relocate.cc session.cc \
           session-dialog.cc squeezelabel.cc stats-dialog.cc torrent.cc \
           torrent-delegate.cc torrent-delegate-min.cc torrent-filter.cc \
           torrent-model.cc triconpushbutton.cc utils.cc watchdir.cc
HEADERS += $$replace(SOURCES, .cc, .h)
HEADERS += speed.h types.h

