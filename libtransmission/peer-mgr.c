/*
 * This file Copyright (C) 2007-2008 Charles Kerr <charles@rebelbase.com>
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
#include <string.h> /* memcpy, memcmp, strstr */
#include <stdlib.h> /* qsort */
#include <stdio.h> /* printf */
#include <limits.h> /* INT_MAX */

#include <libgen.h> /* basename */

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
#include "torrent.h"
#include "trcompat.h" /* strlcpy */
#include "trevent.h"
#include "utils.h"

/**
*** The "SWIFT" system is described by Karthik Tamilmani,
*** Vinay Pai, and Alexander Mohr of Stony Brook University
*** in their paper "SWIFT: A System With Incentives For Trading"
*** http://citeseer.ist.psu.edu/tamilmani04swift.html
***
*** More SWIFT constants are defined in peer-mgr-private.h
**/

/**
 * Allow new peers to download this many bytes from
 * us when getting started.  This can prevent gridlock
 * with other peers using tit-for-tat algorithms
 */
static const int SWIFT_INITIAL_CREDIT = 64 * 1024; /* 64 KiB */

/**
 * We expend a fraction of our torrent's total upload speed
 * on largesse by uniformly distributing free credit to
 * all of our peers.  This too helps prevent gridlock.
 */
static const double SWIFT_LARGESSE = 0.15; /* 15% of our UL */

/**
 * How frequently to extend largesse-based credit
 */
static const int SWIFT_PERIOD_MSEC = 5000;


enum
{
    /* how frequently to change which peers are choked */
    RECHOKE_PERIOD_MSEC = (10 * 1000),

    /* how frequently to refill peers' request lists */
    REFILL_PERIOD_MSEC = 666,

    /* following the BT spec, we consider ourselves `snubbed' if 
     * we're we don't get piece data from a peer in this long */
    SNUBBED_SEC = 60,

    /* when many peers are available, keep idle ones this long */
    MIN_UPLOAD_IDLE_SECS = (60 * 3),

    /* when few peers are available, keep idle ones this long */
    MAX_UPLOAD_IDLE_SECS = (60 * 10),

    /* how frequently to decide which peers live and die */
    RECONNECT_PERIOD_MSEC = (2 * 1000),

    /* max # of peers to ask fer per torrent per reconnect pulse */
    MAX_RECONNECTIONS_PER_PULSE = 8,

    /* max number of peers to ask for per second overall.
     * this throttle is to avoid overloading the router */
    MAX_CONNECTIONS_PER_SECOND = 32,

    /* corresponds to ut_pex's added.f flags */
    ADDED_F_ENCRYPTION_FLAG = 1,

    /* corresponds to ut_pex's added.f flags */
    ADDED_F_SEED_FLAG = 2,

    /* number of bad pieces a peer is allowed to send before we ban them */
    MAX_BAD_PIECES_PER_PEER = 3,

    /* use for bitwise operations w/peer_atom.myflags */
    MYFLAG_BANNED = 1,

    /* unreachable for now... but not banned.  if they try to connect to us it's okay */
    MYFLAG_UNREACHABLE = 2
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
    uint16_t numFails;
    struct in_addr addr;
    time_t time;
    time_t piece_data_time;
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
    tr_timer * swiftTimer;
    tr_torrent * tor;
    tr_peer * optimistic; /* the optimistic peer, or NULL if none */
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
        char timestr[64];
        struct evbuffer * buf = evbuffer_new( );
        char * myfile = tr_strdup( file );

        evbuffer_add_printf( buf, "[%s] ", tr_getLogTimeStr( timestr, sizeof(timestr) ) );
        if( t != NULL )
            evbuffer_add_printf( buf, "%s ", t->tor->info.name );
        va_start( args, fmt );
        evbuffer_add_vprintf( buf, fmt, args );
        va_end( args );
        evbuffer_add_printf( buf, " (%s:%d)\n", basename(myfile), line );
        fwrite( EVBUFFER_DATA(buf), 1, EVBUFFER_LENGTH(buf), fp );

