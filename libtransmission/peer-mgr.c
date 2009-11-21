/*
 * This file Copyright (C) 2007-2009 Charles Kerr <charles@transmissionbt.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#include <assert.h>
#include <errno.h>
#include <string.h> /* memcpy, memcmp, strstr */
#include <stdlib.h> /* qsort */
#include <limits.h> /* INT_MAX */

#include <event.h>

#include "transmission.h"
#include "bandwidth.h"
#include "bencode.h"
#include "blocklist.h"
#include "clients.h"
#include "completion.h"
#include "crypto.h"
#include "handshake.h"
#include "inout.h" /* tr_ioTestPiece */
#include "net.h"
#include "peer-io.h"
#include "peer-mgr.h"
#include "peer-msgs.h"
#include "ptrarray.h"
#include "session.h"
#include "stats.h" /* tr_statsAddUploaded, tr_statsAddDownloaded */
#include "torrent.h"
#include "trevent.h"
#include "utils.h"
#include "webseed.h"

enum
{
    /* how frequently to change which peers are choked */
    RECHOKE_PERIOD_MSEC = ( 10 * 1000 ),

    /* minimum interval for refilling peers' request lists */
    REFILL_PERIOD_MSEC = 400,

    /* how frequently to reallocate bandwidth */
    BANDWIDTH_PERIOD_MSEC = 500,

    /* how frequently to age out old piece request lists */
    REFILL_UPKEEP_PERIOD_MSEC = ( 10 * 1000 ),

    /* how frequently to decide which peers live and die */
    RECONNECT_PERIOD_MSEC = 500,

    /* when many peers are available, keep idle ones this long */
    MIN_UPLOAD_IDLE_SECS = ( 60 ),

    /* when few peers are available, keep idle ones this long */
    MAX_UPLOAD_IDLE_SECS = ( 60 * 5 ),

    /* max # of peers to ask fer per torrent per reconnect pulse */
    MAX_RECONNECTIONS_PER_PULSE = 8,

    /* max number of peers to ask for per second overall.
    * this throttle is to avoid overloading the router */
    MAX_CONNECTIONS_PER_SECOND = 16,

    /* number of bad pieces a peer is allowed to send before we ban them */
    MAX_BAD_PIECES_PER_PEER = 5,

    /* amount of time to keep a list of request pieces lying around
       before it's considered too old and needs to be rebuilt */
    PIECE_LIST_SHELF_LIFE_SECS = 60,

    /* use for bitwise operations w/peer_atom.myflags */
    MYFLAG_BANNED = 1,

    /* use for bitwise operations w/peer_atom.myflags */
    /* unreachable for now... but not banned.
     * if they try to connect to us it's okay */
    MYFLAG_UNREACHABLE = 2,

    /* the minimum we'll wait before attempting to reconnect to a peer */
    MINIMUM_RECONNECT_INTERVAL_SECS = 5
};


/**
***
**/

enum
{
    UPLOAD_ONLY_UKNOWN,
    UPLOAD_ONLY_YES,
    UPLOAD_ONLY_NO
};

/**
 * Peer information that should be kept even before we've connected and
 * after we've disconnected.  These are kept in a pool of peer_atoms to decide
 * which ones would make good candidates for connecting to, and to watch out
 * for banned peers.
 *
 * @see tr_peer
 * @see tr_peermsgs
 */
struct peer_atom
{
    uint8_t     from;
    uint8_t     flags;       /* these match the added_f flags */
    uint8_t     myflags;     /* flags that aren't defined in added_f */
    uint8_t     uploadOnly;  /* UPLOAD_ONLY_ */
    tr_port     port;
    uint16_t    numFails;
    tr_address  addr;
    time_t      time;        /* when the peer's connection status last changed */
    time_t      piece_data_time;
};

static tr_bool
tr_isAtom( const struct peer_atom * atom )
{
    return ( atom != NULL )
        && ( atom->from < TR_PEER_FROM__MAX )
        && ( tr_isAddress( &atom->addr ) );
}

static const char*
tr_atomAddrStr( const struct peer_atom * atom )
{
    return tr_peerIoAddrStr( &atom->addr, atom->port );
}

struct block_request
{
    tr_block_index_t block;
    tr_peer * peer;
    time_t sentAt;
};

struct weighted_piece
{
    tr_piece_index_t index;
    int16_t salt;
    int16_t requestCount;
};

typedef struct tr_torrent_peers
{
    struct event               refillTimer;

    tr_ptrArray                outgoingHandshakes; /* tr_handshake */
    tr_ptrArray                pool; /* struct peer_atom */
    tr_ptrArray                peers; /* tr_peer */
    tr_ptrArray                webseeds; /* tr_webseed */

    tr_torrent               * tor;
    tr_peer                  * optimistic; /* the optimistic peer, or NULL if none */
    struct tr_peerMgr        * manager;
    //int                      * pendingRequestCount;

    tr_bool                    isRunning;
    tr_bool                    needsCompletenessCheck;

    struct block_request     * requests;
    int                        requestsSort;
    int                        requestCount;
    int                        requestAlloc;

    struct weighted_piece    * pieces;
    int                        piecesSort;
    int                        pieceCount;

    tr_bool                    isInEndgame;
}
Torrent;

struct tr_peerMgr
{
    tr_session      * session;
    tr_ptrArray       incomingHandshakes; /* tr_handshake */
    tr_timer        * bandwidthTimer;
    tr_timer        * rechokeTimer;
    tr_timer        * reconnectTimer;
    tr_timer        * refillUpkeepTimer;
};

#define tordbg( t, ... ) \
    do { \
        if( tr_deepLoggingIsActive( ) ) \
            tr_deepLog( __FILE__, __LINE__, tr_torrentName( t->tor ), __VA_ARGS__ ); \
    } while( 0 )

#define dbgmsg( ... ) \
    do { \
        if( tr_deepLoggingIsActive( ) ) \
            tr_deepLog( __FILE__, __LINE__, NULL, __VA_ARGS__ ); \
    } while( 0 )

/**
***
**/

static TR_INLINE void
managerLock( const struct tr_peerMgr * manager )
{
    tr_globalLock( manager->session );
}

static TR_INLINE void
managerUnlock( const struct tr_peerMgr * manager )
{
    tr_globalUnlock( manager->session );
}

static TR_INLINE void
torrentLock( Torrent * torrent )
{
    managerLock( torrent->manager );
}

static TR_INLINE void
torrentUnlock( Torrent * torrent )
{
    managerUnlock( torrent->manager );
}

static TR_INLINE int
torrentIsLocked( const Torrent * t )
{
    return tr_globalIsLocked( t->manager->session );
}

/**
***
**/

static int
handshakeCompareToAddr( const void * va, const void * vb )
{
    const tr_handshake * a = va;

    return tr_compareAddresses( tr_handshakeGetAddr( a, NULL ), vb );
}

static int
handshakeCompare( const void * a, const void * b )
{
    return handshakeCompareToAddr( a, tr_handshakeGetAddr( b, NULL ) );
}

static tr_handshake*
getExistingHandshake( tr_ptrArray      * handshakes,
                      const tr_address * addr )
{
    return tr_ptrArrayFindSorted( handshakes, addr, handshakeCompareToAddr );
}

static int
comparePeerAtomToAddress( const void * va, const void * vb )
{
    const struct peer_atom * a = va;

    return tr_compareAddresses( &a->addr, vb );
}

static int
comparePeerAtoms( const void * va, const void * vb )
{
    const struct peer_atom * b = vb;

    assert( tr_isAtom( b ) );

    return comparePeerAtomToAddress( va, &b->addr );
}

/**
***
**/

const tr_address *
tr_peerAddress( const tr_peer * peer )
{
    return &peer->atom->addr;
}

static Torrent*
getExistingTorrent( tr_peerMgr *    manager,
                    const uint8_t * hash )
{
    tr_torrent * tor = tr_torrentFindFromHash( manager->session, hash );

    return tor == NULL ? NULL : tor->torrentPeers;
}

static int
peerCompare( const void * a, const void * b )
{
    return tr_compareAddresses( tr_peerAddress( a ), tr_peerAddress( b ) );
}

static int
peerCompareToAddr( const void * a, const void * vb )
{
    return tr_compareAddresses( tr_peerAddress( a ), vb );
}

static tr_peer*
getExistingPeer( Torrent          * torrent,
                 const tr_address * addr )
{
    assert( torrentIsLocked( torrent ) );
    assert( addr );

    return tr_ptrArrayFindSorted( &torrent->peers, addr, peerCompareToAddr );
}

static struct peer_atom*
getExistingAtom( const Torrent    * t,
                 const tr_address * addr )
{
    Torrent * tt = (Torrent*)t;
    assert( torrentIsLocked( t ) );
    return tr_ptrArrayFindSorted( &tt->pool, addr, comparePeerAtomToAddress );
}

static tr_bool
peerIsInUse( const Torrent    * ct,
             const tr_address * addr )
{
    Torrent * t = (Torrent*) ct;

    assert( torrentIsLocked ( t ) );

    return getExistingPeer( t, addr )
        || getExistingHandshake( &t->outgoingHandshakes, addr )
        || getExistingHandshake( &t->manager->incomingHandshakes, addr );
}

static tr_peer*
peerConstructor( struct peer_atom * atom )
{
    tr_peer * peer = tr_new0( tr_peer, 1 );
    peer->atom = atom;
    return peer;
}

static tr_peer*
getPeer( Torrent * torrent, struct peer_atom * atom )
{
    tr_peer * peer;

    assert( torrentIsLocked( torrent ) );

    peer = getExistingPeer( torrent, &atom->addr );

    if( peer == NULL )
    {
        peer = peerConstructor( atom );
        tr_ptrArrayInsertSorted( &torrent->peers, peer, peerCompare );
    }

    return peer;
}

static void peerDeclinedAllRequests( Torrent *, const tr_peer * );

static void
peerDestructor( Torrent * t, tr_peer * peer )
{
    assert( peer != NULL );

    peerDeclinedAllRequests( t, peer );

    if( peer->msgs != NULL )
    {
        tr_peerMsgsUnsubscribe( peer->msgs, peer->msgsTag );
        tr_peerMsgsFree( peer->msgs );
    }

    tr_peerIoClear( peer->io );
    tr_peerIoUnref( peer->io ); /* balanced by the ref in handshakeDoneCB() */

    tr_bitfieldFree( peer->have );
    tr_bitfieldFree( peer->blame );
    tr_free( peer->client );

    tr_free( peer );
}

static void
removePeer( Torrent * t, tr_peer * peer )
{
    tr_peer * removed;
    struct peer_atom * atom = peer->atom;

    assert( torrentIsLocked( t ) );
    assert( atom );

    atom->time = time( NULL );

    removed = tr_ptrArrayRemoveSorted( &t->peers, peer, peerCompare );
    assert( removed == peer );
    peerDestructor( t, removed );
}

static void
removeAllPeers( Torrent * t )
{
    while( !tr_ptrArrayEmpty( &t->peers ) )
        removePeer( t, tr_ptrArrayNth( &t->peers, 0 ) );
}

