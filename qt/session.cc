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
#include <QByteArray>
#include <QClipboard>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QMessageBox>
#include <QNetworkProxy>
#include <QNetworkProxyFactory>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>
#include <QStringList>
#include <QStyle>
#include <QTextStream>

#include <curl/curl.h>

#include <event2/buffer.h>

#include <libtransmission/transmission.h>
#include <libtransmission/bencode.h>
#include <libtransmission/json.h>
#include <libtransmission/rpcimpl.h>
#include <libtransmission/utils.h> // tr_free
#include <libtransmission/version.h> // LONG_VERSION
#include <libtransmission/web.h>

#include "add-data.h"
#include "prefs.h"
#include "session.h"
#include "session-dialog.h"
#include "torrent.h"
#include "utils.h"

// #define DEBUG_HTTP

namespace
{
    enum
    {
        TAG_SOME_TORRENTS,
        TAG_ALL_TORRENTS,
        TAG_SESSION_STATS,
        TAG_SESSION_INFO,
        TAG_BLOCKLIST_UPDATE,
        TAG_ADD_TORRENT,
        TAG_PORT_TEST,
        TAG_MAGNET_LINK,

        FIRST_UNIQUE_TAG
    };
}

/***
****
***/

namespace
{
    typedef Torrent::KeyList KeyList;
    const KeyList& getInfoKeys( ) { return Torrent::getInfoKeys( ); }
    const KeyList& getStatKeys( ) { return Torrent::getStatKeys( ); }
    const KeyList& getExtraStatKeys( ) { return Torrent::getExtraStatKeys( ); }

    void
    addList( tr_benc * list, const KeyList& strings )
    {
        tr_bencListReserve( list, strings.size( ) );
        foreach( const char * str, strings )
            tr_bencListAddStr( list, str );
    }
}

/***
****
***/

void
Session :: sessionSet( const char * key, const QVariant& value )
{
    tr_benc top;
    tr_bencInitDict( &top, 2 );
    tr_bencDictAddStr( &top, "method", "session-set" );
    tr_benc * args( tr_bencDictAddDict( &top, "arguments", 1 ) );
    switch( value.type( ) ) {
        case QVariant::Bool:   tr_bencDictAddBool ( args, key, value.toBool() ); break;
        case QVariant::Int:    tr_bencDictAddInt  ( args, key, value.toInt() ); break;
        case QVariant::Double: tr_bencDictAddReal ( args, key, value.toDouble() ); break;
        case QVariant::String: tr_bencDictAddStr  ( args, key, value.toString().toUtf8().constData() ); break;
        default: assert( "unknown type" );
    }
    exec( &top );
    tr_bencFree( &top );
}

void
Session :: portTest( )
{
    tr_benc top;
    tr_bencInitDict( &top, 2 );
    tr_bencDictAddStr( &top, "method", "port-test" );
    tr_bencDictAddInt( &top, "tag", TAG_PORT_TEST );
    exec( &top );
    tr_bencFree( &top );
}

void
Session :: copyMagnetLinkToClipboard( int torrentId )
{
    tr_benc top;
    tr_bencInitDict( &top, 3 );
    tr_bencDictAddStr( &top, "method", "torrent-get" );
    tr_bencDictAddInt( &top, "tag", TAG_MAGNET_LINK );
    tr_benc * args = tr_bencDictAddDict( &top, "arguments", 2 );
    tr_bencListAddInt( tr_bencDictAddList( args, "ids", 1 ), torrentId );
    tr_bencListAddStr( tr_bencDictAddList( args, "fields", 1 ), "magnetLink" );

    exec( &top );
    tr_bencFree( &top );
}

