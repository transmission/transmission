// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <memory>

#include <QDir>
#include <QFileSystemWatcher>
#include <QTimer>

#include <libtransmission/transmission.h>

#include <libtransmission/torrent-metainfo.h>

#include "TorrentModel.h"
#include "WatchDir.h"

// ---

WatchDir::WatchDir(TorrentModel const& model)
    : model_{ model }
{
}

// ---

WatchDir::AddResult WatchDir::metainfoTest(QString const& filename) const
{
    auto metainfo = tr_torrent_metainfo();
    if (!metainfo.parse_torrent_file(filename.toUtf8().constData()))
    {
        return AddResult::Error;
    }

    if (model_.hasTorrent(TorrentHash{ metainfo.info_hash() }))
    {
        return AddResult::Duplicate;
    }

    return AddResult::Success;
}

void WatchDir::onTimeout()
{
    auto* t = qobject_cast<QTimer*>(sender());

    if (auto const filename = t->objectName(); metainfoTest(filename) == AddResult::Success)
    {
        emit torrentFileAdded(filename);
    }

    t->deleteLater();
}

void WatchDir::setPath(QString const& path, bool is_enabled)
{
    // clear out any remnants of the previous watcher, if any
    watch_dir_files_.clear();
    watcher_.reset();

    // maybe create a new watcher
    if (is_enabled)
    {
        watcher_ = std::make_unique<QFileSystemWatcher>(QStringList{ path });
        connect(watcher_.get(), &QFileSystemWatcher::directoryChanged, this, &WatchDir::watcherActivated);
        // trigger the watchdir for torrent files in there already
        QTimer::singleShot(0, this, SLOT(rescanAllWatchedDirectories()));
    }
}

void WatchDir::watcherActivated(QString const& path)
{
    // get the list of files currently in the watch directory
    auto const dir = QDir{ path };
    auto const files = dir.entryList(QDir::Readable | QDir::Files);

    // try to add any new files which end in torrent
    auto const torrent_suffix = QStringLiteral(".torrent");
    for (auto const& name : files)
    {
        if (!name.endsWith(torrent_suffix, Qt::CaseInsensitive) || (watch_dir_files_.count(name) != 0U))
        {
            continue;
        }

        auto const filename = dir.absoluteFilePath(name);
        switch (metainfoTest(filename))
        {
        case AddResult::Success:
            emit torrentFileAdded(filename);
            break;

        case AddResult::Duplicate:
            break;

        case AddResult::Error:
            {
                // give the torrent a few seconds to finish downloading
                auto* t = new QTimer{ this };
                t->setObjectName(dir.absoluteFilePath(name));
                t->setSingleShot(true);
                connect(t, &QTimer::timeout, this, &WatchDir::onTimeout);
                t->start(5000);
            }
        }
    }

    // update our file list so that we can use it
    // for comparison the next time around
    watch_dir_files_ = { std::begin(files), std::end(files) };
}

void WatchDir::rescanAllWatchedDirectories()
{
    if (!watcher_)
    {
        return;
    }

    for (auto const& path : watcher_->directories())
    {
        watcherActivated(path);
    }
}
