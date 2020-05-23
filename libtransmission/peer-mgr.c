/*
 * This file Copyright (C) 2007-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <errno.h> /* error codes ERANGE, ... */
#include <limits.h> /* INT_MAX */
#include <string.h> /* memcpy, memcmp, strstr */
#include <stdlib.h> /* qsort */

#include <event2/event.h>

#include <libutp/utp.h>

#include "transmission.h"
#include "announcer.h"
#include "bandwidth.h"
#include "blocklist.h"
#include "cache.h"
#include "clients.h"
#include "completion.h"
#include "crypto-utils.h"
#include "handshake.h"
#include "log.h"
#include "net.h"
#include "peer-io.h"
#include "peer-mgr.h"
#include "peer-msgs.h"
#include "ptrarray.h"
#include "session.h"
#include "stats.h" /* tr_statsAddUploaded, tr_statsAddDownloaded */
#include "torrent.h"
#include "tr-assert.h"
#include "tr-utp.h"
#include "utils.h"
#include "webseed.h"

enum
{
    /* how frequently to cull old atoms */
    ATOM_PERIOD_MSEC = (60 * 1000),
    /* how frequently to change which peers are choked */
    RECHOKE_PERIOD_MSEC = (10 * 1000),
    /* an optimistically unchoked peer is immune from rechoking
       for this many calls to rechokeUploads(). */
    OPTIMISTIC_UNCHOKE_MULTIPLIER = 4,
    /* how frequently to reallocate bandwidth */
    BANDWIDTH_PERIOD_MSEC = 500,
    /* how frequently to age out old piece request lists */
    REFILL_UPKEEP_PERIOD_MSEC = (10 * 1000),
    /* how frequently to decide which peers live and die */
    RECONNECT_PERIOD_MSEC = 500,
    /* when many peers are available, keep idle ones this long */
    MIN_UPLOAD_IDLE_SECS = (60),
    /* when few peers are available, keep idle ones this long */
    MAX_UPLOAD_IDLE_SECS = (60 * 5),
    /* max number of peers to ask for per second overall.
     * this throttle is to avoid overloading the router */
    MAX_CONNECTIONS_PER_SECOND = 12,
    /* */
    MAX_CONNECTIONS_PER_PULSE = (int)(MAX_CONNECTIONS_PER_SECOND * (RECONNECT_PERIOD_MSEC / 1000.0)),
    /* number of bad pieces a peer is allowed to send before we ban them */
    MAX_BAD_PIECES_PER_PEER = 5,
    /* amount of time to keep a list of request pieces lying around
       before it's considered too old and needs to be rebuilt */
    PIECE_LIST_SHELF_LIFE_SECS = 60,
    /* use for bitwise operations w/peer_atom.flags2 */
    MYFLAG_BANNED = 1,
    /* use for bitwise operations w/peer_atom.flags2 */
    /* unreachable for now... but not banned.
     * if they try to connect to us it's okay */
    MYFLAG_UNREACHABLE = 2,
    /* the minimum we'll wait before attempting to reconnect to a peer */
    MINIMUM_RECONNECT_INTERVAL_SECS = 5,
    /** how long we'll let requests we've made linger before we cancel them */
    REQUEST_TTL_SECS = 90,
    /* */
    NO_BLOCKS_CANCEL_HISTORY = 120,
    /* */
    CANCEL_HISTORY_SEC = 60
};

tr_peer_event const TR_PEER_EVENT_INIT =
{
    .eventType = TR_PEER_CLIENT_GOT_BLOCK,
    .pieceIndex = 0,
    .bitfield = NULL,
    .offset = 0,
    .length = 0,
    .err = 0,
    .port = 0
};

tr_swarm_stats const TR_SWARM_STATS_INIT =
{
    .activePeerCount = { 0, 0 },
    .activeWebseedCount = 0,
    .peerCount = 0,
    .peerFromCount = { 0, 0, 0, 0, 0, 0, 0 }
};

/**
***
**/

/**
 * Peer information that should be kept even before we've connected and
 * after we've disconnected. These are kept in a pool of peer_atoms to decide
 * which ones would make good candidates for connecting to, and to watch out
 * for banned peers.
 *
 * @see tr_peer
 * @see tr_peerMsgs
 */
struct peer_atom
{
    uint8_t fromFirst; /* where the peer was first found */
    uint8_t fromBest; /* the "best" value of where the peer has been found */
    uint8_t flags; /* these match the added_f flags */
    uint8_t flags2; /* flags that aren't defined in added_f */
    int8_t seedProbability; /* how likely is this to be a seed... [0..100] or -1 for unknown */
    int8_t blocklisted; /* -1 for unknown, true for blocklisted, false for not blocklisted */

    tr_port port;
    bool utp_failed; /* We recently failed to connect over uTP */
    uint16_t numFails;
    time_t time; /* when the peer's connection status last changed */
    time_t piece_data_time;

    time_t lastConnectionAttemptAt;
    time_t lastConnectionAt;

    /* similar to a TTL field, but less rigid --
     * if the swarm is small, the atom will be kept past this date. */
    time_t shelf_date;
    tr_peer* peer; /* will be NULL if not connected */
    tr_address addr;
};

#ifndef TR_ENABLE_ASSERTS

#define tr_isAtom(a) (true)

#else

static bool tr_isAtom(struct peer_atom const* atom)
{
    return atom != NULL && atom->fromFirst < TR_PEER_FROM__MAX && atom->fromBest < TR_PEER_FROM__MAX &&
        tr_address_is_valid(&atom->addr);
}

#endif

static char const* tr_atomAddrStr(struct peer_atom const* atom)
{
    return atom != NULL ? tr_peerIoAddrStr(&atom->addr, atom->port) : "[no atom]";
}

struct block_request
{
    tr_block_index_t block;
    tr_peer* peer;
    time_t sentAt;
};

struct weighted_piece
{
    tr_piece_index_t index;
    int16_t salt;
    int16_t requestCount;
};

enum piece_sort_state
{
    PIECES_UNSORTED,
    PIECES_SORTED_BY_INDEX,
    PIECES_SORTED_BY_WEIGHT
};

/** @brief Opaque, per-torrent data structure for peer connection information */
typedef struct tr_swarm
{
    tr_swarm_stats stats;

    tr_ptrArray outgoingHandshakes; /* tr_handshake */
    tr_ptrArray pool; /* struct peer_atom */
    tr_ptrArray peers; /* tr_peerMsgs */
    tr_ptrArray webseeds; /* tr_webseed */

    tr_torrent* tor;
    struct tr_peerMgr* manager;

    tr_peerMsgs* optimistic; /* the optimistic peer, or NULL if none */
    int optimisticUnchokeTimeScaler;

    bool isRunning;
    bool needsCompletenessCheck;

    struct block_request* requests;
    int requestCount;
    int requestAlloc;

    struct weighted_piece* pieces;
    int pieceCount;
    enum piece_sort_state pieceSortState;

    /* An array of pieceCount items stating how many peers have each piece.
       This is used to help us for downloading pieces "rarest first."
       This may be NULL if we don't have metainfo yet, or if we're not
       downloading and don't care about rarity */
    uint16_t* pieceReplication;
    size_t pieceReplicationSize;

    int interestedCount;
    int maxPeers;
    time_t lastCancel;

    /* Before the endgame this should be 0. In endgame, is contains the average
     * number of pending requests per peer. Only peers which have more pending
     * requests are considered 'fast' are allowed to request a block that's
     * already been requested from another (slower?) peer. */
    int endgame;
}
tr_swarm;

struct tr_peerMgr
{
    tr_session* session;
    tr_ptrArray incomingHandshakes; /* tr_handshake */
    struct event* bandwidthTimer;
    struct event* rechokeTimer;
    struct event* refillUpkeepTimer;
    struct event* atomTimer;
};

#define tordbg(t, ...) tr_logAddDeepNamed(tr_torrentName((t)->tor), __VA_ARGS__)

#define dbgmsg(...) tr_logAddDeepNamed(NULL, __VA_ARGS__)

/**
*** tr_peer virtual functions
**/

static bool tr_peerIsTransferringPieces(tr_peer const* peer, uint64_t now, tr_direction direction, unsigned int* Bps)
{
    TR_ASSERT(peer != NULL);
    TR_ASSERT(peer->funcs != NULL);

    return (*peer->funcs->is_transferring_pieces)(peer, now, direction, Bps);
}

unsigned int tr_peerGetPieceSpeed_Bps(tr_peer const* peer, uint64_t now, tr_direction direction)
{
    unsigned int Bps = 0;
    tr_peerIsTransferringPieces(peer, now, direction, &Bps);
    return Bps;
}

static void tr_peerFree(tr_peer* peer)
{
    TR_ASSERT(peer != NULL);
    TR_ASSERT(peer->funcs != NULL);

    (*peer->funcs->destruct)(peer);

    tr_free(peer);
}

void tr_peerConstruct(tr_peer* peer, tr_torrent const* tor)
{
    TR_ASSERT(peer != NULL);
    TR_ASSERT(tr_isTorrent(tor));

    memset(peer, 0, sizeof(tr_peer));

    peer->client = TR_KEY_NONE;
    peer->swarm = tor->swarm;
    tr_bitfieldConstruct(&peer->have, tor->info.pieceCount);
    tr_bitfieldConstruct(&peer->blame, tor->blockCount);
}

static void peerDeclinedAllRequests(tr_swarm*, tr_peer const*);

void tr_peerDestruct(tr_peer* peer)
{
    TR_ASSERT(peer != NULL);

    if (peer->swarm != NULL)
    {
        peerDeclinedAllRequests(peer->swarm, peer);
    }

    tr_bitfieldDestruct(&peer->have);
    tr_bitfieldDestruct(&peer->blame);

    if (peer->atom != NULL)
    {
        peer->atom->peer = NULL;
    }
}

/**
***
**/

static inline void managerLock(struct tr_peerMgr const* manager)
{
    tr_sessionLock(manager->session);
}

static inline void managerUnlock(struct tr_peerMgr const* manager)
{
    tr_sessionUnlock(manager->session);
}

static inline void swarmLock(tr_swarm* swarm)
{
    managerLock(swarm->manager);
}

static inline void swarmUnlock(tr_swarm* swarm)
{
    managerUnlock(swarm->manager);
}

#ifdef TR_ENABLE_ASSERTS

static inline bool swarmIsLocked(tr_swarm const* swarm)
{
    return tr_sessionIsLocked(swarm->manager->session);
}

#endif /* TR_ENABLE_ASSERTS */

/**
***
**/

static int handshakeCompareToAddr(void const* va, void const* vb)
{
    tr_handshake const* a = va;

    return tr_address_compare(tr_handshakeGetAddr(a, NULL), vb);
}

static int handshakeCompare(void const* a, void const* b)
{
    return handshakeCompareToAddr(a, tr_handshakeGetAddr(b, NULL));
}

static inline tr_handshake* getExistingHandshake(tr_ptrArray* handshakes, tr_address const* addr)
{
    if (tr_ptrArrayEmpty(handshakes))
    {
        return NULL;
    }

    return tr_ptrArrayFindSorted(handshakes, addr, handshakeCompareToAddr);
}

static int comparePeerAtomToAddress(void const* va, void const* vb)
{
    struct peer_atom const* a = va;

    return tr_address_compare(&a->addr, vb);
}

static int compareAtomsByAddress(void const* va, void const* vb)
{
    struct peer_atom const* b = vb;

    TR_ASSERT(tr_isAtom(b));

    return comparePeerAtomToAddress(va, &b->addr);
}

/**
***
**/

tr_address const* tr_peerAddress(tr_peer const* peer)
{
    return &peer->atom->addr;
}

static tr_swarm* getExistingSwarm(tr_peerMgr* manager, uint8_t const* hash)
{
    tr_torrent* tor = tr_torrentFindFromHash(manager->session, hash);

    return tor == NULL ? NULL : tor->swarm;
}

static int peerCompare(void const* a, void const* b)
{
    return tr_address_compare(tr_peerAddress(a), tr_peerAddress(b));
}

static struct peer_atom* getExistingAtom(tr_swarm const* cswarm, tr_address const* addr)
{
    tr_swarm* swarm = (tr_swarm*)cswarm;
    return tr_ptrArrayFindSorted(&swarm->pool, addr, comparePeerAtomToAddress);
}

static bool peerIsInUse(tr_swarm const* cs, struct peer_atom const* atom)
{
    tr_swarm* s = (tr_swarm*)cs;

    TR_ASSERT(swarmIsLocked(s));

    return atom->peer != NULL || getExistingHandshake(&s->outgoingHandshakes, &atom->addr) != NULL ||
        getExistingHandshake(&s->manager->incomingHandshakes, &atom->addr) != NULL;
}

static inline bool replicationExists(tr_swarm const* s)
{
    return s->pieceReplication != NULL;
}

static void replicationFree(tr_swarm* s)
{
    tr_free(s->pieceReplication);
    s->pieceReplication = NULL;
    s->pieceReplicationSize = 0;
}

static void replicationNew(tr_swarm* s)
{
    TR_ASSERT(!replicationExists(s));

    tr_piece_index_t const piece_count = s->tor->info.pieceCount;
    int const n = tr_ptrArraySize(&s->peers);

    s->pieceReplicationSize = piece_count;
    s->pieceReplication = tr_new0(uint16_t, piece_count);

    for (tr_piece_index_t piece_i = 0; piece_i < piece_count; ++piece_i)
    {
        uint16_t r = 0;

        for (int peer_i = 0; peer_i < n; ++peer_i)
        {
            tr_peer* peer = tr_ptrArrayNth(&s->peers, peer_i);

            if (tr_bitfieldHas(&peer->have, piece_i))
            {
                ++r;
            }
        }

        s->pieceReplication[piece_i] = r;
    }
}

static void swarmFree(void* vs)
{
    tr_swarm* s = vs;

    TR_ASSERT(s != NULL);
    TR_ASSERT(!s->isRunning);
    TR_ASSERT(swarmIsLocked(s));
    TR_ASSERT(tr_ptrArrayEmpty(&s->outgoingHandshakes));
    TR_ASSERT(tr_ptrArrayEmpty(&s->peers));

    tr_ptrArrayDestruct(&s->webseeds, (PtrArrayForeachFunc)tr_peerFree);
    tr_ptrArrayDestruct(&s->pool, (PtrArrayForeachFunc)tr_free);
    tr_ptrArrayDestruct(&s->outgoingHandshakes, NULL);
    tr_ptrArrayDestruct(&s->peers, NULL);
    s->stats = TR_SWARM_STATS_INIT;

    replicationFree(s);

    tr_free(s->requests);
    tr_free(s->pieces);
    tr_free(s);
}

static void peerCallbackFunc(tr_peer*, tr_peer_event const*, void*);

static void rebuildWebseedArray(tr_swarm* s, tr_torrent* tor)
{
    tr_info const* inf = &tor->info;

    /* clear the array */
    tr_ptrArrayDestruct(&s->webseeds, (PtrArrayForeachFunc)tr_peerFree);
    s->webseeds = TR_PTR_ARRAY_INIT;
    s->stats.activeWebseedCount = 0;

    /* repopulate it */
    for (unsigned int i = 0; i < inf->webseedCount; ++i)
    {
        tr_webseed* w = tr_webseedNew(tor, inf->webseeds[i], peerCallbackFunc, s);
        tr_ptrArrayAppend(&s->webseeds, w);
    }
}

static tr_swarm* swarmNew(tr_peerMgr* manager, tr_torrent* tor)
{
    tr_swarm* s;

    s = tr_new0(tr_swarm, 1);
    s->manager = manager;
    s->tor = tor;
    s->pool = TR_PTR_ARRAY_INIT;
    s->peers = TR_PTR_ARRAY_INIT;
    s->webseeds = TR_PTR_ARRAY_INIT;
    s->outgoingHandshakes = TR_PTR_ARRAY_INIT;

    rebuildWebseedArray(s, tor);

    return s;
}

