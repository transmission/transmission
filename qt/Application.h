/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <memory>
#include <unordered_set>

#include <QApplication>
#include <QTimer>
#include <QTranslator>

#include "FaviconCache.h"
#include "Macros.h"
#include "Typedefs.h"
#include "Utils.h" // std::hash<QString>

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

    void raise() const;
    bool notifyApp(QString const& title, QString const& body) const;

    QString const& intern(QString const& in) { return *interned_strings_.insert(in).first; }
    FaviconCache& faviconCache();

public slots:
    void addTorrent(AddData const&) const;
    void addTorrent(QString const&) const;

private slots:
    void consentGiven(int result) const;
    void onSessionSourceChanged() const;
    void onTorrentsAdded(torrent_ids_t const& torrents) const;
    void onTorrentsCompleted(torrent_ids_t const& torrents) const;
    void onTorrentsEdited(torrent_ids_t const& torrents) const;
    void onTorrentsNeedInfo(torrent_ids_t const& torrents) const;
    void refreshPref(int key) const;
    void refreshTorrents();
    void saveGeometry() const;

private:
    void maybeUpdateBlocklist() const;
    void loadTranslations();
    QStringList getNames(torrent_ids_t const& ids) const;
    void quitLater() const;

    std::unique_ptr<Prefs> prefs_;
    std::unique_ptr<Session> session_;
    std::unique_ptr<TorrentModel> model_;
    std::unique_ptr<MainWindow> window_;
    std::unique_ptr<WatchDir> watch_dir_;
    QTimer model_timer_;
    QTimer stats_timer_;
    QTimer session_timer_;
    time_t last_full_update_time_ = {};
    QTranslator qt_translator_;
    QTranslator app_translator_;
    FaviconCache favicons_;

    QString const config_name_ = QStringLiteral("transmission");
    QString const display_name_ = QStringLiteral("transmission-qt");

    std::unordered_set<QString> interned_strings_;
};

#define trApp static_cast<Application*>(Application::instance())
