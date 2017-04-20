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

private slots:
    void consentGiven(int result);
    void onSessionSourceChanged();
    void refreshPref(int key);
    void refreshTorrents();
    void onTorrentsAdded(QSet<int> const& torrents);
    void onTorrentCompleted(int);
    void onNewTorrentChanged(int);

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
