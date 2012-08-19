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

#include <QDir>
#include <QFile>

#include <libtransmission/transmission.h>
#include <libtransmission/bencode.h>
#include <libtransmission/json.h>
#include <libtransmission/utils.h>
#include <stdlib.h>
#include "prefs.h"
#include "types.h"
#include "utils.h"

/***
****
***/

Prefs::PrefItem Prefs::myItems[] =
{
    /* gui settings */
    { OPTIONS_PROMPT, "show-options-window", QVariant::Bool },
    { OPEN_DIALOG_FOLDER, "open-dialog-dir", QVariant::String },
    { INHIBIT_HIBERNATION, "inhibit-desktop-hibernation", QVariant::Bool },
    { DIR_WATCH, "watch-dir", QVariant::String },
    { DIR_WATCH_ENABLED, "watch-dir-enabled", QVariant::Bool },
    { SHOW_TRAY_ICON, "show-notification-area-icon", QVariant::Bool },
    { SHOW_DESKTOP_NOTIFICATION, "show-desktop-notification", QVariant::Bool },
    { ASKQUIT, "prompt-before-exit", QVariant::Bool },
    { SORT_MODE, "sort-mode", TrTypes::SortModeType },
    { SORT_REVERSED, "sort-reversed", QVariant::Bool },
    { COMPACT_VIEW, "compact-view", QVariant::Bool },
    { FILTERBAR, "show-filterbar", QVariant::Bool },
    { STATUSBAR, "show-statusbar", QVariant::Bool },
    { STATUSBAR_STATS, "statusbar-stats", QVariant::String },
    { SHOW_TRACKER_SCRAPES, "show-extra-peer-details", QVariant::Bool },
    { SHOW_BACKUP_TRACKERS, "show-backup-trackers", QVariant::Bool },
    { TOOLBAR, "show-toolbar" , QVariant::Bool },
    { BLOCKLIST_DATE, "blocklist-date", QVariant::DateTime },
    { BLOCKLIST_UPDATES_ENABLED, "blocklist-updates-enabled" , QVariant::Bool },
    { MAIN_WINDOW_LAYOUT_ORDER, "main-window-layout-order", QVariant::String },
    { MAIN_WINDOW_HEIGHT, "main-window-height", QVariant::Int },
    { MAIN_WINDOW_WIDTH, "main-window-width", QVariant::Int },
    { MAIN_WINDOW_X, "main-window-x", QVariant::Int },
    { MAIN_WINDOW_Y, "main-window-y", QVariant::Int },
    { FILTER_MODE, "filter-mode", TrTypes::FilterModeType },
    { FILTER_TRACKERS, "filter-trackers", QVariant::String },
    { FILTER_TEXT, "filter-text", QVariant::String },
    { SESSION_IS_REMOTE, "remote-session-enabled", QVariant::Bool },
    { SESSION_REMOTE_HOST, "remote-session-host", QVariant::String },
    { SESSION_REMOTE_PORT, "remote-session-port", QVariant::Int },
    { SESSION_REMOTE_AUTH, "remote-session-requres-authentication", QVariant::Bool },
    { SESSION_REMOTE_USERNAME, "remote-session-username", QVariant::String },
    { SESSION_REMOTE_PASSWORD, "remote-session-password", QVariant::String },
    { USER_HAS_GIVEN_INFORMED_CONSENT, "user-has-given-informed-consent", QVariant::Bool },

    /* libtransmission settings */
    { ALT_SPEED_LIMIT_UP, TR_PREFS_KEY_ALT_SPEED_UP_KBps, QVariant::Int },
    { ALT_SPEED_LIMIT_DOWN, TR_PREFS_KEY_ALT_SPEED_DOWN_KBps, QVariant::Int },
    { ALT_SPEED_LIMIT_ENABLED, TR_PREFS_KEY_ALT_SPEED_ENABLED, QVariant::Bool },
    { ALT_SPEED_LIMIT_TIME_BEGIN, TR_PREFS_KEY_ALT_SPEED_TIME_BEGIN, QVariant::Int },
    { ALT_SPEED_LIMIT_TIME_END, TR_PREFS_KEY_ALT_SPEED_TIME_END, QVariant::Int },
    { ALT_SPEED_LIMIT_TIME_ENABLED, TR_PREFS_KEY_ALT_SPEED_TIME_ENABLED, QVariant::Bool },
    { ALT_SPEED_LIMIT_TIME_DAY, TR_PREFS_KEY_ALT_SPEED_TIME_DAY, QVariant::Int },
    { BLOCKLIST_ENABLED, TR_PREFS_KEY_BLOCKLIST_ENABLED, QVariant::Bool },
    { BLOCKLIST_URL, TR_PREFS_KEY_BLOCKLIST_URL, QVariant::String },
    { DSPEED, TR_PREFS_KEY_DSPEED_KBps, QVariant::Int },
    { DSPEED_ENABLED, TR_PREFS_KEY_DSPEED_ENABLED, QVariant::Bool },
    { DOWNLOAD_DIR, TR_PREFS_KEY_DOWNLOAD_DIR, QVariant::String },
    { DOWNLOAD_QUEUE_ENABLED, TR_PREFS_KEY_DOWNLOAD_QUEUE_ENABLED, QVariant::Bool },
    { DOWNLOAD_QUEUE_SIZE, TR_PREFS_KEY_DOWNLOAD_QUEUE_SIZE, QVariant::Int },
    { ENCRYPTION, TR_PREFS_KEY_ENCRYPTION, QVariant::Int },
    { IDLE_LIMIT, TR_PREFS_KEY_IDLE_LIMIT, QVariant::Int },
    { IDLE_LIMIT_ENABLED, TR_PREFS_KEY_IDLE_LIMIT_ENABLED, QVariant::Bool },
    { INCOMPLETE_DIR, TR_PREFS_KEY_INCOMPLETE_DIR, QVariant::String },
    { INCOMPLETE_DIR_ENABLED, TR_PREFS_KEY_INCOMPLETE_DIR_ENABLED, QVariant::Bool },
    { MSGLEVEL, TR_PREFS_KEY_MSGLEVEL, QVariant::Int },
    { PEER_LIMIT_GLOBAL, TR_PREFS_KEY_PEER_LIMIT_GLOBAL, QVariant::Int },
    { PEER_LIMIT_TORRENT, TR_PREFS_KEY_PEER_LIMIT_TORRENT, QVariant::Int },
    { PEER_PORT, TR_PREFS_KEY_PEER_PORT, QVariant::Int },
    { PEER_PORT_RANDOM_ON_START, TR_PREFS_KEY_PEER_PORT_RANDOM_ON_START, QVariant::Bool },
    { PEER_PORT_RANDOM_LOW, TR_PREFS_KEY_PEER_PORT_RANDOM_LOW, QVariant::Int },
    { PEER_PORT_RANDOM_HIGH, TR_PREFS_KEY_PEER_PORT_RANDOM_HIGH, QVariant::Int },
    { QUEUE_STALLED_MINUTES, TR_PREFS_KEY_QUEUE_STALLED_MINUTES, QVariant::Int },
    { SCRIPT_TORRENT_DONE_ENABLED, TR_PREFS_KEY_SCRIPT_TORRENT_DONE_ENABLED, QVariant::Bool },
    { SCRIPT_TORRENT_DONE_FILENAME, TR_PREFS_KEY_SCRIPT_TORRENT_DONE_FILENAME, QVariant::String },
    { SOCKET_TOS, TR_PREFS_KEY_PEER_SOCKET_TOS, QVariant::Int },
    { START, TR_PREFS_KEY_START, QVariant::Bool },
    { TRASH_ORIGINAL, TR_PREFS_KEY_TRASH_ORIGINAL, QVariant::Bool },
    { PEX_ENABLED, TR_PREFS_KEY_PEX_ENABLED, QVariant::Bool },
    { DHT_ENABLED, TR_PREFS_KEY_DHT_ENABLED, QVariant::Bool },
    { UTP_ENABLED, TR_PREFS_KEY_UTP_ENABLED, QVariant::Bool },
    { LPD_ENABLED, TR_PREFS_KEY_LPD_ENABLED, QVariant::Bool },
    { PORT_FORWARDING, TR_PREFS_KEY_PORT_FORWARDING, QVariant::Bool },
    { PREALLOCATION, TR_PREFS_KEY_PREALLOCATION, QVariant::Int },
    { RATIO, TR_PREFS_KEY_RATIO, QVariant::Double },
    { RATIO_ENABLED, TR_PREFS_KEY_RATIO_ENABLED, QVariant::Bool },
    { RENAME_PARTIAL_FILES, TR_PREFS_KEY_RENAME_PARTIAL_FILES, QVariant::Bool },
    { RPC_AUTH_REQUIRED, TR_PREFS_KEY_RPC_AUTH_REQUIRED, QVariant::Bool },
    { RPC_ENABLED, TR_PREFS_KEY_RPC_ENABLED, QVariant::Bool },
    { RPC_PASSWORD, TR_PREFS_KEY_RPC_PASSWORD, QVariant::String },
    { RPC_PORT, TR_PREFS_KEY_RPC_PORT, QVariant::Int },
    { RPC_USERNAME, TR_PREFS_KEY_RPC_USERNAME, QVariant::String },
    { RPC_WHITELIST_ENABLED, TR_PREFS_KEY_RPC_WHITELIST_ENABLED, QVariant::Bool },
    { RPC_WHITELIST, TR_PREFS_KEY_RPC_WHITELIST, QVariant::String },
    { USPEED_ENABLED, TR_PREFS_KEY_USPEED_ENABLED, QVariant::Bool },
    { USPEED, TR_PREFS_KEY_USPEED_KBps, QVariant::Int },
    { UPLOAD_SLOTS_PER_TORRENT, TR_PREFS_KEY_UPLOAD_SLOTS_PER_TORRENT, QVariant::Int }
};

