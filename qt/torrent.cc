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

#include <cassert>
#include <iostream>

#include <QApplication>
#include <QStyle>
#include <QSet>
#include <QString>
#include <QFileInfo>
#include <QVariant>

#include <libtransmission/transmission.h>
#include <libtransmission/bencode.h>
#include <libtransmission/utils.h> /* tr_new0, tr_strdup */

#include "app.h"
#include "prefs.h"
#include "qticonloader.h"
#include "torrent.h"
#include "utils.h"


Torrent :: Torrent( Prefs& prefs, int id ):
    myPrefs( prefs )
{
    for( int i=0; i<PROPERTY_COUNT; ++i )
        assert( myProperties[i].id == i );

    setInt( ID, id );
    setIcon( MIME_ICON, QApplication::style()->standardIcon( QStyle::SP_FileIcon ) );
}

Torrent :: ~Torrent( )
{
}

/***
****
***/

Torrent :: Property
Torrent :: myProperties[] =
{
    { ID, "id", QVariant::Int, INFO, },
    { UPLOAD_SPEED, "rateUpload", QVariant::Int, STAT } /* B/s */,
    { DOWNLOAD_SPEED, "rateDownload", QVariant::Int, STAT }, /* B/s */
    { SWARM_SPEED, "swarmSpeed", QVariant::Int, STAT_EXTRA },/* KB/s */
    { DOWNLOAD_DIR, "downloadDir", QVariant::String, STAT },
    { ACTIVITY, "status", QVariant::Int, STAT },
    { NAME, "name", QVariant::String, INFO },
    { ERROR, "errorString", QVariant::String, STAT },
    { SIZE_WHEN_DONE, "sizeWhenDone", QVariant::ULongLong, STAT },
    { LEFT_UNTIL_DONE, "leftUntilDone", QVariant::ULongLong, STAT },
    { HAVE_UNCHECKED, "haveUnchecked", QVariant::ULongLong, STAT },
    { HAVE_VERIFIED, "haveValid", QVariant::ULongLong, STAT },
    { TOTAL_SIZE, "totalSize", QVariant::ULongLong, INFO },
    { PIECE_SIZE, "pieceSize", QVariant::ULongLong, INFO },
    { PIECE_COUNT, "pieceCount", QVariant::Int, INFO },
    { PEERS_GETTING_FROM_US, "peersGettingFromUs", QVariant::Int, STAT },
    { PEERS_SENDING_TO_US, "peersSendingToUs", QVariant::Int, STAT },
    { WEBSEEDS_SENDING_TO_US, "webseedsSendingToUs", QVariant::Int, STAT_EXTRA },
    { PERCENT_DONE, "percentDone", QVariant::Double, STAT },
    { PERCENT_VERIFIED, "recheckProgress", QVariant::Double, STAT },
    { DATE_ACTIVITY, "activityDate", QVariant::DateTime, STAT_EXTRA },
    { DATE_ADDED, "addedDate", QVariant::DateTime, INFO },
    { DATE_STARTED, "startDate", QVariant::DateTime, STAT_EXTRA },
    { DATE_CREATED, "dateCreated", QVariant::DateTime, INFO },
    { PEERS_CONNECTED, "peersConnected", QVariant::Int, STAT },
    { ETA, "eta", QVariant::Int, STAT },
    { RATIO, "uploadRatio", QVariant::Double, STAT },
    { DOWNLOADED_EVER, "downloadedEver", QVariant::ULongLong, STAT },
    { UPLOADED_EVER, "uploadedEver", QVariant::ULongLong, STAT },
    { FAILED_EVER, "corruptEver", QVariant::ULongLong, STAT_EXTRA },
    { TRACKERS, "trackers", QVariant::StringList, INFO },
    { MIME_ICON, "ccc", QVariant::Icon, DERIVED },
    { SEED_RATIO_LIMIT, "seedRatioLimit", QVariant::Double, STAT_EXTRA },
    { SEED_RATIO_MODE, "seedRatioMode", QVariant::Int, STAT_EXTRA },
    { DOWN_LIMIT, "downloadLimit", QVariant::Int, STAT_EXTRA }, /* KB/s */
    { DOWN_LIMITED, "downloadLimited", QVariant::Bool, STAT_EXTRA },
    { UP_LIMIT, "uploadLimit", QVariant::Int, STAT_EXTRA }, /* KB/s */
    { UP_LIMITED, "uploadLimited", QVariant::Bool, STAT_EXTRA },
    { HONORS_SESSION_LIMITS, "honorsSessionLimits", QVariant::Bool, STAT_EXTRA },
    { PEER_LIMIT, "peer-limit", QVariant::Int, STAT_EXTRA },
    { HASH_STRING, "hashString", QVariant::String, INFO },
    { IS_PRIVATE, "isPrivate", QVariant::Bool, INFO },
    { COMMENT, "comment", QVariant::String, INFO },
    { CREATOR, "creator", QVariant::String, INFO },
    { LAST_ANNOUNCE_TIME, "lastAnnounceTime", QVariant::DateTime, STAT_EXTRA },
    { LAST_SCRAPE_TIME, "lastScrapeTime", QVariant::DateTime, STAT_EXTRA },
    { MANUAL_ANNOUNCE_TIME, "manualAnnounceTime", QVariant::DateTime, STAT_EXTRA },
    { NEXT_ANNOUNCE_TIME, "nextAnnounceTime", QVariant::DateTime, STAT_EXTRA },
    { NEXT_SCRAPE_TIME, "nextScrapeTime", QVariant::DateTime, STAT_EXTRA },
    { SCRAPE_RESPONSE, "scrapeResponse", QVariant::String, STAT_EXTRA },
    { ANNOUNCE_RESPONSE, "announceResponse", QVariant::String, STAT_EXTRA },
    { ANNOUNCE_URL, "announceURL", QVariant::String, STAT_EXTRA },
    { SEEDERS, "seeders", QVariant::Int, STAT_EXTRA },
    { LEECHERS, "leechers", QVariant::Int, STAT_EXTRA },
    { TIMES_COMPLETED, "timesCompleted", QVariant::Int, STAT_EXTRA },
    { PEERS, "peers", TrTypes::PeerList, STAT_EXTRA },
    { TORRENT_FILE, "torrentFile", QVariant::String, STAT_EXTRA },
    { BANDWIDTH_PRIORITY, "bandwidthPriority", QVariant::Int, STAT_EXTRA }
};