static void ensureMgrTimersExist(struct tr_peerMgr* m);

tr_peerMgr* tr_peerMgrNew(tr_session* session)
{
    tr_peerMgr* m = tr_new0(tr_peerMgr, 1);
    m->session = session;
    m->incomingHandshakes = TR_PTR_ARRAY_INIT;
    ensureMgrTimersExist(m);
    return m;
}

static void deleteTimer(struct event** t)
{
    if (*t != NULL)
    {
        event_free(*t);
        *t = NULL;
    }
}

static void deleteTimers(struct tr_peerMgr* m)
{
    deleteTimer(&m->atomTimer);
    deleteTimer(&m->bandwidthTimer);
    deleteTimer(&m->rechokeTimer);
    deleteTimer(&m->refillUpkeepTimer);
}

void tr_peerMgrFree(tr_peerMgr* manager)
{
    managerLock(manager);

    deleteTimers(manager);

    /* free the handshakes. Abort invokes handshakeDoneCB(), which removes
     * the item from manager->handshakes, so this is a little roundabout... */
    while (!tr_ptrArrayEmpty(&manager->incomingHandshakes))
    {
        tr_handshakeAbort(tr_ptrArrayNth(&manager->incomingHandshakes, 0));
    }

    tr_ptrArrayDestruct(&manager->incomingHandshakes, NULL);

    managerUnlock(manager);
    tr_free(manager);
}

/***
****
***/

void tr_peerMgrOnBlocklistChanged(tr_peerMgr* mgr)
{
    tr_torrent* tor = NULL;
    tr_session* session = mgr->session;

    /* we cache whether or not a peer is blocklisted...
       since the blocklist has changed, erase that cached value */
    while ((tor = tr_torrentNext(session, tor)) != NULL)
    {
        tr_swarm* s = tor->swarm;

        for (int i = 0, n = tr_ptrArraySize(&s->pool); i < n; ++i)
        {
            struct peer_atom* atom = tr_ptrArrayNth(&s->pool, i);
            atom->blocklisted = -1;
        }
    }
}

static bool isAtomBlocklisted(tr_session* session, struct peer_atom* atom)
{
    if (atom->blocklisted < 0)
    {
        atom->blocklisted = (int8_t)tr_sessionIsAddressBlocked(session, &atom->addr);
    }

    return atom->blocklisted != 0;
}

/***
****
***/

static void atomSetSeedProbability(struct peer_atom* atom, int seedProbability)
{
    TR_ASSERT(atom != NULL);
    TR_ASSERT(seedProbability >= -1);
    TR_ASSERT(seedProbability <= 100);

    atom->seedProbability = seedProbability;

    if (seedProbability == 100)
    {
        atom->flags |= ADDED_F_SEED_FLAG;
    }
    else if (seedProbability != -1)
    {
        atom->flags &= ~ADDED_F_SEED_FLAG;
    }
}

static inline bool atomIsSeed(struct peer_atom const* atom)
{
    return atom->seedProbability == 100;
}

static void atomSetSeed(tr_swarm const* s, struct peer_atom* atom)
{
    if (!atomIsSeed(atom))
    {
        tordbg(s, "marking peer %s as a seed", tr_atomAddrStr(atom));

        atomSetSeedProbability(atom, 100);
    }
}

bool tr_peerMgrPeerIsSeed(tr_torrent const* tor, tr_address const* addr)
{
    bool isSeed = false;
    tr_swarm const* s = tor->swarm;
    struct peer_atom const* atom = getExistingAtom(s, addr);

    if (atom != NULL)
    {
        isSeed = atomIsSeed(atom);
    }

    return isSeed;
}

void tr_peerMgrSetUtpSupported(tr_torrent* tor, tr_address const* addr)
{
    struct peer_atom* atom = getExistingAtom(tor->swarm, addr);

    if (atom != NULL)
    {
        atom->flags |= ADDED_F_UTP_FLAGS;
    }
}

void tr_peerMgrSetUtpFailed(tr_torrent* tor, tr_address const* addr, bool failed)
{
    struct peer_atom* atom = getExistingAtom(tor->swarm, addr);

    if (atom != NULL)
    {
        atom->utp_failed = failed;
    }
}

/**
***  REQUESTS
***
*** There are two data structures associated with managing block requests:
***
*** 1. tr_swarm::requests, an array of "struct block_request" which keeps
***    track of which blocks have been requested, and when, and by which peers.
***    This is list is used for (a) cancelling requests that have been pending
***    for too long and (b) avoiding duplicate requests before endgame.
***
*** 2. tr_swarm::pieces, an array of "struct weighted_piece" which lists the
***    pieces that we want to request. It's used to decide which blocks to
***    return next when tr_peerMgrGetBlockRequests() is called.
**/

/**
*** struct block_request
**/

static int compareReqByBlock(void const* va, void const* vb)
{
    struct block_request const* a = va;
    struct block_request const* b = vb;

    /* primary key: block */
    if (a->block < b->block)
    {
        return -1;
    }

    if (a->block > b->block)
    {
        return 1;
    }

    /* secondary key: peer */
    if (a->peer < b->peer)
    {
        return -1;
    }

    if (a->peer > b->peer)
    {
        return 1;
    }

    return 0;
}

static void requestListAdd(tr_swarm* s, tr_block_index_t block, tr_peer* peer)
{
    struct block_request key;

    /* ensure enough room is available... */
    if (s->requestCount + 1 >= s->requestAlloc)
    {
        int const CHUNK_SIZE = 128;
        s->requestAlloc += CHUNK_SIZE;
        s->requests = tr_renew(struct block_request, s->requests, s->requestAlloc);
    }

    /* populate the record we're inserting */
    key.block = block;
    key.peer = peer;
    key.sentAt = tr_time();

    /* insert the request to our array... */
    {
        bool exact;
        int const pos = tr_lowerBound(&key, s->requests, s->requestCount, sizeof(struct block_request), compareReqByBlock,
            &exact);
        TR_ASSERT(!exact);
        memmove(s->requests + pos + 1, s->requests + pos, sizeof(struct block_request) * (s->requestCount++ - pos));
        s->requests[pos] = key;
    }

    if (peer != NULL)
    {
        ++peer->pendingReqsToPeer;
        TR_ASSERT(peer->pendingReqsToPeer >= 0);
    }

    // fprintf(stderr, "added request of block %lu from peer %s... there are now %d block\n", (unsigned long)block,
    //     tr_atomAddrStr(peer->atom), s->requestCount);
}

static struct block_request* requestListLookup(tr_swarm* s, tr_block_index_t block, tr_peer const* peer)
{
    struct block_request key;
    key.block = block;
    key.peer = (tr_peer*)peer;

    return bsearch(&key, s->requests, s->requestCount, sizeof(struct block_request), compareReqByBlock);
}

/**
 * Find the peers are we currently requesting the block
 * with index @a block from and append them to @a peerArr.
 */
static void getBlockRequestPeers(tr_swarm* s, tr_block_index_t block, tr_ptrArray* peerArr)
{
    bool exact;
    int pos;
    struct block_request key;

    key.block = block;
    key.peer = NULL;
    pos = tr_lowerBound(&key, s->requests, s->requestCount, sizeof(struct block_request), compareReqByBlock, &exact);

    TR_ASSERT(!exact); /* shouldn't have a request with .peer == NULL */

    for (int i = pos; i < s->requestCount; ++i)
    {
        if (s->requests[i].block != block)
        {
            break;
        }

        tr_ptrArrayAppend(peerArr, s->requests[i].peer);
    }
}

static void decrementPendingReqCount(struct block_request const* b)
{
    if (b->peer != NULL)
    {
        if (b->peer->pendingReqsToPeer > 0)
        {
            --b->peer->pendingReqsToPeer;
        }
    }
}

static void requestListRemove(tr_swarm* s, tr_block_index_t block, tr_peer const* peer)
{
    struct block_request const* b = requestListLookup(s, block, peer);

    if (b != NULL)
    {
        int const pos = b - s->requests;
        TR_ASSERT(pos < s->requestCount);

        decrementPendingReqCount(b);

        tr_removeElementFromArray(s->requests, pos, sizeof(struct block_request), s->requestCount);
        --s->requestCount;

        // fprintf(stderr, "removing request of block %lu from peer %s... there are now %d block requests left\n", (unsigned long)block,
        //     tr_atomAddrStr(peer->atom), t->requestCount);
    }
}

static int countActiveWebseeds(tr_swarm* s)
{
    int activeCount = 0;

    if (s->tor->isRunning && !tr_torrentIsSeed(s->tor))
    {
        uint64_t const now = tr_time_msec();

        for (int i = 0, n = tr_ptrArraySize(&s->webseeds); i < n; ++i)
        {
            if (tr_peerIsTransferringPieces(tr_ptrArrayNth(&s->webseeds, i), now, TR_DOWN, NULL))
            {
                ++activeCount;
            }
        }
    }

    return activeCount;
}

static bool testForEndgame(tr_swarm const* s)
{
    /* we consider ourselves to be in endgame if the number of bytes
       we've got requested is >= the number of bytes left to download */
    return (uint64_t)s->requestCount * s->tor->blockSize >= tr_torrentGetLeftUntilDone(s->tor);
}

static void updateEndgame(tr_swarm* s)
{
    TR_ASSERT(s->requestCount >= 0);

    if (!testForEndgame(s))
    {
        /* not in endgame */
        s->endgame = 0;
    }
    else if (s->endgame == 0) /* only recalculate when endgame first begins */
    {
        int numDownloading = 0;

        /* add the active bittorrent peers... */
        for (int i = 0, n = tr_ptrArraySize(&s->peers); i < n; ++i)
        {
            tr_peer const* p = tr_ptrArrayNth(&s->peers, i);

            if (p->pendingReqsToPeer > 0)
            {
                ++numDownloading;
            }
        }

        /* add the active webseeds... */
        numDownloading += countActiveWebseeds(s);

        /* average number of pending requests per downloading peer */
        s->endgame = s->requestCount / MAX(numDownloading, 1);
    }
}

/****
*****
*****  Piece List Manipulation / Accessors
*****
****/

static inline void invalidatePieceSorting(tr_swarm* s)
{
    s->pieceSortState = PIECES_UNSORTED;
}

static tr_torrent const* weightTorrent;

static uint16_t const* weightReplication;

static void setComparePieceByWeightTorrent(tr_swarm* s)
{
    if (!replicationExists(s))
    {
        replicationNew(s);
    }

    weightTorrent = s->tor;
    weightReplication = s->pieceReplication;
}

/* we try to create a "weight" s.t. high-priority pieces come before others,
 * and that partially-complete pieces come before empty ones. */
static int comparePieceByWeight(void const* va, void const* vb)
{
    struct weighted_piece const* a = va;
    struct weighted_piece const* b = vb;
    int ia;
    int ib;
    int missing;
    int pending;
    tr_torrent const* tor = weightTorrent;
    uint16_t const* rep = weightReplication;

    /* primary key: weight */
    missing = tr_torrentMissingBlocksInPiece(tor, a->index);
    pending = a->requestCount;
    ia = missing > pending ? missing - pending : tor->blockCountInPiece + pending;
    missing = tr_torrentMissingBlocksInPiece(tor, b->index);
    pending = b->requestCount;
    ib = missing > pending ? missing - pending : tor->blockCountInPiece + pending;

    if (ia < ib)
    {
        return -1;
    }

    if (ia > ib)
    {
        return 1;
    }

    /* secondary key: higher priorities go first */
    ia = tor->info.pieces[a->index].priority;
    ib = tor->info.pieces[b->index].priority;

    if (ia > ib)
    {
        return -1;
    }

    if (ia < ib)
    {
        return 1;
    }

    /* tertiary key: rarest first. */
    ia = rep[a->index];
    ib = rep[b->index];

    if (ia < ib)
    {
        return -1;
    }

    if (ia > ib)
    {
        return 1;
    }

    /* quaternary key: random */
    if (a->salt < b->salt)
    {
        return -1;
    }

    if (a->salt > b->salt)
    {
        return 1;
    }

    /* okay, they're equal */
    return 0;
}

static int comparePieceByIndex(void const* va, void const* vb)
{
    struct weighted_piece const* a = va;
    struct weighted_piece const* b = vb;

    if (a->index < b->index)
    {
        return -1;
    }

    if (a->index > b->index)
    {
        return 1;
    }

    return 0;
}

static void pieceListSort(tr_swarm* s, enum piece_sort_state state)
{
    TR_ASSERT(state == PIECES_SORTED_BY_INDEX || state == PIECES_SORTED_BY_WEIGHT);

    if (state == PIECES_SORTED_BY_WEIGHT)
    {
        setComparePieceByWeightTorrent(s);
        qsort(s->pieces, s->pieceCount, sizeof(struct weighted_piece), comparePieceByWeight);
    }
    else
    {
        qsort(s->pieces, s->pieceCount, sizeof(struct weighted_piece), comparePieceByIndex);
    }

    s->pieceSortState = state;
}

/**
 * These functions are useful for testing, but too expensive for nightly builds.
 * let's leave it disabled but add an easy hook to compile it back in
 */
#if 1

#define assertWeightedPiecesAreSorted(t)
#define assertReplicationCountIsExact(t)

#else

static void assertWeightedPiecesAreSorted(Torrent* t)
{
    if (t->endgame == 0)
    {
        setComparePieceByWeightTorrent(t);

        for (int i = 0; i < t->pieceCount - 1; ++i)
        {
            TR_ASSERT(comparePieceByWeight(&t->pieces[i], &t->pieces[i + 1]) <= 0);
        }
    }
}

static void assertReplicationCountIsExact(Torrent* t)
{
    /* This assert might fail due to errors of implementations in other
     * clients. It happens when receiving duplicate bitfields/HaveAll/HaveNone
     * from a client. If a such a behavior is noticed,
     * a bug report should be filled to the faulty client. */

    uint16_t const* rep = t->pieceReplication;
    size_t const piece_count = t->pieceReplicationSize;
    tr_peer const** peers = (tr_peer const**)tr_ptrArrayBase(&t->peers);
    int const peer_count = tr_ptrArraySize(&t->peers);

    TR_ASSERT(piece_count == t->tor->info.pieceCount);

    for (size_t piece_i = 0; piece_i < piece_count; ++piece_i)
    {
        uint16_t r = 0;

        for (int peer_i = 0; peer_i < peer_count; ++peer_i)
        {
            if (tr_bitsetHas(&peers[peer_i]->have, piece_i))
            {
                ++r;
            }
        }

        TR_ASSERT(rep[piece_i] == r);
    }
}

#endif

static struct weighted_piece* pieceListLookup(tr_swarm* s, tr_piece_index_t index)
{
    for (int i = 0; i < s->pieceCount; ++i)
    {
        if (s->pieces[i].index == index)
        {
            return &s->pieces[i];
        }
    }

    return NULL;
}

