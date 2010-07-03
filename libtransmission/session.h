/*
 * This file Copyright (C) 2008-2010 Mnemosyne LLC
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
#include "bitfield.h"

typedef enum { TR_NET_OK, TR_NET_ERROR, TR_NET_WAIT } tr_tristate_t;

uint8_t*       tr_peerIdNew( void );

const uint8_t* tr_getPeerId( void );

struct tr_address;
struct tr_announcer;
struct tr_bandwidth;
struct tr_bindsockets;
struct tr_cache;
struct tr_fdInfo;

struct tr_turtle_info
{
    /* TR_UP and TR_DOWN speed limits */
    int speedLimit_Bps[2];

    /* is turtle mode on right now? */
    tr_bool isEnabled;

    /* does turtle mode turn itself on and off at given times? */
    tr_bool isClockEnabled;

    /* when clock mode is on, minutes after midnight to turn on turtle mode */
    int beginMinute;

    /* when clock mode is on, minutes after midnight to turn off turtle mode */
    int endMinute;

    /* only use clock mode on these days of the week */
    tr_sched_day days;

    /* called when isEnabled changes */
    tr_altSpeedFunc * callback;

    /* the callback's user_data argument */
    void * callbackUserData;

    /* the callback's changedByUser argument.
     * indicates whether the change came from the user or from the clock. */
    tr_bool changedByUser;

    /* bitfield of all the minutes in a week.
     * Each bit's value indicates whether the scheduler wants turtle
     * limits on or off at that given minute in the week. */
    tr_bitfield minutes;
};

/** @brief handle to an active libtransmission session */
struct tr_session
{
    tr_bool                      isPortRandom;
    tr_bool                      isPexEnabled;
    tr_bool                      isDHTEnabled;
    tr_bool                      isLPDEnabled;
    tr_bool                      isBlocklistEnabled;
    tr_bool                      isProxyEnabled;
    tr_bool                      isProxyAuthEnabled;
    tr_bool                      isTorrentDoneScriptEnabled;
    tr_bool                      isClosed;
    tr_bool                      useLazyBitfield;
    tr_bool                      isIncompleteFileNamingEnabled;
    tr_bool                      isRatioLimited;
    tr_bool                      isIncompleteDirEnabled;
    tr_bool                      pauseAddedTorrent;
    tr_bool                      deleteSourceTorrent;

    tr_benc                      removedTorrents;

    int                          umask;

    int                          speedLimit_Bps[2];
    tr_bool                      speedLimitEnabled[2];

    struct tr_turtle_info        turtle;

    struct tr_fdInfo           * fdInfo;

    int                          magicNumber;

    tr_encryption_mode           encryptionMode;

    tr_preallocation_mode        preallocationMode;

    struct tr_event_handle *     events;

    uint16_t                     peerLimitPerTorrent;

    int                          uploadSlotsPerTorrent;

    tr_port                      peerPort;
    tr_port                      randomPortLow;
    tr_port                      randomPortHigh;

    int                          proxyPort;
    int                          peerSocketTOS;
    char *                       peer_congestion_algorithm;

    int                          torrentCount;
    tr_torrent *                 torrentList;

    char *                       torrentDoneScript;

    char *                       tag;
    char *                       configDir;
    char *                       downloadDir;
    char *                       resumeDir;
    char *                       torrentDir;
    char *                       incompleteDir;

    tr_proxy_type                proxyType;
    char *                       proxy;
    char *                       proxyUsername;
    char *                       proxyPassword;

    struct tr_list *             blocklists;
    struct tr_peerMgr *          peerMgr;
    struct tr_shared *           shared;

    struct tr_cache *            cache;

    struct tr_lock *             lock;

    struct tr_web *              web;

    struct tr_rpc_server *       rpcServer;
    tr_rpc_func                  rpc_func;
    void *                       rpc_func_user_data;

    struct tr_stats_handle     * sessionStats;

    struct tr_announcer        * announcer;

    tr_benc                    * metainfoLookup;

    struct event               * nowTimer;
    struct event               * saveTimer;

    /* monitors the "global pool" speeds */
    struct tr_bandwidth        * bandwidth;

    double                       desiredRatio;

    struct tr_bindinfo         * public_ipv4;
    struct tr_bindinfo         * public_ipv6;

    /* a page-aligned buffer for use by the libtransmission thread.
     * @see SESSION_BUFFER_SIZE */
    void * buffer;

    tr_bool bufferInUse;
};

tr_bool      tr_sessionAllowsDHT( const tr_session * session );

tr_bool      tr_sessionAllowsLPD( const tr_session * session );

const char * tr_sessionFindTorrentFile( const tr_session * session,
                                        const char *       hashString );

void         tr_sessionSetTorrentFile( tr_session * session,
                                       const char * hashString,
                                       const char * filename );

tr_bool      tr_sessionIsAddressBlocked( const tr_session        * session,
                                         const struct tr_address * addr );

void         tr_sessionLock( tr_session * );

void         tr_sessionUnlock( tr_session * );

tr_bool      tr_sessionIsLocked( const tr_session * );

const struct tr_address*  tr_sessionGetPublicAddress( const tr_session *, int tr_af_type );

struct tr_bindsockets * tr_sessionGetBindSockets( tr_session * );

int tr_sessionCountTorrents( const tr_session * session ); 

enum
{
    SESSION_MAGIC_NUMBER = 3845,

    /* @see tr_session.buffer */
    SESSION_BUFFER_SIZE = (16*1024)
};

void* tr_sessionGetBuffer( tr_session * session );

void tr_sessionReleaseBuffer( tr_session * session );

static inline tr_bool tr_isSession( const tr_session * session )
{
    return ( session != NULL ) && ( session->magicNumber == SESSION_MAGIC_NUMBER );
}

static inline tr_bool tr_isPreallocationMode( tr_preallocation_mode m  )
{
    return ( m == TR_PREALLOCATE_NONE )
        || ( m == TR_PREALLOCATE_SPARSE )
        || ( m == TR_PREALLOCATE_FULL );
}

static inline tr_bool tr_isEncryptionMode( tr_encryption_mode m )
{
    return ( m == TR_CLEAR_PREFERRED )
        || ( m == TR_ENCRYPTION_PREFERRED )
        || ( m == TR_ENCRYPTION_REQUIRED );
}

static inline tr_bool tr_isPriority( tr_priority_t p )
{
    return ( p == TR_PRI_LOW )
        || ( p == TR_PRI_NORMAL )
        || ( p == TR_PRI_HIGH );
}

#endif
