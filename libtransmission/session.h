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

enum
{
    /* How frequently to reallocate peer bandwidth. */
    BANDWIDTH_PULSES_PER_SECOND = 4,

    /* HOw many pulses to remember for averaging the current speed */
    BANDWIDTH_PULSE_HISTORY = ( BANDWIDTH_PULSES_PER_SECOND * 2 )
};


typedef enum { TR_NET_OK, TR_NET_ERROR, TR_NET_WAIT } tr_tristate_t;

uint8_t*       tr_peerIdNew( void );

const uint8_t* tr_getPeerId( void );

struct tr_metainfo_lookup
{
    char    hashString[2 * SHA_DIGEST_LENGTH + 1];
    char *  filename;
};

struct tr_handle
{
    unsigned int                 isPortSet          : 1;
    unsigned int                 isPexEnabled       : 1;
    unsigned int                 isBlocklistEnabled : 1;
    unsigned int                 isProxyEnabled     : 1;
    unsigned int                 isProxyAuthEnabled : 1;
    unsigned int                 isClosed           : 1;
    unsigned int                 useUploadLimit     : 1;
    unsigned int                 useDownloadLimit   : 1;
    unsigned int                 useLazyBitfield    : 1;

    tr_encryption_mode           encryptionMode;

    struct tr_event_handle *     events;

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

    int                          uploadLimit;
    int                          downloadLimit;

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
};

const char * tr_sessionFindTorrentFile( const tr_session * session,
                                        const char *       hashString );

void         tr_sessionSetTorrentFile( tr_session * session,
                                       const char * hashString,
                                       const char * filename );

struct in_addr;

int          tr_sessionIsAddressBlocked( const tr_session *     session,
                                         const struct in_addr * addr );


void         tr_globalLock( tr_session * );

void         tr_globalUnlock( tr_session * );

int          tr_globalIsLocked( const tr_session * );

#endif