Torrent :: KeyList
Torrent :: buildKeyList( Group group )
{
    KeyList keys;

    if( keys.empty( ) )
        for( int i=0; i<PROPERTY_COUNT; ++i )
            if( myProperties[i].id==ID || myProperties[i].group==group )
                keys << myProperties[i].key;

    return keys;
}

const Torrent :: KeyList&
Torrent :: getInfoKeys( )
{
    static KeyList keys;
    if( keys.isEmpty( ) )
        keys << buildKeyList( INFO ) << "files";
    return keys;
}

const Torrent :: KeyList&
Torrent :: getStatKeys( )
{
    static KeyList keys( buildKeyList( STAT ) );
    return keys;
}

const Torrent :: KeyList&
Torrent :: getExtraStatKeys( )
{
    static KeyList keys;
    if( keys.isEmpty( ) )
        keys << buildKeyList( STAT_EXTRA ) << "fileStats";
    return keys;
}


bool
Torrent :: setInt( int i, int value )
{
    bool changed = false;

    assert( 0<=i && i<PROPERTY_COUNT );
    assert( myProperties[i].type == QVariant::Int );

    if( myValues[i].isNull() || myValues[i].toInt()!=value )
    {
        myValues[i].setValue( value );
        changed = true;
    }

    return changed;
}

bool
Torrent :: setBool( int i, bool value )
{
    bool changed = false;

    assert( 0<=i && i<PROPERTY_COUNT );
    assert( myProperties[i].type == QVariant::Bool );

    if( myValues[i].isNull() || myValues[i].toBool()!=value )
    {
        myValues[i].setValue( value );
        changed = true;
    }

    return changed;
}

