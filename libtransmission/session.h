/*
 * This file Copyright (C) 2008-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
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
  #define UNUSED __attribute__ ((unused))
 #else
  #define UNUSED
 #endif
#endif

#include "bandwidth.h"
#include "bitfield.h"
#include "net.h"
#include "utils.h"
#include "variant.h"

typedef enum { TR_NET_OK, TR_NET_ERROR, TR_NET_WAIT } tr_tristate_t;

typedef enum {
    TR_AUTO_SWITCH_UNUSED,
    TR_AUTO_SWITCH_ON,
    TR_AUTO_SWITCH_OFF,
} tr_auto_switch_state_t;

enum
{
  PEER_ID_LEN = 20
};

void tr_peerIdInit (uint8_t * setme);

struct event_base;
struct evdns_base;

struct tr_address;
struct tr_announcer;
struct tr_announcer_udp;
struct tr_bindsockets;
struct tr_cache;
struct tr_fdInfo;
struct tr_device_info;

struct tr_turtle_info
{
    /* TR_UP and TR_DOWN speed limits */
    unsigned int speedLimit_Bps[2];

    /* is turtle mode on right now? */
    bool isEnabled;

    /* does turtle mode turn itself on and off at given times? */
    bool isClockEnabled;

    /* when clock mode is on, minutes after midnight to turn on turtle mode */
    int beginMinute;

    /* when clock mode is on, minutes after midnight to turn off turtle mode */
    int endMinute;

    /* only use clock mode on these days of the week */
    tr_sched_day days;

    /* called when isEnabled changes */
    tr_altSpeedFunc callback;

    /* the callback's user_data argument */
    void * callbackUserData;

    /* the callback's changedByUser argument.
     * indicates whether the change came from the user or from the clock. */
    bool changedByUser;

    /* bitfield of all the minutes in a week.
     * Each bit's value indicates whether the scheduler wants turtle
     * limits on or off at that given minute in the week. */
    tr_bitfield minutes;

    /* recent action that was done by turtle's automatic switch */
    tr_auto_switch_state_t autoTurtleState;
};

/** @brief handle to an active libtransmission session */
struct tr_session
{
    bool                         isPortRandom;
    bool                         isPexEnabled;
    bool                         isDHTEnabled;
    bool                         isUTPEnabled;
    bool                         isLPDEnabled;
    bool                         isBlocklistEnabled;
    bool                         isPrefetchEnabled;
    bool                         isTorrentDoneScriptEnabled;
    bool                         isClosing;
    bool                         isClosed;
    bool                         isIncompleteFileNamingEnabled;
    bool                         isRatioLimited;
    bool                         isIdleLimited;
    bool                         isIncompleteDirEnabled;
    bool                         pauseAddedTorrent;
    bool                         deleteSourceTorrent;
    bool                         scrapePausedTorrents;

    uint8_t                      peer_id_ttl_hours;

    tr_variant                   removedTorrents;

    bool                         stalledEnabled;
    bool                         queueEnabled[2];
    int                          queueSize[2];
    int                          queueStalledMinutes;

    int                          umask;

    unsigned int                 speedLimit_Bps[2];
    bool                         speedLimitEnabled[2];

    struct tr_turtle_info        turtle;

    struct tr_fdInfo           * fdInfo;

    int                          magicNumber;

    tr_encryption_mode           encryptionMode;

    tr_preallocation_mode        preallocationMode;

    struct event_base          * event_base;
    struct evdns_base          * evdns_base;
    struct tr_event_handle     * events;

    uint16_t                     peerLimit;
    uint16_t                     peerLimitPerTorrent;

    int                          uploadSlotsPerTorrent;

    /* The UDP sockets used for the DHT and uTP. */
    tr_port                      udp_port;
    tr_socket_t                  udp_socket;
    tr_socket_t                  udp6_socket;
    unsigned char *              udp6_bound;
    struct event                 *udp_event;
    struct event                 *udp6_event;

    /* The open port on the local machine for incoming peer requests */
    tr_port                      private_peer_port;

    /**
     * The open port on the public device for incoming peer requests.
     * This is usually the same as private_peer_port but can differ
     * if the public device is a router and it decides to use a different
     * port than the one requested by Transmission.
     */
    tr_port                      public_peer_port;

    tr_port                      randomPortLow;
    tr_port                      randomPortHigh;

    int                          peerSocketTOS;
    char *                       peer_congestion_algorithm;