static void
torrentDestructor( void * vt )
{
    Torrent * t = vt;

    assert( t );
    assert( !t->isRunning );
    assert( torrentIsLocked( t ) );
    assert( tr_ptrArrayEmpty( &t->outgoingHandshakes ) );
    assert( tr_ptrArrayEmpty( &t->peers ) );

    evtimer_del( &t->refillTimer );

    tr_ptrArrayDestruct( &t->webseeds, (PtrArrayForeachFunc)tr_webseedFree );
    tr_ptrArrayDestruct( &t->pool, (PtrArrayForeachFunc)tr_free );
    tr_ptrArrayDestruct( &t->outgoingHandshakes, NULL );
    tr_ptrArrayDestruct( &t->peers, NULL );

    tr_free( t->requests );
    tr_free( t->pieces );
    tr_free( t );
}


//static void refillPulse( int, short, void* );

static void peerCallbackFunc( void * vpeer,
                              void * vevent,
                              void * vt );

static Torrent*
torrentConstructor( tr_peerMgr * manager,
                    tr_torrent * tor )
{
    int       i;
    Torrent * t;

    t = tr_new0( Torrent, 1 );
    t->manager = manager;
    t->tor = tor;
    t->pool = TR_PTR_ARRAY_INIT;
    t->peers = TR_PTR_ARRAY_INIT;
    t->webseeds = TR_PTR_ARRAY_INIT;
    t->outgoingHandshakes = TR_PTR_ARRAY_INIT;
    t->requests = 0;
    //evtimer_set( &t->refillTimer, refillPulse, t );


    for( i = 0; i < tor->info.webseedCount; ++i )
    {
        tr_webseed * w =
            tr_webseedNew( tor, tor->info.webseeds[i], peerCallbackFunc, t );
        tr_ptrArrayAppend( &t->webseeds, w );
    }

    return t;
}


static int bandwidthPulse ( void * vmgr );
static int rechokePulse   ( void * vmgr );
static int reconnectPulse ( void * vmgr );
static int refillUpkeep   ( void * vmgr );

tr_peerMgr*
tr_peerMgrNew( tr_session * session )
{
    tr_peerMgr * m = tr_new0( tr_peerMgr, 1 );
    m->session = session;
    m->incomingHandshakes = TR_PTR_ARRAY_INIT;
    return m;
}

static void
deleteTimers( struct tr_peerMgr * m )
{
    if( m->bandwidthTimer )
        tr_timerFree( &m->bandwidthTimer );

    if( m->rechokeTimer )
        tr_timerFree( &m->rechokeTimer );

    if( m->reconnectTimer )
        tr_timerFree( &m->reconnectTimer );

    if( m->refillUpkeepTimer )
        tr_timerFree( &m->refillUpkeepTimer );
}

void
tr_peerMgrFree( tr_peerMgr * manager )
{
    managerLock( manager );

    deleteTimers( manager );

    /* free the handshakes.  Abort invokes handshakeDoneCB(), which removes
     * the item from manager->handshakes, so this is a little roundabout... */
    while( !tr_ptrArrayEmpty( &manager->incomingHandshakes ) )
        tr_handshakeAbort( tr_ptrArrayNth( &manager->incomingHandshakes, 0 ) );

    tr_ptrArrayDestruct( &manager->incomingHandshakes, NULL );

    managerUnlock( manager );
    tr_free( manager );
}

static int
clientIsDownloadingFrom( const tr_peer * peer )
{
    return peer->clientIsInterested && !peer->clientIsChoked;
}

static int
clientIsUploadingTo( const tr_peer * peer )
{
    return peer->peerIsInterested && !peer->peerIsChoked;
}

/***
****
***/

tr_bool
tr_peerMgrPeerIsSeed( const tr_torrent  * tor,
                      const tr_address  * addr )
{
    tr_bool isSeed = FALSE;
    const Torrent * t = tor->torrentPeers;
    const struct peer_atom * atom = getExistingAtom( t, addr );

    if( atom )
        isSeed = ( atom->flags & ADDED_F_SEED_FLAG ) != 0;

    return isSeed;
}

/**
***  REQUESTS
***
*** There are two data structures associated with managing block requests:
*** 
*** 1. Torrent::requests, an array of "struct block_request" which keeps
***    track of which blocks have been requested, and when, and by which peers.
***    This is list is used for (a) cancelling requests that have been pending
***    for too long and (b) avoiding duplicate requests before endgame.
*** 
*** 2. Torrent::pieces, an array of "struct weighted_piece" which lists the
***    pieces that we want to request.  It's used to decide which pieces to
***    return next when tr_peerMgrGetBlockRequests() is called.
**/ 

/**
*** struct block_request
**/

enum
{
    REQ_UNSORTED,
    REQ_SORTED_BY_BLOCK,
    REQ_SORTED_BY_TIME
};

static int
compareReqByBlock( const void * va, const void * vb )
{
    const struct block_request * a = va;
    const struct block_request * b = vb;
    if( a->block < b->block ) return -1;
    if( a->block > b->block ) return 1;
    return 0;
}

static int
compareReqByTime( const void * va, const void * vb )
{
    const struct block_request * a = va;
    const struct block_request * b = vb;
    if( a->sentAt < b->sentAt ) return -1;
    if( a->sentAt > b->sentAt ) return 1;
    return 0;
}

static void
requestListSort( Torrent * t, int mode )
{
    assert( mode==REQ_SORTED_BY_BLOCK || mode==REQ_SORTED_BY_TIME );

    if( t->requestsSort != mode )
    {
        int(*compar)(const void *, const void *);

        t->requestsSort = mode;

        switch( mode ) {
            case REQ_SORTED_BY_BLOCK: compar = compareReqByBlock; break;
            case REQ_SORTED_BY_TIME: compar = compareReqByTime; break;
            default: assert( 0 && "unhandled" );
        }

//fprintf( stderr, "sorting requests by %s\n", (mode==REQ_SORTED_BY_BLOCK)?"block":"time" ); 
        qsort( t->requests, t->requestCount,
               sizeof( struct block_request ), compar );
    }
}

static void
requestListAdd( Torrent * t, const time_t now, tr_block_index_t block, tr_peer * peer )
{
    struct block_request key;

    /* ensure enough room is available... */
    if( t->requestCount + 1 >= t->requestAlloc )
    {
        const int CHUNK_SIZE = 128;
        t->requestAlloc += CHUNK_SIZE;
        t->requests = tr_renew( struct block_request,
                                t->requests, t->requestAlloc );
    }

    /* populate the record we're inserting */
    key.block = block;
    key.peer = peer;
    key.sentAt = now;

    /* insert the request to our array... */
    switch( t->requestsSort )
    {
        case REQ_UNSORTED:
        case REQ_SORTED_BY_TIME:
            t->requests[t->requestCount++] = key;
            break;

        case REQ_SORTED_BY_BLOCK: {
            tr_bool exact;
            const int pos = tr_lowerBound( &key, t->requests, t->requestCount,
                                           sizeof( struct block_request ),
                                           compareReqByBlock, &exact );
            assert( !exact );
            memmove( t->requests + pos + 1,
                     t->requests + pos,
                     sizeof( struct block_request ) * ( t->requestCount++ - pos ) );
            t->requests[pos] = key;
            break;
        }
    }
}

static struct block_request *
requestListLookup( Torrent * t, tr_block_index_t block )
{
    struct block_request key;
    key.block = block;

    requestListSort( t, REQ_SORTED_BY_BLOCK );

    return bsearch( &key, t->requests, t->requestCount,
                    sizeof( struct block_request ),
                    compareReqByBlock );
}

static void
requestListRemove( Torrent * t, tr_block_index_t block )
{
    const struct block_request * b = requestListLookup( t, block );
    if( b != NULL )
    {
        const int pos = b - t->requests;
        assert( pos < t->requestCount );
        memmove( t->requests + pos,
                 t->requests + pos + 1,
                 sizeof( struct block_request ) * ( --t->requestCount - pos ) );
    }
}

/**
*** struct weighted_piece
**/

enum
{
    PIECES_UNSORTED,
    PIECES_SORTED_BY_INDEX,
    PIECES_SORTED_BY_WEIGHT
};

const tr_torrent * weightTorrent;

/* we try to create a "weight" s.t. high-priority pieces come before others,
 * and that partially-complete pieces come before empty ones. */
static int
comparePieceByWeight( const void * va, const void * vb )
{
    const struct weighted_piece * a = va;
    const struct weighted_piece * b = vb;
    int ia, ib, missing, pending;
    const tr_torrent * tor = weightTorrent;

    /* primary key: weight */
    missing = tr_cpMissingBlocksInPiece( &tor->completion, a->index );
    pending = a->requestCount;
    ia = missing > pending ? missing - pending : (int)(tor->blockCountInPiece + pending);
    missing = tr_cpMissingBlocksInPiece( &tor->completion, b->index );
    pending = b->requestCount;
    ib = missing > pending ? missing - pending : (int)(tor->blockCountInPiece + pending);
    if( ia < ib ) return -1;
    if( ia > ib ) return 1;

    /* secondary key: higher priorities go first */
    ia = tor->info.pieces[a->index].priority;
    ib = tor->info.pieces[b->index].priority;
    if( ia > ib ) return -1;
    if( ia < ib ) return 1;

    /* tertiary key: random */
    return a->salt - b->salt;
}

static int
comparePieceByIndex( const void * va, const void * vb )
{
    const struct weighted_piece * a = va;
    const struct weighted_piece * b = vb;
    if( a->index < b->index ) return -1;
    if( a->index > b->index ) return 1;
    return 0;
}

static void
pieceListSort( Torrent * t, int mode )
{
    int(*compar)(const void *, const void *);

    assert( mode==PIECES_SORTED_BY_INDEX
         || mode==PIECES_SORTED_BY_WEIGHT );

    if( t->piecesSort != mode )
    {
//fprintf( stderr, "sort mode was %d, is now %d\n", t->piecesSort, mode );
        t->piecesSort = mode;

        switch( mode ) {
            case PIECES_SORTED_BY_WEIGHT: compar = comparePieceByWeight; break;
            case PIECES_SORTED_BY_INDEX: compar = comparePieceByIndex; break;
            default: assert( 0 && "unhandled" );  break;
        }

//fprintf( stderr, "sorting pieces by %s...\n", (mode==PIECES_SORTED_BY_WEIGHT)?"weight":"index" );
        weightTorrent = t->tor;
        qsort( t->pieces, t->pieceCount,
               sizeof( struct weighted_piece ), compar );
    }

    /* Also, as long as we've got the pieces sorted by weight,
     * let's also update t.isInEndgame */
    if( t->piecesSort == PIECES_SORTED_BY_WEIGHT )
    {
        tr_bool endgame = TRUE;

        if( ( t->pieces != NULL ) && ( t->pieceCount > 0 ) )
        {
            const tr_completion * cp = &t->tor->completion;
            const struct weighted_piece * p = t->pieces;
            const int pending = p->requestCount;
            const int missing = tr_cpMissingBlocksInPiece( cp, p->index );
            endgame = pending >= missing;
        }

        t->isInEndgame = endgame;
    }
}

static struct weighted_piece *
pieceListLookup( Torrent * t, tr_piece_index_t index )
{
    struct weighted_piece key;
    key.index = index;

    pieceListSort( t, PIECES_SORTED_BY_INDEX );

    return bsearch( &key, t->pieces, t->pieceCount,
                    sizeof( struct weighted_piece ),
                    comparePieceByIndex );
}

