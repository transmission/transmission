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
#include <QByteArray>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QMessageBox>
#include <QSet>
#include <QStyle>
#include <QTextStream>

#include <libtransmission/transmission.h>
#include <libtransmission/bencode.h>
#include <libtransmission/json.h>
#include <libtransmission/rpcimpl.h>
#include <libtransmission/utils.h> /* tr_free */
#include <libtransmission/version.h> /* LONG_VERSION */

#include "prefs.h"
#include "qticonloader.h"
#include "session.h"
#include "session-dialog.h"
#include "torrent.h"

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
        case QVariant::String: tr_bencDictAddStr  ( args, key, value.toString().toUtf8() ); break;
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
Session :: updatePref( int key )
{
    if( myPrefs.isCore( key ) ) switch( key )
    {
        case Prefs :: ALT_SPEED_LIMIT_UP:
        case Prefs :: ALT_SPEED_LIMIT_DOWN:
        case Prefs :: ALT_SPEED_LIMIT_ENABLED:
        case Prefs :: ALT_SPEED_LIMIT_TIME_BEGIN:
        case Prefs :: ALT_SPEED_LIMIT_TIME_END:
        case Prefs :: ALT_SPEED_LIMIT_TIME_ENABLED:
        case Prefs :: ALT_SPEED_LIMIT_TIME_DAY:
        case Prefs :: BLOCKLIST_ENABLED:
        case Prefs :: BLOCKLIST_DATE:
        case Prefs :: DOWNLOAD_DIR:
        case Prefs :: PEER_LIMIT_GLOBAL:
        case Prefs :: PEER_LIMIT_TORRENT:
        case Prefs :: USPEED_ENABLED:
        case Prefs :: USPEED:
        case Prefs :: DSPEED_ENABLED:
        case Prefs :: DSPEED:
        case Prefs :: PEX_ENABLED:
        case Prefs :: PORT_FORWARDING:
        case Prefs :: PEER_PORT:
        case Prefs :: PEER_PORT_RANDOM_ON_START:
            sessionSet( myPrefs.keyStr(key), myPrefs.variant(key) );
            break;

        case Prefs :: RATIO:
            sessionSet( "seedRatioLimit", myPrefs.variant(key) );           
            break;           
        case Prefs :: RATIO_ENABLED:
            sessionSet( "seedRatioLimited", myPrefs.variant(key) );
            break;

        case Prefs :: RPC_AUTH_REQUIRED:
            if( mySession )
                tr_sessionSetRPCEnabled( mySession, myPrefs.getBool(key) );
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
    myConfigDir( configDir )
{
    myStats.ratio = TR_RATIO_NA;
    myStats.uploadedBytes = 0;
    myStats.downloadedBytes = 0;
    myStats.filesAdded = 0;
    myStats.sessionCount = 0;
    myStats.secondsActive = 0;
    myCumulativeStats = myStats;

    connect( &myHttp, SIGNAL(requestStarted(int)), this, SLOT(onRequestStarted(int)));
    connect( &myHttp, SIGNAL(requestFinished(int,bool)), this, SLOT(onRequestFinished(int,bool)));
    connect( &myHttp, SIGNAL(dataReadProgress(int,int)), this, SIGNAL(dataReadProgress()));
    connect( &myHttp, SIGNAL(dataSendProgress(int,int)), this, SIGNAL(dataSendProgress()));
    connect( &myHttp, SIGNAL(authenticationRequired(QString, quint16, QAuthenticator*)), this, SIGNAL(httpAuthenticationRequired()) );
    connect( &myPrefs, SIGNAL(changed(int)), this, SLOT(updatePref(int)) );

    myBuffer.open( QIODevice::ReadWrite );
}

Session :: ~Session( )
{
    stop( );
}

/***
****
***/

void
Session :: stop( )
{
    myHttp.abort( );
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
        const int port( myPrefs.get<int>(Prefs::SESSION_REMOTE_PORT) );
        const bool auth( myPrefs.get<bool>(Prefs::SESSION_REMOTE_AUTH) );
        const QString host( myPrefs.get<QString>(Prefs::SESSION_REMOTE_HOST) );
        const QString user( myPrefs.get<QString>(Prefs::SESSION_REMOTE_USERNAME) );
        const QString pass( myPrefs.get<QString>(Prefs::SESSION_REMOTE_PASSWORD) );

        QUrl url;
        url.setScheme( "http" );
        url.setHost( host );
        url.setPort( port );
        if( auth ) {
            url.setUserName( user );
            url.setPassword( pass );
        }
        myUrl = url;

        myHttp.setHost( host, port );
        myHttp.setUser( user, pass );
    }
    else
    {
        tr_benc settings;
        tr_bencInitDict( &settings, 0 );
        tr_sessionGetDefaultSettings( &settings );
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

void
Session :: pauseTorrents( const QSet<int>& ids )
{
    sendTorrentRequest( "torrent-stop", ids );
}

void
Session :: startTorrents( const QSet<int>& ids )
{
    sendTorrentRequest( "torrent-start", ids );
}

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
    char * str( tr_bencToJSON( request, FALSE ) );
    exec( str );
    tr_free( str );
}

void
Session :: localSessionCallback( tr_session * session, const char * json, size_t len, void * self )
{
    Q_UNUSED( session );

    ((Session*)self)->parseResponse( json, len );
}

void
Session :: exec( const char * request )
{
    if( mySession  )
    {
        tr_rpc_request_exec_json( mySession, request, strlen( request ), localSessionCallback, this );
    }
    else if( !myUrl.isEmpty( ) )
    {
        static const QString path( "/transmission/rpc" );
        QHttpRequestHeader header( "POST", path );
        header.setValue( "User-Agent", QCoreApplication::instance()->applicationName() + "/" + LONG_VERSION_STRING );
        header.setValue( "Content-Type", "application/json; charset=UTF-8" );
        if( !mySessionId.isEmpty( ) )
            header.setValue( TR_RPC_SESSION_ID_HEADER, mySessionId );
        QBuffer * reqbuf = new QBuffer;
        reqbuf->setData( QByteArray( request ) );
        myHttp.request( header, reqbuf, &myBuffer );
#ifdef DEBUG_HTTP
        std::cerr << "sending " << qPrintable(header.toString()) << "\nBody:\n" << request << std::endl;
#endif
    }
}

void
Session :: onRequestStarted( int id )
{
    Q_UNUSED( id );

    assert( myBuffer.atEnd( ) );
}

void
Session :: onRequestFinished( int id, bool error )
{
    Q_UNUSED( id );
    QIODevice * sourceDevice = myHttp.currentSourceDevice( );

    QHttpResponseHeader response = myHttp.lastResponse();

#ifdef DEBUG_HTTP
    std::cerr << "http request " << id << " ended.. response header: "
              << qPrintable( myHttp.lastResponse().toString() )
              << std::endl
              << "json: " << myBuffer.buffer( ).constData( )
              << std::endl;
#endif

    if( ( response.statusCode() == 409 ) && ( myBuffer.buffer().indexOf("invalid session-id") != -1 ) )
    {
        // we got a 409 telling us our session id has expired.
        // update it and resubmit the request.
        mySessionId = response.value( TR_RPC_SESSION_ID_HEADER );
        exec( qobject_cast<QBuffer*>(sourceDevice)->buffer().constData() );
    }
    else if( error )
    {
        std::cerr << "http error: " << qPrintable(myHttp.errorString()) << std::endl;
    }
    else
    {
        const QByteArray& response( myBuffer.buffer( ) );
        const char * json( response.constData( ) );
        int jsonLength( response.size( ) );
        if( jsonLength>0 && json[jsonLength-1] == '\n' ) --jsonLength;
        parseResponse( json, jsonLength );
    }

    myBuffer.buffer( ).clear( );
    myBuffer.reset( );
    assert( myBuffer.bytesAvailable( ) < 1 );
}

void
Session :: parseResponse( const char * json, size_t jsonLength )
{
    tr_benc top;
    const uint8_t * end( 0 );
    const int err( tr_jsonParse( json, jsonLength, &top, &end ) );
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
                    tr_bool isOpen = 0;
                    if( tr_bencDictFindDict( &top, "arguments", &args ) )
                        tr_bencDictFindBool( args, "port-is-open", &isOpen );
                    emit portTested( (bool)isOpen );
                }

                case TAG_ADD_TORRENT:
                    str = "";
                    if( tr_bencDictFindStr( &top, "result", &str ) && strcmp( str, "success" ) ) {
                        QMessageBox * d = new QMessageBox( QMessageBox::Information,
                                                           tr( "Add Torrent" ),
                                                           QString::fromUtf8(str),
                                                           QMessageBox::Close,
                                                           QApplication::activeWindow());
                        QPixmap pixmap; 
                        QIcon icon = QtIconLoader :: icon( "dialog-information" );
                        if( !icon.isNull( ) ) {
                            const int size = QApplication::style()->pixelMetric( QStyle::PM_LargeIconSize );
                            d->setIconPixmap( icon.pixmap( size, size ) );
                        }
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
                tr_bool val;
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
Session :: addTorrent( QString filename )
{
    QFile file( filename );
    file.open( QIODevice::ReadOnly );
    const QByteArray raw( file.readAll( ) );
    file.close( );

    if( !raw.isEmpty( ) )
    {
        int b64len = 0;
        char * b64 = tr_base64_encode( raw.constData(), raw.size(), &b64len );

        tr_benc top, *args;
        tr_bencInitDict( &top, 2 );
        tr_bencDictAddStr( &top, "method", "torrent-add" );
        args = tr_bencDictAddDict( &top, "arguments", 3 );
        tr_bencDictAddStr( args, "download-dir", qPrintable(myPrefs.getString(Prefs::DOWNLOAD_DIR)) );
        tr_bencDictAddRaw( args, "metainfo", b64, b64len  );
        tr_bencDictAddInt( args, "paused", !myPrefs.getBool( Prefs::START ) );
        exec( &top );

        tr_free( b64 );
        tr_bencFree( &top );
    }
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
        url = myUrl;
    else { // local session
        url.setScheme( "http" );
        url.setHost( "localhost" );
        url.setPort( myPrefs.getInt( Prefs::RPC_PORT ) );
    }
    QDesktopServices :: openUrl( url );
}
