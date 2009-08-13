/*
 * This file Copyright (C) 2008-2009 Charles Kerr <charles@transmissionbt.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#ifndef __TRANSMISSION__
 #error only libtransmission should #include this header.
#endif

#ifndef TR_INTERNAL_H
#define TR_INTERNAL_H 1

#define TR_NAME "Transmission"

#ifndef UNUSED
 #ifdef __GNUC__
  #define UNUSED __attribute__ ( ( unused ) )
 #else
  #define UNUSED
 #endif
#endif

#include "bencode.h"

typedef enum { TR_NET_OK, TR_NET_ERROR, TR_NET_WAIT } tr_tristate_t;

uint8_t*       tr_peerIdNew( void );

const uint8_t* tr_getPeerId( void );

struct tr_address;
struct tr_bandwidth;
struct tr_bindsockets;

struct tr_session
{
    tr_bool                      isPortRandom;
    tr_bool                      isPexEnabled;
    tr_bool                      isDHTEnabled;
    tr_bool                      isBlocklistEnabled;
    tr_bool                      isProxyEnabled;
    tr_bool                      isProxyAuthEnabled;
    tr_bool                      isClosed;
    tr_bool                      isWaiting;
    tr_bool                      useLazyBitfield;
    tr_bool                      isRatioLimited;

    tr_benc                      removedTorrents;

    int                          umask;

    int                          speedLimit[2];
    tr_bool                      speedLimitEnabled[2];

    int                          altSpeed[2];
    tr_bool                      altSpeedEnabled;

    int                          altSpeedTimeBegin;
    int                          altSpeedTimeEnd;
    tr_sched_day                 altSpeedTimeDay;
    tr_bool                      altSpeedTimeEnabled;
    tr_bool                      altSpeedChangedByUser;

    tr_altSpeedFunc            * altCallback;
    void                       * altCallbackUserData;


    int                          magicNumber;

    tr_encryption_mode           encryptionMode;

    tr_preallocation_mode        preallocationMode;

    struct tr_event_handle *     events;

    uint16_t                     peerLimitPerTorrent;
    uint16_t                     openFileLimit;

    int                          uploadSlotsPerTorrent;

    tr_port                      peerPort;
    tr_port                      randomPortLow;
    tr_port                      randomPortHigh;

    int                          proxyPort;
    int                          peerSocketTOS;

    int                          torrentCount;
    tr_torrent *                 torrentList;

    char *                       tag;
    char *                       configDir;
    char *                       downloadDir;
    char *                       resumeDir;
    char *                       torrentDir;

    tr_proxy_type                proxyType;
    char *                       proxy;
    char *                       proxyUsername;
    char *                       proxyPassword;

    struct tr_list *             blocklists;
    struct tr_peerMgr *          peerMgr;
    struct tr_shared *           shared;

    struct tr_lock *             lock;

    struct tr_web *              web;

    struct tr_rpc_server *       rpcServer;
    tr_rpc_func                  rpc_func;
    void *                       rpc_func_user_data;

    struct tr_stats_handle     * sessionStats;
    struct tr_tracker_handle   * tracker;

    tr_benc                    * metainfoLookup;

    struct event               * altTimer;
    struct event               * saveTimer;

    /* the size of the output buffer for peer connections */
    int so_sndbuf;

    /* the size of the input buffer for peer connections */
    int so_rcvbuf;

    /* monitors the "global pool" speeds */
    struct tr_bandwidth        * bandwidth;

    double                       desiredRatio;

    struct tr_bindinfo         * public_ipv4;
    struct tr_bindinfo         * public_ipv6;
};

tr_bool      tr_sessionAllowsDHT( const tr_session * session );

const char * tr_sessionFindTorrentFile( const tr_session * session,
                                        const char *       hashString );

void         tr_sessionSetTorrentFile( tr_session * session,
                                       const char * hashString,
                                       const char * filename );

tr_bool      tr_sessionIsAddressBlocked( const tr_session        * session,
                                         const struct tr_address * addr );

void         tr_globalLock( tr_session * );

void         tr_globalUnlock( tr_session * );

tr_bool      tr_globalIsLocked( const tr_session * );

const struct tr_address*  tr_sessionGetPublicAddress( const tr_session *, int tr_af_type );

struct tr_bindsockets * tr_sessionGetBindSockets( tr_session * );

enum
{
    SESSION_MAGIC_NUMBER = 3845
};

static TR_INLINE tr_bool tr_isSession( const tr_session * session )
{
    return ( session != NULL ) && ( session->magicNumber == SESSION_MAGIC_NUMBER );
}

static TR_INLINE tr_bool tr_isPreallocationMode( tr_preallocation_mode m  )
{
    return ( m == TR_PREALLOCATE_NONE )
        || ( m == TR_PREALLOCATE_SPARSE )
        || ( m == TR_PREALLOCATE_FULL );
}

static TR_INLINE tr_bool tr_isEncryptionMode( tr_encryption_mode m )
{
    return ( m == TR_CLEAR_PREFERRED )
        || ( m == TR_ENCRYPTION_PREFERRED )
        || ( m == TR_ENCRYPTION_REQUIRED );
}

static TR_INLINE tr_bool tr_isPriority( tr_priority_t p )
{
    return ( p == TR_PRI_LOW )
        || ( p == TR_PRI_NORMAL )
        || ( p == TR_PRI_HIGH );
}

#endif