static void
pieceListRebuild( Torrent * t )
{
    if( !tr_torrentIsSeed( t->tor ) )
    {
        tr_piece_index_t i;
        tr_piece_index_t * pool;
        tr_piece_index_t poolCount = 0;
        const tr_torrent * tor = t->tor;
        const tr_info * inf = tr_torrentInfo( tor );
        struct weighted_piece * pieces;
        int pieceCount;

        /* build the new list */
        pool = tr_new( tr_piece_index_t, inf->pieceCount );
        for( i=0; i<inf->pieceCount; ++i )
            if( !inf->pieces[i].dnd )
                if( !tr_cpPieceIsComplete( &tor->completion, i ) )
                    pool[poolCount++] = i;
        pieceCount = poolCount;
        pieces = tr_new0( struct weighted_piece, pieceCount );
        for( i=0; i<poolCount; ++i ) {
            struct weighted_piece * piece = pieces + i;
            piece->index = pool[i];
            piece->requestCount = 0;
            piece->salt = tr_cryptoWeakRandInt( 255 );
        }

        /* if we already had a list of pieces, merge it into
         * the new list so we don't lose its requestCounts */
        if( t->pieces != NULL )
        {
            struct weighted_piece * o = t->pieces;
            struct weighted_piece * oend = o + t->pieceCount;
            struct weighted_piece * n = pieces;
            struct weighted_piece * nend = n + pieceCount;

            pieceListSort( t, PIECES_SORTED_BY_INDEX );

            while( o!=oend && n!=nend ) {
                if( o->index < n->index )
                    ++o;
                else if( o->index > n->index )
                    ++n;
                else
                    *n++ = *o++;
            }

            tr_free( t->pieces );
        }

        t->pieces = pieces;
        t->pieceCount = pieceCount;
        t->piecesSort = PIECES_SORTED_BY_INDEX;

        /* cleanup */
        tr_free( pool );
    }
}

static void
pieceListRemovePiece( Torrent * t, tr_piece_index_t piece )
{
    struct weighted_piece * p = pieceListLookup( t, piece );

    if( p != NULL )
    {
        const int pos = p - t->pieces;

        memmove( t->pieces + pos,
                 t->pieces + pos + 1,
                 sizeof( struct weighted_piece ) * ( --t->pieceCount - pos ) );

        if( t->pieceCount == 0 )
        {
            tr_free( t->pieces );
            t->pieces = NULL;
        }
    }
}

static void
pieceListRemoveRequest( Torrent * t, tr_block_index_t block )
{
    struct weighted_piece * p;
    const tr_piece_index_t index = tr_torBlockPiece( t->tor, block );

    if(( p = pieceListLookup( t, index )))
        if( p->requestCount > 0 )
            --p->requestCount;

    /* note: this invalidates the weighted.piece.weight field,
     * but that's OK since the call to pieceListLookup ensured
     * that we were sorted by index anyway.. next time we resort
     * by weight, pieceListSort() will update the weights */
}

/**
***
**/

void
tr_peerMgrRebuildRequests( tr_torrent * tor )
{
    assert( tr_isTorrent( tor ) );

    pieceListRebuild( tor->torrentPeers );
}

void
tr_peerMgrGetNextRequests( tr_torrent           * tor,
                           tr_peer              * peer,
                           int                    numwant,
                           tr_block_index_t     * setme,
                           int                  * numgot )
{
    int i;
    int got;
    Torrent * t;
    struct weighted_piece * pieces;
    const tr_bitfield * have = peer->have;
    const time_t now = time( NULL );

    /* sanity clause */
    assert( tr_isTorrent( tor ) );
    assert( numwant > 0 );

    /* walk through the pieces and find blocks that should be requested */
    got = 0;
    t = tor->torrentPeers;

    /* prep the pieces list */
    if( t->pieces == NULL )
        pieceListRebuild( t );
    pieceListSort( t, PIECES_SORTED_BY_WEIGHT );
//if( t->isInEndgame ) fprintf( stderr, "endgame\n" );

#if 0
{
int i=0, n=MIN(10,t->pieceCount);
fprintf( stderr, "the next pieces we want to request are " );
for( i=0; i<n; i++ ) fprintf( stderr, "%d(weight:%d) ", (int)t->pieces[i].index, (int)t->pieces[i].weight );
fprintf( stderr, "\n" );
}
#endif

    pieces = t->pieces;
    for( i=0; i<t->pieceCount && got<numwant; ++i )
    {
        struct weighted_piece * p = pieces + i;

        /* if the peer has this piece that we want... */
        if( tr_bitfieldHasFast( have, p->index ) )
        {
            tr_block_index_t b = tr_torPieceFirstBlock( tor, p->index );
            const tr_block_index_t e = b + tr_torPieceCountBlocks( tor, p->index );

            for( ; b!=e && got<numwant; ++b )
            {
                struct block_request * breq;

                /* don't request blocks we've already got */
                if( tr_cpBlockIsCompleteFast( &tor->completion, b ) )
                    continue;

                /* don't request blocks we've already requested (FIXME) */
                breq = requestListLookup( t, b );
                if( breq != NULL ) {
                    assert( breq->peer != NULL );
                    if( breq->peer == peer ) continue;
                    if( !t->isInEndgame ) continue;
                }

                setme[got++] = b;
//fprintf( stderr, "peer %p is requesting block %"PRIu64"\n", peer, b );

                /* update our own tables */
                if( breq == NULL )
                    requestListAdd( t, now, b, peer );
                ++p->requestCount;
            }
        }
    }

    /* We almost always change only a handful of pieces in the array.
     * In these cases, it's cheaper to sort those changed pieces and merge,
     * than qsort()ing the whole array again */
    if( got > 0 )
    {
        struct weighted_piece * p;
        struct weighted_piece * pieces;
        struct weighted_piece * a = t->pieces;
        struct weighted_piece * a_end = t->pieces + i;
        struct weighted_piece * b = a_end;
        struct weighted_piece * b_end = t->pieces + t->pieceCount;

        /* rescore the pieces that we changed */
        weightTorrent = t->tor;
//fprintf( stderr, "sorting %d changed pieces...\n", (int)(a_end-a) );
        qsort( a, a_end-a, sizeof( struct weighted_piece ), comparePieceByWeight );

        /* allocate a new array */
        p = pieces = tr_new( struct weighted_piece, t->pieceCount );

        /* merge the two sorted arrays into this new array */
        weightTorrent = t->tor;
        while( a!=a_end && b!=b_end )
            *p++ = comparePieceByWeight( a, b ) < 0 ? *a++ : *b++;
        while( a!=a_end ) *p++ = *a++;
        while( b!=b_end ) *p++ = *b++;

#if 0
        /* make sure we did it right */
        assert( p - pieces == t->pieceCount );
        for( it=pieces; it+1<p; ++it )
            assert( it->weight <= it[1].weight );
#endif

        /* update */
        tr_free( t->pieces );
        t->pieces = pieces;
    }

    //fprintf( stderr, "peer %p wanted %d requests; got %d\n", peer, numwant, got );
    *numgot = got;
}

tr_bool
tr_peerMgrDidPeerRequest( const tr_torrent  * tor,
                          const tr_peer     * peer,
                          tr_block_index_t    block )
{
    const Torrent * t = tor->torrentPeers;
    const struct block_request * b = requestListLookup( (Torrent*)t, block );
    if( b == NULL ) return FALSE;
    if( b->peer == peer ) return TRUE;
    if( t->isInEndgame ) return TRUE;
    return FALSE;
}

/* cancel requests that are too old */
static int
refillUpkeep( void * vmgr )
{
    time_t now;
    time_t too_old;
    tr_torrent * tor;
    tr_peerMgr * mgr = vmgr;
    managerLock( mgr );

    now = time( NULL );
    too_old = now - REQUEST_TTL_SECS;

    tor = NULL;
    while(( tor = tr_torrentNext( mgr->session, tor )))
    {
        Torrent * t = tor->torrentPeers;
        const int n = t->requestCount;
        if( n > 0 )
        {
            int keepCount = 0;
            int cancelCount = 0;
            struct block_request * keep = tr_new( struct block_request, n );
            struct block_request * cancel = tr_new( struct block_request, n );
            const struct block_request * it;
            const struct block_request * end;

            for( it=t->requests, end=it+n; it!=end; ++it )
                if( it->sentAt <= too_old )
                    cancel[cancelCount++] = *it;
                else
                    keep[keepCount++] = *it;

            /* prune out the ones we aren't keeping */
            tr_free( t->requests );
            t->requests = keep;
            t->requestCount = keepCount;
            t->requestAlloc = n;

            /* send cancel messages for all the "cancel" ones */
            for( it=cancel, end=it+cancelCount; it!=end; ++it )
                if( ( it->peer != NULL ) && ( it->peer->msgs != NULL ) )
                    tr_peerMsgsCancel( it->peer->msgs, it->block );

            /* decrement the pending request counts for the timed-out blocks */
            for( it=cancel, end=it+cancelCount; it!=end; ++it )
                pieceListRemoveRequest( t, it->block );

            /* cleanup loop */
            tr_free( cancel );
        }
    }

    managerUnlock( mgr );
    return TRUE;
}

static void
addStrike( Torrent * t, tr_peer * peer )
{
    tordbg( t, "increasing peer %s strike count to %d",
            tr_atomAddrStr( peer->atom ), peer->strikes + 1 );

    if( ++peer->strikes >= MAX_BAD_PIECES_PER_PEER )
    {
        struct peer_atom * atom = peer->atom;
        atom->myflags |= MYFLAG_BANNED;
        peer->doPurge = 1;
        tordbg( t, "banning peer %s", tr_atomAddrStr( atom ) );
    }
}

static void
gotBadPiece( Torrent * t, tr_piece_index_t pieceIndex )
{
    tr_torrent *   tor = t->tor;
    const uint32_t byteCount = tr_torPieceCountBytes( tor, pieceIndex );

    tor->corruptCur += byteCount;
    tor->downloadedCur -= MIN( tor->downloadedCur, byteCount );
}

static void
peerSuggestedPiece( Torrent            * t UNUSED,
                    tr_peer            * peer UNUSED,
                    tr_piece_index_t     pieceIndex UNUSED,
                    int                  isFastAllowed UNUSED )
{
#if 0
    assert( t );
    assert( peer );
    assert( peer->msgs );

    /* is this a valid piece? */
    if(  pieceIndex >= t->tor->info.pieceCount )
        return;

    /* don't ask for it if we've already got it */
    if( tr_cpPieceIsComplete( t->tor->completion, pieceIndex ) )
        return;

    /* don't ask for it if they don't have it */
    if( !tr_bitfieldHas( peer->have, pieceIndex ) )
        return;

    /* don't ask for it if we're choked and it's not fast */
    if( !isFastAllowed && peer->clientIsChoked )
        return;

    /* request the blocks that we don't have in this piece */
    {
        tr_block_index_t block;
        const tr_torrent * tor = t->tor;
        const tr_block_index_t start = tr_torPieceFirstBlock( tor, pieceIndex );
        const tr_block_index_t end = start + tr_torPieceCountBlocks( tor, pieceIndex );

        for( block=start; block<end; ++block )
        {
            if( !tr_cpBlockIsComplete( tor->completion, block ) )
            {
                const uint32_t offset = getBlockOffsetInPiece( tor, block );
                const uint32_t length = tr_torBlockCountBytes( tor, block );
                tr_peerMsgsAddRequest( peer->msgs, pieceIndex, offset, length );
                incrementPieceRequests( t, pieceIndex );
            }
        }
    }
#endif
}

