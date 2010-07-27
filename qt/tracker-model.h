/*
 * This file Copyright (C) 2010 Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
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
