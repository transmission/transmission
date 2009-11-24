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
#include <limits.h> /* INT_MAX */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <event.h>

#include "transmission.h"
#include "bencode.h"
#include "completion.h"
#include "crypto.h"
#include "inout.h"
#ifdef WIN32
#include "net.h" /* for ECONN */
#endif
#include "peer-io.h"
#include "peer-mgr.h"
#include "peer-msgs.h"
#include "platform.h" /* MAX_STACK_ARRAY_SIZE */
#include "ratecontrol.h"
#include "session.h"
#include "stats.h"
#include "torrent.h"
#include "torrent-magnet.h"
#include "tr-dht.h"
#include "utils.h"
#include "version.h"

/**
***
**/

enum
{
    BT_CHOKE                = 0,
    BT_UNCHOKE              = 1,
    BT_INTERESTED           = 2,
    BT_NOT_INTERESTED       = 3,
    BT_HAVE                 = 4,
    BT_BITFIELD             = 5,
    BT_REQUEST              = 6,
    BT_PIECE                = 7,
    BT_CANCEL               = 8,
    BT_PORT                 = 9,

    BT_FEXT_SUGGEST         = 13,
    BT_FEXT_HAVE_ALL        = 14,
    BT_FEXT_HAVE_NONE       = 15,
    BT_FEXT_REJECT          = 16,
    BT_FEXT_ALLOWED_FAST    = 17,

    BT_LTEP                 = 20,

    LTEP_HANDSHAKE          = 0,

    UT_PEX_ID               = 1,
    UT_METADATA_ID          = 3,

    MAX_PEX_PEER_COUNT      = 50,

    MIN_CHOKE_PERIOD_SEC    = 10,

    /* idle seconds before we send a keepalive */
    KEEPALIVE_INTERVAL_SECS = 100,

    PEX_INTERVAL_SECS       = 90, /* sec between sendPex() calls */

    REQQ                    = 512,

    METADATA_REQQ           = 64,

    MAX_BLOCK_SIZE          = ( 1024 * 16 ),

    /* used in lowering the outMessages queue period */
    IMMEDIATE_PRIORITY_INTERVAL_SECS = 0,
    HIGH_PRIORITY_INTERVAL_SECS = 2,
    LOW_PRIORITY_INTERVAL_SECS = 10,

    /* number of pieces to remove from the bitfield when
     * lazy bitfields are turned on */
    LAZY_PIECE_COUNT = 26,

    /* number of pieces we'll allow in our fast set */
    MAX_FAST_SET_SIZE = 3,

    /* defined in BEP #9 */
    METADATA_MSG_TYPE_REQUEST = 0,
    METADATA_MSG_TYPE_DATA = 1,
    METADATA_MSG_TYPE_REJECT = 2
};

enum
{
    AWAITING_BT_LENGTH,
    AWAITING_BT_ID,
    AWAITING_BT_MESSAGE,
    AWAITING_BT_PIECE
};

/**
***
**/

struct peer_request
{
    uint32_t    index;
    uint32_t    offset;
    uint32_t    length;
};

static uint32_t
getBlockOffsetInPiece( const tr_torrent * tor, uint64_t b )
{
    const uint64_t piecePos = tor->info.pieceSize * tr_torBlockPiece( tor, b );
    const uint64_t blockPos = tor->blockSize * b;
    assert( blockPos >= piecePos );
    return (uint32_t)( blockPos - piecePos );
}

static void
blockToReq( const tr_torrent     * tor,
            tr_block_index_t       block,
            struct peer_request  * setme )
{
    assert( setme != NULL );

    setme->index = tr_torBlockPiece( tor, block );
    setme->offset = getBlockOffsetInPiece( tor, block );
    setme->length = tr_torBlockCountBytes( tor, block );
}

/**
***
**/

/* this is raw, unchanged data from the peer regarding
 * the current message that it's sending us. */
struct tr_incoming
{
    uint8_t                id;
    uint32_t               length; /* includes the +1 for id length */
    struct peer_request    blockReq; /* metadata for incoming blocks */
    struct evbuffer *      block; /* piece data for incoming blocks */
};

/**
 * Low-level communication state information about a connected peer.
 *
 * This structure remembers the low-level protocol states that we're
 * in with this peer, such as active requests, pex messages, and so on.
 * Its fields are all private to peer-msgs.c.
 *
 * Data not directly involved with sending & receiving messages is
 * stored in tr_peer, where it can be accessed by both peermsgs and
 * the peer manager.
 *
 * @see struct peer_atom
 * @see tr_peer
 */
struct tr_peermsgs
{
    tr_bool         peerSupportsPex;
    tr_bool         peerSupportsMetadataXfer;
    tr_bool         clientSentLtepHandshake;
    tr_bool         peerSentLtepHandshake;
    tr_bool         requestingMetadataFromPeer;

    /*tr_bool         haveFastSet;*/

    int             activeRequestCount;
    int             desiredRequestCount;

    int             prefetchCount;

    /* how long the outMessages batch should be allowed to grow before
     * it's flushed -- some messages (like requests >:) should be sent
     * very quickly; others aren't as urgent. */
    int8_t          outMessagesBatchPeriod;

    uint8_t         state;
    uint8_t         ut_pex_id;
    uint8_t         ut_metadata_id;
    uint16_t        pexCount;
    uint16_t        pexCount6;

#if 0
    size_t                 fastsetSize;
    tr_piece_index_t       fastset[MAX_FAST_SET_SIZE];
#endif

    tr_peer *              peer;

    tr_torrent *           torrent;

    tr_publisher           publisher;

    struct evbuffer *      outMessages; /* all the non-piece messages */

    struct peer_request    peerAskedFor[REQQ];
    int                    peerAskedForCount;

    int                    peerAskedForMetadata[METADATA_REQQ];
    int                    peerAskedForMetadataCount;

    tr_pex               * pex;
    tr_pex               * pex6;

    /*time_t                 clientSentPexAt;*/
    time_t                 clientSentAnythingAt;

    /* when we started batching the outMessages */
    time_t                outMessagesBatchedAt;

    struct tr_incoming    incoming;

    /* if the peer supports the Extension Protocol in BEP 10 and
       supplied a reqq argument, it's stored here.  otherwise the
       value is zero and should be ignored. */
    int64_t               reqq;

    struct event          pexTimer;
};

/**
***
**/

#if 0
static tr_bitfield*
getHave( const struct tr_peermsgs * msgs )
{
    if( msgs->peer->have == NULL )
        msgs->peer->have = tr_bitfieldNew( msgs->torrent->info.pieceCount );
    return msgs->peer->have;
}
#endif

static TR_INLINE tr_session*
getSession( struct tr_peermsgs * msgs )
{
    return msgs->torrent->session;
}

/**
***
**/

static void
myDebug( const char * file, int line,
         const struct tr_peermsgs * msgs,
         const char * fmt, ... )
{
    FILE * fp = tr_getLog( );

    if( fp )
    {
        va_list           args;
        char              timestr[64];
        struct evbuffer * buf = evbuffer_new( );
        char *            base = tr_basename( file );

        evbuffer_add_printf( buf, "[%s] %s - %s [%s]: ",
                             tr_getLogTimeStr( timestr, sizeof( timestr ) ),
                             tr_torrentName( msgs->torrent ),
                             tr_peerIoGetAddrStr( msgs->peer->io ),
                             msgs->peer->client );
        va_start( args, fmt );
        evbuffer_add_vprintf( buf, fmt, args );
        va_end( args );
        evbuffer_add_printf( buf, " (%s:%d)\n", base, line );
        /* FIXME(libevent2) tr_getLog() should return an fd, then use evbuffer_write() here */
        fwrite( EVBUFFER_DATA( buf ), 1, EVBUFFER_LENGTH( buf ), fp );

        tr_free( base );
        evbuffer_free( buf );
    }
}

#define dbgmsg( msgs, ... ) \
    do { \
        if( tr_deepLoggingIsActive( ) ) \
            myDebug( __FILE__, __LINE__, msgs, __VA_ARGS__ ); \
    } while( 0 )

/**
***
**/

static void
pokeBatchPeriod( tr_peermsgs * msgs,
                 int           interval )
{
    if( msgs->outMessagesBatchPeriod > interval )
    {
        msgs->outMessagesBatchPeriod = interval;
        dbgmsg( msgs, "lowering batch interval to %d seconds", interval );
    }
}

static TR_INLINE void
dbgOutMessageLen( tr_peermsgs * msgs )
{
    dbgmsg( msgs, "outMessage size is now %zu", EVBUFFER_LENGTH( msgs->outMessages ) );
}

static void
protocolSendReject( tr_peermsgs * msgs, const struct peer_request * req )
{
    tr_peerIo       * io  = msgs->peer->io;
    struct evbuffer * out = msgs->outMessages;

    assert( tr_peerIoSupportsFEXT( msgs->peer->io ) );

    tr_peerIoWriteUint32( io, out, sizeof( uint8_t ) + 3 * sizeof( uint32_t ) );
    tr_peerIoWriteUint8 ( io, out, BT_FEXT_REJECT );
    tr_peerIoWriteUint32( io, out, req->index );
    tr_peerIoWriteUint32( io, out, req->offset );
    tr_peerIoWriteUint32( io, out, req->length );

    dbgmsg( msgs, "rejecting %u:%u->%u...", req->index, req->offset, req->length );
    dbgOutMessageLen( msgs );
}

static void
protocolSendRequest( tr_peermsgs               * msgs,
                     const struct peer_request * req )
{
    tr_peerIo       * io  = msgs->peer->io;
    struct evbuffer * out = msgs->outMessages;

    tr_peerIoWriteUint32( io, out, sizeof( uint8_t ) + 3 * sizeof( uint32_t ) );
    tr_peerIoWriteUint8 ( io, out, BT_REQUEST );
    tr_peerIoWriteUint32( io, out, req->index );
    tr_peerIoWriteUint32( io, out, req->offset );
    tr_peerIoWriteUint32( io, out, req->length );

    dbgmsg( msgs, "requesting %u:%u->%u...", req->index, req->offset, req->length );
    dbgOutMessageLen( msgs );
    pokeBatchPeriod( msgs, IMMEDIATE_PRIORITY_INTERVAL_SECS );
}