static void pieceListRebuild(tr_swarm* s)
{
    if (!tr_torrentIsSeed(s->tor))
    {
        tr_piece_index_t* pool;
        tr_piece_index_t poolCount = 0;
        tr_torrent const* tor = s->tor;
        tr_info const* inf = tr_torrentInfo(tor);
        struct weighted_piece* pieces;
        int pieceCount;

        /* build the new list */
        pool = tr_new(tr_piece_index_t, inf->pieceCount);

        for (tr_piece_index_t i = 0; i < inf->pieceCount; ++i)
        {
            if (!inf->pieces[i].dnd)
            {
                if (!tr_torrentPieceIsComplete(tor, i))
                {
                    pool[poolCount++] = i;
                }
            }
        }

        pieceCount = poolCount;
        pieces = tr_new0(struct weighted_piece, pieceCount);

        for (tr_piece_index_t i = 0; i < poolCount; ++i)
        {
            struct weighted_piece* piece = pieces + i;
            piece->index = pool[i];
            piece->requestCount = 0;
            piece->salt = tr_rand_int_weak(4096);
        }

        /* if we already had a list of pieces, merge it into
         * the new list so we don't lose its requestCounts */
        if (s->pieces != NULL)
        {
            struct weighted_piece* o = s->pieces;
            struct weighted_piece* oend = o + s->pieceCount;
            struct weighted_piece* n = pieces;
            struct weighted_piece* nend = n + pieceCount;

            pieceListSort(s, PIECES_SORTED_BY_INDEX);

            while (o != oend && n != nend)
            {
                if (o->index < n->index)
                {
                    ++o;
                }
                else if (o->index > n->index)
                {
                    ++n;
                }
                else
                {
                    *n++ = *o++;
                }
            }

            tr_free(s->pieces);
        }

        s->pieces = pieces;
        s->pieceCount = pieceCount;

        pieceListSort(s, PIECES_SORTED_BY_WEIGHT);

        /* cleanup */
        tr_free(pool);
    }
}

static void pieceListRemovePiece(tr_swarm* s, tr_piece_index_t piece)
{
    struct weighted_piece* p;

    if ((p = pieceListLookup(s, piece)) != NULL)
    {
        int const pos = p - s->pieces;

        tr_removeElementFromArray(s->pieces, pos, sizeof(struct weighted_piece), s->pieceCount);
        --s->pieceCount;

        if (s->pieceCount == 0)
        {
            tr_free(s->pieces);
            s->pieces = NULL;
        }
    }
}

static void pieceListResortPiece(tr_swarm* s, struct weighted_piece* p)
{
    int pos;
    bool isSorted = true;

    if (p == NULL)
    {
        return;
    }

    /* is the torrent already sorted? */
    pos = p - s->pieces;
    setComparePieceByWeightTorrent(s);

    if (isSorted && pos > 0 && comparePieceByWeight(p - 1, p) > 0)
    {
        isSorted = false;
    }

    if (isSorted && pos < s->pieceCount - 1 && comparePieceByWeight(p, p + 1) > 0)
    {
        isSorted = false;
    }

    if (s->pieceSortState != PIECES_SORTED_BY_WEIGHT)
    {
        pieceListSort(s, PIECES_SORTED_BY_WEIGHT);
        isSorted = true;
    }

    /* if it's not sorted, move it around */
    if (!isSorted)
    {
        bool exact;
        struct weighted_piece const tmp = *p;

        tr_removeElementFromArray(s->pieces, pos, sizeof(struct weighted_piece), s->pieceCount);
        --s->pieceCount;

        pos = tr_lowerBound(&tmp, s->pieces, s->pieceCount, sizeof(struct weighted_piece), comparePieceByWeight, &exact);

        memmove(&s->pieces[pos + 1], &s->pieces[pos], sizeof(struct weighted_piece) * (s->pieceCount - pos));
        ++s->pieceCount;

        s->pieces[pos] = tmp;
    }

    assertWeightedPiecesAreSorted(s);
}

static void pieceListRemoveRequest(tr_swarm* s, tr_block_index_t block)
{
    struct weighted_piece* p;
    tr_piece_index_t const index = tr_torBlockPiece(s->tor, block);

    if ((p = pieceListLookup(s, index)) != NULL && p->requestCount > 0)
    {
        --p->requestCount;
        pieceListResortPiece(s, p);
    }
}

/****
*****
*****  Replication count (for rarest first policy)
*****
****/

/**
 * Increase the replication count of this piece and sort it if the
 * piece list is already sorted
 */
static void tr_incrReplicationOfPiece(tr_swarm* s, size_t const index)
{
    TR_ASSERT(replicationExists(s));
    TR_ASSERT(s->pieceReplicationSize == s->tor->info.pieceCount);

    /* One more replication of this piece is present in the swarm */
    ++s->pieceReplication[index];

    /* we only resort the piece if the list is already sorted */
    if (s->pieceSortState == PIECES_SORTED_BY_WEIGHT)
    {
        pieceListResortPiece(s, pieceListLookup(s, index));
    }
}

/**
 * Increases the replication count of pieces present in the bitfield
 */
static void tr_incrReplicationFromBitfield(tr_swarm* s, tr_bitfield const* b)
{
    TR_ASSERT(replicationExists(s));

    uint16_t* rep = s->pieceReplication;

    for (size_t i = 0, n = s->tor->info.pieceCount; i < n; ++i)
    {
        if (tr_bitfieldHas(b, i))
        {
            ++rep[i];
        }
    }

    if (s->pieceSortState == PIECES_SORTED_BY_WEIGHT)
    {
        invalidatePieceSorting(s);
    }
}

/**
 * Increase the replication count of every piece
 */
static void tr_incrReplication(tr_swarm* s)
{
    TR_ASSERT(replicationExists(s));
    TR_ASSERT(s->pieceReplicationSize == s->tor->info.pieceCount);

    for (size_t i = 0; i < s->pieceReplicationSize; ++i)
    {
        ++s->pieceReplication[i];
    }
}

/**
 * Decrease the replication count of pieces present in the bitset.
 */
static void tr_decrReplicationFromBitfield(tr_swarm* s, tr_bitfield const* b)
{
    TR_ASSERT(replicationExists(s));
    TR_ASSERT(s->pieceReplicationSize == s->tor->info.pieceCount);

    if (tr_bitfieldHasAll(b))
    {
        for (size_t i = 0; i < s->pieceReplicationSize; ++i)
        {
            --s->pieceReplication[i];
        }
    }
    else if (!tr_bitfieldHasNone(b))
    {
        for (size_t i = 0; i < s->pieceReplicationSize; ++i)
        {
            if (tr_bitfieldHas(b, i))
            {
                --s->pieceReplication[i];
            }
        }

        if (s->pieceSortState == PIECES_SORTED_BY_WEIGHT)
        {
            invalidatePieceSorting(s);
        }
    }
}

/**
***
**/

void tr_peerMgrRebuildRequests(tr_torrent* tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    pieceListRebuild(tor->swarm);
}

void tr_peerMgrGetNextRequests(tr_torrent* tor, tr_peer* peer, int numwant, tr_block_index_t* setme, int* numgot,
    bool get_intervals)
{
    /* sanity clause */
    TR_ASSERT(tr_isTorrent(tor));
    TR_ASSERT(numwant > 0);

    tr_swarm* s;
    tr_bitfield const* const have = &peer->have;

    /* walk through the pieces and find blocks that should be requested */
    s = tor->swarm;

    /* prep the pieces list */
    if (s->pieces == NULL)
    {
        pieceListRebuild(s);
    }

    if (s->pieceSortState != PIECES_SORTED_BY_WEIGHT)
    {
        pieceListSort(s, PIECES_SORTED_BY_WEIGHT);
    }

    assertReplicationCountIsExact(s);
    assertWeightedPiecesAreSorted(s);

    updateEndgame(s);

    struct weighted_piece* pieces = s->pieces;
    int got = 0;
    int checkedPieceCount = 0;

    for (int i = 0; i < s->pieceCount && got < numwant; ++i, ++checkedPieceCount)
    {
        struct weighted_piece* p = pieces + i;

        /* if the peer has this piece that we want... */
        if (tr_bitfieldHas(have, p->index))
        {
            tr_block_index_t first;
            tr_block_index_t last;
            tr_ptrArray peerArr = TR_PTR_ARRAY_INIT;

            tr_torGetPieceBlockRange(tor, p->index, &first, &last);

            for (tr_block_index_t b = first; b <= last && (got < numwant || (get_intervals && setme[2 * got - 1] == b - 1));
                ++b)
            {
                int peerCount;
                tr_peer** peers;

                /* don't request blocks we've already got */
                if (tr_torrentBlockIsComplete(tor, b))
                {
                    continue;
                }

                /* always add peer if this block has no peers yet */
                tr_ptrArrayClear(&peerArr);
                getBlockRequestPeers(s, b, &peerArr);
                peers = (tr_peer**)tr_ptrArrayPeek(&peerArr, &peerCount);

                if (peerCount != 0)
                {
                    /* don't make a second block request until the endgame */
                    if (s->endgame == 0)
                    {
                        continue;
                    }

                    /* don't have more than two peers requesting this block */
                    if (peerCount > 1)
                    {
                        continue;
                    }

                    /* don't send the same request to the same peer twice */
                    if (peer == peers[0])
                    {
                        continue;
                    }

                    /* in the endgame allow an additional peer to download a
                       block but only if the peer seems to be handling requests
                       relatively fast */
                    if (peer->pendingReqsToPeer + numwant - got < s->endgame)
                    {
                        continue;
                    }
                }

                /* update the caller's table */
                if (!get_intervals)
                {
                    setme[got++] = b;
                }
                /* if intervals are requested two array entries are necessarry:
                   one for the interval's starting block and one for its end block */
                else if (got != 0 && setme[2 * got - 1] == b - 1 && b != first)
                {
                    /* expand the last interval */
                    ++setme[2 * got - 1];
                }
                else
                {
                    /* begin a new interval */
                    setme[2 * got] = b;
                    setme[2 * got + 1] = b;
                    ++got;
                }

                /* update our own tables */
                requestListAdd(s, b, peer);
                ++p->requestCount;
            }

            tr_ptrArrayDestruct(&peerArr, NULL);
        }
    }

    /* In most cases we've just changed the weights of a small number of pieces.
     * So rather than qsort()ing the entire array, it's faster to apply an
     * adaptive insertion sort algorithm. */
    if (got > 0)
    {
        /* not enough requests || last piece modified */
        if (checkedPieceCount == s->pieceCount)
        {
            --checkedPieceCount;
        }

        setComparePieceByWeightTorrent(s);

        for (int i = checkedPieceCount - 1; i >= 0; --i)
        {
            bool exact;

            /* relative position! */
            int const newpos = tr_lowerBound(&s->pieces[i], &s->pieces[i + 1], s->pieceCount - (i + 1),
                sizeof(struct weighted_piece), comparePieceByWeight, &exact);

            if (newpos > 0)
            {
                struct weighted_piece const piece = s->pieces[i];
                memmove(&s->pieces[i], &s->pieces[i + 1], sizeof(struct weighted_piece) * newpos);
                s->pieces[i + newpos] = piece;
            }
        }
    }

    assertWeightedPiecesAreSorted(t);
    *numgot = got;
}

bool tr_peerMgrDidPeerRequest(tr_torrent const* tor, tr_peer const* peer, tr_block_index_t block)
{
    return requestListLookup((tr_swarm*)tor->swarm, block, peer) != NULL;
}

/* cancel requests that are too old */
static void refillUpkeep(evutil_socket_t foo UNUSED, short bar UNUSED, void* vmgr)
{
    time_t now;
    time_t too_old;
    tr_torrent* tor;
    int cancel_buflen = 0;
    struct block_request* cancel = NULL;
    tr_peerMgr* mgr = vmgr;
    managerLock(mgr);

    now = tr_time();
    too_old = now - REQUEST_TTL_SECS;

    /* alloc the temporary "cancel" buffer */
    tor = NULL;

    while ((tor = tr_torrentNext(mgr->session, tor)) != NULL)
    {
        cancel_buflen = MAX(cancel_buflen, tor->swarm->requestCount);
    }

    if (cancel_buflen > 0)
    {
        cancel = tr_new(struct block_request, cancel_buflen);
    }

    /* prune requests that are too old */
    tor = NULL;

    while ((tor = tr_torrentNext(mgr->session, tor)) != NULL)
    {
        tr_swarm* s = tor->swarm;
        int const n = s->requestCount;

        if (n > 0)
        {
            int keepCount = 0;
            int cancelCount = 0;

            for (int i = 0; i < n; ++i)
            {
                struct block_request const* const request = &s->requests[i];
                tr_peerMsgs* msgs = PEER_MSGS(request->peer);

                if (msgs != NULL && request->sentAt <= too_old && !tr_peerMsgsIsReadingBlock(msgs, request->block))
                {
                    TR_ASSERT(cancel != NULL);
                    TR_ASSERT(cancelCount < cancel_buflen);

                    cancel[cancelCount++] = *request;
                }
                else
                {
                    if (i != keepCount)
                    {
                        s->requests[keepCount] = *request;
                    }

                    keepCount++;
                }
            }

            /* prune out the ones we aren't keeping */
            s->requestCount = keepCount;

            /* send cancel messages for all the "cancel" ones */
            for (int i = 0; i < cancelCount; ++i)
            {
                struct block_request const* const request = &cancel[i];
                tr_peerMsgs* msgs = PEER_MSGS(request->peer);

                if (msgs != NULL)
                {
                    tr_historyAdd(&request->peer->cancelsSentToPeer, now, 1);
                    tr_peerMsgsCancel(msgs, request->block);
                    decrementPendingReqCount(request);
                }
            }

            /* decrement the pending request counts for the timed-out blocks */
            for (int i = 0; i < cancelCount; ++i)
            {
                struct block_request const* const request = &cancel[i];

                pieceListRemoveRequest(s, request->block);
            }
        }
    }

    tr_free(cancel);
    tr_timerAddMsec(mgr->refillUpkeepTimer, REFILL_UPKEEP_PERIOD_MSEC);
    managerUnlock(mgr);
}

static void addStrike(tr_swarm* s, tr_peer* peer)
{
    tordbg(s, "increasing peer %s strike count to %d", tr_atomAddrStr(peer->atom), peer->strikes + 1);

    if (++peer->strikes >= MAX_BAD_PIECES_PER_PEER)
    {
        struct peer_atom* atom = peer->atom;
        atom->flags2 |= MYFLAG_BANNED;
        peer->doPurge = true;
        tordbg(s, "banning peer %s", tr_atomAddrStr(atom));
    }
}

static void peerSuggestedPiece(tr_swarm* s UNUSED, tr_peer* peer UNUSED, tr_piece_index_t pieceIndex UNUSED,
    int isFastAllowed UNUSED)
{
#if 0

    TR_ASSERT(t != NULL);
    TR_ASSERT(peer != NULL);
    TR_ASSERT(peer->msgs != NULL);

    /* is this a valid piece? */
    if (pieceIndex >= t->tor->info.pieceCount)
    {
        return;
    }

    /* don't ask for it if we've already got it */
    if (tr_torrentPieceIsComplete(t->tor, pieceIndex))
    {
        return;
    }

    /* don't ask for it if they don't have it */
    if (!tr_bitfieldHas(peer->have, pieceIndex))
    {
        return;
    }

    /* don't ask for it if we're choked and it's not fast */
    if (!isFastAllowed && peer->clientIsChoked)
    {
        return;
    }

    /* request the blocks that we don't have in this piece */
    {
        tr_block_index_t first;
        tr_block_index_t last;
        tr_torrent const* tor = t->tor;

        tr_torGetPieceBlockRange(t->tor, pieceIndex, &first, &last);

        for (tr_block_index_t b = first; b <= last; ++b)
        {
            if (tr_torrentBlockIsComplete(tor, b))
            {
                uint32_t const offset = getBlockOffsetInPiece(tor, b);
                uint32_t const length = tr_torBlockCountBytes(tor, b);
                tr_peerMsgsAddRequest(peer->msgs, pieceIndex, offset, length);
                incrementPieceRequests(t, pieceIndex);
            }
        }
    }

#endif
}

static void removeRequestFromTables(tr_swarm* s, tr_block_index_t block, tr_peer const* peer)
{
    requestListRemove(s, block, peer);
    pieceListRemoveRequest(s, block);
}

/* peer choked us, or maybe it disconnected.
   either way we need to remove all its requests */
