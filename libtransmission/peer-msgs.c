/*
 * This file Copyright (C) 2007-2008 Charles Kerr <charles@transmissionbt.com>
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
#include "session.h"
#include "bencode.h"
#include "completion.h"
#include "crypto.h"
#include "inout.h"
#ifdef WIN32
#include "net.h" /* for ECONN */
#endif
#include "peer-io.h"
#include "peer-mgr.h"
#include "peer-mgr-private.h"
#include "peer-msgs.h"
#include "platform.h" /* MAX_STACK_ARRAY_SIZE */
#include "ratecontrol.h"
#include "stats.h"
#include "torrent.h"
#include "trevent.h"
#include "utils.h"

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

    TR_LTEP_PEX             = 1,



    MIN_CHOKE_PERIOD_SEC    = ( 10 ),

    /* idle seconds before we send a keepalive */
    KEEPALIVE_INTERVAL_SECS = 100,

    PEX_INTERVAL            = ( 90 * 1000 ), /* msec between sendPex() calls */


    MAX_BLOCK_SIZE          = ( 1024 * 16 ),


    /* how long an unsent request can stay queued before it's returned
       back to the peer-mgr's pool of requests */
    QUEUED_REQUEST_TTL_SECS = 20,

    /* how long a sent request can stay queued before it's returned
       back to the peer-mgr's pool of requests */
    SENT_REQUEST_TTL_SECS = 240,

    /* used in lowering the outMessages queue period */
    IMMEDIATE_PRIORITY_INTERVAL_SECS = 0,
    HIGH_PRIORITY_INTERVAL_SECS = 2,
    LOW_PRIORITY_INTERVAL_SECS = 20,

    /* number of pieces to remove from the bitfield when
     * lazy bitfields are turned on */
    LAZY_PIECE_COUNT = 26,

    /* number of pieces we'll allow in our fast set */
    MAX_FAST_SET_SIZE = 3
};

/**
***  REQUEST MANAGEMENT
**/

enum
{
    AWAITING_BT_LENGTH,
    AWAITING_BT_ID,
    AWAITING_BT_MESSAGE,
    AWAITING_BT_PIECE
};

struct peer_request
{
    uint32_t    index;
    uint32_t    offset;
    uint32_t    length;
    time_t      time_requested;
};

static inline tr_bool
requestsMatch( const struct peer_request * a, const struct peer_request * b )
{
    return (a->index==b->index) && (a->offset==b->offset) && (a->length==b->length);
}

struct request_list
{
    uint16_t               count;
    uint16_t               max;
    struct peer_request *  requests;
};

static const struct request_list REQUEST_LIST_INIT = { 0, 0, NULL };

static void
reqListReserve( struct request_list * list,
                uint16_t              max )
{
    if( list->max < max )
    {
        list->max = max;
        list->requests = tr_renew( struct peer_request,
                                   list->requests,
                                   list->max );
    }
}

static void
reqListClear( struct request_list * list )
{
    tr_free( list->requests );
    *list = REQUEST_LIST_INIT;
}

static void
reqListCopy( struct request_list * dest, const struct request_list * src )
{
    dest->count = dest->max = src->count;
    dest->requests = tr_memdup( src->requests, dest->count * sizeof( struct peer_request ) );
}

static void
reqListRemoveOne( struct request_list * list,
                  int                   i )
{
    assert( 0 <= i && i < list->count );

    memmove( &list->requests[i],
             &list->requests[i + 1],
             sizeof( struct peer_request ) * ( --list->count - i ) );
}

static void
reqListAppend( struct request_list *       list,
               const struct peer_request * req )
{
    if( ++list->count >= list->max )
        reqListReserve( list, list->max + 8 );

    list->requests[list->count - 1] = *req;
}

static int
reqListPop( struct request_list * list,
            struct peer_request * setme )
{
    int success;

    if( !list->count )
        success = FALSE;
    else {
        *setme = list->requests[0];
        reqListRemoveOne( list, 0 );
        success = TRUE;
    }

    return success;
}

static int
reqListFind( struct request_list *       list,
             const struct peer_request * key )
{
    uint16_t i;

    for( i = 0; i < list->count; ++i )
        if( requestsMatch( key, list->requests + i ) )
            return i;

    return -1;
}

