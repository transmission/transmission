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

#include <sys/types.h> /* event.h needs this */
#include <event.h>

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
#include "shared.h"
#include "trevent.h"
#include "utils.h"

enum
{
    /* how frequently to change which peers are choked */
    RECHOKE_PERIOD_MSEC = (1000),

    /* how frequently to decide which peers live and die */
    RECONNECT_PERIOD_MSEC = (5 * 1000),

    /* how frequently to refill peers' request lists */
    REFILL_PERIOD_MSEC = 666,

    /* don't change a peer's choke status more often than this */
    MIN_CHOKE_PERIOD_SEC = 10,

    /* following the BT spec, we consider ourselves `snubbed' if 
     * we're we don't get piece data from a peer in this long */
    SNUBBED_SEC = 60,

    /* arbitrary */
    MAX_CONNECTED_PEERS_PER_TORRENT = 60,

    /* when many peers are available, keep idle ones this long */
    MIN_UPLOAD_IDLE_SECS = 60,

    /* when few peers are available, keep idle ones this long */
    MAX_UPLOAD_IDLE_SECS = 240,

    /* how many peers to unchoke per-torrent. */
    /* FIXME: make this user-configurable? */
    NUM_UNCHOKED_PEERS_PER_TORRENT = 12, /* arbitrary */

    /* set this too high and there will be a lot of churn.
     * set it too low and you'll get peers too slowly */
    MAX_RECONNECTIONS_PER_PULSE = 10,

    /* corresponds to ut_pex's added.f flags */
    ADDED_F_ENCRYPTION_FLAG = 1,
    /* corresponds to ut_pex's added.f flags */
    ADDED_F_SEED_FLAG = 2,

    /* number of bad pieces a peer is allowed to send before we ban them */
    MAX_BAD_PIECES_PER_PEER = 3,
    /* use for bitwise operations w/peer_atom.myflags */
    MYFLAG_BANNED = 1
};


/**
***
**/

/* We keep one of these for every peer we know about, whether
 * it's connected or not, so the struct must be small.
 * When our current connections underperform, we dip back
 * into this list for new ones. */
struct peer_atom
{   
    uint8_t from;
    uint8_t flags; /* these match the added_f flags */
    uint8_t myflags; /* flags that aren't defined in added_f */
    uint16_t port;
    struct in_addr addr; 
    time_t time;
};

typedef struct
{
    uint8_t hash[SHA_DIGEST_LENGTH];
    tr_ptrArray * outgoingHandshakes; /* tr_handshake */
    tr_ptrArray * pool; /* struct peer_atom */
    tr_ptrArray * peers; /* tr_peer */
    tr_timer * reconnectTimer;
    tr_timer * rechokeTimer;
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
    tr_ptrArray * incomingHandshakes; /* tr_handshake */
};

/**
***
**/

static void
myDebug( const char * file, int line, const Torrent * t, const char * fmt, ... )
{
    FILE * fp = tr_getLog( );
    if( fp != NULL )
    {
        va_list args;
        struct evbuffer * buf = evbuffer_new( );
        char timestr[64];
        evbuffer_add_printf( buf, "[%s] %s: ",
                             tr_getLogTimeStr( timestr, sizeof(timestr) ),
                             t->tor->info.name );
        va_start( args, fmt );
        evbuffer_add_vprintf( buf, fmt, args );
        va_end( args );
        evbuffer_add_printf( buf, " (%s:%d)\n", file, line );

        fwrite( EVBUFFER_DATA(buf), 1, EVBUFFER_LENGTH(buf), fp );
        evbuffer_free( buf );
    }
}

#define tordbg(t, fmt...) myDebug(__FILE__, __LINE__, t, ##fmt )

/**
***
**/