static void peerDeclinedAllRequests(tr_swarm* s, tr_peer const* peer)
{
    int n = 0;
    tr_block_index_t* blocks = tr_new(tr_block_index_t, s->requestCount);

    for (int i = 0; i < s->requestCount; ++i)
    {
        if (peer == s->requests[i].peer)
        {
            blocks[n++] = s->requests[i].block;
        }
    }

    for (int i = 0; i < n; ++i)
    {
        removeRequestFromTables(s, blocks[i], peer);
    }

    tr_free(blocks);
}

static void cancelAllRequestsForBlock(tr_swarm* s, tr_block_index_t block, tr_peer* no_notify)
{
    int peerCount;
    tr_peer** peers;
    tr_ptrArray peerArr;

    peerArr = TR_PTR_ARRAY_INIT;
    getBlockRequestPeers(s, block, &peerArr);
    peers = (tr_peer**)tr_ptrArrayPeek(&peerArr, &peerCount);

    for (int i = 0; i < peerCount; ++i)
    {
        tr_peer* p = peers[i];

        if (p != no_notify && tr_isPeerMsgs(p))
        {
            tr_historyAdd(&p->cancelsSentToPeer, tr_time(), 1);
            tr_peerMsgsCancel(PEER_MSGS(p), block);
        }

        removeRequestFromTables(s, block, p);
    }

    tr_ptrArrayDestruct(&peerArr, NULL);
}

void tr_peerMgrPieceCompleted(tr_torrent* tor, tr_piece_index_t p)
{
    bool pieceCameFromPeers = false;
    tr_swarm* const s = tor->swarm;

    /* walk through our peers */
    for (int i = 0, n = tr_ptrArraySize(&s->peers); i < n; ++i)
    {
        tr_peer* peer = tr_ptrArrayNth(&s->peers, i);

        /* notify the peer that we now have this piece */
        tr_peerMsgsHave(PEER_MSGS(peer), p);

        if (!pieceCameFromPeers)
        {
            pieceCameFromPeers = tr_bitfieldHas(&peer->blame, p);
        }
    }

    if (pieceCameFromPeers) /* webseed downloads don't belong in announce totals */
    {
        tr_announcerAddBytes(tor, TR_ANN_DOWN, tr_torPieceCountBytes(tor, p));
    }

    /* bookkeeping */
    pieceListRemovePiece(s, p);
    s->needsCompletenessCheck = true;
}

static void peerCallbackFunc(tr_peer* peer, tr_peer_event const* e, void* vs)
{
    TR_ASSERT(peer != NULL);

    tr_swarm* s = vs;

    swarmLock(s);

    switch (e->eventType)
    {
    case TR_PEER_PEER_GOT_PIECE_DATA:
        {
            time_t const now = tr_time();
            tr_torrent* tor = s->tor;

            tor->uploadedCur += e->length;
            tr_announcerAddBytes(tor, TR_ANN_UP, e->length);
            tr_torrentSetDateActive(tor, now);
            tr_torrentSetDirty(tor);
            tr_statsAddUploaded(tor->session, e->length);

            if (peer->atom != NULL)
            {
                peer->atom->piece_data_time = now;
            }

            break;
        }

    case TR_PEER_CLIENT_GOT_PIECE_DATA:
        {
            time_t const now = tr_time();
            tr_torrent* tor = s->tor;

            tor->downloadedCur += e->length;
            tr_torrentSetDateActive(tor, now);
            tr_torrentSetDirty(tor);

            tr_statsAddDownloaded(tor->session, e->length);

            if (peer->atom != NULL)
            {
                peer->atom->piece_data_time = now;
            }

            break;
        }

    case TR_PEER_CLIENT_GOT_HAVE:
        if (replicationExists(s))
        {
            tr_incrReplicationOfPiece(s, e->pieceIndex);
            assertReplicationCountIsExact(s);
        }

        break;

    case TR_PEER_CLIENT_GOT_HAVE_ALL:
        if (replicationExists(s))
        {
            tr_incrReplication(s);
            assertReplicationCountIsExact(s);
        }

        break;

    case TR_PEER_CLIENT_GOT_HAVE_NONE:
        /* noop */
        break;

    case TR_PEER_CLIENT_GOT_BITFIELD:
        TR_ASSERT(e->bitfield != NULL);

        if (replicationExists(s))
        {
            tr_incrReplicationFromBitfield(s, e->bitfield);
            assertReplicationCountIsExact(s);
        }

        break;

    case TR_PEER_CLIENT_GOT_REJ:
        {
            tr_block_index_t b = _tr_block(s->tor, e->pieceIndex, e->offset);

            if (b < s->tor->blockCount)
            {
                removeRequestFromTables(s, b, peer);
            }
            else
            {
                tordbg(s, "Peer %s sent an out-of-range reject message", tr_atomAddrStr(peer->atom));
            }

            break;
        }

    case TR_PEER_CLIENT_GOT_CHOKE:
        peerDeclinedAllRequests(s, peer);
        break;

    case TR_PEER_CLIENT_GOT_PORT:
        if (peer->atom != NULL)
        {
            peer->atom->port = e->port;
        }

        break;

    case TR_PEER_CLIENT_GOT_SUGGEST:
        peerSuggestedPiece(s, peer, e->pieceIndex, false);
        break;

    case TR_PEER_CLIENT_GOT_ALLOWED_FAST:
        peerSuggestedPiece(s, peer, e->pieceIndex, true);
        break;

    case TR_PEER_CLIENT_GOT_BLOCK:
        {
            tr_torrent* tor = s->tor;
            tr_piece_index_t const p = e->pieceIndex;
            tr_block_index_t const block = _tr_block(tor, p, e->offset);
            cancelAllRequestsForBlock(s, block, peer);
            tr_historyAdd(&peer->blocksSentToClient, tr_time(), 1);
            pieceListResortPiece(s, pieceListLookup(s, p));
            tr_torrentGotBlock(tor, block);
            break;
        }

    case TR_PEER_ERROR:
        if (e->err == ERANGE || e->err == EMSGSIZE || e->err == ENOTCONN)
        {
            /* some protocol error from the peer */
            peer->doPurge = true;
            tordbg(s, "setting %s doPurge flag because we got an ERANGE, EMSGSIZE, or ENOTCONN error", tr_atomAddrStr(
                peer->atom));
        }
        else
        {
            tordbg(s, "unhandled error: %s", tr_strerror(e->err));
        }

        break;

    default:
        TR_ASSERT_MSG(false, "unhandled peer event type %d", (int)e->eventType);
    }

    swarmUnlock(s);
}

static int getDefaultShelfLife(uint8_t from)
{
    /* in general, peers obtained from firsthand contact
     * are better than those from secondhand, etc etc */
    switch (from)
    {
    case TR_PEER_FROM_INCOMING:
        return 60 * 60 * 6;

    case TR_PEER_FROM_LTEP:
        return 60 * 60 * 6;

    case TR_PEER_FROM_TRACKER:
        return 60 * 60 * 3;

    case TR_PEER_FROM_DHT:
        return 60 * 60 * 3;

    case TR_PEER_FROM_PEX:
        return 60 * 60 * 2;

    case TR_PEER_FROM_RESUME:
        return 60 * 60;

    case TR_PEER_FROM_LPD:
        return 10 * 60;

    default:
        return 60 * 60;
    }
}

static void ensureAtomExists(tr_swarm* s, tr_address const* addr, tr_port const port, uint8_t const flags,
    int8_t const seedProbability, uint8_t const from)
{
    TR_ASSERT(tr_address_is_valid(addr));
    TR_ASSERT(from < TR_PEER_FROM__MAX);

    struct peer_atom* a = getExistingAtom(s, addr);

    if (a == NULL)
    {
        int const jitter = tr_rand_int_weak(60 * 10);
        a = tr_new0(struct peer_atom, 1);
        a->addr = *addr;
        a->port = port;
        a->flags = flags;
        a->fromFirst = from;
        a->fromBest = from;
        a->shelf_date = tr_time() + getDefaultShelfLife(from) + jitter;
        a->blocklisted = -1;
        atomSetSeedProbability(a, seedProbability);
        tr_ptrArrayInsertSorted(&s->pool, a, compareAtomsByAddress);

        tordbg(s, "got a new atom: %s", tr_atomAddrStr(a));
    }
    else
    {
        if (from < a->fromBest)
        {
            a->fromBest = from;
        }

        if (a->seedProbability == -1)
        {
            atomSetSeedProbability(a, seedProbability);
        }

        a->flags |= flags;
    }
}

static int getMaxPeerCount(tr_torrent const* tor)
{
    return tor->maxConnectedPeers;
}

static int getPeerCount(tr_swarm const* s)
{
    return tr_ptrArraySize(&s->peers); /* + tr_ptrArraySize(&t->outgoingHandshakes); */
}

static void createBitTorrentPeer(tr_torrent* tor, struct tr_peerIo* io, struct peer_atom* atom, tr_quark client)
{
    TR_ASSERT(atom != NULL);
    TR_ASSERT(tr_isTorrent(tor));
    TR_ASSERT(tor->swarm != NULL);

    tr_swarm* swarm = tor->swarm;

    tr_peer* peer = (tr_peer*)tr_peerMsgsNew(tor, io, peerCallbackFunc, swarm);
    peer->atom = atom;
    peer->client = client;
    atom->peer = peer;

    tr_ptrArrayInsertSorted(&swarm->peers, peer, peerCompare);
    ++swarm->stats.peerCount;
    ++swarm->stats.peerFromCount[atom->fromFirst];

    TR_ASSERT(swarm->stats.peerCount == tr_ptrArraySize(&swarm->peers));
    TR_ASSERT(swarm->stats.peerFromCount[atom->fromFirst] <= swarm->stats.peerCount);

    tr_peerMsgs* msgs = PEER_MSGS(peer);
    tr_peerMsgsUpdateActive(msgs, TR_UP);
    tr_peerMsgsUpdateActive(msgs, TR_DOWN);
}

/* FIXME: this is kind of a mess. */
static bool myHandshakeDoneCB(tr_handshake* handshake, tr_peerIo* io, bool readAnythingFromPeer, bool isConnected,
    uint8_t const* peer_id, void* vmanager)
{
    TR_ASSERT(io != NULL);

    bool ok = isConnected;
    bool success = false;
    tr_port port;
    tr_address const* addr;
    tr_peerMgr* manager = vmanager;
    tr_swarm* s = tr_peerIoHasTorrentHash(io) ? getExistingSwarm(manager, tr_peerIoGetTorrentHash(io)) : NULL;

    if (tr_peerIoIsIncoming(io))
    {
        tr_ptrArrayRemoveSortedPointer(&manager->incomingHandshakes, handshake, handshakeCompare);
    }
    else if (s != NULL)
    {
        tr_ptrArrayRemoveSortedPointer(&s->outgoingHandshakes, handshake, handshakeCompare);
    }

    if (s != NULL)
    {
        swarmLock(s);
    }

    addr = tr_peerIoGetAddress(io, &port);

    if (!ok || s == NULL || !s->isRunning)
    {
        if (s != NULL)
        {
            struct peer_atom* atom = getExistingAtom(s, addr);

            if (atom != NULL)
            {
                ++atom->numFails;

                if (!readAnythingFromPeer)
                {
                    tordbg(s, "marking peer %s as unreachable... numFails is %d", tr_atomAddrStr(atom), (int)atom->numFails);
                    atom->flags2 |= MYFLAG_UNREACHABLE;
                }
            }
        }
    }
    else /* looking good */
    {
        struct peer_atom* atom;

        ensureAtomExists(s, addr, port, 0, -1, TR_PEER_FROM_INCOMING);
        atom = getExistingAtom(s, addr);

        TR_ASSERT(atom != NULL);

        atom->time = tr_time();
        atom->piece_data_time = 0;
        atom->lastConnectionAt = tr_time();

        if (!tr_peerIoIsIncoming(io))
        {
            atom->flags |= ADDED_F_CONNECTABLE;
            atom->flags2 &= ~MYFLAG_UNREACHABLE;
        }

        /* In principle, this flag specifies whether the peer groks uTP,
           not whether it's currently connected over uTP. */
        if (io->socket.type == TR_PEER_SOCKET_TYPE_UTP)
        {
            atom->flags |= ADDED_F_UTP_FLAGS;
        }

        if ((atom->flags2 & MYFLAG_BANNED) != 0)
        {
            tordbg(s, "banned peer %s tried to reconnect", tr_atomAddrStr(atom));
        }
        else if (tr_peerIoIsIncoming(io) && getPeerCount(s) >= getMaxPeerCount(s->tor))
        {
        }
        else
        {
            tr_peer* peer = atom->peer;

            if (peer != NULL)
            {
                /* we already have this peer */
            }
            else
            {
                tr_quark client;
                tr_peerIo* io;
                char buf[128];

                if (peer_id != NULL)
                {
                    client = tr_quark_new(tr_clientForId(buf, sizeof(buf), peer_id), TR_BAD_SIZE);
                }
                else
                {
                    client = TR_KEY_NONE;
                }

                io = tr_handshakeStealIO(handshake); /* this steals its refcount too, which is balanced by our unref in peerDelete() */
                tr_peerIoSetParent(io, &s->tor->bandwidth);
                createBitTorrentPeer(s->tor, io, atom, client);

                success = true;
            }
        }
    }

    if (s != NULL)
    {
        swarmUnlock(s);
    }

    return success;
}

static void close_peer_socket(struct tr_peer_socket const socket, tr_session* session)
{
    switch (socket.type)
    {
    case TR_PEER_SOCKET_TYPE_NONE:
        break;

    case TR_PEER_SOCKET_TYPE_TCP:
        tr_netClose(session, socket.handle.tcp);
        break;

#ifdef WITH_UTP

    case TR_PEER_SOCKET_TYPE_UTP:
        UTP_Close(socket.handle.utp);
        break;

#endif

    default:
        TR_ASSERT_MSG(false, "unsupported peer socket type %d", socket.type);
    }
}

void tr_peerMgrAddIncoming(tr_peerMgr* manager, tr_address* addr, tr_port port, struct tr_peer_socket const socket)
{
    TR_ASSERT(tr_isSession(manager->session));

    managerLock(manager);

    tr_session* session = manager->session;

    if (tr_sessionIsAddressBlocked(session, addr))
    {
        tr_logAddDebug("Banned IP address \"%s\" tried to connect to us", tr_address_to_string(addr));
        close_peer_socket(socket, session);
    }
    else if (getExistingHandshake(&manager->incomingHandshakes, addr) != NULL)
    {
        close_peer_socket(socket, session);
    }
    else /* we don't have a connection to them yet... */
    {
        tr_peerIo* io;
        tr_handshake* handshake;

        io = tr_peerIoNewIncoming(session, &session->bandwidth, addr, port, socket);

        handshake = tr_handshakeNew(io, session->encryptionMode, myHandshakeDoneCB, manager);

        tr_peerIoUnref(io); /* balanced by the implicit ref in tr_peerIoNewIncoming() */

        tr_ptrArrayInsertSorted(&manager->incomingHandshakes, handshake, handshakeCompare);
    }

    managerUnlock(manager);
}

void tr_peerMgrAddPex(tr_torrent* tor, uint8_t from, tr_pex const* pex, int8_t seedProbability)
{
    if (tr_isPex(pex)) /* safeguard against corrupt data */
    {
        tr_swarm* s = tor->swarm;
        managerLock(s->manager);

        if (!tr_sessionIsAddressBlocked(s->manager->session, &pex->addr))
        {
            if (tr_address_is_valid_for_peers(&pex->addr, pex->port))
            {
                ensureAtomExists(s, &pex->addr, pex->port, pex->flags, seedProbability, from);
            }
        }

        managerUnlock(s->manager);
    }
}

