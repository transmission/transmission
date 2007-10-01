/*
 * This file Copyright (C) 2007 Charles Kerr <charles@rebelbase.com>
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
#include <string.h> /* memcpy, memcmp */
#include <stdlib.h> /* qsort */
#include <stdio.h> /* printf */
#include <limits.h> /* INT_MAX */

#include "transmission.h"
#include "clients.h"
#include "completion.h"
#include "crypto.h"
#include "handshake.h"
#include "net.h"
#include "peer-io.h"
#include "peer-mgr.h"
#include "peer-mgr-private.h"
#include "peer-msgs.h"
#include "platform.h"
#include "ptrarray.h"
#include "ratecontrol.h"
#include "trevent.h"
#include "utils.h"

#include "pthread.h"

enum
{
    /* how frequently to change which peers are choked */
    RECHOKE_PERIOD_MSEC = (15 * 1000),

    /* how frequently to decide which peers live and die */
    RECONNECT_PERIOD_MSEC = (15 * 1000),

    /* how frequently to refill peers' request lists */
    REFILL_PERIOD_MSEC = 1000,

    /* how many peers to unchoke per-torrent. */
    /* FIXME: make this user-configurable? */
    NUM_UNCHOKED_PEERS_PER_TORRENT = 8,

    /* don't change a peer's choke status more often than this */
    MIN_CHOKE_PERIOD_SEC = 10,

    /* how soon is `soon' in the rechokeSoon, reconnecSoon funcs */
    SOON_MSEC = 1000,

    /* following the BT spec, we consider ourselves `snubbed' if 
     * we're we don't get piece data from a peer in this long */
    SNUBBED_SEC = 60,

    /* this is arbitrary and, hopefully, temporary until we come up
     * with a better idea for managing the connection limits */
    MAX_CONNECTED_PEERS_PER_TORRENT = 60,
};

/**
***
**/

/* We keep one of these for every peer we know about, whether
 * it's connected or not, so the struct must be small.
 * When our current connections underperform, we dip back
 * int this list for new ones. */
struct peer_atom
{   
    uint8_t from;
    uint8_t flags; /* these match the added_f flags */
    uint16_t port;
    struct in_addr addr; 
    time_t time;
};

typedef struct
{
    uint8_t hash[SHA_DIGEST_LENGTH];
    tr_ptrArray * pool; /* struct peer_atom */
    tr_ptrArray * peers; /* tr_peer */
    tr_timer * reconnectTimer;
    tr_timer * reconnectSoonTimer;
    tr_timer * rechokeTimer;
    tr_timer * rechokeSoonTimer;
    tr_timer * refillTimer;
    tr_torrent * tor;
    tr_bitfield * requested;

    unsigned int isRunning : 1;

    struct tr_peerMgr * manager;
}
Torrent;

struct tr_peerMgr
{
    tr_handle * handle;
    tr_ptrArray * torrents; /* Torrent */
    int connectionCount;
    tr_ptrArray * handshakes; /* in-process */
    tr_lock * lock;
    pthread_t lockThread;
};

/**
***
**/

static void
managerLock( struct tr_peerMgr * manager )
{
    assert( manager->lockThread != pthread_self() );
    tr_lockLock( manager->lock );
    manager->lockThread = pthread_self();
}
static void
managerUnlock( struct tr_peerMgr * manager )
{
    assert( manager->lockThread == pthread_self() );
    manager->lockThread = 0;
    tr_lockUnlock( manager->lock );
}
static void
torrentLock( Torrent * torrent )
{
    managerLock( torrent->manager );
}
static void
torrentUnlock( Torrent * torrent )
{
    managerUnlock( torrent->manager );
}
static int
torrentIsLocked( const Torrent * t )
{
    return ( t != NULL )
        && ( t->manager != NULL )
        && ( t->manager->lockThread != 0 )
        && ( t->manager->lockThread == pthread_self( ) );
}

/**
***
**/

static int
compareAddresses( const struct in_addr * a, const struct in_addr * b )
{
    return tr_compareUint32( a->s_addr, b->s_addr );
}

static int
handshakeCompareToAddr( const void * va, const void * vb )
{
    const tr_handshake * a = va;
    return compareAddresses( tr_handshakeGetAddr( a, NULL ), vb );
}

static int
handshakeCompare( const void * a, const void * b )
{
    return handshakeCompareToAddr( a, tr_handshakeGetAddr( b, NULL ) );
}

static tr_handshake*
getExistingHandshake( tr_peerMgr * mgr, const struct in_addr * in_addr )
{
    return tr_ptrArrayFindSorted( mgr->handshakes,
                                  in_addr,
                                  handshakeCompareToAddr );
}