static void
decrementDownloadedCount( tr_torrent * tor, uint32_t byteCount )
{
    tor->downloadedCur -= MIN( tor->downloadedCur, byteCount );
}

static void
clientGotUnwantedBlock( tr_torrent * tor, tr_block_index_t block )
{
    decrementDownloadedCount( tor, tr_torBlockCountBytes( tor, block ) );
}

static void
removeRequestFromTables( Torrent * t, tr_block_index_t block )
{
    requestListRemove( t, block );
    pieceListRemoveRequest( t, block );
}

/* peer choked us, or maybe it disconnected.
   either way we need to remove all its requests */
static void
peerDeclinedAllRequests( Torrent * t, const tr_peer * peer )
{
    int i, n;
    tr_block_index_t * blocks = tr_new( tr_block_index_t, t->requestCount );

    for( i=n=0; i<t->requestCount; ++i )
        if( peer == t->requests[i].peer )
            blocks[n++] = t->requests[i].block;

    for( i=0; i<n; ++i )
        removeRequestFromTables( t, blocks[i] );

    tr_free( blocks );
}

static void
peerCallbackFunc( void * vpeer, void * vevent, void * vt )
{
    tr_peer * peer = vpeer; /* may be NULL if peer is a webseed */
    Torrent * t = vt;
    const tr_peer_event * e = vevent;

    torrentLock( t );

    switch( e->eventType )
    {
        case TR_PEER_UPLOAD_ONLY:
            /* update our atom */
            if( peer ) {
                if( e->uploadOnly ) {
                    peer->atom->uploadOnly = UPLOAD_ONLY_YES;
                    peer->atom->flags |= ADDED_F_SEED_FLAG;
                } else {
                    peer->atom->uploadOnly = UPLOAD_ONLY_NO;
                    peer->atom->flags &= ~ADDED_F_SEED_FLAG;
                }
            }
            break;

        case TR_PEER_PEER_GOT_DATA:
        {
            const time_t now = time( NULL );
            tr_torrent * tor = t->tor;

            tr_torrentSetActivityDate( tor, now );

            if( e->wasPieceData ) {
                tor->uploadedCur += e->length;
                tr_torrentSetDirty( tor );
            }

            /* update the stats */
            if( e->wasPieceData )
                tr_statsAddUploaded( tor->session, e->length );

            /* update our atom */
            if( peer && e->wasPieceData )
                peer->atom->piece_data_time = now;

            tor->needsSeedRatioCheck = TRUE;

            break;
        }

        case TR_PEER_CLIENT_GOT_REJ:
            removeRequestFromTables( t, _tr_block( t->tor, e->pieceIndex, e->offset ) );
            break;

        case TR_PEER_CLIENT_GOT_CHOKE:
            peerDeclinedAllRequests( t, peer );
            break;

        case TR_PEER_CLIENT_GOT_PORT:
            if( peer )
                peer->atom->port = e->port;
            break;

        case TR_PEER_CLIENT_GOT_SUGGEST:
            if( peer )
                peerSuggestedPiece( t, peer, e->pieceIndex, FALSE );
            break;

        case TR_PEER_CLIENT_GOT_ALLOWED_FAST:
            if( peer )
                peerSuggestedPiece( t, peer, e->pieceIndex, TRUE );
            break;

        case TR_PEER_CLIENT_GOT_DATA:
        {
            const time_t now = time( NULL );
            tr_torrent * tor = t->tor;

            tr_torrentSetActivityDate( tor, now );

            /* only add this to downloadedCur if we got it from a peer --
             * webseeds shouldn't count against our ratio.  As one tracker
             * admin put it, "Those pieces are downloaded directly from the
             * content distributor, not the peers, it is the tracker's job
             * to manage the swarms, not the web server and does not fit
             * into the jurisdiction of the tracker." */
            if( peer && e->wasPieceData ) {
                tor->downloadedCur += e->length;
                tr_torrentSetDirty( tor );
            }

            /* update the stats */
            if( e->wasPieceData )
                tr_statsAddDownloaded( tor->session, e->length );

            /* update our atom */
            if( peer && e->wasPieceData )
                peer->atom->piece_data_time = now;

            break;
        }

        case TR_PEER_PEER_PROGRESS:
        {
            if( peer )
            {
                struct peer_atom * atom = peer->atom;
                if( e->progress >= 1.0 ) {
                    tordbg( t, "marking peer %s as a seed",
                            tr_atomAddrStr( atom ) );
                    atom->flags |= ADDED_F_SEED_FLAG;
                }
            }
            break;
        }

        case TR_PEER_CLIENT_GOT_BLOCK:
        {
            tr_torrent * tor = t->tor;
            tr_block_index_t block = _tr_block( tor, e->pieceIndex, e->offset );
//static int numBlocks = 0;
//fprintf( stderr, "got a total of %d blocks\n", ++numBlocks );

            requestListRemove( t, block );
            pieceListRemoveRequest( t, block );

            if( tr_cpBlockIsComplete( &tor->completion, block ) )
            {
                tordbg( t, "we have this block already..." );
                clientGotUnwantedBlock( tor, block );
            }
            else
            {
                tr_cpBlockAdd( &tor->completion, block );
                tr_torrentSetDirty( tor );

                if( tr_cpPieceIsComplete( &tor->completion, e->pieceIndex ) )
                {
                    const tr_piece_index_t p = e->pieceIndex;
                    const tr_bool ok = tr_ioTestPiece( tor, p, NULL, 0 );
//fprintf( stderr, "we now have piece #%d\n", (int)p );

                    if( !ok )
                    {
                        tr_torerr( tor, _( "Piece %lu, which was just downloaded, failed its checksum test" ),
                                   (unsigned long)p );
                    }

                    tr_torrentSetHasPiece( tor, p, ok );
                    tr_torrentSetPieceChecked( tor, p, TRUE );
                    tr_peerMgrSetBlame( tor, p, ok );

                    if( !ok )
                    {
                        gotBadPiece( t, p );
                    }
                    else
                    {
                        int i;
                        int peerCount;
                        tr_peer ** peers;
                        tr_file_index_t fileIndex;

                        peerCount = tr_ptrArraySize( &t->peers );
                        peers = (tr_peer**) tr_ptrArrayBase( &t->peers );
                        for( i=0; i<peerCount; ++i )
                            tr_peerMsgsHave( peers[i]->msgs, p );

                        for( fileIndex=0; fileIndex<tor->info.fileCount; ++fileIndex ) {
                            const tr_file * file = &tor->info.files[fileIndex];
                            if( ( file->firstPiece <= p ) && ( p <= file->lastPiece ) )
                                if( tr_cpFileIsComplete( &tor->completion, fileIndex ) )
                                    tr_torrentFileCompleted( tor, fileIndex );
                        }

                        pieceListRemovePiece( t, p );
                    }
                }

                t->needsCompletenessCheck = TRUE;
            }
            break;
        }

        case TR_PEER_ERROR:
            if( ( e->err == ERANGE ) || ( e->err == EMSGSIZE ) || ( e->err == ENOTCONN ) )
            {
                /* some protocol error from the peer */
                peer->doPurge = 1;
                tordbg( t, "setting %s doPurge flag because we got an ERANGE, EMSGSIZE, or ENOTCONN error",
                        tr_atomAddrStr( peer->atom ) );
            }
            else 
            {
                tordbg( t, "unhandled error: %s", tr_strerror( e->err ) );
            }
            break;

        default:
            assert( 0 );
    }

    torrentUnlock( t );
}

static void
ensureAtomExists( Torrent          * t,
                  const tr_address * addr,
                  tr_port            port,
                  uint8_t            flags,
                  uint8_t            from )
{
    assert( tr_isAddress( addr ) );
    assert( from < TR_PEER_FROM__MAX );

    if( getExistingAtom( t, addr ) == NULL )
    {
        struct peer_atom * a;
        a = tr_new0( struct peer_atom, 1 );
        a->addr = *addr;
        a->port = port;
        a->flags = flags;
        a->from = from;
        tordbg( t, "got a new atom: %s", tr_atomAddrStr( a ) );
        tr_ptrArrayInsertSorted( &t->pool, a, comparePeerAtoms );
    }
}

static int
getMaxPeerCount( const tr_torrent * tor )
{
    return tor->maxConnectedPeers;
}

static int
getPeerCount( const Torrent * t )
{
    return tr_ptrArraySize( &t->peers );/* + tr_ptrArraySize( &t->outgoingHandshakes ); */
}

/* FIXME: this is kind of a mess. */
static tr_bool
myHandshakeDoneCB( tr_handshake  * handshake,
                   tr_peerIo     * io,
                   tr_bool         isConnected,
                   const uint8_t * peer_id,
                   void          * vmanager )
{
    tr_bool            ok = isConnected;
    tr_bool            success = FALSE;
    tr_port            port;
    const tr_address * addr;
    tr_peerMgr       * manager = vmanager;
    Torrent          * t;
    tr_handshake     * ours;

    assert( io );
    assert( tr_isBool( ok ) );

    t = tr_peerIoHasTorrentHash( io )
        ? getExistingTorrent( manager, tr_peerIoGetTorrentHash( io ) )
        : NULL;

    if( tr_peerIoIsIncoming ( io ) )
        ours = tr_ptrArrayRemoveSorted( &manager->incomingHandshakes,
                                        handshake, handshakeCompare );
    else if( t )
        ours = tr_ptrArrayRemoveSorted( &t->outgoingHandshakes,
                                        handshake, handshakeCompare );
    else
        ours = handshake;

    assert( ours );
    assert( ours == handshake );

    if( t )
        torrentLock( t );

    addr = tr_peerIoGetAddress( io, &port );

    if( !ok || !t || !t->isRunning )
    {
        if( t )
        {
            struct peer_atom * atom = getExistingAtom( t, addr );
            if( atom )
                ++atom->numFails;
        }
    }
    else /* looking good */
    {
        struct peer_atom * atom;
        ensureAtomExists( t, addr, port, 0, TR_PEER_FROM_INCOMING );
        atom = getExistingAtom( t, addr );
        atom->time = time( NULL );
        atom->piece_data_time = 0;

        if( atom->myflags & MYFLAG_BANNED )
        {
            tordbg( t, "banned peer %s tried to reconnect",
                    tr_atomAddrStr( atom ) );
        }
        else if( tr_peerIoIsIncoming( io )
               && ( getPeerCount( t ) >= getMaxPeerCount( t->tor ) ) )

        {
        }
        else
        {
            tr_peer * peer = getExistingPeer( t, addr );

            if( peer ) /* we already have this peer */
            {
            }
            else
            {
                peer = getPeer( t, atom );
                tr_free( peer->client );

                if( !peer_id )
                    peer->client = NULL;
                else {
                    char client[128];
                    tr_clientForId( client, sizeof( client ), peer_id );
                    peer->client = tr_strdup( client );
                }

                peer->io = tr_handshakeStealIO( handshake ); /* this steals its refcount too, which is
                                                                balanced by our unref in peerDestructor()  */
                tr_peerIoSetParent( peer->io, t->tor->bandwidth );
                tr_peerMsgsNew( t->tor, peer, peerCallbackFunc, t, &peer->msgsTag );

                success = TRUE;
            }
        }
    }

    if( t )
        torrentUnlock( t );

    return success;
}