        tr_free( myfile );
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
    p->credit = SWIFT_INITIAL_CREDIT;
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
    tr_timerFree( &t->swiftTimer );

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
 * For explanation, see http://www.bittorrent.org/fast_extensions.html
 * Also see the "test-allowed-set" unit test
 */
struct tr_bitfield *
tr_peerMgrGenerateAllowedSet( const uint32_t         k,         /* number of pieces in set */
                              const uint32_t         sz,        /* number of pieces in torrent */
                              const uint8_t        * infohash,  /* torrent's SHA1 hash*/
                              const struct in_addr * ip )       /* peer's address */
{
    uint8_t w[SHA_DIGEST_LENGTH + 4];
    uint8_t x[SHA_DIGEST_LENGTH];
    tr_bitfield_t * a;
    uint32_t a_size;

    *(uint32_t*)w = ntohl( htonl(ip->s_addr) & 0xffffff00 );   /* (1) */
    memcpy( w + 4, infohash, SHA_DIGEST_LENGTH );              /* (2) */
    tr_sha1( x, w, sizeof( w ), NULL );                        /* (3) */

    a = tr_bitfieldNew( sz );
    a_size = 0;
    
    while( a_size < k )
    {
        int i;
        for ( i=0; i<5 && a_size<k; ++i )                      /* (4) */
        {
            uint32_t j = i * 4;                                /* (5) */
            uint32_t y = ntohl(*(uint32_t*)(x+j));             /* (6) */
            uint32_t index = y % sz;                           /* (7) */
            if ( !tr_bitfieldHas( a, index ) ) {               /* (8) */
                tr_bitfieldAdd( a, index );                    /* (9) */
                ++a_size;
            }
        }
        tr_sha1( x, x, sizeof( x ), NULL );                    /* (3) */
    }
    
    return a;
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
    int percentDone;
    uint16_t random;
    uint32_t piece;
    uint32_t peerCount;
    uint32_t fastAllowed;
    uint32_t suggested;
};

static int
compareRefillPiece (const void * aIn, const void * bIn)
{
    const struct tr_refill_piece * a = aIn;
    const struct tr_refill_piece * b = bIn;
    
    /* if one piece has a higher priority, it goes first */
    if( a->priority != b->priority )
        return a->priority > b->priority ? -1 : 1;

    /* try to fill partial pieces */
    if( a->percentDone != b->percentDone )
        return a->percentDone > b->percentDone ? -1 : 1;
    
    /* if one *might be* fastallowed to us, get it first...
     * I'm putting it on top so we prioritize those pieces at
     * startup, then we'll have them, and we'll be denied access
     * to them */
    if (a->fastAllowed != b->fastAllowed)
        return a->fastAllowed < b->fastAllowed ? -1 : 1;
    
    /* otherwise if one was suggested to us, get it */
    if (a->suggested != b->suggested)
        return a->suggested < b->suggested ? -1 : 1;
    
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
            setme->percentDone = (int)( 100.0 * tr_cpPercentBlocksInPiece( tor->completion, piece ) );

            for( k=0; k<peerCount; ++k ) {
                const tr_peer * peer = peers[k];
                if( peer->peerIsInterested && !peer->clientIsChoked && tr_bitfieldHas( peer->have, piece ) )
                    ++setme->peerCount;
                /* The fast peer extension doesn't force a peer to actually HAVE a fast-allowed piece,
                    but we're guaranteed to get the same pieces from different peers, 
                    so we'll build a list and pray one actually have this one */
                setme->fastAllowed = tr_peerMsgsIsPieceFastAllowed( peer->msgs, i );
                /* Also, if someone SUGGESTed a piece to us, prioritize it over non-suggested others
                 */
                setme->suggested   = tr_peerMsgsIsPieceSuggested( peer->msgs, i );
            }
        }

