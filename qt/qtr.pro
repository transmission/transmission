TARGET = transmission-qt
NAME = "Transmission"
DESCRIPTION = "Transmission: a fast, easy, and free BitTorrent client"
VERSION = 2.81
LICENSE = "GPL"

target.path = /bin
INSTALLS += target

unix: INSTALLS += man
man.path = /share/man/man1/
man.files = transmission-qt.1

CONFIG += qt thread link_pkgconfig c++1z warn_on
QT += network dbus
win32:QT += winextras
PKGCONFIG = fontconfig libcurl openssl libevent

greaterThan(QT_MAJOR_VERSION, 4) {
    QT += widgets
}

DEFINES += QT_NO_CAST_FROM_ASCII ENABLE_DBUS_INTEROP
win32:DEFINES += QT_DBUS

TRANSMISSION_TOP = ..

include(config.pri)

INCLUDEPATH = $${EVENT_TOP}/include $${INCLUDEPATH}
INCLUDEPATH += $${TRANSMISSION_TOP}
LIBS += $${TRANSMISSION_TOP}/libtransmission/libtransmission.a
LIBS += $${LIBUTP_LIBS}
LIBS += $${DHT_LIBS}
LIBS += $${LIBB64_LIBS}
LIBS += $${LIBUPNP_LIBS}
LIBS += $${LIBNATPMP_LIBS}
unix: LIBS += -L$${EVENT_TOP}/lib -lz -lrt
win32:LIBS += -levent-2.0 -lws2_32 -lintl
win32:LIBS += -lidn -liconv -lwldap32 -liphlpapi

TRANSLATIONS += translations/transmission_de.ts \
                translations/transmission_en.ts \
                translations/transmission_es.ts \
                translations/transmission_eu.ts \
                translations/transmission_fr.ts \
                translations/transmission_hu.ts \
                translations/transmission_id.ts \
                translations/transmission_it_IT.ts \
                translations/transmission_ka.ts \
                translations/transmission_kk.ts \
                translations/transmission_ko.ts \
                translations/transmission_lt.ts \
                translations/transmission_nl.ts \
                translations/transmission_pl.ts \
                translations/transmission_pt_BR.ts \
                translations/transmission_pt_PT.ts \
                translations/transmission_ru.ts \
                translations/transmission_sv.ts \
                translations/transmission_tr.ts \
                translations/transmission_uk.ts \
                translations/transmission_zh_CN.ts

FORMS += AboutDialog.ui \
         DetailsDialog.ui \
         LicenseDialog.ui \
         MainWindow.ui \
         MakeDialog.ui \
         MakeProgressDialog.ui \
         OptionsDialog.ui \
         PrefsDialog.ui \
         RelocateDialog.ui \
         SessionDialog.ui \
         StatsDialog.ui
RESOURCES += application.qrc
win32|macx:RESOURCES += icons/Faenza/Faenza.qrc
SOURCES += AboutDialog.cc \
           AddData.cc \
           Application.cc \
           ColumnResizer.cc \
           DBusInteropHelper.cc \
           DetailsDialog.cc \
           FaviconCache.cc \
           FileTreeDelegate.cc \
           FileTreeItem.cc \
           FileTreeModel.cc \
           FileTreeView.cc \
           FilterBar.cc \
           FilterBarComboBox.cc \
           FilterBarComboBoxDelegate.cc \
           Filters.cc \
           Formatter.cc \
           FreeSpaceLabel.cc \
           IconToolButton.cc \
           InteropHelper.cc \
           InteropObject.cc \
           LicenseDialog.cc \
           MainWindow.cc \
           MakeDialog.cc \
           OptionsDialog.cc \
           PathButton.cc \
           Prefs.cc \
           PrefsDialog.cc \
           RelocateDialog.cc \
           RpcClient.cc \
           RpcQueue.cc \
           Session.cc \
           SessionDialog.cc \
           SqueezeLabel.cc \
           StatsDialog.cc \
           StyleHelper.cc \
           Torrent.cc \
           TorrentDelegate.cc \
           TorrentDelegateMin.cc \
           TorrentFilter.cc \
           TorrentModel.cc \
           TorrentView.cc \
           TrackerDelegate.cc \
           TrackerModel.cc \
           TrackerModelFilter.cc \
           Utils.cc \
           WatchDir.cc
HEADERS += $$replace(SOURCES, .cc, .h)
HEADERS += BaseDialog.h CustomVariantType.h Speed.h Typedefs.h

win32:RC_FILE = qtr.rc