void
tr_peerMgrAddIncoming( tr_peerMgr * manager,
                       tr_address * addr,
                       tr_port      port,
                       int          socket )
{
    tr_session * session;

    managerLock( manager );

    assert( tr_isSession( manager->session ) );
    session = manager->session;

    if( tr_sessionIsAddressBlocked( session, addr ) )
    {
        tr_dbg( "Banned IP address \"%s\" tried to connect to us", tr_ntop_non_ts( addr ) );
        tr_netClose( session, socket );
    }
    else if( getExistingHandshake( &manager->incomingHandshakes, addr ) )
    {
        tr_netClose( session, socket );
    }
    else /* we don't have a connetion to them yet... */
    {
        tr_peerIo *    io;
        tr_handshake * handshake;

        io = tr_peerIoNewIncoming( session, session->bandwidth, addr, port, socket );

        handshake = tr_handshakeNew( io,
                                     session->encryptionMode,
                                     myHandshakeDoneCB,
                                     manager );

        tr_peerIoUnref( io ); /* balanced by the implicit ref in tr_peerIoNewIncoming() */

        tr_ptrArrayInsertSorted( &manager->incomingHandshakes, handshake,
                                 handshakeCompare );
    }

    managerUnlock( manager );
}

static tr_bool
tr_isPex( const tr_pex * pex )
{
    return pex && tr_isAddress( &pex->addr );
}

void
tr_peerMgrAddPex( tr_torrent   *  tor,
                  uint8_t         from,
                  const tr_pex *  pex )
{
    if( tr_isPex( pex ) ) /* safeguard against corrupt data */
    {
        Torrent * t = tor->torrentPeers;
        managerLock( t->manager );

        if( !tr_sessionIsAddressBlocked( t->manager->session, &pex->addr ) )
            if( tr_isValidPeerAddress( &pex->addr, pex->port ) )
                ensureAtomExists( t, &pex->addr, pex->port, pex->flags, from );

        managerUnlock( t->manager );
    }
}

tr_pex *
tr_peerMgrCompactToPex( const void *    compact,
                        size_t          compactLen,
                        const uint8_t * added_f,
                        size_t          added_f_len,
                        size_t *        pexCount )
{
    size_t          i;
    size_t          n = compactLen / 6;
    const uint8_t * walk = compact;
    tr_pex *        pex = tr_new0( tr_pex, n );

    for( i = 0; i < n; ++i )
    {
        pex[i].addr.type = TR_AF_INET;
        memcpy( &pex[i].addr.addr, walk, 4 ); walk += 4;
        memcpy( &pex[i].port, walk, 2 ); walk += 2;
        if( added_f && ( n == added_f_len ) )
            pex[i].flags = added_f[i];
    }

    *pexCount = n;
    return pex;
}

tr_pex *
tr_peerMgrCompact6ToPex( const void    * compact,
                         size_t          compactLen,
                         const uint8_t * added_f,
                         size_t          added_f_len,
                         size_t        * pexCount )
{
    size_t          i;
    size_t          n = compactLen / 18;
    const uint8_t * walk = compact;
    tr_pex *        pex = tr_new0( tr_pex, n );

    for( i = 0; i < n; ++i )
    {
        pex[i].addr.type = TR_AF_INET6;
        memcpy( &pex[i].addr.addr.addr6.s6_addr, walk, 16 ); walk += 16;
        memcpy( &pex[i].port, walk, 2 ); walk += 2;
        if( added_f && ( n == added_f_len ) )
            pex[i].flags = added_f[i];
    }

    *pexCount = n;
    return pex;
}

tr_pex *
tr_peerMgrArrayToPex( const void * array,
                      size_t       arrayLen,
                      size_t      * pexCount )
{
    size_t          i;
    size_t          n = arrayLen / ( sizeof( tr_address ) + 2 );
    /*size_t          n = arrayLen / sizeof( tr_peerArrayElement );*/
    const uint8_t * walk = array;
    tr_pex        * pex = tr_new0( tr_pex, n );

    for( i = 0 ; i < n ; i++ ) {
        memcpy( &pex[i].addr, walk, sizeof( tr_address ) );
        memcpy( &pex[i].port, walk + sizeof( tr_address ), 2 );
        pex[i].flags = 0x00;
        walk += sizeof( tr_address ) + 2;
    }

    *pexCount = n;
    return pex;
}

/**
***
**/

void
tr_peerMgrSetBlame( tr_torrent     * tor,
                    tr_piece_index_t pieceIndex,
                    int              success )
{
    if( !success )
    {
        int        peerCount, i;
        Torrent *  t = tor->torrentPeers;
        tr_peer ** peers;

        assert( torrentIsLocked( t ) );

        peers = (tr_peer **) tr_ptrArrayPeek( &t->peers, &peerCount );
        for( i = 0; i < peerCount; ++i )
        {
            tr_peer * peer = peers[i];
            if( tr_bitfieldHas( peer->blame, pieceIndex ) )
            {
                tordbg( t, "peer %s contributed to corrupt piece (%d); now has %d strikes",
                        tr_atomAddrStr( peer->atom ),
                        pieceIndex, (int)peer->strikes + 1 );
                addStrike( t, peer );
            }
        }
    }
}

int
tr_pexCompare( const void * va, const void * vb )
{
    const tr_pex * a = va;
    const tr_pex * b = vb;
    int i;

    assert( tr_isPex( a ) );
    assert( tr_isPex( b ) );

    if(( i = tr_compareAddresses( &a->addr, &b->addr )))
        return i;

    if( a->port != b->port )
        return a->port < b->port ? -1 : 1;

    return 0;
}

#if 0
static int
peerPrefersCrypto( const tr_peer * peer )
{
    if( peer->encryption_preference == ENCRYPTION_PREFERENCE_YES )
        return TRUE;

    if( peer->encryption_preference == ENCRYPTION_PREFERENCE_NO )
        return FALSE;

    return tr_peerIoIsEncrypted( peer->io );
}
#endif

/* better goes first */
static int
compareAtomsByUsefulness( const void * va, const void *vb )
{
    const struct peer_atom * a = * (const struct peer_atom**) va;
    const struct peer_atom * b = * (const struct peer_atom**) vb;

    assert( tr_isAtom( a ) );
    assert( tr_isAtom( b ) );

    if( a->piece_data_time != b->piece_data_time )
        return a->piece_data_time > b->piece_data_time ? -1 : 1;
    if( a->from != b->from )
        return a->from < b->from ? -1 : 1;
    if( a->numFails != b->numFails )
        return a->numFails < b->numFails ? -1 : 1;

    return 0;
}

int
tr_peerMgrGetPeers( tr_torrent   * tor,
                    tr_pex      ** setme_pex,
                    uint8_t        af,
                    uint8_t        list_mode,
                    int            maxCount )
{
    int i;
    int n;
    int count = 0;
    int atomCount = 0;
    const Torrent * t = tor->torrentPeers;
    struct peer_atom ** atoms = NULL;
    tr_pex * pex;
    tr_pex * walk;

    assert( tr_isTorrent( tor ) );
    assert( setme_pex != NULL );
    assert( af==TR_AF_INET || af==TR_AF_INET6 );
    assert( list_mode==TR_PEERS_CONNECTED || list_mode==TR_PEERS_ALL );

    managerLock( t->manager );

    /**
    ***  build a list of atoms
    **/

    if( list_mode == TR_PEERS_CONNECTED ) /* connected peers only */
    {
        int i;
        const tr_peer ** peers = (const tr_peer **) tr_ptrArrayBase( &t->peers );
        atomCount = tr_ptrArraySize( &t->peers );
        atoms = tr_new( struct peer_atom *, atomCount );
        for( i=0; i<atomCount; ++i )
            atoms[i] = peers[i]->atom;
    }
    else /* TR_PEERS_ALL */
    {
        const struct peer_atom ** atomsBase = (const struct peer_atom**) tr_ptrArrayBase( &t->pool );
        atomCount = tr_ptrArraySize( &t->pool );
        atoms = tr_memdup( atomsBase, atomCount * sizeof( struct peer_atom * ) );
    }

    qsort( atoms, atomCount, sizeof( struct peer_atom * ), compareAtomsByUsefulness );

    /**
    ***  add the first N of them into our return list
    **/

    n = MIN( atomCount, maxCount );
    pex = walk = tr_new0( tr_pex, n );

    for( i=0; i<atomCount && count<n; ++i )
    {
        const struct peer_atom * atom = atoms[i];
        if( atom->addr.type == af )
        {
            assert( tr_isAddress( &atom->addr ) );
            walk->addr = atom->addr;
            walk->port = atom->port;
            walk->flags = atom->flags;
            ++count;
            ++walk;
        }
    }

    qsort( pex, count, sizeof( tr_pex ), tr_pexCompare );

    assert( ( walk - pex ) == count );
    *setme_pex = pex;

    /* cleanup */
    tr_free( atoms );
    managerUnlock( t->manager );
    return count;
}

static void
ensureMgrTimersExist( struct tr_peerMgr * m )
{
    tr_session * s = m->session;

    if( m->bandwidthTimer == NULL )
        m->bandwidthTimer = tr_timerNew( s, bandwidthPulse, m, BANDWIDTH_PERIOD_MSEC );

    if( m->rechokeTimer == NULL )
        m->rechokeTimer = tr_timerNew( s, rechokePulse, m, RECHOKE_PERIOD_MSEC );

    if( m->reconnectTimer == NULL )
        m->reconnectTimer = tr_timerNew( s, reconnectPulse, m, RECONNECT_PERIOD_MSEC );

    if( m->refillUpkeepTimer == NULL )
        m->refillUpkeepTimer = tr_timerNew( s, refillUpkeep, m, REFILL_UPKEEP_PERIOD_MSEC );
}

void
tr_peerMgrStartTorrent( tr_torrent * tor )
{
    Torrent * t = tor->torrentPeers;

    assert( t != NULL );
    managerLock( t->manager );
    ensureMgrTimersExist( t->manager );

    t->isRunning = TRUE;

    rechokePulse( t->manager );
    managerUnlock( t->manager );
}

static void
stopTorrent( Torrent * t )
{
    int i, n;

    assert( torrentIsLocked( t ) );

    t->isRunning = FALSE;

    /* disconnect the peers. */
    for( i=0, n=tr_ptrArraySize( &t->peers ); i<n; ++i )
        peerDestructor( t, tr_ptrArrayNth( &t->peers, i ) );
    tr_ptrArrayClear( &t->peers );

    /* disconnect the handshakes.  handshakeAbort calls handshakeDoneCB(),
     * which removes the handshake from t->outgoingHandshakes... */
    while( !tr_ptrArrayEmpty( &t->outgoingHandshakes ) )
        tr_handshakeAbort( tr_ptrArrayNth( &t->outgoingHandshakes, 0 ) );
}