/***
****
***/

Prefs :: Prefs( const char * configDir ):
    myConfigDir( QString::fromUtf8( configDir ) )
{
    assert( sizeof(myItems) / sizeof(myItems[0]) == PREFS_COUNT );
    for( int i=0; i<PREFS_COUNT; ++i )
        assert( myItems[i].id == i );

    // these are the prefs that don't get saved to settings.json
    // when the application exits.
    myTemporaryPrefs << FILTER_TEXT;

    tr_benc top;
    tr_bencInitDict( &top, 0 );
    initDefaults( &top );
    tr_sessionLoadSettings( &top, configDir, NULL );
    for( int i=0; i<PREFS_COUNT; ++i )
    {
        double d;
        bool boolVal;
        int64_t intVal;
        const char * str;
        tr_benc * b( tr_bencDictFind( &top, myItems[i].key ) );

        switch( myItems[i].type )
        {
            case QVariant::Int:
                if( tr_bencGetInt( b, &intVal ) )
                    myValues[i].setValue( qlonglong(intVal) );
                break;
            case TrTypes::SortModeType:
                if( tr_bencGetStr( b, &str ) )
                    myValues[i] = QVariant::fromValue( SortMode( str ) );
                break;
            case TrTypes::FilterModeType:
                if( tr_bencGetStr( b, &str ) )
                    myValues[i] = QVariant::fromValue( FilterMode( str ) );
                break;
            case QVariant::String:
                if( tr_bencGetStr( b, &str ) )
                    myValues[i].setValue( QString::fromUtf8( str ) );
                break;
            case QVariant::Bool:
                if( tr_bencGetBool( b, &boolVal ) )
                    myValues[i].setValue( bool(boolVal) );
                break;
            case QVariant::Double:
                if( tr_bencGetReal( b, &d ) )
                    myValues[i].setValue( d );
                break;
            case QVariant::DateTime:
                if( tr_bencGetInt( b, &intVal ) )
                    myValues[i].setValue( QDateTime :: fromTime_t( intVal ) );
                break;
            default:
                assert( "unhandled type" && 0 );
                break;
        }
    }

    tr_bencFree( &top );
}

