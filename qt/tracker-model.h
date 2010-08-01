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

#ifndef QTR_TRACKER_MODEL_H
#define QTR_TRACKER_MODEL_H

#include <QAbstractListModel>
#include <QSet>
#include <QVector>

#include "torrent.h"
#include "torrent-model.h"

struct TrackerInfo
{
    TrackerStat st;
    int torrentId;
};
Q_DECLARE_METATYPE(TrackerInfo)

class TrackerModel: public QAbstractListModel
{
        Q_OBJECT

        typedef QVector<TrackerInfo> rows_t;
        rows_t myRows;

    public:
        void refresh( const TorrentModel&, const QSet<int>& ids );
        int find( int torrentId, const QString& url ) const;

    public:
        virtual int rowCount( const QModelIndex& parent = QModelIndex() ) const;
        virtual QVariant data( const QModelIndex& index, int role = Qt::DisplayRole ) const;
        enum Role { TrackerRole = Qt::UserRole };

    public:
        TrackerModel( ) { }
        virtual ~TrackerModel( ) { }
};

#endif