void
Session :: updatePref( int key )
{
    if( myPrefs.isCore( key ) ) switch( key )
    {
        case Prefs :: ALT_SPEED_LIMIT_DOWN:
        case Prefs :: ALT_SPEED_LIMIT_ENABLED:
        case Prefs :: ALT_SPEED_LIMIT_TIME_BEGIN:
        case Prefs :: ALT_SPEED_LIMIT_TIME_DAY:
        case Prefs :: ALT_SPEED_LIMIT_TIME_ENABLED:
        case Prefs :: ALT_SPEED_LIMIT_TIME_END:
        case Prefs :: ALT_SPEED_LIMIT_UP:
        case Prefs :: BLOCKLIST_DATE:
        case Prefs :: BLOCKLIST_ENABLED:
        case Prefs :: BLOCKLIST_URL:
        case Prefs :: DHT_ENABLED:
        case Prefs :: DOWNLOAD_DIR:
        case Prefs :: DOWNLOAD_QUEUE_ENABLED:
        case Prefs :: DOWNLOAD_QUEUE_SIZE:
        case Prefs :: DSPEED:
        case Prefs :: DSPEED_ENABLED:
        case Prefs :: IDLE_LIMIT:
        case Prefs :: IDLE_LIMIT_ENABLED:
        case Prefs :: INCOMPLETE_DIR:
        case Prefs :: INCOMPLETE_DIR_ENABLED:
        case Prefs :: LPD_ENABLED:
        case Prefs :: PEER_LIMIT_GLOBAL:
        case Prefs :: PEER_LIMIT_TORRENT:
        case Prefs :: PEER_PORT:
        case Prefs :: PEER_PORT_RANDOM_ON_START:
        case Prefs :: QUEUE_STALLED_MINUTES:
        case Prefs :: PEX_ENABLED:
        case Prefs :: PORT_FORWARDING:
        case Prefs :: RENAME_PARTIAL_FILES:
        case Prefs :: SCRIPT_TORRENT_DONE_ENABLED:
        case Prefs :: SCRIPT_TORRENT_DONE_FILENAME:
        case Prefs :: START:
        case Prefs :: TRASH_ORIGINAL:
        case Prefs :: USPEED:
        case Prefs :: USPEED_ENABLED:
        case Prefs :: UTP_ENABLED:
            sessionSet( myPrefs.keyStr(key), myPrefs.variant(key) );
            break;

        case Prefs :: RATIO:
            sessionSet( "seedRatioLimit", myPrefs.variant(key) );
            break;
        case Prefs :: RATIO_ENABLED:
            sessionSet( "seedRatioLimited", myPrefs.variant(key) );
            break;

        case Prefs :: ENCRYPTION:
            {
                const int i = myPrefs.variant(key).toInt();
                switch( i )
                {
                    case 0:
                        sessionSet( myPrefs.keyStr(key), "tolerated" );
                        break;
                    case 1:
                        sessionSet( myPrefs.keyStr(key), "preferred" );
                        break;
                    case 2:
                        sessionSet( myPrefs.keyStr(key), "required" );
                        break;
                }
                break;
            }

        case Prefs :: RPC_AUTH_REQUIRED:
            if( mySession )
                tr_sessionSetRPCPasswordEnabled( mySession, myPrefs.getBool(key) );
            break;
        case Prefs :: RPC_ENABLED:
            if( mySession )
                tr_sessionSetRPCEnabled( mySession, myPrefs.getBool(key) );
            break;
        case Prefs :: RPC_PASSWORD:
            if( mySession )
                tr_sessionSetRPCPassword( mySession, myPrefs.getString(key).toUtf8().constData() );
            break;
        case Prefs :: RPC_PORT:
            if( mySession )
                tr_sessionSetRPCPort( mySession, myPrefs.getInt(key) );
            break;
        case Prefs :: RPC_USERNAME:
            if( mySession )
                tr_sessionSetRPCUsername( mySession, myPrefs.getString(key).toUtf8().constData() );
            break;
        case Prefs :: RPC_WHITELIST_ENABLED:
            if( mySession )
                tr_sessionSetRPCWhitelistEnabled( mySession, myPrefs.getBool(key) );
            break;
        case Prefs :: RPC_WHITELIST:
            if( mySession )
                tr_sessionSetRPCWhitelist( mySession, myPrefs.getString(key).toUtf8().constData() );
            break;

        default:
            std::cerr << "unhandled pref: " << key << std::endl;
    }
}

/***
****
***/

Session :: Session( const char * configDir, Prefs& prefs ):
    nextUniqueTag( FIRST_UNIQUE_TAG ),
    myBlocklistSize( -1 ),
    myPrefs( prefs ),
    mySession( 0 ),
    myConfigDir( QString::fromUtf8( configDir ) ),
    myNAM( 0 )
{
    myStats.ratio = TR_RATIO_NA;
    myStats.uploadedBytes = 0;
    myStats.downloadedBytes = 0;
    myStats.filesAdded = 0;
    myStats.sessionCount = 0;
    myStats.secondsActive = 0;
    myCumulativeStats = myStats;

    connect( &myPrefs, SIGNAL(changed(int)), this, SLOT(updatePref(int)) );
}

Session :: ~Session( )
{
    stop( );
}

QNetworkAccessManager *
Session :: networkAccessManager( )
{
    if( myNAM == 0 )
    {
        myNAM = new QNetworkAccessManager;

        connect( myNAM, SIGNAL(finished(QNetworkReply*)),
                 this, SLOT(onFinished(QNetworkReply*)) );

        connect( myNAM, SIGNAL(authenticationRequired(QNetworkReply*,QAuthenticator*)),
                 this, SIGNAL(httpAuthenticationRequired()) );
    }

    return myNAM;
}

