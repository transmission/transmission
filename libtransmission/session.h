/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2008 Transmission authors and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *****************************************************************************/

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


typedef enum { TR_NET_OK, TR_NET_ERROR, TR_NET_WAIT } tr_tristate_t;

uint8_t*       tr_peerIdNew( void );

const uint8_t* tr_getPeerId( void );

struct tr_metainfo_lookup
{
    char    hashString[2 * SHA_DIGEST_LENGTH + 1];
    char *  filename;
};

struct tr_address;
struct tr_bandwidth;

struct tr_session
{
    tr_bool                      isPortSet;
    tr_bool                      isPortRandom;
    tr_bool                      isPexEnabled;
    tr_bool                      isBlocklistEnabled;
    tr_bool                      isProxyEnabled;
    tr_bool                      isProxyAuthEnabled;
    tr_bool                      isClosed;
    tr_bool                      isWaiting;
    tr_bool                      useLazyBitfield;
    tr_bool                      isRatioLimited;

    
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

    struct tr_stats_handle *     sessionStats;
    struct tr_tracker_handle *   tracker;

    struct tr_metainfo_lookup *  metainfoLookup;
    int                          metainfoLookupCount;

    struct event               * altTimer;

    /* the size of the output buffer for peer connections */
    int so_sndbuf;

    /* the size of the input buffer for peer connections */
    int so_rcvbuf;

    /* monitors the "global pool" speeds */
    struct tr_bandwidth        * bandwidth;

    double                       desiredRatio;
};

tr_bool      tr_sessionGetActiveSpeedLimit( const tr_session  * session,
                                            tr_direction        dir,
                                            int               * setme );

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

enum
{
    SESSION_MAGIC_NUMBER = 3845
};

static inline tr_bool tr_isSession( const tr_session * session )
{
    return ( session != NULL ) && ( session->magicNumber == SESSION_MAGIC_NUMBER );
}

#endif
