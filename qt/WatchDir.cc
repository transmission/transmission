/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <iostream>

#include <QDir>
#include <QFileSystemWatcher>
#include <QTimer>

#include <libtransmission/transmission.h>

#include "Prefs.h"
#include "TorrentModel.h"
#include "WatchDir.h"

/***
****
***/

WatchDir::WatchDir(TorrentModel const& model) :
    model_(model)
{
}

/***
****
***/

int WatchDir::metainfoTest(QString const& filename) const
{
    int ret;
    tr_info inf;
    tr_ctor* ctor = tr_ctorNew(nullptr);

    // parse
    tr_ctorSetMetainfoFromFile(ctor, filename.toUtf8().constData());
    int const err = tr_torrentParse(ctor, &inf);

    if (err != 0)
    {
        ret = ERROR;
    }
    else if (model_.hasTorrent(QString::fromUtf8(inf.hashString)))
    {
        ret = DUPLICATE;
    }
    else
    {
        ret = OK;
    }

    // cleanup
    if (err == 0)
    {
        tr_metainfoFree(&inf);
    }

    tr_ctorFree(ctor);
    return ret;
}

void WatchDir::onTimeout()
{
    auto* t = qobject_cast<QTimer*>(sender());
    QString const filename = t->objectName();

    if (metainfoTest(filename) == OK)
    {
        emit torrentFileAdded(filename);
    }

    t->deleteLater();
}

void WatchDir::setPath(QString const& path, bool is_enabled)
{
    // clear out any remnants of the previous watcher, if any
    watch_dir_files_.clear();

    if (watcher_ != nullptr)
    {
        delete watcher_;
        watcher_ = nullptr;
    }

    // maybe create a new watcher
    if (is_enabled)
    {
        watcher_ = new QFileSystemWatcher();
        watcher_->addPath(path);
        connect(watcher_, SIGNAL(directoryChanged(QString)), this, SLOT(watcherActivated(QString)));
        // std::cerr << "watching " << qPrintable(path) << " for new .torrent files" << std::endl;
        QTimer::singleShot(0, this, SLOT(rescanAllWatchedDirectories())); // trigger the watchdir for .torrent files in there already
    }
}

void WatchDir::watcherActivated(QString const& path)
{
    QDir const dir(path);

    // get the list of files currently in the watch directory
    QSet<QString> files;

    for (QString const& str : dir.entryList(QDir::Readable | QDir::Files))
    {
        files.insert(str);
    }

    // try to add any new files which end in .torrent
    QSet<QString> const new_files(files - watch_dir_files_);
    auto const torrent_suffix = QStringLiteral(".torrent");

    for (QString const& name : new_files)
    {
        if (name.endsWith(torrent_suffix, Qt::CaseInsensitive))
        {
            QString const filename = dir.absoluteFilePath(name);

            switch (metainfoTest(filename))
            {
            case OK:
                emit torrentFileAdded(filename);
                break;

            case DUPLICATE:
                break;

            case ERROR:
                {
                    // give the .torrent a few seconds to finish downloading
                    auto* t = new QTimer(this);
                    t->setObjectName(dir.absoluteFilePath(name));
                    t->setSingleShot(true);
                    connect(t, SIGNAL(timeout()), this, SLOT(onTimeout()));
                    t->start(5000);
                }
            }
        }
    }

    // update our file list so that we can use it
    // for comparison the next time around
    watch_dir_files_ = files;
}

void WatchDir::rescanAllWatchedDirectories()
{
    if (watcher_ == nullptr)
    {
        return;
    }

    for (QString const& path : watcher_->directories())
    {
        watcherActivated(path);
    }
}
