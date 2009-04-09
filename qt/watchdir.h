/*
 * This file Copyright (C) 2009 Charles Kerr <charles@transmissionbt.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id:$
 */

#ifndef QTR_WATCHDIR_H
#define QTR_WATCHDIR_H

#include <QObject>
#include <QSet>
#include <QString>

class TorrentModel;
class QFileSystemWatcher;

class WatchDir: public QObject
{
        Q_OBJECT

    public:
        WatchDir( const TorrentModel& );
        ~WatchDir( );

    public:
        void setPath( const QString& path, bool isEnabled );

    private:
        enum { OK, DUPLICATE, ERROR };
        int metainfoTest( const QString& filename ) const;


    signals:
        void torrentFileAdded( QString filename );

    private slots:
        void watcherActivated( const QString& path );
        void onTimeout( );

    private:
        const TorrentModel& myModel;
        QSet<QString> myWatchDirFiles;
        QFileSystemWatcher * myWatcher;
};

#endif