static int
comparePeerAtomToAddress( const void * va, const void * vb )
{
    const struct peer_atom * a = va;
    return compareAddresses( &a->addr, vb );
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

static int
torrentCompare( const void * va, const void * vb )
{
    const Torrent * a = va;
    const Torrent * b = vb;
    return memcmp( a->hash, b->hash, SHA_DIGEST_LENGTH );
}

static int
torrentCompareToHash( const void * va, const void * vb )
{
    const Torrent * a = (const Torrent*) va;
    const uint8_t * b_hash = (const uint8_t*) vb;
    return memcmp( a->hash, b_hash, SHA_DIGEST_LENGTH );
}

static Torrent*
getExistingTorrent( tr_peerMgr * manager, const uint8_t * hash )
{
    return (Torrent*) tr_ptrArrayFindSorted( manager->torrents,
                                             hash,
                                             torrentCompareToHash );
}

static int
peerCompare( const void * va, const void * vb )
{
    const tr_peer * a = (const tr_peer *) va;
    const tr_peer * b = (const tr_peer *) vb;
    return compareAddresses( &a->in_addr, &b->in_addr );
}

static int
peerCompareToAddr( const void * va, const void * vb )
{
    const tr_peer * a = (const tr_peer *) va;
    return compareAddresses( &a->in_addr, vb );
}

static tr_peer*
getExistingPeer( Torrent * torrent, const struct in_addr * in_addr )
{
    assert( torrentIsLocked( torrent ) );
    assert( in_addr != NULL );

    return (tr_peer*) tr_ptrArrayFindSorted( torrent->peers,
                                             in_addr,
                                             peerCompareToAddr );
}

static struct peer_atom*
getExistingAtom( const Torrent * t, const struct in_addr * addr )
{
    assert( torrentIsLocked( t ) );
    return tr_ptrArrayFindSorted( t->pool, addr, comparePeerAtomToAddress );
}

static int
peerIsKnown( const Torrent * t, const struct in_addr * addr )
{
    return getExistingAtom( t, addr ) != NULL;
}

static int
peerIsInUse( const Torrent * t, const struct in_addr * addr )
{
    assert( torrentIsLocked ( t ) );

    return ( getExistingPeer( (Torrent*)t, addr ) != NULL )
        || ( getExistingHandshake( ((Torrent*)t)->manager, addr ) != NULL );
}

static tr_peer*
getPeer( Torrent * torrent, const struct in_addr * in_addr )
{
    tr_peer * peer;

    assert( torrentIsLocked( torrent ) );

    peer = getExistingPeer( torrent, in_addr );

    if( peer == NULL )
    {
        peer = tr_new0( tr_peer, 1 );
        memcpy( &peer->in_addr, in_addr, sizeof(struct in_addr) );
        tr_ptrArrayInsertSorted( torrent->peers, peer, peerCompare );
    }

    return peer;
}

static void
disconnectPeer( tr_peer * peer )
{
    assert( peer != NULL );

    tr_peerIoFree( peer->io );
    peer->io = NULL;

    if( peer->msgs != NULL )
    {
        tr_peerMsgsUnsubscribe( peer->msgs, peer->msgsTag );
        tr_peerMsgsFree( peer->msgs );
        peer->msgs = NULL;
    }

    tr_bitfieldFree( peer->have );
    peer->have = NULL;

    tr_bitfieldFree( peer->blame );
    peer->blame = NULL;

    tr_bitfieldFree( peer->banned );
    peer->banned = NULL;
}

static void
freePeer( tr_peer * peer )
{
    disconnectPeer( peer );
    tr_free( peer->client );
    tr_free( peer );
}

static void
removePeer( Torrent * t, tr_peer * peer )
{
    tr_peer * removed;
    struct peer_atom * atom;

    assert( torrentIsLocked( t ) );

    atom = getExistingAtom( t, &peer->in_addr );
    assert( atom != NULL );
    atom->time = time( NULL );

    removed = tr_ptrArrayRemoveSorted  ( t->peers, peer, peerCompare );
    assert( removed == peer );
    freePeer( removed );
}

static void
freeTorrent( tr_peerMgr * manager, Torrent * t )
{
    uint8_t hash[SHA_DIGEST_LENGTH];

    assert( t != NULL );
    assert( t->peers != NULL );
    assert( torrentIsLocked( t ) );
    assert( getExistingTorrent( manager, t->hash ) != NULL );

    memcpy( hash, t->hash, SHA_DIGEST_LENGTH );

    tr_timerFree( &t->reconnectTimer );
    tr_timerFree( &t->reconnectSoonTimer );
    tr_timerFree( &t->rechokeTimer );
    tr_timerFree( &t->rechokeSoonTimer );
    tr_timerFree( &t->refillTimer );

    tr_bitfieldFree( t->requested );
    tr_ptrArrayFree( t->pool, (PtrArrayForeachFunc)tr_free );
    tr_ptrArrayFree( t->peers, (PtrArrayForeachFunc)freePeer );
    tr_ptrArrayRemoveSorted( manager->torrents, t, torrentCompare );
    tr_free( t );

    assert( getExistingTorrent( manager, hash ) == NULL );
}

/**
***
**/

struct tr_bitfield *
tr_peerMgrGenerateAllowedSet( const uint32_t         setCount,
                              const uint32_t         pieceCount,
                              const uint8_t          infohash[20],
                              const struct in_addr * ip )
{
    /* This has been checked against the spec example implementation. Feeding it with :
    setCount = 9, pieceCount = 1313, infohash = Oxaa,0xaa,...0xaa, ip = 80.4.4.200
generate :
    1059, 431, 808, 1217, 287, 376, 1188, 353, 508
    but since we're storing in a bitfield, it won't be in this order... */
    /* TODO : We should translate link-local IPv4 adresses to external IP, 
     * so that being on same local network gives us the same allowed pieces */
    
    printf( "%d piece allowed fast set for torrent with %d pieces and hex infohash\n", setCount, pieceCount );
    printf( "%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x for node with IP %s:\n",
            infohash[0], infohash[1], infohash[2], infohash[3], infohash[4], infohash[5], infohash[6], infohash[7], infohash[8], infohash[9],
            infohash[10], infohash[11], infohash[12], infohash[13], infohash[14], infohash[15], infohash[16], infohash[7], infohash[18], infohash[19],
            inet_ntoa( *ip ) );
    
    uint8_t *seed = malloc(4 + SHA_DIGEST_LENGTH);
    char buf[4];
    uint32_t allowedPieceCount = 0;
    tr_bitfield_t * ret;
    
    ret = tr_bitfieldNew( pieceCount );
    
    /* We need a seed based on most significant bytes of peer address
        concatenated with torrent's infohash */
    *(uint32_t*)buf = ntohl( htonl(ip->s_addr) & 0xffffff00 );
    
    memcpy( seed, &buf, 4 );
    memcpy( seed + 4, infohash, SHA_DIGEST_LENGTH );
    
    tr_sha1( seed, seed, 4 + SHA_DIGEST_LENGTH, NULL );
    
    while ( allowedPieceCount < setCount )
    {
        int i;
        for ( i = 0 ; i < 5 && allowedPieceCount < setCount ; i++ )
        {
            /* We generate indices from 4-byte chunks of the seed */
            uint32_t j = i * 4;
            uint32_t y = ntohl( *(uint32_t*)(seed + j) );
            uint32_t index = y % pieceCount;
            
            if ( !tr_bitfieldHas( ret, index ) )
            {
                tr_bitfieldAdd( ret, index );
                allowedPieceCount++;
            }
        }
        /* We randomize the seed, in case we need to iterate more */
        tr_sha1( seed, seed, SHA_DIGEST_LENGTH, NULL );
    }
    tr_free( seed );
    
    return ret;
}

tr_peerMgr*
tr_peerMgrNew( tr_handle * handle )
{
    tr_peerMgr * m = tr_new0( tr_peerMgr, 1 );
    m->handle = handle;
    m->torrents = tr_ptrArrayNew( );
    m->handshakes = tr_ptrArrayNew( );
    m->lock = tr_lockNew( );
    return m;
}

void
tr_peerMgrFree( tr_peerMgr * manager )
{
    managerLock( manager );

    tr_ptrArrayFree( manager->handshakes, (PtrArrayForeachFunc)tr_handshakeAbort );
    tr_ptrArrayFree( manager->torrents, (PtrArrayForeachFunc)freeTorrent );

    managerUnlock( manager );
    tr_lockFree( manager->lock );
    tr_free( manager );
}

static tr_peer**
getConnectedPeers( Torrent * t, int * setmeCount )
{
    int i, peerCount, connectionCount;
    tr_peer **peers;
    tr_peer **ret;

    assert( torrentIsLocked( t ) );

    peers = (tr_peer **) tr_ptrArrayPeek( t->peers, &peerCount );
    ret = tr_new( tr_peer*, peerCount );

    for( i=connectionCount=0; i<peerCount; ++i )
        if( peers[i]->msgs != NULL )
            ret[connectionCount++] = peers[i];

    *setmeCount = connectionCount;
    return ret;
}

/***
****  Refill
***/

struct tr_refill_piece
{
    tr_priority_t priority;
    uint32_t piece;
    uint32_t peerCount;
    uint32_t fastAllowed;
};

static int
compareRefillPiece (const void * aIn, const void * bIn)
{
    const struct tr_refill_piece * a = aIn;
    const struct tr_refill_piece * b = bIn;

    /* if one piece has a higher priority, it goes first */
    if (a->priority != b->priority)
        return a->priority > b->priority ? -1 : 1;

    /* otherwise if one has fewer peers, it goes first */
    if (a->peerCount != b->peerCount)
        return a->peerCount < b->peerCount ? -1 : 1;
    
    /* otherwise if one *might be* fastallowed to us */
    if (a->fastAllowed != b->fastAllowed)
        return a->fastAllowed < b->fastAllowed ? -1 : 1;

    /* otherwise go with the earlier piece */
    return a->piece - b->piece;
}

static int
isPieceInteresting( const tr_torrent  * tor,
                    int                 piece )
{
    if( tor->info.pieces[piece].dnd ) /* we don't want it */
        return 0;

    if( tr_cpPieceIsComplete( tor->completion, piece ) ) /* we already have it */
        return 0;

    return 1;
}

static uint32_t*
getPreferredPieces( Torrent     * t,
                    uint32_t    * pieceCount )
{
    const tr_torrent * tor = t->tor;
    const tr_info * inf = &tor->info;
    int i;
    uint32_t poolSize = 0;
    uint32_t * pool = tr_new( uint32_t, inf->pieceCount );
    int peerCount;
    tr_peer** peers;

    assert( torrentIsLocked( t ) );

    peers = getConnectedPeers( t, &peerCount );

    for( i=0; i<inf->pieceCount; ++i )
        if( isPieceInteresting( tor, i ) )
            pool[poolSize++] = i;

    /* sort the pool from most interesting to least... */
    if( poolSize > 1 )
    {
        uint32_t j;
        struct tr_refill_piece * p = tr_new( struct tr_refill_piece, poolSize );

        for( j=0; j<poolSize; ++j )
        {
            int k;
            const int piece = pool[j];
            struct tr_refill_piece * setme = p + j;

            setme->piece = piece;
            setme->priority = inf->pieces[piece].priority;
            setme->peerCount = 0;
            setme->fastAllowed = 0;
            /* FIXME */
//            setme->fastAllowed = tr_bitfieldHas( t->tor->allowedList, i);

            for( k=0; k<peerCount; ++k ) {
                const tr_peer * peer = peers[k];
                if( peer->peerIsInterested && !peer->clientIsChoked && tr_bitfieldHas( peer->have, piece ) )
                    ++setme->peerCount;
            }
        }

        qsort (p, poolSize, sizeof(struct tr_refill_piece), compareRefillPiece);

        for( j=0; j<poolSize; ++j )
            pool[j] = p[j].piece;

        tr_free( p );
    }

#if 0
fprintf (stderr, "new pool: ");
for (i=0; i<15 && i<(int)poolSize; ++i ) fprintf (stderr, "%d, ", (int)pool[i] );
fprintf (stderr, "\n");
#endif
    tr_free( peers );

    *pieceCount = poolSize;
    return pool;
}

static uint64_t*
getPreferredBlocks( Torrent * t, uint64_t * setmeCount )
{
    uint32_t i;
    uint32_t pieceCount;
    uint32_t * pieces;
    uint64_t *req, *unreq, *ret, *walk;
    int reqCount, unreqCount;
    const tr_torrent * tor = t->tor;

    assert( torrentIsLocked( t ) );

    pieces = getPreferredPieces( t, &pieceCount );
/*fprintf( stderr, "REFILL refillPulse for {%s} got %d of %d pieces\n", tor->info.name, (int)pieceCount, t->tor->info.pieceCount );*/

    req = tr_new( uint64_t, pieceCount *  tor->blockCountInPiece );
    reqCount = 0;
    unreq = tr_new( uint64_t, pieceCount *  tor->blockCountInPiece );
    unreqCount = 0;

    for( i=0; i<pieceCount; ++i ) {
        const uint32_t index = pieces[i];
        const int begin = tr_torPieceFirstBlock( tor, index );
        const int end = begin + tr_torPieceCountBlocks( tor, (int)index );
        int block;
        for( block=begin; block<end; ++block )
            if( tr_cpBlockIsComplete( tor->completion, block ) )
                continue;
            else if( tr_bitfieldHas( t->requested, block ) )
                req[reqCount++] = block;
            else
                unreq[unreqCount++] = block;
    }

/*fprintf( stderr, "REFILL refillPulse for {%s} reqCount is %d, unreqCount is %d\n", tor->info.name, (int)reqCount, (int)unreqCount );*/
    ret = walk = tr_new( uint64_t, unreqCount + reqCount );
    memcpy( walk, unreq, sizeof(uint64_t) * unreqCount );
    walk += unreqCount;
    memcpy( walk, req, sizeof(uint64_t) * reqCount );
    walk += reqCount;
    assert( ( walk - ret ) == ( unreqCount + reqCount ) );
    *setmeCount = walk - ret;

    tr_free( req );
    tr_free( unreq );
    tr_free( pieces );

    return ret;
}

static int
refillPulse( void * vtorrent )
{
    Torrent * t = vtorrent;
    tr_torrent * tor = t->tor;
    uint32_t i;
    int peerCount;
    tr_peer ** peers;
    uint64_t blockCount;
    uint64_t * blocks;

    if( !t->isRunning )
        return TRUE;
    if( tr_cpGetStatus( t->tor->completion ) != TR_CP_INCOMPLETE )
        return TRUE;

    torrentLock( t );

    blocks = getPreferredBlocks( t, &blockCount );
    peers = getConnectedPeers( t, &peerCount );

/*fprintf( stderr, "REFILL refillPulse for {%s} got %d blocks\n", tor->info.name, (int)blockCount );*/

    for( i=0; peerCount && i<blockCount; ++i )
    {
        const int block = blocks[i];
        const uint32_t index = tr_torBlockPiece( tor, block );
        const uint32_t begin = (block * tor->blockSize) - (index * tor->info.pieceSize);
        const uint32_t length = tr_torBlockCountBytes( tor, block );
        int j;
        assert( _tr_block( tor, index, begin ) == block );
        assert( begin < (uint32_t)tr_torPieceCountBytes( tor, (int)index ) );
        assert( (begin + length) <= (uint32_t)tr_torPieceCountBytes( tor, (int)index ) );


        /* find a peer who can ask for this block */
        for( j=0; j<peerCount; )
        {
            const int val = tr_peerMsgsAddRequest( peers[j]->msgs, index, begin, length );
            switch( val )
            {
                case TR_ADDREQ_FULL: 
                case TR_ADDREQ_CLIENT_CHOKED:
                    memmove( peers+j, peers+j+1, sizeof(tr_peer*)*(--peerCount-j) );
                    break;

                case TR_ADDREQ_MISSING: 
                    ++j;
                    break;

                case TR_ADDREQ_OK:
                    /*fprintf( stderr, "REFILL peer %p took the request for block %d\n", peers[j]->msgs, block );*/
                    tr_bitfieldAdd( t->requested, block );
                    j = peerCount;
                    break;

                default:
                    assert( 0 && "unhandled value" );
                    break;
            }
        }
    }

    /* cleanup */
    tr_free( peers );
    tr_free( blocks );

    t->refillTimer = NULL;

    torrentUnlock( t );
    return FALSE;
}

static void
broadcastClientHave( Torrent * t, uint32_t index )
{
    int i, size;
    tr_peer ** peers;

    assert( torrentIsLocked( t ) );

    peers = getConnectedPeers( t, &size );
    for( i=0; i<size; ++i )
        tr_peerMsgsHave( peers[i]->msgs, index );
    tr_free( peers );
}

static void
broadcastGotBlock( Torrent * t, uint32_t index, uint32_t offset, uint32_t length )
{
    int i, size;
    tr_peer ** peers;

    assert( torrentIsLocked( t ) );

    peers = getConnectedPeers( t, &size );
    for( i=0; i<size; ++i )
        tr_peerMsgsCancel( peers[i]->msgs, index, offset, length );
    tr_free( peers );
}

/**
***
**/

static int reconnectPulse( void * vtorrent );

static void
restartReconnectTimer( Torrent * t )
{
    tr_timerFree( &t->reconnectTimer );
    if( t->isRunning )
        t->reconnectTimer = tr_timerNew( t->manager->handle, reconnectPulse, t, RECONNECT_PERIOD_MSEC );
}

static void
reconnectNow( Torrent * t )
{
    reconnectPulse( t );
    restartReconnectTimer( t );
}

static int
reconnectSoonCB( void * vt )
{
    Torrent * t = vt;
    reconnectNow( t );
    t->reconnectSoonTimer = NULL;
    return FALSE;
}

static void
reconnectSoon( Torrent * t )
{
    if( t->reconnectSoonTimer == NULL )
        t->reconnectSoonTimer = tr_timerNew( t->manager->handle,
                                             reconnectSoonCB, t, SOON_MSEC );
}

/**
***
**/

static int rechokePulse( void * vtorrent );

static void
restartChokeTimer( Torrent * t )
{
    tr_timerFree( &t->rechokeTimer );
    if( t->isRunning )
        t->rechokeTimer = tr_timerNew( t->manager->handle, rechokePulse, t, RECHOKE_PERIOD_MSEC );
}

static void
rechokeNow( Torrent * t )
{
    rechokePulse( t );
    restartChokeTimer( t );
}

static int
rechokeSoonCB( void * vt )
{
    Torrent * t = vt;
    rechokeNow( t );
    t->rechokeSoonTimer = NULL;
    return FALSE;
}

static void
rechokeSoon( Torrent * t )
{
    if( t->rechokeSoonTimer == NULL )
        t->rechokeSoonTimer = tr_timerNew( t->manager->handle,
                                           rechokeSoonCB, t, SOON_MSEC );
}

static void
msgsCallbackFunc( void * vpeer, void * vevent, void * vt )
{
    tr_peer * peer = vpeer;
    Torrent * t = (Torrent *) vt;
    const tr_peermsgs_event * e = (const tr_peermsgs_event *) vevent;
    const int needLock = !torrentIsLocked( t );

    if( needLock )
        torrentLock( t );

    switch( e->eventType )
    {
        case TR_PEERMSG_NEED_REQ:
            if( t->refillTimer == NULL )
                t->refillTimer = tr_timerNew( t->manager->handle,
                                              refillPulse, t,
                                              REFILL_PERIOD_MSEC );
            break;

        case TR_PEERMSG_CLIENT_HAVE:
            broadcastClientHave( t, e->pieceIndex );
            tr_torrentRecheckCompleteness( t->tor );
            break;

        case TR_PEERMSG_PEER_PROGRESS: { /* if we're both seeds, then disconnect. */
#if 0
            const int clientIsSeed = tr_cpGetStatus( t->tor->completion ) != TR_CP_INCOMPLETE;
            const int peerIsSeed = e->progress >= 1.0;
            if( clientIsSeed && peerIsSeed )
                peer->doPurge = 1;
#endif
            break;
        }

        case TR_PEERMSG_CLIENT_BLOCK:
            broadcastGotBlock( t, e->pieceIndex, e->offset, e->length );
            break;

        case TR_PEERMSG_GOT_ERROR:
            peer->doPurge = 1;
            reconnectSoon( t );
            break;

        default:
            assert(0);
    }

    if( needLock )
        torrentUnlock( t );
}

static void
myHandshakeDoneCB( tr_handshake    * handshake,
                   tr_peerIo       * io,
                   int               isConnected,
                   const uint8_t   * peer_id,
                   void            * vmanager )
{
    int ok = isConnected;
    uint16_t port;
    const struct in_addr * in_addr;
    tr_peerMgr * manager = (tr_peerMgr*) vmanager;
    const uint8_t * hash = NULL;
    Torrent * t;
    tr_handshake * ours;

    assert( io != NULL );
    assert( isConnected==0 || isConnected==1 );

    ours = tr_ptrArrayRemoveSorted( manager->handshakes,
                                    handshake,
                                    handshakeCompare );
    assert( ours != NULL );
    assert( ours == handshake );

    in_addr = tr_peerIoGetAddress( io, &port );

    if( !tr_peerIoHasTorrentHash( io ) ) /* incoming connection gone wrong? */
    {
        tr_peerIoFree( io );
        --manager->connectionCount;
        return;
    }

    hash = tr_peerIoGetTorrentHash( io );
    t = getExistingTorrent( manager, hash );

    if( t != NULL )
        torrentLock( t );

    if( !t || !t->isRunning )
    {
        tr_peerIoFree( io );
        --manager->connectionCount;
    }
    else if( !ok )
    {
        /* if we couldn't connect or were snubbed,
         * the peer's probably not worth remembering. */
        tr_peer * peer = getExistingPeer( t, in_addr );
        tr_peerIoFree( io );
        --manager->connectionCount;
        if( peer )
            peer->doPurge = 1;
    }
    else /* looking good */
    {
        tr_peer * peer = getPeer( t, in_addr );
        if( peer->msgs != NULL ) { /* we already have this peer */
            tr_peerIoFree( io );
            --manager->connectionCount;
        } else {
            peer->port = port;
            peer->io = io;
            peer->msgs = tr_peerMsgsNew( t->tor, peer );
            tr_free( peer->client );
            peer->client = peer_id ? tr_clientForId( peer_id ) : NULL;
            peer->msgsTag = tr_peerMsgsSubscribe( peer->msgs, msgsCallbackFunc, t );
            rechokeSoon( t );
        }
    }

    if( t != NULL )
        torrentUnlock( t );
}

static void
initiateHandshake( tr_peerMgr * manager, tr_peerIo * io )
{
    tr_handshake * handshake;

    assert( manager->lockThread!=0 );
    assert( io != NULL );

    handshake = tr_handshakeNew( io,
                                 manager->handle->encryptionMode,
                                 myHandshakeDoneCB,
                                 manager );
    ++manager->connectionCount;

    tr_ptrArrayInsertSorted( manager->handshakes, handshake, handshakeCompare );
}

void
tr_peerMgrAddIncoming( tr_peerMgr      * manager,
                       struct in_addr  * addr,
                       uint16_t          port,
                       int               socket )
{
    managerLock( manager );

    if( getExistingHandshake( manager, addr ) == NULL )
    {
        tr_peerIo * io = tr_peerIoNewIncoming( manager->handle, addr, port, socket );
        initiateHandshake( manager, io );
    }

    managerUnlock( manager );
}

static void
maybeAddNewAtom( Torrent * t, const struct in_addr * addr, uint16_t port, uint8_t flags, uint8_t from )
{
    if( !peerIsKnown( t, addr ) )
    {
        struct peer_atom * a = tr_new( struct peer_atom, 1 );
        a->addr = *addr;
        a->port = port;
        a->flags = flags;
        a->from = from;
        a->time = 0;
fprintf( stderr, "torrent [%s] getting a new atom: %s\n", t->tor->info.name, tr_peerIoAddrStr(&a->addr,a->port) );
        tr_ptrArrayInsertSorted( t->pool, a, comparePeerAtoms );
    }
}

void
tr_peerMgrAddPex( tr_peerMgr     * manager,
                  const uint8_t  * torrentHash,
                  int              from,
                  const tr_pex   * pex,
                  int              pexCount )
{
    Torrent * t;
    const tr_pex * end;

    managerLock( manager );

    t = getExistingTorrent( manager, torrentHash );
    for( end=pex+pexCount; pex!=end; ++pex )
        maybeAddNewAtom( t, &pex->in_addr, pex->port, pex->flags, from );
    reconnectSoon( t );

    managerUnlock( manager );
}

void
tr_peerMgrAddPeers( tr_peerMgr    * manager,
                    const uint8_t * torrentHash,
                    int             from,
                    const uint8_t * peerCompact,
                    int             peerCount )
{
    int i;
    const uint8_t * walk = peerCompact;
    Torrent * t;

    managerLock( manager );

    t = getExistingTorrent( manager, torrentHash );
    for( i=0; t!=NULL && i<peerCount; ++i )
    {
        struct in_addr addr;
        uint16_t port;
        memcpy( &addr, walk, 4 ); walk += 4;
        memcpy( &port, walk, 2 ); walk += 2;
        maybeAddNewAtom( t, &addr, port, 0, from );
    }
    reconnectSoon( t );

    managerUnlock( manager );
}

/**
***
**/

int
tr_peerMgrIsAcceptingConnections( const tr_peerMgr * manager UNUSED )
{
    return TRUE; /* manager->connectionCount < MAX_CONNECTED_PEERS; */
}

void
tr_peerMgrSetBlame( tr_peerMgr     * manager UNUSED,
                    const uint8_t  * torrentHash UNUSED,
                    int              pieceIndex UNUSED,
                    int              success UNUSED )
{
    /*fprintf( stderr, "FIXME: tr_peerMgrSetBlame\n" );*/
}

int
tr_pexCompare( const void * va, const void * vb )
{
    const tr_pex * a = (const tr_pex *) va;
    const tr_pex * b = (const tr_pex *) vb;
    int i = memcmp( &a->in_addr, &b->in_addr, sizeof(struct in_addr) );
    if( i ) return i;
    if( a->port < b->port ) return -1;
    if( a->port > b->port ) return 1;
    return 0;
}

int tr_pexCompare( const void * a, const void * b );

static int
peerPrefersCrypto( const tr_peer * peer )
{
    if( peer->encryption_preference == ENCRYPTION_PREFERENCE_YES )
        return TRUE;

    if( peer->encryption_preference == ENCRYPTION_PREFERENCE_NO )
        return FALSE;

    return tr_peerIoIsEncrypted( peer->io );
};

int
tr_peerMgrGetPeers( tr_peerMgr      * manager,
                    const uint8_t   * torrentHash,
                    tr_pex         ** setme_pex )
{
    const Torrent * t = getExistingTorrent( (tr_peerMgr*)manager, torrentHash );
    const int isLocked = torrentIsLocked( t );
    int i, peerCount;
    const tr_peer ** peers;
    tr_pex * pex;
    tr_pex * walk;

    if( !isLocked )
        torrentLock( (Torrent*)t );

    peers = (const tr_peer **) tr_ptrArrayPeek( t->peers, &peerCount );
    pex = walk = tr_new( tr_pex, peerCount );

    for( i=0; i<peerCount; ++i, ++walk )
    {
        const tr_peer * peer = peers[i];

        walk->in_addr = peer->in_addr;

        walk->port = peer->port;

        walk->flags = 0;
        if( peerPrefersCrypto(peer) )  walk->flags |= 1;
        if( peer->progress >= 1.0 )    walk->flags |= 2;
    }

    assert( ( walk - pex ) == peerCount );
    qsort( pex, peerCount, sizeof(tr_pex), tr_pexCompare );
    *setme_pex = pex;

    if( !isLocked )
        torrentUnlock( (Torrent*)t );

    return peerCount;
}

void
tr_peerMgrStartTorrent( tr_peerMgr     * manager,
                        const uint8_t  * torrentHash )
{
    Torrent * t;

    managerLock( manager );

    t = getExistingTorrent( manager, torrentHash );
    t->isRunning = 1;
    restartChokeTimer( t );
    reconnectSoon( t );

    managerUnlock( manager );
}

static void
stopTorrent( Torrent * t )
{
    int i, size;
    tr_peer ** peers;

    assert( torrentIsLocked( t ) );

    t->isRunning = 0;
    tr_timerFree( &t->rechokeTimer );
    tr_timerFree( &t->reconnectTimer );

    peers = getConnectedPeers( t, &size );
    for( i=0; i<size; ++i )
        disconnectPeer( peers[i] );

    tr_free( peers );
}
void
tr_peerMgrStopTorrent( tr_peerMgr     * manager,
                       const uint8_t  * torrentHash)
{
    managerLock( manager );

    stopTorrent( getExistingTorrent( manager, torrentHash ) );

    managerUnlock( manager );
}

void
tr_peerMgrAddTorrent( tr_peerMgr * manager,
                      tr_torrent * tor )
{
    Torrent * t;

    managerLock( manager );

    assert( tor != NULL );
    assert( getExistingTorrent( manager, tor->info.hash ) == NULL );

    t = tr_new0( Torrent, 1 );
    t->manager = manager;
    t->tor = tor;
    t->pool = tr_ptrArrayNew( );
    t->peers = tr_ptrArrayNew( );
    t->requested = tr_bitfieldNew( tor->blockCount );
    restartChokeTimer( t );
    restartReconnectTimer( t );

    memcpy( t->hash, tor->info.hash, SHA_DIGEST_LENGTH );
    tr_ptrArrayInsertSorted( manager->torrents, t, torrentCompare );

    managerUnlock( manager );
}

void
tr_peerMgrRemoveTorrent( tr_peerMgr     * manager,
                         const uint8_t  * torrentHash )
{
    Torrent * t;

    managerLock( manager );

    t = getExistingTorrent( manager, torrentHash );
    assert( t != NULL );
    stopTorrent( t );
    freeTorrent( manager, t );

    managerUnlock( manager );
}

void
tr_peerMgrTorrentAvailability( const tr_peerMgr * manager,
                               const uint8_t    * torrentHash,
                               int8_t           * tab,
                               int                tabCount )
{
    int i;
    const Torrent * t;
    const tr_torrent * tor;
    float interval;

    managerLock( (tr_peerMgr*)manager );

    t = getExistingTorrent( (tr_peerMgr*)manager, torrentHash );
    tor = t->tor;
    interval = tor->info.pieceCount / (float)tabCount;

    memset( tab, 0, tabCount );

    for( i=0; i<tabCount; ++i )
    {
        const int piece = i * interval;

        if( tor == NULL )
            tab[i] = 0;
        else if( tr_cpPieceIsComplete( tor->completion, piece ) )
            tab[i] = -1;
        else {
            int j, peerCount;
            const tr_peer ** peers = (const tr_peer **) tr_ptrArrayPeek( t->peers, &peerCount );
            for( j=0; j<peerCount; ++j )
                if( tr_bitfieldHas( peers[j]->have, i ) )
                    ++tab[i];
        }
    }

    managerUnlock( (tr_peerMgr*)manager );
}

/* Returns the pieces that we and/or a connected peer has */
tr_bitfield*
tr_peerMgrGetAvailable( const tr_peerMgr * manager,
                        const uint8_t    * torrentHash )
{
    int i, size;
    const Torrent * t;
    const tr_peer ** peers;
    tr_bitfield * pieces;

    managerLock( (tr_peerMgr*)manager );

    t = getExistingTorrent( (tr_peerMgr*)manager, torrentHash );
    peers = (const tr_peer **) tr_ptrArrayPeek( t->peers, &size );
    pieces = tr_bitfieldDup( tr_cpPieceBitfield( t->tor->completion ) );
    for( i=0; i<size; ++i )
        if( peers[i]->io != NULL )
            tr_bitfieldAnd( pieces, peers[i]->have );

    managerUnlock( (tr_peerMgr*)manager );
    return pieces;
}

void
tr_peerMgrTorrentStats( const tr_peerMgr * manager,
                        const uint8_t    * torrentHash,
                        int              * setmePeersTotal,
                        int              * setmePeersConnected,
                        int              * setmePeersSendingToUs,
                        int              * setmePeersGettingFromUs,
                        int              * setmePeersFrom )
{
    int i, size;
    const Torrent * t;
    const tr_peer ** peers;

    managerLock( (tr_peerMgr*)manager );

    t = getExistingTorrent( (tr_peerMgr*)manager, torrentHash );
    peers = (const tr_peer **) tr_ptrArrayPeek( t->peers, &size );

    *setmePeersTotal          = size;
    *setmePeersConnected      = 0;
    *setmePeersSendingToUs    = 0;
    *setmePeersGettingFromUs  = 0;

    for( i=0; i<TR_PEER_FROM__MAX; ++i )
        setmePeersFrom[i] = 0;

    for( i=0; i<size; ++i )
    {
        const tr_peer * peer = peers[i];

        if( peer->io == NULL ) /* not connected */
            continue;

        ++*setmePeersConnected;

        ++setmePeersFrom[peer->from];

        if( tr_peerIoGetRateToPeer( peer->io ) > 0.01 )
            ++*setmePeersGettingFromUs;

        if( tr_peerIoGetRateToClient( peer->io ) > 0.01 )
            ++*setmePeersSendingToUs;
    }

    managerUnlock( (tr_peerMgr*)manager );
}

struct tr_peer_stat *
tr_peerMgrPeerStats( const tr_peerMgr  * manager,
                     const uint8_t     * torrentHash,
                     int               * setmeCount UNUSED )
{
    int i, size;
    const Torrent * t;
    const tr_peer ** peers;
    tr_peer_stat * ret;

    assert( manager != NULL );
    managerLock( (tr_peerMgr*)manager );

    t = getExistingTorrent( (tr_peerMgr*)manager, torrentHash );
    peers = (const tr_peer **) tr_ptrArrayPeek( t->peers, &size );

    ret = tr_new0( tr_peer_stat, size );

    for( i=0; i<size; ++i )
    {
        const tr_peer * peer = peers[i];
        const int live = peer->io != NULL;
        tr_peer_stat * stat = ret + i;

        tr_netNtop( &peer->in_addr, stat->addr, sizeof(stat->addr) );
        stat->port             = peer->port;
        stat->from             = peer->from;
        stat->client           = peer->client;
        stat->progress         = peer->progress;
        stat->isConnected      = live ? 1 : 0;
        stat->isEncrypted      = tr_peerIoIsEncrypted( peer->io ) ? 1 : 0;
        stat->uploadToRate     = tr_peerIoGetRateToPeer( peer->io );
        stat->downloadFromRate = tr_peerIoGetRateToClient( peer->io );
        stat->isDownloading    = stat->uploadToRate > 0.01;
        stat->isUploading      = stat->downloadFromRate > 0.01;
    }

    *setmeCount = size;

    managerUnlock( (tr_peerMgr*)manager );
    return ret;
}

/**
***
**/

typedef struct
{
    tr_peer * peer;
    float rate;
    int randomKey;
    int preferred;
    int doUnchoke;
}
ChokeData;

static int
compareChoke( const void * va, const void * vb )
{
    const ChokeData * a = ( const ChokeData * ) va;
    const ChokeData * b = ( const ChokeData * ) vb;

    if( a->preferred != b->preferred )
        return a->preferred ? -1 : 1;

    if( a->preferred )
    {
        if( a->rate > b->rate ) return -1;
        if( a->rate < b->rate ) return 1;
        return 0;
    }
    else
    {
        return a->randomKey - b->randomKey;
    }
}

static int
clientIsSnubbedBy( const tr_peer * peer )
{
    assert( peer != NULL );

    return peer->peerSentPieceDataAt < (time(NULL) - SNUBBED_SEC);
}

/**
***
**/

static void
rechokeLeech( Torrent * t )
{
    int i, peerCount, size=0, unchoked=0;
    const time_t ignorePeersNewerThan = time(NULL) - MIN_CHOKE_PERIOD_SEC;
    tr_peer ** peers = getConnectedPeers( t, &peerCount );
    ChokeData * choke = tr_new0( ChokeData, peerCount );

    assert( torrentIsLocked( t ) );
    
    /* sort the peers by preference and rate */
    for( i=0; i<peerCount; ++i )
    {
        tr_peer * peer = peers[i];
        ChokeData * node;
        if( peer->chokeChangedAt > ignorePeersNewerThan )
            continue;

        node = &choke[size++];
        node->peer = peer;
        node->preferred = peer->peerIsInterested && !clientIsSnubbedBy(peer);
        node->randomKey = tr_rand( INT_MAX );
        node->rate = tr_peerIoGetRateToClient( peer->io );
    }

    qsort( choke, size, sizeof(ChokeData), compareChoke );

    for( i=0; i<size && i<NUM_UNCHOKED_PEERS_PER_TORRENT; ++i ) {
        choke[i].doUnchoke = 1;
        ++unchoked;
    }

    for( ; i<size; ++i ) {
        choke[i].doUnchoke = 1;
        ++unchoked;
        if( choke[i].peer->peerIsInterested )
            break;
    }

    for( i=0; i<size; ++i )
        tr_peerMsgsSetChoke( choke[i].peer->msgs, !choke[i].doUnchoke );

    /* cleanup */
    tr_free( choke );
    tr_free( peers );
}

static void
rechokeSeed( Torrent * t )
{
    int i, size;
    tr_peer ** peers;

    assert( torrentIsLocked( t ) );

    peers = getConnectedPeers( t, &size );

    /* FIXME */
    for( i=0; i<size; ++i )
        tr_peerMsgsSetChoke( peers[i]->msgs, FALSE );

    tr_free( peers );
}

static int
rechokePulse( void * vtorrent )
{
    Torrent * t = vtorrent;
    torrentLock( t );

    const int done = tr_cpGetStatus( t->tor->completion ) != TR_CP_INCOMPLETE;
    if( done )
        rechokeLeech( vtorrent );
    else
        rechokeSeed( vtorrent );

    torrentUnlock( t );
    return TRUE;
}

/***
****
****
****
***/

struct tr_connection
{
    tr_peer * peer;
    double throughput;
};

#define LAISSEZ_FAIRE_PERIOD_SECS 60

static int
compareConnections( const void * va, const void * vb )
{
    const struct tr_connection * a = va;
    const struct tr_connection * b = vb;
    if( a->throughput < b->throughput ) return -1;
    if( a->throughput > b->throughput ) return 1;
    return 0;
}

static struct tr_connection *
getWeakConnections( Torrent * t, int * setmeSize )
{
    int i, insize, outsize;
    tr_peer ** peers = (tr_peer**) tr_ptrArrayPeek( t->peers, &insize );
    struct tr_connection * ret = tr_new( struct tr_connection, insize );
    const int clientIsSeed = tr_cpGetStatus( t->tor->completion ) != TR_CP_INCOMPLETE;
    const time_t now = time( NULL );

    assert( torrentIsLocked( t ) );

    for( i=outsize=0; i<insize; ++i )
    {
        tr_peer * peer = peers[i];
        int isWeak;
        const int peerIsSeed = peer->progress >= 1.0;
        const struct peer_atom * atom = getExistingAtom( t, &peer->in_addr );
        const double throughput = (2*tr_peerIoGetRateToPeer( peer->io ))
                                + tr_peerIoGetRateToClient( peer->io );

        assert( atom != NULL );

        /* if we're both seeds, give a little bit of time for
         * a mutual pex -- peer-msgs initiates a pex exchange
         * on startup -- and then disconnect */
        if( peerIsSeed && clientIsSeed && (now-atom->time >= 30) )
            isWeak = TRUE;
        else if( ( now - atom->time ) < LAISSEZ_FAIRE_PERIOD_SECS )
            isWeak = FALSE;
        else if( throughput >= 5 )
            isWeak = FALSE;
        else
            isWeak = TRUE;

        if( isWeak )
        {
            ret[outsize].peer = peer;
            ret[outsize].throughput = throughput;
            ++outsize;
        }
    }

    qsort( ret, outsize, sizeof(struct tr_connection), compareConnections );
    *setmeSize = outsize;
    return ret;
}

static int
compareAtomByTime( const void * va, const void * vb )
{
    const struct peer_atom * a = * (const struct peer_atom**) va;
    const struct peer_atom * b = * (const struct peer_atom**) vb;
    if( a->time < b->time ) return -1;
    if( a->time > b->time ) return 1;
    return 0;
}

static struct peer_atom **
getPeerCandidates( Torrent * t, int * setmeSize )
{
    int i, insize, outsize;
    struct peer_atom ** atoms;
    struct peer_atom ** ret;
    const time_t now = time( NULL );
    const int seed = tr_cpGetStatus( t->tor->completion ) != TR_CP_INCOMPLETE;

    assert( torrentIsLocked( t ) );

    atoms = (struct peer_atom**) tr_ptrArrayPeek( t->pool, &insize );
    ret = tr_new( struct peer_atom*, insize );
    for( i=outsize=0; i<insize; ++i )
    {
        struct peer_atom * atom = atoms[i];

        /* we don't need two connections to the same peer... */
        if( peerIsInUse( t, &atom->addr ) ) {
            fprintf( stderr, "RECONNECT peer %d (%s) is in use...\n", i, tr_peerIoAddrStr(&atom->addr,atom->port) );
            continue;
        }

        /* no need to connect if we're both seeds... */
        if( seed && ( atom->flags & 2 ) ) {
            fprintf( stderr, "RECONNECT peer %d (%s) is a seed and so are we...\n", i, tr_peerIoAddrStr(&atom->addr,atom->port) );
            continue;
        }

        /* if we used this peer recently, give someone else a turn */
        if( ( now - atom->time ) <  LAISSEZ_FAIRE_PERIOD_SECS ) {
            fprintf( stderr, "RECONNECT peer %d (%s) is in its grace period...\n", i, tr_peerIoAddrStr(&atom->addr,atom->port) );
            continue;
        }

        ret[outsize++] = atom;
    }

    qsort( ret, outsize, sizeof(struct peer_atom*), compareAtomByTime );
    *setmeSize = outsize;
    return ret;
}

static int
reconnectPulse( void * vtorrent )
{
    Torrent * t = vtorrent;
    struct peer_atom ** candidates;
    struct tr_connection * connections;
    int i, nCandidates, nConnections, nCull, nAdd;
    int peerCount;

    torrentLock( t );

    connections = getWeakConnections( t, &nConnections );
    candidates = getPeerCandidates( t, &nCandidates );

    /* figure out how many peers to disconnect */
    nCull = nConnections-4; 

fprintf( stderr, "RECONNECT pulse for [%s]: %d connections, %d candidates, %d atoms, %d cull\n", t->tor->info.name, nConnections, nCandidates, tr_ptrArraySize(t->pool), nCull );

for( i=0; i<nConnections; ++i )
fprintf( stderr, "connection #%d: %s @ %.2f\n", i+1, tr_peerIoAddrStr( &connections[i].peer->in_addr, connections[i].peer->port ), connections[i].throughput );

    /* disconnect some peers */
    for( i=0; i<nCull && i<nConnections; ++i ) {
        const double throughput = connections[i].throughput;
        tr_peer * peer = connections[i].peer;
        fprintf( stderr, "RECONNECT culling peer %s, whose throughput was %f\n", tr_peerIoAddrStr(&peer->in_addr, peer->port), throughput );
        removePeer( t, peer );
    }

    /* add some new ones */
    peerCount = tr_ptrArraySize( t->peers );
    nAdd = MAX_CONNECTED_PEERS_PER_TORRENT - peerCount;
    for( i=0; i<nAdd && i<nCandidates; ++i ) {
        struct peer_atom * atom = candidates[i];
        tr_peerIo * io = tr_peerIoNewOutgoing( t->manager->handle, &atom->addr, atom->port, t->hash );
fprintf( stderr, "RECONNECT adding an outgoing connection...\n" );
        initiateHandshake( t->manager, io );
        atom->time = time( NULL );
    }

    /* cleanup */
    tr_free( connections );
    tr_free( candidates );
    torrentUnlock( t );
    return TRUE;
}