void
tr_peerMgrStopTorrent( tr_torrent * tor )
{
    Torrent * t = tor->torrentPeers;

    managerLock( t->manager );

    stopTorrent( t );

    managerUnlock( t->manager );
}

void
tr_peerMgrAddTorrent( tr_peerMgr * manager,
                      tr_torrent * tor )
{
    managerLock( manager );

    assert( tor );
    assert( tor->torrentPeers == NULL );

    tor->torrentPeers = torrentConstructor( manager, tor );

    managerUnlock( manager );
}

void
tr_peerMgrRemoveTorrent( tr_torrent * tor )
{
    tr_torrentLock( tor );

    stopTorrent( tor->torrentPeers );
    torrentDestructor( tor->torrentPeers );

    tr_torrentUnlock( tor );
}

void
tr_peerMgrTorrentAvailability( const tr_torrent * tor,
                               int8_t           * tab,
                               unsigned int       tabCount )
{
    tr_piece_index_t   i;
    const Torrent *    t;
    float              interval;
    tr_bool            isSeed;
    int                peerCount;
    const tr_peer **   peers;
    tr_torrentLock( tor );

    t = tor->torrentPeers;
    tor = t->tor;
    interval = tor->info.pieceCount / (float)tabCount;
    isSeed = tor && ( tr_cpGetStatus ( &tor->completion ) == TR_SEED );
    peers = (const tr_peer **) tr_ptrArrayBase( &t->peers );
    peerCount = tr_ptrArraySize( &t->peers );

    memset( tab, 0, tabCount );

    for( i = 0; tor && i < tabCount; ++i )
    {
        const int piece = i * interval;

        if( isSeed || tr_cpPieceIsComplete( &tor->completion, piece ) )
            tab[i] = -1;
        else if( peerCount ) {
            int j;
            for( j = 0; j < peerCount; ++j )
                if( tr_bitfieldHas( peers[j]->have, i ) )
                    ++tab[i];
        }
    }

    tr_torrentUnlock( tor );
}

/* Returns the pieces that are available from peers */
tr_bitfield*
tr_peerMgrGetAvailable( const tr_torrent * tor )
{
    int i;
    int peerCount;
    Torrent * t = tor->torrentPeers;
    const tr_peer ** peers;
    tr_bitfield * pieces;
    managerLock( t->manager );

    pieces = tr_bitfieldNew( t->tor->info.pieceCount );
    peerCount = tr_ptrArraySize( &t->peers );
    peers = (const tr_peer**) tr_ptrArrayBase( &t->peers );
    for( i=0; i<peerCount; ++i )
        tr_bitfieldOr( pieces, peers[i]->have );

    managerUnlock( t->manager );
    return pieces;
}

void
tr_peerMgrTorrentStats( tr_torrent       * tor,
                        int              * setmePeersKnown,
                        int              * setmePeersConnected,
                        int              * setmeSeedsConnected,
                        int              * setmeWebseedsSendingToUs,
                        int              * setmePeersSendingToUs,
                        int              * setmePeersGettingFromUs,
                        int              * setmePeersFrom )
{
    int i, size;
    const Torrent * t = tor->torrentPeers;
    const tr_peer ** peers;
    const tr_webseed ** webseeds;

    managerLock( t->manager );

    peers = (const tr_peer **) tr_ptrArrayBase( &t->peers );
    size = tr_ptrArraySize( &t->peers );

    *setmePeersKnown           = tr_ptrArraySize( &t->pool );
    *setmePeersConnected       = 0;
    *setmeSeedsConnected       = 0;
    *setmePeersGettingFromUs   = 0;
    *setmePeersSendingToUs     = 0;
    *setmeWebseedsSendingToUs  = 0;

    for( i=0; i<TR_PEER_FROM__MAX; ++i )
        setmePeersFrom[i] = 0;

    for( i=0; i<size; ++i )
    {
        const tr_peer * peer = peers[i];
        const struct peer_atom * atom = peer->atom;

        if( peer->io == NULL ) /* not connected */
            continue;

        ++*setmePeersConnected;

        ++setmePeersFrom[atom->from];

        if( clientIsDownloadingFrom( peer ) )
            ++*setmePeersSendingToUs;

        if( clientIsUploadingTo( peer ) )
            ++*setmePeersGettingFromUs;

        if( atom->flags & ADDED_F_SEED_FLAG )
            ++*setmeSeedsConnected;
    }

    webseeds = (const tr_webseed**) tr_ptrArrayBase( &t->webseeds );
    size = tr_ptrArraySize( &t->webseeds );
    for( i=0; i<size; ++i )
        if( tr_webseedIsActive( webseeds[i] ) )
            ++*setmeWebseedsSendingToUs;

    managerUnlock( t->manager );
}

float
tr_peerMgrGetWebseedSpeed( const tr_torrent * tor, uint64_t now )
{
    int i;
    float tmp;
    float ret = 0;

    const Torrent * t = tor->torrentPeers;
    const int n = tr_ptrArraySize( &t->webseeds );
    const tr_webseed ** webseeds = (const tr_webseed**) tr_ptrArrayBase( &t->webseeds );

    for( i=0; i<n; ++i )
        if( tr_webseedGetSpeed( webseeds[i], now, &tmp ) )
            ret += tmp;

    return ret;
}


float*
tr_peerMgrWebSpeeds( const tr_torrent * tor )
{
    const Torrent * t = tor->torrentPeers;
    const tr_webseed ** webseeds;
    int i;
    int webseedCount;
    float * ret;
    uint64_t now;

    assert( t->manager );
    managerLock( t->manager );

    webseeds = (const tr_webseed**) tr_ptrArrayBase( &t->webseeds );
    webseedCount = tr_ptrArraySize( &t->webseeds );
    assert( webseedCount == tor->info.webseedCount );
    ret = tr_new0( float, webseedCount );
    now = tr_date( );

    for( i=0; i<webseedCount; ++i )
        if( !tr_webseedGetSpeed( webseeds[i], now, &ret[i] ) )
            ret[i] = -1.0;

    managerUnlock( t->manager );
    return ret;
}

double
tr_peerGetPieceSpeed( const tr_peer * peer, uint64_t now, tr_direction direction )
{
    return peer->io ? tr_peerIoGetPieceSpeed( peer->io, now, direction ) : 0.0;
}


struct tr_peer_stat *
tr_peerMgrPeerStats( const tr_torrent    * tor,
                     int                 * setmeCount )
{
    int i, size;
    const Torrent * t = tor->torrentPeers;
    const tr_peer ** peers;
    tr_peer_stat * ret;
    uint64_t now;

    assert( t->manager );
    managerLock( t->manager );

    size = tr_ptrArraySize( &t->peers );
    peers = (const tr_peer**) tr_ptrArrayBase( &t->peers );
    ret = tr_new0( tr_peer_stat, size );
    now = tr_date( );

    for( i=0; i<size; ++i )
    {
        char *                   pch;
        const tr_peer *          peer = peers[i];
        const struct peer_atom * atom = peer->atom;
        tr_peer_stat *           stat = ret + i;

        tr_ntop( &atom->addr, stat->addr, sizeof( stat->addr ) );
        tr_strlcpy( stat->client, ( peer->client ? peer->client : "" ),
                   sizeof( stat->client ) );
        stat->port               = ntohs( peer->atom->port );
        stat->from               = atom->from;
        stat->progress           = peer->progress;
        stat->isEncrypted        = tr_peerIoIsEncrypted( peer->io ) ? 1 : 0;
        stat->rateToPeer         = tr_peerGetPieceSpeed( peer, now, TR_CLIENT_TO_PEER );
        stat->rateToClient       = tr_peerGetPieceSpeed( peer, now, TR_PEER_TO_CLIENT );
        stat->peerIsChoked       = peer->peerIsChoked;
        stat->peerIsInterested   = peer->peerIsInterested;
        stat->clientIsChoked     = peer->clientIsChoked;
        stat->clientIsInterested = peer->clientIsInterested;
        stat->isIncoming         = tr_peerIoIsIncoming( peer->io );
        stat->isDownloadingFrom  = clientIsDownloadingFrom( peer );
        stat->isUploadingTo      = clientIsUploadingTo( peer );
        stat->isSeed             = ( atom->uploadOnly == UPLOAD_ONLY_YES ) || ( peer->progress >= 1.0 );

        pch = stat->flagStr;
        if( t->optimistic == peer ) *pch++ = 'O';
        if( stat->isDownloadingFrom ) *pch++ = 'D';
        else if( stat->clientIsInterested ) *pch++ = 'd';
        if( stat->isUploadingTo ) *pch++ = 'U';
        else if( stat->peerIsInterested ) *pch++ = 'u';
        if( !stat->clientIsChoked && !stat->clientIsInterested ) *pch++ = 'K';
        if( !stat->peerIsChoked && !stat->peerIsInterested ) *pch++ = '?';
        if( stat->isEncrypted ) *pch++ = 'E';
        if( stat->from == TR_PEER_FROM_DHT ) *pch++ = 'H';
        if( stat->from == TR_PEER_FROM_PEX ) *pch++ = 'X';
        if( stat->isIncoming ) *pch++ = 'I';
        *pch = '\0';
    }

    *setmeCount = size;

    managerUnlock( t->manager );
    return ret;
}

/**
***
**/

struct ChokeData
{
    tr_bool         doUnchoke;
    tr_bool         isInterested;
    tr_bool         isChoked;
    int             rate;
    tr_peer *       peer;
};

static int
compareChoke( const void * va,
              const void * vb )
{
    const struct ChokeData * a = va;
    const struct ChokeData * b = vb;

    if( a->rate != b->rate ) /* prefer higher overall speeds */
        return a->rate > b->rate ? -1 : 1;

    if( a->isChoked != b->isChoked ) /* prefer unchoked */
        return a->isChoked ? 1 : -1;

    return 0;
}

static int
isNew( const tr_peer * peer )
{
    return peer && peer->io && tr_peerIoGetAge( peer->io ) < 45;
}

static int
isSame( const tr_peer * peer )
{
    return peer && peer->client && strstr( peer->client, "Transmission" );
}

/**
***
**/