/***
****
***/

void
Session :: stop( )
{
    if( myNAM != 0 )
    {
        myNAM->deleteLater( );
        myNAM = 0;
    }

    myUrl.clear( );

    if( mySession )
    {
        tr_sessionClose( mySession );
        mySession = 0;
    }
}

void
Session :: restart( )
{
    stop( );
    start( );
}

void
Session :: start( )
{
    if( myPrefs.get<bool>(Prefs::SESSION_IS_REMOTE) )
    {
        QUrl url;
        url.setScheme( "http" );
        url.setHost( myPrefs.get<QString>(Prefs::SESSION_REMOTE_HOST) );
        url.setPort( myPrefs.get<int>(Prefs::SESSION_REMOTE_PORT) );
        url.setPath( "/transmission/rpc" );
        if( myPrefs.get<bool>(Prefs::SESSION_REMOTE_AUTH) )
        {
            url.setUserName( myPrefs.get<QString>(Prefs::SESSION_REMOTE_USERNAME) );
            url.setPassword( myPrefs.get<QString>(Prefs::SESSION_REMOTE_PASSWORD) );
        }
        myUrl = url;
    }
    else
    {
        tr_benc settings;
        tr_bencInitDict( &settings, 0 );
        tr_sessionLoadSettings( &settings, myConfigDir.toUtf8().constData(), "qt" );
        mySession = tr_sessionInit( "qt", myConfigDir.toUtf8().constData(), true, &settings );
        tr_bencFree( &settings );

        tr_ctor * ctor = tr_ctorNew( mySession );
        int torrentCount;
        tr_torrent ** torrents = tr_sessionLoadTorrents( mySession, ctor, &torrentCount );
        tr_free( torrents );
        tr_ctorFree( ctor );
    }

    emit sourceChanged( );
}

bool
Session :: isServer( ) const
{
    return mySession != 0;
}

bool
Session :: isLocal( ) const
{
    if( mySession != 0 )
        return true;

    if( myUrl.host() == "127.0.0.1" )
        return true;

    if( !myUrl.host().compare( "localhost", Qt::CaseInsensitive ) )
        return true;

    return false;
}

/***
****
***/

namespace
{
    tr_benc *
    buildRequest( const char * method, tr_benc& top, int tag=-1 )
    {
        tr_bencInitDict( &top, 3 );
        tr_bencDictAddStr( &top, "method", method );
        if( tag >= 0 )
            tr_bencDictAddInt( &top, "tag", tag );
        return tr_bencDictAddDict( &top, "arguments", 0 );
    }

    void
    addOptionalIds( tr_benc * args, const QSet<int>& ids )
    {
        if( !ids.isEmpty( ) )
        {
            tr_benc * idList( tr_bencDictAddList( args, "ids", ids.size( ) ) );
            foreach( int i, ids )
                tr_bencListAddInt( idList, i );
        }
    }
}

void
Session :: torrentSet( const QSet<int>& ids, const QString& key, double value )
{
    tr_benc top;
    tr_bencInitDict( &top, 2 );
    tr_bencDictAddStr( &top, "method", "torrent-set" );
    tr_benc * args = tr_bencDictAddDict( &top, "arguments", 2 );
    tr_bencDictAddReal( args, key.toUtf8().constData(), value );
    addOptionalIds( args, ids );
    exec( &top );
    tr_bencFree( &top );
}

void
Session :: torrentSet( const QSet<int>& ids, const QString& key, int value )
{
    tr_benc top;
    tr_bencInitDict( &top, 2 );
    tr_bencDictAddStr( &top, "method", "torrent-set" );
    tr_benc * args = tr_bencDictAddDict( &top, "arguments", 2 );
    tr_bencDictAddInt( args, key.toUtf8().constData(), value );
    addOptionalIds( args, ids );
    exec( &top );
    tr_bencFree( &top );
}

void
Session :: torrentSet( const QSet<int>& ids, const QString& key, bool value )
{
    tr_benc top;
    tr_bencInitDict( &top, 2 );
    tr_bencDictAddStr( &top, "method", "torrent-set" );
    tr_benc * args = tr_bencDictAddDict( &top, "arguments", 2 );
    tr_bencDictAddBool( args, key.toUtf8().constData(), value );
    addOptionalIds( args, ids );
    exec( &top );
    tr_bencFree( &top );
}

