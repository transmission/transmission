// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <memory>
#include <set>

#include <QObject>
#include <QFileSystemWatcher>
#include <QString>

class TorrentModel;

class WatchDir : public QObject
{
    Q_OBJECT

public:
    explicit WatchDir(TorrentModel const& model);
    ~WatchDir() override = default;
    WatchDir(WatchDir&&) = delete;
    WatchDir(WatchDir const&) = delete;
    WatchDir& operator=(WatchDir&&) = delete;
    WatchDir& operator=(WatchDir const&) = delete;

    void set_path(QString const& path, bool is_enabled);

signals:
    void torrent_file_added(QString const& filename);

private slots:
    void watcher_activated(QString const& path);
    void on_timeout();

    void rescan_all_watched_directories();

private:
    enum class AddResult : uint8_t
    {
        Success,
        Duplicate,
        Error
    };

    [[nodiscard]] AddResult metainfo_test(QString const& filename) const;

    TorrentModel const& model_;

    std::set<QString> watch_dir_files_;
    std::unique_ptr<QFileSystemWatcher> watcher_;
};