bool
Torrent :: setDouble( int i, double value )
{
    bool changed = false;

    assert( 0<=i && i<PROPERTY_COUNT );
    assert( myProperties[i].type == QVariant::Double );

    if( myValues[i].isNull() || myValues[i].toDouble()!=value )
    {
        myValues[i].setValue( value );
        changed = true;
    }

    return changed;
}

bool
Torrent :: setDateTime( int i, const QDateTime& value )
{
    bool changed = false;

    assert( 0<=i && i<PROPERTY_COUNT );
    assert( myProperties[i].type == QVariant::DateTime );

    if( myValues[i].isNull() || myValues[i].toDateTime()!=value )
    {
        myValues[i].setValue( value );
        changed = true;
    }

    return changed;
}
  
bool
Torrent :: setSize( int i, qulonglong value )
{
    bool changed = false;

    assert( 0<=i && i<PROPERTY_COUNT );
    assert( myProperties[i].type == QVariant::ULongLong );

    if( myValues[i].isNull() || myValues[i].toULongLong()!=value )
    {
        myValues[i].setValue( value );
        changed = true;
    }

    return changed;
}

bool
Torrent :: setString( int i, const char * value )
{
    bool changed = false;

    assert( 0<=i && i<PROPERTY_COUNT );
    assert( myProperties[i].type == QVariant::String );

    if( myValues[i].isNull() || myValues[i].toString()!=value )
    {
        myValues[i].setValue( QString::fromUtf8( value ) );
        changed = true;
    }

    return changed;
}

bool
Torrent :: setIcon( int i, const QIcon& value )
{
    assert( 0<=i && i<PROPERTY_COUNT );
    assert( myProperties[i].type == QVariant::Icon );

    myValues[i].setValue( value );
    return true;
}

int
Torrent :: getInt( int i ) const
{
    assert( 0<=i && i<PROPERTY_COUNT );
    assert( myProperties[i].type == QVariant::Int );

    return myValues[i].toInt( );
}

QDateTime
Torrent :: getDateTime( int i ) const
{
    assert( 0<=i && i<PROPERTY_COUNT );
    assert( myProperties[i].type == QVariant::DateTime );

    return myValues[i].toDateTime( );
}

bool
Torrent :: getBool( int i ) const
{
    assert( 0<=i && i<PROPERTY_COUNT );
    assert( myProperties[i].type == QVariant::Bool );

    return myValues[i].toBool( );
}

qulonglong
Torrent :: getSize( int i ) const
{
    assert( 0<=i && i<PROPERTY_COUNT );
    assert( myProperties[i].type == QVariant::ULongLong );

    return myValues[i].toULongLong( );
}
double
Torrent :: getDouble( int i ) const
{
    assert( 0<=i && i<PROPERTY_COUNT );
    assert( myProperties[i].type == QVariant::Double );

    return myValues[i].toDouble( );
}
QString
Torrent :: getString( int i ) const
{
    assert( 0<=i && i<PROPERTY_COUNT );
    assert( myProperties[i].type == QVariant::String );

    return myValues[i].toString( );
}
QIcon
Torrent :: getIcon( int i ) const
{
    assert( 0<=i && i<PROPERTY_COUNT );
    assert( myProperties[i].type == QVariant::Icon );

    return myValues[i].value<QIcon>();
}

/***
****
***/

bool
Torrent :: getSeedRatio( double& ratio ) const
{
    bool isLimited;

    switch( seedRatioMode( ) )
    {
        case TR_RATIOLIMIT_SINGLE:
            isLimited = true;
            ratio = seedRatioLimit( );
            break;

        case TR_RATIOLIMIT_GLOBAL:
            if(( isLimited = myPrefs.getBool( Prefs :: SEED_RATIO_LIMITED )))
                ratio = myPrefs.getDouble( Prefs :: SEED_RATIO_LIMIT );
            break;

        case TR_RATIOLIMIT_UNLIMITED:
            isLimited = false;
            break;
    }

    return isLimited;
}