void
Session :: torrentSet( const QSet<int>& ids, const QString& key, const QStringList& value )
{
    tr_benc top;
    tr_bencInitDict( &top, 2 );
    tr_bencDictAddStr( &top, "method", "torrent-set" );
    tr_benc * args = tr_bencDictAddDict( &top, "arguments", 2 );
    addOptionalIds( args, ids );
    tr_benc * list( tr_bencDictAddList( args, key.toUtf8().constData(), value.size( ) ) );
    foreach( const QString str, value )
        tr_bencListAddStr( list, str.toUtf8().constData() );
    exec( &top );
    tr_bencFree( &top );
}

void
Session :: torrentSet( const QSet<int>& ids, const QString& key, const QList<int>& value )
{
    tr_benc top;
    tr_bencInitDict( &top, 2 );
    tr_bencDictAddStr( &top, "method", "torrent-set" );
    tr_benc * args( tr_bencDictAddDict( &top, "arguments", 2 ) );
    addOptionalIds( args, ids );
    tr_benc * list( tr_bencDictAddList( args, key.toUtf8().constData(), value.size( ) ) );
    foreach( int i, value )
        tr_bencListAddInt( list, i );
    exec( &top );
    tr_bencFree( &top );
}

void
Session :: torrentSet( const QSet<int>& ids, const QString& key, const QPair<int,QString>& value )
{
    tr_benc top;
    tr_bencInitDict( &top, 2 );
    tr_bencDictAddStr( &top, "method", "torrent-set" );
    tr_benc * args( tr_bencDictAddDict( &top, "arguments", 2 ) );
    addOptionalIds( args, ids );
    tr_benc * list( tr_bencDictAddList( args, key.toUtf8().constData(), 2 ) );
    tr_bencListAddInt( list, value.first );
    tr_bencListAddStr( list, value.second.toUtf8().constData() );
    exec( &top );
    tr_bencFree( &top );
}

void
Session :: torrentSetLocation( const QSet<int>& ids, const QString& location, bool doMove )
{
    tr_benc top;
    tr_bencInitDict( &top, 2 );
    tr_bencDictAddStr( &top, "method", "torrent-set-location" );
    tr_benc * args( tr_bencDictAddDict( &top, "arguments", 3 ) );
    addOptionalIds( args, ids );
    tr_bencDictAddStr( args, "location", location.toUtf8().constData() );
    tr_bencDictAddBool( args, "move", doMove );
    exec( &top );
    tr_bencFree( &top );
}

void
Session :: refreshTorrents( const QSet<int>& ids )
{
    if( ids.empty( ) )
    {
        refreshAllTorrents( );
    }
    else
    {
        tr_benc top;
        tr_bencInitDict( &top, 3 );
        tr_bencDictAddStr( &top, "method", "torrent-get" );
        tr_bencDictAddInt( &top, "tag", TAG_SOME_TORRENTS );
        tr_benc * args( tr_bencDictAddDict( &top, "arguments", 2 ) );
        addList( tr_bencDictAddList( args, "fields", 0 ), getStatKeys( ) );
        addOptionalIds( args, ids );
        exec( &top );
        tr_bencFree( &top );
    }
}

void
Session :: refreshExtraStats( const QSet<int>& ids )
{
    tr_benc top;
    tr_bencInitDict( &top, 3 );
    tr_bencDictAddStr( &top, "method", "torrent-get" );
    tr_bencDictAddInt( &top, "tag", TAG_SOME_TORRENTS );
    tr_benc * args( tr_bencDictAddDict( &top, "arguments", 2 ) );
    addOptionalIds( args, ids );
    addList( tr_bencDictAddList( args, "fields", 0 ), getStatKeys( ) + getExtraStatKeys( ));
    exec( &top );
    tr_bencFree( &top );
}

void
Session :: sendTorrentRequest( const char * request, const QSet<int>& ids )
{
    tr_benc top;
    tr_benc * args( buildRequest( request, top ) );
    addOptionalIds( args, ids );
    exec( &top );
    tr_bencFree( &top );

    refreshTorrents( ids );
}

void Session :: pauseTorrents    ( const QSet<int>& ids ) { sendTorrentRequest( "torrent-stop", ids ); }
void Session :: startTorrents    ( const QSet<int>& ids ) { sendTorrentRequest( "torrent-start", ids ); } 
void Session :: startTorrentsNow ( const QSet<int>& ids ) { sendTorrentRequest( "torrent-start-now", ids ); }
void Session :: queueMoveTop     ( const QSet<int>& ids ) { sendTorrentRequest( "queue-move-top", ids ); } 
void Session :: queueMoveUp      ( const QSet<int>& ids ) { sendTorrentRequest( "queue-move-up", ids ); } 
void Session :: queueMoveDown    ( const QSet<int>& ids ) { sendTorrentRequest( "queue-move-down", ids ); } 
void Session :: queueMoveBottom  ( const QSet<int>& ids ) { sendTorrentRequest( "queue-move-bottom", ids ); } 