static void
managerLock( struct tr_peerMgr * manager )
{
    tr_globalLock( manager->handle );
}
static void
managerUnlock( struct tr_peerMgr * manager )
{
    tr_globalUnlock( manager->handle );
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
    return tr_globalIsLocked( t->manager->handle );
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
getExistingHandshake( tr_ptrArray * handshakes, const struct in_addr * in_addr )
{
    return tr_ptrArrayFindSorted( handshakes,
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
    const Torrent * a = va;
    const uint8_t * b_hash = vb;
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
    const tr_peer * a = va;
    const tr_peer * b = vb;
    return compareAddresses( &a->in_addr, &b->in_addr );
}

static int
peerCompareToAddr( const void * va, const void * vb )
{
    const tr_peer * a = va;
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
peerIsInUse( const Torrent * ct, const struct in_addr * addr )
{
    Torrent * t = (Torrent*) ct;

    assert( torrentIsLocked ( t ) );

    return getExistingPeer( t, addr )
        || getExistingHandshake( t->outgoingHandshakes, addr )
        || getExistingHandshake( t->manager->incomingHandshakes, addr );
}

static tr_peer*
peerConstructor( const struct in_addr * in_addr )
{
    tr_peer * p;
    p = tr_new0( tr_peer, 1 );
    p->rcToClient = tr_rcInit( );
    p->rcToPeer = tr_rcInit( );
    memcpy( &p->in_addr, in_addr, sizeof(struct in_addr) );
    return p;
}

static tr_peer*
getPeer( Torrent * torrent, const struct in_addr * in_addr )
{
    tr_peer * peer;

    assert( torrentIsLocked( torrent ) );

    peer = getExistingPeer( torrent, in_addr );

    if( peer == NULL ) {
        peer = peerConstructor( in_addr );
        tr_ptrArrayInsertSorted( torrent->peers, peer, peerCompare );
    }

    return peer;
}

static void
peerDestructor( tr_peer * peer )
{
    assert( peer != NULL );
    assert( peer->msgs != NULL );

    tr_peerMsgsUnsubscribe( peer->msgs, peer->msgsTag );
    tr_peerMsgsFree( peer->msgs );

    tr_peerIoFree( peer->io );

    tr_bitfieldFree( peer->have );
    tr_bitfieldFree( peer->blame );
    tr_rcClose( peer->rcToClient );
    tr_rcClose( peer->rcToPeer );
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
    peerDestructor( removed );
}

static void
removeAllPeers( Torrent * t )
{
    while( !tr_ptrArrayEmpty( t->peers ) )
        removePeer( t, tr_ptrArrayNth( t->peers, 0 ) );
}

static void
torrentDestructor( Torrent * t )
{
    uint8_t hash[SHA_DIGEST_LENGTH];

    assert( t != NULL );
    assert( !t->isRunning );
    assert( t->peers != NULL );
    assert( torrentIsLocked( t ) );
    assert( tr_ptrArrayEmpty( t->outgoingHandshakes ) );
    assert( tr_ptrArrayEmpty( t->peers ) );

    memcpy( hash, t->hash, SHA_DIGEST_LENGTH );

    tr_timerFree( &t->reconnectTimer );
    tr_timerFree( &t->rechokeTimer );
    tr_timerFree( &t->refillTimer );

    tr_bitfieldFree( t->requested );
    tr_ptrArrayFree( t->pool, (PtrArrayForeachFunc)tr_free );
    tr_ptrArrayFree( t->outgoingHandshakes, NULL );
    tr_ptrArrayFree( t->peers, NULL );

    tr_free( t );
}

static Torrent*
torrentConstructor( tr_peerMgr * manager, tr_torrent * tor )
{
    Torrent * t;

    t = tr_new0( Torrent, 1 );
    t->manager = manager;
    t->tor = tor;
    t->pool = tr_ptrArrayNew( );
    t->peers = tr_ptrArrayNew( );
    t->outgoingHandshakes = tr_ptrArrayNew( );
    t->requested = tr_bitfieldNew( tor->blockCount );
    memcpy( t->hash, tor->info.hash, SHA_DIGEST_LENGTH );

    return t;
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
    m->incomingHandshakes = tr_ptrArrayNew( );
    return m;
}

void
tr_peerMgrFree( tr_peerMgr * manager )
{
    managerLock( manager );

    /* free the handshakes.  Abort invokes handshakeDoneCB(), which removes
     * the item from manager->handshakes, so this is a little roundabout... */
    while( !tr_ptrArrayEmpty( manager->incomingHandshakes ) )
        tr_handshakeAbort( tr_ptrArrayNth( manager->incomingHandshakes, 0 ) );
    tr_ptrArrayFree( manager->incomingHandshakes, NULL );

    /* free the torrents. */
    tr_ptrArrayFree( manager->torrents, (PtrArrayForeachFunc)torrentDestructor );

    managerUnlock( manager );
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
    uint16_t random;
    uint32_t piece;
    uint32_t peerCount;
    uint32_t fastAllowed;
};

static int
compareRefillPiece (const void * aIn, const void * bIn)
{
    const struct tr_refill_piece * a = aIn;
    const struct tr_refill_piece * b = bIn;
    
    /* if one *might be* fastallowed to us, get it first...
     * I'm putting it on top so we prioritise those pieces at
     * startup, then we'll have them, and we'll be denied access
     * to them */
    if (a->fastAllowed != b->fastAllowed)
        return a->fastAllowed < b->fastAllowed ? -1 : 1;
    
    /* if one piece has a higher priority, it goes first */
    if (a->priority != b->priority)
        return a->priority > b->priority ? -1 : 1;
    
    /* otherwise if one has fewer peers, it goes first */
    if (a->peerCount != b->peerCount)
        return a->peerCount < b->peerCount ? -1 : 1;

    /* otherwise go with our random seed */
    return tr_compareUint16( a->random, b->random );
}

static int
isPieceInteresting( const tr_torrent  * tor,
                    int                 piece )
{
    if( tor->info.pieces[piece].dnd ) /* we don't want it */
        return 0;

    if( tr_cpPieceIsComplete( tor->completion, piece ) ) /* we have it */
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
            setme->random = tr_rand( UINT16_MAX );

            for( k=0; k<peerCount; ++k ) {
                const tr_peer * peer = peers[k];
                if( peer->peerIsInterested && !peer->clientIsChoked && tr_bitfieldHas( peer->have, piece ) )
                    ++setme->peerCount;
                /* The fast peer extension doesn't force a peer to actually HAVE a fast-allowed piece,
                    but we're guaranteed to get the same pieces from different peers, 
                    so we'll build a list and pray one actually have this one */
                setme->fastAllowed = tr_peerMsgIsPieceFastAllowed( peer->msgs, i);
            }
        }

        qsort (p, poolSize, sizeof(struct tr_refill_piece), compareRefillPiece);

        for( j=0; j<poolSize; ++j )
            pool[j] = p[j].piece;

        tr_free( p );
    }

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
    tordbg( t, "Refilling Request Buffers..." );

    blocks = getPreferredBlocks( t, &blockCount );
    peers = getConnectedPeers( t, &peerCount );

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
                case TR_ADDREQ_DUPLICATE: 
                    ++j;
                    break;

                case TR_ADDREQ_OK:
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

static void
msgsCallbackFunc( void * vpeer, void * vevent, void * vt )
{
    tr_peer * peer = vpeer;
    Torrent * t = (Torrent *) vt;
    const tr_peermsgs_event * e = (const tr_peermsgs_event *) vevent;

    torrentLock( t );

    switch( e->eventType )
    {
        case TR_PEERMSG_NEED_REQ:
            if( t->refillTimer == NULL )
                t->refillTimer = tr_timerNew( t->manager->handle,
                                              refillPulse, t,
                                              REFILL_PERIOD_MSEC );
            break;

        case TR_PEERMSG_CANCEL:
            tr_bitfieldRem( t->requested, _tr_block( t->tor, e->pieceIndex, e->offset ) );
            break;

        case TR_PEERMSG_CLIENT_HAVE:
            broadcastClientHave( t, e->pieceIndex );
            tr_torrentRecheckCompleteness( t->tor );
            break;

        case TR_PEERMSG_PEER_PROGRESS: {
            struct peer_atom * atom = getExistingAtom( t, &peer->in_addr );
            const int peerIsSeed = e->progress >= 1.0;
            if( peerIsSeed ) {
                tordbg( t, "marking peer %s as a seed", tr_peerIoAddrStr(&atom->addr,atom->port) );
                atom->flags |= ADDED_F_SEED_FLAG;
            } else {
                tordbg( t, "marking peer %s as a non-seed", tr_peerIoAddrStr(&atom->addr,atom->port) );
                atom->flags &= ~ADDED_F_SEED_FLAG;
            } break;
        }

        case TR_PEERMSG_CLIENT_BLOCK:
            broadcastGotBlock( t, e->pieceIndex, e->offset, e->length );
            break;

        case TR_PEERMSG_GOT_ERROR:
            peer->doPurge = 1;
            break;

        default:
            assert(0);
    }

    torrentUnlock( t );
}

static void
ensureAtomExists( Torrent * t, const struct in_addr * addr, uint16_t port, uint8_t flags, uint8_t from )
{
    if( !peerIsKnown( t, addr ) )
    {
        struct peer_atom * a = tr_new0( struct peer_atom, 1 );
        a->addr = *addr;
        a->port = port;
        a->flags = flags;
        a->from = from;
        tordbg( t, "got a new atom: %s", tr_peerIoAddrStr(&a->addr,a->port) );
        tr_ptrArrayInsertSorted( t->pool, a, comparePeerAtoms );
    }
}

/* FIXME: this is kind of a mess. */
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
    Torrent * t;
    tr_handshake * ours;

    assert( io != NULL );
    assert( isConnected==0 || isConnected==1 );

    t = tr_peerIoHasTorrentHash( io )
        ? getExistingTorrent( manager, tr_peerIoGetTorrentHash( io ) )
        : NULL;

    if( tr_peerIoIsIncoming ( io ) )
        ours = tr_ptrArrayRemoveSorted( manager->incomingHandshakes,
                                        handshake, handshakeCompare );
    else if( t != NULL )
        ours = tr_ptrArrayRemoveSorted( t->outgoingHandshakes,
                                        handshake, handshakeCompare );
    else
        ours = handshake;

    assert( ours != NULL );
    assert( ours == handshake );

    if( t != NULL )
        torrentLock( t );

    in_addr = tr_peerIoGetAddress( io, &port );

    if( !ok || !t || !t->isRunning )
    {
        tr_peerIoFree( io );
    }
    else /* looking good */
    {
        uint16_t port;
        const struct in_addr * addr = tr_peerIoGetAddress( io,  &port );
        struct peer_atom * atom;
        ensureAtomExists( t, addr, port, 0, TR_PEER_FROM_INCOMING );
        atom = getExistingAtom( t, addr );

        if( atom->myflags & MYFLAG_BANNED )
        {
            tordbg( t, "banned peer %s tried to reconnect", tr_peerIoAddrStr(&atom->addr,atom->port) );
            tr_peerIoFree( io );
        }
        else
        {
            tr_peer * peer = getExistingPeer( t, addr );

            if( peer != NULL ) /* we already have this peer */
            {
                tr_peerIoFree( io );
            }
            else
            {
                peer = getPeer( t, addr );
                tr_free( peer->client );
                peer->client = peer_id ? tr_clientForId( peer_id ) : NULL;
                peer->port = port;
                peer->io = io;
                peer->msgs = tr_peerMsgsNew( t->tor, peer, msgsCallbackFunc, t, &peer->msgsTag );
                atom->time = time( NULL );
            }
        }
    }

    if( t != NULL )
        torrentUnlock( t );
}

void
tr_peerMgrAddIncoming( tr_peerMgr      * manager,
                       struct in_addr  * addr,
                       uint16_t          port,
                       int               socket )
{
    managerLock( manager );

    if( getExistingHandshake( manager->incomingHandshakes, addr ) == NULL )
    {
        tr_peerIo * io = tr_peerIoNewIncoming( manager->handle, addr, port, socket );

        tr_handshake * handshake = tr_handshakeNew( io,
                                                    manager->handle->encryptionMode,
                                                    myHandshakeDoneCB,
                                                    manager );

        tr_ptrArrayInsertSorted( manager->incomingHandshakes, handshake, handshakeCompare );
    }

    managerUnlock( manager );
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
        ensureAtomExists( t, &pex->in_addr, pex->port, pex->flags, from );

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
        ensureAtomExists( t, &addr, port, 0, from );
    }

    managerUnlock( manager );
}