Prefs :: ~Prefs( )
{
    tr_benc top;

    /* load in the existing preferences file */
    QFile file( QDir( myConfigDir ).absoluteFilePath( "settings.json" ) );
    file.open( QIODevice::ReadOnly | QIODevice::Text );
    const QByteArray oldPrefs = file.readAll( );
    file.close( );
    if( tr_jsonParse( "settings.json", oldPrefs.data(), oldPrefs.length(), &top, NULL ) )
        tr_bencInitDict( &top, PREFS_COUNT );

    /* merge our own settings with the ones already in the file */
    for( int i=0; i<PREFS_COUNT; ++i )
    {
        if( myTemporaryPrefs.contains( i ) )
            continue;

        const char * key = myItems[i].key;
        const QVariant& val = myValues[i];

        switch( myItems[i].type )
        {
            case QVariant::Int:
                tr_bencDictAddInt( &top, key, val.toInt() );
                break;
            case TrTypes::SortModeType:
                tr_bencDictAddStr( &top, key, val.value<SortMode>().name().toUtf8().constData() );
                break;
            case TrTypes::FilterModeType:
                tr_bencDictAddStr( &top, key, val.value<FilterMode>().name().toUtf8().constData() );
                break;
            case QVariant::String:
                {   const char * s = val.toByteArray().constData();
                    if ( Utils::isValidUtf8( s ) )
                        tr_bencDictAddStr( &top, key, s );
                    else
                        tr_bencDictAddStr( &top, key, val.toString().toUtf8().constData() );
                }
                break;
            case QVariant::Bool:
                tr_bencDictAddBool( &top, key, val.toBool() );
                break;
            case QVariant::Double:
                tr_bencDictAddReal( &top, key, val.toDouble() );
                break;
            case QVariant::DateTime:
                tr_bencDictAddInt( &top, key, val.toDateTime().toTime_t() );
                break;
            default:
                assert( "unhandled type" && 0 );
                break;
        }
    }

    /* write back out the serialized preferences */
    tr_bencToFile( &top, TR_FMT_JSON, file.fileName().toUtf8().constData() );
    tr_bencFree( &top );
}