tr_pex* tr_peerMgrCompactToPex(void const* compact, size_t compactLen, uint8_t const* added_f, size_t added_f_len,
    size_t* pexCount)
{
    size_t n = compactLen / 6;
    uint8_t const* walk = compact;
    tr_pex* pex = tr_new0(tr_pex, n);

    for (size_t i = 0; i < n; ++i)
    {
        pex[i].addr.type = TR_AF_INET;
        memcpy(&pex[i].addr.addr, walk, 4);
        walk += 4;
        memcpy(&pex[i].port, walk, 2);
        walk += 2;

        if (added_f != NULL && n == added_f_len)
        {
            pex[i].flags = added_f[i];
        }
    }

    *pexCount = n;
    return pex;
}

tr_pex* tr_peerMgrCompact6ToPex(void const* compact, size_t compactLen, uint8_t const* added_f, size_t added_f_len,
    size_t* pexCount)
{
    size_t n = compactLen / 18;
    uint8_t const* walk = compact;
    tr_pex* pex = tr_new0(tr_pex, n);

    for (size_t i = 0; i < n; ++i)
    {
        pex[i].addr.type = TR_AF_INET6;
        memcpy(&pex[i].addr.addr.addr6.s6_addr, walk, 16);
        walk += 16;
        memcpy(&pex[i].port, walk, 2);
        walk += 2;

        if (added_f != NULL && n == added_f_len)
        {
            pex[i].flags = added_f[i];
        }
    }

    *pexCount = n;
    return pex;
}

/**
***
**/

void tr_peerMgrGotBadPiece(tr_torrent* tor, tr_piece_index_t pieceIndex)
{
    tr_swarm* s = tor->swarm;
    uint32_t const byteCount = tr_torPieceCountBytes(tor, pieceIndex);

    for (int i = 0, n = tr_ptrArraySize(&s->peers); i != n; ++i)
    {
        tr_peer* peer = tr_ptrArrayNth(&s->peers, i);

        if (tr_bitfieldHas(&peer->blame, pieceIndex))
        {
            tordbg(s, "peer %s contributed to corrupt piece (%d); now has %d strikes", tr_atomAddrStr(peer->atom), pieceIndex,
                (int)peer->strikes + 1);
            addStrike(s, peer);
        }
    }

    tr_announcerAddBytes(tor, TR_ANN_CORRUPT, byteCount);
}

int tr_pexCompare(void const* va, void const* vb)
{
    tr_pex const* a = va;
    tr_pex const* b = vb;

    TR_ASSERT(tr_isPex(a));
    TR_ASSERT(tr_isPex(b));

    int i;

    if ((i = tr_address_compare(&a->addr, &b->addr)) != 0)
    {
        return i;
    }

    if (a->port != b->port)
    {
        return a->port < b->port ? -1 : 1;
    }

    return 0;
}

/* better goes first */
static int compareAtomsByUsefulness(void const* va, void const* vb)
{
    struct peer_atom const* a = *(struct peer_atom const* const*)va;
    struct peer_atom const* b = *(struct peer_atom const* const*)vb;

    TR_ASSERT(tr_isAtom(a));
    TR_ASSERT(tr_isAtom(b));

    if (a->piece_data_time != b->piece_data_time)
    {
        return a->piece_data_time > b->piece_data_time ? -1 : 1;
    }

    if (a->fromBest != b->fromBest)
    {
        return a->fromBest < b->fromBest ? -1 : 1;
    }

    if (a->numFails != b->numFails)
    {
        return a->numFails < b->numFails ? -1 : 1;
    }

    return 0;
}

static bool isAtomInteresting(tr_torrent const* tor, struct peer_atom* atom)
{
    if (tr_torrentIsSeed(tor) && atomIsSeed(atom))
    {
        return false;
    }

    if (peerIsInUse(tor->swarm, atom))
    {
        return true;
    }

    if (isAtomBlocklisted(tor->session, atom))
    {
        return false;
    }

    if ((atom->flags2 & MYFLAG_BANNED) != 0)
    {
        return false;
    }

    return true;
}

int tr_peerMgrGetPeers(tr_torrent* tor, tr_pex** setme_pex, uint8_t af, uint8_t list_mode, int maxCount)
{
    TR_ASSERT(tr_isTorrent(tor));
    TR_ASSERT(setme_pex != NULL);
    TR_ASSERT(af == TR_AF_INET || af == TR_AF_INET6);
    TR_ASSERT(list_mode == TR_PEERS_CONNECTED || list_mode == TR_PEERS_INTERESTING);

    int n;
    int count = 0;
    int atomCount = 0;
    tr_swarm const* s = tor->swarm;
    struct peer_atom** atoms = NULL;
    tr_pex* pex;
    tr_pex* walk;

    managerLock(s->manager);

    /**
    ***  build a list of atoms
    **/

    if (list_mode == TR_PEERS_CONNECTED) /* connected peers only */
    {
        tr_peer const** peers = (tr_peer const**)tr_ptrArrayBase(&s->peers);
        atomCount = tr_ptrArraySize(&s->peers);
        atoms = tr_new(struct peer_atom*, atomCount);

        for (int i = 0; i < atomCount; ++i)
        {
            atoms[i] = peers[i]->atom;
        }
    }
    else /* TR_PEERS_INTERESTING */
    {
        struct peer_atom** atomBase = (struct peer_atom**)tr_ptrArrayBase(&s->pool);
        n = tr_ptrArraySize(&s->pool);
        atoms = tr_new(struct peer_atom*, n);

        for (int i = 0; i < n; ++i)
        {
            if (isAtomInteresting(tor, atomBase[i]))
            {
                atoms[atomCount++] = atomBase[i];
            }
        }
    }

    qsort(atoms, atomCount, sizeof(struct peer_atom*), compareAtomsByUsefulness);

    /**
    ***  add the first N of them into our return list
    **/

    n = MIN(atomCount, maxCount);
    pex = walk = tr_new0(tr_pex, n);

    for (int i = 0; i < atomCount && count < n; ++i)
    {
        struct peer_atom const* atom = atoms[i];

        if (atom->addr.type == af)
        {
            TR_ASSERT(tr_address_is_valid(&atom->addr));

            walk->addr = atom->addr;
            walk->port = atom->port;
            walk->flags = atom->flags;
            ++count;
            ++walk;
        }
    }

    qsort(pex, count, sizeof(tr_pex), tr_pexCompare);

    TR_ASSERT(walk - pex == count);
    *setme_pex = pex;

    /* cleanup */
    tr_free(atoms);
    managerUnlock(s->manager);
    return count;
}

static void atomPulse(evutil_socket_t, short, void*);
static void bandwidthPulse(evutil_socket_t, short, void*);
static void rechokePulse(evutil_socket_t, short, void*);
static void reconnectPulse(evutil_socket_t, short, void*);

static struct event* createTimer(tr_session* session, int msec, event_callback_fn callback, void* cbdata)
{
    struct event* timer = evtimer_new(session->event_base, callback, cbdata);
    tr_timerAddMsec(timer, msec);
    return timer;
}

static void ensureMgrTimersExist(struct tr_peerMgr* m)
{
    if (m->atomTimer == NULL)
    {
        m->atomTimer = createTimer(m->session, ATOM_PERIOD_MSEC, atomPulse, m);
    }

    if (m->bandwidthTimer == NULL)
    {
        m->bandwidthTimer = createTimer(m->session, BANDWIDTH_PERIOD_MSEC, bandwidthPulse, m);
    }

    if (m->rechokeTimer == NULL)
    {
        m->rechokeTimer = createTimer(m->session, RECHOKE_PERIOD_MSEC, rechokePulse, m);
    }

    if (m->refillUpkeepTimer == NULL)
    {
        m->refillUpkeepTimer = createTimer(m->session, REFILL_UPKEEP_PERIOD_MSEC, refillUpkeep, m);
    }
}

void tr_peerMgrStartTorrent(tr_torrent* tor)
{
    TR_ASSERT(tr_isTorrent(tor));
    TR_ASSERT(tr_torrentIsLocked(tor));

    tr_swarm* s = tor->swarm;

    ensureMgrTimersExist(s->manager);

    s->isRunning = true;
    s->maxPeers = tor->maxConnectedPeers;
    s->pieceSortState = PIECES_UNSORTED;

    rechokePulse(0, 0, s->manager);
}

static void removeAllPeers(tr_swarm*);

static void stopSwarm(tr_swarm* swarm)
{
    swarm->isRunning = false;

    replicationFree(swarm);
    invalidatePieceSorting(swarm);

    removeAllPeers(swarm);

    /* disconnect the handshakes. handshakeAbort calls handshakeDoneCB(),
     * which removes the handshake from t->outgoingHandshakes... */
    while (!tr_ptrArrayEmpty(&swarm->outgoingHandshakes))
    {
        tr_handshakeAbort(tr_ptrArrayNth(&swarm->outgoingHandshakes, 0));
    }
}

void tr_peerMgrStopTorrent(tr_torrent* tor)
{
    TR_ASSERT(tr_isTorrent(tor));
    TR_ASSERT(tr_torrentIsLocked(tor));

    stopSwarm(tor->swarm);
}

void tr_peerMgrAddTorrent(tr_peerMgr* manager, tr_torrent* tor)
{
    TR_ASSERT(tr_isTorrent(tor));
    TR_ASSERT(tr_torrentIsLocked(tor));
    TR_ASSERT(tor->swarm == NULL);

    tor->swarm = swarmNew(manager, tor);
}

void tr_peerMgrRemoveTorrent(tr_torrent* tor)
{
    TR_ASSERT(tr_isTorrent(tor));
    TR_ASSERT(tr_torrentIsLocked(tor));

    stopSwarm(tor->swarm);
    swarmFree(tor->swarm);
}

void tr_peerUpdateProgress(tr_torrent* tor, tr_peer* peer)
{
    tr_bitfield const* have = &peer->have;

    if (tr_bitfieldHasAll(have))
    {
        peer->progress = 1.0;
    }
    else if (tr_bitfieldHasNone(have))
    {
        peer->progress = 0.0;
    }
    else
    {
        float const true_count = tr_bitfieldCountTrueBits(have);

        if (tr_torrentHasMetadata(tor))
        {
            peer->progress = true_count / tor->info.pieceCount;
        }
        else /* without pieceCount, this result is only a best guess... */
        {
            peer->progress = true_count / (have->bit_count + 1);
        }
    }

    /* clamp the progress range */
    if (peer->progress < 0.0)
    {
        peer->progress = 0.0;
    }

    if (peer->progress > 1.0)
    {
        peer->progress = 1.0;
    }

    if (peer->atom != NULL && peer->progress >= 1.0)
    {
        atomSetSeed(tor->swarm, peer->atom);
    }
}

void tr_peerMgrOnTorrentGotMetainfo(tr_torrent* tor)
{
    int peerCount;
    tr_peer** peers;

    /* the webseed list may have changed... */
    rebuildWebseedArray(tor->swarm, tor);

    /* some peer_msgs' progress fields may not be accurate if we
       didn't have the metadata before now... so refresh them all... */
    peerCount = tr_ptrArraySize(&tor->swarm->peers);
    peers = (tr_peer**)tr_ptrArrayBase(&tor->swarm->peers);

    for (int i = 0; i < peerCount; ++i)
    {
        tr_peerUpdateProgress(tor, peers[i]);
    }

    /* update the bittorrent peers' willingnes... */
    for (int i = 0; i < peerCount; ++i)
    {
        tr_peerMsgsUpdateActive(tr_peerMsgsCast(peers[i]), TR_UP);
        tr_peerMsgsUpdateActive(tr_peerMsgsCast(peers[i]), TR_DOWN);
    }
}

void tr_peerMgrTorrentAvailability(tr_torrent const* tor, int8_t* tab, unsigned int tabCount)
{
    TR_ASSERT(tr_isTorrent(tor));
    TR_ASSERT(tab != NULL);
    TR_ASSERT(tabCount > 0);

    memset(tab, 0, tabCount);

    if (tr_torrentHasMetadata(tor))
    {
        int const peerCount = tr_ptrArraySize(&tor->swarm->peers);
        tr_peer const** peers = (tr_peer const**)tr_ptrArrayBase(&tor->swarm->peers);
        float const interval = tor->info.pieceCount / (float)tabCount;
        bool const isSeed = tr_torrentGetCompleteness(tor) == TR_SEED;

        for (tr_piece_index_t i = 0; i < tabCount; ++i)
        {
            int const piece = i * interval;

            if (isSeed || tr_torrentPieceIsComplete(tor, piece))
            {
                tab[i] = -1;
            }
            else if (peerCount != 0)
            {
                for (int j = 0; j < peerCount; ++j)
                {
                    if (tr_bitfieldHas(&peers[j]->have, piece))
                    {
                        ++tab[i];
                    }
                }
            }
        }
    }
}

void tr_swarmGetStats(tr_swarm const* swarm, tr_swarm_stats* setme)
{
    TR_ASSERT(swarm != NULL);
    TR_ASSERT(setme != NULL);

    *setme = swarm->stats;
}

void tr_swarmIncrementActivePeers(tr_swarm* swarm, tr_direction direction, bool is_active)
{
    int n = swarm->stats.activePeerCount[direction];

    if (is_active)
    {
        ++n;
    }
    else
    {
        --n;
    }

    TR_ASSERT(n >= 0);
    TR_ASSERT(n <= swarm->stats.peerCount);

    swarm->stats.activePeerCount[direction] = n;
}

bool tr_peerIsSeed(tr_peer const* peer)
{
    if (peer->progress >= 1.0)
    {
        return true;
    }

    if (peer->atom != NULL && atomIsSeed(peer->atom))
    {
        return true;
    }

    return false;
}

/* count how many bytes we want that connected peers have */
uint64_t tr_peerMgrGetDesiredAvailable(tr_torrent const* tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    /* common shortcuts... */

    if (!tor->isRunning || tor->isStopping || tr_torrentIsSeed(tor) || !tr_torrentHasMetadata(tor))
    {
        return 0;
    }

    tr_swarm const* s = tor->swarm;

    if (s == NULL || !s->isRunning)
    {
        return 0;
    }

    size_t const n = tr_ptrArraySize(&s->peers);

    if (n == 0)
    {
        return 0;
    }
    else
    {
        tr_peer const** peers = (tr_peer const**)tr_ptrArrayBase(&s->peers);

        for (size_t i = 0; i < n; ++i)
        {
            if (peers[i]->atom != NULL && atomIsSeed(peers[i]->atom))
            {
                return tr_torrentGetLeftUntilDone(tor);
            }
        }
    }

    if (s->pieceReplication == NULL || s->pieceReplicationSize == 0)
    {
        return 0;
    }

    /* do it the hard way */

    uint64_t desiredAvailable = 0;

    for (size_t i = 0, n = MIN(tor->info.pieceCount, s->pieceReplicationSize); i < n; ++i)
    {
        if (!tor->info.pieces[i].dnd && s->pieceReplication[i] > 0)
        {
            desiredAvailable += tr_torrentMissingBytesInPiece(tor, i);
        }
    }

    TR_ASSERT(desiredAvailable <= tor->info.totalSize);
    return desiredAvailable;
}

double* tr_peerMgrWebSpeeds_KBps(tr_torrent const* tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    uint64_t const now = tr_time_msec();

    tr_swarm* s = tor->swarm;
    TR_ASSERT(s->manager != NULL);

    unsigned int n = tr_ptrArraySize(&s->webseeds);
    TR_ASSERT(n == tor->info.webseedCount);

    double* ret = tr_new0(double, n);

    for (unsigned int i = 0; i < n; ++i)
    {
        unsigned int Bps = 0;

        if (tr_peerIsTransferringPieces(tr_ptrArrayNth(&s->webseeds, i), now, TR_DOWN, &Bps))
        {
            ret[i] = Bps / (double)tr_speed_K;
        }
        else
        {
            ret[i] = -1.0;
        }
    }

    return ret;
}

struct tr_peer_stat* tr_peerMgrPeerStats(tr_torrent const* tor, int* setmeCount)
{
    TR_ASSERT(tr_isTorrent(tor));
    TR_ASSERT(tor->swarm->manager != NULL);

