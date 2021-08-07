/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <memory>

#include <QObject>
#include <QFileSystemWatcher>
#include <QSet>
#include <QString>

#include "Macros.h"

class TorrentModel;

class WatchDir : public QObject
{
    Q_OBJECT
    TR_DISABLE_COPY_MOVE(WatchDir)

public:
    explicit WatchDir(TorrentModel const&);

    void setPath(QString const& path, bool is_enabled);

signals:
    void torrentFileAdded(QString const& filename);

private slots:
    void watcherActivated(QString const& path);
    void onTimeout();

    void rescanAllWatchedDirectories();

private:
    enum
    {
        OK,
        DUPLICATE,
        ERROR
    };

    int metainfoTest(QString const& filename) const;

    TorrentModel const& model_;

    QSet<QString> watch_dir_files_;
    std::unique_ptr<QFileSystemWatcher> watcher_;
};