/**
 * This is where we initialize the preferences file with the default values.
 * If you add a new preferences key, you /must/ add a default value here.
 */
void
Prefs :: initDefaults( tr_benc * d )
{
    tr_bencDictAddStr ( d, keyStr(DIR_WATCH), tr_getDefaultDownloadDir( ) );
    tr_bencDictAddBool( d, keyStr(DIR_WATCH_ENABLED), false );
    tr_bencDictAddBool( d, keyStr(INHIBIT_HIBERNATION), false );
    tr_bencDictAddInt ( d, keyStr(BLOCKLIST_DATE), 0 );
    tr_bencDictAddBool( d, keyStr(BLOCKLIST_UPDATES_ENABLED), true );
    tr_bencDictAddStr ( d, keyStr(OPEN_DIALOG_FOLDER), QDir::home().absolutePath().toUtf8() );
    tr_bencDictAddInt ( d, keyStr(SHOW_TRACKER_SCRAPES), false );
    tr_bencDictAddBool( d, keyStr(TOOLBAR), true );
    tr_bencDictAddBool( d, keyStr(FILTERBAR), true );
    tr_bencDictAddBool( d, keyStr(STATUSBAR), true );
    tr_bencDictAddBool( d, keyStr(SHOW_TRAY_ICON), false );
    tr_bencDictAddBool( d, keyStr(SHOW_DESKTOP_NOTIFICATION), true );
    tr_bencDictAddStr ( d, keyStr(STATUSBAR_STATS), "total-ratio" );
    tr_bencDictAddBool( d, keyStr(SHOW_TRACKER_SCRAPES), false );
    tr_bencDictAddBool( d, keyStr(SHOW_BACKUP_TRACKERS), false );
    tr_bencDictAddBool( d, keyStr(OPTIONS_PROMPT), true );
    tr_bencDictAddInt ( d, keyStr(MAIN_WINDOW_HEIGHT), 500 );
    tr_bencDictAddInt ( d, keyStr(MAIN_WINDOW_WIDTH), 300 );
    tr_bencDictAddInt ( d, keyStr(MAIN_WINDOW_X), 50 );
    tr_bencDictAddInt ( d, keyStr(MAIN_WINDOW_Y), 50 );
    tr_bencDictAddStr ( d, keyStr(FILTER_MODE), "all" );
    tr_bencDictAddStr ( d, keyStr(MAIN_WINDOW_LAYOUT_ORDER), "menu,toolbar,filter,list,statusbar" );
    tr_bencDictAddStr ( d, keyStr(DOWNLOAD_DIR), tr_getDefaultDownloadDir( ) );
    tr_bencDictAddBool( d, keyStr(ASKQUIT), true );
    tr_bencDictAddStr ( d, keyStr(SORT_MODE), "sort-by-name" );
    tr_bencDictAddBool( d, keyStr(SORT_REVERSED), false );
    tr_bencDictAddBool( d, keyStr(COMPACT_VIEW), false );
    tr_bencDictAddStr ( d, keyStr(SESSION_REMOTE_HOST), "localhost" );
    tr_bencDictAddInt ( d, keyStr(SESSION_REMOTE_PORT), atoi(TR_DEFAULT_RPC_PORT_STR) );
    tr_bencDictAddBool( d, keyStr(SESSION_IS_REMOTE), false );
    tr_bencDictAddBool( d, keyStr(SESSION_REMOTE_AUTH), false );
    tr_bencDictAddBool( d, keyStr(USER_HAS_GIVEN_INFORMED_CONSENT), false );
}

/***
****
***/

bool
Prefs :: getBool( int key ) const
{
    assert( myItems[key].type == QVariant::Bool );
    return myValues[key].toBool( );
}

QString
Prefs :: getString( int key ) const
{
    assert( myItems[key].type == QVariant::String );
    QByteArray b = myValues[key].toByteArray();
    if ( Utils::isValidUtf8( b.constData() ) )
       myValues[key].setValue( QString::fromUtf8( b.constData() ) );
    return myValues[key].toString();
}

int
Prefs :: getInt( int key ) const
{
    assert( myItems[key].type == QVariant::Int );
    return myValues[key].toInt( );
}

double
Prefs :: getDouble( int key ) const
{
    assert( myItems[key].type == QVariant::Double );
    return myValues[key].toDouble( );
}

QDateTime
Prefs :: getDateTime( int key ) const
{
    assert( myItems[key].type == QVariant::DateTime );
    return myValues[key].toDateTime( );
}

/***
****
***/

void
Prefs :: toggleBool( int key )
{
    set( key, !getBool( key ) );
}