static void
protocolSendCancel( tr_peermsgs               * msgs,
                    const struct peer_request * req )
{
    tr_peerIo       * io  = msgs->peer->io;
    struct evbuffer * out = msgs->outMessages;

    tr_peerIoWriteUint32( io, out, sizeof( uint8_t ) + 3 * sizeof( uint32_t ) );
    tr_peerIoWriteUint8 ( io, out, BT_CANCEL );
    tr_peerIoWriteUint32( io, out, req->index );
    tr_peerIoWriteUint32( io, out, req->offset );
    tr_peerIoWriteUint32( io, out, req->length );

    dbgmsg( msgs, "cancelling %u:%u->%u...", req->index, req->offset, req->length );
    dbgOutMessageLen( msgs );
    pokeBatchPeriod( msgs, IMMEDIATE_PRIORITY_INTERVAL_SECS );
}

static void
protocolSendPort(tr_peermsgs *msgs, uint16_t port)
{
    tr_peerIo       * io  = msgs->peer->io;
    struct evbuffer * out = msgs->outMessages;

    dbgmsg( msgs, "sending Port %u", port);
    tr_peerIoWriteUint32( io, out, 3 );
    tr_peerIoWriteUint8 ( io, out, BT_PORT );
    tr_peerIoWriteUint16( io, out, port);
}

static void
protocolSendHave( tr_peermsgs * msgs,
                  uint32_t      index )
{
    tr_peerIo       * io  = msgs->peer->io;
    struct evbuffer * out = msgs->outMessages;

    tr_peerIoWriteUint32( io, out, sizeof(uint8_t) + sizeof(uint32_t) );
    tr_peerIoWriteUint8 ( io, out, BT_HAVE );
    tr_peerIoWriteUint32( io, out, index );

    dbgmsg( msgs, "sending Have %u", index );
    dbgOutMessageLen( msgs );
    pokeBatchPeriod( msgs, LOW_PRIORITY_INTERVAL_SECS );
}

#if 0
static void
protocolSendAllowedFast( tr_peermsgs * msgs, uint32_t pieceIndex )
{
    tr_peerIo       * io  = msgs->peer->io;
    struct evbuffer * out = msgs->outMessages;

    assert( tr_peerIoSupportsFEXT( msgs->peer->io ) );

    tr_peerIoWriteUint32( io, out, sizeof(uint8_t) + sizeof(uint32_t) );
    tr_peerIoWriteUint8 ( io, out, BT_FEXT_ALLOWED_FAST );
    tr_peerIoWriteUint32( io, out, pieceIndex );

    dbgmsg( msgs, "sending Allowed Fast %u...", pieceIndex );
    dbgOutMessageLen( msgs );
}
#endif

static void
protocolSendChoke( tr_peermsgs * msgs,
                   int           choke )
{
    tr_peerIo       * io  = msgs->peer->io;
    struct evbuffer * out = msgs->outMessages;

    tr_peerIoWriteUint32( io, out, sizeof( uint8_t ) );
    tr_peerIoWriteUint8 ( io, out, choke ? BT_CHOKE : BT_UNCHOKE );

    dbgmsg( msgs, "sending %s...", choke ? "Choke" : "Unchoke" );
    dbgOutMessageLen( msgs );
    pokeBatchPeriod( msgs, IMMEDIATE_PRIORITY_INTERVAL_SECS );
}

static void
protocolSendHaveAll( tr_peermsgs * msgs )
{
    tr_peerIo       * io  = msgs->peer->io;
    struct evbuffer * out = msgs->outMessages;

    assert( tr_peerIoSupportsFEXT( msgs->peer->io ) );

    tr_peerIoWriteUint32( io, out, sizeof( uint8_t ) );
    tr_peerIoWriteUint8 ( io, out, BT_FEXT_HAVE_ALL );

    dbgmsg( msgs, "sending HAVE_ALL..." );
    dbgOutMessageLen( msgs );
    pokeBatchPeriod( msgs, IMMEDIATE_PRIORITY_INTERVAL_SECS );
}

static void
protocolSendHaveNone( tr_peermsgs * msgs )
{
    tr_peerIo       * io  = msgs->peer->io;
    struct evbuffer * out = msgs->outMessages;

    assert( tr_peerIoSupportsFEXT( msgs->peer->io ) );

    tr_peerIoWriteUint32( io, out, sizeof( uint8_t ) );
    tr_peerIoWriteUint8 ( io, out, BT_FEXT_HAVE_NONE );

    dbgmsg( msgs, "sending HAVE_NONE..." );
    dbgOutMessageLen( msgs );
    pokeBatchPeriod( msgs, IMMEDIATE_PRIORITY_INTERVAL_SECS );
}

/**
***  EVENTS
**/

static const tr_peer_event blankEvent = { 0, 0, 0, 0, 0.0f, 0, 0, 0, 0 };

static void
publish( tr_peermsgs * msgs, tr_peer_event * e )
{
    assert( msgs->peer );
    assert( msgs->peer->msgs == msgs );

    tr_publisherPublish( &msgs->publisher, msgs->peer, e );
}

static void
fireError( tr_peermsgs * msgs, int err )
{
    tr_peer_event e = blankEvent;
    e.eventType = TR_PEER_ERROR;
    e.err = err;
    publish( msgs, &e );
}

static void
fireUploadOnly( tr_peermsgs * msgs, tr_bool uploadOnly )
{
    tr_peer_event e = blankEvent;
    e.eventType = TR_PEER_UPLOAD_ONLY;
    e.uploadOnly = uploadOnly;
    publish( msgs, &e );
}

static void
firePeerProgress( tr_peermsgs * msgs )
{
    tr_peer_event e = blankEvent;
    e.eventType = TR_PEER_PEER_PROGRESS;
    e.progress = msgs->peer->progress;
    publish( msgs, &e );
}

static void
fireGotBlock( tr_peermsgs * msgs, const struct peer_request * req )
{
    tr_peer_event e = blankEvent;
    e.eventType = TR_PEER_CLIENT_GOT_BLOCK;
    e.pieceIndex = req->index;
    e.offset = req->offset;
    e.length = req->length;
    publish( msgs, &e );
}

static void
fireGotRej( tr_peermsgs * msgs, const struct peer_request * req )
{
    tr_peer_event e = blankEvent;
    e.eventType = TR_PEER_CLIENT_GOT_REJ;
    e.pieceIndex = req->index;
    e.offset = req->offset;
    e.length = req->length;
    publish( msgs, &e );
}

static void
fireGotChoke( tr_peermsgs * msgs )
{
    tr_peer_event e = blankEvent;
    e.eventType = TR_PEER_CLIENT_GOT_CHOKE;
    publish( msgs, &e );
}

static void
fireClientGotData( tr_peermsgs * msgs,
                   uint32_t      length,
                   int           wasPieceData )
{
    tr_peer_event e = blankEvent;

    e.length = length;
    e.eventType = TR_PEER_CLIENT_GOT_DATA;
    e.wasPieceData = wasPieceData;
    publish( msgs, &e );
}

static void
fireClientGotSuggest( tr_peermsgs * msgs, uint32_t pieceIndex )
{
    tr_peer_event e = blankEvent;
    e.eventType = TR_PEER_CLIENT_GOT_SUGGEST;
    e.pieceIndex = pieceIndex;
    publish( msgs, &e );
}

static void
fireClientGotPort( tr_peermsgs * msgs, tr_port port )
{
    tr_peer_event e = blankEvent;
    e.eventType = TR_PEER_CLIENT_GOT_PORT;
    e.port = port;
    publish( msgs, &e );
}

static void
fireClientGotAllowedFast( tr_peermsgs * msgs, uint32_t pieceIndex )
{
    tr_peer_event e = blankEvent;
    e.eventType = TR_PEER_CLIENT_GOT_ALLOWED_FAST;
    e.pieceIndex = pieceIndex;
    publish( msgs, &e );
}

static void
firePeerGotData( tr_peermsgs  * msgs,
                 uint32_t       length,
                 int            wasPieceData )
{
    tr_peer_event e = blankEvent;

    e.length = length;
    e.eventType = TR_PEER_PEER_GOT_DATA;
    e.wasPieceData = wasPieceData;

    publish( msgs, &e );
}

/**
***  ALLOWED FAST SET
***  For explanation, see http://www.bittorrent.org/beps/bep_0006.html
**/

size_t
tr_generateAllowedSet( tr_piece_index_t * setmePieces,
                       size_t             desiredSetSize,
                       size_t             pieceCount,
                       const uint8_t    * infohash,
                       const tr_address * addr )
{
    size_t setSize = 0;

    assert( setmePieces );
    assert( desiredSetSize <= pieceCount );
    assert( desiredSetSize );
    assert( pieceCount );
    assert( infohash );
    assert( addr );

    if( addr->type == TR_AF_INET )
    {
        uint8_t w[SHA_DIGEST_LENGTH + 4], *walk=w;
        uint8_t x[SHA_DIGEST_LENGTH];

        uint32_t ui32 = ntohl( htonl( addr->addr.addr4.s_addr ) & 0xffffff00 );   /* (1) */
        memcpy( w, &ui32, sizeof( uint32_t ) );
        walk += sizeof( uint32_t );
        memcpy( walk, infohash, SHA_DIGEST_LENGTH );                 /* (2) */
        walk += SHA_DIGEST_LENGTH;
        tr_sha1( x, w, walk-w, NULL );                               /* (3) */
        assert( sizeof( w ) == walk-w );

        while( setSize<desiredSetSize )
        {
            int i;
            for( i=0; i<5 && setSize<desiredSetSize; ++i )           /* (4) */
            {
                size_t k;
                uint32_t j = i * 4;                                  /* (5) */
                uint32_t y = ntohl( *( uint32_t* )( x + j ) );       /* (6) */
                uint32_t index = y % pieceCount;                     /* (7) */

                for( k=0; k<setSize; ++k )                           /* (8) */
                    if( setmePieces[k] == index )
                        break;

                if( k == setSize )
                    setmePieces[setSize++] = index;                  /* (9) */
            }

            tr_sha1( x, x, sizeof( x ), NULL );                      /* (3) */
        }
    }

    return setSize;
}

static void
updateFastSet( tr_peermsgs * msgs UNUSED )
{
#if 0
    const tr_bool fext = tr_peerIoSupportsFEXT( msgs->peer->io );
    const int peerIsNeedy = msgs->peer->progress < 0.10;

    if( fext && peerIsNeedy && !msgs->haveFastSet )
    {
        size_t i;
        const struct tr_address * addr = tr_peerIoGetAddress( msgs->peer->io, NULL );
        const tr_info * inf = &msgs->torrent->info;
        const size_t numwant = MIN( MAX_FAST_SET_SIZE, inf->pieceCount );

        /* build the fast set */
        msgs->fastsetSize = tr_generateAllowedSet( msgs->fastset, numwant, inf->pieceCount, inf->hash, addr );
        msgs->haveFastSet = 1;

        /* send it to the peer */
        for( i=0; i<msgs->fastsetSize; ++i )
            protocolSendAllowedFast( msgs, msgs->fastset[i] );
    }
#endif
}