void
Session :: refreshActiveTorrents( )
{
    tr_benc top;
    tr_bencInitDict( &top, 3 );
    tr_bencDictAddStr( &top, "method", "torrent-get" );
    tr_bencDictAddInt( &top, "tag", TAG_SOME_TORRENTS );
    tr_benc * args( tr_bencDictAddDict( &top, "arguments", 2 ) );
    tr_bencDictAddStr( args, "ids", "recently-active" );
    addList( tr_bencDictAddList( args, "fields", 0 ), getStatKeys( ) );
    exec( &top );
    tr_bencFree( &top );
}

void
Session :: refreshAllTorrents( )
{
    tr_benc top;
    tr_bencInitDict( &top, 3 );
    tr_bencDictAddStr( &top, "method", "torrent-get" );
    tr_bencDictAddInt( &top, "tag", TAG_ALL_TORRENTS );
    tr_benc * args( tr_bencDictAddDict( &top, "arguments", 1 ) );
    addList( tr_bencDictAddList( args, "fields", 0 ), getStatKeys( ) );
    exec( &top );
    tr_bencFree( &top );
}

void
Session :: initTorrents( const QSet<int>& ids )
{
    tr_benc top;
    const int tag( ids.isEmpty() ? TAG_ALL_TORRENTS : TAG_SOME_TORRENTS );
    tr_benc * args( buildRequest( "torrent-get", top, tag ) );
    addOptionalIds( args, ids );
    addList( tr_bencDictAddList( args, "fields", 0 ), getStatKeys()+getInfoKeys() );
    exec( &top );
    tr_bencFree( &top );
}

void
Session :: refreshSessionStats( )
{
    tr_benc top;
    tr_bencInitDict( &top, 2 );
    tr_bencDictAddStr( &top, "method", "session-stats" );
    tr_bencDictAddInt( &top, "tag", TAG_SESSION_STATS );
    exec( &top );
    tr_bencFree( &top );
}

void
Session :: refreshSessionInfo( )
{
    tr_benc top;
    tr_bencInitDict( &top, 2 );
    tr_bencDictAddStr( &top, "method", "session-get" );
    tr_bencDictAddInt( &top, "tag", TAG_SESSION_INFO );
    exec( &top );
    tr_bencFree( &top );
}

void
Session :: updateBlocklist( )
{
    tr_benc top;
    tr_bencInitDict( &top, 2 );
    tr_bencDictAddStr( &top, "method", "blocklist-update" );
    tr_bencDictAddInt( &top, "tag", TAG_BLOCKLIST_UPDATE );
    exec( &top );
    tr_bencFree( &top );
}

/***
****
***/

void
Session :: exec( const tr_benc * request )
{
    char * str = tr_bencToStr( request, TR_FMT_JSON_LEAN, NULL );
    exec( str );
    tr_free( str );
}

void
Session :: localSessionCallback( tr_session * session, struct evbuffer * json, void * self )
{
    Q_UNUSED( session );

    ((Session*)self)->parseResponse( (const char*) evbuffer_pullup( json, -1 ), evbuffer_get_length( json ) );
}

#define REQUEST_DATA_PROPERTY_KEY "requestData"

void
Session :: exec( const char * json )
{
    if( mySession  )
    {
        tr_rpc_request_exec_json( mySession, json, strlen( json ), localSessionCallback, this );
    }
    else if( !myUrl.isEmpty( ) )
    {
        QNetworkRequest request;
        request.setUrl( myUrl );
        request.setRawHeader( "User-Agent", QString( QCoreApplication::instance()->applicationName() + "/" + LONG_VERSION_STRING ).toAscii() );
        request.setRawHeader( "Content-Type", "application/json; charset=UTF-8" );
        if( !mySessionId.isEmpty( ) )
            request.setRawHeader( TR_RPC_SESSION_ID_HEADER, mySessionId.toAscii() );

        const QByteArray requestData( json );
        QNetworkReply * reply = networkAccessManager()->post( request, requestData );
        reply->setProperty( REQUEST_DATA_PROPERTY_KEY, requestData );
        connect( reply, SIGNAL(downloadProgress(qint64,qint64)), this, SIGNAL(dataReadProgress()));
        connect( reply, SIGNAL(uploadProgress(qint64,qint64)), this, SIGNAL(dataSendProgress()));

#ifdef DEBUG_HTTP
        std::cerr << "sending " << "POST " << qPrintable( myUrl.path() ) << std::endl;
        foreach( QByteArray b, request.rawHeaderList() )
            std::cerr << b.constData()
                      << ": "
                      << request.rawHeader( b ).constData()
                      << std::endl;
        std::cerr << "Body:\n" << json << std::endl;
#endif
    }
}

