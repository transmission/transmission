
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
#include "session.h"
#include "bandwidth.h"
#include "bencode.h"
#include "blocklist.h"
#include "clients.h"
#include "completion.h"
#include "crypto.h"
#include "fdlimit.h"
#include "handshake.h"
#include "inout.h" /* tr_ioTestPiece */
#include "net.h"
#include "peer-io.h"
#include "peer-mgr.h"
#include "peer-msgs.h"
#include "ptrarray.h"
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
    REFILL_UPKEEP_PERIOD_MSEC = 10000,

    /* how frequently to decide which peers live and die */
    RECONNECT_PERIOD_MSEC = 500,

    /* when many peers are available, keep idle ones this long */
    MIN_UPLOAD_IDLE_SECS = ( 30 ),

    /* when few peers are available, keep idle ones this long */
    MAX_UPLOAD_IDLE_SECS = ( 60 * 5 ),

    /* max # of peers to ask fer per torrent per reconnect pulse */
    MAX_RECONNECTIONS_PER_PULSE = 16,

    /* max number of peers to ask for per second overall.
    * this throttle is to avoid overloading the router */
    MAX_CONNECTIONS_PER_SECOND = 32,

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

struct tr_blockIterator
{
    time_t expirationDate;
    struct tr_torrent_peers * t;
    tr_block_index_t blockIndex, blockCount, *blocks;
    tr_piece_index_t pieceIndex, pieceCount, *pieces;
};

typedef struct tr_torrent_peers
{
    tr_bool                    isRunning;

    uint8_t                    hash[SHA_DIGEST_LENGTH];
    int                      * pendingRequestCount;
    tr_ptrArray                outgoingHandshakes; /* tr_handshake */
    tr_ptrArray                pool; /* struct peer_atom */
    tr_ptrArray                peers; /* tr_peer */
    tr_ptrArray                webseeds; /* tr_webseed */
    tr_timer                 * refillTimer;
    tr_torrent               * tor;
    tr_peer                  * optimistic; /* the optimistic peer, or NULL if none */
    struct tr_blockIterator  * refillQueue; /* used in refillPulse() */

    struct tr_peerMgr        * manager;
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
            tr_deepLog( __FILE__, __LINE__, t->tor->info.name, __VA_ARGS__ ); \
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

    return comparePeerAtomToAddress( va, &b->addr );
}

/**
***
**/

static Torrent*
getExistingTorrent( tr_peerMgr *    manager,
                    const uint8_t * hash )
{
    tr_torrent * tor = tr_torrentFindFromHash( manager->session, hash );

    return tor == NULL ? NULL : tor->torrentPeers;
}

static int
peerCompare( const void * va, const void * vb )
{
    const tr_peer * a = va;
    const tr_peer * b = vb;

    return tr_compareAddresses( &a->addr, &b->addr );
}