/**
***  INTEREST
**/

static tr_bool
isPieceInteresting( const tr_peermsgs * msgs,
                    tr_piece_index_t    piece )
{
    const tr_torrent * torrent = msgs->torrent;

    return ( !torrent->info.pieces[piece].dnd )                  /* we want it */
          && ( !tr_cpPieceIsComplete( &torrent->completion, piece ) ) /* !have */
          && ( tr_bitsetHas( &msgs->peer->have, piece ) );      /* peer has it */
}

/* "interested" means we'll ask for piece data if they unchoke us */
static tr_bool
isPeerInteresting( const tr_peermsgs * msgs )
{
    tr_piece_index_t    i;
    const tr_torrent *  torrent;
    const tr_bitfield * bitfield;
    const int           clientIsSeed = tr_torrentIsSeed( msgs->torrent );

    if( clientIsSeed )
        return FALSE;

    if( !tr_torrentIsPieceTransferAllowed( msgs->torrent, TR_PEER_TO_CLIENT ) )
        return FALSE;

    torrent = msgs->torrent;
    bitfield = tr_cpPieceBitfield( &torrent->completion );

    for( i = 0; i < torrent->info.pieceCount; ++i )
        if( isPieceInteresting( msgs, i ) )
            return TRUE;

    return FALSE;
}

static void
sendInterest( tr_peermsgs * msgs,
              int           weAreInterested )
{
    struct evbuffer * out = msgs->outMessages;

    assert( msgs );
    assert( weAreInterested == 0 || weAreInterested == 1 );

    msgs->peer->clientIsInterested = weAreInterested;
    dbgmsg( msgs, "Sending %s", weAreInterested ? "Interested" : "Not Interested" );
    tr_peerIoWriteUint32( msgs->peer->io, out, sizeof( uint8_t ) );
    tr_peerIoWriteUint8 ( msgs->peer->io, out, weAreInterested ? BT_INTERESTED : BT_NOT_INTERESTED );

    pokeBatchPeriod( msgs, HIGH_PRIORITY_INTERVAL_SECS );
    dbgOutMessageLen( msgs );
}

static void
updateInterest( tr_peermsgs * msgs )
{
    const int i = isPeerInteresting( msgs );

    if( i != msgs->peer->clientIsInterested )
        sendInterest( msgs, i );
}

static tr_bool
popNextMetadataRequest( tr_peermsgs * msgs, int * piece )
{
    if( msgs->peerAskedForMetadataCount == 0 )
        return FALSE;

    *piece = msgs->peerAskedForMetadata[0];

    memmove( msgs->peerAskedForMetadata,
             msgs->peerAskedForMetadata + 1,
             sizeof( int ) * --msgs->peerAskedForMetadataCount );

    return TRUE;
}

static tr_bool
popNextRequest( tr_peermsgs * msgs, struct peer_request * setme )
{
    if( msgs->peerAskedForCount == 0 )
        return FALSE;

    *setme = msgs->peerAskedFor[0];

    memmove( msgs->peerAskedFor,
             msgs->peerAskedFor + 1,
             sizeof( struct peer_request ) * --msgs->peerAskedForCount );

    return TRUE;
}

static void
cancelAllRequestsToClient( tr_peermsgs * msgs )
{
    struct peer_request req;
    const int mustSendCancel = tr_peerIoSupportsFEXT( msgs->peer->io );

    while( popNextRequest( msgs, &req ))
        if( mustSendCancel )
            protocolSendReject( msgs, &req );
}

void
tr_peerMsgsSetChoke( tr_peermsgs * msgs,
                     int           choke )
{
    const time_t now = time( NULL );
    const time_t fibrillationTime = now - MIN_CHOKE_PERIOD_SEC;

    assert( msgs );
    assert( msgs->peer );
    assert( choke == 0 || choke == 1 );

    if( msgs->peer->chokeChangedAt > fibrillationTime )
    {
        dbgmsg( msgs, "Not changing choke to %d to avoid fibrillation", choke );
    }
    else if( msgs->peer->peerIsChoked != choke )
    {
        msgs->peer->peerIsChoked = choke;
        if( choke )
            cancelAllRequestsToClient( msgs );
        protocolSendChoke( msgs, choke );
        msgs->peer->chokeChangedAt = now;
    }
}

/**
***
**/

void
tr_peerMsgsHave( tr_peermsgs * msgs,
                 uint32_t      index )
{
    protocolSendHave( msgs, index );

    /* since we have more pieces now, we might not be interested in this peer */
    updateInterest( msgs );
}

/**
***
**/

static tr_bool
reqIsValid( const tr_peermsgs * peer,
            uint32_t            index,
            uint32_t            offset,
            uint32_t            length )
{
    return tr_torrentReqIsValid( peer->torrent, index, offset, length );
}

static tr_bool
requestIsValid( const tr_peermsgs * msgs, const struct peer_request * req )
{
    return reqIsValid( msgs, req->index, req->offset, req->length );
}


void
tr_peerMsgsCancel( tr_peermsgs * msgs, tr_block_index_t block )
{
    struct peer_request req;
    blockToReq( msgs->torrent, block, &req );
    protocolSendCancel( msgs, &req );
}

/**
***
**/

static void
sendLtepHandshake( tr_peermsgs * msgs )
{
    tr_benc val, *m;
    char * buf;
    int len;
    tr_bool allow_pex;
    tr_bool allow_metadata_xfer;
    struct evbuffer * out = msgs->outMessages;
    const unsigned char * ipv6 = tr_globalIPv6();

    if( msgs->clientSentLtepHandshake )
        return;

    dbgmsg( msgs, "sending an ltep handshake" );
    msgs->clientSentLtepHandshake = 1;

    /* decide if we want to advertise metadata xfer support (BEP 9) */
    if( tr_torrentIsPrivate( msgs->torrent ) )
        allow_metadata_xfer = 0;
    else
        allow_metadata_xfer = 1;

    /* decide if we want to advertise pex support */
    if( !tr_torrentAllowsPex( msgs->torrent ) )
        allow_pex = 0;
    else if( msgs->peerSentLtepHandshake )
        allow_pex = msgs->peerSupportsPex ? 1 : 0;
    else
        allow_pex = 1;

    tr_bencInitDict( &val, 8 );
    tr_bencDictAddInt( &val, "e", getSession(msgs)->encryptionMode != TR_CLEAR_PREFERRED );
    if( ipv6 != NULL )
        tr_bencDictAddRaw( &val, "ipv6", ipv6, 16 );
    if( allow_metadata_xfer && tr_torrentHasMetadata( msgs->torrent )
                            && ( msgs->torrent->infoDictLength > 0 ) )
        tr_bencDictAddInt( &val, "metadata_size", msgs->torrent->infoDictLength );
    tr_bencDictAddInt( &val, "p", tr_sessionGetPeerPort( getSession(msgs) ) );
    tr_bencDictAddInt( &val, "reqq", REQQ );
    tr_bencDictAddInt( &val, "upload_only", tr_torrentIsSeed( msgs->torrent ) );
    tr_bencDictAddStr( &val, "v", TR_NAME " " USERAGENT_PREFIX );
    m  = tr_bencDictAddDict( &val, "m", 2 );
    if( allow_metadata_xfer )
        tr_bencDictAddInt( m, "ut_metadata", UT_METADATA_ID );
    if( allow_pex )
        tr_bencDictAddInt( m, "ut_pex", UT_PEX_ID );

    buf = tr_bencToStr( &val, TR_FMT_BENC, &len );

    tr_peerIoWriteUint32( msgs->peer->io, out, 2 * sizeof( uint8_t ) + len );
    tr_peerIoWriteUint8 ( msgs->peer->io, out, BT_LTEP );
    tr_peerIoWriteUint8 ( msgs->peer->io, out, LTEP_HANDSHAKE );
    tr_peerIoWriteBytes ( msgs->peer->io, out, buf, len );
    pokeBatchPeriod( msgs, IMMEDIATE_PRIORITY_INTERVAL_SECS );
    dbgOutMessageLen( msgs );

    /* cleanup */
    tr_bencFree( &val );
    tr_free( buf );
}

static void
parseLtepHandshake( tr_peermsgs *     msgs,
                    int               len,
                    struct evbuffer * inbuf )
{
    int64_t   i;
    tr_benc   val, * sub;
    uint8_t * tmp = tr_new( uint8_t, len );
    const uint8_t *addr;
    size_t addr_len;
    tr_pex pex;

    memset( &pex, 0, sizeof( tr_pex ) );

    tr_peerIoReadBytes( msgs->peer->io, inbuf, tmp, len );
    msgs->peerSentLtepHandshake = 1;

    if( tr_bencLoad( tmp, len, &val, NULL ) || !tr_bencIsDict( &val ) )
    {
        dbgmsg( msgs, "GET  extended-handshake, couldn't get dictionary" );
        tr_free( tmp );
        return;
    }

    dbgmsg( msgs, "here is the handshake: [%*.*s]", len, len,  tmp );

    /* does the peer prefer encrypted connections? */
    if( tr_bencDictFindInt( &val, "e", &i ) ) {
        msgs->peer->encryption_preference = i ? ENCRYPTION_PREFERENCE_YES
                                              : ENCRYPTION_PREFERENCE_NO;
        if( i )
            pex.flags |= ADDED_F_ENCRYPTION_FLAG;
    }

    /* check supported messages for utorrent pex */
    msgs->peerSupportsPex = 0;
    msgs->peerSupportsMetadataXfer = 0;

    if( tr_bencDictFindDict( &val, "m", &sub ) ) {
        if( tr_bencDictFindInt( sub, "ut_pex", &i ) ) {
            msgs->peerSupportsPex = i != 0;
            msgs->ut_pex_id = (uint8_t) i;
            dbgmsg( msgs, "msgs->ut_pex is %d", (int)msgs->ut_pex_id );
        }
        if( tr_bencDictFindInt( sub, "ut_metadata", &i ) ) {
            msgs->peerSupportsMetadataXfer = i != 0;
            msgs->ut_metadata_id = (uint8_t) i;
            dbgmsg( msgs, "msgs->ut_metadata_id is %d", (int)msgs->ut_metadata_id );
        }
    }

    /* look for metainfo size (BEP 9) */
    if( tr_bencDictFindInt( &val, "metadata_size", &i ) )
        tr_torrentSetMetadataSizeHint( msgs->torrent, i );

