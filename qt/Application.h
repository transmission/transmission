/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <QApplication>
#include <QTimer>
#include <QTranslator>

#include "FaviconCache.h"
#include "Typedefs.h"

class AddData;
class Prefs;
class Session;
class Torrent;
class TorrentModel;
class MainWindow;
class WatchDir;

class Application : public QApplication
{
    Q_OBJECT

public:
    Application(int& argc, char** argv);
    virtual ~Application();

    void raise();
    bool notifyApp(QString const& title, QString const& body) const;

    FaviconCache& faviconCache();

public slots:
    void addTorrent(AddData const&);

private slots:
    void consentGiven(int result);
    void onSessionSourceChanged();
    void onTorrentsAdded(torrent_ids_t const& torrents);
    void onTorrentsCompleted(torrent_ids_t const& torrents);
    void onTorrentsEdited(torrent_ids_t const& torrents);
    void onTorrentsNeedInfo(torrent_ids_t const& torrents);
    void refreshPref(int key);
    void refreshTorrents();

private:
    void maybeUpdateBlocklist();
    void loadTranslations();
    QStringList getNames(torrent_ids_t const& ids) const;
    void quitLater();

private:
    Prefs* myPrefs;
    Session* mySession;
    TorrentModel* myModel;
    MainWindow* myWindow;
    WatchDir* myWatchDir;
    QTimer myModelTimer;
    QTimer myStatsTimer;
    QTimer mySessionTimer;
    time_t myLastFullUpdateTime;
    QTranslator myQtTranslator;
    QTranslator myAppTranslator;
    FaviconCache myFavicons;
};

#undef qApp
#define qApp static_cast<Application*>(Application::instance())