void
Session :: onFinished( QNetworkReply * reply )
{
#ifdef DEBUG_HTTP
    std::cerr << "http response header: " << std::endl;
    foreach( QByteArray b, reply->rawHeaderList() )
        std::cerr << b.constData()
                  << ": "
                  << reply->rawHeader( b ).constData()
                  << std::endl;
    std::cerr << "json:\n" << reply->peek( reply->bytesAvailable() ).constData() << std::endl;
#endif

    if( ( reply->attribute( QNetworkRequest::HttpStatusCodeAttribute ).toInt() == 409 )
        && ( reply->hasRawHeader( TR_RPC_SESSION_ID_HEADER ) ) )
    {
        // we got a 409 telling us our session id has expired.
        // update it and resubmit the request.
        mySessionId = QString( reply->rawHeader( TR_RPC_SESSION_ID_HEADER ) );
        exec( reply->property( REQUEST_DATA_PROPERTY_KEY ).toByteArray( ).constData( ) );
    }
    else if( reply->error() != QNetworkReply::NoError )
    {
        std::cerr << "http error: " << qPrintable( reply->errorString() ) << std::endl;
    }
    else
    {
        const QByteArray response( reply->readAll() );
        const char * json( response.constData( ) );
        int jsonLength( response.size( ) );
        if( jsonLength>0 && json[jsonLength-1] == '\n' ) --jsonLength;
        parseResponse( json, jsonLength );
    }

    reply->deleteLater();
}

void
Session :: parseResponse( const char * json, size_t jsonLength )
{
    tr_benc top;
    const uint8_t * end( 0 );
    const int err( tr_jsonParse( "rpc", json, jsonLength, &top, &end ) );
    if( !err )
    {
        int64_t tag = -1;
        const char * result = NULL;
        tr_benc * args = NULL;

        tr_bencDictFindInt ( &top, "tag", &tag );
        tr_bencDictFindStr ( &top, "result", &result );
        tr_bencDictFindDict( &top, "arguments", &args );

        emit executed( tag, result, args );

        tr_benc * torrents;
        const char * str;

        if( tr_bencDictFindInt( &top, "tag", &tag ) )
        {
            switch( tag )
            {
                case TAG_SOME_TORRENTS:
                case TAG_ALL_TORRENTS:
                    if( tr_bencDictFindDict( &top, "arguments", &args ) ) {
                        if( tr_bencDictFindList( args, "torrents", &torrents ) )
                            emit torrentsUpdated( torrents, tag==TAG_ALL_TORRENTS );
                        if( tr_bencDictFindList( args, "removed", &torrents ) )
                            emit torrentsRemoved( torrents );
                    }
                    break;

                case TAG_SESSION_STATS:
                    if( tr_bencDictFindDict( &top, "arguments", &args ) )
                        updateStats( args );
                    break;

                case TAG_SESSION_INFO:
                    if( tr_bencDictFindDict( &top, "arguments", &args ) )
                        updateInfo( args );
                    break;

                case TAG_BLOCKLIST_UPDATE: {
                    int64_t intVal = 0;
                    if( tr_bencDictFindDict( &top, "arguments", &args ) )
                        if( tr_bencDictFindInt( args, "blocklist-size", &intVal ) )
                            setBlocklistSize( intVal );
                    break;
                }

                case TAG_PORT_TEST: {
                    bool isOpen = 0;
                    if( tr_bencDictFindDict( &top, "arguments", &args ) )
                        tr_bencDictFindBool( args, "port-is-open", &isOpen );
                    emit portTested( (bool)isOpen );
                }

                case TAG_MAGNET_LINK: {
                    tr_benc * args;
                    tr_benc * torrents;
                    tr_benc * child;
                    const char * str;
                    if( tr_bencDictFindDict( &top, "arguments", &args )
                        && tr_bencDictFindList( args, "torrents", &torrents )
                        && (( child = tr_bencListChild( torrents, 0 )))
                        && tr_bencDictFindStr( child, "magnetLink", &str ) )
                            QApplication::clipboard()->setText( str );
                    break;
                }

                case TAG_ADD_TORRENT:
                    str = "";
                    if( tr_bencDictFindStr( &top, "result", &str ) && strcmp( str, "success" ) ) {
                        QMessageBox * d = new QMessageBox( QMessageBox::Information,
                                                           tr( "Add Torrent" ),
                                                           QString::fromUtf8(str),
                                                           QMessageBox::Close,
                                                           QApplication::activeWindow());
                        connect( d, SIGNAL(rejected()), d, SLOT(deleteLater()) );
                        d->show( );
                    }
                    break;

                default:
                    break;
            }
        }
        tr_bencFree( &top );
    }
}