    /* look for upload_only (BEP 21) */
    if( tr_bencDictFindInt( &val, "upload_only", &i ) ) {
        fireUploadOnly( msgs, i!=0 );
        if( i )
            pex.flags |= ADDED_F_SEED_FLAG;
    }

    /* get peer's listening port */
    if( tr_bencDictFindInt( &val, "p", &i ) ) {
        fireClientGotPort( msgs, (tr_port)i );
        pex.port = htons( (uint16_t)i );
        dbgmsg( msgs, "peer's port is now %d", (int)i );
    }

    if( tr_bencDictFindRaw( &val, "ipv4", &addr, &addr_len) && addr_len == 4 ) {
        pex.addr.type = TR_AF_INET;
        memcpy( &pex.addr.addr.addr4, addr, 4 );
        tr_peerMgrAddPex( msgs->torrent, TR_PEER_FROM_ALT, &pex );
    }

    if( tr_bencDictFindRaw( &val, "ipv6", &addr, &addr_len) && addr_len == 16 ) {
        pex.addr.type = TR_AF_INET6;
        memcpy( &pex.addr.addr.addr6, addr, 16 );
        tr_peerMgrAddPex( msgs->torrent, TR_PEER_FROM_ALT, &pex );
    }

    /* get peer's maximum request queue size */
    if( tr_bencDictFindInt( &val, "reqq", &i ) )
        msgs->reqq = i;

    tr_bencFree( &val );
    tr_free( tmp );
}

static void
parseUtMetadata( tr_peermsgs * msgs, int msglen, struct evbuffer * inbuf )
{
    tr_benc dict;
    char * msg_end;
    char * benc_end;
    int64_t msg_type = -1;
    int64_t piece = -1;
    int64_t total_size = 0;
    uint8_t * tmp = tr_new( uint8_t, msglen );

    tr_peerIoReadBytes( msgs->peer->io, inbuf, tmp, msglen );
    msg_end = (char*)tmp + msglen;

    if( !tr_bencLoad( tmp, msglen, &dict, &benc_end ) )
    {
        tr_bencDictFindInt( &dict, "msg_type", &msg_type );
        tr_bencDictFindInt( &dict, "piece", &piece );
        tr_bencDictFindInt( &dict, "total_size", &total_size );
        tr_bencFree( &dict );
    }

    dbgmsg( msgs, "got ut_metadata msg: type %d, piece %d, total_size %d",
            (int)msg_type, (int)piece, (int)total_size );

    if( msg_type == METADATA_MSG_TYPE_REJECT )
    {
        /* NOOP */
    }

    if( ( msg_type == METADATA_MSG_TYPE_DATA )
        && ( !tr_torrentHasMetadata( msgs->torrent ) )
        && ( msg_end - benc_end <= METADATA_PIECE_SIZE )
        && ( piece * METADATA_PIECE_SIZE + (msg_end - benc_end) <= total_size ) )
    {
        const int pieceLen = msg_end - benc_end;
dbgmsg( msgs, "got a metadata piece... calling tr_torrentSetMetadataPiece" );
        msgs->requestingMetadataFromPeer = FALSE;
        tr_torrentSetMetadataPiece( msgs->torrent, piece, benc_end, pieceLen );
    }

    if( msg_type == METADATA_MSG_TYPE_REQUEST )
    {
        if( ( piece >= 0 )
            && tr_torrentHasMetadata( msgs->torrent )
            && !tr_torrentIsPrivate( msgs->torrent )
            && ( msgs->peerAskedForMetadataCount < METADATA_REQQ ) )
        {
            msgs->peerAskedForMetadata[msgs->peerAskedForMetadataCount++] = piece;
        }
        else
        {
            tr_benc tmp;
            int payloadLen;
            char * payload;
            tr_peerIo  * io  = msgs->peer->io;
            struct evbuffer * out = msgs->outMessages;

            /* build the rejection message */
            tr_bencInitDict( &tmp, 2 );
            tr_bencDictAddInt( &tmp, "msg_type", METADATA_MSG_TYPE_REJECT );
            tr_bencDictAddInt( &tmp, "piece", piece );
            payload = tr_bencToStr( &tmp, TR_FMT_BENC, &payloadLen );
            tr_bencFree( &tmp );

            /* write it out as a LTEP message to our outMessages buffer */
            tr_peerIoWriteUint32( io, out, 2 * sizeof( uint8_t ) + payloadLen );
            tr_peerIoWriteUint8 ( io, out, BT_LTEP );
            tr_peerIoWriteUint8 ( io, out, msgs->ut_metadata_id );
            tr_peerIoWriteBytes ( io, out, payload, payloadLen );
            pokeBatchPeriod( msgs, HIGH_PRIORITY_INTERVAL_SECS );
            dbgOutMessageLen( msgs );

            tr_free( payload );
        }
    }

    tr_free( tmp );
}

static void
parseUtPex( tr_peermsgs * msgs, int msglen, struct evbuffer * inbuf )
{
    int loaded = 0;
    uint8_t * tmp = tr_new( uint8_t, msglen );
    tr_benc val;
    tr_torrent * tor = msgs->torrent;
    const uint8_t * added;
    size_t added_len;

    tr_peerIoReadBytes( msgs->peer->io, inbuf, tmp, msglen );

    if( tr_torrentAllowsPex( tor )
      && ( ( loaded = !tr_bencLoad( tmp, msglen, &val, NULL ) ) ) )
    {
        if( tr_bencDictFindRaw( &val, "added", &added, &added_len ) )
        {
            tr_pex * pex;
            size_t i, n;
            size_t added_f_len = 0;
            const uint8_t * added_f = NULL;

            tr_bencDictFindRaw( &val, "added.f", &added_f, &added_f_len );
            pex = tr_peerMgrCompactToPex( added, added_len, added_f, added_f_len, &n );

            n = MIN( n, MAX_PEX_PEER_COUNT );
            for( i=0; i<n; ++i )
                tr_peerMgrAddPex( tor, TR_PEER_FROM_PEX, pex + i );

            tr_free( pex );
        }

        if( tr_bencDictFindRaw( &val, "added6", &added, &added_len ) )
        {
            tr_pex * pex;
            size_t i, n;
            size_t added_f_len = 0;
            const uint8_t * added_f = NULL;

            tr_bencDictFindRaw( &val, "added6.f", &added_f, &added_f_len );
            pex = tr_peerMgrCompact6ToPex( added, added_len, added_f, added_f_len, &n );

            n = MIN( n, MAX_PEX_PEER_COUNT );
            for( i=0; i<n; ++i )
                tr_peerMgrAddPex( tor, TR_PEER_FROM_PEX, pex + i );

            tr_free( pex );
        }
    }

    if( loaded )
        tr_bencFree( &val );
    tr_free( tmp );
}

static void sendPex( tr_peermsgs * msgs );

static void
parseLtep( tr_peermsgs *     msgs,
           int               msglen,
           struct evbuffer * inbuf )
{
    uint8_t ltep_msgid;

    tr_peerIoReadUint8( msgs->peer->io, inbuf, &ltep_msgid );
    msglen--;

    if( ltep_msgid == LTEP_HANDSHAKE )
    {
        dbgmsg( msgs, "got ltep handshake" );
        parseLtepHandshake( msgs, msglen, inbuf );
        if( tr_peerIoSupportsLTEP( msgs->peer->io ) )
        {
            sendLtepHandshake( msgs );
            sendPex( msgs );
        }
    }
    else if( ltep_msgid == UT_PEX_ID )
    {
        dbgmsg( msgs, "got ut pex" );
        msgs->peerSupportsPex = 1;
        parseUtPex( msgs, msglen, inbuf );
    }
    else if( ltep_msgid == UT_METADATA_ID )
    {
        dbgmsg( msgs, "got ut metadata" );
        msgs->peerSupportsMetadataXfer = 1;
        parseUtMetadata( msgs, msglen, inbuf );
    }
    else
    {
        dbgmsg( msgs, "skipping unknown ltep message (%d)", (int)ltep_msgid );
        evbuffer_drain( inbuf, msglen );
    }
}

static int
readBtLength( tr_peermsgs *     msgs,
              struct evbuffer * inbuf,
              size_t            inlen )
{
    uint32_t len;

    if( inlen < sizeof( len ) )
        return READ_LATER;

    tr_peerIoReadUint32( msgs->peer->io, inbuf, &len );

    if( len == 0 ) /* peer sent us a keepalive message */
        dbgmsg( msgs, "got KeepAlive" );
    else
    {
        msgs->incoming.length = len;
        msgs->state = AWAITING_BT_ID;
    }

    return READ_NOW;
}

static int readBtMessage( tr_peermsgs *     msgs,
                          struct evbuffer * inbuf,
                          size_t            inlen );

static int
readBtId( tr_peermsgs *     msgs,
          struct evbuffer * inbuf,
          size_t            inlen )
{
    uint8_t id;

    if( inlen < sizeof( uint8_t ) )
        return READ_LATER;

    tr_peerIoReadUint8( msgs->peer->io, inbuf, &id );
    msgs->incoming.id = id;
    dbgmsg( msgs, "msgs->incoming.id is now %d; msgs->incoming.length is %zu", id, (size_t)msgs->incoming.length );

    if( id == BT_PIECE )
    {
        msgs->state = AWAITING_BT_PIECE;
        return READ_NOW;
    }
    else if( msgs->incoming.length != 1 )
    {
        msgs->state = AWAITING_BT_MESSAGE;
        return READ_NOW;
    }
    else return readBtMessage( msgs, inbuf, inlen - 1 );
}

static void
updatePeerProgress( tr_peermsgs * msgs )
{
    msgs->peer->progress = tr_bitsetPercent( &msgs->peer->have );
    dbgmsg( msgs, "peer progress is %f", msgs->peer->progress );
    updateFastSet( msgs );
    updateInterest( msgs );
    firePeerProgress( msgs );
}

static void
peerMadeRequest( tr_peermsgs *               msgs,
                 const struct peer_request * req )
{
    const tr_bool fext = tr_peerIoSupportsFEXT( msgs->peer->io );
    const int reqIsValid = requestIsValid( msgs, req );
    const int clientHasPiece = reqIsValid && tr_cpPieceIsComplete( &msgs->torrent->completion, req->index );
    const int peerIsChoked = msgs->peer->peerIsChoked;

    int allow = FALSE;

    if( !reqIsValid )
        dbgmsg( msgs, "rejecting an invalid request." );
    else if( !clientHasPiece )
        dbgmsg( msgs, "rejecting request for a piece we don't have." );
    else if( peerIsChoked )
        dbgmsg( msgs, "rejecting request from choked peer" );
    else if( msgs->peerAskedForCount + 1 >= REQQ )
        dbgmsg( msgs, "rejecting request ... reqq is full" );
    else
        allow = TRUE;

    if( allow )
        msgs->peerAskedFor[msgs->peerAskedForCount++] = *req;
    else if( fext )
        protocolSendReject( msgs, req );
}

