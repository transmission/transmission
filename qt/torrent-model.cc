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

#include <cassert>
#include <iostream>

#include <libtransmission/transmission.h>
#include <libtransmission/variant.h>

#include "torrent-delegate.h"
#include "torrent-model.h"

void
TorrentModel :: clear( )
{
    myIdToRow.clear( );
    myIdToTorrent.clear( );
    foreach( Torrent * tor, myTorrents ) delete tor;
    myTorrents.clear( );
    reset( );
}

int
TorrentModel :: rowCount( const QModelIndex& parent ) const
{
    Q_UNUSED( parent );

    return myTorrents.size( );
}

QVariant
TorrentModel :: data( const QModelIndex& index, int role ) const
{
    QVariant var;
    const int row = index.row( );
    if( row<0 || row>=myTorrents.size() )
        return QVariant( );

    const Torrent* t = myTorrents.at( row );

    switch( role )
    {
        case Qt::DisplayRole:
            var = QString( t->name() );
            break;

        case Qt::DecorationRole:
            var = t->getMimeTypeIcon( );
            break;

        case TorrentRole:
            var = qVariantFromValue( t );
            break;

        default:
            //std::cerr << "Unhandled role: " << role << std::endl;
            break;
    }

    return var;
}

/***
****
***/

void
TorrentModel :: addTorrent( Torrent * t )
{
    myIdToTorrent.insert( t->id( ), t );
    myIdToRow.insert( t->id( ), myTorrents.size( ) );
    myTorrents.append( t );
}

TorrentModel :: TorrentModel( Prefs& prefs ):
    myPrefs( prefs )
{
}

TorrentModel :: ~TorrentModel( )
{
    clear( );
}

/***
****
***/

Torrent*
TorrentModel :: getTorrentFromId( int id )
{
    id_to_torrent_t::iterator it( myIdToTorrent.find( id ) );
    return it == myIdToTorrent.end() ? 0 : it.value( );
}

const Torrent*
TorrentModel :: getTorrentFromId( int id ) const
{
    id_to_torrent_t::const_iterator it( myIdToTorrent.find( id ) );
    return it == myIdToTorrent.end() ? 0 : it.value( );
}

/***
****
***/

void
TorrentModel :: onTorrentChanged( int torrentId )
{
    const int row( myIdToRow.value( torrentId, -1 ) );
    if( row >= 0 ) {
        QModelIndex qmi( index( row, 0 ) );
        emit dataChanged( qmi, qmi );
    }
}

void
TorrentModel :: removeTorrents( tr_variant * torrents )
{
    int i = 0;
    tr_variant * child;
    while(( child = tr_variantListChild( torrents, i++ ))) {
        int64_t intVal;
        if( tr_variantGetInt( child, &intVal ) )
            removeTorrent( intVal );
    }
}

void
TorrentModel :: updateTorrents( tr_variant * torrents, bool isCompleteList )
{
    QList<Torrent*> newTorrents;
    QSet<int> oldIds( getIds( ) );
    QSet<int> addIds;
    QSet<int> newIds;
    int updatedCount = 0;

    if( tr_variantIsList( torrents ) )
    {
        size_t i( 0 );
        tr_variant * child;
        while(( child = tr_variantListChild( torrents, i++ )))
        {
            int64_t id;
            if( tr_variantDictFindInt( child, TR_KEY_id, &id ) )
            {
                newIds.insert( id );

                Torrent * tor = getTorrentFromId( id );
                if( tor == 0 )
                {
                    tor = new Torrent( myPrefs, id );
                    tor->update( child );
                    if( !tor->hasMetadata() )
                        tor->setMagnet( true );
                    newTorrents.append( tor );
                    connect( tor, SIGNAL(torrentChanged(int)), this, SLOT(onTorrentChanged(int)));
                }
                else
                {
                    tor->update( child );
                    ++updatedCount;
                    if( tor->isMagnet() && tor->hasMetadata() )
                    {
                        addIds.insert( tor->id() );
                        tor->setMagnet( false );
                    }
                }
            }
        }
    }

    if( !newTorrents.isEmpty( ) )
    {
        const int oldCount( rowCount( ) );
        const int newCount( oldCount + newTorrents.size( ) );
        QSet<int> ids;

        beginInsertRows( QModelIndex(), oldCount, newCount - 1 );

        foreach( Torrent * tor, newTorrents ) {
            addTorrent( tor );
            addIds.insert( tor->id( ) );
        }
        endInsertRows( );
    }

    if( !addIds.isEmpty() )
        emit torrentsAdded( addIds );

    if( isCompleteList )
    {
        QSet<int> removedIds( oldIds );
        removedIds -= newIds;
        foreach( int id, removedIds )
            removeTorrent( id );
    }
}

void
TorrentModel :: removeTorrent( int id )
{
    const int row = myIdToRow.value( id, -1 );
    if( row >= 0 )
    {
        Torrent * tor = myIdToTorrent.value( id, 0 );

        beginRemoveRows( QModelIndex(), row, row );
        // make the myIdToRow map consistent with list view/model
        for( QMap<int,int>::iterator i = myIdToRow.begin(); i != myIdToRow.end(); ++i )
            if( i.value() > row )
                --i.value();
        myIdToRow.remove( id );
        myIdToTorrent.remove( id );
        myTorrents.remove( myTorrents.indexOf( tor ) );
        endRemoveRows( );

        delete tor;
    }
}

Speed
TorrentModel :: getUploadSpeed( ) const
{
    Speed up;
    foreach( const Torrent * tor, myTorrents )
        up += tor->uploadSpeed( );
    return up;
}

Speed
TorrentModel :: getDownloadSpeed( ) const
{
    Speed down;
    foreach( const Torrent * tor, myTorrents )
        down += tor->downloadSpeed( );
    return down;
}

QSet<int>
TorrentModel :: getIds( ) const
{
    QSet<int> ids;
    foreach( const Torrent * tor, myTorrents )
        ids.insert( tor->id( ) );
    return ids;
}

bool
TorrentModel :: hasTorrent( const QString& hashString ) const
{
    foreach( const Torrent * tor, myTorrents )
        if( tor->hashString( ) == hashString )
            return true;
    return false;
}
