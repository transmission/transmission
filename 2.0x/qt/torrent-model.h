/*
 * This file Copyright (C) 2009-2010 Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#ifndef QTR_TORRENT_MODEL_H
#define QTR_TORRENT_MODEL_H

#include <QAbstractListModel>
#include <QMap>
#include <QSet>
#include <QVector>

#include "speed.h"
#include "torrent.h"

class Prefs;

extern "C"
{
    struct tr_benc;
};

class TorrentModel: public QAbstractListModel
{
        Q_OBJECT

    private:
        typedef QMap<int,int> id_to_row_t;
        typedef QMap<int,Torrent*> id_to_torrent_t;
        typedef QVector<Torrent*> torrents_t;
        id_to_row_t myIdToRow;
        id_to_torrent_t myIdToTorrent;
        torrents_t myTorrents;
        Prefs& myPrefs;

    public:
        void clear( );
        bool hasTorrent( const QString& hashString ) const;
        virtual int rowCount( const QModelIndex& parent = QModelIndex() ) const;
        virtual QVariant data( const QModelIndex& index, int role = Qt::DisplayRole ) const;
        enum Role { TorrentRole = Qt::UserRole };

    public:
        Torrent* getTorrentFromId( int id );
        const Torrent* getTorrentFromId( int id ) const;

    private:
        QSet<int> getIds( ) const;
        void addTorrent( Torrent * );

    public:
        Speed getUploadSpeed( ) const;
        Speed getDownloadSpeed( ) const;

    signals:
        void torrentsAdded( QSet<int> );

    public slots:
        void updateTorrents( tr_benc * torrentList, bool isCompleteList );
        void removeTorrents( tr_benc * torrentList );
        void removeTorrent( int id );

    private slots:
        void onTorrentChanged( int propertyId );

    public:
        TorrentModel( Prefs& prefs );
        virtual ~TorrentModel( );
};

#endif
