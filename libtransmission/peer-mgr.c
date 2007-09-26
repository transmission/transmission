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
#include "handshake.h"
#include "net.h"
#include "peer-io.h"
#include "peer-mgr.h"
#include "peer-mgr-private.h"
#include "peer-msgs.h"
#include "ptrarray.h"
#include "trevent.h"
#include "utils.h"

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

    /* if our connection count for a torrent is <= N% of what we wanted,
     * start relaxing the rules that decide when to disconnect a peer */
    RELAX_RULES_PERCENTAGE = 25,

    /* if we're not relaxing the rules, disconnect a peer that hasn't
     * given us anything (or taken, if we're seeding) in this long */
    MIN_TRANSFER_IDLE = 90000,

    /* even if we're relaxing the rules, disconnect a peer that hasn't
     * given us anything (or taken, if we're seeding) in this long */
    MAX_TRANSFER_IDLE = 240000,

    /* this is arbitrary and, hopefully, temporary until we come up
     * with a better idea for managing the connection limits */
    MAX_CONNECTED_PEERS_PER_TORRENT = 60,

    /* if we hang up on a peer for being worthless, don't try to
     * reconnect to it for this long. */
    MIN_HANGUP_PERIOD_SEC = 120
};

/**
***
**/

typedef struct
{
    uint8_t hash[SHA_DIGEST_LENGTH];
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
};

/**
***
**/

static int
handshakeCompareToAddr( const void * va, const void * vb )
{
    const tr_handshake * a = va;
    const struct in_addr * b = vb;
    return memcmp( tr_handshakeGetAddr( a, NULL ), b, sizeof( struct in_addr ) );
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
    return memcmp( &a->in_addr, &b->in_addr, sizeof(struct in_addr) );
}

static int
peerCompareToAddr( const void * va, const void * vb )
{
    const tr_peer * a = (const tr_peer *) va;
    const struct in_addr * b = (const struct in_addr *) vb;
    return memcmp( &a->in_addr, b, sizeof(struct in_addr) );
}

static tr_peer*
getExistingPeer( Torrent * torrent, const struct in_addr * in_addr )
{
    assert( torrent != NULL );
    assert( torrent->peers != NULL );
    assert( in_addr != NULL );

    return (tr_peer*) tr_ptrArrayFindSorted( torrent->peers,
                                             in_addr,
                                             peerCompareToAddr );
}