static tr_bool
messageLengthIsCorrect( const tr_peermsgs * msg, uint8_t id, uint32_t len )
{
    switch( id )
    {
        case BT_CHOKE:
        case BT_UNCHOKE:
        case BT_INTERESTED:
        case BT_NOT_INTERESTED:
        case BT_FEXT_HAVE_ALL:
        case BT_FEXT_HAVE_NONE:
            return len == 1;

        case BT_HAVE:
        case BT_FEXT_SUGGEST:
        case BT_FEXT_ALLOWED_FAST:
            return len == 5;

        case BT_BITFIELD:
            return len == ( msg->torrent->info.pieceCount + 7u ) / 8u + 1u;

        case BT_REQUEST:
        case BT_CANCEL:
        case BT_FEXT_REJECT:
            return len == 13;

        case BT_PIECE:
            return len > 9 && len <= 16393;

        case BT_PORT:
            return len == 3;

        case BT_LTEP:
            return len >= 2;

        default:
            return FALSE;
    }
}

static int clientGotBlock( tr_peermsgs *               msgs,
                           const uint8_t *             block,
                           const struct peer_request * req );

static int
readBtPiece( tr_peermsgs      * msgs,
             struct evbuffer  * inbuf,
             size_t             inlen,
             size_t           * setme_piece_bytes_read )
{
    struct peer_request * req = &msgs->incoming.blockReq;

    assert( EVBUFFER_LENGTH( inbuf ) >= inlen );
    dbgmsg( msgs, "In readBtPiece" );

    if( !req->length )
    {
        if( inlen < 8 )
            return READ_LATER;

        tr_peerIoReadUint32( msgs->peer->io, inbuf, &req->index );
        tr_peerIoReadUint32( msgs->peer->io, inbuf, &req->offset );
        req->length = msgs->incoming.length - 9;
        dbgmsg( msgs, "got incoming block header %u:%u->%u", req->index, req->offset, req->length );
        return READ_NOW;
    }
    else
    {
        int err;

        /* read in another chunk of data */
        const size_t nLeft = req->length - EVBUFFER_LENGTH( msgs->incoming.block );
        size_t n = MIN( nLeft, inlen );
        size_t i = n;

        while( i > 0 )
        {
            uint8_t buf[MAX_STACK_ARRAY_SIZE];
            const size_t thisPass = MIN( i, sizeof( buf ) );
            tr_peerIoReadBytes( msgs->peer->io, inbuf, buf, thisPass );
            evbuffer_add( msgs->incoming.block, buf, thisPass );
            i -= thisPass;
        }

        fireClientGotData( msgs, n, TRUE );
        *setme_piece_bytes_read += n;
        dbgmsg( msgs, "got %zu bytes for block %u:%u->%u ... %d remain",
               n, req->index, req->offset, req->length,
               (int)( req->length - EVBUFFER_LENGTH( msgs->incoming.block ) ) );
        if( EVBUFFER_LENGTH( msgs->incoming.block ) < req->length )
            return READ_LATER;

        /* we've got the whole block ... process it */
        err = clientGotBlock( msgs, EVBUFFER_DATA( msgs->incoming.block ), req );

        /* cleanup */
        evbuffer_free( msgs->incoming.block );
        msgs->incoming.block = evbuffer_new( );
        req->length = 0;
        msgs->state = AWAITING_BT_LENGTH;
        return err ? READ_ERR : READ_NOW;
    }
}

static void updateDesiredRequestCount( tr_peermsgs * msgs, uint64_t now );

static void
decrementActiveRequestCount( tr_peermsgs * msgs )
{
    if( msgs->activeRequestCount > 0 )
        msgs->activeRequestCount--;
}

static int
readBtMessage( tr_peermsgs * msgs, struct evbuffer * inbuf, size_t inlen )
{
    uint32_t      ui32;
    uint32_t      msglen = msgs->incoming.length;
    const uint8_t id = msgs->incoming.id;
    const size_t  startBufLen = EVBUFFER_LENGTH( inbuf );
    const tr_bool fext = tr_peerIoSupportsFEXT( msgs->peer->io );

    --msglen; /* id length */

    dbgmsg( msgs, "got BT id %d, len %d, buffer size is %zu", (int)id, (int)msglen, inlen );

    if( inlen < msglen )
        return READ_LATER;

    if( !messageLengthIsCorrect( msgs, id, msglen + 1 ) )
    {
        dbgmsg( msgs, "bad packet - BT message #%d with a length of %d", (int)id, (int)msglen );
        fireError( msgs, EMSGSIZE );
        return READ_ERR;
    }

    switch( id )
    {
        case BT_CHOKE:
            dbgmsg( msgs, "got Choke" );
            msgs->peer->clientIsChoked = 1;
            if( !fext )
                fireGotChoke( msgs );
            break;

        case BT_UNCHOKE:
            dbgmsg( msgs, "got Unchoke" );
            msgs->peer->clientIsChoked = 0;
            updateDesiredRequestCount( msgs, tr_date( ) );
            break;

        case BT_INTERESTED:
            dbgmsg( msgs, "got Interested" );
            msgs->peer->peerIsInterested = 1;
            break;

        case BT_NOT_INTERESTED:
            dbgmsg( msgs, "got Not Interested" );
            msgs->peer->peerIsInterested = 0;
            break;

        case BT_HAVE:
            tr_peerIoReadUint32( msgs->peer->io, inbuf, &ui32 );
            dbgmsg( msgs, "got Have: %u", ui32 );
            if( tr_bitsetAdd( &msgs->peer->have, ui32 ) ) {
                fireError( msgs, ERANGE );
                return READ_ERR;
            }
            updatePeerProgress( msgs );
            break;

        case BT_BITFIELD:
            dbgmsg( msgs, "got a bitfield" );
            tr_bitsetReserve( &msgs->peer->have, msglen*8 );
            tr_peerIoReadBytes( msgs->peer->io, inbuf, msgs->peer->have.bitfield.bits, msglen );
            updatePeerProgress( msgs );
            break;

        case BT_REQUEST:
        {
            struct peer_request r;
            tr_peerIoReadUint32( msgs->peer->io, inbuf, &r.index );
            tr_peerIoReadUint32( msgs->peer->io, inbuf, &r.offset );
            tr_peerIoReadUint32( msgs->peer->io, inbuf, &r.length );
            dbgmsg( msgs, "got Request: %u:%u->%u", r.index, r.offset, r.length );
            peerMadeRequest( msgs, &r );
            break;
        }

        case BT_CANCEL:
        {
            int i;
            struct peer_request r;
            tr_peerIoReadUint32( msgs->peer->io, inbuf, &r.index );
            tr_peerIoReadUint32( msgs->peer->io, inbuf, &r.offset );
            tr_peerIoReadUint32( msgs->peer->io, inbuf, &r.length );
            dbgmsg( msgs, "got a Cancel %u:%u->%u", r.index, r.offset, r.length );

            for( i=0; i<msgs->peerAskedForCount; ++i ) {
                const struct peer_request * req = msgs->peerAskedFor + i;
                if( ( req->index == r.index ) && ( req->offset == r.offset ) && ( req->length == r.length ) )
                    break;
            }

            if( i < msgs->peerAskedForCount )
                memmove( msgs->peerAskedFor+i,
                         msgs->peerAskedFor+i+1,
                         sizeof(struct peer_request) *( --msgs->peerAskedForCount-i) );
            break;
        }

        case BT_PIECE:
            assert( 0 ); /* handled elsewhere! */
            break;

        case BT_PORT:
            dbgmsg( msgs, "Got a BT_PORT" );
            tr_peerIoReadUint16( msgs->peer->io, inbuf, &msgs->peer->dht_port );
            if( msgs->peer->dht_port > 0 )
                tr_dhtAddNode( getSession(msgs),
                               tr_peerAddress( msgs->peer ),
                               msgs->peer->dht_port, 0 );
            break;

        case BT_FEXT_SUGGEST:
            dbgmsg( msgs, "Got a BT_FEXT_SUGGEST" );
            tr_peerIoReadUint32( msgs->peer->io, inbuf, &ui32 );
            if( fext )
                fireClientGotSuggest( msgs, ui32 );
            else {
                fireError( msgs, EMSGSIZE );
                return READ_ERR;
            }
            break;

        case BT_FEXT_ALLOWED_FAST:
            dbgmsg( msgs, "Got a BT_FEXT_ALLOWED_FAST" );
            tr_peerIoReadUint32( msgs->peer->io, inbuf, &ui32 );
            if( fext )
                fireClientGotAllowedFast( msgs, ui32 );
            else {
                fireError( msgs, EMSGSIZE );
                return READ_ERR;
            }
            break;

        case BT_FEXT_HAVE_ALL:
            dbgmsg( msgs, "Got a BT_FEXT_HAVE_ALL" );
            if( fext ) {
                tr_bitsetSetHaveAll( &msgs->peer->have );
                updatePeerProgress( msgs );
            } else {
                fireError( msgs, EMSGSIZE );
                return READ_ERR;
            }
            break;

        case BT_FEXT_HAVE_NONE:
            dbgmsg( msgs, "Got a BT_FEXT_HAVE_NONE" );
            if( fext ) {
                tr_bitsetSetHaveNone( &msgs->peer->have );
                updatePeerProgress( msgs );
            } else {
                fireError( msgs, EMSGSIZE );
                return READ_ERR;
            }
            break;

        case BT_FEXT_REJECT:
        {
            struct peer_request r;
            dbgmsg( msgs, "Got a BT_FEXT_REJECT" );
            tr_peerIoReadUint32( msgs->peer->io, inbuf, &r.index );
            tr_peerIoReadUint32( msgs->peer->io, inbuf, &r.offset );
            tr_peerIoReadUint32( msgs->peer->io, inbuf, &r.length );
            if( fext ) {
                decrementActiveRequestCount( msgs );
                fireGotRej( msgs, &r );
            } else {
                fireError( msgs, EMSGSIZE );
                return READ_ERR;
            }
            break;
        }

        case BT_LTEP:
            dbgmsg( msgs, "Got a BT_LTEP" );
            parseLtep( msgs, msglen, inbuf );
            break;

        default:
            dbgmsg( msgs, "peer sent us an UNKNOWN: %d", (int)id );
            tr_peerIoDrain( msgs->peer->io, inbuf, msglen );
            break;
    }

    assert( msglen + 1 == msgs->incoming.length );
    assert( EVBUFFER_LENGTH( inbuf ) == startBufLen - msglen );

    msgs->state = AWAITING_BT_LENGTH;
    return READ_NOW;
}