static int
reqListRemove( struct request_list *       list,
               const struct peer_request * key )
{
    int success;
    const int i = reqListFind( list, key );

    if( i < 0 )
        success = FALSE;
    else {
        reqListRemoveOne( list, i );
        success = TRUE;
    }

    return success;
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
    tr_bool         clientSentLtepHandshake;
    tr_bool         peerSentLtepHandshake;
    tr_bool         haveFastSet;

    uint8_t         state;
    uint8_t         ut_pex_id;
    uint16_t        pexCount;
    uint16_t        pexCount6;
    uint16_t        maxActiveRequests;

    size_t                 fastsetSize;
    tr_piece_index_t       fastset[MAX_FAST_SET_SIZE];

    /* how long the outMessages batch should be allowed to grow before
     * it's flushed -- some messages (like requests >:) should be sent
     * very quickly; others aren't as urgent. */
    int                    outMessagesBatchPeriod;

    tr_peer *              peer;

    tr_session *           session;
    tr_torrent *           torrent;

    tr_publisher           publisher;

    struct evbuffer *      outMessages; /* all the non-piece messages */

    struct request_list    peerAskedFor;
    struct request_list    clientAskedFor;
    struct request_list    clientWillAskFor;

    tr_timer             * pexTimer;
    tr_pex               * pex;
    tr_pex               * pex6;

    time_t                 clientSentPexAt;
    time_t                 clientSentAnythingAt;

    /* when we started batching the outMessages */
    time_t                outMessagesBatchedAt;

    struct tr_incoming    incoming;

    /* if the peer supports the Extension Protocol in BEP 10 and
       supplied a reqq argument, it's stored here.  otherwise the
       value is zero and should be ignored. */
    int64_t               reqq;
};

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
        struct evbuffer * buf = tr_getBuffer( );
        char *            base = tr_basename( file );

        evbuffer_add_printf( buf, "[%s] %s - %s [%s]: ",
                             tr_getLogTimeStr( timestr, sizeof( timestr ) ),
                             msgs->torrent->info.name,
                             tr_peerIoGetAddrStr( msgs->peer->io ),
                             msgs->peer->client );
        va_start( args, fmt );
        evbuffer_add_vprintf( buf, fmt, args );
        va_end( args );
        evbuffer_add_printf( buf, " (%s:%d)\n", base, line );
        fwrite( EVBUFFER_DATA( buf ), 1, EVBUFFER_LENGTH( buf ), fp );

        tr_free( base );
        tr_releaseBuffer( buf );
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

static inline void
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