void
Session :: updateStats( tr_benc * d, struct tr_session_stats * stats )
{
    int64_t i;

    if( tr_bencDictFindInt( d, "uploadedBytes", &i ) )
        stats->uploadedBytes = i;
    if( tr_bencDictFindInt( d, "downloadedBytes", &i ) )
        stats->downloadedBytes = i;
    if( tr_bencDictFindInt( d, "filesAdded", &i ) )
        stats->filesAdded = i;
    if( tr_bencDictFindInt( d, "sessionCount", &i ) )
        stats->sessionCount = i;
    if( tr_bencDictFindInt( d, "secondsActive", &i ) )
        stats->secondsActive = i;

    stats->ratio = tr_getRatio( stats->uploadedBytes, stats->downloadedBytes );

}

void
Session :: updateStats( tr_benc * d )
{
    tr_benc * c;

    if( tr_bencDictFindDict( d, "current-stats", &c ) )
        updateStats( c, &myStats );

    if( tr_bencDictFindDict( d, "cumulative-stats", &c ) )
        updateStats( c, &myCumulativeStats );

    emit statsUpdated( );
}

void
Session :: updateInfo( tr_benc * d )
{
    int64_t i;
    const char * str;
    disconnect( &myPrefs, SIGNAL(changed(int)), this, SLOT(updatePref(int)) );

    for( int i=Prefs::FIRST_CORE_PREF; i<=Prefs::LAST_CORE_PREF; ++i )
    {
        const tr_benc * b( tr_bencDictFind( d, myPrefs.keyStr( i ) ) );

        if( !b )
            continue;

        if( i == Prefs :: ENCRYPTION )
        {
            const char * val;
            if( tr_bencGetStr( b, &val ) )
            {
                if( !qstrcmp( val , "required" ) )
                    myPrefs.set( i, 2 );
                else if( !qstrcmp( val , "preferred" ) )
                    myPrefs.set( i, 1 );
                else if( !qstrcmp( val , "tolerated" ) )
                    myPrefs.set( i, 0 );
            }
            continue;
        }

        switch( myPrefs.type( i ) )
        {
            case QVariant :: Int: {
                int64_t val;
                if( tr_bencGetInt( b, &val ) )
                    myPrefs.set( i, (int)val );
                break;
            }
            case QVariant :: Double: {
                double val;
                if( tr_bencGetReal( b, &val ) )
                    myPrefs.set( i, val );
                break;
            }
            case QVariant :: Bool: {
                bool val;
                if( tr_bencGetBool( b, &val ) )
                    myPrefs.set( i, (bool)val );
                break;
            }
            case TrTypes :: FilterModeType:
            case TrTypes :: SortModeType:
            case QVariant :: String: {
                const char * val;
                if( tr_bencGetStr( b, &val ) )
                    myPrefs.set( i, QString(val) );
                break;
            }
            default:
                break;
        }
    }

    bool b;
    double x;
    if( tr_bencDictFindBool( d, "seedRatioLimited", &b ) )
        myPrefs.set( Prefs::RATIO_ENABLED, b ? true : false );
    if( tr_bencDictFindReal( d, "seedRatioLimit", &x ) )
        myPrefs.set( Prefs::RATIO, x );

    /* Use the C API to get settings that, for security reasons, aren't supported by RPC */
    if( mySession != 0 )
    {
        myPrefs.set( Prefs::RPC_ENABLED,           tr_sessionIsRPCEnabled           ( mySession ) );
        myPrefs.set( Prefs::RPC_AUTH_REQUIRED,     tr_sessionIsRPCPasswordEnabled   ( mySession ) );
        myPrefs.set( Prefs::RPC_PASSWORD,          tr_sessionGetRPCPassword         ( mySession ) );
        myPrefs.set( Prefs::RPC_PORT,              tr_sessionGetRPCPort             ( mySession ) );
        myPrefs.set( Prefs::RPC_USERNAME,          tr_sessionGetRPCUsername         ( mySession ) );
        myPrefs.set( Prefs::RPC_WHITELIST_ENABLED, tr_sessionGetRPCWhitelistEnabled ( mySession ) );
        myPrefs.set( Prefs::RPC_WHITELIST,         tr_sessionGetRPCWhitelist        ( mySession ) );
    }

    if( tr_bencDictFindInt( d, "blocklist-size", &i ) && i!=blocklistSize( ) )
        setBlocklistSize( i );

    if( tr_bencDictFindStr( d, "version", &str ) && ( mySessionVersion != str ) )
        mySessionVersion = str;

    //std::cerr << "Session :: updateInfo end" << std::endl;
    connect( &myPrefs, SIGNAL(changed(int)), this, SLOT(updatePref(int)) );

    emit sessionUpdated( );
}

