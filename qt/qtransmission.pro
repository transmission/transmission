VERSION = 1.6.0

CONFIG += qt thread debug link_pkgconfig
QT += network
PKGCONFIG = fontconfig libcurl openssl
INCLUDEPATH += .

TRANSMISSION_TOP = ..
INCLUDEPATH += $${TRANSMISSION_TOP}
LIBS += $${TRANSMISSION_TOP}/libtransmission/libtransmission.a
LIBS += $${TRANSMISSION_TOP}/third-party/miniupnp/libminiupnp.a
LIBS += $${TRANSMISSION_TOP}/third-party/libnatpmp/libnatpmp.a
LIBS += $${TRANSMISSION_TOP}/third-party/libevent/.libs/libevent.a

FORMS += mainwin.ui about.ui

RESOURCES += application.qrc

HEADERS += about.h \
           app.h \
           details.h \
           file-tree.h \
           filters.h \
           hig.h \
           mainwin.h \
           make-dialog.h \
           options.h \
           prefs-dialog.h \
           prefs.h \
           qticonloader.h \
           session.h \
           speed.h \
           squeezelabel.h \
           stats-dialog.h \
           torrent-delegate.h \
           torrent-delegate-min.h \
           torrent-filter.h \
           torrent.h \
           torrent-model.h \
           types.h \
           utils.h \
           watchdir.h \

SOURCES += about.cc \
           app.cc \
           details.cc \
           file-tree.cc \
           filters.cc \
           hig.cc \
           mainwin.cc \
           make-dialog.cc \
           options.cc \
           prefs.cc \
           prefs-dialog.cc \
           qticonloader.cc \
           session.cc \
           squeezelabel.cc \
           stats-dialog.cc \
           torrent.cc \
           torrent-delegate.cc \
           torrent-delegate-min.cc \
           torrent-filter.cc \
           torrent-model.cc \
           utils.cc \
           watchdir.cc

TRANSLATIONS += transmission_en.ts