        qsort( p, poolSize, sizeof(struct tr_refill_piece), compareRefillPiece );

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
    int s;
    uint32_t i;
    uint32_t pieceCount;
    uint32_t blockCount;
    uint32_t unreqCount[3], reqCount[3];
    uint32_t * pieces;
    uint64_t * ret, * walk;
    uint64_t * unreq[3], *req[3];
    const tr_torrent * tor = t->tor;

    assert( torrentIsLocked( t ) );

    pieces = getPreferredPieces( t, &pieceCount );

    /**
     * Now we walk through those preferred pieces to find all the blocks
     * are still missing from them.  We put unrequested blocks first,
     * of course, but by including requested blocks afterwards, endgame
     * handling happens naturally.
     *
     * By doing this once per priority we also effectively get an endgame
     * mode for each priority level.  The helps keep high priority files
     * from getting stuck at 99% due of unresponsive peers.
     */

    /* make temporary bins for the four tiers of blocks */
    for( i=0; i<3; ++i ) {
        req[i] = tr_new( uint64_t, pieceCount *  tor->blockCountInPiece );
        reqCount[i] = 0;
        unreq[i] = tr_new( uint64_t, pieceCount *  tor->blockCountInPiece );
        unreqCount[i] = 0;
    }

    /* sort the blocks into our temp bins */
    for( i=blockCount=0; i<pieceCount; ++i )
    {
        const uint32_t index = pieces[i];
        const int priorityIndex = tor->info.pieces[index].priority + 1;
        const int begin = tr_torPieceFirstBlock( tor, index );
        const int end = begin + tr_torPieceCountBlocks( tor, (int)index );
        int block;

        for( block=begin; block<end; ++block )
        {
            if( tr_cpBlockIsComplete( tor->completion, block ) )
                continue;

            ++blockCount;

            if( tr_bitfieldHas( t->requested, block ) )
            {
                const uint32_t n = reqCount[priorityIndex]++;
                req[priorityIndex][n] = block;
            }
            else
            {
                const uint32_t n = unreqCount[priorityIndex]++;
                unreq[priorityIndex][n] = block;
            }
        }
    }

    /* join the bins together, going from highest priority to lowest so
     * the the blocks we want to request first will be first in the list */
    ret = walk = tr_new( uint64_t, blockCount );
    for( s=2; s>=0; --s ) {
        memcpy( walk, unreq[s], sizeof(uint64_t) * unreqCount[s] );
        walk += unreqCount[s];
        memcpy( walk, req[s], sizeof(uint64_t) * reqCount[s] );
        walk += reqCount[s];
    }
    assert( ( walk - ret ) == ( int )blockCount );
    *setmeCount = blockCount;

    /* cleanup */
    tr_free( pieces );
    for( i=0; i<3; ++i ) {
        tr_free( unreq[i] );
        tr_free( req[i] );
    }
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
    if( tr_torrentIsSeed( t->tor ) )
        return TRUE;

    torrentLock( t );
    tordbg( t, "Refilling Request Buffers..." );

    blocks = getPreferredBlocks( t, &blockCount );
    peers = getConnectedPeers( t, &peerCount );