static TR_INLINE void
decrementDownloadedCount( tr_peermsgs * msgs, uint32_t byteCount )
{
    tr_torrent * tor = msgs->torrent;

    tor->downloadedCur -= MIN( tor->downloadedCur, byteCount );
}

static TR_INLINE void
clientGotUnwantedBlock( tr_peermsgs * msgs, const struct peer_request * req )
{
    decrementDownloadedCount( msgs, req->length );
}

static void
addPeerToBlamefield( tr_peermsgs * msgs, uint32_t index )
{
    if( !msgs->peer->blame )
         msgs->peer->blame = tr_bitfieldNew( msgs->torrent->info.pieceCount );
    tr_bitfieldAdd( msgs->peer->blame, index );
}

/* returns 0 on success, or an errno on failure */
static int
clientGotBlock( tr_peermsgs *               msgs,
                const uint8_t *             data,
                const struct peer_request * req )
{
    int err;
    tr_torrent * tor = msgs->torrent;
    const tr_block_index_t block = _tr_block( tor, req->index, req->offset );

    assert( msgs );
    assert( req );

    if( req->length != tr_torBlockCountBytes( msgs->torrent, block ) ) {
        dbgmsg( msgs, "wrong block size -- expected %u, got %d",
                tr_torBlockCountBytes( msgs->torrent, block ), req->length );
        return EMSGSIZE;
    }

    /* save the block */
    dbgmsg( msgs, "got block %u:%u->%u", req->index, req->offset, req->length );

    if( !tr_peerMgrDidPeerRequest( msgs->torrent, msgs->peer, block ) ) {
        dbgmsg( msgs, "we didn't ask for this message..." );
        return 0;
    }

    /**
    ***  Save the block
    **/

    if(( err = tr_ioWrite( tor, req->index, req->offset, req->length, data )))
        return err;

    addPeerToBlamefield( msgs, req->index );
    decrementActiveRequestCount( msgs );
    fireGotBlock( msgs, req );
    return 0;
}

static int peerPulse( void * vmsgs );

static void
didWrite( tr_peerIo * io UNUSED, size_t bytesWritten, int wasPieceData, void * vmsgs )
{
    tr_peermsgs * msgs = vmsgs;
    firePeerGotData( msgs, bytesWritten, wasPieceData );

    if ( tr_isPeerIo( io ) && io->userData )
        peerPulse( msgs );
}

static ReadState
canRead( tr_peerIo * io, void * vmsgs, size_t * piece )
{
    ReadState         ret;
    tr_peermsgs *     msgs = vmsgs;
    struct evbuffer * in = tr_peerIoGetReadBuffer( io );
    const size_t      inlen = EVBUFFER_LENGTH( in );

    dbgmsg( msgs, "canRead: inlen is %zu, msgs->state is %d", inlen, msgs->state );

    if( !inlen )
    {
        ret = READ_LATER;
    }
    else if( msgs->state == AWAITING_BT_PIECE )
    {
        ret = inlen ? readBtPiece( msgs, in, inlen, piece ) : READ_LATER;
    }
    else switch( msgs->state )
    {
        case AWAITING_BT_LENGTH:
            ret = readBtLength ( msgs, in, inlen ); break;

        case AWAITING_BT_ID:
            ret = readBtId     ( msgs, in, inlen ); break;

        case AWAITING_BT_MESSAGE:
            ret = readBtMessage( msgs, in, inlen ); break;

        default:
            ret = READ_ERR;
            assert( 0 );
    }

    dbgmsg( msgs, "canRead: ret is %d", (int)ret );

    /* log the raw data that was read */
    if( ( ret != READ_ERR ) && ( EVBUFFER_LENGTH( in ) != inlen ) )
        fireClientGotData( msgs, inlen - EVBUFFER_LENGTH( in ), FALSE );

    return ret;
}

/**
***
**/

static void
updateDesiredRequestCount( tr_peermsgs * msgs, uint64_t now )
{
    const tr_torrent * const torrent = msgs->torrent;

    if( tr_torrentIsSeed( msgs->torrent ) )
    {
        msgs->desiredRequestCount = 0;
    }
    else if( msgs->peer->clientIsChoked )
    {
        msgs->desiredRequestCount = 0;
    }
    else
    {
        int irate;
        int estimatedBlocksInPeriod;
        double rate;
        const int floor = 16;
        const int seconds = REQUEST_BUF_SECS;

        /* Get the rate limit we should use.
         * FIXME: this needs to consider all the other peers as well... */
        rate = tr_peerGetPieceSpeed( msgs->peer, now, TR_PEER_TO_CLIENT );
        if( tr_torrentUsesSpeedLimit( torrent, TR_PEER_TO_CLIENT ) )
            rate = MIN( rate, tr_torrentGetSpeedLimit( torrent, TR_PEER_TO_CLIENT ) );

        /* honor the session limits, if enabled */
        if( tr_torrentUsesSessionLimits( torrent ) )
            if( tr_sessionGetActiveSpeedLimit( torrent->session, TR_PEER_TO_CLIENT, &irate ) )
                rate = MIN( rate, irate );

        /* use this desired rate to figure out how
         * many requests we should send to this peer */
        estimatedBlocksInPeriod = ( rate * seconds * 1024 ) / torrent->blockSize;
        msgs->desiredRequestCount = MAX( floor, estimatedBlocksInPeriod );

        /* honor the peer's maximum request count, if specified */
        if( msgs->reqq > 0 )
            if( msgs->desiredRequestCount > msgs->reqq )
                msgs->desiredRequestCount = msgs->reqq;
    }
}

static void
updateMetadataRequests( tr_peermsgs * msgs, time_t now )
{
    int piece;

    if( msgs->peerSupportsMetadataXfer
        && !msgs->requestingMetadataFromPeer
        && tr_torrentGetNextMetadataRequest( msgs->torrent, now, &piece ) )
    {
        tr_benc tmp;
        int payloadLen;
        char * payload;
        tr_peerIo  * io  = msgs->peer->io;
        struct evbuffer * out = msgs->outMessages;

        /* build the data message */
        tr_bencInitDict( &tmp, 3 );
        tr_bencDictAddInt( &tmp, "msg_type", METADATA_MSG_TYPE_REQUEST );
        tr_bencDictAddInt( &tmp, "piece", piece );
        payload = tr_bencToStr( &tmp, TR_FMT_BENC, &payloadLen );
        tr_bencFree( &tmp );

        dbgmsg( msgs, "requesting metadata piece #%d", piece );

        /* write it out as a LTEP message to our outMessages buffer */
        tr_peerIoWriteUint32( io, out, 2 * sizeof( uint8_t ) + payloadLen );
        tr_peerIoWriteUint8 ( io, out, BT_LTEP );
        tr_peerIoWriteUint8 ( io, out, msgs->ut_metadata_id );
        tr_peerIoWriteBytes ( io, out, payload, payloadLen );
        pokeBatchPeriod( msgs, HIGH_PRIORITY_INTERVAL_SECS );
        dbgOutMessageLen( msgs );

        msgs->requestingMetadataFromPeer = TRUE;

        tr_free( payload );
    }
}

static void
updateBlockRequests( tr_peermsgs * msgs )
{
    const int MIN_BATCH_SIZE = 4;
    const int numwant = msgs->desiredRequestCount - msgs->activeRequestCount;

    /* make sure we have enough block requests queued up */
    if( numwant >= MIN_BATCH_SIZE )
    {
        int i;
        int n;
        tr_block_index_t * blocks = tr_new( tr_block_index_t, numwant );

        tr_peerMgrGetNextRequests( msgs->torrent, msgs->peer, numwant, blocks, &n );

        for( i=0; i<n; ++i )
        {
            struct peer_request req;
            blockToReq( msgs->torrent, blocks[i], &req );
            protocolSendRequest( msgs, &req );
        }

        msgs->activeRequestCount += n;

        tr_free( blocks );
    }
}

static void
prefetchPieces( tr_peermsgs *msgs )
{
    int i;
    uint64_t next = 0;

    /* Maintain at least 8 prefetched blocks per unchoked peer, but allow
       up to 4 extra blocks if that would cause sequential writes. */
    for( i=msgs->prefetchCount; i<msgs->peerAskedForCount; ++i )
    {
        const struct peer_request * req = msgs->peerAskedFor + i;
        const uint64_t begin = tr_pieceOffset( msgs->torrent, req->index, req->offset, 0 );
        const uint64_t end = begin + req->length;
        const tr_bool isSequential = next == begin;

        if( ( i >= 12 ) || ( !isSequential && ( i >= 8 ) ) )
            break;

        tr_ioPrefetch( msgs->torrent, req->index, req->offset, req->length );
        ++msgs->prefetchCount;

        next = end;
    }
}

