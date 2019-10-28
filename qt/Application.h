/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <QApplication>
#include <QSet>
#include <QTimer>
#include <QTranslator>

#include "FaviconCache.h"

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
    void addTorrent(QString const&);
    void addTorrent(AddData const&);

private:
    void maybeUpdateBlocklist();
    void loadTranslations();
    void quitLater();

    void popupsInit(Torrent&);
    void popupsShowTorrentAdded(Torrent const&) const;
    void popupsShowTorrentComplete(Torrent const&) const;

    void torStateInit();
    QTimer myTorStateTimer;
    time_t myTorStateLastFullUpdate = 0;

private slots:
    void consentGiven(int result);
    void onSessionSourceChanged();
    void refreshPref(int key);
    void onTorrentsAdded(QSet<int> const& torrents);

    void popupsOnTorrentChanged(Torrent&);
    void popupsOnTorrentCompleted(Torrent&);

    void torStateOnSessionChanged();
    void torStateOnBootstrapped();
    void torStateOnTimer();

private:
    Prefs* myPrefs = nullptr;
    Session* mySession = nullptr;
    TorrentModel* myModel = nullptr;
    MainWindow* myWindow = nullptr;
    WatchDir* myWatchDir = nullptr;
    QTimer myStatsTimer;
    QTimer mySessionTimer;
    QTranslator myQtTranslator;
    QTranslator myAppTranslator;
    FaviconCache myFavicons;
};

#undef qApp
#define qApp static_cast<Application*>(Application::instance())
