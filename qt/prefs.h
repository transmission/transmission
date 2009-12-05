/*
 * This file Copyright (C) 2009 Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#ifndef QTR_PREFS_H
#define QTR_PREFS_H

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QVariant>

#include "filters.h"

extern "C"
{
    struct tr_benc;
}

class Prefs: public QObject
{
        Q_OBJECT;

    public:

        enum
        {
            /* client prefs */
            OPTIONS_PROMPT,
            OPEN_DIALOG_FOLDER,
            INHIBIT_HIBERNATION,
            DIR_WATCH,
            DIR_WATCH_ENABLED,
            SHOW_TRAY_ICON,
            SHOW_DESKTOP_NOTIFICATION,
            START,
            TRASH_ORIGINAL,
            ASKQUIT,
            SORT_MODE,
            SORT_REVERSED,
            MINIMAL_VIEW,
            FILTERBAR,
            STATUSBAR,
            STATUSBAR_STATS,
            TOOLBAR,
            BLOCKLIST_DATE,
            BLOCKLIST_UPDATES_ENABLED,
            MAIN_WINDOW_LAYOUT_ORDER,
            MAIN_WINDOW_HEIGHT,
            MAIN_WINDOW_WIDTH,
            MAIN_WINDOW_X,
            MAIN_WINDOW_Y,
            FILTER_MODE,
            SESSION_IS_REMOTE,
            SESSION_REMOTE_HOST,
            SESSION_REMOTE_PORT,
            SESSION_REMOTE_AUTH,
            SESSION_REMOTE_USERNAME,
            SESSION_REMOTE_PASSWORD,
            USER_HAS_GIVEN_INFORMED_CONSENT,

            /* core prefs */
            FIRST_CORE_PREF,
            ALT_SPEED_LIMIT_UP = FIRST_CORE_PREF,
            ALT_SPEED_LIMIT_DOWN,
            ALT_SPEED_LIMIT_ENABLED,
            ALT_SPEED_LIMIT_TIME_BEGIN,
            ALT_SPEED_LIMIT_TIME_END,
            ALT_SPEED_LIMIT_TIME_ENABLED,
            ALT_SPEED_LIMIT_TIME_DAY,
            BLOCKLIST_ENABLED,
            DSPEED,
            DSPEED_ENABLED,
            DOWNLOAD_DIR,
            ENCRYPTION,
            INCOMPLETE_DIR,
            INCOMPLETE_DIR_ENABLED,
            LAZY_BITFIELD,
            MSGLEVEL,
            OPEN_FILE_LIMIT,
            PEER_LIMIT_GLOBAL,
            PEER_LIMIT_TORRENT,
            PEER_PORT,
            PEER_PORT_RANDOM_ON_START,
            PEER_PORT_RANDOM_LOW,
            PEER_PORT_RANDOM_HIGH,
            SOCKET_TOS,
            PEX_ENABLED,
            DHT_ENABLED,
            PORT_FORWARDING,
            PROXY_AUTH_ENABLED,
            PREALLOCATION,
            PROXY_ENABLED,
            PROXY_PASSWORD,
            PROXY_PORT,
            PROXY,
            PROXY_TYPE,
            PROXY_USERNAME,
            RATIO,
            RATIO_ENABLED,
            RPC_AUTH_REQUIRED,
            RPC_ENABLED,
            RPC_PASSWORD,
            RPC_PORT,
            RPC_USERNAME,
            RPC_WHITELIST_ENABLED,
            RPC_WHITELIST,
            USPEED_ENABLED,
            USPEED,
            UPLOAD_SLOTS_PER_TORRENT,
            LAST_CORE_PREF = UPLOAD_SLOTS_PER_TORRENT,

            PREFS_COUNT
        };

    private:

        struct PrefItem {
            int id;
            const char * key;
            int type;
        };

        static PrefItem myItems[];

    private:
        QString myConfigDir;
        QVariant myValues[PREFS_COUNT];
        void initDefaults( struct tr_benc* );

    public:
        bool isCore( int key ) const { return FIRST_CORE_PREF<=key && key<=LAST_CORE_PREF; }
        bool isClient( int key ) const { return !isCore( key ); }
        const char * keyStr( int i ) const { return myItems[i].key; }
        int type( int i ) const { return myItems[i].type; }
        const QVariant& variant( int i ) const { return myValues[i]; }

        Prefs( const char * configDir );
        ~Prefs( );

        int getInt( int key ) const;
        bool getBool( int key) const;
        QString getString( int key ) const;
        double getDouble( int key) const;
        QDateTime getDateTime( int key ) const;

        template<typename T> T get( int key ) const {
            return myValues[key].value<T>();
        }

        void set( int key, char * value ) { set( key, QString::fromUtf8(value) ); }
        void set( int key, const char * value ) { set( key, QString::fromUtf8(value) ); }

        template<typename T> void set( int key, const T& value ) {
            QVariant& v( myValues[key] );
            const QVariant tmp = QVariant::fromValue(value);
            if( v.isNull() || (v!=tmp) ) {
                v = tmp;
                emit changed( key );
            }
        }

        void toggleBool( int key );

     signals:
        void changed( int key );
};

#endif