static tr_peer*
getPeer( Torrent * torrent, const struct in_addr * in_addr, int * isNew )
{
    tr_peer * peer = getExistingPeer( torrent, in_addr );

    if( isNew )
        *isNew = peer == NULL;

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
freeTorrent( tr_peerMgr * manager, Torrent * t )
{
    int i, size;
    tr_peer ** peers;
    uint8_t hash[SHA_DIGEST_LENGTH];

    assert( manager != NULL );
    assert( t != NULL );
    assert( t->peers != NULL );
    assert( getExistingTorrent( manager, t->hash ) != NULL );

    memcpy( hash, t->hash, SHA_DIGEST_LENGTH );

    tr_timerFree( &t->reconnectTimer );
    tr_timerFree( &t->reconnectSoonTimer );
    tr_timerFree( &t->rechokeTimer );
    tr_timerFree( &t->rechokeSoonTimer );
    tr_timerFree( &t->refillTimer );

    peers = (tr_peer **) tr_ptrArrayPeek( t->peers, &size );
    for( i=0; i<size; ++i )
        freePeer( peers[i] );

    tr_bitfieldFree( t->requested );
    tr_ptrArrayFree( t->peers );
    tr_ptrArrayRemoveSorted( manager->torrents, t, torrentCompare );
    tr_free( t );

    assert( getExistingTorrent( manager, hash ) == NULL );
}

/**
***
**/

tr_peerMgr*
tr_peerMgrNew( tr_handle * handle )
{
    tr_peerMgr * m = tr_new0( tr_peerMgr, 1 );
    m->handle = handle;
    m->torrents = tr_ptrArrayNew( );
    m->handshakes = tr_ptrArrayNew( );
    return m;
}

void
tr_peerMgrFree( tr_peerMgr * manager )
{
    while( !tr_ptrArrayEmpty( manager->handshakes ) )
        tr_handshakeAbort( (tr_handshake*)tr_ptrArrayNth( manager->handshakes, 0) );
    tr_ptrArrayFree( manager->handshakes );

    while( !tr_ptrArrayEmpty( manager->torrents ) )
        freeTorrent( manager, (Torrent*)tr_ptrArrayNth( manager->torrents, 0) );
    tr_ptrArrayFree( manager->torrents );

    tr_free( manager );
}

static tr_peer**
getConnectedPeers( Torrent * t, int * setmeCount )
{
    int i, peerCount, connectionCount;
    tr_peer **peers = (tr_peer **) tr_ptrArrayPeek( t->peers, &peerCount );
    tr_peer **ret = tr_new( tr_peer*, peerCount );

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
    tr_peer** peers = getConnectedPeers( t, &peerCount );

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
    return FALSE;
}

static void
broadcastClientHave( Torrent * t, uint32_t index )
{
    int i, size;
    tr_peer ** peers = getConnectedPeers( t, &size );
    for( i=0; i<size; ++i )
        tr_peerMsgsHave( peers[i]->msgs, index );
    tr_free( peers );
}

static void
broadcastGotBlock( Torrent * t, uint32_t index, uint32_t offset, uint32_t length )
{
    int i, size;
    tr_peer ** peers = getConnectedPeers( t, &size );
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
            const int clientIsSeed = tr_cpGetStatus( t->tor->completion ) != TR_CP_INCOMPLETE;
            const int peerIsSeed = e->progress >= 1.0;
            if( clientIsSeed && peerIsSeed )
                peer->doPurge = 1;
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
}

static void
myHandshakeDoneCB( tr_handshake    * handshake,
                   tr_peerIo       * io,
                   int               isConnected,
                   const uint8_t   * peer_id,
                   int               peerSupportsEncryption,
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
    assert( peerSupportsEncryption==0 || peerSupportsEncryption==1 );

    ours = tr_ptrArrayRemoveSorted( manager->handshakes,
                                    handshake,
                                    handshakeCompare );
    //assert( ours != NULL );
    //assert( ours == handshake );

    in_addr = tr_peerIoGetAddress( io, &port );

    if( !tr_peerIoHasTorrentHash( io ) ) /* incoming connection gone wrong? */
    {
        tr_peerIoFree( io );
        --manager->connectionCount;
        return;
    }

    hash = tr_peerIoGetTorrentHash( io );
    t = getExistingTorrent( manager, hash );
    if( !t || !t->isRunning )
    {
        tr_peerIoFree( io );
        --manager->connectionCount;
        return;
    }

    /* if we couldn't connect or were snubbed,
     * the peer's probably not worth remembering. */
    if( !ok ) {
        tr_peer * peer = getExistingPeer( t, in_addr );
        tr_peerIoFree( io );
        --manager->connectionCount;
        if( peer )
            peer->doPurge = 1;
        return;
    }

    if( 1 ) {
        tr_peer * peer = getPeer( t, in_addr, NULL );
        if( peer->msgs != NULL ) { /* we already have this peer */
            tr_peerIoFree( io );
            --manager->connectionCount;
        } else {
            peer->port = port;
            peer->io = io;
            peer->msgs = tr_peerMsgsNew( t->tor, peer );
            tr_free( peer->client );
            peer->client = peer_id ? tr_clientForId( peer_id ) : NULL;
            peer->peerSupportsEncryption = peerSupportsEncryption ? 1 : 0;
            peer->msgsTag = tr_peerMsgsSubscribe( peer->msgs, msgsCallbackFunc, t );
            peer->connectionChangedAt = time( NULL );
            rechokeSoon( t );
        }
    }
}

static void
initiateHandshake( tr_peerMgr * manager, tr_peerIo * io )
{
    tr_handshake * handshake = tr_handshakeNew( io,
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
    if( getExistingHandshake( manager, addr ) == NULL )
    {
        tr_peerIo * io = tr_peerIoNewIncoming( manager->handle, addr, port, socket );
        initiateHandshake( manager, io );
    }
}

void
tr_peerMgrAddPex( tr_peerMgr     * manager,
                  const uint8_t  * torrentHash,
                  int              from,
                  const tr_pex   * pex,
                  int              pexCount )
{
    Torrent * t = getExistingTorrent( manager, torrentHash );
    const tr_pex * end = pex + pexCount;
    while( pex != end )
    {
        int isNew;
        tr_peer * peer = getPeer( t, &pex->in_addr, &isNew );
        if( isNew ) {
            peer->port = pex->port;
            peer->from = from;
        }
        ++pex;
    }
    reconnectSoon( t );
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
    Torrent * t = getExistingTorrent( manager, torrentHash );
    for( i=0; t!=NULL && i<peerCount; ++i )
    {
        int isNew;
        tr_peer * peer;
        struct in_addr addr;
        uint16_t port;
        memcpy( &addr, walk, 4 ); walk += 4;
        memcpy( &port, walk, 2 ); walk += 2;
        peer = getPeer( t, &addr, &isNew );
        if( isNew ) {
            peer->port = port;
            peer->from = from;
        }
    }
    reconnectSoon( t );
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


int
tr_peerMgrGetPeers( tr_peerMgr      * manager,
                    const uint8_t   * torrentHash,
                    tr_pex         ** setme_pex )
{
    const Torrent * t = getExistingTorrent( (tr_peerMgr*)manager, torrentHash );
    int i, peerCount;
    const tr_peer ** peers = (const tr_peer **) tr_ptrArrayPeek( t->peers, &peerCount );
    tr_pex * pex = tr_new( tr_pex, peerCount );
    tr_pex * walk = pex;

    for( i=0; i<peerCount; ++i, ++walk )
    {
        const tr_peer * peer = peers[i];

        walk->in_addr = peer->in_addr;

        walk->port = peer->port;

        walk->flags = 0;
        if( peer->peerSupportsEncryption ) walk->flags |= 1;
        if( peer->progress >= 1.0 )        walk->flags |= 2;
    }

    assert( ( walk - pex ) == peerCount );
    qsort( pex, peerCount, sizeof(tr_pex), tr_pexCompare );
    *setme_pex = pex;
    return peerCount;
}

void
tr_peerMgrStartTorrent( tr_peerMgr     * manager,
                        const uint8_t  * torrentHash )
{
    Torrent * t = getExistingTorrent( manager, torrentHash );
    t->isRunning = 1;
    restartChokeTimer( t );
    reconnectNow( t );
}

void
tr_peerMgrStopTorrent( tr_peerMgr     * manager,
                       const uint8_t  * torrentHash)
{
    Torrent * t = getExistingTorrent( manager, torrentHash );
    t->isRunning = 0;
    tr_timerFree( &t->rechokeTimer );
    tr_timerFree( &t->reconnectTimer );
    reconnectPulse( t );
}

void
tr_peerMgrAddTorrent( tr_peerMgr * manager,
                      tr_torrent * tor )
{
    Torrent * t;

    assert( tor != NULL );
    assert( getExistingTorrent( manager, tor->info.hash ) == NULL );

    t = tr_new0( Torrent, 1 );
    t->manager = manager;
    t->tor = tor;
    t->peers = tr_ptrArrayNew( );
    t->requested = tr_bitfieldNew( tor->blockCount );
    restartChokeTimer( t );
    restartReconnectTimer( t );

    memcpy( t->hash, tor->info.hash, SHA_DIGEST_LENGTH );
    tr_ptrArrayInsertSorted( manager->torrents, t, torrentCompare );
}

void
tr_peerMgrRemoveTorrent( tr_peerMgr     * manager,
                         const uint8_t  * torrentHash )
{
    Torrent * t = getExistingTorrent( manager, torrentHash );
    assert( t != NULL );
    tr_peerMgrStopTorrent( manager, torrentHash );
    freeTorrent( manager, t );
}

void
tr_peerMgrTorrentAvailability( const tr_peerMgr * manager,
                               const uint8_t    * torrentHash,
                               int8_t           * tab,
                               int                tabCount )
{
    int i;
    const Torrent * t = getExistingTorrent( (tr_peerMgr*)manager, torrentHash );
    const tr_torrent * tor = t->tor;
    const float interval = tor->info.pieceCount / (float)tabCount;

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
    const Torrent * t = getExistingTorrent( (tr_peerMgr*)manager, torrentHash );
    const tr_peer ** peers = (const tr_peer **) tr_ptrArrayPeek( t->peers, &size );

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
}

struct tr_peer_stat *
tr_peerMgrPeerStats( const tr_peerMgr  * manager,
                     const uint8_t     * torrentHash,
                     int               * setmeCount UNUSED )
{
    int i, size;
    const Torrent * t = getExistingTorrent( (tr_peerMgr*)manager, torrentHash );
    const tr_peer ** peers = (const tr_peer **) tr_ptrArrayPeek( t->peers, &size );
    tr_peer_stat * ret;

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
    tr_peer ** peers = getConnectedPeers( t, &size );

    /* FIXME */
    for( i=0; i<size; ++i )
        tr_peerMsgsSetChoke( peers[i]->msgs, FALSE );

    tr_free( peers );
}

static int
rechokePulse( void * vtorrent )
{
    Torrent * t = vtorrent;
    const int done = tr_cpGetStatus( t->tor->completion ) != TR_CP_INCOMPLETE;
    if( done )
        rechokeLeech( vtorrent );
    else
        rechokeSeed( vtorrent );
    return TRUE;
}

/**
***
**/

static int
shouldPeerBeDisconnected( Torrent * t, tr_peer * peer, int peerCount, int isSeeding )
{
    const time_t now = time( NULL );
    int relaxStrictnessIfFewerThanN;
    double strictness;

    if( peer->io == NULL ) /* not connected */
        return FALSE;

    if( !t->isRunning ) /* the torrent is stopped... nobody should be connected */
        return TRUE;

    /* not enough peers to go around... might as well keep this one;
     * they might unchoke us or give us a pex or something */
    if( peerCount < MAX_CONNECTED_PEERS_PER_TORRENT )
        return FALSE;

    /* when deciding whether or not to keep a peer, judge its responsiveness
       on a sliding scale that's based on how many other peers are available */
    relaxStrictnessIfFewerThanN =
        (int)(((TR_MAX_PEER_COUNT * RELAX_RULES_PERCENTAGE) / 100.0) + 0.5);

    /* if we have >= relaxIfFewerThan, strictness is 100%.
       if we have zero connections, strictness is 0% */
    if( peerCount >= relaxStrictnessIfFewerThanN )
        strictness = 1.0;
    else
        strictness = peerCount / (double)relaxStrictnessIfFewerThanN;

    /* test: has it been too long since we exchanged piece data? */
    if( ( now - peer->connectionChangedAt ) >= MAX_TRANSFER_IDLE ) {
        const uint64_t lo = MIN_TRANSFER_IDLE;
        const uint64_t hi = MAX_TRANSFER_IDLE;
        const uint64_t limit = lo + ((hi-lo) * strictness);
        const uint64_t interval = now - (isSeeding ? peer->clientSentPieceDataAt : peer->peerSentPieceDataAt);
        if( interval > limit )
            return TRUE;
    }

    /* FIXME: SWE had other tests too... */

    return FALSE;
}

static int
comparePeerByConnectionDate( const void * va, const void * vb )
{
    const tr_peer * a = *(const tr_peer**) va;
    const tr_peer * b = *(const tr_peer**) vb;
    return tr_compareUint64( a->connectionChangedAt, b->connectionChangedAt );
}

static int
reconnectPulse( void * vt UNUSED )
{
    int i, size, liveCount;
    Torrent * t = vt;
    tr_peer ** peers = (tr_peer**) tr_ptrArrayPeek( t->peers, &size );
    const int isSeeding = tr_cpGetStatus( t->tor->completion ) != TR_CP_INCOMPLETE;

    /* how many connections do we have? */
    for( i=liveCount=0; i<size; ++i )
        if( peers[i]->msgs != NULL )
            ++liveCount;

    /* destroy and/or disconnect from some peers */
    for( i=0; i<size; )
    {
        tr_peer * peer = peers[i];

        if( peer->doPurge ) {
            tr_ptrArrayErase( t->peers, i, i+1 );
            freePeer( peer );
            --size;
            --liveCount;
            continue;
        }

        if( shouldPeerBeDisconnected( t, peer, liveCount, isSeeding ) ) {
            disconnectPeer( peer );
            --liveCount;
        }

        ++i;
    }

    /* maybe connect to some new peers */ 
    if( t->isRunning && (liveCount<MAX_CONNECTED_PEERS_PER_TORRENT) )
    {
        int poolSize;
        int left = MAX_CONNECTED_PEERS_PER_TORRENT - liveCount;
        tr_peer ** pool;
        tr_peerMgr * manager = t->manager;
        const time_t now = time( NULL );

        /* make a list of peers we know about but aren't connected to */
        poolSize = 0;
        pool = tr_new0( tr_peer*, size );
        for( i=0; i<size; ++i ) {
            tr_peer * peer = peers[i];
            if( peer->msgs == NULL )
                pool[poolSize++] = peer;
        }

        /* sort them s.t. the ones we've already tried are at the last of the list */
        qsort( pool, poolSize, sizeof(tr_peer*), comparePeerByConnectionDate );

        /* make some connections */
        for( i=0; i<poolSize && left>0; ++i )
        {
            tr_peer * peer = pool[i];
            tr_peerIo * io;

            if( ( now - peer->connectionChangedAt ) < MIN_HANGUP_PERIOD_SEC )
                break;

            /* already have a handshake pending */
            if( getExistingHandshake( manager, &peer->in_addr ) != NULL )
                continue;

            /* initiate a connection to the peer */
            io = tr_peerIoNewOutgoing( manager->handle, &peer->in_addr, peer->port, t->hash );
            /*fprintf( stderr, "[%s] connecting to potential peer %s\n", t->tor->info.name, tr_peerIoGetAddrStr(io) );*/
            peer->connectionChangedAt = time( NULL );
            initiateHandshake( manager, io );
            --left;
        }

        tr_free( pool );
    }

    return TRUE;
}