    time_t const now = tr_time();
    uint64_t const now_msec = tr_time_msec();

    tr_swarm const* s = tor->swarm;
    tr_peer** peers = (tr_peer**)tr_ptrArrayBase(&s->peers);
    int size = tr_ptrArraySize(&s->peers);
    tr_peer_stat* ret = tr_new0(tr_peer_stat, size);

    for (int i = 0; i < size; ++i)
    {
        char* pch;
        tr_peer* peer = peers[i];
        tr_peerMsgs* msgs = PEER_MSGS(peer);
        struct peer_atom const* atom = peer->atom;
        tr_peer_stat* stat = ret + i;

        tr_address_to_string_with_buf(&atom->addr, stat->addr, sizeof(stat->addr));
        tr_strlcpy(stat->client, tr_quark_get_string(peer->client, NULL), sizeof(stat->client));
        stat->port = ntohs(peer->atom->port);
        stat->from = atom->fromFirst;
        stat->progress = peer->progress;
        stat->isUTP = tr_peerMsgsIsUtpConnection(msgs);
        stat->isEncrypted = tr_peerMsgsIsEncrypted(msgs);
        stat->rateToPeer_KBps = toSpeedKBps(tr_peerGetPieceSpeed_Bps(peer, now_msec, TR_CLIENT_TO_PEER));
        stat->rateToClient_KBps = toSpeedKBps(tr_peerGetPieceSpeed_Bps(peer, now_msec, TR_PEER_TO_CLIENT));
        stat->peerIsChoked = tr_peerMsgsIsPeerChoked(msgs);
        stat->peerIsInterested = tr_peerMsgsIsPeerInterested(msgs);
        stat->clientIsChoked = tr_peerMsgsIsClientChoked(msgs);
        stat->clientIsInterested = tr_peerMsgsIsClientInterested(msgs);
        stat->isIncoming = tr_peerMsgsIsIncomingConnection(msgs);
        stat->isDownloadingFrom = tr_peerMsgsIsActive(msgs, TR_PEER_TO_CLIENT);
        stat->isUploadingTo = tr_peerMsgsIsActive(msgs, TR_CLIENT_TO_PEER);
        stat->isSeed = tr_peerIsSeed(peer);

        stat->blocksToPeer = tr_historyGet(&peer->blocksSentToPeer, now, CANCEL_HISTORY_SEC);
        stat->blocksToClient = tr_historyGet(&peer->blocksSentToClient, now, CANCEL_HISTORY_SEC);
        stat->cancelsToPeer = tr_historyGet(&peer->cancelsSentToPeer, now, CANCEL_HISTORY_SEC);
        stat->cancelsToClient = tr_historyGet(&peer->cancelsSentToClient, now, CANCEL_HISTORY_SEC);

        stat->pendingReqsToPeer = peer->pendingReqsToPeer;
        stat->pendingReqsToClient = peer->pendingReqsToClient;

        pch = stat->flagStr;

        if (stat->isUTP)
        {
            *pch++ = 'T';
        }

        if (s->optimistic == msgs)
        {
            *pch++ = 'O';
        }

        if (stat->isDownloadingFrom)
        {
            *pch++ = 'D';
        }
        else if (stat->clientIsInterested)
        {
            *pch++ = 'd';
        }

        if (stat->isUploadingTo)
        {
            *pch++ = 'U';
        }
        else if (stat->peerIsInterested)
        {
            *pch++ = 'u';
        }

        if (!stat->clientIsChoked && !stat->clientIsInterested)
        {
            *pch++ = 'K';
        }

        if (!stat->peerIsChoked && !stat->peerIsInterested)
        {
            *pch++ = '?';
        }

        if (stat->isEncrypted)
        {
            *pch++ = 'E';
        }

        if (stat->from == TR_PEER_FROM_DHT)
        {
            *pch++ = 'H';
        }
        else if (stat->from == TR_PEER_FROM_PEX)
        {
            *pch++ = 'X';
        }

        if (stat->isIncoming)
        {
            *pch++ = 'I';
        }

        *pch = '\0';
    }

    *setmeCount = size;
    return ret;
}

/***
****
****
***/

void tr_peerMgrClearInterest(tr_torrent* tor)
{
    TR_ASSERT(tr_isTorrent(tor));
    TR_ASSERT(tr_torrentIsLocked(tor));

    tr_swarm* s = tor->swarm;
    int const peerCount = tr_ptrArraySize(&s->peers);

    for (int i = 0; i < peerCount; ++i)
    {
        tr_peerMsgsSetInterested(tr_ptrArrayNth(&s->peers, i), false);
    }
}

/* does this peer have any pieces that we want? */
static bool isPeerInteresting(tr_torrent* const tor, bool const* const piece_is_interesting, tr_peer const* const peer)
{
    /* these cases should have already been handled by the calling code... */
    TR_ASSERT(!tr_torrentIsSeed(tor));
    TR_ASSERT(tr_torrentIsPieceTransferAllowed(tor, TR_PEER_TO_CLIENT));

    if (tr_peerIsSeed(peer))
    {
        return true;
    }

    for (tr_piece_index_t i = 0; i < tor->info.pieceCount; ++i)
    {
        if (piece_is_interesting[i] && tr_bitfieldHas(&peer->have, i))
        {
            return true;
        }
    }

    return false;
}

typedef enum
{
    RECHOKE_STATE_GOOD,
    RECHOKE_STATE_UNTESTED,
    RECHOKE_STATE_BAD
}
tr_rechoke_state;

struct tr_rechoke_info
{
    tr_peer* peer;
    int salt;
    int rechoke_state;
};

static int compare_rechoke_info(void const* va, void const* vb)
{
    struct tr_rechoke_info const* a = va;
    struct tr_rechoke_info const* b = vb;

    if (a->rechoke_state != b->rechoke_state)
    {
        return a->rechoke_state - b->rechoke_state;
    }

    return a->salt - b->salt;
}

/* determines who we send "interested" messages to */
static void rechokeDownloads(tr_swarm* s)
{
    int maxPeers = 0;
    int rechoke_count = 0;
    struct tr_rechoke_info* rechoke = NULL;
    int const MIN_INTERESTING_PEERS = 5;
    int const peerCount = tr_ptrArraySize(&s->peers);
    time_t const now = tr_time();

    /* some cases where this function isn't necessary */
    if (tr_torrentIsSeed(s->tor))
    {
        return;
    }

    if (!tr_torrentIsPieceTransferAllowed(s->tor, TR_PEER_TO_CLIENT))
    {
        return;
    }

    /* decide HOW MANY peers to be interested in */
    {
        int blocks = 0;
        int cancels = 0;
        time_t timeSinceCancel;

        /* Count up how many blocks & cancels each peer has.
         *
         * There are two situations where we send out cancels --
         *
         * 1. We've got unresponsive peers, which is handled by deciding
         *    -which- peers to be interested in.
         *
         * 2. We've hit our bandwidth cap, which is handled by deciding
         *    -how many- peers to be interested in.
         *
         * We're working on 2. here, so we need to ignore unresponsive
         * peers in our calculations lest they confuse Transmission into
         * thinking it's hit its bandwidth cap.
         */
        for (int i = 0; i < peerCount; ++i)
        {
            tr_peer const* peer = tr_ptrArrayNth(&s->peers, i);
            int const b = tr_historyGet(&peer->blocksSentToClient, now, CANCEL_HISTORY_SEC);
            int const c = tr_historyGet(&peer->cancelsSentToPeer, now, CANCEL_HISTORY_SEC);

            if (b == 0) /* ignore unresponsive peers, as described above */
            {
                continue;
            }

            blocks += b;
            cancels += c;
        }

        if (cancels > 0)
        {
            /* cancelRate: of the block requests we've recently made, the percentage we cancelled.
             * higher values indicate more congestion. */
            double const cancelRate = cancels / (double)(cancels + blocks);
            double const mult = 1 - MIN(cancelRate, 0.5);
            maxPeers = s->interestedCount * mult;
            tordbg(s, "cancel rate is %.3f -- reducing the number of peers we're interested in by %.0f percent", cancelRate,
                mult * 100);
            s->lastCancel = now;
        }

        timeSinceCancel = now - s->lastCancel;

        if (timeSinceCancel != 0)
        {
            int const maxIncrease = 15;
            time_t const maxHistory = 2 * CANCEL_HISTORY_SEC;
            double const mult = MIN(timeSinceCancel, maxHistory) / (double)maxHistory;
            int const inc = maxIncrease * mult;
            maxPeers = s->maxPeers + inc;
            tordbg(s, "time since last cancel is %jd -- increasing the number of peers we're interested in by %d",
                (intmax_t)timeSinceCancel, inc);
        }
    }

    /* don't let the previous section's number tweaking go too far... */
    if (maxPeers < MIN_INTERESTING_PEERS)
    {
        maxPeers = MIN_INTERESTING_PEERS;
    }

    if (maxPeers > s->tor->maxConnectedPeers)
    {
        maxPeers = s->tor->maxConnectedPeers;
    }

    s->maxPeers = maxPeers;

    if (peerCount > 0)
    {
        bool* piece_is_interesting;
        tr_torrent const* const tor = s->tor;
        int const n = tor->info.pieceCount;

        /* build a bitfield of interesting pieces... */
        piece_is_interesting = tr_new(bool, n);

        for (int i = 0; i < n; ++i)
        {
            piece_is_interesting[i] = !tor->info.pieces[i].dnd && !tr_torrentPieceIsComplete(tor, i);
        }

        /* decide WHICH peers to be interested in (based on their cancel-to-block ratio) */
        for (int i = 0; i < peerCount; ++i)
        {
            tr_peer* peer = tr_ptrArrayNth(&s->peers, i);

            if (!isPeerInteresting(s->tor, piece_is_interesting, peer))
            {
                tr_peerMsgsSetInterested(PEER_MSGS(peer), false);
            }
            else
            {
                tr_rechoke_state rechoke_state;
                int const blocks = tr_historyGet(&peer->blocksSentToClient, now, CANCEL_HISTORY_SEC);
                int const cancels = tr_historyGet(&peer->cancelsSentToPeer, now, CANCEL_HISTORY_SEC);

                if (blocks == 0 && cancels == 0)
                {
                    rechoke_state = RECHOKE_STATE_UNTESTED;
                }
                else if (cancels == 0)
                {
                    rechoke_state = RECHOKE_STATE_GOOD;
                }
                else if (blocks == 0)
                {
                    rechoke_state = RECHOKE_STATE_BAD;
                }
                else if (cancels * 10 < blocks)
                {
                    rechoke_state = RECHOKE_STATE_GOOD;
                }
                else
                {
                    rechoke_state = RECHOKE_STATE_BAD;
                }

                if (rechoke == NULL)
                {
                    rechoke = tr_new(struct tr_rechoke_info, peerCount);
                }

                rechoke[rechoke_count].peer = peer;
                rechoke[rechoke_count].rechoke_state = rechoke_state;
                rechoke[rechoke_count].salt = tr_rand_int_weak(INT_MAX);
                rechoke_count++;
            }
        }

        tr_free(piece_is_interesting);
    }

    /* now that we know which & how many peers to be interested in... update the peer interest */
    qsort(rechoke, rechoke_count, sizeof(struct tr_rechoke_info), compare_rechoke_info);
    s->interestedCount = MIN(maxPeers, rechoke_count);

    for (int i = 0; i < rechoke_count; ++i)
    {
        tr_peerMsgsSetInterested(PEER_MSGS(rechoke[i].peer), i < s->interestedCount);
    }

    /* cleanup */
    tr_free(rechoke);
}

/**
***
**/

struct ChokeData
{
    bool isInterested;
    bool wasChoked;
    bool isChoked;
    int rate;
    int salt;
    tr_peerMsgs* msgs;
};

static int compareChoke(void const* va, void const* vb)
{
    struct ChokeData const* a = va;
    struct ChokeData const* b = vb;

    if (a->rate != b->rate) /* prefer higher overall speeds */
    {
        return a->rate > b->rate ? -1 : 1;
    }

    if (a->wasChoked != b->wasChoked) /* prefer unchoked */
    {
        return a->wasChoked ? 1 : -1;
    }

    if (a->salt != b->salt) /* random order */
    {
        return a->salt - b->salt;
    }

    return 0;
}

/* is this a new connection? */
static bool isNew(tr_peerMsgs const* msgs)
{
    return msgs != NULL && tr_peerMsgsGetConnectionAge(msgs) < 45;
}

/* get a rate for deciding which peers to choke and unchoke. */
static int getRate(tr_torrent const* tor, struct peer_atom* atom, uint64_t now)
{
    unsigned int Bps;

    if (tr_torrentIsSeed(tor))
    {
        Bps = tr_peerGetPieceSpeed_Bps(atom->peer, now, TR_CLIENT_TO_PEER);
    }
    /* downloading a private torrent... take upload speed into account
     * because there may only be a small window of opportunity to share */
    else if (tr_torrentIsPrivate(tor))
    {
        Bps = tr_peerGetPieceSpeed_Bps(atom->peer, now, TR_PEER_TO_CLIENT) + tr_peerGetPieceSpeed_Bps(atom->peer, now,
            TR_CLIENT_TO_PEER);
    }
    /* downloading a public torrent */
    else
    {
        Bps = tr_peerGetPieceSpeed_Bps(atom->peer, now, TR_PEER_TO_CLIENT);
    }

    /* convert it to bytes per second */
    return Bps;
}

static inline bool isBandwidthMaxedOut(tr_bandwidth const* b, uint64_t const now_msec, tr_direction dir)
{
    if (!tr_bandwidthIsLimited(b, dir))
    {
        return false;
    }
    else
    {
        unsigned int const got = tr_bandwidthGetPieceSpeed_Bps(b, now_msec, dir);
        unsigned int const want = tr_bandwidthGetDesiredSpeed_Bps(b, dir);
        return got >= want;
    }
}