    int                          torrentCount;
    tr_torrent *                 torrentList;

    char *                       torrentDoneScript;

    char *                       configDir;
    char *                       resumeDir;
    char *                       torrentDir;
    char *                       incompleteDir;

    char *                       blocklist_url;

    struct tr_device_info *      downloadDir;

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
    struct tr_announcer_udp    * announcer_udp;

    tr_variant                 * metainfoLookup;

    struct event               * nowTimer;
    struct event               * saveTimer;

    /* monitors the "global pool" speeds */
    struct tr_bandwidth          bandwidth;

    float                        desiredRatio;

    uint16_t                     idleLimitMinutes;

    struct tr_bindinfo         * public_ipv4;
    struct tr_bindinfo         * public_ipv6;
};

static inline tr_port
tr_sessionGetPublicPeerPort (const tr_session * session)
{
    return session->public_peer_port;
}

bool         tr_sessionAllowsDHT (const tr_session * session);

bool         tr_sessionAllowsLPD (const tr_session * session);

const char * tr_sessionFindTorrentFile (const tr_session * session,
                                        const char *       hashString);

void         tr_sessionSetTorrentFile (tr_session * session,
                                       const char * hashString,
                                       const char * filename);

bool         tr_sessionIsAddressBlocked (const tr_session        * session,
                                         const struct tr_address * addr);

void         tr_sessionLock (tr_session *);

void         tr_sessionUnlock (tr_session *);

bool         tr_sessionIsLocked (const tr_session *);

const struct tr_address*  tr_sessionGetPublicAddress (const tr_session  * session,
                                                      int                 tr_af_type,
                                                      bool              * is_default_value);


struct tr_bindsockets * tr_sessionGetBindSockets (tr_session *);

int tr_sessionCountTorrents (const tr_session * session);

tr_torrent ** tr_sessionGetTorrents (tr_session * session, int * setme_n);

enum
{
    SESSION_MAGIC_NUMBER = 3845,
};

static inline bool tr_isSession (const tr_session * session)
{
    return (session != NULL) && (session->magicNumber == SESSION_MAGIC_NUMBER);
}

static inline bool tr_isPreallocationMode (tr_preallocation_mode m)
{
    return (m == TR_PREALLOCATE_NONE)
        || (m == TR_PREALLOCATE_SPARSE)
        || (m == TR_PREALLOCATE_FULL);
}

static inline bool tr_isEncryptionMode (tr_encryption_mode m)
{
    return (m == TR_CLEAR_PREFERRED)
        || (m == TR_ENCRYPTION_PREFERRED)
        || (m == TR_ENCRYPTION_REQUIRED);
}

static inline bool tr_isPriority (tr_priority_t p)
{
    return (p == TR_PRI_LOW)
        || (p == TR_PRI_NORMAL)
        || (p == TR_PRI_HIGH);
}

/***
****
***/

static inline unsigned int
toSpeedBytes (unsigned int KBps) { return KBps * tr_speed_K; }
static inline double
toSpeedKBps (unsigned int Bps)  { return Bps / (double)tr_speed_K; }

static inline uint64_t
toMemBytes (unsigned int MB) { uint64_t B = tr_mem_K * tr_mem_K; B *= MB; return B; }
static inline int
toMemMB  (uint64_t B)      { return B / (tr_mem_K * tr_mem_K); }

/**
**/

unsigned int  tr_sessionGetSpeedLimit_Bps (const tr_session *, tr_direction);
unsigned int  tr_sessionGetAltSpeed_Bps (const tr_session *, tr_direction);
unsigned int  tr_sessionGetRawSpeed_Bps (const tr_session *, tr_direction);
unsigned int  tr_sessionGetPieceSpeed_Bps (const tr_session *, tr_direction);

void tr_sessionSetSpeedLimit_Bps (tr_session *, tr_direction, unsigned int Bps);
void tr_sessionSetAltSpeed_Bps (tr_session *, tr_direction, unsigned int Bps);

bool  tr_sessionGetActiveSpeedLimit_Bps (const tr_session  * session,
                                         tr_direction        dir,
                                         unsigned int      * setme);

void tr_sessionGetNextQueuedTorrents (tr_session   * session,
                                      tr_direction   dir,
                                      size_t         numwanted,
                                      tr_ptrArray  * setme);

int tr_sessionCountQueueFreeSlots (tr_session * session, tr_direction);


#endif