/**
***
**/

void
tr_peerMgrSetBlame( tr_peerMgr     * manager,
                    const uint8_t  * torrentHash,
                    int              pieceIndex,
                    int              success )
{
    if( !success )
    {
        int peerCount, i;
        Torrent * t = getExistingTorrent( manager, torrentHash );
        tr_peer ** peers;

        assert( torrentIsLocked( t ) );

        peers = (tr_peer **) tr_ptrArrayPeek( t->peers, &peerCount );
        for( i=0; i<peerCount; ++i )
        {
            struct peer_atom * atom;
            tr_peer * peer;

            peer = peers[i];
            if( !tr_bitfieldHas( peer->blame, pieceIndex ) )
                continue;

            ++peer->strikes;
            tordbg( t, "peer %s contributed to corrupt piece (%d); now has %d strikes",
                       tr_peerIoAddrStr(&peer->in_addr,peer->port),
                       pieceIndex, (int)peer->strikes );
            if( peer->strikes < MAX_BAD_PIECES_PER_PEER )
                continue;

            atom = getExistingAtom( t, &peer->in_addr );
            atom->myflags |= MYFLAG_BANNED;
            peer->doPurge = 1;
            tordbg( t, "banning peer %s due to corrupt data", tr_peerIoAddrStr(&atom->addr,atom->port) );
        }
    }
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
    int i, peerCount;
    const tr_peer ** peers;
    tr_pex * pex;
    tr_pex * walk;

    torrentLock( (Torrent*)t );

    peers = (const tr_peer **) tr_ptrArrayPeek( t->peers, &peerCount );
    pex = walk = tr_new( tr_pex, peerCount );

    for( i=0; i<peerCount; ++i, ++walk )
    {
        const tr_peer * peer = peers[i];

        walk->in_addr = peer->in_addr;

        walk->port = peer->port;

        walk->flags = 0;
        if( peerPrefersCrypto(peer) )  walk->flags |= ADDED_F_ENCRYPTION_FLAG;
        if( peer->progress >= 1.0 )    walk->flags |= ADDED_F_SEED_FLAG;
    }

    assert( ( walk - pex ) == peerCount );
    qsort( pex, peerCount, sizeof(tr_pex), tr_pexCompare );
    *setme_pex = pex;

    torrentUnlock( (Torrent*)t );

    return peerCount;
}