static const tr_peer_event blankEvent = { 0, 0, 0, 0, 0.0f, 0, 0, 0 };

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
fireNeedReq( tr_peermsgs * msgs )
{
    tr_peer_event e = blankEvent;
    e.eventType = TR_PEER_NEED_REQ;
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

static void
fireCancelledReq( tr_peermsgs * msgs, const struct peer_request * req )
{
    tr_peer_event e = blankEvent;
    e.eventType = TR_PEER_CANCEL;
    e.pieceIndex = req->index;
    e.offset = req->offset;
    e.length = req->length;
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
        uint8_t w[SHA_DIGEST_LENGTH + 4];
        uint8_t x[SHA_DIGEST_LENGTH];

        *(uint32_t*)w = ntohl( htonl( addr->addr.addr4.s_addr ) & 0xffffff00 );   /* (1) */
        memcpy( w + 4, infohash, SHA_DIGEST_LENGTH );                /* (2) */
        tr_sha1( x, w, sizeof( w ), NULL );                          /* (3) */

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

static int
isPieceInteresting( const tr_peermsgs * msgs,
                    tr_piece_index_t    piece )
{
    const tr_torrent * torrent = msgs->torrent;

    return ( !torrent->info.pieces[piece].dnd )                 /* we want it */
          && ( !tr_cpPieceIsComplete( &torrent->completion, piece ) ) /* !have */
          && ( tr_bitfieldHas( msgs->peer->have, piece ) );    /* peer has it */
}

/* "interested" means we'll ask for piece data if they unchoke us */
static int
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

    if( !msgs->peer->have )
        return TRUE;

    assert( bitfield->byteCount == msgs->peer->have->byteCount );

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
    if( i )
        fireNeedReq( msgs );
}

static int
popNextRequest( tr_peermsgs *         msgs,
                struct peer_request * setme )
{
    return reqListPop( &msgs->peerAskedFor, setme );
}

static void
cancelAllRequestsToClient( tr_peermsgs * msgs )
{
    struct peer_request req;
    const int mustSendCancel = tr_peerIoSupportsFEXT( msgs->peer->io );

    while( popNextRequest( msgs, &req ) )
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

static int
reqIsValid( const tr_peermsgs * peer,
            uint32_t            index,
            uint32_t            offset,
            uint32_t            length )
{
    return tr_torrentReqIsValid( peer->torrent, index, offset, length );
}

static int
requestIsValid( const tr_peermsgs * msgs, const struct peer_request * req )
{
    return reqIsValid( msgs, req->index, req->offset, req->length );
}

static void
expireFromList( tr_peermsgs          * msgs,
                struct request_list  * list,
                const time_t           oldestAllowed )
{
    int i;

    /* walk through the list, looking for the first req that's too old */
    for( i=0; i<list->count; ++i ) {
        const struct peer_request * req = &list->requests[i];
        if( req->time_requested < oldestAllowed )
            break;
    }

    /* if we found one too old, start pruning them */
    if( i < list->count ) {
        struct request_list tmp = REQUEST_LIST_INIT;
        reqListCopy( &tmp, list );
        for( ; i<tmp.count; ++i ) {
            const struct peer_request * req = &tmp.requests[i];
            if( req->time_requested < oldestAllowed )
                tr_peerMsgsCancel( msgs, req->index, req->offset, req->length );
        }
        reqListClear( &tmp );
    }
}

static void
expireOldRequests( tr_peermsgs * msgs, const time_t now  )
{
    time_t oldestAllowed;
    const tr_bool fext = tr_peerIoSupportsFEXT( msgs->peer->io );
    dbgmsg( msgs, "entering `expire old requests' block" );

    /* cancel requests that have been queued for too long */
    oldestAllowed = now - QUEUED_REQUEST_TTL_SECS;
    expireFromList( msgs, &msgs->clientWillAskFor, oldestAllowed );

    /* if the peer doesn't support "Reject Request",
     * cancel requests that were sent too long ago. */
    if( !fext ) {
        oldestAllowed = now - SENT_REQUEST_TTL_SECS;
        expireFromList( msgs, &msgs->clientAskedFor, oldestAllowed );
    }

    dbgmsg( msgs, "leaving `expire old requests' block" );
}

static void
pumpRequestQueue( tr_peermsgs * msgs, const time_t now )
{
    const int           max = msgs->maxActiveRequests;
    int                 sent = 0;
    int                 count = msgs->clientAskedFor.count;
    struct peer_request req;

    if( msgs->peer->clientIsChoked )
        return;
    if( !tr_torrentIsPieceTransferAllowed( msgs->torrent, TR_PEER_TO_CLIENT ) )
        return;

    while( ( count < max ) && reqListPop( &msgs->clientWillAskFor, &req ) )
    {
        const tr_block_index_t block = _tr_block( msgs->torrent, req.index, req.offset );

        assert( requestIsValid( msgs, &req ) );
        assert( tr_bitfieldHas( msgs->peer->have, req.index ) );

        /* don't ask for it if we've already got it... this block may have
         * come in from a different peer after we cancelled a request for it */
        if( !tr_cpBlockIsComplete( &msgs->torrent->completion, block ) )
        {
            protocolSendRequest( msgs, &req );
            req.time_requested = now;
            reqListAppend( &msgs->clientAskedFor, &req );

            ++count;
            ++sent;
        }
    }

    if( sent )
        dbgmsg( msgs, "pump sent %d requests, now have %d active and %d queued",
                sent, msgs->clientAskedFor.count, msgs->clientWillAskFor.count );

    if( count < max )
        fireNeedReq( msgs );
}

static inline int
requestQueueIsFull( const tr_peermsgs * msgs )
{
    const int req_max = msgs->maxActiveRequests;
    return msgs->clientWillAskFor.count >= req_max;
}

tr_addreq_t
tr_peerMsgsAddRequest( tr_peermsgs *    msgs,
                       uint32_t         index,
                       uint32_t         offset,
                       uint32_t         length )
{
    struct peer_request req;

    assert( msgs );
    assert( msgs->torrent );
    assert( reqIsValid( msgs, index, offset, length ) );

    /**
    ***  Reasons to decline the request
    **/

    /* don't send requests to choked clients */
    if( msgs->peer->clientIsChoked ) {
        dbgmsg( msgs, "declining request because they're choking us" );
        return TR_ADDREQ_CLIENT_CHOKED;
    }

    /* peer doesn't have this piece */
    if( !tr_bitfieldHas( msgs->peer->have, index ) )
        return TR_ADDREQ_MISSING;

    /* peer's queue is full */
    if( requestQueueIsFull( msgs ) ) {
        dbgmsg( msgs, "declining request because we're full" );
        return TR_ADDREQ_FULL;
    }

    /* have we already asked for this piece? */
    req.index = index;
    req.offset = offset;
    req.length = length;
    if( reqListFind( &msgs->clientAskedFor, &req ) != -1 ) {
        dbgmsg( msgs, "declining because it's a duplicate" );
        return TR_ADDREQ_DUPLICATE;
    }
    if( reqListFind( &msgs->clientWillAskFor, &req ) != -1 ) {
        dbgmsg( msgs, "declining because it's a duplicate" );
        return TR_ADDREQ_DUPLICATE;
    }

    /**
    ***  Accept this request
    **/

    dbgmsg( msgs, "adding req for %"PRIu32":%"PRIu32"->%"PRIu32" to our `will request' list",
            index, offset, length );
    req.time_requested = time( NULL );
    reqListAppend( &msgs->clientWillAskFor, &req );
    return TR_ADDREQ_OK;
}

static void
cancelAllRequestsToPeer( tr_peermsgs * msgs, tr_bool sendCancel )
{
    int i;
    struct request_list a = msgs->clientWillAskFor;
    struct request_list b = msgs->clientAskedFor;
    dbgmsg( msgs, "cancelling all requests to peer" );

    msgs->clientAskedFor = REQUEST_LIST_INIT;
    msgs->clientWillAskFor = REQUEST_LIST_INIT;

    for( i=0; i<a.count; ++i )
        fireCancelledReq( msgs, &a.requests[i] );

    for( i = 0; i < b.count; ++i ) {
        fireCancelledReq( msgs, &b.requests[i] );
        if( sendCancel )
            protocolSendCancel( msgs, &b.requests[i] );
    }

    reqListClear( &a );
    reqListClear( &b );
}

void
tr_peerMsgsCancel( tr_peermsgs * msgs,
                   uint32_t      pieceIndex,
                   uint32_t      offset,
                   uint32_t      length )
{
    struct peer_request req;

    assert( msgs != NULL );
    assert( length > 0 );


    /* have we asked the peer for this piece? */
    req.index = pieceIndex;
    req.offset = offset;
    req.length = length;

    /* if it's only in the queue and hasn't been sent yet, free it */
    if( reqListRemove( &msgs->clientWillAskFor, &req ) ) {
        dbgmsg( msgs, "cancelling %"PRIu32":%"PRIu32"->%"PRIu32, pieceIndex, offset, length );
        fireCancelledReq( msgs, &req );
    }

    /* if it's already been sent, send a cancel message too */
    if( reqListRemove( &msgs->clientAskedFor, &req ) ) {
        dbgmsg( msgs, "cancelling %"PRIu32":%"PRIu32"->%"PRIu32, pieceIndex, offset, length );
        protocolSendCancel( msgs, &req );
        fireCancelledReq( msgs, &req );
    }
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
    int pex;
    struct evbuffer * out = msgs->outMessages;

    if( msgs->clientSentLtepHandshake )
        return;

    dbgmsg( msgs, "sending an ltep handshake" );
    msgs->clientSentLtepHandshake = 1;

    /* decide if we want to advertise pex support */
    if( !tr_torrentAllowsPex( msgs->torrent ) )
        pex = 0;
    else if( msgs->peerSentLtepHandshake )
        pex = msgs->peerSupportsPex ? 1 : 0;
    else
        pex = 1;

    tr_bencInitDict( &val, 5 );
    tr_bencDictAddInt( &val, "e", msgs->session->encryptionMode != TR_CLEAR_PREFERRED );
    tr_bencDictAddInt( &val, "p", tr_sessionGetPeerPort( msgs->session ) );
    tr_bencDictAddInt( &val, "upload_only", tr_torrentIsSeed( msgs->torrent ) );
    tr_bencDictAddStr( &val, "v", TR_NAME " " USERAGENT_PREFIX );
    m  = tr_bencDictAddDict( &val, "m", 1 );
    if( pex )
        tr_bencDictAddInt( m, "ut_pex", TR_LTEP_PEX );
    buf = tr_bencSave( &val, &len );

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
    if( tr_bencDictFindInt( &val, "e", &i ) )
        msgs->peer->encryption_preference = i ? ENCRYPTION_PREFERENCE_YES
                                              : ENCRYPTION_PREFERENCE_NO;

    /* check supported messages for utorrent pex */
    msgs->peerSupportsPex = 0;
    if( tr_bencDictFindDict( &val, "m", &sub ) ) {
        if( tr_bencDictFindInt( sub, "ut_pex", &i ) ) {
            msgs->ut_pex_id = (uint8_t) i;
            msgs->peerSupportsPex = msgs->ut_pex_id == 0 ? 0 : 1;
            dbgmsg( msgs, "msgs->ut_pex is %d", (int)msgs->ut_pex_id );
        }
    }

    /* look for upload_only (BEP 21) */
    if( tr_bencDictFindInt( &val, "upload_only", &i ) )
        fireUploadOnly( msgs, i!=0 );

    /* get peer's listening port */
    if( tr_bencDictFindInt( &val, "p", &i ) ) {
        msgs->peer->port = htons( (uint16_t)i );
        dbgmsg( msgs, "msgs->port is now %hu", msgs->peer->port );
    }

    /* get peer's maximum request queue size */
    if( tr_bencDictFindInt( &val, "reqq", &i ) )
        msgs->reqq = i;

    tr_bencFree( &val );
    tr_free( tmp );
}

static void
parseUtPex( tr_peermsgs * msgs, int msglen, struct evbuffer * inbuf )
{
    int loaded = 0;
    uint8_t * tmp = tr_new( uint8_t, msglen );
    tr_benc val;
    const tr_torrent * tor = msgs->torrent;
    const uint8_t * added;
    size_t added_len;

    tr_peerIoReadBytes( msgs->peer->io, inbuf, tmp, msglen );

    if( tr_torrentAllowsPex( tor )
      && ( ( loaded = !tr_bencLoad( tmp, msglen, &val, NULL ) ) ) )
    {
        if( tr_bencDictFindRaw( &val, "added", &added, &added_len ) )
        {
            const uint8_t * added_f = NULL;
            tr_pex *        pex;
            size_t          i, n;
            size_t          added_f_len = 0;
            tr_bencDictFindRaw( &val, "added.f", &added_f, &added_f_len );
            pex =
                tr_peerMgrCompactToPex( added, added_len, added_f, added_f_len,
                                        &n );
            for( i = 0; i < n; ++i )
                tr_peerMgrAddPex( msgs->session->peerMgr, tor->info.hash,
                                  TR_PEER_FROM_PEX, pex + i );
            tr_free( pex );
        }
        
        if( tr_bencDictFindRaw( &val, "added6", &added, &added_len ) )
        {
            const uint8_t * added_f = NULL;
            tr_pex *        pex;
            size_t          i, n;
            size_t          added_f_len = 0;
            tr_bencDictFindRaw( &val, "added6.f", &added_f, &added_f_len );
            pex =
                tr_peerMgrCompact6ToPex( added, added_len, added_f, added_f_len,
                                         &n );
            for( i = 0; i < n; ++i )
                tr_peerMgrAddPex( msgs->session->peerMgr, tor->info.hash,
                                  TR_PEER_FROM_PEX, pex + i );
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
    else if( ltep_msgid == TR_LTEP_PEX )
    {
        dbgmsg( msgs, "got ut pex" );
        msgs->peerSupportsPex = 1;
        parseUtPex( msgs, msglen, inbuf );
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
    msgs->peer->progress = tr_bitfieldCountTrueBits( msgs->peer->have ) / (float)msgs->torrent->info.pieceCount;
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
    else
        allow = TRUE;

    if( allow )
        reqListAppend( &msgs->peerAskedFor, req );
    else if( fext )
        protocolSendReject( msgs, req );
}

static int
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
        evbuffer_drain( msgs->incoming.block, EVBUFFER_LENGTH( msgs->incoming.block ) );
        req->length = 0;
        msgs->state = AWAITING_BT_LENGTH;
        if( !err )
            return READ_NOW;
        else {
            fireError( msgs, err );
            return READ_ERR;
        }
    }
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

    if( inlen < msglen )
        return READ_LATER;

    dbgmsg( msgs, "got BT id %d, len %d, buffer size is %zu", (int)id, (int)msglen, inlen );

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
                cancelAllRequestsToPeer( msgs, FALSE );
            break;

        case BT_UNCHOKE:
            dbgmsg( msgs, "got Unchoke" );
            msgs->peer->clientIsChoked = 0;
            fireNeedReq( msgs );
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
            if( tr_bitfieldAdd( msgs->peer->have, ui32 ) ) {
                fireError( msgs, ERANGE );
                return READ_ERR;
            }
            updatePeerProgress( msgs );
            tr_rcTransferred( &msgs->torrent->swarmSpeed,
                              msgs->torrent->info.pieceSize );
            break;

        case BT_BITFIELD:
        {
            dbgmsg( msgs, "got a bitfield" );
            tr_peerIoReadBytes( msgs->peer->io, inbuf, msgs->peer->have->bits, msglen );
            updatePeerProgress( msgs );
            fireNeedReq( msgs );
            break;
        }

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
            struct peer_request r;
            tr_peerIoReadUint32( msgs->peer->io, inbuf, &r.index );
            tr_peerIoReadUint32( msgs->peer->io, inbuf, &r.offset );
            tr_peerIoReadUint32( msgs->peer->io, inbuf, &r.length );
            dbgmsg( msgs, "got a Cancel %u:%u->%u", r.index, r.offset, r.length );
            if( reqListRemove( &msgs->peerAskedFor, &r ) && fext )
                protocolSendReject( msgs, &r );
            break;
        }

        case BT_PIECE:
            assert( 0 ); /* handled elsewhere! */
            break;

        case BT_PORT:
            dbgmsg( msgs, "Got a BT_PORT" );
            tr_peerIoReadUint16( msgs->peer->io, inbuf, &msgs->peer->port );
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
                tr_bitfieldAddRange( msgs->peer->have, 0, msgs->torrent->info.pieceCount );
                updatePeerProgress( msgs );
            } else {
                fireError( msgs, EMSGSIZE );
                return READ_ERR;
            }
            break;

        case BT_FEXT_HAVE_NONE:
            dbgmsg( msgs, "Got a BT_FEXT_HAVE_NONE" );
            if( fext ) {
                tr_bitfieldClear( msgs->peer->have );
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
            if( fext )
                reqListRemove( &msgs->clientAskedFor, &r );
            else {
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

static inline void
decrementDownloadedCount( tr_peermsgs * msgs, uint32_t byteCount )
{
    tr_torrent * tor = msgs->torrent;

    tor->downloadedCur -= MIN( tor->downloadedCur, byteCount );
}

static inline void
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

    /**
    *** Remove the block from our `we asked for this' list
    **/

    if( !reqListRemove( &msgs->clientAskedFor, req ) ) {
        clientGotUnwantedBlock( msgs, req );
        dbgmsg( msgs, "we didn't ask for this message..." );
        return 0;
    }

    dbgmsg( msgs, "peer has %d more blocks we've asked for",
            msgs->clientAskedFor.count );

    /**
    *** Error checks
    **/

    if( tr_cpBlockIsComplete( &tor->completion, block ) ) {
        dbgmsg( msgs, "we have this block already..." );
        clientGotUnwantedBlock( msgs, req );
        return 0;
    }

    /**
    ***  Save the block
    **/

    if(( err = tr_ioWrite( tor, req->index, req->offset, req->length, data )))
        return err;

    addPeerToBlamefield( msgs, req->index );
    fireGotBlock( msgs, req );
    return 0;
}

static int peerPulse( void * vmsgs );

static void
didWrite( tr_peerIo * io UNUSED, size_t bytesWritten, int wasPieceData, void * vmsgs )
{
    tr_peermsgs * msgs = vmsgs;
    firePeerGotData( msgs, bytesWritten, wasPieceData );
    peerPulse( msgs );
}

static ReadState
canRead( tr_peerIo * io, void * vmsgs, size_t * piece )
{
    ReadState         ret;
    tr_peermsgs *     msgs = vmsgs;
    struct evbuffer * in = tr_peerIoGetReadBuffer( io );
    const size_t      inlen = EVBUFFER_LENGTH( in );

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
            assert( 0 );
    }

    /* log the raw data that was read */
    if( ( ret != READ_ERR ) && ( EVBUFFER_LENGTH( in ) != inlen ) )
        fireClientGotData( msgs, inlen - EVBUFFER_LENGTH( in ), FALSE );

    return ret;
}

/**
***
**/

static int
ratePulse( void * vmsgs )
{
    tr_peermsgs * msgs = vmsgs;
    const double rateToClient = tr_peerGetPieceSpeed( msgs->peer, TR_PEER_TO_CLIENT );
    const int seconds = 10;
    const int floor = 8;
    const int estimatedBlocksInPeriod = ( rateToClient * seconds * 1024 ) / msgs->torrent->blockSize;

    msgs->maxActiveRequests = floor + estimatedBlocksInPeriod;

    if( msgs->reqq > 0 )
        msgs->maxActiveRequests = MIN( msgs->maxActiveRequests, msgs->reqq );

    return TRUE;
}

static size_t
fillOutputBuffer( tr_peermsgs * msgs, time_t now )
{
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
    ***  Blocks
    **/

    if( ( tr_peerIoGetWriteBufferSpace( msgs->peer->io ) >= msgs->torrent->blockSize )
        && popNextRequest( msgs, &req ) )
    {
        if( requestIsValid( msgs, &req )
            && tr_cpPieceIsComplete( &msgs->torrent->completion, req.index ) )
        {
            int err;
            static uint8_t * buf = NULL;

            if( buf == NULL )
                buf = tr_new( uint8_t, MAX_BLOCK_SIZE );

            /* send a block */
            if(( err = tr_ioRead( msgs->torrent, req.index, req.offset, req.length, buf ))) {
                fireError( msgs, err );
                bytesWritten = 0;
                msgs = NULL;
            } else {
                tr_peerIo * io = msgs->peer->io;
                struct evbuffer * out = tr_getBuffer( );
                dbgmsg( msgs, "sending block %u:%u->%u", req.index, req.offset, req.length );
                tr_peerIoWriteUint32( io, out, sizeof( uint8_t ) + 2 * sizeof( uint32_t ) + req.length );
                tr_peerIoWriteUint8 ( io, out, BT_PIECE );
                tr_peerIoWriteUint32( io, out, req.index );
                tr_peerIoWriteUint32( io, out, req.offset );
                tr_peerIoWriteBytes ( io, out, buf, req.length );
                tr_peerIoWriteBuf( io, out, TRUE );
                bytesWritten += EVBUFFER_LENGTH( out );
                msgs->clientSentAnythingAt = now;
                tr_releaseBuffer( out );
            }
        }
        else if( fext ) /* peer needs a reject message */
        {
            protocolSendReject( msgs, &req );
        }
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

    ratePulse( msgs );

    pumpRequestQueue( msgs, now );
    expireOldRequests( msgs, now );

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

    if( tr_sessionIsLazyBitfieldEnabled( msgs->session ) )
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

static inline void
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

static inline void
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
        const int newCount = tr_peerMgrGetPeers( msgs->session->peerMgr,
                                                 msgs->torrent->info.hash,
                                                 &newPex, TR_AF_INET );
        const int newCount6 = tr_peerMgrGetPeers( msgs->session->peerMgr,
                                                  msgs->torrent->info.hash,
                                                  &newPex6, TR_AF_INET6 );

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
            "pex: old peer count %d, new peer count %d, added %d, removed %d",
            msgs->pexCount, newCount + newCount6,
            diffs.addedCount + diffs6.addedCount,
            diffs.droppedCount + diffs6.droppedCount );

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

            /* "added" */
            tmp = walk = tr_new( uint8_t, diffs.addedCount * 6 );
            for( i = 0; i < diffs.addedCount; ++i )
            {
                tr_suspectAddress( &diffs.added[i].addr, "pex" );
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

            /* "dropped" */
            tmp = walk = tr_new( uint8_t, diffs.droppedCount * 6 );
            for( i = 0; i < diffs.droppedCount; ++i )
            {
                memcpy( walk, &diffs.dropped[i].addr.addr, 4 ); walk += 4;
                memcpy( walk, &diffs.dropped[i].port, 2 ); walk += 2;
            }
            assert( ( walk - tmp ) == diffs.droppedCount * 6 );
            tr_bencDictAddRaw( &val, "dropped", tmp, walk - tmp );
            tr_free( tmp );
            
            /* "added6" */
            tmp = walk = tr_new( uint8_t, diffs6.addedCount * 18 );
            for( i = 0; i < diffs6.addedCount; ++i )
            {
                tr_suspectAddress( &diffs6.added[i].addr, "pex6" );
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
            
            /* "dropped6" */
            tmp = walk = tr_new( uint8_t, diffs6.droppedCount * 18 );
            for( i = 0; i < diffs6.droppedCount; ++i )
            {
                memcpy( walk, &diffs6.dropped[i].addr.addr.addr6.s6_addr, 16 );
                walk += 16;
                memcpy( walk, &diffs6.dropped[i].port, 2 );
                walk += 2;
            }
            assert( ( walk - tmp ) == diffs6.droppedCount * 18);
            tr_bencDictAddRaw( &val, "dropped6", tmp, walk - tmp );
            tr_free( tmp );

            /* write the pex message */
            benc = tr_bencSave( &val, &bencLen );
            tr_peerIoWriteUint32( msgs->peer->io, out, 2 * sizeof( uint8_t ) + bencLen );
            tr_peerIoWriteUint8 ( msgs->peer->io, out, BT_LTEP );
            tr_peerIoWriteUint8 ( msgs->peer->io, out, msgs->ut_pex_id );
            tr_peerIoWriteBytes ( msgs->peer->io, out, benc, bencLen );
            pokeBatchPeriod( msgs, HIGH_PRIORITY_INTERVAL_SECS );
            dbgmsg( msgs, "sending a pex message; outMessage size is now %zu", EVBUFFER_LENGTH( out ) );

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

        msgs->clientSentPexAt = time( NULL );
    }
}

static inline int
pexPulse( void * vpeer )
{
    sendPex( vpeer );
    return TRUE;
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
    m->session = torrent->session;
    m->torrent = torrent;
    m->peer->clientIsChoked = 1;
    m->peer->peerIsChoked = 1;
    m->peer->clientIsInterested = 0;
    m->peer->peerIsInterested = 0;
    m->peer->have = tr_bitfieldNew( torrent->info.pieceCount );
    m->state = AWAITING_BT_LENGTH;
    m->pexTimer = tr_timerNew( m->session, pexPulse, m, PEX_INTERVAL );
    m->outMessages = evbuffer_new( );
    m->outMessagesBatchedAt = 0;
    m->outMessagesBatchPeriod = LOW_PRIORITY_INTERVAL_SECS;
    m->incoming.block = evbuffer_new( );
    m->peerAskedFor = REQUEST_LIST_INIT;
    m->clientAskedFor = REQUEST_LIST_INIT;
    m->clientWillAskFor = REQUEST_LIST_INIT;
    peer->msgs = m;

    *setme = tr_publisherSubscribe( &m->publisher, func, userData );

    if( tr_peerIoSupportsLTEP( peer->io ) )
        sendLtepHandshake( m );

    tellPeerWhatWeHave( m );

    tr_peerIoSetIOFuncs( m->peer->io, canRead, didWrite, gotError, m );
    ratePulse( m );

    return m;
}

void
tr_peerMsgsFree( tr_peermsgs* msgs )
{
    if( msgs )
    {
        tr_timerFree( &msgs->pexTimer );
        tr_publisherDestruct( &msgs->publisher );
        reqListClear( &msgs->clientWillAskFor );
        reqListClear( &msgs->clientAskedFor );
        reqListClear( &msgs->peerAskedFor );

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