static int
peerCompareToAddr( const void * va, const void * vb )
{
    const tr_peer * a = va;

    return tr_compareAddresses( &a->addr, vb );
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
peerConstructor( const tr_address * addr )
{
    tr_peer * p;
    p = tr_new0( tr_peer, 1 );
    p->addr = *addr;
    return p;
}

static tr_peer*
getPeer( Torrent          * torrent,
         const tr_address * addr )
{
    tr_peer * peer;

    assert( torrentIsLocked( torrent ) );

    peer = getExistingPeer( torrent, addr );

    if( peer == NULL )
    {
        peer = peerConstructor( addr );
        tr_ptrArrayInsertSorted( &torrent->peers, peer, peerCompare );
    }

    return peer;
}

static void
peerDestructor( tr_peer * peer )
{
    assert( peer );

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
removePeer( Torrent * t,
            tr_peer * peer )
{
    tr_peer *          removed;
    struct peer_atom * atom;

    assert( torrentIsLocked( t ) );

    atom = getExistingAtom( t, &peer->addr );
    assert( atom );
    atom->time = time( NULL );

    removed = tr_ptrArrayRemoveSorted( &t->peers, peer, peerCompare );
    assert( removed == peer );
    peerDestructor( removed );
}

static void
removeAllPeers( Torrent * t )
{
    while( !tr_ptrArrayEmpty( &t->peers ) )
        removePeer( t, tr_ptrArrayNth( &t->peers, 0 ) );
}

static void blockIteratorFree( struct tr_blockIterator ** inout );

static void
torrentDestructor( void * vt )
{
    Torrent * t = vt;
    uint8_t   hash[SHA_DIGEST_LENGTH];

    assert( t );
    assert( !t->isRunning );
    assert( torrentIsLocked( t ) );
    assert( tr_ptrArrayEmpty( &t->outgoingHandshakes ) );
    assert( tr_ptrArrayEmpty( &t->peers ) );

    memcpy( hash, t->hash, SHA_DIGEST_LENGTH );

    tr_timerFree( &t->refillTimer );

    blockIteratorFree( &t->refillQueue );
    tr_ptrArrayDestruct( &t->webseeds, (PtrArrayForeachFunc)tr_webseedFree );
    tr_ptrArrayDestruct( &t->pool, (PtrArrayForeachFunc)tr_free );
    tr_ptrArrayDestruct( &t->outgoingHandshakes, NULL );
    tr_ptrArrayDestruct( &t->peers, NULL );

    tr_free( t->pendingRequestCount );
    tr_free( t );
}

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
    memcpy( t->hash, tor->info.hash, SHA_DIGEST_LENGTH );

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
    m->bandwidthTimer    = tr_timerNew( session, bandwidthPulse, m, BANDWIDTH_PERIOD_MSEC );
    m->rechokeTimer      = tr_timerNew( session, rechokePulse,   m, RECHOKE_PERIOD_MSEC );
    m->reconnectTimer    = tr_timerNew( session, reconnectPulse, m, RECONNECT_PERIOD_MSEC );
    m->refillUpkeepTimer = tr_timerNew( session, refillUpkeep,   m, REFILL_UPKEEP_PERIOD_MSEC );

    rechokePulse( m );

    return m;
}

void
tr_peerMgrFree( tr_peerMgr * manager )
{
    managerLock( manager );

    tr_timerFree( &manager->refillUpkeepTimer );
    tr_timerFree( &manager->reconnectTimer );
    tr_timerFree( &manager->rechokeTimer );
    tr_timerFree( &manager->bandwidthTimer );

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

/****
*****
*****  REFILL
*****
****/

static void
assertValidPiece( Torrent * t, tr_piece_index_t piece )
{
    assert( t );
    assert( t->tor );
    assert( piece < t->tor->info.pieceCount );
}

static int
getPieceRequests( Torrent * t, tr_piece_index_t piece )
{
    assertValidPiece( t, piece );

    return t->pendingRequestCount ? t->pendingRequestCount[piece] : 0;
}

static void
incrementPieceRequests( Torrent * t, tr_piece_index_t piece )
{
    assertValidPiece( t, piece );

    if( t->pendingRequestCount == NULL )
        t->pendingRequestCount = tr_new0( int, t->tor->info.pieceCount );
    t->pendingRequestCount[piece]++;
}

static void
decrementPieceRequests( Torrent * t, tr_piece_index_t piece )
{
    assertValidPiece( t, piece );

    if( t->pendingRequestCount )
        t->pendingRequestCount[piece]--;
}

struct tr_refill_piece
{
    tr_priority_t    priority;
    uint32_t         piece;
    uint32_t         peerCount;
    int              random;
    int              pendingRequestCount;
    int              missingBlockCount;
};

static int
compareRefillPiece( const void * aIn, const void * bIn )
{
    const struct tr_refill_piece * a = aIn;
    const struct tr_refill_piece * b = bIn;

    /* if one piece has a higher priority, it goes first */
    if( a->priority != b->priority )
        return a->priority > b->priority ? -1 : 1;

    /* have a per-priority endgame */
    if( a->pendingRequestCount != b->pendingRequestCount )
        return a->pendingRequestCount < b->pendingRequestCount ? -1 : 1;

    /* fewer missing pieces goes first */
    if( a->missingBlockCount != b->missingBlockCount )
        return a->missingBlockCount < b->missingBlockCount ? -1 : 1;

    /* otherwise if one has fewer peers, it goes first */
    if( a->peerCount != b->peerCount )
        return a->peerCount < b->peerCount ? -1 : 1;

    /* otherwise go with our random seed */
    if( a->random != b->random )
        return a->random < b->random ? -1 : 1;

    return 0;
}

static tr_piece_index_t *
getPreferredPieces( Torrent * t, tr_piece_index_t * pieceCount )
{
    const tr_torrent  * tor = t->tor;
    const tr_info     * inf = &tor->info;
    tr_piece_index_t    i;
    tr_piece_index_t    poolSize = 0;
    tr_piece_index_t  * pool = tr_new( tr_piece_index_t , inf->pieceCount );
    int                 peerCount;
    const tr_peer    ** peers;

    assert( torrentIsLocked( t ) );

    peers = (const tr_peer**) tr_ptrArrayBase( &t->peers );
    peerCount = tr_ptrArraySize( &t->peers );

    /* make a list of the pieces that we want but don't have */
    for( i = 0; i < inf->pieceCount; ++i )
        if( !tor->info.pieces[i].dnd
                && !tr_cpPieceIsComplete( &tor->completion, i ) )
            pool[poolSize++] = i;

    /* sort the pool by which to request next */
    if( poolSize > 1 )
    {
        tr_piece_index_t j;
        struct tr_refill_piece * p = tr_new( struct tr_refill_piece, poolSize );

        for( j = 0; j < poolSize; ++j )
        {
            int k;
            const tr_piece_index_t piece = pool[j];
            struct tr_refill_piece * setme = p + j;

            setme->piece = piece;
            setme->priority = inf->pieces[piece].priority;
            setme->peerCount = 0;
            setme->random = tr_cryptoWeakRandInt( INT_MAX );
            setme->pendingRequestCount = getPieceRequests( t, piece );
            setme->missingBlockCount
                         = tr_cpMissingBlocksInPiece( &tor->completion, piece );

            for( k = 0; k < peerCount; ++k )
            {
                const tr_peer * peer = peers[k];
                if( peer->peerIsInterested
                        && !peer->clientIsChoked
                        && tr_bitfieldHas( peer->have, piece ) )
                    ++setme->peerCount;
            }
        }

        qsort( p, poolSize, sizeof( struct tr_refill_piece ),
               compareRefillPiece );

        for( j = 0; j < poolSize; ++j )
            pool[j] = p[j].piece;

        tr_free( p );
    }

    *pieceCount = poolSize;
    return pool;
}

static struct tr_blockIterator*
blockIteratorNew( Torrent * t )
{
    struct tr_blockIterator * i = tr_new0( struct tr_blockIterator, 1 );
    i->expirationDate = time( NULL ) + PIECE_LIST_SHELF_LIFE_SECS;
    i->t = t;
    i->pieces = getPreferredPieces( t, &i->pieceCount );
    i->blocks = tr_new0( tr_block_index_t, t->tor->blockCountInPiece );
    tordbg( t, "creating new refill queue.. it contains %"PRIu32" pieces", i->pieceCount );
    return i;
}

static tr_bool
blockIteratorNext( struct tr_blockIterator * i, tr_block_index_t * setme )
{
    tr_bool found;
    Torrent * t = i->t;
    tr_torrent * tor = t->tor;

    while( ( i->blockIndex == i->blockCount )
        && ( i->pieceIndex < i->pieceCount ) )
    {
        const tr_piece_index_t index = i->pieces[i->pieceIndex++];
        const tr_block_index_t b = tr_torPieceFirstBlock( tor, index );
        const tr_block_index_t e = b + tr_torPieceCountBlocks( tor, index );
        tr_block_index_t block;

        assert( index < tor->info.pieceCount );

        i->blockCount = 0;
        i->blockIndex = 0;
        for( block=b; block!=e; ++block )
            if( !tr_cpBlockIsComplete( &tor->completion, block ) )
                i->blocks[i->blockCount++] = block;
    }

    assert( i->blockCount <= tor->blockCountInPiece );

    if(( found = ( i->blockIndex < i->blockCount )))
        *setme = i->blocks[i->blockIndex++];

    return found;
}

static void
blockIteratorSkipCurrentPiece( struct tr_blockIterator * i )
{
    i->blockIndex = i->blockCount;
}

static void
blockIteratorFree( struct tr_blockIterator ** inout )
{
    struct tr_blockIterator * it = *inout;

    if( it != NULL )
    {
        tr_free( it->blocks );
        tr_free( it->pieces );
        tr_free( it );
    }

    *inout = NULL;
}

static tr_peer**
getPeersUploadingToClient( Torrent * t,
                           int *     setmeCount )
{
    int j;
    int peerCount = 0;
    int retCount = 0;
    tr_peer ** peers = (tr_peer **) tr_ptrArrayPeek( &t->peers, &peerCount );
    tr_peer ** ret = tr_new( tr_peer *, peerCount );

    j = 0; /* this is a temporary test to make sure we walk through all the peers */
    if( peerCount )
    {
        /* Get a list of peers we're downloading from.
           Pick a different starting point each time so all peers
           get a chance at being the first in line */
        const int fencepost = tr_cryptoWeakRandInt( peerCount );
        int i = fencepost;
        do {
            if( clientIsDownloadingFrom( peers[i] ) )
                ret[retCount++] = peers[i];
            i = ( i + 1 ) % peerCount;
            ++j;
        } while( i != fencepost );
    }
    assert( j == peerCount );
    *setmeCount = retCount;
    return ret;
}

static uint32_t
getBlockOffsetInPiece( const tr_torrent * tor, uint64_t b )
{
    const uint64_t piecePos = tor->info.pieceSize * tr_torBlockPiece( tor, b );
    const uint64_t blockPos = tor->blockSize * b;
    assert( blockPos >= piecePos );
    return (uint32_t)( blockPos - piecePos );
}

static int
refillUpkeep( void * vmgr )
{
    tr_torrent * tor = NULL;
    tr_peerMgr * mgr = vmgr;
    time_t now;
    managerLock( mgr );

    now = time( NULL );
    while(( tor = tr_torrentNext( mgr->session, tor ))) {
        Torrent * t = tor->torrentPeers;
        if( t && t->refillQueue && ( t->refillQueue->expirationDate <= now ) ) {
            tordbg( t, "refill queue is past its shelf date; discarding." );
            blockIteratorFree( &t->refillQueue );
        }
    }

    managerUnlock( mgr );
    return TRUE;
}

static int
refillPulse( void * vtorrent )
{
    tr_block_index_t block;
    int peerCount;
    int webseedCount;
    tr_peer ** peers;
    tr_webseed ** webseeds;
    Torrent * t = vtorrent;
    tr_torrent * tor = t->tor;
    tr_bool hasNext = TRUE;

    if( !t->isRunning )
        return TRUE;
    if( tr_torrentIsSeed( t->tor ) )
        return TRUE;

    torrentLock( t );
    tordbg( t, "Refilling Request Buffers..." );

    if( t->refillQueue == NULL )
        t->refillQueue = blockIteratorNew( t );

    peers = getPeersUploadingToClient( t, &peerCount );
    webseedCount = tr_ptrArraySize( &t->webseeds );
    webseeds = tr_memdup( tr_ptrArrayBase( &t->webseeds ),
                          webseedCount * sizeof( tr_webseed* ) );

    while( ( webseedCount || peerCount )
        && (( hasNext = blockIteratorNext( t->refillQueue, &block ))) )
    {
        int j;
        tr_bool handled = FALSE;

        const tr_piece_index_t index = tr_torBlockPiece( tor, block );
        const uint32_t offset = getBlockOffsetInPiece( tor, block );
        const uint32_t length = tr_torBlockCountBytes( tor, block );

        assert( block < tor->blockCount );

        /* find a peer who can ask for this block */
        for( j=0; !handled && j<peerCount; )
        {
            const tr_addreq_t val = tr_peerMsgsAddRequest( peers[j]->msgs, index, offset, length );
            switch( val )
            {
                case TR_ADDREQ_FULL:
                case TR_ADDREQ_CLIENT_CHOKED:
                    peers[j] = peers[--peerCount];
                    break;

                case TR_ADDREQ_MISSING:
                case TR_ADDREQ_DUPLICATE:
                    ++j;
                    break;

                case TR_ADDREQ_OK:
                    incrementPieceRequests( t, index );
                    handled = TRUE;
                    break;

                default:
                    assert( 0 && "unhandled value" );
                    break;
            }
        }

        /* maybe one of the webseeds can do it */
        for( j=0; !handled && j<webseedCount; )
        {
            const tr_addreq_t val = tr_webseedAddRequest( webseeds[j], index, offset, length );
            switch( val )
            {
                case TR_ADDREQ_FULL:
                    webseeds[j] = webseeds[--webseedCount];
                    break;

                case TR_ADDREQ_OK:
                    incrementPieceRequests( t, index );
                    handled = TRUE;
                    break;

                default:
                    assert( 0 && "unhandled value" );
                    break;
            }
        }

        if( !handled )
            blockIteratorSkipCurrentPiece( t->refillQueue );
    }

    /* cleanup */
    tr_free( webseeds );
    tr_free( peers );

    if( !hasNext ) {
        tordbg( t, "refill queue has no more blocks to request... freeing (webseed count: %d, peer count: %d)", webseedCount, peerCount );
        blockIteratorFree( &t->refillQueue );
    }

    t->refillTimer = NULL;
    torrentUnlock( t );
    return FALSE;
}

static void
broadcastGotBlock( Torrent * t, uint32_t index, uint32_t offset, uint32_t length )
{
    size_t i;
    size_t peerCount;
    tr_peer ** peers;

    assert( torrentIsLocked( t ) );

    tordbg( t, "got a block; cancelling any duplicate requests from peers %"PRIu32":%"PRIu32"->%"PRIu32, index, offset, length );

    peerCount = tr_ptrArraySize( &t->peers );
    peers = (tr_peer**) tr_ptrArrayBase( &t->peers );
    for( i=0; i<peerCount; ++i )
        if( peers[i]->msgs )
            tr_peerMsgsCancel( peers[i]->msgs, index, offset, length );
}

static void
addStrike( Torrent * t,
           tr_peer * peer )
{
    tordbg( t, "increasing peer %s strike count to %d",
            tr_peerIoAddrStr( &peer->addr,
                              peer->port ), peer->strikes + 1 );

    if( ++peer->strikes >= MAX_BAD_PIECES_PER_PEER )
    {
        struct peer_atom * atom = getExistingAtom( t, &peer->addr );
        atom->myflags |= MYFLAG_BANNED;
        peer->doPurge = 1;
        tordbg( t, "banning peer %s", tr_peerIoAddrStr( &atom->addr, atom->port ) );
    }
}

static void
gotBadPiece( Torrent *        t,
             tr_piece_index_t pieceIndex )
{
    tr_torrent *   tor = t->tor;
    const uint32_t byteCount = tr_torPieceCountBytes( tor, pieceIndex );

    tor->corruptCur += byteCount;
    tor->downloadedCur -= MIN( tor->downloadedCur, byteCount );
}

static void
refillSoon( Torrent * t )
{
    if( t->refillTimer == NULL )
        t->refillTimer = tr_timerNew( t->manager->session,
                                      refillPulse, t,
                                      REFILL_PERIOD_MSEC );
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
                struct peer_atom * a = getExistingAtom( t, &peer->addr );
                a->uploadOnly = e->uploadOnly ? UPLOAD_ONLY_YES : UPLOAD_ONLY_NO;
            }
            break;

        case TR_PEER_NEED_REQ:
            refillSoon( t );
            break;

        case TR_PEER_CANCEL:
            decrementPieceRequests( t, e->pieceIndex );
            break;

        case TR_PEER_PEER_GOT_DATA:
        {
            const time_t now = time( NULL );
            tr_torrent * tor = t->tor;

            tor->activityDate = now;

            if( e->wasPieceData )
                tor->uploadedCur += e->length;

            /* update the stats */
            if( e->wasPieceData )
                tr_statsAddUploaded( tor->session, e->length );

            /* update our atom */
            if( peer ) {
                struct peer_atom * a = getExistingAtom( t, &peer->addr );
                if( e->wasPieceData )
                    a->piece_data_time = now;
            }

            tr_torrentCheckSeedRatio( tor );

            break;
        }

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
            tor->activityDate = now;

            /* only add this to downloadedCur if we got it from a peer --
             * webseeds shouldn't count against our ratio.  As one tracker
             * admin put it, "Those pieces are downloaded directly from the
             * content distributor, not the peers, it is the tracker's job
             * to manage the swarms, not the web server and does not fit
             * into the jurisdiction of the tracker." */
            if( peer && e->wasPieceData )
                tor->downloadedCur += e->length;

            /* update the stats */ 
            if( e->wasPieceData )
                tr_statsAddDownloaded( tor->session, e->length );

            /* update our atom */
            if( peer ) {
                struct peer_atom * a = getExistingAtom( t, &peer->addr );
                if( e->wasPieceData )
                    a->piece_data_time = now;
            }

            break;
        }

        case TR_PEER_PEER_PROGRESS:
        {
            if( peer )
            {
                struct peer_atom * atom = getExistingAtom( t, &peer->addr );
                const int peerIsSeed = e->progress >= 1.0;
                if( peerIsSeed ) {
                    tordbg( t, "marking peer %s as a seed", tr_peerIoAddrStr( &atom->addr, atom->port ) );
                    atom->flags |= ADDED_F_SEED_FLAG;
                } else {
                    tordbg( t, "marking peer %s as a non-seed", tr_peerIoAddrStr( &atom->addr, atom->port ) );
                    atom->flags &= ~ADDED_F_SEED_FLAG;
                }
            }
            break;
        }

        case TR_PEER_CLIENT_GOT_BLOCK:
        {
            tr_torrent * tor = t->tor;

            tr_block_index_t block = _tr_block( tor, e->pieceIndex, e->offset );

            tr_cpBlockAdd( &tor->completion, block );
            decrementPieceRequests( t, e->pieceIndex );

            broadcastGotBlock( t, e->pieceIndex, e->offset, e->length );

            if( tr_cpPieceIsComplete( &tor->completion, e->pieceIndex ) )
            {
                const tr_piece_index_t p = e->pieceIndex;
                const tr_bool ok = tr_ioTestPiece( tor, p, NULL, 0 );

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

                    for( fileIndex=0; fileIndex<tor->info.fileCount; ++fileIndex )
                    {
                        const tr_file * file = &tor->info.files[fileIndex];
                        if( ( file->firstPiece <= p ) && ( p <= file->lastPiece ) && tr_cpFileIsComplete( &tor->completion, fileIndex ) )
                        {
                            char * path = tr_buildPath( tor->downloadDir, file->name, NULL );
                            tordbg( t, "closing recently-completed file \"%s\"", path );
                            tr_fdFileClose( path );
                            tr_free( path );
                        }
                    }
                }

                tr_torrentRecheckCompleteness( tor );
            }
            break;
        }

        case TR_PEER_ERROR:
            if( e->err == EINVAL )
            {
                addStrike( t, peer );
                peer->doPurge = 1;
                tordbg( t, "setting %s doPurge flag because we got an EINVAL error", tr_peerIoAddrStr( &peer->addr, peer->port ) );
            }
            else if( ( e->err == ERANGE )
                  || ( e->err == EMSGSIZE )
                  || ( e->err == ENOTCONN ) )
            {
                /* some protocol error from the peer */
                peer->doPurge = 1;
                tordbg( t, "setting %s doPurge flag because we got an ERANGE, EMSGSIZE, or ENOTCONN error", tr_peerIoAddrStr( &peer->addr, peer->port ) );
            }
            else /* a local error, such as an IO error */
            {
                t->tor->error = e->err;
                tr_strlcpy( t->tor->errorString,
                            tr_strerror( t->tor->error ),
                            sizeof( t->tor->errorString ) );
                tr_torrentStop( t->tor );
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
    if( getExistingAtom( t, addr ) == NULL )
    {
        struct peer_atom * a;
        a = tr_new0( struct peer_atom, 1 );
        a->addr = *addr;
        a->port = port;
        a->flags = flags;
        a->from = from;
        tordbg( t, "got a new atom: %s", tr_peerIoAddrStr( &a->addr, a->port ) );
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
    return tr_ptrArraySize( &t->peers );// + tr_ptrArraySize( &t->outgoingHandshakes );
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
                    tr_peerIoAddrStr( &atom->addr, atom->port ) );
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
                peer = getPeer( t, addr );
                tr_free( peer->client );

                if( !peer_id )
                    peer->client = NULL;
                else {
                    char client[128];
                    tr_clientForId( client, sizeof( client ), peer_id );
                    peer->client = tr_strdup( client );
                }

                peer->port = port;
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
    managerLock( manager );

    if( tr_sessionIsAddressBlocked( manager->session, addr ) )
    {
        tr_dbg( "Banned IP address \"%s\" tried to connect to us", tr_ntop_non_ts( addr ) );
        tr_netClose( socket );
    }
    else if( getExistingHandshake( &manager->incomingHandshakes, addr ) )
    {
        tr_netClose( socket );
    }
    else /* we don't have a connetion to them yet... */
    {
        tr_peerIo *    io;
        tr_handshake * handshake;

        io = tr_peerIoNewIncoming( manager->session, manager->session->bandwidth, addr, port, socket );

        handshake = tr_handshakeNew( io,
                                     manager->session->encryptionMode,
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
                        tr_peerIoAddrStr( &peer->addr, peer->port ),
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

static int
peerPrefersCrypto( const tr_peer * peer )
{
    if( peer->encryption_preference == ENCRYPTION_PREFERENCE_YES )
        return TRUE;

    if( peer->encryption_preference == ENCRYPTION_PREFERENCE_NO )
        return FALSE;

    return tr_peerIoIsEncrypted( peer->io );
}

int
tr_peerMgrGetPeers( tr_torrent      * tor,
                    tr_pex         ** setme_pex,
                    uint8_t           af)
{
    int peersReturning = 0;
    const Torrent * t = tor->torrentPeers;

    managerLock( t->manager );

    {
        int i;
        const tr_peer ** peers = (const tr_peer**) tr_ptrArrayBase( &t->peers );
        const int peerCount = tr_ptrArraySize( &t->peers );
        /* for now, this will waste memory on torrents that have both
         * ipv6 and ipv4 peers */
        tr_pex * pex = tr_new( tr_pex, peerCount );
        tr_pex * walk = pex;

        for( i=0; i<peerCount; ++i )
        {
            const tr_peer * peer = peers[i];
            if( peer->addr.type == af )
            {
                const struct peer_atom * atom = getExistingAtom( t, &peer->addr );

                assert( tr_isAddress( &peer->addr ) );
                walk->addr = peer->addr;
                walk->port = peer->port;
                walk->flags = 0;
                if( peerPrefersCrypto( peer ) )
                    walk->flags |= ADDED_F_ENCRYPTION_FLAG;
                if( ( atom->uploadOnly == UPLOAD_ONLY_YES ) || ( peer->progress >= 1.0 ) )
                    walk->flags |= ADDED_F_SEED_FLAG;
                ++peersReturning;
                ++walk;
            }
        }

        assert( ( walk - pex ) == peersReturning );
        qsort( pex, peersReturning, sizeof( tr_pex ), tr_pexCompare );
        *setme_pex = pex;
    }

    managerUnlock( t->manager );
    return peersReturning;
}

void
tr_peerMgrStartTorrent( tr_torrent * tor )
{
    Torrent * t = tor->torrentPeers;

    managerLock( t->manager );

    assert( t );

    if( !t->isRunning )
    {
        t->isRunning = TRUE;

        if( !tr_ptrArrayEmpty( &t->webseeds ) )
            refillSoon( t );
    }

    managerUnlock( t->manager );
}

static void
stopTorrent( Torrent * t )
{
    assert( torrentIsLocked( t ) );

    t->isRunning = FALSE;

    /* disconnect the peers. */
    tr_ptrArrayForeach( &t->peers, (PtrArrayForeachFunc)peerDestructor );
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
        const struct peer_atom * atom = getExistingAtom( t, &peer->addr );

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

    for( i = 0; i < size; ++i )
    {
        char *                   pch;
        const tr_peer *          peer = peers[i];
        const struct peer_atom * atom = getExistingAtom( t, &peer->addr );
        tr_peer_stat *           stat = ret + i;

        tr_ntop( &peer->addr, stat->addr, sizeof( stat->addr ) );
        tr_strlcpy( stat->client, ( peer->client ? peer->client : "" ),
                   sizeof( stat->client ) );
        stat->port               = ntohs( peer->port );
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
rechokeTorrent( Torrent * t )
{
    int i, size, unchokedInterested;
    const int peerCount = tr_ptrArraySize( &t->peers );
    tr_peer ** peers = (tr_peer**) tr_ptrArrayBase( &t->peers );
    struct ChokeData * choke = tr_new0( struct ChokeData, peerCount );
    const tr_session * session = t->manager->session;
    const int chokeAll = !tr_torrentIsPieceTransferAllowed( t->tor, TR_CLIENT_TO_PEER );
    const uint64_t now = tr_date( );

    assert( torrentIsLocked( t ) );

    /* sort the peers by preference and rate */
    for( i = 0, size = 0; i < peerCount; ++i )
    {
        tr_peer * peer = peers[i];
        struct peer_atom * atom = getExistingAtom( t, &peer->addr );

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
    tr_torrent * tor = NULL;
    tr_peerMgr * mgr = vmgr;
    managerLock( mgr );

    while(( tor = tr_torrentNext( mgr->session, tor )))
        if( tor->isRunning )
            rechokeTorrent( tor->torrentPeers );

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
                    int                peerCount )
{
    const tr_torrent *       tor = t->tor;
    const time_t             now = time( NULL );
    const struct peer_atom * atom = getExistingAtom( t, &peer->addr );

    /* if it's marked for purging, close it */
    if( peer->doPurge )
    {
        tordbg( t, "purging peer %s because its doPurge flag is set",
                tr_peerIoAddrStr( &atom->addr, atom->port ) );
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
                    tr_peerIoAddrStr( &atom->addr, atom->port ) );
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
                       tr_peerIoAddrStr( &atom->addr, atom->port ), idleTime );
            return TR_CAN_CLOSE;
        }
    }

    return TR_CAN_KEEP;
}

static tr_peer **
getPeersToClose( Torrent * t, tr_close_type_t closeType, int * setmeSize )
{
    int i, peerCount, outsize;
    tr_peer ** peers = (tr_peer**) tr_ptrArrayPeek( &t->peers, &peerCount );
    struct tr_peer ** ret = tr_new( tr_peer *, peerCount );

    assert( torrentIsLocked( t ) );

    for( i = outsize = 0; i < peerCount; ++i )
        if( shouldPeerBeClosed( t, peers[i], peerCount ) == closeType )
            ret[outsize++] = peers[i];

    *setmeSize = outsize;
    return ret;
}

static int
compareCandidates( const void * va,
                   const void * vb )
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

    /* all other things being equal, prefer peers whose
     * information comes from a more reliable source */
    if( a->from != b->from )
        return a->from < b->from ? -1 : 1;

    return 0;
}

static int
getReconnectIntervalSecs( const struct peer_atom * atom )
{
    int          sec;
    const time_t now = time( NULL );

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
getPeerCandidates( Torrent * t, int * setmeSize )
{
    int                 i, atomCount, retCount;
    struct peer_atom ** atoms;
    struct peer_atom ** ret;
    const time_t        now = time( NULL );
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
        interval = getReconnectIntervalSecs( atom );
        if( ( now - atom->time ) < interval )
        {
            tordbg( t, "RECONNECT peer %d (%s) is in its grace period of %d seconds..",
                    i, tr_peerIoAddrStr( &atom->addr, atom->port ), interval );
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

    /* if we transferred piece data, then they might be good peers,
       so reset their `numFails' weight to zero.  otherwise we connected
       to them fruitlessly, so mark it as another fail */
    atom = getExistingAtom( t, &peer->addr );
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
    time_t        now;

    now = time( NULL );
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
        struct tr_peer ** canClose = getPeersToClose( t, TR_CAN_CLOSE, &canCloseCount );
        struct tr_peer ** mustClose = getPeersToClose( t, TR_MUST_CLOSE, &mustCloseCount );
        struct peer_atom ** candidates = getPeerCandidates( t, &candidateCount );

        tordbg( t, "reconnect pulse for [%s]: "
                   "%d must-close connections, "
                   "%d can-close connections, "
                   "%d connection candidates, "
                   "%d atoms, "
                   "max per pulse is %d",
                   t->tor->info.name,
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
                   tr_peerIoAddrStr( &atom->addr, atom->port ) );

            io = tr_peerIoNewOutgoing( mgr->session, mgr->session->bandwidth, &atom->addr, atom->port, t->hash );

            if( io == NULL )
            {
                tordbg( t, "peerIo not created; marking peer %s as unreachable",
                        tr_peerIoAddrStr( &atom->addr, atom->port ) );
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

            atom->time = time( NULL );
        }

        /* cleanup */
        tr_free( candidates );
        tr_free( mustClose );
        tr_free( canClose );
    }
}

static int
reconnectPulse( void * vmgr )
{
    tr_torrent * tor = NULL;
    tr_peerMgr * mgr = vmgr;
    managerLock( mgr );

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
    tr_peerMgr * mgr = vmgr;
    managerLock( mgr );

    /* FIXME: this next line probably isn't necessary... */
    pumpAllPeers( mgr );

    /* allocate bandwidth to the peers */
    tr_bandwidthAllocate( mgr->session->bandwidth, TR_UP, BANDWIDTH_PERIOD_MSEC );
    tr_bandwidthAllocate( mgr->session->bandwidth, TR_DOWN, BANDWIDTH_PERIOD_MSEC );

    managerUnlock( mgr );
    return TRUE;
}
