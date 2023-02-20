// This file Copyright © 2009-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <ctime>
#include <memory>
#include <unordered_set>

#include <QApplication>
#include <QRegularExpression>
#include <QTimer>
#include <QTranslator>

#include <libtransmission/tr-macros.h>

#include "FaviconCache.h"
#include "Typedefs.h"
#include "Utils.h" // std::hash<QString>

class AddData;
class MainWindow;
class Prefs;
class Session;
class Torrent;
class TorrentModel;
class WatchDir;

class Application : public QApplication
{
    Q_OBJECT
    TR_DISABLE_COPY_MOVE(Application)

public:
    Application(int& argc, char** argv);

    void raise() const;
    bool notifyApp(QString const& title, QString const& body, QStringList const& actions = {}) const;

    QString const& intern(QString const& in)
    {
        return *interned_strings_.insert(in).first;
    }

    FaviconCache& faviconCache();

public slots:
    void addTorrent(AddData const&) const;
    void addTorrent(QString const&) const;

private slots:
    void consentGiven(int result) const;
    void onSessionSourceChanged() const;
    void onTorrentsAdded(torrent_ids_t const& torrent_ids) const;
    void onTorrentsCompleted(torrent_ids_t const& torrent_ids) const;
    void onTorrentsEdited(torrent_ids_t const& torrent_ids) const;
    void onTorrentsNeedInfo(torrent_ids_t const& torrent_ids) const;
    void refreshPref(int key) const;
    void refreshTorrents();
    void saveGeometry() const;
#ifdef QT_DBUS_LIB
    void onNotificationActionInvoked(quint32 notification_id, QString action_key);
#endif

private:
    void maybeUpdateBlocklist() const;
    void loadTranslations();
    QStringList getNames(torrent_ids_t const& ids) const;
    void quitLater() const;
    void notifyTorrentAdded(Torrent const*) const;

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

#ifdef QT_DBUS_LIB
    QString const fdo_notifications_service_name_ = QStringLiteral("org.freedesktop.Notifications");
    QString const fdo_notifications_path_ = QStringLiteral("/org/freedesktop/Notifications");
    QString const fdo_notifications_interface_name_ = QStringLiteral("org.freedesktop.Notifications");
    QRegularExpression const start_now_regex_;
#endif
};

#define trApp dynamic_cast<Application*>(Application::instance())
