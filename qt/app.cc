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
#include <ctime>
#include <iostream>

#include <QIcon>
#include <QLibraryInfo>
#include <QRect>
#include <QTranslator>

#include <libtransmission/transmission.h>
#include <libtransmission/tr-getopt.h>
#include <libtransmission/version.h>

#include "app.h"
#include "mainwin.h"
#include "options.h"
#include "prefs.h"
#include "torrent-model.h"
#include "session.h"
#include "utils.h"
#include "watchdir.h"

namespace
{
    const char * MY_NAME( "transmission" );

    const tr_option opts[] =
    {
        { 'g', "config-dir", "Where to look for configuration files", "g", 1, "<path>" },
        { 'm', "minimized",  "Start minimized in system tray", "m", 0, NULL },
        { 'p', "paused",  "Pause all torrents on sartup", "p", 0, NULL },
        { 'r', "remote",  "Remotely control a pre-existing session", "r", 1, "<URL>" },
        { 'v', "version", "Show version number and exit", "v", 0, NULL },
        { 0, NULL, NULL, NULL, 0, NULL }
    };

    const char*
    getUsage( void )
    {
        return "Transmission " LONG_VERSION_STRING "\n"
               "http://www.transmissionbt.com/\n"
               "A fast and easy BitTorrent client";
    }

    void
    showUsage( void )
    {
        tr_getopt_usage( MY_NAME, getUsage( ), opts );
        exit( 0 );
    }

    enum
    {
        STATS_REFRESH_INTERVAL_MSEC = 3000,
        SESSION_REFRESH_INTERVAL_MSEC = 3000,
        MODEL_REFRESH_INTERVAL_MSEC = 3000
    };
}

MyApp :: MyApp( int& argc, char ** argv ):
    QApplication( argc, argv ),
    myLastFullUpdateTime( 0 )
{
    setApplicationName( MY_NAME );

    // install the qt translator
    QTranslator * t = new QTranslator( );
    t->load( "qt_" + QLocale::system().name(), QLibraryInfo::location(QLibraryInfo::TranslationsPath));
    installTranslator( t );

    // install the transmission translator
    t = new QTranslator( );
    t->load( QString(MY_NAME) + "_" + QLocale::system().name() );
    installTranslator( t );

    // set the default icon
    QIcon icon;
    icon.addPixmap( QPixmap( ":/icons/transmission-16.png" ) );
    icon.addPixmap( QPixmap( ":/icons/transmission-22.png" ) );
    icon.addPixmap( QPixmap( ":/icons/transmission-24.png" ) );
    icon.addPixmap( QPixmap( ":/icons/transmission-32.png" ) );
    icon.addPixmap( QPixmap( ":/icons/transmission-48.png" ) );
    setWindowIcon( icon );

    // parse the command-line arguments
    int c;
    bool paused = false;
    bool minimized = false;
    const char * optarg;
    const char * configDir = 0;
    const char * url = 0;
    while( ( c = tr_getopt( getUsage( ), argc, (const char**)argv, opts, &optarg ) ) ) {
        switch( c ) {
            case 'g': configDir = optarg; break;
            case 'm': minimized = true; break;
            case 'p': paused = true; break;
            case 'r': url = optarg; break;
            case 'v':        Utils::toStderr( QObject::tr( "transmission %1" ).arg( LONG_VERSION_STRING ) ); exit( 0 ); break;
            case TR_OPT_ERR: Utils::toStderr( QObject::tr( "Invalid option" ) ); showUsage( ); break;
            default:         Utils::toStderr( QObject::tr( "Got opt %1" ).arg((int)c) ); showUsage( ); break;
        }
    }

    // set the fallback config dir
    if( configDir == 0 )
        configDir = tr_getDefaultConfigDir( MY_NAME );

    myPrefs = new Prefs ( configDir );
    mySession = new Session( configDir, *myPrefs, url, paused );
    myModel = new TorrentModel( *myPrefs );
    myWindow = new TrMainWindow( *mySession, *myPrefs, *myModel, minimized );
    myWatchDir = new WatchDir( *myModel );

    /* when the session gets torrent info, update the model */
    connect( mySession, SIGNAL(torrentsUpdated(tr_benc*,bool)), myModel, SLOT(updateTorrents(tr_benc*,bool)) );
    connect( mySession, SIGNAL(torrentsUpdated(tr_benc*,bool)), myWindow, SLOT(refreshActionSensitivity()) );
    connect( mySession, SIGNAL(torrentsRemoved(tr_benc*)), myModel, SLOT(removeTorrents(tr_benc*)) );
    /* when the model sees a torrent for the first time, ask the session for full info on it */
    connect( myModel, SIGNAL(torrentsAdded(QSet<int>)), mySession, SLOT(initTorrents(QSet<int>)) );

    mySession->initTorrents( );
    mySession->refreshSessionStats( );

    /* when torrents are added to the watch directory, tell the session */
    connect( myWatchDir, SIGNAL(torrentFileAdded(QString)), this, SLOT(addTorrent(QString)) );

    /* init from preferences */
    QList<int> initKeys;
    initKeys << Prefs::DIR_WATCH;
    foreach( int key, initKeys )
        refreshPref( key );
    connect( myPrefs, SIGNAL(changed(int)), this, SLOT(refreshPref(const int)) );

    QTimer * timer = &myModelTimer;
    connect( timer, SIGNAL(timeout()), this, SLOT(refreshTorrents()) );
    timer->setSingleShot( false );
    timer->setInterval( MODEL_REFRESH_INTERVAL_MSEC );
    timer->start( );

    timer = &myStatsTimer;
    connect( timer, SIGNAL(timeout()), mySession, SLOT(refreshSessionStats()) );
    timer->setSingleShot( false );
    timer->setInterval( STATS_REFRESH_INTERVAL_MSEC );
    timer->start( );

    timer = &mySessionTimer;
    connect( timer, SIGNAL(timeout()), mySession, SLOT(refreshSessionInfo()) );
    timer->setSingleShot( false );
    timer->setInterval( SESSION_REFRESH_INTERVAL_MSEC );
    timer->start( );

    maybeUpdateBlocklist( );
}

