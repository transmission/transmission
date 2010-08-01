/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 *
 * $Id$
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