    for( i=0; peerCount && i<blockCount; ++i )
    {
        const uint64_t block = blocks[i];
        const uint32_t index = tr_torBlockPiece( tor, block );
        const uint32_t begin = (block * tor->blockSize) - (index * tor->info.pieceSize);
        const uint32_t length = tr_torBlockCountBytes( tor, (int)block );
        int j;
        assert( _tr_block( tor, index, begin ) == (int)block );
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
addStrike( Torrent * t, tr_peer * peer )
{
    tordbg( t, "increasing peer %s strike count to %d", tr_peerIoAddrStr(&peer->in_addr,peer->port), peer->strikes+1 );

    if( ++peer->strikes >= MAX_BAD_PIECES_PER_PEER )
    {
        struct peer_atom * atom = getExistingAtom( t, &peer->in_addr );
        atom->myflags |= MYFLAG_BANNED;
        peer->doPurge = 1;
        tordbg( t, "banning peer %s", tr_peerIoAddrStr(&atom->addr,atom->port) );
    }
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

        case TR_PEERMSG_PIECE_DATA: {
            struct peer_atom * atom = getExistingAtom( t, &peer->in_addr );
            atom->piece_data_time = time( NULL );
            break;
        }

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

        case TR_PEERMSG_GOT_ASSERT_ERROR:
            addStrike( t, peer );
            peer->doPurge = 1;
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
    if( getExistingAtom( t, addr ) == NULL )
    {
        struct peer_atom * a;
        a = tr_new0( struct peer_atom, 1 );
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
    const struct in_addr * addr;
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

    addr = tr_peerIoGetAddress( io, &port );

    if( !ok || !t || !t->isRunning )
    {
        if( t ) {
            struct peer_atom * atom = getExistingAtom( t, addr );
            if( atom )
                ++atom->numFails;
        }

        tr_peerIoFree( io );
    }
    else /* looking good */
    {
        struct peer_atom * atom;
        ensureAtomExists( t, addr, port, 0, TR_PEER_FROM_INCOMING );
        atom = getExistingAtom( t, addr );

        if( atom->myflags & MYFLAG_BANNED )
        {
            tordbg( t, "banned peer %s tried to reconnect", tr_peerIoAddrStr(&atom->addr,atom->port) );
            tr_peerIoFree( io );
        }
        else if( tr_ptrArraySize( t->peers ) >= t->tor->maxConnectedPeers )
        {
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

    if( getExistingHandshake( manager->incomingHandshakes, addr ) )
    {
        tr_netClose( socket );
    }
    else /* we don't have a connetion to them yet... */
    {
        tr_peerIo * io;
        tr_handshake * handshake;

        tordbg( NULL, "Got an INCOMING connection with %s", tr_peerIoAddrStr( addr, port ) );

        io = tr_peerIoNewIncoming( manager->handle, addr, port, socket );

        handshake = tr_handshakeNew( io,
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
                  uint8_t          from,
                  const tr_pex   * pex )
{
    Torrent * t;
    managerLock( manager );

    t = getExistingTorrent( manager, torrentHash );
    ensureAtomExists( t, &pex->in_addr, pex->port, pex->flags, from );

    managerUnlock( manager );
}

void
tr_peerMgrAddPeers( tr_peerMgr    * manager,
                    const uint8_t * torrentHash,
                    uint8_t         from,
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
            tr_peer * peer = peers[i];
            if( tr_bitfieldHas( peer->blame, pieceIndex ) )
            {
                tordbg( t, "peer %s contributed to corrupt piece (%d); now has %d strikes",
                           tr_peerIoAddrStr(&peer->in_addr,peer->port),
                           pieceIndex, (int)peer->strikes+1 );
                addStrike( t, peer );
            }
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
static int swiftPulse( void * vtorrent );

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
    assert( ( t->isRunning != 0 ) == ( t->swiftTimer != NULL ) );

    if( !t->isRunning )
    {
        t->isRunning = 1;

        t->reconnectTimer = tr_timerNew( t->manager->handle,
                                         reconnectPulse, t,
                                         RECONNECT_PERIOD_MSEC );

        t->rechokeTimer = tr_timerNew( t->manager->handle,
                                       rechokePulse, t,
                                       RECHOKE_PERIOD_MSEC );

        t->swiftTimer = tr_timerNew( t->manager->handle,
                                     swiftPulse, t,
                                     SWIFT_PERIOD_MSEC );

        reconnectPulse( t );

        rechokePulse( t );

        swiftPulse( t );
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
    tr_timerFree( &t->swiftTimer );

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
            tr_bitfieldOr( pieces, peers[i]->have );

    managerUnlock( (tr_peerMgr*)manager );
    return pieces;
}

int
tr_peerMgrHasConnections( const tr_peerMgr * manager,
                          const uint8_t    * torrentHash )
{
    int ret;
    const Torrent * t;
    managerLock( (tr_peerMgr*)manager );

    t = getExistingTorrent( (tr_peerMgr*)manager, torrentHash );
    ret = t && tr_ptrArraySize( t->peers );

    managerUnlock( (tr_peerMgr*)manager );
    return ret;
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
    tr_peer ** peers;
    tr_peer_stat * ret;

    assert( manager != NULL );
    managerLock( (tr_peerMgr*)manager );

    t = getExistingTorrent( (tr_peerMgr*)manager, torrentHash );
    peers = getConnectedPeers( (Torrent*)t, &size );
    ret = tr_new0( tr_peer_stat, size );

    for( i=0; i<size; ++i )
    {
        const tr_peer * peer = peers[i];
        const struct peer_atom * atom = getExistingAtom( t, &peer->in_addr );
        tr_peer_stat * stat = ret + i;

        tr_netNtop( &peer->in_addr, stat->addr, sizeof(stat->addr) );
        strlcpy( stat->client, (peer->client ? peer->client : ""), sizeof(stat->client) );
        stat->port             = peer->port;
        stat->from             = atom->from;
        stat->progress         = peer->progress;
        stat->isEncrypted      = tr_peerIoIsEncrypted( peer->io ) ? 1 : 0;
        stat->uploadToRate     = peer->rateToPeer;
        stat->downloadFromRate = peer->rateToClient;
        stat->isDownloading    = stat->uploadToRate > 0.01;
        stat->isUploading      = stat->downloadFromRate > 0.01;
        stat->status           = peer->status;
    }

    *setmeCount = size;
    tr_free( peers );

    managerUnlock( (tr_peerMgr*)manager );
    return ret;
}

/**
***
**/

struct ChokeData
{
    uint8_t doUnchoke;
    uint8_t isInterested;
    uint32_t rate;
    tr_peer * peer;
};

static int
compareChoke( const void * va, const void * vb )
{
    const struct ChokeData * a = va;
    const struct ChokeData * b = vb;
    return -tr_compareUint32( a->rate, b->rate );
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

static double
getWeightedRate( const tr_peer * peer, int clientIsSeed )
{
    return (int)( 10.0 * ( clientIsSeed ? peer->rateToPeer
                                        : peer->rateToClient ) );
}

static void
rechoke( Torrent * t )
{
    int i, n, peerCount, size, unchokedInterested;
    tr_peer ** peers = getConnectedPeers( t, &peerCount );
    struct ChokeData * choke = tr_new0( struct ChokeData, peerCount );
    const int clientIsSeed = tr_torrentIsSeed( t->tor );

    assert( torrentIsLocked( t ) );
    
    /* sort the peers by preference and rate */
    for( i=0, size=0; i<peerCount; ++i )
    {
        tr_peer * peer = peers[i];
        if( peer->progress >= 1.0 ) /* choke all seeds */
            tr_peerMsgsSetChoke( peer->msgs, TRUE );
        else {
            struct ChokeData * node = &choke[size++];
            node->peer = peer;
            node->isInterested = peer->peerIsInterested;
            node->rate = getWeightedRate( peer, clientIsSeed );
        }
    }

    qsort( choke, size, sizeof(struct ChokeData), compareChoke );

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
    for( i=0; i<size && unchokedInterested<t->tor->maxUnchokedPeers; ++i ) {
        choke[i].doUnchoke = 1;
        if( choke[i].isInterested )
            ++unchokedInterested;
    }
    n = i;
    while( i<size )
        choke[i++].doUnchoke = 0;

    /* optimistic unchoke */
    if( i < size )
    {
        struct ChokeData * c;
        tr_ptrArray * randPool = tr_ptrArrayNew( );
        for( ; i<size; ++i )
        {
            const tr_peer * peer = choke[i].peer;
            int x=1, y;
            if( isNew( peer ) ) x *= 3;
            if( isSame( peer ) ) x *= 3;
            for( y=0; y<x; ++y )
                tr_ptrArrayAppend( randPool, choke );
        }
        i = tr_rand( tr_ptrArraySize( randPool ) );
        c = ( struct ChokeData* )tr_ptrArrayNth( randPool, i);
        c->doUnchoke = 1;
        t->optimistic = c->peer;
        tr_ptrArrayFree( randPool, NULL );
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
***/

static int
swiftPulse( void * vtorrent )
{
    Torrent * t = vtorrent;
    torrentLock( t );

    if( !tr_torrentIsSeed( t->tor ) )
    {
        int i;
        int peerCount = 0;
        int deadbeatCount = 0;
        tr_peer ** peers = getConnectedPeers( t, &peerCount );
        tr_peer ** deadbeats = tr_new( tr_peer*, peerCount );

        const double ul_KiBsec = tr_rcRate( t->tor->upload );
        const double ul_KiB = ul_KiBsec * (SWIFT_PERIOD_MSEC/1000.0);
        const double ul_bytes = ul_KiB * 1024;
        const double freeCreditTotal = ul_bytes * SWIFT_LARGESSE;
        int freeCreditPerPeer;

        for( i=0; i<peerCount; ++i ) {
            tr_peer * peer = peers[i];
            if( peer->credit <= 0 )
                deadbeats[deadbeatCount++] =  peer;
        }

        freeCreditPerPeer = (int)( freeCreditTotal / deadbeatCount );
        for( i=0; i<deadbeatCount; ++i )
            deadbeats[i]->credit = freeCreditPerPeer;

        tordbg( t, "%d deadbeats, "
            "who are each being granted %d bytes' credit "
            "for a total of %.1f KiB, "
            "%d%% of the torrent's ul speed %.1f\n",
            deadbeatCount, freeCreditPerPeer,
            ul_KiBsec*SWIFT_LARGESSE, (int)(SWIFT_LARGESSE*100), ul_KiBsec );

        tr_free( deadbeats );
        tr_free( peers );
    }

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
        const int clientIsSeed = tr_torrentIsSeed( tor );
        const int peerIsSeed = atom->flags & ADDED_F_SEED_FLAG;
        if( peerIsSeed && clientIsSeed && ( !tr_torrentAllowsPex(tor) || (now-atom->time>=30) ) ) {
            tordbg( t, "purging peer %s because we're both seeds", tr_peerIoAddrStr(&atom->addr,atom->port) );
            return TRUE;
        }
    }

    /* disconnect if it's been too long since piece data has been transferred.
     * this is on a sliding scale based on number of available peers... */
    if( 1 ) {
        const int relaxStrictnessIfFewerThanN = (int)((tor->maxConnectedPeers * 0.9) + 0.5);
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
compareCandidates( const void * va, const void * vb )
{
    const struct peer_atom * a = * (const struct peer_atom**) va;
    const struct peer_atom * b = * (const struct peer_atom**) vb;
    int i;

    if( a->piece_data_time > b->piece_data_time ) return -1;
    if( a->piece_data_time < b->piece_data_time ) return  1;

    if(( i = tr_compareUint16( a->numFails, b->numFails )))
        return i;

    if( a->time != b->time )
        return a->time < b->time ? -1 : 1;

    return 0;
}

static struct peer_atom **
getPeerCandidates( Torrent * t, int * setmeSize )
{
    int i, atomCount, retCount;
    struct peer_atom ** atoms;
    struct peer_atom ** ret;
    const time_t now = time( NULL );
    const int seed = tr_torrentIsSeed( t->tor );

    assert( torrentIsLocked( t ) );

    atoms = (struct peer_atom**) tr_ptrArrayPeek( t->pool, &atomCount );
    ret = tr_new( struct peer_atom*, atomCount );
    for( i=retCount=0; i<atomCount; ++i )
    {
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
        if( seed && (atom->flags & ADDED_F_SEED_FLAG) )
            continue;

        /* we're wasting our time trying to connect to this bozo. */
        if( atom->numFails > 5 )
            continue;

        /* If we were connected to this peer recently and transferring
         * piece data, try to reconnect -- network troubles may have
         * disconnected us.  but if we weren't sharing piece data,
         * hold off on this peer to give another one a try instead */
        if( ( now - atom->piece_data_time ) > 30 )
        {
            int minWait = (60 * 2); /* two minutes */
            int maxWait = (60 * 20); /* twenty minutes */
            int wait = atom->numFails * 30; /* add 30 secs to the wait interval for each consecutive failure*/
            if( wait < minWait ) wait = minWait;
            if( wait > maxWait ) wait = maxWait;
            if( ( now - atom->time ) < wait ) {
                tordbg( t, "RECONNECT peer %d (%s) is in its grace period of %d seconds..",
                        i, tr_peerIoAddrStr(&atom->addr,atom->port), wait );
                continue;
            }
        }

        ret[retCount++] = atom;
    }

    qsort( ret, retCount, sizeof(struct peer_atom*), compareCandidates );
    *setmeSize = retCount;
    return ret;
}

static int
reconnectPulse( void * vtorrent )
{
    Torrent * t = vtorrent;
    static time_t prevTime = 0;
    static int newConnectionsThisSecond = 0;
    time_t now;

    torrentLock( t );

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
        int i, nCandidates, nBad;
        struct peer_atom ** candidates = getPeerCandidates( t, &nCandidates );
        struct tr_peer ** connections = getPeersToClose( t, &nBad );

        if( nBad || nCandidates )
            tordbg( t, "reconnect pulse for [%s]: %d bad connections, "
                       "%d connection candidates, %d atoms, max per pulse is %d",
                       t->tor->info.name, nBad, nCandidates,
                       tr_ptrArraySize(t->pool),
                       (int)MAX_RECONNECTIONS_PER_PULSE );

        /* disconnect some peers.
           if we got transferred piece data, then they might be good peers,
           so reset their `numFails' weight to zero.  otherwise we connected
           to them fruitlessly, so mark it as another fail */
        for( i=0; i<nBad; ++i ) {
            tr_peer * peer = connections[i];
            struct peer_atom * atom = getExistingAtom( t, &peer->in_addr );
            if( peer->pieceDataActivityDate )
                atom->numFails = 0;
            else
                ++atom->numFails;
            removePeer( t, peer );
        }

        /* add some new ones */
        for( i=0;    i < nCandidates
                  && i < MAX_RECONNECTIONS_PER_PULSE
                  && newConnectionsThisSecond < MAX_CONNECTIONS_PER_SECOND; ++i )
        {
            tr_peerMgr * mgr = t->manager;
            struct peer_atom * atom = candidates[i];
            tr_peerIo * io;

            tordbg( t, "Starting an OUTGOING connection with %s",
                       tr_peerIoAddrStr( &atom->addr, atom->port ) );

            io = tr_peerIoNewOutgoing( mgr->handle, &atom->addr, atom->port, t->hash );
            if( io == NULL )
            {
                atom->myflags |= MYFLAG_UNREACHABLE;
            }
            else
            {
                tr_handshake * handshake = tr_handshakeNew( io,
                                                            mgr->handle->encryptionMode,
                                                            myHandshakeDoneCB,
                                                            mgr );

                assert( tr_peerIoGetTorrentHash( io ) != NULL );

                ++newConnectionsThisSecond;

                tr_ptrArrayInsertSorted( t->outgoingHandshakes, handshake, handshakeCompare );
            }

            atom->time = time( NULL );
        }

        /* cleanup */
        tr_free( connections );
        tr_free( candidates );
    }

    torrentUnlock( t );
    return TRUE;
}