static size_t
fillOutputBuffer( tr_peermsgs * msgs, time_t now )
{
    int piece;
    size_t bytesWritten = 0;
    struct peer_request req;
    const tr_bool haveMessages = EVBUFFER_LENGTH( msgs->outMessages ) != 0;
    const tr_bool fext = tr_peerIoSupportsFEXT( msgs->peer->io );

    /**
    ***  Protocol messages
    **/

    if( haveMessages && !msgs->outMessagesBatchedAt ) /* fresh batch */
    {
        dbgmsg( msgs, "started an outMessages batch (length is %zu)", EVBUFFER_LENGTH( msgs->outMessages ) );
        msgs->outMessagesBatchedAt = now;
    }
    else if( haveMessages && ( ( now - msgs->outMessagesBatchedAt ) >= msgs->outMessagesBatchPeriod ) )
    {
        const size_t len = EVBUFFER_LENGTH( msgs->outMessages );
        /* flush the protocol messages */
        dbgmsg( msgs, "flushing outMessages... to %p (length is %zu)", msgs->peer->io, len );
        tr_peerIoWriteBuf( msgs->peer->io, msgs->outMessages, FALSE );
        msgs->clientSentAnythingAt = now;
        msgs->outMessagesBatchedAt = 0;
        msgs->outMessagesBatchPeriod = LOW_PRIORITY_INTERVAL_SECS;
        bytesWritten +=  len;
    }

    /**
    ***  Metadata Pieces
    **/

    if( ( tr_peerIoGetWriteBufferSpace( msgs->peer->io, now ) >= METADATA_PIECE_SIZE )
        && popNextMetadataRequest( msgs, &piece ) )
    {
        char * data;
        int dataLen;
        tr_bool ok = FALSE;

        data = tr_torrentGetMetadataPiece( msgs->torrent, piece, &dataLen );
        if( ( dataLen > 0 ) && ( data != NULL ) )
        {
            tr_benc tmp;
            int payloadLen;
            char * payload;
            tr_peerIo  * io  = msgs->peer->io;
            struct evbuffer * out = msgs->outMessages;

            /* build the data message */
            tr_bencInitDict( &tmp, 3 );
            tr_bencDictAddInt( &tmp, "msg_type", METADATA_MSG_TYPE_DATA );
            tr_bencDictAddInt( &tmp, "piece", piece );
            tr_bencDictAddInt( &tmp, "total_size", dataLen );
            payload = tr_bencToStr( &tmp, TR_FMT_BENC, &payloadLen );
            tr_bencFree( &tmp );

            /* write it out as a LTEP message to our outMessages buffer */
            tr_peerIoWriteUint32( io, out, 2 * sizeof( uint8_t ) + payloadLen + dataLen );
            tr_peerIoWriteUint8 ( io, out, BT_LTEP );
            tr_peerIoWriteUint8 ( io, out, msgs->ut_metadata_id );
            tr_peerIoWriteBytes ( io, out, payload, payloadLen );
            tr_peerIoWriteBytes ( io, out, data, dataLen );
            pokeBatchPeriod( msgs, HIGH_PRIORITY_INTERVAL_SECS );
            dbgOutMessageLen( msgs );

            tr_free( payload );
            tr_free( data );

            ok = TRUE;
        }

        if( !ok ) /* send a rejection message */
        {
            tr_benc tmp;
            int payloadLen;
            char * payload;
            tr_peerIo  * io  = msgs->peer->io;
            struct evbuffer * out = msgs->outMessages;

            /* build the rejection message */
            tr_bencInitDict( &tmp, 2 );
            tr_bencDictAddInt( &tmp, "msg_type", METADATA_MSG_TYPE_REJECT );
            tr_bencDictAddInt( &tmp, "piece", piece );
            payload = tr_bencToStr( &tmp, TR_FMT_BENC, &payloadLen );
            tr_bencFree( &tmp );

            /* write it out as a LTEP message to our outMessages buffer */
            tr_peerIoWriteUint32( io, out, 2 * sizeof( uint8_t ) + payloadLen );
            tr_peerIoWriteUint8 ( io, out, BT_LTEP );
            tr_peerIoWriteUint8 ( io, out, msgs->ut_metadata_id );
            tr_peerIoWriteBytes ( io, out, payload, payloadLen );
            pokeBatchPeriod( msgs, HIGH_PRIORITY_INTERVAL_SECS );
            dbgOutMessageLen( msgs );

            tr_free( payload );
        }
    }

    /**
    ***  Data Blocks
    **/

    if( ( tr_peerIoGetWriteBufferSpace( msgs->peer->io, now ) >= msgs->torrent->blockSize )
        && popNextRequest( msgs, &req ) )
    {
        --msgs->prefetchCount;

        if( requestIsValid( msgs, &req )
            && tr_cpPieceIsComplete( &msgs->torrent->completion, req.index ) )
        {
            /* FIXME(libevent2) use evbuffer_reserve_space() + evbuffer_commit_space() */
            int err;
            const uint32_t msglen = 4 + 1 + 4 + 4 + req.length;
            struct evbuffer * out;
            tr_peerIo * io = msgs->peer->io;

            out = evbuffer_new( );
            evbuffer_expand( out, msglen );

            tr_peerIoWriteUint32( io, out, sizeof( uint8_t ) + 2 * sizeof( uint32_t ) + req.length );
            tr_peerIoWriteUint8 ( io, out, BT_PIECE );
            tr_peerIoWriteUint32( io, out, req.index );
            tr_peerIoWriteUint32( io, out, req.offset );

            err = tr_ioRead( msgs->torrent, req.index, req.offset, req.length, EVBUFFER_DATA(out)+EVBUFFER_LENGTH(out) );
            if( err )
            {
                if( fext )
                    protocolSendReject( msgs, &req );
            }
            else
            {
                dbgmsg( msgs, "sending block %u:%u->%u", req.index, req.offset, req.length );
                EVBUFFER_LENGTH(out) += req.length;
                assert( EVBUFFER_LENGTH( out ) == msglen );
                tr_peerIoWriteBuf( io, out, TRUE );
                bytesWritten += EVBUFFER_LENGTH( out );
                msgs->clientSentAnythingAt = now;
            }

            evbuffer_free( out );

            if( err )
            {
                bytesWritten = 0;
                msgs = NULL;
            }
        }
        else if( fext ) /* peer needs a reject message */
        {
            protocolSendReject( msgs, &req );
        }

        prefetchPieces( msgs );
    }

    /**
    ***  Keepalive
    **/

    if( ( msgs != NULL )
        && ( msgs->clientSentAnythingAt != 0 )
        && ( ( now - msgs->clientSentAnythingAt ) > KEEPALIVE_INTERVAL_SECS ) )
    {
        dbgmsg( msgs, "sending a keepalive message" );
        tr_peerIoWriteUint32( msgs->peer->io, msgs->outMessages, 0 );
        pokeBatchPeriod( msgs, IMMEDIATE_PRIORITY_INTERVAL_SECS );
    }

    return bytesWritten;
}

static int
peerPulse( void * vmsgs )
{
    tr_peermsgs * msgs = vmsgs;
    const time_t  now = time( NULL );

    if ( tr_isPeerIo( msgs->peer->io ) ) {
        updateDesiredRequestCount( msgs, now );
        updateBlockRequests( msgs );
        updateMetadataRequests( msgs, now );
    }

    for( ;; )
        if( fillOutputBuffer( msgs, now ) < 1 )
            break;

    return TRUE; /* loop forever */
}

void
tr_peerMsgsPulse( tr_peermsgs * msgs )
{
    if( msgs != NULL )
        peerPulse( msgs );
}

static void
gotError( tr_peerIo  * io UNUSED,
          short        what,
          void       * vmsgs )
{
    if( what & EVBUFFER_TIMEOUT )
        dbgmsg( vmsgs, "libevent got a timeout, what=%hd", what );
    if( what & ( EVBUFFER_EOF | EVBUFFER_ERROR ) )
        dbgmsg( vmsgs, "libevent got an error! what=%hd, errno=%d (%s)",
               what, errno, tr_strerror( errno ) );
    fireError( vmsgs, ENOTCONN );
}

static void
sendBitfield( tr_peermsgs * msgs )
{
    struct evbuffer * out = msgs->outMessages;
    tr_bitfield *     field;
    tr_piece_index_t  lazyPieces[LAZY_PIECE_COUNT];
    size_t            i;
    size_t            lazyCount = 0;

    field = tr_bitfieldDup( tr_cpPieceBitfield( &msgs->torrent->completion ) );

    if( tr_sessionIsLazyBitfieldEnabled( getSession( msgs ) ) )
    {
        /** Lazy bitfields aren't a high priority or secure, so I'm opting for
            speed over a truly random sample -- let's limit the pool size to
            the first 1000 pieces so large torrents don't bog things down */
        size_t poolSize;
        const size_t maxPoolSize = MIN( msgs->torrent->info.pieceCount, 1000 );
        tr_piece_index_t * pool = tr_new( tr_piece_index_t, maxPoolSize );

        /* build the pool */
        for( i=poolSize=0; i<maxPoolSize; ++i )
            if( tr_bitfieldHas( field, i ) )
                pool[poolSize++] = i;

        /* pull random piece indices from the pool */
        while( ( poolSize > 0 ) && ( lazyCount < LAZY_PIECE_COUNT ) )
        {
            const int pos = tr_cryptoWeakRandInt( poolSize );
            const tr_piece_index_t piece = pool[pos];
            tr_bitfieldRem( field, piece );
            lazyPieces[lazyCount++] = piece;
            pool[pos] = pool[--poolSize];
        }

        /* cleanup */
        tr_free( pool );
    }

    tr_peerIoWriteUint32( msgs->peer->io, out,
                          sizeof( uint8_t ) + field->byteCount );
    tr_peerIoWriteUint8 ( msgs->peer->io, out, BT_BITFIELD );
    /* FIXME(libevent2): use evbuffer_add_reference() */
    tr_peerIoWriteBytes ( msgs->peer->io, out, field->bits, field->byteCount );
    dbgmsg( msgs, "sending bitfield... outMessage size is now %zu",
            EVBUFFER_LENGTH( out ) );
    pokeBatchPeriod( msgs, IMMEDIATE_PRIORITY_INTERVAL_SECS );

    for( i = 0; i < lazyCount; ++i )
        protocolSendHave( msgs, lazyPieces[i] );

    tr_bitfieldFree( field );
}

static void
tellPeerWhatWeHave( tr_peermsgs * msgs )
{
    const tr_bool fext = tr_peerIoSupportsFEXT( msgs->peer->io );

    if( fext && ( tr_cpGetStatus( &msgs->torrent->completion ) == TR_SEED ) )
    {
        protocolSendHaveAll( msgs );
    }
    else if( fext && ( tr_cpHaveValid( &msgs->torrent->completion ) == 0 ) )
    {
        protocolSendHaveNone( msgs );
    }
    else
    {
        sendBitfield( msgs );
    }
}

/**
***
**/

/* some peers give us error messages if we send
   more than this many peers in a single pex message
   http://wiki.theory.org/BitTorrentPeerExchangeConventions */
#define MAX_PEX_ADDED 50
#define MAX_PEX_DROPPED 50

typedef struct
{
    tr_pex *  added;
    tr_pex *  dropped;
    tr_pex *  elements;
    int       addedCount;
    int       droppedCount;
    int       elementCount;
}
PexDiffs;

static void
pexAddedCb( void * vpex,
            void * userData )
{
    PexDiffs * diffs = userData;
    tr_pex *   pex = vpex;

    if( diffs->addedCount < MAX_PEX_ADDED )
    {
        diffs->added[diffs->addedCount++] = *pex;
        diffs->elements[diffs->elementCount++] = *pex;
    }
}

static TR_INLINE void
pexDroppedCb( void * vpex,
              void * userData )
{
    PexDiffs * diffs = userData;
    tr_pex *   pex = vpex;

    if( diffs->droppedCount < MAX_PEX_DROPPED )
    {
        diffs->dropped[diffs->droppedCount++] = *pex;
    }
}