static void
rechokeTorrent( Torrent * t, const uint64_t now )
{
    int i, size, unchokedInterested;
    const int peerCount = tr_ptrArraySize( &t->peers );
    tr_peer ** peers = (tr_peer**) tr_ptrArrayBase( &t->peers );
    struct ChokeData * choke = tr_new0( struct ChokeData, peerCount );
    const tr_session * session = t->manager->session;
    const int chokeAll = !tr_torrentIsPieceTransferAllowed( t->tor, TR_CLIENT_TO_PEER );

    assert( torrentIsLocked( t ) );

    /* sort the peers by preference and rate */
    for( i = 0, size = 0; i < peerCount; ++i )
    {
        tr_peer * peer = peers[i];
        struct peer_atom * atom = peer->atom;

        if( peer->progress >= 1.0 ) /* choke all seeds */
        {
            tr_peerMsgsSetChoke( peer->msgs, TRUE );
        }
        else if( atom->uploadOnly == UPLOAD_ONLY_YES ) /* choke partial seeds */
        {
            tr_peerMsgsSetChoke( peer->msgs, TRUE );
        }
        else if( chokeAll ) /* choke everyone if we're not uploading */
        {
            tr_peerMsgsSetChoke( peer->msgs, TRUE );
        }
        else
        {
            struct ChokeData * n = &choke[size++];
            n->peer         = peer;
            n->isInterested = peer->peerIsInterested;
            n->isChoked     = peer->peerIsChoked;
            n->rate         = tr_peerGetPieceSpeed( peer, now, TR_CLIENT_TO_PEER ) * 1024;
        }
    }

    qsort( choke, size, sizeof( struct ChokeData ), compareChoke );

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
     */
    unchokedInterested = 0;
    for( i=0; i<size && unchokedInterested<session->uploadSlotsPerTorrent; ++i ) {
        choke[i].doUnchoke = 1;
        if( choke[i].isInterested )
            ++unchokedInterested;
    }

    /* optimistic unchoke */
    if( i < size )
    {
        int n;
        struct ChokeData * c;
        tr_ptrArray randPool = TR_PTR_ARRAY_INIT;

        for( ; i<size; ++i )
        {
            if( choke[i].isInterested )
            {
                const tr_peer * peer = choke[i].peer;
                int x = 1, y;
                if( isNew( peer ) ) x *= 3;
                if( isSame( peer ) ) x *= 3;
                for( y=0; y<x; ++y )
                    tr_ptrArrayAppend( &randPool, &choke[i] );
            }
        }

        if(( n = tr_ptrArraySize( &randPool )))
        {
            c = tr_ptrArrayNth( &randPool, tr_cryptoWeakRandInt( n ));
            c->doUnchoke = 1;
            t->optimistic = c->peer;
        }

        tr_ptrArrayDestruct( &randPool, NULL );
    }

    for( i=0; i<size; ++i )
        tr_peerMsgsSetChoke( choke[i].peer->msgs, !choke[i].doUnchoke );

    /* cleanup */
    tr_free( choke );
}

static int
rechokePulse( void * vmgr )
{
    uint64_t now;
    tr_torrent * tor = NULL;
    tr_peerMgr * mgr = vmgr;
    managerLock( mgr );

    now = tr_date( );
    while(( tor = tr_torrentNext( mgr->session, tor )))
        if( tor->isRunning )
            rechokeTorrent( tor->torrentPeers, now );

    managerUnlock( mgr );
    return TRUE;
}

/***
****
****  Life and Death
****
***/

typedef enum
{
    TR_CAN_KEEP,
    TR_CAN_CLOSE,
    TR_MUST_CLOSE,
}
tr_close_type_t;

static tr_close_type_t
shouldPeerBeClosed( const Torrent    * t,
                    const tr_peer    * peer,
                    int                peerCount,
                    const time_t       now )
{
    const tr_torrent *       tor = t->tor;
    const struct peer_atom * atom = peer->atom;

    /* if it's marked for purging, close it */
    if( peer->doPurge )
    {
        tordbg( t, "purging peer %s because its doPurge flag is set",
                tr_atomAddrStr( atom ) );
        return TR_MUST_CLOSE;
    }

    /* if we're seeding and the peer has everything we have,
     * and enough time has passed for a pex exchange, then disconnect */
    if( tr_torrentIsSeed( tor ) )
    {
        int peerHasEverything;
        if( atom->flags & ADDED_F_SEED_FLAG )
            peerHasEverything = TRUE;
        else if( peer->progress < tr_cpPercentDone( &tor->completion ) )
            peerHasEverything = FALSE;
        else {
            tr_bitfield * tmp = tr_bitfieldDup( tr_cpPieceBitfield( &tor->completion ) );
            tr_bitfieldDifference( tmp, peer->have );
            peerHasEverything = tr_bitfieldCountTrueBits( tmp ) == 0;
            tr_bitfieldFree( tmp );
        }

        if( peerHasEverything && ( !tr_torrentAllowsPex(tor) || (now-atom->time>=30 )))
        {
            tordbg( t, "purging peer %s because we're both seeds",
                    tr_atomAddrStr( atom ) );
            return TR_MUST_CLOSE;
        }
    }

    /* disconnect if it's been too long since piece data has been transferred.
     * this is on a sliding scale based on number of available peers... */
    {
        const int relaxStrictnessIfFewerThanN = (int)( ( getMaxPeerCount( tor ) * 0.9 ) + 0.5 );
        /* if we have >= relaxIfFewerThan, strictness is 100%.
         * if we have zero connections, strictness is 0% */
        const float strictness = peerCount >= relaxStrictnessIfFewerThanN
                               ? 1.0
                               : peerCount / (float)relaxStrictnessIfFewerThanN;
        const int lo = MIN_UPLOAD_IDLE_SECS;
        const int hi = MAX_UPLOAD_IDLE_SECS;
        const int limit = hi - ( ( hi - lo ) * strictness );
        const int idleTime = now - MAX( atom->time, atom->piece_data_time );
/*fprintf( stderr, "strictness is %.3f, limit is %d seconds... time since connect is %d, time since piece is %d ... idleTime is %d, doPurge is %d\n", (double)strictness, limit, (int)(now - atom->time), (int)(now - atom->piece_data_time), idleTime, idleTime > limit );*/
        if( idleTime > limit ) {
            tordbg( t, "purging peer %s because it's been %d secs since we shared anything",
                       tr_atomAddrStr( atom ), idleTime );
            return TR_CAN_CLOSE;
        }
    }

    return TR_CAN_KEEP;
}

static void sortPeersByLivelinessReverse( tr_peer ** peers, void ** clientData, int n, uint64_t now );

static tr_peer **
getPeersToClose( Torrent * t, tr_close_type_t closeType, const time_t now, int * setmeSize )
{
    int i, peerCount, outsize;
    tr_peer ** peers = (tr_peer**) tr_ptrArrayPeek( &t->peers, &peerCount );
    struct tr_peer ** ret = tr_new( tr_peer *, peerCount );

    assert( torrentIsLocked( t ) );

    for( i = outsize = 0; i < peerCount; ++i )
        if( shouldPeerBeClosed( t, peers[i], peerCount, now ) == closeType )
            ret[outsize++] = peers[i];

    sortPeersByLivelinessReverse ( ret, NULL, outsize, tr_date( ) );

    *setmeSize = outsize;
    return ret;
}

static int
compareCandidates( const void * va, const void * vb )
{
    const struct peer_atom * a = *(const struct peer_atom**) va;
    const struct peer_atom * b = *(const struct peer_atom**) vb;

    /* <Charles> Here we would probably want to try reconnecting to
     * peers that had most recently given us data. Lots of users have
     * trouble with resets due to their routers and/or ISPs. This way we
     * can quickly recover from an unwanted reset. So we sort
     * piece_data_time in descending order.
     */

    if( a->piece_data_time != b->piece_data_time )
        return a->piece_data_time < b->piece_data_time ? 1 : -1;

    if( a->numFails != b->numFails )
        return a->numFails < b->numFails ? -1 : 1;

    if( a->time != b->time )
        return a->time < b->time ? -1 : 1;

    /* In order to avoid fragmenting the swarm, peers from trackers and
     * from the DHT should be preferred to peers from PEX. */
    if( a->from != b->from )
        return a->from < b->from ? -1 : 1;

    return 0;
}

static int
getReconnectIntervalSecs( const struct peer_atom * atom, const time_t now )
{
    int sec;

    /* if we were recently connected to this peer and transferring piece
     * data, try to reconnect to them sooner rather that later -- we don't
     * want network troubles to get in the way of a good peer. */
    if( ( now - atom->piece_data_time ) <= ( MINIMUM_RECONNECT_INTERVAL_SECS * 2 ) )
        sec = MINIMUM_RECONNECT_INTERVAL_SECS;

    /* don't allow reconnects more often than our minimum */
    else if( ( now - atom->time ) < MINIMUM_RECONNECT_INTERVAL_SECS )
        sec = MINIMUM_RECONNECT_INTERVAL_SECS;

    /* otherwise, the interval depends on how many times we've tried
     * and failed to connect to the peer */
    else switch( atom->numFails ) {
        case 0: sec = 0; break;
        case 1: sec = 5; break;
        case 2: sec = 2 * 60; break;
        case 3: sec = 15 * 60; break;
        case 4: sec = 30 * 60; break;
        case 5: sec = 60 * 60; break;
        default: sec = 120 * 60; break;
    }

    return sec;
}

static struct peer_atom **
getPeerCandidates( Torrent * t, const time_t now, int * setmeSize )
{
    int                 i, atomCount, retCount;
    struct peer_atom ** atoms;
    struct peer_atom ** ret;
    const int           seed = tr_torrentIsSeed( t->tor );

    assert( torrentIsLocked( t ) );

    atoms = (struct peer_atom**) tr_ptrArrayPeek( &t->pool, &atomCount );
    ret = tr_new( struct peer_atom*, atomCount );
    for( i = retCount = 0; i < atomCount; ++i )
    {
        int                interval;
        struct peer_atom * atom = atoms[i];

        /* peer fed us too much bad data ... we only keep it around
         * now to weed it out in case someone sends it to us via pex */
        if( atom->myflags & MYFLAG_BANNED )
            continue;

        /* peer was unconnectable before, so we're not going to keep trying.
         * this is needs a separate flag from `banned', since if they try
         * to connect to us later, we'll let them in */
        if( atom->myflags & MYFLAG_UNREACHABLE )
            continue;

        /* we don't need two connections to the same peer... */
        if( peerIsInUse( t, &atom->addr ) )
            continue;

        /* no need to connect if we're both seeds... */
        if( seed && ( ( atom->flags & ADDED_F_SEED_FLAG ) ||
                      ( atom->uploadOnly == UPLOAD_ONLY_YES ) ) )
            continue;

        /* don't reconnect too often */
        interval = getReconnectIntervalSecs( atom, now );
        if( ( now - atom->time ) < interval )
        {
            tordbg( t, "RECONNECT peer %d (%s) is in its grace period of %d seconds..",
                    i, tr_atomAddrStr( atom ), interval );
            continue;
        }

        /* Don't connect to peers in our blocklist */
        if( tr_sessionIsAddressBlocked( t->manager->session, &atom->addr ) )
            continue;

        ret[retCount++] = atom;
    }

    qsort( ret, retCount, sizeof( struct peer_atom* ), compareCandidates );
    *setmeSize = retCount;
    return ret;
}

static void
closePeer( Torrent * t, tr_peer * peer )
{
    struct peer_atom * atom;

    assert( t != NULL );
    assert( peer != NULL );

    atom = peer->atom;

    /* if we transferred piece data, then they might be good peers,
       so reset their `numFails' weight to zero.  otherwise we connected
       to them fruitlessly, so mark it as another fail */
    if( atom->piece_data_time )
        atom->numFails = 0;
    else
        ++atom->numFails;

    tordbg( t, "removing bad peer %s", tr_peerIoGetAddrStr( peer->io ) );
    removePeer( t, peer );
}