static void rechokeUploads(tr_swarm* s, uint64_t const now)
{
    TR_ASSERT(swarmIsLocked(s));

    int const peerCount = tr_ptrArraySize(&s->peers);
    tr_peer** peers = (tr_peer**)tr_ptrArrayBase(&s->peers);
    struct ChokeData* choke = tr_new0(struct ChokeData, peerCount);
    tr_session const* session = s->manager->session;
    bool const chokeAll = !tr_torrentIsPieceTransferAllowed(s->tor, TR_CLIENT_TO_PEER);
    bool const isMaxedOut = isBandwidthMaxedOut(&s->tor->bandwidth, now, TR_UP);

    /* an optimistic unchoke peer's "optimistic"
     * state lasts for N calls to rechokeUploads(). */
    if (s->optimisticUnchokeTimeScaler > 0)
    {
        s->optimisticUnchokeTimeScaler--;
    }
    else
    {
        s->optimistic = NULL;
    }

    int size = 0;

    /* sort the peers by preference and rate */
    for (int i = 0; i < peerCount; ++i)
    {
        tr_peer* peer = peers[i];
        tr_peerMsgs* msgs = PEER_MSGS(peer);

        struct peer_atom* atom = peer->atom;

        if (tr_peerIsSeed(peer))
        {
            /* choke seeds and partial seeds */
            tr_peerMsgsSetChoke(PEER_MSGS(peer), true);
        }
        else if (chokeAll)
        {
            /* choke everyone if we're not uploading */
            tr_peerMsgsSetChoke(PEER_MSGS(peer), true);
        }
        else if (msgs != s->optimistic)
        {
            struct ChokeData* n = &choke[size++];
            n->msgs = msgs;
            n->isInterested = tr_peerMsgsIsPeerInterested(msgs);
            n->wasChoked = tr_peerMsgsIsPeerChoked(msgs);
            n->rate = getRate(s->tor, atom, now);
            n->salt = tr_rand_int_weak(INT_MAX);
            n->isChoked = true;
        }
    }

    qsort(choke, size, sizeof(struct ChokeData), compareChoke);

    /**
     * Reciprocation and number of uploads capping is managed by unchoking
     * the N peers which have the best upload rate and are interested.
     * This maximizes the client's download rate. These N peers are
     * referred to as downloaders, because they are interested in downloading
     * from the client.
     *
     * Peers which have a better upload rate (as compared to the downloaders)
     * but aren't interested get unchoked. If they become interested, the
     * downloader with the worst upload rate gets choked. If a client has
     * a complete file, it uses its upload rate rather than its download
     * rate to decide which peers to unchoke.
     *
     * If our bandwidth is maxed out, don't unchoke any more peers.
     */
    int unchokedInterested = 0;
    int checkedChokeCount = 0;

    for (int i = 0; i < size && unchokedInterested < session->uploadSlotsPerTorrent; ++i, ++checkedChokeCount)
    {
        choke[i].isChoked = isMaxedOut ? choke[i].wasChoked : false;

        if (choke[i].isInterested)
        {
            ++unchokedInterested;
        }
    }

    /* optimistic unchoke */
    if (s->optimistic == NULL && !isMaxedOut && checkedChokeCount < size)
    {
        int n;
        struct ChokeData* c;
        tr_ptrArray randPool = TR_PTR_ARRAY_INIT;

        for (int i = checkedChokeCount; i < size; ++i)
        {
            if (choke[i].isInterested)
            {
                tr_peerMsgs const* msgs = choke[i].msgs;
                int const x = isNew(msgs) ? 3 : 1;

                for (int y = 0; y < x; ++y)
                {
                    tr_ptrArrayAppend(&randPool, &choke[i]);
                }
            }
        }

        if ((n = tr_ptrArraySize(&randPool)) != 0)
        {
            c = tr_ptrArrayNth(&randPool, tr_rand_int_weak(n));
            c->isChoked = false;
            s->optimistic = c->msgs;
            s->optimisticUnchokeTimeScaler = OPTIMISTIC_UNCHOKE_MULTIPLIER;
        }

        tr_ptrArrayDestruct(&randPool, NULL);
    }

    for (int i = 0; i < size; ++i)
    {
        tr_peerMsgsSetChoke(choke[i].msgs, choke[i].isChoked);
    }

    /* cleanup */
    tr_free(choke);
}

static void rechokePulse(evutil_socket_t foo UNUSED, short bar UNUSED, void* vmgr)
{
    tr_torrent* tor = NULL;
    tr_peerMgr* mgr = vmgr;
    uint64_t const now = tr_time_msec();

    managerLock(mgr);

    while ((tor = tr_torrentNext(mgr->session, tor)) != NULL)
    {
        if (tor->isRunning)
        {
            tr_swarm* s = tor->swarm;

            if (s->stats.peerCount > 0)
            {
                rechokeUploads(s, now);
                rechokeDownloads(s);
            }
        }
    }

    tr_timerAddMsec(mgr->rechokeTimer, RECHOKE_PERIOD_MSEC);
    managerUnlock(mgr);
}

/***
****
****  Life and Death
****
***/

static bool shouldPeerBeClosed(tr_swarm const* s, tr_peer const* peer, int peerCount, time_t const now)
{
    tr_torrent const* tor = s->tor;
    struct peer_atom const* atom = peer->atom;

    /* if it's marked for purging, close it */
    if (peer->doPurge)
    {
        tordbg(s, "purging peer %s because its doPurge flag is set", tr_atomAddrStr(atom));
        return true;
    }

    /* disconnect if we're both seeds and enough time has passed for PEX */
    if (tr_torrentIsSeed(tor) && tr_peerIsSeed(peer))
    {
        return !tr_torrentAllowsPex(tor) || now - atom->time >= 30;
    }

    /* disconnect if it's been too long since piece data has been transferred.
     * this is on a sliding scale based on number of available peers... */
    {
        int const relaxStrictnessIfFewerThanN = (int)(getMaxPeerCount(tor) * 0.9 + 0.5);
        /* if we have >= relaxIfFewerThan, strictness is 100%.
         * if we have zero connections, strictness is 0% */
        float const strictness = peerCount >= relaxStrictnessIfFewerThanN ? 1.0 :
            peerCount / (float)relaxStrictnessIfFewerThanN;
        int const lo = MIN_UPLOAD_IDLE_SECS;
        int const hi = MAX_UPLOAD_IDLE_SECS;
        int const limit = hi - (hi - lo) * strictness;
        int const idleTime = now - MAX(atom->time, atom->piece_data_time);

        /*
        fprintf(stderr, "strictness is %.3f, limit is %d seconds... time since connect is %d, time since piece is %d ... "
            "idleTime is %d, doPurge is %d\n", (double)strictness, limit, (int)(now - atom->time),
            (int)(now - atom->piece_data_time), idleTime, idleTime > limit);
        */

        if (idleTime > limit)
        {
            tordbg(s, "purging peer %s because it's been %d secs since we shared anything", tr_atomAddrStr(atom), idleTime);
            return true;
        }
    }

    return false;
}

static tr_peer** getPeersToClose(tr_swarm* s, time_t const now_sec, int* setmeSize)
{
    TR_ASSERT(swarmIsLocked(s));

    int peerCount;
    int outsize = 0;
    struct tr_peer** ret = NULL;
    tr_peer** peers = (tr_peer**)tr_ptrArrayPeek(&s->peers, &peerCount);

    for (int i = 0; i < peerCount; ++i)
    {
        if (shouldPeerBeClosed(s, peers[i], peerCount, now_sec))
        {
            if (ret == NULL)
            {
                ret = tr_new(tr_peer*, peerCount);
            }

            ret[outsize++] = peers[i];
        }
    }

    *setmeSize = outsize;
    return ret;
}

static int getReconnectIntervalSecs(struct peer_atom const* atom, time_t const now)
{
    int sec;
    bool const unreachable = (atom->flags2 & MYFLAG_UNREACHABLE) != 0;

    /* if we were recently connected to this peer and transferring piece
     * data, try to reconnect to them sooner rather that later -- we don't
     * want network troubles to get in the way of a good peer. */
    if (!unreachable && now - atom->piece_data_time <= MINIMUM_RECONNECT_INTERVAL_SECS * 2)
    {
        sec = MINIMUM_RECONNECT_INTERVAL_SECS;
    }
    /* otherwise, the interval depends on how many times we've tried
     * and failed to connect to the peer */
    else
    {
        int step = atom->numFails;

        /* penalize peers that were unreachable the last time we tried */
        if (unreachable)
        {
            step += 2;
        }

        switch (step)
        {
        case 0:
            sec = 0;
            break;

        case 1:
            sec = 10;
            break;

        case 2:
            sec = 60 * 2;
            break;

        case 3:
            sec = 60 * 15;
            break;

        case 4:
            sec = 60 * 30;
            break;

        case 5:
            sec = 60 * 60;
            break;

        default:
            sec = 60 * 120;
            break;
        }
    }

    dbgmsg("reconnect interval for %s is %d seconds", tr_atomAddrStr(atom), sec);
    return sec;
}

static void removePeer(tr_swarm* s, tr_peer* peer)
{
    TR_ASSERT(swarmIsLocked(s));

    struct peer_atom* atom = peer->atom;
    TR_ASSERT(atom != NULL);

    atom->time = tr_time();

    tr_ptrArrayRemoveSortedPointer(&s->peers, peer, peerCompare);
    --s->stats.peerCount;
    --s->stats.peerFromCount[atom->fromFirst];

    if (replicationExists(s))
    {
        tr_decrReplicationFromBitfield(s, &peer->have);
    }

    TR_ASSERT(s->stats.peerCount == tr_ptrArraySize(&s->peers));
    TR_ASSERT(s->stats.peerFromCount[atom->fromFirst] >= 0);

    tr_peerFree(peer);
}

static void closePeer(tr_swarm* s, tr_peer* peer)
{
    TR_ASSERT(s != NULL);
    TR_ASSERT(peer != NULL);

    struct peer_atom* atom = peer->atom;

    /* if we transferred piece data, then they might be good peers,
       so reset their `numFails' weight to zero. otherwise we connected
       to them fruitlessly, so mark it as another fail */
    if (atom->piece_data_time != 0)
    {
        tordbg(s, "resetting atom %s numFails to 0", tr_atomAddrStr(atom));
        atom->numFails = 0;
    }
    else
    {
        ++atom->numFails;
        tordbg(s, "incremented atom %s numFails to %d", tr_atomAddrStr(atom), (int)atom->numFails);
    }

    tordbg(s, "removing bad peer %s", tr_atomAddrStr(peer->atom));
    removePeer(s, peer);
}

static void removeAllPeers(tr_swarm* s)
{
    while (!tr_ptrArrayEmpty(&s->peers))
    {
        removePeer(s, tr_ptrArrayNth(&s->peers, 0));
    }

    TR_ASSERT(s->stats.peerCount == 0);
}

static void closeBadPeers(tr_swarm* s, time_t const now_sec)
{
    if (!tr_ptrArrayEmpty(&s->peers))
    {
        int peerCount;
        struct tr_peer** peers;

        peers = getPeersToClose(s, now_sec, &peerCount);

        for (int i = 0; i < peerCount; ++i)
        {
            closePeer(s, peers[i]);
        }

        tr_free(peers);
    }
}

struct peer_liveliness
{
    tr_peer* peer;
    void* clientData;
    time_t pieceDataTime;
    time_t time;
    unsigned int speed;
    bool doPurge;
};

static int comparePeerLiveliness(void const* va, void const* vb)
{
    struct peer_liveliness const* a = va;
    struct peer_liveliness const* b = vb;

    if (a->doPurge != b->doPurge)
    {
        return a->doPurge ? 1 : -1;
    }

    if (a->speed != b->speed) /* faster goes first */
    {
        return a->speed > b->speed ? -1 : 1;
    }

    /* the one to give us data more recently goes first */
    if (a->pieceDataTime != b->pieceDataTime)
    {
        return a->pieceDataTime > b->pieceDataTime ? -1 : 1;
    }

    /* the one we connected to most recently goes first */
    if (a->time != b->time)
    {
        return a->time > b->time ? -1 : 1;
    }

    return 0;
}

static void sortPeersByLivelinessImpl(tr_peer** peers, void** clientData, int n, uint64_t now, tr_voidptr_compare_func compare)
{
    struct peer_liveliness* lives;
    struct peer_liveliness* l;

    /* build a sortable array of peer + extra info */
    lives = tr_new0(struct peer_liveliness, n);
    l = lives;

    for (int i = 0; i < n; ++i, ++l)
    {
        tr_peer* p = peers[i];
        l->peer = p;
        l->doPurge = p->doPurge;
        l->pieceDataTime = p->atom->piece_data_time;
        l->time = p->atom->time;
        l->speed = tr_peerGetPieceSpeed_Bps(p, now, TR_UP) + tr_peerGetPieceSpeed_Bps(p, now, TR_DOWN);

        if (clientData != NULL)
        {
            l->clientData = clientData[i];
        }
    }

    /* sort 'em */
    TR_ASSERT(n == l - lives);
    qsort(lives, n, sizeof(struct peer_liveliness), compare);

    l = lives;

    /* build the peer array */
    for (int i = 0; i < n; ++i, ++l)
    {
        peers[i] = l->peer;

        if (clientData != NULL)
        {
            clientData[i] = l->clientData;
        }
    }

    TR_ASSERT(n == l - lives);

    /* cleanup */
    tr_free(lives);
}

static void sortPeersByLiveliness(tr_peer** peers, void** clientData, int n, uint64_t now)
{
    sortPeersByLivelinessImpl(peers, clientData, n, now, comparePeerLiveliness);
}

static void enforceTorrentPeerLimit(tr_swarm* s, uint64_t now)
{
    int n = tr_ptrArraySize(&s->peers);
    int const max = tr_torrentGetPeerLimit(s->tor);

    if (n > max)
    {
        void* base = tr_ptrArrayBase(&s->peers);
        tr_peer** peers = tr_memdup(base, n * sizeof(tr_peer*));
        sortPeersByLiveliness(peers, NULL, n, now);

        while (n > max)
        {
            closePeer(s, peers[--n]);
        }

        tr_free(peers);
    }
}

static void enforceSessionPeerLimit(tr_session* session, uint64_t now)
{
    int n = 0;
    tr_torrent* tor = NULL;
    int const max = tr_sessionGetPeerLimit(session);

    /* count the total number of peers */
    while ((tor = tr_torrentNext(session, tor)) != NULL)
    {
        n += tr_ptrArraySize(&tor->swarm->peers);
    }

    /* if there are too many, prune out the worst */
    if (n > max)
    {
        tr_peer** peers = tr_new(tr_peer*, n);
        tr_swarm** swarms = tr_new(tr_swarm*, n);

        /* populate the peer array */
        n = 0;
        tor = NULL;

        while ((tor = tr_torrentNext(session, tor)) != NULL)
        {
            tr_swarm* s = tor->swarm;

            for (int i = 0, tn = tr_ptrArraySize(&s->peers); i < tn; ++i, ++n)
            {
                peers[n] = tr_ptrArrayNth(&s->peers, i);
                swarms[n] = s;
            }
        }

        /* sort 'em */
        sortPeersByLiveliness(peers, (void**)swarms, n, now);

        /* cull out the crappiest */
        while (n-- > max)
        {
            closePeer(swarms[n], peers[n]);
        }

        /* cleanup */
        tr_free(swarms);
        tr_free(peers);
    }
}

static void makeNewPeerConnections(tr_peerMgr* mgr, int const max);

static void reconnectPulse(evutil_socket_t foo UNUSED, short bar UNUSED, void* vmgr)
{
    tr_torrent* tor;
    tr_peerMgr* mgr = vmgr;
    time_t const now_sec = tr_time();
    uint64_t const now_msec = tr_time_msec();

    /**
    ***  enforce the per-session and per-torrent peer limits
    **/

    /* if we're over the per-torrent peer limits, cull some peers */
    tor = NULL;

    while ((tor = tr_torrentNext(mgr->session, tor)) != NULL)
    {
        if (tor->isRunning)
        {
            enforceTorrentPeerLimit(tor->swarm, now_msec);
        }
    }

    /* if we're over the per-session peer limits, cull some peers */
    enforceSessionPeerLimit(mgr->session, now_msec);

    /* remove crappy peers */
    tor = NULL;

    while ((tor = tr_torrentNext(mgr->session, tor)) != NULL)
    {
        if (!tor->swarm->isRunning)
        {
            removeAllPeers(tor->swarm);
        }
        else
        {
            closeBadPeers(tor->swarm, now_sec);
        }
    }

    /* try to make new peer connections */
    makeNewPeerConnections(mgr, MAX_CONNECTIONS_PER_PULSE);
}

/****
*****
*****  BANDWIDTH ALLOCATION
*****
****/

static void pumpAllPeers(tr_peerMgr* mgr)
{
    tr_torrent* tor = NULL;

    while ((tor = tr_torrentNext(mgr->session, tor)) != NULL)
    {
        tr_swarm* s = tor->swarm;

        for (int j = 0, n = tr_ptrArraySize(&s->peers); j < n; ++j)
        {
            tr_peerMsgsPulse(tr_ptrArrayNth(&s->peers, j));
        }
    }
}

static void queuePulseForeach(void* vtor)
{
    tr_torrent* tor = vtor;

    tr_torrentStartNow(tor);

    if (tor->queue_started_callback != NULL)
    {
        (*tor->queue_started_callback)(tor, tor->queue_started_user_data);
    }
}