bool
Torrent :: hasFileSubstring( const QString& substr ) const
{
    foreach( const TrFile file, myFiles )
        if( file.filename.contains( substr, Qt::CaseInsensitive ) )
            return true;
    return false;
}

bool
Torrent :: hasTrackerSubstring( const QString& substr ) const
{
    foreach( QString s, myValues[TRACKERS].toStringList() )
        if( s.contains( substr, Qt::CaseInsensitive ) )
            return true;
    return false;
}

int
Torrent :: compareRatio( const Torrent& that ) const
{
    const double a = ratio( );
    const double b = that.ratio( );
    if( (int)a == TR_RATIO_INF && (int)b == TR_RATIO_INF ) return 0;
    if( (int)a == TR_RATIO_INF ) return 1;
    if( (int)b == TR_RATIO_INF ) return -1;
    if( a < b ) return -1;
    if( a > b ) return 1;
    return 0;
}

int
Torrent :: compareETA( const Torrent& that ) const
{
    const bool haveA( hasETA( ) );
    const bool haveB( that.hasETA( ) );
    if( haveA && haveB ) return getETA() - that.getETA();
    if( haveA ) return -1;
    if( haveB ) return 1;
    return 0;
}

int
Torrent :: compareTracker( const Torrent& that ) const
{
    Q_UNUSED( that );

    // FIXME
    return 0;
}

/***
****
***/

void
Torrent :: updateMimeIcon( )
{
    const FileList& files( myFiles );

    QIcon icon;

    if( files.size( ) > 1 )
        icon = QtIconLoader :: icon( "folder", QApplication::style()->standardIcon( QStyle::SP_DirIcon ) );
    else
        icon = Utils :: guessMimeIcon( files.at(0).filename );

    setIcon( MIME_ICON, icon );
}

/***
****
***/

void
Torrent :: notifyComplete( ) const
{
    // if someone wants to implement notification, here's the hook.
}

/***
****
***/