static int reconnectPulse( void * vtorrent );
static int rechokePulse( void * vtorrent );

void
tr_peerMgrStartTorrent( tr_peerMgr     * manager,
                        const uint8_t  * torrentHash )
{
    Torrent * t;

    managerLock( manager );

    t = getExistingTorrent( manager, torrentHash );

    assert( t != NULL );
    assert( ( t->isRunning != 0 ) == ( t->reconnectTimer != NULL ) );
    assert( ( t->isRunning != 0 ) == ( t->rechokeTimer != NULL ) );

    if( !t->isRunning )
    {
        t->isRunning = 1;

        t->reconnectTimer = tr_timerNew( t->manager->handle,
                                         reconnectPulse, t,
                                         RECONNECT_PERIOD_MSEC );

        t->rechokeTimer = tr_timerNew( t->manager->handle,
                                       rechokePulse, t,
                                       RECHOKE_PERIOD_MSEC );

        reconnectPulse( t );

        rechokePulse( t );
    }

    managerUnlock( manager );
}

static void
stopTorrent( Torrent * t )
{
    assert( torrentIsLocked( t ) );

    t->isRunning = 0;
    tr_timerFree( &t->rechokeTimer );
    tr_timerFree( &t->reconnectTimer );

    /* disconnect the peers. */
    tr_ptrArrayForeach( t->peers, (PtrArrayForeachFunc)peerDestructor );
    tr_ptrArrayClear( t->peers );

    /* disconnect the handshakes.  handshakeAbort calls handshakeDoneCB(),
     * which removes the handshake from t->outgoingHandshakes... */
    while( !tr_ptrArrayEmpty( t->outgoingHandshakes ) )
        tr_handshakeAbort( tr_ptrArrayNth( t->outgoingHandshakes, 0 ) );
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

    t = torrentConstructor( manager, tor );
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
    tr_ptrArrayRemoveSorted( manager->torrents, t, torrentCompare );
    torrentDestructor( t );

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
                        int              * setmePeersKnown,
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

    *setmePeersKnown          = tr_ptrArraySize( t->pool );
    *setmePeersConnected      = 0;
    *setmePeersSendingToUs    = 0;
    *setmePeersGettingFromUs  = 0;

    for( i=0; i<TR_PEER_FROM__MAX; ++i )
        setmePeersFrom[i] = 0;

    for( i=0; i<size; ++i )
    {
        const tr_peer * peer = peers[i];
        const struct peer_atom * atom = getExistingAtom( t, &peer->in_addr );

        if( peer->io == NULL ) /* not connected */
            continue;

        ++*setmePeersConnected;

        ++setmePeersFrom[atom->from];

        if( peer->rateToPeer > 0.01 )
            ++*setmePeersGettingFromUs;

        if( peer->rateToClient > 0.01 )
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
        const struct peer_atom * atom = getExistingAtom( t, &peer->in_addr );
        tr_peer_stat * stat = ret + i;

        tr_netNtop( &peer->in_addr, stat->addr, sizeof(stat->addr) );
        stat->port             = peer->port;
        stat->from             = atom->from;
        stat->client           = tr_strdup( peer->client ? peer->client : "" );
        stat->progress         = peer->progress;
        stat->isEncrypted      = tr_peerIoIsEncrypted( peer->io ) ? 1 : 0;
        stat->uploadToRate     = peer->rateToPeer;
        stat->downloadFromRate = peer->rateToClient;
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

struct ChokeData
{
    tr_peer * peer;
    float rate;
    int randomKey;
    int preferred;
    int doUnchoke;
};

static int
compareChoke( const void * va, const void * vb )
{
    const struct ChokeData * a = va;
    const struct ChokeData * b = vb;

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

static double
getWeightedThroughput( const tr_peer * peer )
{
    return ( 3 * peer->rateToPeer )
         + ( 1 * peer->rateToClient );
}

static void
rechoke( Torrent * t )
{
    int i, peerCount, size=0, unchoked=0;
    const time_t ignorePeersNewerThan = time(NULL) - MIN_CHOKE_PERIOD_SEC;
    tr_peer ** peers = getConnectedPeers( t, &peerCount );
    struct ChokeData * choke = tr_new0( struct ChokeData, peerCount );

    assert( torrentIsLocked( t ) );
    
    /* sort the peers by preference and rate */
    for( i=0; i<peerCount; ++i )
    {
        tr_peer * peer = peers[i];
        struct ChokeData * node;
        if( peer->chokeChangedAt > ignorePeersNewerThan )
            continue;

        node = &choke[size++];
        node->peer = peer;
        node->preferred = peer->peerIsInterested && !clientIsSnubbedBy(peer);
        node->randomKey = tr_rand( INT_MAX );
        node->rate = getWeightedThroughput( peer );
    }

    qsort( choke, size, sizeof(struct ChokeData), compareChoke );

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

static int
rechokePulse( void * vtorrent )
{
    Torrent * t = vtorrent;
    torrentLock( t );
    rechoke( t );
    torrentUnlock( t );
    return TRUE;
}

/***
****
****  Life and Death
****
***/

static int
shouldPeerBeClosed( const Torrent * t, const tr_peer * peer, int peerCount )
{
    const tr_torrent * tor = t->tor;
    const time_t now = time( NULL );
    const struct peer_atom * atom = getExistingAtom( t, &peer->in_addr );

    /* if it's marked for purging, close it */
    if( peer->doPurge ) {
        tordbg( t, "purging peer %s because its doPurge flag is set", tr_peerIoAddrStr(&atom->addr,atom->port) );
        return TRUE;
    }

    /* if we're both seeds and it's been long enough for a pex exchange, close it */
    if( 1 ) {
        const int clientIsSeed = tr_cpGetStatus( tor->completion ) != TR_CP_INCOMPLETE;
        const int peerIsSeed = atom->flags & ADDED_F_SEED_FLAG;
        if( peerIsSeed && clientIsSeed && ( tor->pexDisabled || (now-atom->time>=30) ) ) {
            tordbg( t, "purging peer %s because we're both seeds", tr_peerIoAddrStr(&atom->addr,atom->port) );
            return TRUE;
        }
    }

    /* disconnect if it's been too long since piece data has been transferred.
     * this is on a sliding scale based on number of available peers... */
    if( 1 ) {
        const int relaxStrictnessIfFewerThanN = (int)((MAX_CONNECTED_PEERS_PER_TORRENT * 0.9) + 0.5);
        /* if we have >= relaxIfFewerThan, strictness is 100%.
         * if we have zero connections, strictness is 0% */
        const double strictness = peerCount >= relaxStrictnessIfFewerThanN
            ? 1.0
            : peerCount / (double)relaxStrictnessIfFewerThanN;
        const int lo = MIN_UPLOAD_IDLE_SECS;
        const int hi = MAX_UPLOAD_IDLE_SECS;
        const int limit = lo + ((hi-lo) * strictness);
        const time_t then = peer->pieceDataActivityDate;
        const int idleTime = then ? (now-then) : 0;
        if( idleTime > limit ) {
            tordbg( t, "purging peer %s because it's been %d secs since we shared anything",
                       tr_peerIoAddrStr(&atom->addr,atom->port), idleTime );
            return TRUE;
        }
    }

    return FALSE;
}

static tr_peer **
getPeersToClose( Torrent * t, int * setmeSize )
{
    int i, peerCount, outsize;
    tr_peer ** peers = (tr_peer**) tr_ptrArrayPeek( t->peers, &peerCount );
    struct tr_peer ** ret = tr_new( tr_peer*, peerCount );

    assert( torrentIsLocked( t ) );

    for( i=outsize=0; i<peerCount; ++i )
        if( shouldPeerBeClosed( t, peers[i], peerCount ) )
            ret[outsize++] = peers[i];

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

        /* peer fed us too much bad data ... we only keep it around
         * now to weed it out in case someone sends it to us via pex */
        if( atom->myflags & MYFLAG_BANNED ) {
            tordbg( t, "RECONNECT peer %d (%s) is banned...",
                    i, tr_peerIoAddrStr(&atom->addr,atom->port) );
            continue;
        }

        /* we don't need two connections to the same peer... */
        if( peerIsInUse( t, &atom->addr ) ) {
            tordbg( t, "RECONNECT peer %d (%s) is in use..",
                    i, tr_peerIoAddrStr(&atom->addr,atom->port) );
            continue;
        }

        /* no need to connect if we're both seeds... */
        if( seed && (atom->flags & ADDED_F_SEED_FLAG) ) {
            tordbg( t, "RECONNECT peer %d (%s) is a seed and so are we..",
                    i, tr_peerIoAddrStr(&atom->addr,atom->port) );
            continue;
        }

        /* if we used this peer recently, give someone else a turn */
        if( ( now - atom->time ) < 60 ) {
            tordbg( t, "RECONNECT peer %d (%s) is in its grace period..",
                    i, tr_peerIoAddrStr(&atom->addr,atom->port) );
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

    torrentLock( t );

    if( !t->isRunning )
    {
        removeAllPeers( t );
    }
    else
    {
        int i, nCandidates, nBad, nAdd;
        struct peer_atom ** candidates = getPeerCandidates( t, &nCandidates );
        struct tr_peer ** connections = getPeersToClose( t, &nBad );
        const int peerCount = tr_ptrArraySize( t->peers );

        if( nBad || nCandidates )
            tordbg( t, "reconnect pulse for [%s]: %d bad connections, "
                       "%d connection candidates, %d atoms, max per pulse is %d",
                       t->tor->info.name, nBad, nCandidates,
                       tr_ptrArraySize(t->pool),
                       (int)MAX_RECONNECTIONS_PER_PULSE );

        /* disconnect some peers */
        for( i=0; i<nBad; ++i )
            removePeer( t, connections[i] );

        /* add some new ones */
        nAdd = MAX_CONNECTED_PEERS_PER_TORRENT - peerCount;
        for( i=0; i<nAdd && i<nCandidates && i<MAX_RECONNECTIONS_PER_PULSE; ++i )
        {
            tr_peerMgr * mgr = t->manager;

            struct peer_atom * atom = candidates[i];

            tr_peerIo * io = tr_peerIoNewOutgoing( mgr->handle, &atom->addr, atom->port, t->hash );

            tr_handshake * handshake = tr_handshakeNew( io,
                                                        mgr->handle->encryptionMode,
                                                        myHandshakeDoneCB,
                                                        mgr );

            assert( tr_peerIoGetTorrentHash( io ) != NULL );

            tr_ptrArrayInsertSorted( t->outgoingHandshakes, handshake, handshakeCompare );

            atom->time = time( NULL );
        }

        /* cleanup */
        tr_free( connections );
        tr_free( candidates );
    }

    torrentUnlock( t );
    return TRUE;
}
