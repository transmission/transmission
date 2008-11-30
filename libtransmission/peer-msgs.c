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
#include "iobuf.h"
#ifdef WIN32
#include "net.h" /* for ECONN */
#endif
#include "peer-io.h"
#include "peer-mgr.h"
#include "peer-mgr-private.h"
#include "peer-msgs.h"
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
    BT_SUGGEST              = 13,
    BT_HAVE_ALL             = 14,
    BT_HAVE_NONE            = 15,
    BT_REJECT               = 16,
    BT_LTEP                 = 20,

    LTEP_HANDSHAKE          = 0,

    TR_LTEP_PEX             = 1,

    MIN_CHOKE_PERIOD_SEC    = ( 10 ),

    /* idle seconds before we send a keepalive */
    KEEPALIVE_INTERVAL_SECS = 100,

    PEX_INTERVAL            = ( 90 * 1000 ), /* msec between sendPex() calls */
    PEER_PULSE_INTERVAL     = ( 250 ),       /* msec between peerPulse() calls
                                               */

    MAX_QUEUE_SIZE          = ( 100 ),

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
    LAZY_PIECE_COUNT = 26
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

static int
compareRequest( const void * va,
                const void * vb )
{
    const struct peer_request * a = va;
    const struct peer_request * b = vb;

    if( a->index != b->index )
        return a->index < b->index ? -1 : 1;

    if( a->offset != b->offset )
        return a->offset < b->offset ? -1 : 1;

    if( a->length != b->length )
        return a->length < b->length ? -1 : 1;

    return 0;
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
reqListCopy( struct request_list *       dest,
             const struct request_list * src )
{
    dest->count = dest->max = src->count;
    dest->requests =
        tr_memdup( src->requests, dest->count * sizeof( struct peer_request ) );
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
        if( !compareRequest( key, list->requests + i ) )
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

struct tr_peermsgs
{
    tr_bool         peerSentBitfield;
    tr_bool         peerSupportsPex;
    tr_bool         clientSentLtepHandshake;
    tr_bool         peerSentLtepHandshake;

    uint8_t         state;
    uint8_t         ut_pex_id;
    uint16_t        pexCount;
    uint16_t        minActiveRequests;
    uint16_t        maxActiveRequests;

    /* how long the outMessages batch should be allowed to grow before
     * it's flushed -- some messages (like requests >:) should be sent
     * very quickly; others aren't as urgent. */
    int                    outMessagesBatchPeriod;

    tr_peer *              info;

    tr_session *           session;
    tr_torrent *           torrent;
    tr_peerIo *            io;

    tr_publisher_t *       publisher;

    struct evbuffer *      outMessages; /* all the non-piece messages */

    struct request_list    peerAskedFor;
    struct request_list    clientAskedFor;
    struct request_list    clientWillAskFor;

    tr_timer *             pexTimer;

    time_t                 clientSentPexAt;
    time_t                 clientSentAnythingAt;

    /* when we started batching the outMessages */
    time_t                outMessagesBatchedAt;

    tr_bitfield *         peerAllowedPieces;

    struct tr_incoming    incoming;

    tr_pex *              pex;
};

/**
***
**/

static void
myDebug( const char *               file,
         int                        line,
         const struct tr_peermsgs * msgs,
         const char *               fmt,
         ... )
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
                             msgs->torrent->info.name,
                             tr_peerIoGetAddrStr( msgs->io ),
                             msgs->info->client );
        va_start( args, fmt );
        evbuffer_add_vprintf( buf, fmt, args );
        va_end( args );
        evbuffer_add_printf( buf, " (%s:%d)\n", base, line );
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

static void
protocolSendRequest( tr_peermsgs *               msgs,
                     const struct peer_request * req )
{
    tr_peerIo *       io = msgs->io;
    struct evbuffer * out = msgs->outMessages;

    tr_peerIoWriteUint32( io, out, sizeof( uint8_t ) + 3 * sizeof( uint32_t ) );
    tr_peerIoWriteUint8 ( io, out, BT_REQUEST );
    tr_peerIoWriteUint32( io, out, req->index );
    tr_peerIoWriteUint32( io, out, req->offset );
    tr_peerIoWriteUint32( io, out, req->length );
    dbgmsg( msgs, "requesting %u:%u->%u... outMessage size is now %d",
           req->index, req->offset, req->length, (int)EVBUFFER_LENGTH( out ) );
    pokeBatchPeriod( msgs, HIGH_PRIORITY_INTERVAL_SECS );
}

static void
protocolSendCancel( tr_peermsgs *               msgs,
                    const struct peer_request * req )
{
    tr_peerIo *       io = msgs->io;
    struct evbuffer * out = msgs->outMessages;

    tr_peerIoWriteUint32( io, out, sizeof( uint8_t ) + 3 * sizeof( uint32_t ) );
    tr_peerIoWriteUint8 ( io, out, BT_CANCEL );
    tr_peerIoWriteUint32( io, out, req->index );
    tr_peerIoWriteUint32( io, out, req->offset );
    tr_peerIoWriteUint32( io, out, req->length );
    dbgmsg( msgs, "cancelling %u:%u->%u... outMessage size is now %d",
           req->index, req->offset, req->length, (int)EVBUFFER_LENGTH( out ) );
    pokeBatchPeriod( msgs, IMMEDIATE_PRIORITY_INTERVAL_SECS );
}

static void
protocolSendHave( tr_peermsgs * msgs,
                  uint32_t      index )
{
    tr_peerIo *       io = msgs->io;
    struct evbuffer * out = msgs->outMessages;

    tr_peerIoWriteUint32( io, out, sizeof( uint8_t ) + sizeof( uint32_t ) );
    tr_peerIoWriteUint8 ( io, out, BT_HAVE );
    tr_peerIoWriteUint32( io, out, index );
    dbgmsg( msgs, "sending Have %u.. outMessage size is now %d",
           index, (int)EVBUFFER_LENGTH( out ) );
    pokeBatchPeriod( msgs, LOW_PRIORITY_INTERVAL_SECS );
}

static void
protocolSendChoke( tr_peermsgs * msgs,
                   int           choke )
{
    tr_peerIo *       io = msgs->io;
    struct evbuffer * out = msgs->outMessages;

    tr_peerIoWriteUint32( io, out, sizeof( uint8_t ) );
    tr_peerIoWriteUint8 ( io, out, choke ? BT_CHOKE : BT_UNCHOKE );
    dbgmsg( msgs, "sending %s... outMessage size is now %d",
           ( choke ? "Choke" : "Unchoke" ),
           (int)EVBUFFER_LENGTH( out ) );
    pokeBatchPeriod( msgs, IMMEDIATE_PRIORITY_INTERVAL_SECS );
}

/**
***  EVENTS
**/

static const tr_peer_event blankEvent = { 0, 0, 0, 0, 0.0f, 0, 0 };

static void
publish( tr_peermsgs *   msgs,
         tr_peer_event * e )
{
    tr_publisherPublish( msgs->publisher, msgs->info, e );
}

static void
fireError( tr_peermsgs * msgs,
           int           err )
{
    tr_peer_event e = blankEvent;

    e.eventType = TR_PEER_ERROR;
    e.err = err;
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
    e.progress = msgs->info->progress;
    publish( msgs, &e );
}

static void
fireGotBlock( tr_peermsgs *               msgs,
              const struct peer_request * req )
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
***  INTEREST
**/

static int
isPieceInteresting( const tr_peermsgs * peer,
                    tr_piece_index_t    piece )
{
    const tr_torrent * torrent = peer->torrent;

    return ( !torrent->info.pieces[piece].dnd )                 /* we want it */
           && ( !tr_cpPieceIsComplete( torrent->completion, piece ) ) /* !have
                                                                        */
           && ( tr_bitfieldHas( peer->info->have, piece ) );   /* peer has it */
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
    bitfield = tr_cpPieceBitfield( torrent->completion );

    if( !msgs->info->have )
        return TRUE;

    assert( bitfield->byteCount == msgs->info->have->byteCount );

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

    msgs->info->clientIsInterested = weAreInterested;
    dbgmsg( msgs, "Sending %s",
            weAreInterested ? "Interested" : "Not Interested" );
    tr_peerIoWriteUint32( msgs->io, out, sizeof( uint8_t ) );
    tr_peerIoWriteUint8 (
        msgs->io, out, weAreInterested ? BT_INTERESTED : BT_NOT_INTERESTED );
    pokeBatchPeriod( msgs, HIGH_PRIORITY_INTERVAL_SECS );
    dbgmsg( msgs, "outMessage size is now %d", (int)EVBUFFER_LENGTH( out ) );
}

static void
updateInterest( tr_peermsgs * msgs )
{
    const int i = isPeerInteresting( msgs );

    if( i != msgs->info->clientIsInterested )
        sendInterest( msgs, i );
    if( i )
        fireNeedReq( msgs );
}

static void
cancelAllRequestsToClient( tr_peermsgs * msgs )
{
    reqListClear( &msgs->peerAskedFor );
}

void
tr_peerMsgsSetChoke( tr_peermsgs * msgs,
                     int           choke )
{
    const time_t now = time( NULL );
    const time_t fibrillationTime = now - MIN_CHOKE_PERIOD_SEC;

    assert( msgs );
    assert( msgs->info );
    assert( choke == 0 || choke == 1 );

    if( msgs->info->chokeChangedAt > fibrillationTime )
    {
        dbgmsg( msgs, "Not changing choke to %d to avoid fibrillation",
                choke );
    }
    else if( msgs->info->peerIsChoked != choke )
    {
        msgs->info->peerIsChoked = choke;
        if( choke )
            cancelAllRequestsToClient( msgs );
        protocolSendChoke( msgs, choke );
        msgs->info->chokeChangedAt = now;
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
expireOldRequests( tr_peermsgs * msgs, const time_t now  )
{
    int                 i;
    time_t              oldestAllowed;
    struct request_list tmp = REQUEST_LIST_INIT;

    /* cancel requests that have been queued for too long */
    oldestAllowed = now - QUEUED_REQUEST_TTL_SECS;
    reqListCopy( &tmp, &msgs->clientWillAskFor );
    for( i = 0; i < tmp.count; ++i )
    {
        const struct peer_request * req = &tmp.requests[i];
        if( req->time_requested < oldestAllowed )
            tr_peerMsgsCancel( msgs, req->index, req->offset, req->length );
    }
    reqListClear( &tmp );

    /* cancel requests that were sent too long ago */
    oldestAllowed = now - SENT_REQUEST_TTL_SECS;
    reqListCopy( &tmp, &msgs->clientAskedFor );
    for( i = 0; i < tmp.count; ++i )
    {
        const struct peer_request * req = &tmp.requests[i];
        if( req->time_requested < oldestAllowed )
            tr_peerMsgsCancel( msgs, req->index, req->offset, req->length );
    }
    reqListClear( &tmp );
}

static void
pumpRequestQueue( tr_peermsgs * msgs, const time_t now )
{
    const int           max = msgs->maxActiveRequests;
    const int           min = msgs->minActiveRequests;
    int                 sent = 0;
    int                 count = msgs->clientAskedFor.count;
    struct peer_request req;

    if( count > min )
        return;
    if( msgs->info->clientIsChoked )
        return;
    if( !tr_torrentIsPieceTransferAllowed( msgs->torrent, TR_PEER_TO_CLIENT ) )
        return;

    while( ( count < max ) && reqListPop( &msgs->clientWillAskFor, &req ) )
    {
        const tr_block_index_t block =
            _tr_block( msgs->torrent, req.index, req.offset );

        assert( requestIsValid( msgs, &req ) );
        assert( tr_bitfieldHas( msgs->info->have, req.index ) );

        /* don't ask for it if we've already got it... this block may have
         * come in from a different peer after we cancelled a request for it */
        if( !tr_cpBlockIsComplete( msgs->torrent->completion, block ) )
        {
            protocolSendRequest( msgs, &req );
            req.time_requested = now;
            reqListAppend( &msgs->clientAskedFor, &req );

            ++count;
            ++sent;
        }
    }

    if( sent )
        dbgmsg( msgs,
                "pump sent %d requests, now have %d active and %d queued",
                sent,
                msgs->clientAskedFor.count,
                msgs->clientWillAskFor.count );

    if( count < max )
        fireNeedReq( msgs );
}

static int
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
    if( msgs->info->clientIsChoked )
    {
        dbgmsg( msgs, "declining request because they're choking us" );
        return TR_ADDREQ_CLIENT_CHOKED;
    }

    /* peer doesn't have this piece */
    if( !tr_bitfieldHas( msgs->info->have, index ) )
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
cancelAllRequestsToPeer( tr_peermsgs * msgs )
{
    int                 i;
    struct request_list a = msgs->clientWillAskFor;
    struct request_list b = msgs->clientAskedFor;
    dbgmsg( msgs, "cancelling all requests to peer" );

    msgs->clientAskedFor = REQUEST_LIST_INIT;
    msgs->clientWillAskFor = REQUEST_LIST_INIT;

    for( i=0; i<a.count; ++i )
        fireCancelledReq( msgs, &a.requests[i] );

    for( i = 0; i < b.count; ++i ) {
        fireCancelledReq( msgs, &b.requests[i] );
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
        dbgmsg( msgs, "cancelling %"PRIu32":%"PRIu32"->%"PRIu32"\n", pieceIndex, offset, length );
        fireCancelledReq( msgs, &req );
    }

    /* if it's already been sent, send a cancel message too */
    if( reqListRemove( &msgs->clientAskedFor, &req ) ) {
        dbgmsg( msgs, "cancelling %"PRIu32":%"PRIu32"->%"PRIu32"\n", pieceIndex, offset, length );
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
    tr_benc           val, *m;
    char *            buf;
    int               len;
    int               pex;
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

    tr_bencInitDict( &val, 4 );
    tr_bencDictAddInt( &val, "e",
                       msgs->session->encryptionMode != TR_CLEAR_PREFERRED );
    tr_bencDictAddInt( &val, "p", tr_sessionGetPeerPort( msgs->session ) );
    tr_bencDictAddStr( &val, "v", TR_NAME " " USERAGENT_PREFIX );
    m  = tr_bencDictAddDict( &val, "m", 1 );
    if( pex )
        tr_bencDictAddInt( m, "ut_pex", TR_LTEP_PEX );
    buf = tr_bencSave( &val, &len );

    tr_peerIoWriteUint32( msgs->io, out, 2 * sizeof( uint8_t ) + len );
    tr_peerIoWriteUint8 ( msgs->io, out, BT_LTEP );
    tr_peerIoWriteUint8 ( msgs->io, out, LTEP_HANDSHAKE );
    tr_peerIoWriteBytes ( msgs->io, out, buf, len );
    pokeBatchPeriod( msgs, IMMEDIATE_PRIORITY_INTERVAL_SECS );
    dbgmsg( msgs, "outMessage size is now %d", (int)EVBUFFER_LENGTH( out ) );

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

    tr_peerIoReadBytes( msgs->io, inbuf, tmp, len );
    msgs->peerSentLtepHandshake = 1;

    if( tr_bencLoad( tmp, len, &val, NULL ) || val.type != TYPE_DICT )
    {
        dbgmsg( msgs, "GET  extended-handshake, couldn't get dictionary" );
        tr_free( tmp );
        return;
    }

    dbgmsg( msgs, "here is the handshake: [%*.*s]", len, len,  tmp );

    /* does the peer prefer encrypted connections? */
    if( tr_bencDictFindInt( &val, "e", &i ) )
        msgs->info->encryption_preference = i ? ENCRYPTION_PREFERENCE_YES
                                            : ENCRYPTION_PREFERENCE_NO;

    /* check supported messages for utorrent pex */
    msgs->peerSupportsPex = 0;
    if( tr_bencDictFindDict( &val, "m", &sub ) )
    {
        if( tr_bencDictFindInt( sub, "ut_pex", &i ) )
        {
            msgs->ut_pex_id = (uint8_t) i;
            msgs->peerSupportsPex = msgs->ut_pex_id == 0 ? 0 : 1;
            dbgmsg( msgs, "msgs->ut_pex is %d", (int)msgs->ut_pex_id );
        }
    }

    /* get peer's listening port */
    if( tr_bencDictFindInt( &val, "p", &i ) )
    {
        msgs->info->port = htons( (uint16_t)i );
        dbgmsg( msgs, "msgs->port is now %hu", msgs->info->port );
    }

    tr_bencFree( &val );
    tr_free( tmp );
}

static void
parseUtPex( tr_peermsgs *     msgs,
            int               msglen,
            struct evbuffer * inbuf )
{
    int                loaded = 0;
    uint8_t *          tmp = tr_new( uint8_t, msglen );
    tr_benc            val;
    const tr_torrent * tor = msgs->torrent;
    const uint8_t *    added;
    size_t             added_len;

    tr_peerIoReadBytes( msgs->io, inbuf, tmp, msglen );

    if( tr_torrentAllowsPex( tor )
      && ( ( loaded = !tr_bencLoad( tmp, msglen, &val, NULL ) ) )
      && tr_bencDictFindRaw( &val, "added", &added, &added_len ) )
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

    tr_peerIoReadUint8( msgs->io, inbuf, &ltep_msgid );
    msglen--;

    if( ltep_msgid == LTEP_HANDSHAKE )
    {
        dbgmsg( msgs, "got ltep handshake" );
        parseLtepHandshake( msgs, msglen, inbuf );
        if( tr_peerIoSupportsLTEP( msgs->io ) )
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

    tr_peerIoReadUint32( msgs->io, inbuf, &len );

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

    tr_peerIoReadUint8( msgs->io, inbuf, &id );
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
    msgs->info->progress = tr_bitfieldCountTrueBits( msgs->info->have )
                           / (float)msgs->torrent->info.pieceCount;
    dbgmsg( msgs, "peer progress is %f", msgs->info->progress );
    updateInterest( msgs );
    firePeerProgress( msgs );
}

static void
peerMadeRequest( tr_peermsgs *               msgs,
                 const struct peer_request * req )
{
    const int reqIsValid = requestIsValid( msgs, req );
    const int clientHasPiece = reqIsValid && tr_cpPieceIsComplete(
        msgs->torrent->completion, req->index );
    const int peerIsChoked = msgs->info->peerIsChoked;

    if( !reqIsValid ) /* bad request */
    {
        dbgmsg( msgs, "rejecting an invalid request." );
    }
    else if( !clientHasPiece ) /* we don't have it */
    {
        dbgmsg( msgs, "rejecting request for a piece we don't have." );
    }
    else if( peerIsChoked ) /* doesn't he know he's choked? */
    {
        tr_peerMsgsSetChoke( msgs, 1 );
    }
    else /* YAY */
    {
        reqListAppend( &msgs->peerAskedFor, req );
    }
}

static int
messageLengthIsCorrect( const tr_peermsgs * msg,
                        uint8_t             id,
                        uint32_t            len )
{
    switch( id )
    {
        case BT_CHOKE:
        case BT_UNCHOKE:
        case BT_INTERESTED:
        case BT_NOT_INTERESTED:
        case BT_HAVE_ALL:
        case BT_HAVE_NONE:
            return len == 1;

        case BT_HAVE:
        case BT_SUGGEST:

        case BT_BITFIELD:
            return len == ( msg->torrent->info.pieceCount + 7u ) / 8u + 1u;

        case BT_REQUEST:
        case BT_CANCEL:
        case BT_REJECT:
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

        tr_peerIoReadUint32( msgs->io, inbuf, &req->index );
        tr_peerIoReadUint32( msgs->io, inbuf, &req->offset );
        req->length = msgs->incoming.length - 9;
        dbgmsg( msgs, "got incoming block header %u:%u->%u", req->index,
                req->offset,
                req->length );
        return READ_NOW;
    }
    else
    {
        int          err;

        /* read in another chunk of data */
        const size_t nLeft = req->length - EVBUFFER_LENGTH(
            msgs->incoming.block );
        size_t       n = MIN( nLeft, inlen );
        uint8_t *    buf = tr_new( uint8_t, n );
        assert( EVBUFFER_LENGTH( inbuf ) >= n );
        tr_peerIoReadBytes( msgs->io, inbuf, buf, n );
        evbuffer_add( msgs->incoming.block, buf, n );
        fireClientGotData( msgs, n, TRUE );
        *setme_piece_bytes_read += n;
        tr_free( buf );
        dbgmsg( msgs, "got %d bytes for block %u:%u->%u ... %d remain",
               (int)n, req->index, req->offset, req->length,
               (int)( req->length - EVBUFFER_LENGTH( msgs->incoming.block ) ) );
        if( EVBUFFER_LENGTH( msgs->incoming.block ) < req->length )
            return READ_LATER;

        /* we've got the whole block ... process it */
        err = clientGotBlock( msgs, EVBUFFER_DATA(
                                  msgs->incoming.block ), req );

        /* cleanup */
        evbuffer_drain( msgs->incoming.block,
                       EVBUFFER_LENGTH( msgs->incoming.block ) );
        req->length = 0;
        msgs->state = AWAITING_BT_LENGTH;
        if( !err )
            return READ_NOW;
        else
        {
            fireError( msgs, err );
            return READ_ERR;
        }
    }
}

static int
readBtMessage( tr_peermsgs *     msgs,
               struct evbuffer * inbuf,
               size_t            inlen )
{
    uint32_t      ui32;
    uint32_t      msglen = msgs->incoming.length;
    const uint8_t id = msgs->incoming.id;
    const size_t  startBufLen = EVBUFFER_LENGTH( inbuf );

    --msglen; /* id length */

    if( inlen < msglen )
        return READ_LATER;

    dbgmsg( msgs, "got BT id %d, len %d, buffer size is %d", (int)id,
            (int)msglen,
            (int)inlen );

    if( !messageLengthIsCorrect( msgs, id, msglen + 1 ) )
    {
        dbgmsg( msgs, "bad packet - BT message #%d with a length of %d",
                (int)id, (int)msglen );
        fireError( msgs, EMSGSIZE );
        return READ_ERR;
    }

    switch( id )
    {
        case BT_CHOKE:
            dbgmsg( msgs, "got Choke" );
            msgs->info->clientIsChoked = 1;
            cancelAllRequestsToPeer( msgs );
            cancelAllRequestsToClient( msgs );
            break;

        case BT_UNCHOKE:
            dbgmsg( msgs, "got Unchoke" );
            msgs->info->clientIsChoked = 0;
            fireNeedReq( msgs );
            break;

        case BT_INTERESTED:
            dbgmsg( msgs, "got Interested" );
            msgs->info->peerIsInterested = 1;
            break;

        case BT_NOT_INTERESTED:
            dbgmsg( msgs, "got Not Interested" );
            msgs->info->peerIsInterested = 0;
            break;

        case BT_HAVE:
            tr_peerIoReadUint32( msgs->io, inbuf, &ui32 );
            dbgmsg( msgs, "got Have: %u", ui32 );
            if( tr_bitfieldAdd( msgs->info->have, ui32 ) )
                fireError( msgs, ERANGE );
            updatePeerProgress( msgs );
            tr_rcTransferred( msgs->torrent->swarmSpeed,
                              msgs->torrent->info.pieceSize );
            break;

        case BT_BITFIELD:
        {
            dbgmsg( msgs, "got a bitfield" );
            msgs->peerSentBitfield = 1;
            tr_peerIoReadBytes( msgs->io, inbuf, msgs->info->have->bits,
                                msglen );
            updatePeerProgress( msgs );
            fireNeedReq( msgs );
            break;
        }

        case BT_REQUEST:
        {
            struct peer_request r;
            tr_peerIoReadUint32( msgs->io, inbuf, &r.index );
            tr_peerIoReadUint32( msgs->io, inbuf, &r.offset );
            tr_peerIoReadUint32( msgs->io, inbuf, &r.length );
            dbgmsg( msgs, "got Request: %u:%u->%u", r.index, r.offset,
                    r.length );
            peerMadeRequest( msgs, &r );
            break;
        }

        case BT_CANCEL:
        {
            struct peer_request r;
            tr_peerIoReadUint32( msgs->io, inbuf, &r.index );
            tr_peerIoReadUint32( msgs->io, inbuf, &r.offset );
            tr_peerIoReadUint32( msgs->io, inbuf, &r.length );
            dbgmsg( msgs, "got a Cancel %u:%u->%u", r.index, r.offset,
                    r.length );
            reqListRemove( &msgs->peerAskedFor, &r );
            break;
        }

        case BT_PIECE:
            assert( 0 ); /* handled elsewhere! */
            break;

        case BT_PORT:
            dbgmsg( msgs, "Got a BT_PORT" );
            tr_peerIoReadUint16( msgs->io, inbuf, &msgs->info->port );
            break;

        case BT_SUGGEST:
        {
            dbgmsg( msgs, "Got a BT_SUGGEST" );
            tr_peerIoReadUint32( msgs->io, inbuf, &ui32 );
            /* we don't do anything with this yet */
            break;
        }

        case BT_HAVE_ALL:
            dbgmsg( msgs, "Got a BT_HAVE_ALL" );
            tr_bitfieldAddRange( msgs->info->have, 0,
                                 msgs->torrent->info.pieceCount );
            updatePeerProgress( msgs );
            break;


        case BT_HAVE_NONE:
            dbgmsg( msgs, "Got a BT_HAVE_NONE" );
            tr_bitfieldClear( msgs->info->have );
            updatePeerProgress( msgs );
            break;

        case BT_REJECT:
        {
            struct peer_request r;
            dbgmsg( msgs, "Got a BT_REJECT" );
            tr_peerIoReadUint32( msgs->io, inbuf, &r.index );
            tr_peerIoReadUint32( msgs->io, inbuf, &r.offset );
            tr_peerIoReadUint32( msgs->io, inbuf, &r.length );
            reqListRemove( &msgs->clientAskedFor, &r );
            break;
        }

        case BT_LTEP:
            dbgmsg( msgs, "Got a BT_LTEP" );
            parseLtep( msgs, msglen, inbuf );
            break;

        default:
            dbgmsg( msgs, "peer sent us an UNKNOWN: %d", (int)id );
            tr_peerIoDrain( msgs->io, inbuf, msglen );
            break;
    }

    assert( msglen + 1 == msgs->incoming.length );
    assert( EVBUFFER_LENGTH( inbuf ) == startBufLen - msglen );

    msgs->state = AWAITING_BT_LENGTH;
    return READ_NOW;
}

static void
decrementDownloadedCount( tr_peermsgs * msgs,
                          uint32_t      byteCount )
{
    tr_torrent * tor = msgs->torrent;

    tor->downloadedCur -= MIN( tor->downloadedCur, byteCount );
}

static void
clientGotUnwantedBlock( tr_peermsgs *               msgs,
                        const struct peer_request * req )
{
    decrementDownloadedCount( msgs, req->length );
}

static void
addPeerToBlamefield( tr_peermsgs * msgs,
                     uint32_t      index )
{
    if( !msgs->info->blame )
        msgs->info->blame = tr_bitfieldNew( msgs->torrent->info.pieceCount );
    tr_bitfieldAdd( msgs->info->blame, index );
}

/* returns 0 on success, or an errno on failure */
static int
clientGotBlock( tr_peermsgs *               msgs,
                const uint8_t *             data,
                const struct peer_request * req )
{
    int                    err;
    tr_torrent *           tor = msgs->torrent;
    const tr_block_index_t block = _tr_block( tor, req->index, req->offset );

    assert( msgs );
    assert( req );

    if( req->length != tr_torBlockCountBytes( msgs->torrent, block ) )
    {
        dbgmsg( msgs, "wrong block size -- expected %u, got %d",
                tr_torBlockCountBytes( msgs->torrent, block ), req->length );
        return EMSGSIZE;
    }

    /* save the block */
    dbgmsg( msgs, "got block %u:%u->%u", req->index, req->offset,
            req->length );

    /**
    *** Remove the block from our `we asked for this' list
    **/

    if( !reqListRemove( &msgs->clientAskedFor, req ) )
    {
        clientGotUnwantedBlock( msgs, req );
        dbgmsg( msgs, "we didn't ask for this message..." );
        return 0;
    }

    dbgmsg( msgs, "peer has %d more blocks we've asked for",
            msgs->clientAskedFor.count );

    /**
    *** Error checks
    **/

    if( tr_cpBlockIsComplete( tor->completion, block ) )
    {
        dbgmsg( msgs, "we have this block already..." );
        clientGotUnwantedBlock( msgs, req );
        return 0;
    }

    /**
    ***  Save the block
    **/

    msgs->info->peerSentPieceDataAt = time( NULL );
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
canRead( struct tr_iobuf * iobuf, void * vmsgs, size_t * piece )
{
    ReadState         ret;
    tr_peermsgs *     msgs = vmsgs;
    struct evbuffer * in = tr_iobuf_input( iobuf );
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
    if( EVBUFFER_LENGTH( in ) != inlen )
        fireClientGotData( msgs, inlen - EVBUFFER_LENGTH( in ), FALSE );

    return ret;
}

/**
***
**/

static int
ratePulse( void * vpeer )
{
    tr_peermsgs * peer = vpeer;
    const double rateToClient = tr_peerGetPieceSpeed( peer->info,
                                                      TR_PEER_TO_CLIENT );
    const int estimatedBlocksInNext30Seconds =
                  ( rateToClient * 30 * 1024 ) / peer->torrent->blockSize;

    peer->minActiveRequests = 8;
    peer->maxActiveRequests = peer->minActiveRequests + estimatedBlocksInNext30Seconds;
    return TRUE;
}

static int
popNextRequest( tr_peermsgs *         msgs,
                struct peer_request * setme )
{
    return reqListPop( &msgs->peerAskedFor, setme );
}

static size_t
fillOutputBuffer( tr_peermsgs * msgs, time_t now )
{
    size_t bytesWritten = 0;
    struct peer_request req;
    const int haveMessages = EVBUFFER_LENGTH( msgs->outMessages ) != 0;

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
        dbgmsg( msgs, "flushing outMessages... to %p (length is %zu)", msgs->io, len );
        tr_peerIoWriteBuf( msgs->io, msgs->outMessages, FALSE );
        msgs->clientSentAnythingAt = now;
        msgs->outMessagesBatchedAt = 0;
        msgs->outMessagesBatchPeriod = LOW_PRIORITY_INTERVAL_SECS;
        bytesWritten +=  len;
    }

    /**
    ***  Blocks
    **/

    if( ( tr_peerIoGetWriteBufferSpace( msgs->io ) >= msgs->torrent->blockSize )
        && popNextRequest( msgs, &req )
        && requestIsValid( msgs, &req )
        && tr_cpPieceIsComplete( msgs->torrent->completion, req.index ) )
    {
        /* send a block */
        uint8_t * buf = tr_new( uint8_t, req.length );
        const int err = tr_ioRead( msgs->torrent, req.index, req.offset, req.length, buf );
        if( err ) {
            fireError( msgs, err );
        } else {
            tr_peerIo * io = msgs->io;
            struct evbuffer * out = evbuffer_new( );
            dbgmsg( msgs, "sending block %u:%u->%u", req.index, req.offset, req.length );
            tr_peerIoWriteUint32( io, out, sizeof( uint8_t ) + 2 * sizeof( uint32_t ) + req.length );
            tr_peerIoWriteUint8 ( io, out, BT_PIECE );
            tr_peerIoWriteUint32( io, out, req.index );
            tr_peerIoWriteUint32( io, out, req.offset );
            tr_peerIoWriteBytes ( io, out, buf, req.length );
            tr_peerIoWriteBuf( io, out, TRUE );
            bytesWritten += EVBUFFER_LENGTH( out );
            evbuffer_free( out );
            msgs->clientSentAnythingAt = now;
        }
        tr_free( buf );
    }

    /**
    ***  Keepalive
    **/

    if( msgs->clientSentAnythingAt
        && ( ( now - msgs->clientSentAnythingAt ) > KEEPALIVE_INTERVAL_SECS ) )
    {
        dbgmsg( msgs, "sending a keepalive message" );
        tr_peerIoWriteUint32( msgs->io, msgs->outMessages, 0 );
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
gotError( struct tr_iobuf  * iobuf UNUSED,
          short              what,
          void             * vmsgs )
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

    field = tr_bitfieldDup( tr_cpPieceBitfield( msgs->torrent->completion ) );

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

    tr_peerIoWriteUint32( msgs->io, out,
                          sizeof( uint8_t ) + field->byteCount );
    tr_peerIoWriteUint8 ( msgs->io, out, BT_BITFIELD );
    tr_peerIoWriteBytes ( msgs->io, out, field->bits, field->byteCount );
    dbgmsg( msgs, "sending bitfield... outMessage size is now %d",
           (int)EVBUFFER_LENGTH( out ) );
    pokeBatchPeriod( msgs, IMMEDIATE_PRIORITY_INTERVAL_SECS );

    for( i = 0; i < lazyCount; ++i )
        protocolSendHave( msgs, lazyPieces[i] );

    tr_bitfieldFree( field );
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

static void
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

static void
pexElementCb( void * vpex,
              void * userData )
{
    PexDiffs * diffs = userData;
    tr_pex *   pex = vpex;

    diffs->elements[diffs->elementCount++] = *pex;
}

/* TODO: ipv6 pex */
static void
sendPex( tr_peermsgs * msgs )
{
    if( msgs->peerSupportsPex && tr_torrentAllowsPex( msgs->torrent ) )
    {
        PexDiffs diffs;
        tr_pex * newPex = NULL;
        const int newCount = tr_peerMgrGetPeers( msgs->session->peerMgr,
                                                 msgs->torrent->info.hash,
                                                 &newPex );

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
        dbgmsg(
            msgs,
            "pex: old peer count %d, new peer count %d, added %d, removed %d",
            msgs->pexCount, newCount, diffs.addedCount, diffs.droppedCount );

        if( diffs.addedCount || diffs.droppedCount )
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

            /* build the pex payload */
            tr_bencInitDict( &val, 3 );

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

            /* "dropped" */
            tmp = walk = tr_new( uint8_t, diffs.droppedCount * 6 );
            for( i = 0; i < diffs.droppedCount; ++i ) {
                memcpy( walk, &diffs.dropped[i].addr.addr, 4 ); walk += 4;
                memcpy( walk, &diffs.dropped[i].port, 2 ); walk += 2;
            }
            assert( ( walk - tmp ) == diffs.droppedCount * 6 );
            tr_bencDictAddRaw( &val, "dropped", tmp, walk - tmp );
            tr_free( tmp );

            /* write the pex message */
            benc = tr_bencSave( &val, &bencLen );
            tr_peerIoWriteUint32( msgs->io, out, 2 * sizeof( uint8_t ) + bencLen );
            tr_peerIoWriteUint8 ( msgs->io, out, BT_LTEP );
            tr_peerIoWriteUint8 ( msgs->io, out, msgs->ut_pex_id );
            tr_peerIoWriteBytes ( msgs->io, out, benc, bencLen );
            pokeBatchPeriod( msgs, HIGH_PRIORITY_INTERVAL_SECS );
            dbgmsg( msgs, "sending a pex message; outMessage size is now %zu", EVBUFFER_LENGTH( out ) );

            tr_free( benc );
            tr_bencFree( &val );
        }

        /* cleanup */
        tr_free( diffs.added );
        tr_free( diffs.dropped );
        tr_free( newPex );

        msgs->clientSentPexAt = time( NULL );
    }
}

static int
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
                struct tr_peer *    info,
                tr_delivery_func    func,
                void *              userData,
                tr_publisher_tag *  setme )
{
    tr_peermsgs * m;

    assert( info );
    assert( info->io );

    m = tr_new0( tr_peermsgs, 1 );
    m->publisher = tr_publisherNew( );
    m->info = info;
    m->session = torrent->session;
    m->torrent = torrent;
    m->io = info->io;
    m->info->clientIsChoked = 1;
    m->info->peerIsChoked = 1;
    m->info->clientIsInterested = 0;
    m->info->peerIsInterested = 0;
    m->info->have = tr_bitfieldNew( torrent->info.pieceCount );
    m->state = AWAITING_BT_LENGTH;
    m->pexTimer = tr_timerNew( m->session, pexPulse, m, PEX_INTERVAL );
    m->outMessages = evbuffer_new( );
    m->outMessagesBatchedAt = 0;
    m->outMessagesBatchPeriod = LOW_PRIORITY_INTERVAL_SECS;
    m->incoming.block = evbuffer_new( );
    m->peerAllowedPieces = NULL;
    m->peerAskedFor = REQUEST_LIST_INIT;
    m->clientAskedFor = REQUEST_LIST_INIT;
    m->clientWillAskFor = REQUEST_LIST_INIT;
    *setme = tr_publisherSubscribe( m->publisher, func, userData );

    if( tr_peerIoSupportsLTEP( m->io ) )
        sendLtepHandshake( m );

    sendBitfield( m );

    tr_peerIoSetTimeoutSecs( m->io, 150 ); /* timeout after N seconds of
                                             inactivity */
    tr_peerIoSetIOFuncs( m->io, canRead, didWrite, gotError, m );
    ratePulse( m );

    return m;
}

void
tr_peerMsgsFree( tr_peermsgs* msgs )
{
    if( msgs )
    {
        tr_timerFree( &msgs->pexTimer );
        tr_publisherFree( &msgs->publisher );
        reqListClear( &msgs->clientWillAskFor );
        reqListClear( &msgs->clientAskedFor );

        tr_bitfieldFree( msgs->peerAllowedPieces );
        evbuffer_free( msgs->incoming.block );
        evbuffer_free( msgs->outMessages );
        tr_free( msgs->pex );

        memset( msgs, ~0, sizeof( tr_peermsgs ) );
        tr_free( msgs );
    }
}

void
tr_peerMsgsUnsubscribe( tr_peermsgs *    peer,
                        tr_publisher_tag tag )
{
    tr_publisherUnsubscribe( peer->publisher, tag );
}