static void queuePulse(tr_session* session, tr_direction dir)
{
    TR_ASSERT(tr_isSession(session));
    TR_ASSERT(tr_isDirection(dir));

    if (tr_sessionGetQueueEnabled(session, dir))
    {
        tr_ptrArray torrents = TR_PTR_ARRAY_INIT;

        tr_sessionGetNextQueuedTorrents(session, dir, tr_sessionCountQueueFreeSlots(session, dir), &torrents);

        tr_ptrArrayForeach(&torrents, queuePulseForeach);

        tr_ptrArrayDestruct(&torrents, NULL);
    }
}

static void bandwidthPulse(evutil_socket_t foo UNUSED, short bar UNUSED, void* vmgr)
{
    tr_torrent* tor;
    tr_peerMgr* mgr = vmgr;
    tr_session* session = mgr->session;
    managerLock(mgr);

    /* FIXME: this next line probably isn't necessary... */
    pumpAllPeers(mgr);

    /* allocate bandwidth to the peers */
    tr_bandwidthAllocate(&session->bandwidth, TR_UP, BANDWIDTH_PERIOD_MSEC);
    tr_bandwidthAllocate(&session->bandwidth, TR_DOWN, BANDWIDTH_PERIOD_MSEC);

    /* torrent upkeep */
    tor = NULL;

    while ((tor = tr_torrentNext(session, tor)) != NULL)
    {
        /* possibly stop torrents that have seeded enough */
        tr_torrentCheckSeedLimit(tor);

        /* run the completeness check for any torrents that need it */
        if (tor->swarm->needsCompletenessCheck)
        {
            tor->swarm->needsCompletenessCheck = false;
            tr_torrentRecheckCompleteness(tor);
        }

        /* stop torrents that are ready to stop, but couldn't be stopped
           earlier during the peer-io callback call chain */
        if (tor->isStopping)
        {
            tr_torrentStop(tor);
        }

        /* update the torrent's stats */
        tor->swarm->stats.activeWebseedCount = countActiveWebseeds(tor->swarm);
    }

    /* pump the queues */
    queuePulse(session, TR_UP);
    queuePulse(session, TR_DOWN);

    reconnectPulse(0, 0, mgr);

    tr_timerAddMsec(mgr->bandwidthTimer, BANDWIDTH_PERIOD_MSEC);
    managerUnlock(mgr);
}

/***
****
***/

static int compareAtomPtrsByAddress(void const* va, void const* vb)
{
    struct peer_atom const* a = *(struct peer_atom const* const*)va;
    struct peer_atom const* b = *(struct peer_atom const* const*)vb;

    TR_ASSERT(tr_isAtom(a));
    TR_ASSERT(tr_isAtom(b));

    return tr_address_compare(&a->addr, &b->addr);
}

/* best come first, worst go last */
static int compareAtomPtrsByShelfDate(void const* va, void const* vb)
{
    struct peer_atom const* a = *(struct peer_atom const* const*)va;
    struct peer_atom const* b = *(struct peer_atom const* const*)vb;

    TR_ASSERT(tr_isAtom(a));
    TR_ASSERT(tr_isAtom(b));

    int const data_time_cutoff_secs = 60 * 60;
    time_t const tr_now = tr_time();

    /* primary key: the last piece data time *if* it was within the last hour */
    time_t atime = a->piece_data_time;

    if (atime + data_time_cutoff_secs < tr_now)
    {
        atime = 0;
    }

    time_t btime = b->piece_data_time;

    if (btime + data_time_cutoff_secs < tr_now)
    {
        btime = 0;
    }

    if (atime != btime)
    {
        return atime > btime ? -1 : 1;
    }

    /* secondary key: shelf date. */
    if (a->shelf_date != b->shelf_date)
    {
        return a->shelf_date > b->shelf_date ? -1 : 1;
    }

    return 0;
}

static int getMaxAtomCount(tr_torrent const* tor)
{
    return MIN(50, tor->maxConnectedPeers * 3);
}

static void atomPulse(evutil_socket_t foo UNUSED, short bar UNUSED, void* vmgr)
{
    tr_torrent* tor = NULL;
    tr_peerMgr* mgr = vmgr;
    managerLock(mgr);

    while ((tor = tr_torrentNext(mgr->session, tor)) != NULL)
    {
        int atomCount;
        tr_swarm* s = tor->swarm;
        int const maxAtomCount = getMaxAtomCount(tor);
        struct peer_atom** atoms = (struct peer_atom**)tr_ptrArrayPeek(&s->pool, &atomCount);

        if (atomCount > maxAtomCount) /* we've got too many atoms... time to prune */
        {
            int keepCount = 0;
            int testCount = 0;
            struct peer_atom** keep = tr_new(struct peer_atom*, atomCount);
            struct peer_atom** test = tr_new(struct peer_atom*, atomCount);

            /* keep the ones that are in use */
            for (int i = 0; i < atomCount; ++i)
            {
                struct peer_atom* atom = atoms[i];

                if (peerIsInUse(s, atom))
                {
                    keep[keepCount++] = atom;
                }
                else
                {
                    test[testCount++] = atom;
                }
            }

            /* if there's room, keep the best of what's left */
            int i = 0;

            if (keepCount < maxAtomCount)
            {
                qsort(test, testCount, sizeof(struct peer_atom*), compareAtomPtrsByShelfDate);

                while (i < testCount && keepCount < maxAtomCount)
                {
                    keep[keepCount++] = test[i++];
                }
            }

            /* free the culled atoms */
            while (i < testCount)
            {
                tr_free(test[i++]);
            }

            /* rebuild Torrent.pool with what's left */
            tr_ptrArrayDestruct(&s->pool, NULL);
            s->pool = TR_PTR_ARRAY_INIT;
            qsort(keep, keepCount, sizeof(struct peer_atom*), compareAtomPtrsByAddress);

            for (int i = 0; i < keepCount; ++i)
            {
                tr_ptrArrayAppend(&s->pool, keep[i]);
            }

            tordbg(s, "max atom count is %d... pruned from %d to %d\n", maxAtomCount, atomCount, keepCount);

            /* cleanup */
            tr_free(test);
            tr_free(keep);
        }
    }

    tr_timerAddMsec(mgr->atomTimer, ATOM_PERIOD_MSEC);
    managerUnlock(mgr);
}

/***
****
****
****
***/

/* is this atom someone that we'd want to initiate a connection to? */
static bool isPeerCandidate(tr_torrent const* tor, struct peer_atom* atom, time_t const now)
{
    /* not if we're both seeds */
    if (tr_torrentIsSeed(tor) && atomIsSeed(atom))
    {
        return false;
    }

    /* not if we've already got a connection to them... */
    if (peerIsInUse(tor->swarm, atom))
    {
        return false;
    }

    /* not if we just tried them already */
    if (now - atom->time < getReconnectIntervalSecs(atom, now))
    {
        return false;
    }

    /* not if they're blocklisted */
    if (isAtomBlocklisted(tor->session, atom))
    {
        return false;
    }

    /* not if they're banned... */
    if ((atom->flags2 & MYFLAG_BANNED) != 0)
    {
        return false;
    }

    return true;
}

struct peer_candidate
{
    uint64_t score;
    tr_torrent* tor;
    struct peer_atom* atom;
};

static bool torrentWasRecentlyStarted(tr_torrent const* tor)
{
    return difftime(tr_time(), tor->startDate) < 120;
}

static inline uint64_t addValToKey(uint64_t value, int width, uint64_t addme)
{
    value = value << (uint64_t)width;
    value |= addme;
    return value;
}

/* smaller value is better */
static uint64_t getPeerCandidateScore(tr_torrent const* tor, struct peer_atom const* atom, uint8_t salt)
{
    uint64_t i;
    uint64_t score = 0;
    bool const failed = atom->lastConnectionAt < atom->lastConnectionAttemptAt;

    /* prefer peers we've connected to, or never tried, over peers we failed to connect to. */
    i = failed ? 1 : 0;
    score = addValToKey(score, 1, i);

    /* prefer the one we attempted least recently (to cycle through all peers) */
    i = atom->lastConnectionAttemptAt;
    score = addValToKey(score, 32, i);

    /* prefer peers belonging to a torrent of a higher priority */
    switch (tr_torrentGetPriority(tor))
    {
    case TR_PRI_HIGH:
        i = 0;
        break;

    case TR_PRI_NORMAL:
        i = 1;
        break;

    case TR_PRI_LOW:
        i = 2;
        break;
    }

    score = addValToKey(score, 4, i);

    /* prefer recently-started torrents */
    i = torrentWasRecentlyStarted(tor) ? 0 : 1;
    score = addValToKey(score, 1, i);

    /* prefer torrents we're downloading with */
    i = tr_torrentIsSeed(tor) ? 1 : 0;
    score = addValToKey(score, 1, i);

    /* prefer peers that are known to be connectible */
    i = (atom->flags & ADDED_F_CONNECTABLE) != 0 ? 0 : 1;
    score = addValToKey(score, 1, i);

    /* prefer peers that we might have a chance of uploading to...
    so lower seed probability is better */
    if (atom->seedProbability == 100)
    {
        i = 101;
    }
    else if (atom->seedProbability == -1)
    {
        i = 100;
    }
    else
    {
        i = atom->seedProbability;
    }

    score = addValToKey(score, 8, i);

    /* Prefer peers that we got from more trusted sources.
     * lower `fromBest' values indicate more trusted sources */
    score = addValToKey(score, 4, atom->fromBest);

    /* salt */
    score = addValToKey(score, 8, salt);

    return score;
}

static int comparePeerCandidates(void const* va, void const* vb)
{
    int ret;
    struct peer_candidate const* a = va;
    struct peer_candidate const* b = vb;

    if (a->score < b->score)
    {
        ret = -1;
    }
    else if (a->score > b->score)
    {
        ret = 1;
    }
    else
    {
        ret = 0;
    }

    return ret;
}

/* Partial sorting -- selecting the k best candidates
   Adapted from http://en.wikipedia.org/wiki/Selection_algorithm */
static void selectPeerCandidates(struct peer_candidate* candidates, int candidate_count, int select_count)
{
    tr_quickfindFirstK(candidates, candidate_count, sizeof(struct peer_candidate), comparePeerCandidates, select_count);
}

#ifdef TR_ENABLE_ASSERTS

static bool checkBestScoresComeFirst(struct peer_candidate const* candidates, int n, int k)
{
    uint64_t worstFirstScore = 0;
    int const x = MIN(n, k) - 1;

    for (int i = 0; i < x; i++)
    {
        if (worstFirstScore < candidates[i].score)
        {
            worstFirstScore = candidates[i].score;
        }
    }

    for (int i = 0; i < x; i++)
    {
        TR_ASSERT(candidates[i].score <= worstFirstScore);
    }

    for (int i = x + 1; i < n; i++)
    {
        TR_ASSERT(candidates[i].score >= worstFirstScore);
    }

    return true;
}

#endif /* TR_ENABLE_ASSERTS */

/** @return an array of all the atoms we might want to connect to */
static struct peer_candidate* getPeerCandidates(tr_session* session, int* candidateCount, int max)
{
    int atomCount;
    int peerCount;
    tr_torrent* tor;
    struct peer_candidate* candidates;
    struct peer_candidate* walk;
    time_t const now = tr_time();
    uint64_t const now_msec = tr_time_msec();
    /* leave 5% of connection slots for incoming connections -- ticket #2609 */
    int const maxCandidates = tr_sessionGetPeerLimit(session) * 0.95;

    /* count how many peers and atoms we've got */
    tor = NULL;
    atomCount = 0;
    peerCount = 0;

    while ((tor = tr_torrentNext(session, tor)) != NULL)
    {
        atomCount += tr_ptrArraySize(&tor->swarm->pool);
        peerCount += tr_ptrArraySize(&tor->swarm->peers);
    }

    /* don't start any new handshakes if we're full up */
    if (maxCandidates <= peerCount)
    {
        *candidateCount = 0;
        return NULL;
    }

    /* allocate an array of candidates */
    walk = candidates = tr_new(struct peer_candidate, atomCount);

    /* populate the candidate array */
    tor = NULL;

    while ((tor = tr_torrentNext(session, tor)) != NULL)
    {
        int nAtoms;
        struct peer_atom** atoms;

        if (!tor->swarm->isRunning)
        {
            continue;
        }

        /* if we've already got enough peers in this torrent... */
        if (tr_torrentGetPeerLimit(tor) <= tr_ptrArraySize(&tor->swarm->peers))
        {
            continue;
        }

        /* if we've already got enough speed in this torrent... */
        if (tr_torrentIsSeed(tor) && isBandwidthMaxedOut(&tor->bandwidth, now_msec, TR_UP))
        {
            continue;
        }

        atoms = (struct peer_atom**)tr_ptrArrayPeek(&tor->swarm->pool, &nAtoms);

        for (int i = 0; i < nAtoms; ++i)
        {
            struct peer_atom* atom = atoms[i];

            if (isPeerCandidate(tor, atom, now))
            {
                uint8_t const salt = tr_rand_int_weak(1024);
                walk->tor = tor;
                walk->atom = atom;
                walk->score = getPeerCandidateScore(tor, atom, salt);
                ++walk;
            }
        }
    }

    *candidateCount = walk - candidates;

    if (walk != candidates)
    {
        selectPeerCandidates(candidates, walk - candidates, max);
    }

    TR_ASSERT(checkBestScoresComeFirst(candidates, *candidateCount, max));
    return candidates;
}

static void initiateConnection(tr_peerMgr* mgr, tr_swarm* s, struct peer_atom* atom)
{
    tr_peerIo* io;
    time_t const now = tr_time();
    bool utp = tr_sessionIsUTPEnabled(mgr->session) && !atom->utp_failed;

    if (atom->fromFirst == TR_PEER_FROM_PEX)
    {
        /* PEX has explicit signalling for uTP support.  If an atom
           originally came from PEX and doesn't have the uTP flag, skip the
           uTP connection attempt.  Are we being optimistic here? */
        utp = utp && (atom->flags & ADDED_F_UTP_FLAGS) != 0;
    }

    tordbg(s, "Starting an OUTGOING%s connection with %s", utp ? " µTP" : "", tr_atomAddrStr(atom));

    io = tr_peerIoNewOutgoing(mgr->session, &mgr->session->bandwidth, &atom->addr, atom->port, s->tor->info.hash,
        s->tor->completeness == TR_SEED, utp);

    if (io == NULL)
    {
        tordbg(s, "peerIo not created; marking peer %s as unreachable", tr_atomAddrStr(atom));
        atom->flags2 |= MYFLAG_UNREACHABLE;
        atom->numFails++;
    }
    else
    {
        tr_handshake* handshake = tr_handshakeNew(io, mgr->session->encryptionMode, myHandshakeDoneCB, mgr);

        TR_ASSERT(tr_peerIoGetTorrentHash(io));

        tr_peerIoUnref(io); /* balanced by the initial ref in tr_peerIoNewOutgoing() */

        tr_ptrArrayInsertSorted(&s->outgoingHandshakes, handshake, handshakeCompare);
    }

    atom->lastConnectionAttemptAt = now;
    atom->time = now;
}

static void initiateCandidateConnection(tr_peerMgr* mgr, struct peer_candidate* c)
{
#if 0

    fprintf(stderr, "Starting an OUTGOING connection with %s - [%s] seedProbability==%d; %s, %s\n", tr_atomAddrStr(c->atom),
        tr_torrentName(c->tor), (int)c->atom->seedProbability, tr_torrentIsPrivate(c->tor) ? "private" : "public",
        tr_torrentIsSeed(c->tor) ? "seed" : "downloader");

#endif

    initiateConnection(mgr, c->tor->swarm, c->atom);
}

static void makeNewPeerConnections(struct tr_peerMgr* mgr, int const max)
{
    int n;
    struct peer_candidate* candidates;

    candidates = getPeerCandidates(mgr->session, &n, max);

    for (int i = 0; i < n && i < max; ++i)
    {
        initiateCandidateConnection(mgr, &candidates[i]);
    }

    tr_free(candidates);
}