static void
reconnectTorrent( Torrent * t )
{
    static time_t prevTime = 0;
    static int    newConnectionsThisSecond = 0;
    const time_t  now = time( NULL );

    if( prevTime != now )
    {
        prevTime = now;
        newConnectionsThisSecond = 0;
    }

    if( !t->isRunning )
    {
        removeAllPeers( t );
    }
    else
    {
        int i;
        int canCloseCount;
        int mustCloseCount;
        int candidateCount;
        int maxCandidates;
        struct tr_peer ** canClose = getPeersToClose( t, TR_CAN_CLOSE, now, &canCloseCount );
        struct tr_peer ** mustClose = getPeersToClose( t, TR_MUST_CLOSE, now, &mustCloseCount );
        struct peer_atom ** candidates = getPeerCandidates( t, now, &candidateCount );

        tordbg( t, "reconnect pulse for [%s]: "
                   "%d must-close connections, "
                   "%d can-close connections, "
                   "%d connection candidates, "
                   "%d atoms, "
                   "max per pulse is %d",
                   tr_torrentName( t->tor ),
                   mustCloseCount,
                   canCloseCount,
                   candidateCount,
                   tr_ptrArraySize( &t->pool ),
                   MAX_RECONNECTIONS_PER_PULSE );

        /* disconnect the really bad peers */
        for( i=0; i<mustCloseCount; ++i )
            closePeer( t, mustClose[i] );

        /* decide how many peers can we try to add in this pass */
        maxCandidates = candidateCount;
        maxCandidates = MIN( maxCandidates, MAX_RECONNECTIONS_PER_PULSE );
        maxCandidates = MIN( maxCandidates, getMaxPeerCount( t->tor ) - getPeerCount( t ) );
        maxCandidates = MIN( maxCandidates, MAX_CONNECTIONS_PER_SECOND - newConnectionsThisSecond );

        /* maybe disconnect some lesser peers, if we have candidates to replace them with */
        for( i=0; ( i<canCloseCount ) && ( i<maxCandidates ); ++i )
            closePeer( t, canClose[i] );

        tordbg( t, "candidateCount is %d, MAX_RECONNECTIONS_PER_PULSE is %d,"
                   " getPeerCount(t) is %d, getMaxPeerCount(t) is %d, "
                   "newConnectionsThisSecond is %d, MAX_CONNECTIONS_PER_SECOND is %d",
                   candidateCount,
                   MAX_RECONNECTIONS_PER_PULSE,
                   getPeerCount( t ),
                   getMaxPeerCount( t->tor ),
                   newConnectionsThisSecond, MAX_CONNECTIONS_PER_SECOND );

        /* add some new ones */
        for( i=0; i<maxCandidates; ++i )
        {
            tr_peerMgr        * mgr = t->manager;
            struct peer_atom  * atom = candidates[i];
            tr_peerIo         * io;

            tordbg( t, "Starting an OUTGOING connection with %s",
                   tr_atomAddrStr( atom ) );

            io = tr_peerIoNewOutgoing( mgr->session, mgr->session->bandwidth, &atom->addr, atom->port, t->tor->info.hash );

            if( io == NULL )
            {
                tordbg( t, "peerIo not created; marking peer %s as unreachable",
                        tr_atomAddrStr( atom ) );
                atom->myflags |= MYFLAG_UNREACHABLE;
            }
            else
            {
                tr_handshake * handshake = tr_handshakeNew( io,
                                                            mgr->session->encryptionMode,
                                                            myHandshakeDoneCB,
                                                            mgr );

                assert( tr_peerIoGetTorrentHash( io ) );

                tr_peerIoUnref( io ); /* balanced by the implicit ref in tr_peerIoNewOutgoing() */

                ++newConnectionsThisSecond;

                tr_ptrArrayInsertSorted( &t->outgoingHandshakes, handshake,
                                         handshakeCompare );
            }

            atom->time = now;
        }

        /* cleanup */
        tr_free( candidates );
        tr_free( mustClose );
        tr_free( canClose );
    }
}

struct peer_liveliness
{
    tr_peer * peer;
    void * clientData;
    time_t pieceDataTime;
    time_t time;
    int speed;
    tr_bool doPurge;
};

static int
comparePeerLiveliness( const void * va, const void * vb )
{
    const struct peer_liveliness * a = va;
    const struct peer_liveliness * b = vb;

    if( a->doPurge != b->doPurge )
        return a->doPurge ? 1 : -1;

    if( a->speed != b->speed ) /* faster goes first */
        return a->speed > b->speed ? -1 : 1;

    /* the one to give us data more recently goes first */
    if( a->pieceDataTime != b->pieceDataTime )
        return a->pieceDataTime > b->pieceDataTime ? -1 : 1;

    /* the one we connected to most recently goes first */
    if( a->time != b->time )
        return a->time > b->time ? -1 : 1;

    return 0;
}

static int
comparePeerLivelinessReverse( const void * va, const void * vb )
{
    return -comparePeerLiveliness (va, vb);
}

static void
sortPeersByLivelinessImpl( tr_peer  ** peers,
                           void     ** clientData,
                           int         n,
                           uint64_t    now,
                           int (*compare) ( const void *va, const void *vb ) )
{
    int i;
    struct peer_liveliness *lives, *l;

    /* build a sortable array of peer + extra info */
    lives = l = tr_new0( struct peer_liveliness, n );
    for( i=0; i<n; ++i, ++l )
    {
        tr_peer * p = peers[i];
        l->peer = p;
        l->doPurge = p->doPurge;
        l->pieceDataTime = p->atom->piece_data_time;
        l->time = p->atom->time;
        l->speed = 1024.0 * (   tr_peerGetPieceSpeed( p, now, TR_UP )
                              + tr_peerGetPieceSpeed( p, now, TR_DOWN ) );
        if( clientData )
            l->clientData = clientData[i];
    }

    /* sort 'em */
    assert( n == ( l - lives ) );
    qsort( lives, n, sizeof( struct peer_liveliness ), compare );

    /* build the peer array */
    for( i=0, l=lives; i<n; ++i, ++l ) {
        peers[i] = l->peer;
        if( clientData )
            clientData[i] = l->clientData;
    }
    assert( n == ( l - lives ) );

    /* cleanup */
    tr_free( lives );
}

static void
sortPeersByLiveliness( tr_peer ** peers, void ** clientData, int n, uint64_t now )
{
    sortPeersByLivelinessImpl( peers, clientData, n, now, comparePeerLiveliness );
}

static void
sortPeersByLivelinessReverse( tr_peer ** peers, void ** clientData, int n, uint64_t now )
{
    sortPeersByLivelinessImpl( peers, clientData, n, now, comparePeerLivelinessReverse );
}


static void
enforceTorrentPeerLimit( Torrent * t, uint64_t now )
{
    int n = tr_ptrArraySize( &t->peers );
    const int max = tr_torrentGetPeerLimit( t->tor );
    if( n > max )
    {
        void * base = tr_ptrArrayBase( &t->peers );
        tr_peer ** peers = tr_memdup( base, n*sizeof( tr_peer* ) );
        sortPeersByLiveliness( peers, NULL, n, now );
        while( n > max )
            closePeer( t, peers[--n] );
        tr_free( peers );
    }
}

static void
enforceSessionPeerLimit( tr_session * session, uint64_t now )
{
    int n = 0;
    tr_torrent * tor = NULL;
    const int max = tr_sessionGetPeerLimit( session );

    /* count the total number of peers */
    while(( tor = tr_torrentNext( session, tor )))
        n += tr_ptrArraySize( &tor->torrentPeers->peers );

    /* if there are too many, prune out the worst */
    if( n > max )
    {
        tr_peer ** peers = tr_new( tr_peer*, n );
        Torrent ** torrents = tr_new( Torrent*, n );

        /* populate the peer array */
        n = 0;
        tor = NULL;
        while(( tor = tr_torrentNext( session, tor ))) {
            int i;
            Torrent * t = tor->torrentPeers;
            const int tn = tr_ptrArraySize( &t->peers );
            for( i=0; i<tn; ++i, ++n ) {
                peers[n] = tr_ptrArrayNth( &t->peers, i );
                torrents[n] = t;
            }
        }

        /* sort 'em */
        sortPeersByLiveliness( peers, (void**)torrents, n, now );

        /* cull out the crappiest */
        while( n-- > max )
            closePeer( torrents[n], peers[n] );

        /* cleanup */
        tr_free( torrents );
        tr_free( peers );
    }
}


static int
reconnectPulse( void * vmgr )
{
    tr_torrent * tor;
    tr_peerMgr * mgr = vmgr;
    uint64_t now;
    managerLock( mgr );

    now = tr_date( );

    /* if we're over the per-torrent peer limits, cull some peers */
    tor = NULL;
    while(( tor = tr_torrentNext( mgr->session, tor )))
        if( tor->isRunning )
            enforceTorrentPeerLimit( tor->torrentPeers, now );

    /* if we're over the per-session peer limits, cull some peers */
    enforceSessionPeerLimit( mgr->session, now );

    tor = NULL;
    while(( tor = tr_torrentNext( mgr->session, tor )))
        if( tor->isRunning )
            reconnectTorrent( tor->torrentPeers );

    managerUnlock( mgr );
    return TRUE;
}

/****
*****
*****  BANDWIDTH ALLOCATION
*****
****/

static void
pumpAllPeers( tr_peerMgr * mgr )
{
    tr_torrent * tor = NULL;

    while(( tor = tr_torrentNext( mgr->session, tor )))
    {
        int j;
        Torrent * t = tor->torrentPeers;

        for( j=0; j<tr_ptrArraySize( &t->peers ); ++j )
        {
            tr_peer * peer = tr_ptrArrayNth( &t->peers, j );
            tr_peerMsgsPulse( peer->msgs );
        }
    }
}

static int
bandwidthPulse( void * vmgr )
{
    tr_torrent * tor = NULL;
    tr_peerMgr * mgr = vmgr;
    managerLock( mgr );

    /* FIXME: this next line probably isn't necessary... */
    pumpAllPeers( mgr );

    /* allocate bandwidth to the peers */
    tr_bandwidthAllocate( mgr->session->bandwidth, TR_UP, BANDWIDTH_PERIOD_MSEC );
    tr_bandwidthAllocate( mgr->session->bandwidth, TR_DOWN, BANDWIDTH_PERIOD_MSEC );

    /* possibly stop torrents that have seeded enough */
    while(( tor = tr_torrentNext( mgr->session, tor ))) {
        if( tor->needsSeedRatioCheck ) {
            tor->needsSeedRatioCheck = FALSE;
            tr_torrentCheckSeedRatio( tor );
        }
    }

    /* run the completeness check for any torrents that need it */
    tor = NULL;
    while(( tor = tr_torrentNext( mgr->session, tor ))) {
        if( tor->torrentPeers->needsCompletenessCheck ) {
            tor->torrentPeers->needsCompletenessCheck  = FALSE;
            tr_torrentRecheckCompleteness( tor );
        }
    }

    /* possibly stop torrents that have an error */
    tor = NULL;
    while(( tor = tr_torrentNext( mgr->session, tor )))
        if( tor->isRunning && ( tor->error == TR_STAT_LOCAL_ERROR ))
            tr_torrentStop( tor );

    managerUnlock( mgr );
    return TRUE;
}