static TR_INLINE void
pexElementCb( void * vpex,
              void * userData )
{
    PexDiffs * diffs = userData;
    tr_pex * pex = vpex;

    diffs->elements[diffs->elementCount++] = *pex;
}

static void
sendPex( tr_peermsgs * msgs )
{
    if( msgs->peerSupportsPex && tr_torrentAllowsPex( msgs->torrent ) )
    {
        PexDiffs diffs;
        PexDiffs diffs6;
        tr_pex * newPex = NULL;
        tr_pex * newPex6 = NULL;
        const int newCount = tr_peerMgrGetPeers( msgs->torrent, &newPex, TR_AF_INET, TR_PEERS_CONNECTED, MAX_PEX_PEER_COUNT );
        const int newCount6 = tr_peerMgrGetPeers( msgs->torrent, &newPex6, TR_AF_INET6, TR_PEERS_CONNECTED, MAX_PEX_PEER_COUNT );

        /* build the diffs */
        diffs.added = tr_new( tr_pex, newCount );
        diffs.addedCount = 0;
        diffs.dropped = tr_new( tr_pex, msgs->pexCount );
        diffs.droppedCount = 0;
        diffs.elements = tr_new( tr_pex, newCount + msgs->pexCount );
        diffs.elementCount = 0;
        tr_set_compare( msgs->pex, msgs->pexCount,
                        newPex, newCount,
                        tr_pexCompare, sizeof( tr_pex ),
                        pexDroppedCb, pexAddedCb, pexElementCb, &diffs );
        diffs6.added = tr_new( tr_pex, newCount6 );
        diffs6.addedCount = 0;
        diffs6.dropped = tr_new( tr_pex, msgs->pexCount6 );
        diffs6.droppedCount = 0;
        diffs6.elements = tr_new( tr_pex, newCount6 + msgs->pexCount6 );
        diffs6.elementCount = 0;
        tr_set_compare( msgs->pex6, msgs->pexCount6,
                        newPex6, newCount6,
                        tr_pexCompare, sizeof( tr_pex ),
                        pexDroppedCb, pexAddedCb, pexElementCb, &diffs6 );
        dbgmsg(
            msgs,
            "pex: old peer count %d+%d, new peer count %d+%d, "
            "added %d+%d, removed %d+%d",
            msgs->pexCount, msgs->pexCount6, newCount, newCount6,
            diffs.addedCount, diffs6.addedCount,
            diffs.droppedCount, diffs6.droppedCount );

        if( !diffs.addedCount && !diffs.droppedCount && !diffs6.addedCount &&
            !diffs6.droppedCount )
        {
            tr_free( diffs.elements );
            tr_free( diffs6.elements );
        }
        else
        {
            int  i;
            tr_benc val;
            char * benc;
            int bencLen;
            uint8_t * tmp, *walk;
            tr_peerIo       * io  = msgs->peer->io;
            struct evbuffer * out = msgs->outMessages;

            /* update peer */
            tr_free( msgs->pex );
            msgs->pex = diffs.elements;
            msgs->pexCount = diffs.elementCount;
            tr_free( msgs->pex6 );
            msgs->pex6 = diffs6.elements;
            msgs->pexCount6 = diffs6.elementCount;

            /* build the pex payload */
            tr_bencInitDict( &val, 3 ); /* ipv6 support: left as 3:
                                         * speed vs. likelihood? */

            if( diffs.addedCount > 0)
            {
                /* "added" */
                tmp = walk = tr_new( uint8_t, diffs.addedCount * 6 );
                for( i = 0; i < diffs.addedCount; ++i ) {
                    memcpy( walk, &diffs.added[i].addr.addr, 4 ); walk += 4;
                    memcpy( walk, &diffs.added[i].port, 2 ); walk += 2;
                }
                assert( ( walk - tmp ) == diffs.addedCount * 6 );
                tr_bencDictAddRaw( &val, "added", tmp, walk - tmp );
                tr_free( tmp );

                /* "added.f" */
                tmp = walk = tr_new( uint8_t, diffs.addedCount );
                for( i = 0; i < diffs.addedCount; ++i )
                    *walk++ = diffs.added[i].flags;
                assert( ( walk - tmp ) == diffs.addedCount );
                tr_bencDictAddRaw( &val, "added.f", tmp, walk - tmp );
                tr_free( tmp );
            }

            if( diffs.droppedCount > 0 )
            {
                /* "dropped" */
                tmp = walk = tr_new( uint8_t, diffs.droppedCount * 6 );
                for( i = 0; i < diffs.droppedCount; ++i ) {
                    memcpy( walk, &diffs.dropped[i].addr.addr, 4 ); walk += 4;
                    memcpy( walk, &diffs.dropped[i].port, 2 ); walk += 2;
                }
                assert( ( walk - tmp ) == diffs.droppedCount * 6 );
                tr_bencDictAddRaw( &val, "dropped", tmp, walk - tmp );
                tr_free( tmp );
            }

            if( diffs6.addedCount > 0 )
            {
                /* "added6" */
                tmp = walk = tr_new( uint8_t, diffs6.addedCount * 18 );
                for( i = 0; i < diffs6.addedCount; ++i ) {
                    memcpy( walk, &diffs6.added[i].addr.addr.addr6.s6_addr, 16 );
                    walk += 16;
                    memcpy( walk, &diffs6.added[i].port, 2 );
                    walk += 2;
                }
                assert( ( walk - tmp ) == diffs6.addedCount * 18 );
                tr_bencDictAddRaw( &val, "added6", tmp, walk - tmp );
                tr_free( tmp );

                /* "added6.f" */
                tmp = walk = tr_new( uint8_t, diffs6.addedCount );
                for( i = 0; i < diffs6.addedCount; ++i )
                    *walk++ = diffs6.added[i].flags;
                assert( ( walk - tmp ) == diffs6.addedCount );
                tr_bencDictAddRaw( &val, "added6.f", tmp, walk - tmp );
                tr_free( tmp );
            }

            if( diffs6.droppedCount > 0 )
            {
                /* "dropped6" */
                tmp = walk = tr_new( uint8_t, diffs6.droppedCount * 18 );
                for( i = 0; i < diffs6.droppedCount; ++i ) {
                    memcpy( walk, &diffs6.dropped[i].addr.addr.addr6.s6_addr, 16 );
                    walk += 16;
                    memcpy( walk, &diffs6.dropped[i].port, 2 );
                    walk += 2;
                }
                assert( ( walk - tmp ) == diffs6.droppedCount * 18);
                tr_bencDictAddRaw( &val, "dropped6", tmp, walk - tmp );
                tr_free( tmp );
            }

            /* write the pex message */
            benc = tr_bencToStr( &val, TR_FMT_BENC, &bencLen );
            tr_peerIoWriteUint32( io, out, 2 * sizeof( uint8_t ) + bencLen );
            tr_peerIoWriteUint8 ( io, out, BT_LTEP );
            tr_peerIoWriteUint8 ( io, out, msgs->ut_pex_id );
            tr_peerIoWriteBytes ( io, out, benc, bencLen );
            pokeBatchPeriod( msgs, HIGH_PRIORITY_INTERVAL_SECS );
            dbgmsg( msgs, "sending a pex message; outMessage size is now %zu", EVBUFFER_LENGTH( out ) );
            dbgOutMessageLen( msgs );

            tr_free( benc );
            tr_bencFree( &val );
        }

        /* cleanup */
        tr_free( diffs.added );
        tr_free( diffs.dropped );
        tr_free( newPex );
        tr_free( diffs6.added );
        tr_free( diffs6.dropped );
        tr_free( newPex6 );

        /*msgs->clientSentPexAt = time( NULL );*/
    }
}

static void
pexPulse( int foo UNUSED, short bar UNUSED, void * vmsgs )
{
    struct tr_peermsgs * msgs = vmsgs;

    sendPex( msgs );

    tr_timerAdd( &msgs->pexTimer, PEX_INTERVAL_SECS, 0 );
}

/**
***
**/

tr_peermsgs*
tr_peerMsgsNew( struct tr_torrent * torrent,
                struct tr_peer    * peer,
                tr_delivery_func    func,
                void              * userData,
                tr_publisher_tag  * setme )
{
    tr_peermsgs * m;

    assert( peer );
    assert( peer->io );

    m = tr_new0( tr_peermsgs, 1 );
    m->publisher = TR_PUBLISHER_INIT;
    m->peer = peer;
    m->torrent = torrent;
    m->peer->clientIsChoked = 1;
    m->peer->peerIsChoked = 1;
    m->peer->clientIsInterested = 0;
    m->peer->peerIsInterested = 0;
    m->state = AWAITING_BT_LENGTH;
    m->outMessages = evbuffer_new( );
    m->outMessagesBatchedAt = 0;
    m->outMessagesBatchPeriod = LOW_PRIORITY_INTERVAL_SECS;
    m->incoming.block = evbuffer_new( );
    m->peerAskedForCount = 0;
    evtimer_set( &m->pexTimer, pexPulse, m );
    tr_timerAdd( &m->pexTimer, PEX_INTERVAL_SECS, 0 );
    peer->msgs = m;

    *setme = tr_publisherSubscribe( &m->publisher, func, userData );

    if( tr_peerIoSupportsLTEP( peer->io ) )
        sendLtepHandshake( m );

    if(tr_peerIoSupportsDHT(peer->io)) {
        /* Only send PORT over IPv6 when the IPv6 DHT is running (BEP-32). */
        const struct tr_address *addr = tr_peerIoGetAddress( peer->io, NULL );
        if( addr->type == TR_AF_INET || tr_globalIPv6() ) {
            protocolSendPort( m, tr_dhtPort( torrent->session ) );
        }
    }

    tellPeerWhatWeHave( m );

    tr_peerIoSetIOFuncs( m->peer->io, canRead, didWrite, gotError, m );
    updateDesiredRequestCount( m, tr_date( ) );

    return m;
}

void
tr_peerMsgsFree( tr_peermsgs* msgs )
{
    if( msgs )
    {
        evtimer_del( &msgs->pexTimer );
        tr_publisherDestruct( &msgs->publisher );

        evbuffer_free( msgs->incoming.block );
        evbuffer_free( msgs->outMessages );
        tr_free( msgs->pex6 );
        tr_free( msgs->pex );

        memset( msgs, ~0, sizeof( tr_peermsgs ) );
        tr_free( msgs );
    }
}

void
tr_peerMsgsUnsubscribe( tr_peermsgs *    peer,
                        tr_publisher_tag tag )
{
    tr_publisherUnsubscribe( &peer->publisher, tag );
}