void
Torrent :: update( tr_benc * d )
{
    bool changed = false;

    for( int  i=0; i<PROPERTY_COUNT; ++i )
    {
        tr_benc * child = tr_bencDictFind( d, myProperties[i].key );
        if( !child )
            continue;

        switch( myProperties[i].type )
        {
            case QVariant :: Int: {
                int64_t val;
                if( tr_bencGetInt( child, &val ) )
                    changed |= setInt( i, val );
                break;
            }

            case QVariant :: Bool: {
                tr_bool val;
                if( tr_bencGetBool( child, &val ) )
                    changed |= setBool( i, val );
                break;
            }

            case QVariant :: String: {
                const char * val;
                if( tr_bencGetStr( child, &val ) )
                    changed |= setString( i, val );
                break;
            }

            case QVariant :: ULongLong: {
                int64_t val;
                if( tr_bencGetInt( child, &val ) )
                    changed |= setSize( i, val );
                break;
            }

            case QVariant :: Double: {
                double val;
                if( tr_bencGetReal( child, &val ) )
                    changed |= setDouble( i, val );
                break;
            }

            case QVariant :: DateTime: {
                int64_t val;
                if( tr_bencGetInt( child, &val ) && val )
                    changed |= setDateTime( i, QDateTime :: fromTime_t( val ) );
                break;
            }

            case QVariant :: StringList:
            case TrTypes :: PeerList:
                break;

            default:
                assert( 0 && "unhandled type" );
        }
        
    }

    tr_benc * files;

    if( tr_bencDictFindList( d, "files", &files ) ) {
        const char * str;
        int64_t intVal;
        int i = 0;
        myFiles.clear( );
        tr_benc * child;
        while(( child = tr_bencListChild( files, i ))) {
            TrFile file;
            file.index = i++;
            if( tr_bencDictFindStr( child, "name", &str ) )
                file.filename = QString::fromUtf8( str );
            if( tr_bencDictFindInt( child, "length", &intVal ) )
                file.size = intVal;
            myFiles.append( file );
        }
        updateMimeIcon( );
        changed = true;
    }

    if( tr_bencDictFindList( d, "fileStats", &files ) ) {
        const int n = tr_bencListSize( files );
        assert( n == myFiles.size( ) );
        for( int i=0; i<n; ++i ) {
            int64_t intVal;
            tr_bool boolVal;
            tr_benc * child = tr_bencListChild( files, i );
            TrFile& file( myFiles[i] );
            if( tr_bencDictFindInt( child, "bytesCompleted", &intVal ) )
                file.have = intVal;
            if( tr_bencDictFindBool( child, "wanted", &boolVal ) )
                file.wanted = boolVal;
            if( tr_bencDictFindInt( child, "priority", &intVal ) )
                file.priority = intVal;
        }
        changed = true;
    }

    tr_benc * trackers;
    if( tr_bencDictFindList( d, "trackers", &trackers ) ) {
        const char * str;
        int i = 0;
        QStringList list;
        tr_benc * child;
        while(( child = tr_bencListChild( trackers, i++ )))
            if( tr_bencDictFindStr( child, "announce", &str ))
                list.append( QString::fromUtf8( str ) );
        if( myValues[TRACKERS] != list ) {
            myValues[TRACKERS].setValue( list );
            changed = true;
        }
    }

    tr_benc * peers;
    if( tr_bencDictFindList( d, "peers", &peers ) ) {
        tr_benc * child;
        PeerList peerList;
        int childNum = 0;
        while(( child = tr_bencListChild( peers, childNum++ ))) {
            double d;
            tr_bool b;
            int64_t i;
            const char * str;
            Peer peer;
            if( tr_bencDictFindStr( child, "address", &str ) )
                peer.address = QString::fromUtf8( str );
            if( tr_bencDictFindStr( child, "clientName", &str ) )
                peer.clientName = QString::fromUtf8( str );
            if( tr_bencDictFindBool( child, "clientIsChoked", &b ) )
                peer.clientIsChoked = b;
            if( tr_bencDictFindBool( child, "clientIsInterested", &b ) )
                peer.clientIsInterested = b;
            if( tr_bencDictFindBool( child, "isDownloadingFrom", &b ) )
                peer.isDownloadingFrom = b;
            if( tr_bencDictFindBool( child, "isEncrypted", &b ) )
                peer.isEncrypted = b;
            if( tr_bencDictFindBool( child, "isIncoming", &b ) )
                peer.isIncoming = b;
            if( tr_bencDictFindBool( child, "isUploadingTo", &b ) )
                peer.isUploadingTo = b;
            if( tr_bencDictFindBool( child, "peerIsChoked", &b ) )
                peer.peerIsChoked = b;
            if( tr_bencDictFindBool( child, "peerIsInterested", &b ) )
                peer.peerIsInterested = b;
            if( tr_bencDictFindInt( child, "port", &i ) )
                peer.port = i;
            if( tr_bencDictFindReal( child, "progress", &d ) )
                peer.progress = d;
            if( tr_bencDictFindInt( child, "rateToClient", &i ) )
                peer.rateToClient = Speed::fromBps( i );
            if( tr_bencDictFindInt( child, "rateToPeer", &i ) )
                peer.rateToPeer = Speed::fromBps( i );
            peerList << peer;
        }
        myValues[PEERS].setValue( peerList );
        changed = true;
    }

    if( changed )
        emit torrentChanged( id( ) );
}

QString
Torrent :: activityString( ) const
{
    QString str;

    switch( getActivity( ) )
    {
        case TR_STATUS_CHECK_WAIT: str = tr( "Waiting to verify local data" ); break;
        case TR_STATUS_CHECK:      str = tr( "Verifying local data" ); break;
        case TR_STATUS_DOWNLOAD:   str = tr( "Downloading" ); break;
        case TR_STATUS_SEED:       str = tr( "Seeding" ); break;
        case TR_STATUS_STOPPED:    str = tr( "Paused" ); break;
    }

    return str;
}