MyApp :: ~MyApp( )
{
    const QRect mainwinRect( myWindow->geometry( ) );
    delete myWatchDir;
    delete myWindow;
    delete myModel;
    delete mySession;

    myPrefs->set( Prefs :: MAIN_WINDOW_HEIGHT, std::max( 100, mainwinRect.height( ) ) );
    myPrefs->set( Prefs :: MAIN_WINDOW_WIDTH, std::max( 100, mainwinRect.width( ) ) );
    myPrefs->set( Prefs :: MAIN_WINDOW_X, mainwinRect.x( ) );
    myPrefs->set( Prefs :: MAIN_WINDOW_Y, mainwinRect.y( ) );
    delete myPrefs;
}

/***
****
***/

void
MyApp :: refreshPref( int key )
{
    switch( key )
    {
        case Prefs :: BLOCKLIST_UPDATES_ENABLED:
            maybeUpdateBlocklist( );
            break;

        case Prefs :: DIR_WATCH:
        case Prefs :: DIR_WATCH_ENABLED: {
            const QString path( myPrefs->getString( Prefs::DIR_WATCH ) );
            const bool isEnabled( myPrefs->getBool( Prefs::DIR_WATCH_ENABLED ) );
            myWatchDir->setPath( path, isEnabled );
            break;
        }

        default:
            break;
    }
}

void
MyApp :: maybeUpdateBlocklist( )
{
    if( !myPrefs->getBool( Prefs :: BLOCKLIST_UPDATES_ENABLED ) )
        return;

     const QDateTime lastUpdatedAt = myPrefs->getDateTime( Prefs :: BLOCKLIST_DATE );
     const QDateTime nextUpdateAt = lastUpdatedAt.addDays( 7 );
     const QDateTime now = QDateTime::currentDateTime( );
     if( now < nextUpdateAt )
     {
         mySession->updateBlocklist( );
         myPrefs->set( Prefs :: BLOCKLIST_DATE, now );
     }
}

void
MyApp :: refreshTorrents( )
{
    // usually we just poll the torrents that have shown recent activity,
    // but we also periodically ask for updates on the others to ensure
    // nothing's falling through the cracks.
    const time_t now = time( NULL );
    if( myLastFullUpdateTime + 60 >= now )
        mySession->refreshActiveTorrents( );
    else {
        myLastFullUpdateTime = now;
        mySession->refreshAllTorrents( );
    }
}

void
MyApp :: addTorrent( const QString& filename )
{
    if( myPrefs->getBool( Prefs :: OPTIONS_PROMPT ) ) {
        Options * o = new Options( *mySession, *myPrefs, filename, myWindow );
        o->show( );
        QApplication :: alert( o );
    } else {
        mySession->addTorrent( filename );
        QApplication :: alert ( myWindow );
    }
}


/***
****
***/

int
main( int argc, char * argv[] )
{
    MyApp app( argc, argv );
    return app.exec( );
}
