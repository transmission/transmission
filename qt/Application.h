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
#include "Macros.h"
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
    TR_DISABLE_COPY_MOVE(Application)

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

    Prefs* prefs_ = {};
    Session* session_ = {};
    TorrentModel* model_ = {};
    MainWindow* window_ = {};
    WatchDir* watch_dir_ = {};
    QTimer model_timer_;
    QTimer stats_timer_;
    QTimer session_timer_;
    time_t last_full_update_time_ = {};
    QTranslator qt_translator_;
    QTranslator app_translator_;
    FaviconCache favicons_;

    QString const config_name_;
    QString const display_name_;
};

#undef qApp
#define qApp static_cast<Application*>(Application::instance())