void
Session :: setBlocklistSize( int64_t i )
{
    myBlocklistSize = i;

    emit blocklistUpdated( i );
}

void
Session :: addTorrent( const AddData& addMe )
{
    const QByteArray b64 = addMe.toBase64();

    tr_benc top, *args;
    tr_bencInitDict( &top, 2 );
    tr_bencDictAddStr( &top, "method", "torrent-add" );
    args = tr_bencDictAddDict( &top, "arguments", 2 );
    tr_bencDictAddBool( args, "paused", !myPrefs.getBool( Prefs::START ) );
    switch( addMe.type ) {
        case AddData::MAGNET:   tr_bencDictAddStr( args, "filename", addMe.magnet.toUtf8().constData() ); break;
        case AddData::URL:      tr_bencDictAddStr( args, "filename", addMe.url.toString().toUtf8().constData() ); break;
        case AddData::FILENAME: /* fall-through */
        case AddData::METAINFO: tr_bencDictAddRaw( args, "metainfo", b64.constData(), b64.size() ); break;
        default: std::cerr << "Unhandled AddData type: " << addMe.type << std::endl;
    }
    exec( &top );
    tr_bencFree( &top );
}

void
Session :: addNewlyCreatedTorrent( const QString& filename, const QString& localPath )
{
    const QByteArray b64 = AddData(filename).toBase64();

    tr_benc top, *args;
    tr_bencInitDict( &top, 2 );
    tr_bencDictAddStr( &top, "method", "torrent-add" );
    args = tr_bencDictAddDict( &top, "arguments", 3 );
    tr_bencDictAddStr( args, "download-dir", qPrintable(localPath) );
    tr_bencDictAddBool( args, "paused", !myPrefs.getBool( Prefs::START ) );
    tr_bencDictAddRaw( args, "metainfo", b64.constData(), b64.size() );
    exec( &top );
    tr_bencFree( &top );
}

void
Session :: removeTorrents( const QSet<int>& ids, bool deleteFiles )
{
    if( !ids.isEmpty( ) )
    {
        tr_benc top, *args;
        tr_bencInitDict( &top, 2 );
        tr_bencDictAddStr( &top, "method", "torrent-remove" );
        args = tr_bencDictAddDict( &top, "arguments", 2 );
        addOptionalIds( args, ids );
        tr_bencDictAddInt( args, "delete-local-data", deleteFiles );
        exec( &top );
        tr_bencFree( &top );
    }
}

void
Session :: verifyTorrents( const QSet<int>& ids )
{
    if( !ids.isEmpty( ) )
    {
        tr_benc top, *args;
        tr_bencInitDict( &top, 2 );
        tr_bencDictAddStr( &top, "method", "torrent-verify" );
        args = tr_bencDictAddDict( &top, "arguments", 1 );
        addOptionalIds( args, ids );
        exec( &top );
        tr_bencFree( &top );
    }
}

void
Session :: reannounceTorrents( const QSet<int>& ids )
{
    if( !ids.isEmpty( ) )
    {
        tr_benc top, *args;
        tr_bencInitDict( &top, 2 );
        tr_bencDictAddStr( &top, "method", "torrent-reannounce" );
        args = tr_bencDictAddDict( &top, "arguments", 1 );
        addOptionalIds( args, ids );
        exec( &top );
        tr_bencFree( &top );
    }
}

/***
****
***/

void
Session :: launchWebInterface( )
{
    QUrl url;
    if( !mySession ) // remote session
    {
        url = myUrl;
        url.setPath( "/transmission/web/" );
    }
    else // local session
    {
        url.setScheme( "http" );
        url.setHost( "localhost" );
        url.setPort( myPrefs.getInt( Prefs::RPC_PORT ) );
    }
    QDesktopServices :: openUrl( url );
}
