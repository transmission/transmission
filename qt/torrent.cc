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

#include <QApplication>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QSet>
#include <QString>
#include <QStyle>
#include <QUrl>
#include <QVariant>

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h> /* tr_new0, tr_strdup */
#include <libtransmission/variant.h>

#include "app.h"
#include "prefs.h"
#include "torrent.h"
#include "utils.h"


Torrent :: Torrent( Prefs& prefs, int id ):
    magnetTorrent( false ),
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
    { UPLOAD_SPEED, "rateUpload", QVariant::ULongLong, STAT } /* Bps */,
    { DOWNLOAD_SPEED, "rateDownload", QVariant::ULongLong, STAT }, /* Bps */
    { DOWNLOAD_DIR, "downloadDir", QVariant::String, STAT },
    { ACTIVITY, "status", QVariant::Int, STAT },
    { NAME, "name", QVariant::String, INFO },
    { ERROR, "error", QVariant::Int, STAT },
    { ERROR_STRING, "errorString", QVariant::String, STAT },
    { SIZE_WHEN_DONE, "sizeWhenDone", QVariant::ULongLong, STAT },
    { LEFT_UNTIL_DONE, "leftUntilDone", QVariant::ULongLong, STAT },
    { HAVE_UNCHECKED, "haveUnchecked", QVariant::ULongLong, STAT },
    { HAVE_VERIFIED, "haveValid", QVariant::ULongLong, STAT },
    { DESIRED_AVAILABLE, "desiredAvailable", QVariant::ULongLong, STAT },
    { TOTAL_SIZE, "totalSize", QVariant::ULongLong, INFO },
    { PIECE_SIZE, "pieceSize", QVariant::ULongLong, INFO },
    { PIECE_COUNT, "pieceCount", QVariant::Int, INFO },
    { PEERS_GETTING_FROM_US, "peersGettingFromUs", QVariant::Int, STAT },
    { PEERS_SENDING_TO_US, "peersSendingToUs", QVariant::Int, STAT },
    { WEBSEEDS_SENDING_TO_US, "webseedsSendingToUs", QVariant::Int, STAT_EXTRA },
    { PERCENT_DONE, "percentDone", QVariant::Double, STAT },
    { METADATA_PERCENT_DONE, "metadataPercentComplete", QVariant::Double, STAT },
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
    { TRACKERS, "trackers", QVariant::StringList, STAT },
    { TRACKERSTATS, "trackerStats", TrTypes::TrackerStatsList, STAT_EXTRA },
    { MIME_ICON, "ccc", QVariant::Icon, DERIVED },
    { SEED_RATIO_LIMIT, "seedRatioLimit", QVariant::Double, STAT },
    { SEED_RATIO_MODE, "seedRatioMode", QVariant::Int, STAT },
    { SEED_IDLE_LIMIT, "seedIdleLimit", QVariant::Int, STAT_EXTRA },
    { SEED_IDLE_MODE, "seedIdleMode", QVariant::Int, STAT_EXTRA },
    { DOWN_LIMIT, "downloadLimit", QVariant::Int, STAT_EXTRA }, /* KB/s */
    { DOWN_LIMITED, "downloadLimited", QVariant::Bool, STAT_EXTRA },
    { UP_LIMIT, "uploadLimit", QVariant::Int, STAT_EXTRA }, /* KB/s */
    { UP_LIMITED, "uploadLimited", QVariant::Bool, STAT_EXTRA },
    { HONORS_SESSION_LIMITS, "honorsSessionLimits", QVariant::Bool, STAT_EXTRA },
    { PEER_LIMIT, "peer-limit", QVariant::Int, STAT_EXTRA },
    { HASH_STRING, "hashString", QVariant::String, INFO },
    { IS_FINISHED, "isFinished", QVariant::Bool, STAT },
    { IS_PRIVATE, "isPrivate", QVariant::Bool, INFO },
    { IS_STALLED, "isStalled", QVariant::Bool, STAT },
    { COMMENT, "comment", QVariant::String, INFO },
    { CREATOR, "creator", QVariant::String, INFO },
    { MANUAL_ANNOUNCE_TIME, "manualAnnounceTime", QVariant::DateTime, STAT_EXTRA },
    { PEERS, "peers", TrTypes::PeerList, STAT_EXTRA },
    { TORRENT_FILE, "torrentFile", QVariant::String, STAT_EXTRA },
    { BANDWIDTH_PRIORITY, "bandwidthPriority", QVariant::Int, STAT_EXTRA },
    { QUEUE_POSITION, "queuePosition", QVariant::Int, STAT },
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
            if(( isLimited = myPrefs.getBool( Prefs :: RATIO_ENABLED )))
                ratio = myPrefs.getDouble( Prefs :: RATIO );
            break;

        default: // TR_RATIOLIMIT_UNLIMITED:
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
Torrent :: compareSeedRatio( const Torrent& that ) const
{
    double a;
    double b;
    const bool has_a = getSeedRatio( a );
    const bool has_b = that.getSeedRatio( b );
    if( !has_a && !has_b ) return 0;
    if( !has_a || !has_b ) return has_a ? -1 : 1;
    if( a < b ) return -1;
    if( a > b ) return 1;
    return 0;
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
    if( haveA ) return 1;
    if( haveB ) return -1;
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
        icon = QFileIconProvider().icon( QFileIconProvider::Folder );
    else if( files.size( ) == 1 )
        icon = Utils :: guessMimeIcon( files.at(0).filename );
    else
        icon = QIcon( );

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
Torrent :: update( tr_variant * d )
{
    bool changed = false;
    const bool was_seed = isSeed( );
    const uint64_t old_verified_size = haveVerified( );

    for( int  i=0; i<PROPERTY_COUNT; ++i )
    {
        tr_variant * child = tr_variantDictFind( d, myProperties[i].key );
        if( !child )
            continue;

        switch( myProperties[i].type )
        {
            case QVariant :: Int: {
                int64_t val;
                if( tr_variantGetInt( child, &val ) )
                    changed |= setInt( i, val );
                break;
            }

            case QVariant :: Bool: {
                bool val;
                if( tr_variantGetBool( child, &val ) )
                    changed |= setBool( i, val );
                break;
            }

            case QVariant :: String: {
                const char * val;
                if( tr_variantGetStr( child, &val, NULL ) )
                    changed |= setString( i, val );
                break;
            }

            case QVariant :: ULongLong: {
                int64_t val;
                if( tr_variantGetInt( child, &val ) )
                    changed |= setSize( i, val );
                break;
            }

            case QVariant :: Double: {
                double val;
                if( tr_variantGetReal( child, &val ) )
                    changed |= setDouble( i, val );
                break;
            }

            case QVariant :: DateTime: {
                int64_t val;
                if( tr_variantGetInt( child, &val ) && val )
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

    tr_variant * files;

    if( tr_variantDictFindList( d, "files", &files ) ) {
        const char * str;
        int64_t intVal;
        int i = 0;
        myFiles.clear( );
        tr_variant * child;
        while(( child = tr_variantListChild( files, i ))) {
            TrFile file;
            size_t len;
            file.index = i++;
            if( tr_variantDictFindStr( child, "name", &str, &len ) )
                file.filename = QString::fromUtf8( str, len );
            if( tr_variantDictFindInt( child, "length", &intVal ) )
                file.size = intVal;
            myFiles.append( file );
        }
        updateMimeIcon( );
        changed = true;
    }

    if( tr_variantDictFindList( d, "fileStats", &files ) ) {
        const int n = tr_variantListSize( files );
        for( int i=0; i<n && i<myFiles.size(); ++i ) {
            int64_t intVal;
            bool boolVal;
            tr_variant * child = tr_variantListChild( files, i );
            TrFile& file( myFiles[i] );
            if( tr_variantDictFindInt( child, "bytesCompleted", &intVal ) )
                file.have = intVal;
            if( tr_variantDictFindBool( child, "wanted", &boolVal ) )
                file.wanted = boolVal;
            if( tr_variantDictFindInt( child, "priority", &intVal ) )
                file.priority = intVal;
        }
        changed = true;
    }

    tr_variant * trackers;
    if( tr_variantDictFindList( d, "trackers", &trackers ) ) {
        size_t len;
        const char * str;
        int i = 0;
        QStringList list;
        tr_variant * child;
        while(( child = tr_variantListChild( trackers, i++ ))) {
            if( tr_variantDictFindStr( child, "announce", &str, &len )) {
                dynamic_cast<MyApp*>(QApplication::instance())->favicons.add( QUrl(QString::fromUtf8(str)) );
                list.append( QString::fromUtf8( str, len ) );
            }
        }
        if( myValues[TRACKERS] != list ) {
            myValues[TRACKERS].setValue( list );
            changed = true;
        }
    }

    tr_variant * trackerStats;
    if( tr_variantDictFindList( d, "trackerStats", &trackerStats ) ) {
        tr_variant * child;
        TrackerStatsList  trackerStatsList;
        int childNum = 0;
        while(( child = tr_variantListChild( trackerStats, childNum++ ))) {
            bool b;
            int64_t i;
            size_t len;
            const char * str;
            TrackerStat trackerStat;
            if( tr_variantDictFindStr( child, "announce", &str, &len ) ) {
                trackerStat.announce = QString::fromUtf8( str, len );
                dynamic_cast<MyApp*>(QApplication::instance())->favicons.add( QUrl( trackerStat.announce ) );
            }
            if( tr_variantDictFindInt( child, "announceState", &i ) )
                trackerStat.announceState = i;
            if( tr_variantDictFindInt( child, "downloadCount", &i ) )
                trackerStat.downloadCount = i;
            if( tr_variantDictFindBool( child, "hasAnnounced", &b ) )
                trackerStat.hasAnnounced = b;
            if( tr_variantDictFindBool( child, "hasScraped", &b ) )
                trackerStat.hasScraped = b;
            if( tr_variantDictFindStr( child, "host", &str, &len ) )
                trackerStat.host = QString::fromUtf8( str, len );
            if( tr_variantDictFindInt( child, "id", &i ) )
                trackerStat.id = i;
            if( tr_variantDictFindBool( child, "isBackup", &b ) )
                trackerStat.isBackup = b;
            if( tr_variantDictFindInt( child, "lastAnnouncePeerCount", &i ) )
                trackerStat.lastAnnouncePeerCount = i;
            if( tr_variantDictFindStr( child, "lastAnnounceResult", &str, &len ) )
                trackerStat.lastAnnounceResult = QString::fromUtf8(str, len);
            if( tr_variantDictFindInt( child, "lastAnnounceStartTime", &i ) )
                trackerStat.lastAnnounceStartTime = i;
            if( tr_variantDictFindBool( child, "lastAnnounceSucceeded", &b ) )
                trackerStat.lastAnnounceSucceeded = b;
            if( tr_variantDictFindInt( child, "lastAnnounceTime", &i ) )
                trackerStat.lastAnnounceTime = i;
            if( tr_variantDictFindBool( child, "lastAnnounceTimedOut", &b ) )
                trackerStat.lastAnnounceTimedOut = b;
            if( tr_variantDictFindStr( child, "lastScrapeResult", &str, &len ) )
                trackerStat.lastScrapeResult = QString::fromUtf8( str, len );
            if( tr_variantDictFindInt( child, "lastScrapeStartTime", &i ) )
                trackerStat.lastScrapeStartTime = i;
            if( tr_variantDictFindBool( child, "lastScrapeSucceeded", &b ) )
                trackerStat.lastScrapeSucceeded = b;
            if( tr_variantDictFindInt( child, "lastScrapeTime", &i ) )
                trackerStat.lastScrapeTime = i;
            if( tr_variantDictFindBool( child, "lastScrapeTimedOut", &b ) )
                trackerStat.lastScrapeTimedOut = b;
            if( tr_variantDictFindInt( child, "leecherCount", &i ) )
                trackerStat.leecherCount = i;
            if( tr_variantDictFindInt( child, "nextAnnounceTime", &i ) )
                trackerStat.nextAnnounceTime = i;
            if( tr_variantDictFindInt( child, "nextScrapeTime", &i ) )
                trackerStat.nextScrapeTime = i;
            if( tr_variantDictFindInt( child, "scrapeState", &i ) )
                trackerStat.scrapeState = i;
            if( tr_variantDictFindInt( child, "seederCount", &i ) )
                trackerStat.seederCount = i;
            if( tr_variantDictFindInt( child, "tier", &i ) )
                trackerStat.tier = i;
            trackerStatsList << trackerStat;
        }
        myValues[TRACKERSTATS].setValue( trackerStatsList );
        changed = true;
    }

    tr_variant * peers;
    if( tr_variantDictFindList( d, "peers", &peers ) ) {
        tr_variant * child;
        PeerList peerList;
        int childNum = 0;
        while(( child = tr_variantListChild( peers, childNum++ ))) {
            double d;
            bool b;
            int64_t i;
            size_t len;
            const char * str;
            Peer peer;
            if( tr_variantDictFindStr( child, "address", &str, &len ) )
                peer.address = QString::fromUtf8( str, len );
            if( tr_variantDictFindStr( child, "clientName", &str, &len ) )
                peer.clientName = QString::fromUtf8( str, len );
            if( tr_variantDictFindBool( child, "clientIsChoked", &b ) )
                peer.clientIsChoked = b;
            if( tr_variantDictFindBool( child, "clientIsInterested", &b ) )
                peer.clientIsInterested = b;
            if( tr_variantDictFindStr( child, "flagStr", &str, &len ) )
                peer.flagStr = QString::fromUtf8( str, len );
            if( tr_variantDictFindBool( child, "isDownloadingFrom", &b ) )
                peer.isDownloadingFrom = b;
            if( tr_variantDictFindBool( child, "isEncrypted", &b ) )
                peer.isEncrypted = b;
            if( tr_variantDictFindBool( child, "isIncoming", &b ) )
                peer.isIncoming = b;
            if( tr_variantDictFindBool( child, "isUploadingTo", &b ) )
                peer.isUploadingTo = b;
            if( tr_variantDictFindBool( child, "peerIsChoked", &b ) )
                peer.peerIsChoked = b;
            if( tr_variantDictFindBool( child, "peerIsInterested", &b ) )
                peer.peerIsInterested = b;
            if( tr_variantDictFindInt( child, "port", &i ) )
                peer.port = i;
            if( tr_variantDictFindReal( child, "progress", &d ) )
                peer.progress = d;
            if( tr_variantDictFindInt( child, "rateToClient", &i ) )
                peer.rateToClient = Speed::fromBps( i );
            if( tr_variantDictFindInt( child, "rateToPeer", &i ) )
                peer.rateToPeer = Speed::fromBps( i );
            peerList << peer;
        }
        myValues[PEERS].setValue( peerList );
        changed = true;
    }

    if( changed )
        emit torrentChanged( id( ) );

    if( !was_seed && isSeed() && (old_verified_size>0) )
        emit torrentCompleted( id( ) );
}

QString
Torrent :: activityString( ) const
{
    QString str;

    switch( getActivity( ) )
    {
        case TR_STATUS_STOPPED:       str = isFinished() ? tr( "Finished" ): tr( "Paused" ); break;
        case TR_STATUS_CHECK_WAIT:    str = tr( "Queued for verification" ); break;
        case TR_STATUS_CHECK:         str = tr( "Verifying local data" ); break;
        case TR_STATUS_DOWNLOAD_WAIT: str = tr( "Queued for download" ); break;
        case TR_STATUS_DOWNLOAD:      str = tr( "Downloading" ); break;
        case TR_STATUS_SEED_WAIT:     str = tr( "Queued for seeding" ); break;
        case TR_STATUS_SEED:          str = tr( "Seeding" ); break;
    }

    return str;
}

QString
Torrent :: getError( ) const
{
    QString s = getString( ERROR_STRING );

    switch( getInt( ERROR ) )
    {
        case TR_STAT_TRACKER_WARNING: s = tr( "Tracker gave a warning: %1" ).arg( s ); break;
        case TR_STAT_TRACKER_ERROR: s = tr( "Tracker gave an error: %1" ).arg( s ); break;
        case TR_STAT_LOCAL_ERROR: s = tr( "Error: %1" ).arg( s ); break;
        default: s.clear(); break;
    }

    return s;
}

QPixmap
TrackerStat :: getFavicon( ) const
{
    MyApp * myApp = dynamic_cast<MyApp*>(QApplication::instance());
    return myApp->favicons.find( QUrl( announce ) );
}

