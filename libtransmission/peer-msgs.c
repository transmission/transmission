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
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h> /* basename */

#include <netinet/in.h> /* struct in_addr */

#include <event.h>

#include "transmission.h"
#include "bencode.h"
#include "completion.h"
#include "inout.h"
#include "list.h"
#include "peer-io.h"
#include "peer-mgr.h"
#include "peer-mgr-private.h"
#include "peer-msgs.h"
#include "ratecontrol.h"
#include "stats.h"
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
    BT_ALLOWED_FAST         = 17,
    BT_LTEP                 = 20,

    LTEP_HANDSHAKE          = 0,

    TR_LTEP_PEX             = 1,

    MAX_REQUEST_BYTE_COUNT  = (16 * 1024), /* drop requests who want too much */

    /* idle seconds before we send a keepalive */
    KEEPALIVE_INTERVAL_SECS = 90,

    PEX_INTERVAL            = (60 * 1000), /* msec between calls to sendPex() */
    PEER_PULSE_INTERVAL     = (100),       /* msec between calls to pulse() */
    RATE_PULSE_INTERVAL     = (250),       /* msec between calls to ratePulse() */

    /* 13 is the size of the BitTorrent overhead of a BT_PIECE message...
     * since all (or nearly all) piece requests are 16 K, this typically
     * means we'd fill a 4096 K outbuf 5 times (4096, 4096, 4096, 4096, 16)
     * but by adding the 13 here we can shorten it to 4 refills... not sure
     * if this actually makes any difference since the libevent layer smooths
     * things out anyway, but it doesn't hurt either... */
    MAX_OUTBUF_SIZE         = (4096 + 13),
     
    /* Fast Peers Extension constants */
    MAX_FAST_ALLOWED_COUNT   = 10,          /* max. number of pieces we fast-allow to another peer */
    MAX_FAST_ALLOWED_THRESHOLD = 10,        /* max threshold for allowing fast-pieces requests */

};

enum
{
    AWAITING_BT_LENGTH,
    AWAITING_BT_ID,
    AWAITING_BT_MESSAGE,
    AWAITING_BT_PIECE
};

struct peer_request
{
    uint32_t index;
    uint32_t offset;
    uint32_t length;
    time_t time_requested;
};

static int
compareRequest( const void * va, const void * vb )
{
    int i;
    const struct peer_request * a = va;
    const struct peer_request * b = vb;
    if(( i = tr_compareUint32( a->index, b->index ))) return i;
    if(( i = tr_compareUint32( a->offset, b->offset ))) return i;
    if(( i = tr_compareUint32( a->length, b->length ))) return i;
    return 0;
}

/* this is raw, unchanged data from the peer regarding
 * the current message that it's sending us. */
struct tr_incoming
{
    uint32_t length; /* includes the +1 for id length */
    uint8_t id;
    struct peer_request blockReq; /* metadata for incoming blocks */
    struct evbuffer * block; /* piece data for incoming blocks */
};

struct tr_peermsgs
{
    tr_peer * info;

    tr_handle * handle;
    tr_torrent * torrent;
    tr_peerIo * io;

    tr_publisher_t * publisher;

    struct evbuffer * outBlock;    /* buffer of all the current piece message */
    struct evbuffer * outMessages; /* buffer of all the non-piece messages */
    tr_list * peerAskedFor;
    tr_list * peerAskedForFast;
    tr_list * clientAskedFor;
    tr_list * clientWillAskFor;

    tr_timer * rateTimer;
    tr_timer * pulseTimer;
    tr_timer * pexTimer;

    time_t lastReqAddedAt;
    time_t clientSentPexAt;
    time_t clientSentAnythingAt;

    time_t clientSentPieceDataAt;
    time_t peerSentPieceDataAt;

    unsigned int peerSentBitfield         : 1;
    unsigned int peerSupportsPex          : 1;
    unsigned int clientSentLtepHandshake  : 1;
    unsigned int peerSentLtepHandshake    : 1;
    unsigned int sendingBlock             : 1;
    
    tr_bitfield * clientAllowedPieces;
    tr_bitfield * peerAllowedPieces;
    
    tr_bitfield * clientSuggestedPieces;
    
    uint8_t state;
    uint8_t ut_pex_id;
    uint16_t pexCount;
    uint32_t maxActiveRequests;
    uint32_t minActiveRequests;

    struct tr_incoming incoming; 

    tr_pex * pex;
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
    if( fp != NULL )
    {
        va_list args;
        char timestr[64];
        struct evbuffer * buf = evbuffer_new( );
        char * myfile = tr_strdup( file );

        evbuffer_add_printf( buf, "[%s] %s - %s [%s]: ",
                             tr_getLogTimeStr( timestr, sizeof(timestr) ),
                             msgs->torrent->info.name,
                             tr_peerIoGetAddrStr( msgs->io ),
                             msgs->info->client );
        va_start( args, fmt );
        evbuffer_add_vprintf( buf, fmt, args );
        va_end( args );
        evbuffer_add_printf( buf, " (%s:%d)\n", basename(myfile), line );
        fwrite( EVBUFFER_DATA(buf), 1, EVBUFFER_LENGTH(buf), fp );

        tr_free( myfile );
        evbuffer_free( buf );
    }
}

#define dbgmsg(msgs, fmt...) myDebug(__FILE__, __LINE__, msgs, ##fmt )

/**
***
**/

static void
protocolSendRequest( tr_peermsgs * msgs, const struct peer_request * req )
{
    tr_peerIo * io = msgs->io;
    struct evbuffer * out = msgs->outMessages;

    dbgmsg( msgs, "requesting %u:%u->%u", req->index, req->offset, req->length );
    tr_peerIoWriteUint32( io, out, sizeof(uint8_t) + 3*sizeof(uint32_t) );
    tr_peerIoWriteUint8 ( io, out, BT_REQUEST );
    tr_peerIoWriteUint32( io, out, req->index );
    tr_peerIoWriteUint32( io, out, req->offset );
    tr_peerIoWriteUint32( io, out, req->length );
}

static void
protocolSendCancel( tr_peermsgs * msgs, const struct peer_request * req )
{
    tr_peerIo * io = msgs->io;
    struct evbuffer * out = msgs->outMessages;

    dbgmsg( msgs, "cancelling %u:%u->%u", req->index, req->offset, req->length );
    tr_peerIoWriteUint32( io, out, sizeof(uint8_t) + 3*sizeof(uint32_t) );
    tr_peerIoWriteUint8 ( io, out, BT_CANCEL );
    tr_peerIoWriteUint32( io, out, req->index );
    tr_peerIoWriteUint32( io, out, req->offset );
    tr_peerIoWriteUint32( io, out, req->length );
}

static void
protocolSendHave( tr_peermsgs * msgs, uint32_t index )
{
    tr_peerIo * io = msgs->io;
    struct evbuffer * out = msgs->outMessages;

    dbgmsg( msgs, "sending Have %u", index );
    tr_peerIoWriteUint32( io, out, sizeof(uint8_t) + sizeof(uint32_t) );
    tr_peerIoWriteUint8 ( io, out, BT_HAVE );
    tr_peerIoWriteUint32( io, out, index );
}

static void
protocolSendChoke( tr_peermsgs * msgs, int choke )
{
    tr_peerIo * io = msgs->io;
    struct evbuffer * out = msgs->outMessages;

    dbgmsg( msgs, "sending %s", (choke ? "Choke" : "Unchoke") );
    tr_peerIoWriteUint32( io, out, sizeof(uint8_t) );
    tr_peerIoWriteUint8 ( io, out, choke ? BT_CHOKE : BT_UNCHOKE );
}

/**
***  EVENTS
**/

static const tr_peermsgs_event blankEvent = { 0, 0, 0, 0, 0.0f };

static void
publish( tr_peermsgs * msgs, tr_peermsgs_event * e )
{
    tr_publisherPublish( msgs->publisher, msgs->info, e );
}

static void
fireGotAssertError( tr_peermsgs * msgs )
{
    tr_peermsgs_event e = blankEvent;
    e.eventType = TR_PEERMSG_GOT_ASSERT_ERROR;
    publish( msgs, &e );
}

static void
fireGotError( tr_peermsgs * msgs )
{
    tr_peermsgs_event e = blankEvent;
    e.eventType = TR_PEERMSG_GOT_ERROR;
    publish( msgs, &e );
}

static void
fireNeedReq( tr_peermsgs * msgs )
{
    tr_peermsgs_event e = blankEvent;
    e.eventType = TR_PEERMSG_NEED_REQ;
    publish( msgs, &e );
}

static void
firePeerProgress( tr_peermsgs * msgs )
{
    tr_peermsgs_event e = blankEvent;
    e.eventType = TR_PEERMSG_PEER_PROGRESS;
    e.progress = msgs->info->progress;
    publish( msgs, &e );
}

static void
fireClientHave( tr_peermsgs * msgs, uint32_t pieceIndex )
{
    tr_peermsgs_event e = blankEvent;
    e.eventType = TR_PEERMSG_CLIENT_HAVE;
    e.pieceIndex = pieceIndex;
    publish( msgs, &e );
}

static void
fireGotBlock( tr_peermsgs * msgs, const struct peer_request * req )
{
    tr_peermsgs_event e = blankEvent;
    e.eventType = TR_PEERMSG_CLIENT_BLOCK;
    e.pieceIndex = req->index;
    e.offset = req->offset;
    e.length = req->length;
    publish( msgs, &e );
}

static void
fireCancelledReq( tr_peermsgs * msgs, const struct peer_request * req )
{
    tr_peermsgs_event e = blankEvent;
    e.eventType = TR_PEERMSG_CANCEL;
    e.pieceIndex = req->index;
    e.offset = req->offset;
    e.length = req->length;
    publish( msgs, &e );
}

/**
***  INTEREST
**/

static int
isPieceInteresting( const tr_peermsgs   * peer,
                    int                   piece )
{
    const tr_torrent * torrent = peer->torrent;
    if( torrent->info.pieces[piece].dnd ) /* we don't want it */
        return FALSE;
    if( tr_cpPieceIsComplete( torrent->completion, piece ) ) /* we have it */
        return FALSE;
    if( !tr_bitfieldHas( peer->info->have, piece ) ) /* peer doesn't have it */
        return FALSE;
    if( tr_bitfieldHas( peer->info->banned, piece ) ) /* peer is banned */
        return FALSE;
    return TRUE;
}

/* "interested" means we'll ask for piece data if they unchoke us */
static int
isPeerInteresting( const tr_peermsgs * msgs )
{
    int i;
    const tr_torrent * torrent;
    const tr_bitfield * bitfield;
    const int clientIsSeed = tr_torrentIsSeed( msgs->torrent );

    if( clientIsSeed )
        return FALSE;

    torrent = msgs->torrent;
    bitfield = tr_cpPieceBitfield( torrent->completion );

    if( !msgs->info->have )
        return TRUE;

    assert( bitfield->len == msgs->info->have->len );
    for( i=0; i<torrent->info.pieceCount; ++i )
        if( isPieceInteresting( msgs, i ) )
            return TRUE;

    return FALSE;
}

static void
sendInterest( tr_peermsgs * msgs, int weAreInterested )
{
    assert( msgs != NULL );
    assert( weAreInterested==0 || weAreInterested==1 );

    msgs->info->clientIsInterested = weAreInterested;
    dbgmsg( msgs, "Sending %s",
            weAreInterested ? "Interested" : "Not Interested");

    tr_peerIoWriteUint32( msgs->io, msgs->outMessages, sizeof(uint8_t) );
    tr_peerIoWriteUint8 ( msgs->io, msgs->outMessages,
                   weAreInterested ? BT_INTERESTED : BT_NOT_INTERESTED );
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

#define MIN_CHOKE_PERIOD_SEC 10

static void
cancelAllRequestsToClientExceptFast( tr_peermsgs * msgs )
{
    tr_list_free( &msgs->peerAskedFor, tr_free );
}

void
tr_peerMsgsSetChoke( tr_peermsgs * msgs, int choke )
{
    const time_t fibrillationTime = time(NULL) - MIN_CHOKE_PERIOD_SEC;

    assert( msgs != NULL );
    assert( msgs->info != NULL );
    assert( choke==0 || choke==1 );

    if( msgs->info->chokeChangedAt > fibrillationTime )
    {
        dbgmsg( msgs, "Not changing choke to %d to avoid fibrillation", choke );
    }
    else if( msgs->info->peerIsChoked != choke )
    {
        msgs->info->peerIsChoked = choke;
        if( choke )
            cancelAllRequestsToClientExceptFast( msgs );
        protocolSendChoke( msgs, choke );
        msgs->info->chokeChangedAt = time( NULL );
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
#if 0
static void
sendFastSuggest( tr_peermsgs * msgs,
                 uint32_t      pieceIndex )
{
    assert( msgs != NULL );
    
    if( tr_peerIoSupportsFEXT( msgs->io ) )
    {
        tr_peerIoWriteUint32( msgs->io, msgs->outMessages, sizeof(uint8_t) + sizeof(uint32_t) );
        tr_peerIoWriteUint8( msgs->io, msgs->outMessages, BT_SUGGEST );
        tr_peerIoWriteUint32( msgs->io, msgs->outMessages, pieceIndex );
    }
}
#endif
static void
sendFastHave( tr_peermsgs * msgs, int all )
{
    assert( msgs != NULL );
    
    if( tr_peerIoSupportsFEXT( msgs->io ) )
    {
        tr_peerIoWriteUint32( msgs->io, msgs->outMessages, sizeof(uint8_t) );
        tr_peerIoWriteUint8( msgs->io, msgs->outMessages, ( all ? BT_HAVE_ALL
                                                                : BT_HAVE_NONE ) );
        updateInterest( msgs );
    }
}

static void
sendFastReject( tr_peermsgs * msgs,
                uint32_t      pieceIndex,
                uint32_t      offset,
                uint32_t      length )
{
    assert( msgs != NULL );

    if( tr_peerIoSupportsFEXT( msgs->io ) )
    {
        const uint32_t len = sizeof(uint8_t) + 3 * sizeof(uint32_t);
        tr_peerIoWriteUint32( msgs->io, msgs->outMessages, len );
        tr_peerIoWriteUint8( msgs->io, msgs->outMessages, BT_REJECT );
        tr_peerIoWriteUint32( msgs->io, msgs->outMessages, pieceIndex );
        tr_peerIoWriteUint32( msgs->io, msgs->outMessages, offset );
        tr_peerIoWriteUint32( msgs->io, msgs->outMessages, length );
    }
}

static void
sendFastAllowed( tr_peermsgs * msgs,
                 uint32_t      pieceIndex)
{
    assert( msgs != NULL );
    
    if( tr_peerIoSupportsFEXT( msgs->io ) )
    {
        tr_peerIoWriteUint32( msgs->io, msgs->outMessages, sizeof(uint8_t) + sizeof(uint32_t) );
        tr_peerIoWriteUint8( msgs->io, msgs->outMessages, BT_ALLOWED_FAST );
        tr_peerIoWriteUint32( msgs->io, msgs->outMessages, pieceIndex );
    }
}



static void
sendFastAllowedSet( tr_peermsgs * msgs )
{
    int i = 0;
    while (i <= msgs->torrent->info.pieceCount )
    {
        if ( tr_bitfieldHas( msgs->peerAllowedPieces, i) )
            sendFastAllowed( msgs, i );
        i++;
    }
}


/**
***
**/

static int
reqIsValid( const tr_peermsgs   * msgs,
            uint32_t              index,
            uint32_t              offset,
            uint32_t              length )
{
    const tr_torrent * tor = msgs->torrent;

    if( index >= (uint32_t) tor->info.pieceCount )
        return FALSE;
    if ( (int)offset >= tr_torPieceCountBytes( tor, (int)index ) )
        return FALSE;
    if( length > MAX_REQUEST_BYTE_COUNT )
        return FALSE;
    if( tr_pieceOffset( tor, index, offset, length ) > tor->info.totalSize )
        return FALSE;

    return TRUE;
}

static int
requestIsValid( const tr_peermsgs * msgs, const struct peer_request * req )
{
    return reqIsValid( msgs, req->index, req->offset, req->length );
}

static void
pumpRequestQueue( tr_peermsgs * msgs )
{
    const int max = msgs->maxActiveRequests;
    const int min = msgs->minActiveRequests;
    int count = tr_list_size( msgs->clientAskedFor );
    int sent = 0;

    if( count > min )
        return;
    if( msgs->info->clientIsChoked )
        return;

    while( ( count < max ) && ( msgs->clientWillAskFor != NULL ) )
    {
        struct peer_request * r = tr_list_pop_front( &msgs->clientWillAskFor );
        assert( requestIsValid( msgs, r ) );
        assert( tr_bitfieldHas( msgs->info->have, r->index ) );
        protocolSendRequest( msgs, r );
        r->time_requested = msgs->lastReqAddedAt = time( NULL );
        tr_list_append( &msgs->clientAskedFor, r );
        ++count;
        ++sent;
    }

    if( sent )
        dbgmsg( msgs, "pump sent %d requests, now have %d active and %d queued",
                sent,
                tr_list_size(msgs->clientAskedFor),
                tr_list_size(msgs->clientWillAskFor) );

    if( count < max )
        fireNeedReq( msgs );
}

static int
pulse( void * vmsgs );

int
tr_peerMsgsAddRequest( tr_peermsgs * msgs,
                       uint32_t      index, 
                       uint32_t      offset, 
                       uint32_t      length )
{
    const int req_max = msgs->maxActiveRequests;
    struct peer_request tmp, *req;

    assert( msgs != NULL );
    assert( msgs->torrent != NULL );
    assert( reqIsValid( msgs, index, offset, length ) );

    /**
    ***  Reasons to decline the request
    **/

    /* don't send requests to choked clients */
    if( msgs->info->clientIsChoked ) {
        dbgmsg( msgs, "declining request because they're choking us" );
        return TR_ADDREQ_CLIENT_CHOKED;
    }

    /* peer doesn't have this piece */
    if( !tr_bitfieldHas( msgs->info->have, index ) )
        return TR_ADDREQ_MISSING;

    /* peer's queue is full */
    if( tr_list_size( msgs->clientWillAskFor ) >= req_max ) {
        dbgmsg( msgs, "declining request because we're full" );
        return TR_ADDREQ_FULL;
    }

    /* have we already asked for this piece? */
    tmp.index = index;
    tmp.offset = offset;
    tmp.length = length;
    if( tr_list_find( msgs->clientAskedFor, &tmp, compareRequest ) ) {
        dbgmsg( msgs, "declining because it's a duplicate" );
        return TR_ADDREQ_DUPLICATE;
    }
    if( tr_list_find( msgs->clientWillAskFor, &tmp, compareRequest ) ) {
        dbgmsg( msgs, "declining because it's a duplicate" );
        return TR_ADDREQ_DUPLICATE;
    }

    /**
    ***  Accept this request
    **/

    dbgmsg( msgs, "added req for piece %d, offset %d", (int)index, (int)offset );
    req = tr_new0( struct peer_request, 1 );
    *req = tmp;
    tr_list_append( &msgs->clientWillAskFor, req );
    return TR_ADDREQ_OK;
}

static void
cancelAllRequestsToPeer( tr_peermsgs * msgs )
{
    struct peer_request * req;

    while(( req = tr_list_pop_front( &msgs->clientWillAskFor ) ))
    {
        fireCancelledReq( msgs, req );
        tr_free( req );
    }

    while(( req = tr_list_pop_front( &msgs->clientAskedFor ) ))
    {
        fireCancelledReq( msgs, req );
        protocolSendCancel( msgs, req );
        tr_free( req );
    }
}

void
tr_peerMsgsCancel( tr_peermsgs * msgs,
                   uint32_t      pieceIndex,
                   uint32_t      offset,
                   uint32_t      length )
{
    struct peer_request *req, tmp;

    assert( msgs != NULL );
    assert( length > 0 );

    /* have we asked the peer for this piece? */
    tmp.index = pieceIndex;
    tmp.offset = offset;
    tmp.length = length;

    /* if it's only in the queue and hasn't been sent yet, free it */
    if(( req = tr_list_remove( &msgs->clientWillAskFor, &tmp, compareRequest ) ))
    {
        fireCancelledReq( msgs, req );
        tr_free( req );
    }

    /* if it's already been sent, send a cancel message too */
    if(( req = tr_list_remove( &msgs->clientAskedFor, &tmp, compareRequest ) ))
    {
        protocolSendCancel( msgs, req );
        fireCancelledReq( msgs, req );
        tr_free( req );
    }
}

/**
***
**/

static void
sendLtepHandshake( tr_peermsgs * msgs )
{
    benc_val_t val, *m;
    char * buf;
    int len;
    int pex;
    const char * v = TR_NAME " " USERAGENT_PREFIX;
    const int port = tr_getPublicPort( msgs->handle );
    struct evbuffer * outbuf;

    if( msgs->clientSentLtepHandshake )
        return;

    outbuf = evbuffer_new( );
    dbgmsg( msgs, "sending an ltep handshake" );
    msgs->clientSentLtepHandshake = 1;

    /* decide if we want to advertise pex support */
    if( !tr_torrentIsPexEnabled( msgs->torrent ) )
        pex = 0;
    else if( msgs->peerSentLtepHandshake )
        pex = msgs->peerSupportsPex ? 1 : 0;
    else
        pex = 1;

    tr_bencInit( &val, TYPE_DICT );
    tr_bencDictReserve( &val, 4 );
    tr_bencInitInt( tr_bencDictAdd( &val, "e" ), 1 );
    m  = tr_bencDictAdd( &val, "m" );
    tr_bencInit( m, TYPE_DICT );
    if( pex ) {
        tr_bencDictReserve( m, 1 );
        tr_bencInitInt( tr_bencDictAdd( m, "ut_pex" ), TR_LTEP_PEX );
    }
    if( port > 0 )
        tr_bencInitInt( tr_bencDictAdd( &val, "p" ), port );
    tr_bencInitStr( tr_bencDictAdd( &val, "v" ), v, 0, 1 );
    buf = tr_bencSave( &val, &len );

    tr_peerIoWriteUint32( msgs->io, outbuf, 2*sizeof(uint8_t) + len );
    tr_peerIoWriteUint8 ( msgs->io, outbuf, BT_LTEP );
    tr_peerIoWriteUint8 ( msgs->io, outbuf, LTEP_HANDSHAKE );
    tr_peerIoWriteBytes ( msgs->io, outbuf, buf, len );

    tr_peerIoWriteBuf( msgs->io, outbuf );

#if 0
    dbgmsg( msgs, "here is the ltep handshake we sent:" );
    tr_bencPrint( &val );
    dbgmsg( msgs, "here is the ltep handshake we read [%s]:", tr_bencSave( &val, NULL ) );
#endif

    /* cleanup */
    tr_bencFree( &val );
    tr_free( buf );
    evbuffer_free( outbuf );
}

static void
parseLtepHandshake( tr_peermsgs * msgs, int len, struct evbuffer * inbuf )
{
    benc_val_t val, * sub;
    uint8_t * tmp = tr_new( uint8_t, len );

    tr_peerIoReadBytes( msgs->io, inbuf, tmp, len );
    msgs->peerSentLtepHandshake = 1;

    if( tr_bencLoad( tmp, len, &val, NULL ) || val.type!=TYPE_DICT ) {
        dbgmsg( msgs, "GET  extended-handshake, couldn't get dictionary" );
        tr_free( tmp );
        return;
    }

#if 0
    dbgmsg( msgs, "here is the ltep handshake we read:" );
    tr_bencPrint( &val );
    dbgmsg( msgs, "here is the ltep handshake we read [%s]:", tr_bencSave( &val, NULL ) );
#endif

    /* does the peer prefer encrypted connections? */
    if(( sub = tr_bencDictFindType( &val, "e", TYPE_INT )))
        msgs->info->encryption_preference = sub->val.i
                                      ? ENCRYPTION_PREFERENCE_YES
                                      : ENCRYPTION_PREFERENCE_NO;

    /* check supported messages for utorrent pex */
    if(( sub = tr_bencDictFindType( &val, "m", TYPE_DICT ))) {
        if(( sub = tr_bencDictFindType( sub, "ut_pex", TYPE_INT ))) {
            msgs->peerSupportsPex = 1;
            msgs->ut_pex_id = (uint8_t) sub->val.i;
            dbgmsg( msgs, "msgs->ut_pex is %d", (int)msgs->ut_pex_id );
        }
    }

    /* get peer's listening port */
    if(( sub = tr_bencDictFindType( &val, "p", TYPE_INT ))) {
        msgs->info->port = htons( (uint16_t)sub->val.i );
        dbgmsg( msgs, "msgs->port is now %hu", msgs->info->port );
    }

    tr_bencFree( &val );
    tr_free( tmp );
}

static void
parseUtPex( tr_peermsgs * msgs, int msglen, struct evbuffer * inbuf )
{
    benc_val_t val, * sub;
    uint8_t * tmp;

    if( !tr_torrentIsPexEnabled( msgs->torrent ) ) /* no sharing! */
        return;

    tmp = tr_new( uint8_t, msglen );
    tr_peerIoReadBytes( msgs->io, inbuf, tmp, msglen );

    if( tr_bencLoad( tmp, msglen, &val, NULL ) || ( val.type != TYPE_DICT ) ) {
        dbgmsg( msgs, "GET can't read extended-pex dictionary" );
        tr_free( tmp );
        return;
    }

    if(( sub = tr_bencDictFindType( &val, "added", TYPE_STR ))) {
        const int n = sub->val.s.i / 6 ;
        dbgmsg( msgs, "got %d peers from uT pex", n );
        tr_peerMgrAddPeers( msgs->handle->peerMgr,
                            msgs->torrent->info.hash,
                            TR_PEER_FROM_PEX,
                            (uint8_t*)sub->val.s.s, n );
    }

    tr_bencFree( &val );
    tr_free( tmp );
}

static void
sendPex( tr_peermsgs * msgs );

static void
parseLtep( tr_peermsgs * msgs, int msglen, struct evbuffer * inbuf )
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
readBtLength( tr_peermsgs * msgs, struct evbuffer * inbuf, size_t inlen )
{
    uint32_t len;

    if( inlen < sizeof(len) )
        return READ_MORE;

    tr_peerIoReadUint32( msgs->io, inbuf, &len );

    if( len == 0 ) /* peer sent us a keepalive message */
        dbgmsg( msgs, "got KeepAlive" );
    else {
        msgs->incoming.length = len;
        msgs->state = AWAITING_BT_ID;
    }

    return READ_AGAIN;
}

static int
readBtMessage( tr_peermsgs * msgs, struct evbuffer * inbuf, size_t inlen );

static int
readBtId( tr_peermsgs * msgs, struct evbuffer * inbuf, size_t inlen )
{
    uint8_t id;

    if( inlen < sizeof(uint8_t) )
        return READ_MORE;

    tr_peerIoReadUint8( msgs->io, inbuf, &id );
    msgs->incoming.id = id;

    if( id==BT_PIECE )
    {
        msgs->state = AWAITING_BT_PIECE;
        return READ_AGAIN;
    }
    else if( msgs->incoming.length != 1 )
    {
        msgs->state = AWAITING_BT_MESSAGE;
        return READ_AGAIN;
    }
    else return readBtMessage( msgs, inbuf, inlen-1 );
}

static void
updatePeerProgress( tr_peermsgs * msgs )
{
    msgs->info->progress = tr_bitfieldCountTrueBits( msgs->info->have ) / (float)msgs->torrent->info.pieceCount;
    dbgmsg( msgs, "peer progress is %f", msgs->info->progress );
    updateInterest( msgs );
    firePeerProgress( msgs );
}

static int
clientCanSendFastBlock( const tr_peermsgs * msgs UNUSED )
{
    /* We can't send a fast piece if a peer has more than MAX_FAST_ALLOWED_THRESHOLD pieces */
    if( tr_bitfieldCountTrueBits( msgs->info->have ) > MAX_FAST_ALLOWED_THRESHOLD )
        return FALSE;
    
    /* ...or if we don't have ourself enough pieces */
    if( tr_bitfieldCountTrueBits( tr_cpPieceBitfield( msgs->torrent->completion ) ) < MAX_FAST_ALLOWED_THRESHOLD )
        return FALSE;

    /* Maybe a bandwidth limit ? */
    return TRUE;
}

static void
peerMadeRequest( tr_peermsgs * msgs, const struct peer_request * req )
{
    const int reqIsValid = requestIsValid( msgs, req );
    const int clientHasPiece = reqIsValid && tr_cpPieceIsComplete( msgs->torrent->completion, req->index );
    const int peerIsChoked = msgs->info->peerIsChoked;
    const int peerIsFast = tr_peerIoSupportsFEXT( msgs->io );
    const int pieceIsFast = reqIsValid && tr_bitfieldHas( msgs->peerAllowedPieces, req->index );
    const int canSendFast = clientCanSendFastBlock( msgs );

    if( !reqIsValid ) /* bad request */
    {
        dbgmsg( msgs, "rejecting an invalid request." );
        sendFastReject( msgs, req->index, req->offset, req->length );
    }
    else if( !clientHasPiece ) /* we don't have it */
    {
        dbgmsg( msgs, "rejecting request for a piece we don't have." );
        sendFastReject( msgs, req->index, req->offset, req->length );
    }
    else if( peerIsChoked && !peerIsFast ) /* doesn't he know he's choked? */
    {
        tr_peerMsgsSetChoke( msgs, 1 );
        sendFastReject( msgs, req->index, req->offset, req->length );
    }
    else if( peerIsChoked && peerIsFast && ( !pieceIsFast || !canSendFast ) )
    {
        sendFastReject( msgs, req->index, req->offset, req->length );
    }
    else /* YAY */
    {
        struct peer_request * tmp = tr_new( struct peer_request, 1 );
        *tmp = *req;
        if( peerIsFast && pieceIsFast )
            tr_list_append( &msgs->peerAskedForFast, tmp );
        else
            tr_list_append( &msgs->peerAskedFor, tmp );
    }
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
        case BT_HAVE_ALL:
        case BT_HAVE_NONE:
            return len==1;

        case BT_HAVE:
        case BT_SUGGEST:
        case BT_ALLOWED_FAST:
            return len==5;

        case BT_BITFIELD:
            return len == (msg->torrent->info.pieceCount+7u)/8u + 1u;
       
        case BT_REQUEST:
        case BT_CANCEL:
        case BT_REJECT:
            return len==13;

        case BT_PIECE:
            return len>9 && len<=16393;

        case BT_PORT:
            return len==3;

        case BT_LTEP:
            return len >= 2;

        default:
            return FALSE;
    }
}

static int
clientGotBlock( tr_peermsgs * msgs, const uint8_t * block, const struct peer_request * req );

static void
clientGotBytes( tr_peermsgs * msgs, uint32_t byteCount )
{
    tr_torrent * tor = msgs->torrent;
    tor->activityDate = tr_date( );
    tor->downloadedCur += byteCount;
    msgs->peerSentPieceDataAt = time( NULL );
    msgs->info->pieceDataActivityDate = time( NULL );
    msgs->info->credit += (int)(byteCount * SWIFT_REPAYMENT_RATIO);
    tr_rcTransferred( msgs->info->rcToClient, byteCount );
    tr_rcTransferred( tor->download, byteCount );
    tr_rcTransferred( tor->handle->download, byteCount );
    tr_statsAddDownloaded( msgs->handle, byteCount );
}

static int
readBtPiece( tr_peermsgs * msgs, struct evbuffer * inbuf, size_t inlen )
{
    struct peer_request * req = &msgs->incoming.blockReq;
    assert( EVBUFFER_LENGTH(inbuf) >= inlen );
    dbgmsg( msgs, "In readBtPiece" );

    if( !req->length )
    {
        if( inlen < 8 )
            return READ_MORE;

        tr_peerIoReadUint32( msgs->io, inbuf, &req->index );
        tr_peerIoReadUint32( msgs->io, inbuf, &req->offset );
        req->length = msgs->incoming.length - 9;
        dbgmsg( msgs, "got incoming block header %u:%u->%u", req->index, req->offset, req->length );
        return READ_AGAIN;
    }
    else
    {
        int err;

        /* read in another chunk of data */
        const size_t nLeft = req->length - EVBUFFER_LENGTH(msgs->incoming.block);
        size_t n = MIN( nLeft, inlen );
        uint8_t * buf = tr_new( uint8_t, n );
        assert( EVBUFFER_LENGTH(inbuf) >= n );
        tr_peerIoReadBytes( msgs->io, inbuf, buf, n );
        evbuffer_add( msgs->incoming.block, buf, n );
        clientGotBytes( msgs, n );
        tr_free( buf );
        dbgmsg( msgs, "got %d bytes for block %u:%u->%u ... %d remain",
               (int)n, req->index, req->offset, req->length,
               (int)( req->length - EVBUFFER_LENGTH(msgs->incoming.block) ) );
        if( EVBUFFER_LENGTH(msgs->incoming.block) < req->length )
            return READ_MORE;

        /* we've got the whole block ... process it */
        err = clientGotBlock( msgs, EVBUFFER_DATA(msgs->incoming.block), req );

        /* cleanup */
        evbuffer_drain( msgs->incoming.block, EVBUFFER_LENGTH(msgs->incoming.block) );
        req->length = 0;
        msgs->state = AWAITING_BT_LENGTH;
        if( !err )
            return READ_AGAIN;
        else {
            fireGotAssertError( msgs );
            return READ_DONE;
        }
    }
}

static int
readBtMessage( tr_peermsgs * msgs, struct evbuffer * inbuf, size_t inlen )
{
    uint32_t ui32;
    uint32_t msglen = msgs->incoming.length;
    const uint8_t id = msgs->incoming.id;
    const size_t startBufLen = EVBUFFER_LENGTH( inbuf );

    --msglen; // id length

    if( inlen < msglen )
        return READ_MORE;

    dbgmsg( msgs, "got BT id %d, len %d, buffer size is %d", (int)id, (int)msglen, (int)inlen );

    if( !messageLengthIsCorrect( msgs, id, msglen+1 ) )
    {
        dbgmsg( msgs, "bad packet - BT message #%d with a length of %d", (int)id, (int)msglen );
        fireGotError( msgs );
        return READ_DONE;
    }

    switch( id )
    {
        case BT_CHOKE:
            dbgmsg( msgs, "got Choke" );
            msgs->info->clientIsChoked = 1;
            cancelAllRequestsToPeer( msgs );
            cancelAllRequestsToClientExceptFast( msgs );
            break;

        case BT_UNCHOKE:
            dbgmsg( msgs, "got Unchoke" );
            msgs->info->clientIsChoked = 0;
            fireNeedReq( msgs );
            break;

        case BT_INTERESTED:
            dbgmsg( msgs, "got Interested" );
            msgs->info->peerIsInterested = 1;
            tr_peerMsgsSetChoke( msgs, 0 );
            break;

        case BT_NOT_INTERESTED:
            dbgmsg( msgs, "got Not Interested" );
            msgs->info->peerIsInterested = 0;
            break;
            
        case BT_HAVE:
            tr_peerIoReadUint32( msgs->io, inbuf, &ui32 );
            dbgmsg( msgs, "got Have: %u", ui32 );
            tr_bitfieldAdd( msgs->info->have, ui32 );
            /* If this is a fast-allowed piece for this peer, mark it as normal now */
            if( msgs->clientAllowedPieces != NULL && tr_bitfieldHas( msgs->clientAllowedPieces, ui32 ) )
                tr_bitfieldRem( msgs->clientAllowedPieces, ui32 );
            updatePeerProgress( msgs );
            tr_rcTransferred( msgs->torrent->swarmspeed, msgs->torrent->info.pieceSize );
            break;

        case BT_BITFIELD: {
            const int clientIsSeed = tr_torrentIsSeed( msgs->torrent );
            dbgmsg( msgs, "got a bitfield" );
            msgs->peerSentBitfield = 1;
            tr_peerIoReadBytes( msgs->io, inbuf, msgs->info->have->bits, msglen );
            updatePeerProgress( msgs );
            tr_peerMsgsSetChoke( msgs, !clientIsSeed || (msgs->info->progress<1.0) );
            fireNeedReq( msgs );
            break;
        }

        case BT_REQUEST: {
            struct peer_request req;
            tr_peerIoReadUint32( msgs->io, inbuf, &req.index );
            tr_peerIoReadUint32( msgs->io, inbuf, &req.offset );
            tr_peerIoReadUint32( msgs->io, inbuf, &req.length );
            dbgmsg( msgs, "got Request: %u:%u->%u", req.index, req.offset, req.length );
            peerMadeRequest( msgs, &req );
            break;
        }

        case BT_CANCEL: {
            struct peer_request req;
            tr_peerIoReadUint32( msgs->io, inbuf, &req.index );
            tr_peerIoReadUint32( msgs->io, inbuf, &req.offset );
            tr_peerIoReadUint32( msgs->io, inbuf, &req.length );
            dbgmsg( msgs, "got a Cancel %u:%u->%u", req.index, req.offset, req.length );
            tr_free( tr_list_remove( &msgs->peerAskedForFast, &req, compareRequest ) );
            tr_free( tr_list_remove( &msgs->peerAskedFor, &req, compareRequest ) );
            break;
        }

        case BT_PIECE:
            assert( 0 ); /* should be handled elsewhere! */
            break;
        
        case BT_PORT:
            dbgmsg( msgs, "Got a BT_PORT" );
            tr_peerIoReadUint16( msgs->io, inbuf, &msgs->info->port );
            break;
            
        case BT_SUGGEST: {
            const tr_bitfield * bt = tr_cpPieceBitfield( msgs->torrent->completion );
            dbgmsg( msgs, "Got a BT_SUGGEST" );
            tr_peerIoReadUint32( msgs->io, inbuf, &ui32 );
            if( tr_bitfieldHas( bt, ui32 ) )
                tr_bitfieldAdd( msgs->clientSuggestedPieces, ui32 );
            break;
        }
            
        case BT_HAVE_ALL:
            dbgmsg( msgs, "Got a BT_HAVE_ALL" );
            tr_bitfieldAddRange( msgs->info->have, 0, msgs->torrent->info.pieceCount );
            updatePeerProgress( msgs );
            break;
        
        
        case BT_HAVE_NONE:
            dbgmsg( msgs, "Got a BT_HAVE_NONE" );
            tr_bitfieldClear( msgs->info->have );
            updatePeerProgress( msgs );
            break;
        
        case BT_REJECT: {
            struct peer_request req;
            dbgmsg( msgs, "Got a BT_REJECT" );
            tr_peerIoReadUint32( msgs->io, inbuf, &req.index );
            tr_peerIoReadUint32( msgs->io, inbuf, &req.offset );
            tr_peerIoReadUint32( msgs->io, inbuf, &req.length );
            tr_free( tr_list_remove( &msgs->clientAskedFor, &req, compareRequest ) );
            break;
        }

        case BT_ALLOWED_FAST: {
            dbgmsg( msgs, "Got a BT_ALLOWED_FAST" );
            tr_peerIoReadUint32( msgs->io, inbuf, &ui32 );
            tr_bitfieldAdd( msgs->clientAllowedPieces, ui32 );
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
    assert( EVBUFFER_LENGTH(inbuf) == startBufLen - msglen );

    msgs->state = AWAITING_BT_LENGTH;
    return READ_AGAIN;
}

static void
peerGotBytes( tr_peermsgs * msgs, uint32_t byteCount )
{
    tr_torrent * tor = msgs->torrent;
    tor->activityDate = tr_date( );
    tor->uploadedCur += byteCount;
    msgs->clientSentPieceDataAt = time( NULL );
    msgs->info->pieceDataActivityDate = time( NULL );
    msgs->info->credit -= byteCount;
    tr_rcTransferred( msgs->info->rcToPeer, byteCount );
    tr_rcTransferred( tor->upload, byteCount );
    tr_rcTransferred( tor->handle->upload, byteCount );
    tr_statsAddUploaded( msgs->handle, byteCount );
}

static size_t
getDownloadMax( const tr_peermsgs * msgs )
{
    static const size_t maxval = ~0;
    const tr_torrent * tor = msgs->torrent;

    if( tor->downloadLimitMode == TR_SPEEDLIMIT_GLOBAL )
        return tor->handle->useDownloadLimit
            ? tr_rcBytesLeft( tor->handle->download ) : maxval;

    if( tor->downloadLimitMode == TR_SPEEDLIMIT_SINGLE )
        return tr_rcBytesLeft( tor->download );

    return maxval;
}

static void
reassignBytesToCorrupt( tr_peermsgs * msgs, uint32_t byteCount )
{
    tr_torrent * tor = msgs->torrent;

    /* increment the `corrupt' field */
    tor->corruptCur += byteCount;

    /* decrement the `downloaded' field */
    if( tor->downloadedCur >= byteCount )
        tor->downloadedCur -= byteCount;
    else
        tor->downloadedCur = 0;
}


static void
gotBadPiece( tr_peermsgs * msgs, uint32_t pieceIndex )
{
    const uint32_t byteCount =
        tr_torPieceCountBytes( msgs->torrent, (int)pieceIndex );
    reassignBytesToCorrupt( msgs, byteCount );
}

static void
clientGotUnwantedBlock( tr_peermsgs * msgs, const struct peer_request * req )
{
    reassignBytesToCorrupt( msgs, req->length );
}

static void
addPeerToBlamefield( tr_peermsgs * msgs, uint32_t index )
{
    if( !msgs->info->blame )
         msgs->info->blame = tr_bitfieldNew( msgs->torrent->info.pieceCount );
    tr_bitfieldAdd( msgs->info->blame, index );
}

static int
clientGotBlock( tr_peermsgs                * msgs,
                const uint8_t              * data,
                const struct peer_request  * req )
{
    int i;
    tr_torrent * tor = msgs->torrent;
    const int block = _tr_block( tor, req->index, req->offset );
    struct peer_request *myreq;

    assert( msgs != NULL );
    assert( req != NULL );

    if( req->length != (uint32_t)tr_torBlockCountBytes( msgs->torrent, block ) )
    {
        dbgmsg( msgs, "wrong block size -- expected %u, got %d",
                tr_torBlockCountBytes( msgs->torrent, block ), req->length );
        return TR_ERROR_ASSERT;
    }

    /* save the block */
    dbgmsg( msgs, "got block %u:%u->%u", req->index, req->offset, req->length );

    /**
    *** Remove the block from our `we asked for this' list
    **/

    myreq = tr_list_remove( &msgs->clientAskedFor, req, compareRequest );
    if( myreq == NULL ) {
        clientGotUnwantedBlock( msgs, req );
        dbgmsg( msgs, "we didn't ask for this message..." );
        return 0;
    }

    dbgmsg( msgs, "got block %u:%u->%u (turnaround time %d secs)", 
                  myreq->index, myreq->offset, myreq->length,
                  (int)(time(NULL) - myreq->time_requested) );
    dbgmsg( msgs, "peer has %d more blocks we've asked for",
                  tr_list_size(msgs->clientAskedFor));

    tr_free( myreq );
    myreq = NULL;

    /**
    *** Error checks
    **/

    if( tr_cpBlockIsComplete( tor->completion, block ) ) {
        dbgmsg( msgs, "we have this block already..." );
        clientGotUnwantedBlock( msgs, req );
        return 0;
    }

    /**
    ***  Save the block
    **/

    msgs->info->peerSentPieceDataAt = time( NULL );
    i = tr_ioWrite( tor, req->index, req->offset, req->length, data );
    if( i )
        return 0;

    tr_cpBlockAdd( tor->completion, block );

    addPeerToBlamefield( msgs, req->index );

    fireGotBlock( msgs, req );

    /**
    ***  Handle if this was the last block in the piece
    **/

    if( tr_cpPieceIsComplete( tor->completion, req->index ) )
    {
        if( tr_ioHash( tor, req->index ) )
        {
            gotBadPiece( msgs, req->index );
            return 0;
        }

        fireClientHave( msgs, req->index );
    }

    return 0;
}

static void
didWrite( struct bufferevent * evin UNUSED, void * vmsgs )
{
    pulse( vmsgs );
}

static ReadState
canRead( struct bufferevent * evin, void * vmsgs )
{
    ReadState ret;
    tr_peermsgs * msgs = (tr_peermsgs *) vmsgs;
    struct evbuffer * inbuf = EVBUFFER_INPUT ( evin );
    const size_t buflen = EVBUFFER_LENGTH( inbuf );
    const size_t downloadMax = getDownloadMax( msgs );
    const size_t n = MIN( buflen, downloadMax );

    if( !n )
    {
        ret = READ_DONE;
    }
    else switch( msgs->state )
    {
        case AWAITING_BT_LENGTH:  ret = readBtLength ( msgs, inbuf, n ); break;
        case AWAITING_BT_ID:      ret = readBtId     ( msgs, inbuf, n ); break;
        case AWAITING_BT_MESSAGE: ret = readBtMessage( msgs, inbuf, n ); break;
        case AWAITING_BT_PIECE:   ret = readBtPiece  ( msgs, inbuf, n ); break;
        default: assert( 0 );
    }

    return ret;
}

static void
sendKeepalive( tr_peermsgs * msgs )
{
    dbgmsg( msgs, "sending a keepalive message" );
    tr_peerIoWriteUint32( msgs->io, msgs->outMessages, 0 );
}

/**
***
**/

static int
isSwiftEnabled( const tr_peermsgs * msgs )
{
    /* rationale: SWIFT is good for getting rid of deadbeats, but most
     * private trackers have ratios where you _want_ to feed deadbeats
     * as much as possible.  So we disable SWIFT on private torrents */
    return SWIFT_ENABLED
        && !tr_torrentIsSeed( msgs->torrent )
        && !tr_torrentIsPrivate( msgs->torrent );
}

static size_t
getUploadMax( const tr_peermsgs * msgs )
{
    static const size_t maxval = ~0;
    const tr_torrent * tor = msgs->torrent;
    const int useSwift = isSwiftEnabled( msgs );
    const size_t swiftLeft = msgs->info->credit;
    size_t speedLeft;
    size_t bufLeft;
    size_t ret;

    if( tor->uploadLimitMode == TR_SPEEDLIMIT_GLOBAL )
        speedLeft = tor->handle->useUploadLimit ? tr_rcBytesLeft( tor->handle->upload ) : maxval;
    else if( tor->uploadLimitMode == TR_SPEEDLIMIT_SINGLE )
        speedLeft = tr_rcBytesLeft( tor->upload );
    else
        speedLeft = ~0;

    bufLeft = MAX_OUTBUF_SIZE - tr_peerIoWriteBytesWaiting( msgs->io );
    ret = MIN( speedLeft, bufLeft );
    if( useSwift)
        ret = MIN( ret, swiftLeft );
    return ret;
}

static int
ratePulse( void * vmsgs )
{
    tr_peermsgs * msgs = (tr_peermsgs *) vmsgs;
    msgs->info->rateToClient = tr_rcRate( msgs->info->rcToClient );
    msgs->info->rateToPeer = tr_rcRate( msgs->info->rcToPeer );
    msgs->maxActiveRequests = MIN( 8 + (int)(msgs->info->rateToClient/5), 100 );
    msgs->minActiveRequests = msgs->maxActiveRequests / 3;
    return TRUE;
}

static struct peer_request*
popNextRequest( tr_peermsgs * msgs )
{
    struct peer_request * ret;
    ret = tr_list_pop_front( &msgs->peerAskedForFast );
    if( !ret )
        ret = tr_list_pop_front( &msgs->peerAskedFor);
    return ret;
}

static void
updatePeerStatus( tr_peermsgs * msgs )
{
    const time_t now = time( NULL );
    tr_peer * peer = msgs->info;
    tr_peer_status status = 0;

    if( !msgs->peerSentBitfield )
        status |= TR_PEER_STATUS_HANDSHAKE;

    if( msgs->info->peerIsChoked )
        status |= TR_PEER_STATUS_PEER_IS_CHOKED;

    if( msgs->info->peerIsInterested )
        status |= TR_PEER_STATUS_PEER_IS_INTERESTED;

    if( msgs->info->clientIsChoked )
        status |= TR_PEER_STATUS_CLIENT_IS_CHOKED;

    if( msgs->info->clientIsInterested )
        status |= TR_PEER_STATUS_CLIENT_IS_INTERESTED;

    if( ( now - msgs->clientSentPieceDataAt ) < 3 )
        status |= TR_PEER_STATUS_CLIENT_IS_SENDING;

    if( ( now - msgs->peerSentPieceDataAt ) < 3 )
        status |= TR_PEER_STATUS_PEER_IS_SENDING;

    if( msgs->clientAskedFor != NULL )
        status |= TR_PEER_STATUS_CLIENT_SENT_REQUEST;

    peer->status = status;
}

static int
pulse( void * vmsgs )
{
    const time_t now = time( NULL );
    tr_peermsgs * msgs = vmsgs;
    struct peer_request * r;

    tr_peerIoTryRead( msgs->io );
    pumpRequestQueue( msgs );
    updatePeerStatus( msgs );

    if( msgs->sendingBlock )
    {
        const size_t uploadMax = getUploadMax( msgs );
        size_t len = EVBUFFER_LENGTH( msgs->outBlock );
        const size_t outlen = MIN( len, uploadMax );

        assert( len );

        if( outlen )
        {
            tr_peerIoWrite( msgs->io, EVBUFFER_DATA( msgs->outBlock ), outlen );
            evbuffer_drain( msgs->outBlock, outlen );
            peerGotBytes( msgs, outlen );

            len -= outlen;
            msgs->clientSentAnythingAt = now;
            msgs->sendingBlock = len!=0;

            dbgmsg( msgs, "wrote %d bytes; %d left in block", (int)outlen, (int)len );
        }
        else dbgmsg( msgs, "stalled writing block... uploadMax %lu, outlen %lu", uploadMax, outlen );
    }

    if( !msgs->sendingBlock )
    {
        if(( EVBUFFER_LENGTH( msgs->outMessages ) ))
        {
            dbgmsg( msgs, "flushing outMessages..." );
            tr_peerIoWriteBuf( msgs->io, msgs->outMessages );
            msgs->clientSentAnythingAt = now;
        }
        else if( !EVBUFFER_LENGTH( msgs->outBlock )
            && (( r = popNextRequest( msgs )))
            && requestIsValid( msgs, r )
            && tr_cpPieceIsComplete( msgs->torrent->completion, r->index ) )
        {
            uint8_t * buf = tr_new( uint8_t, r->length );

            if( !tr_ioRead( msgs->torrent, r->index, r->offset, r->length, buf ) )
            {
                tr_peerIo * io = msgs->io;
                struct evbuffer * out = msgs->outBlock;

                dbgmsg( msgs, "sending block %u:%u->%u", r->index, r->offset, r->length );
                tr_peerIoWriteUint32( io, out, sizeof(uint8_t) + 2*sizeof(uint32_t) + r->length );
                tr_peerIoWriteUint8 ( io, out, BT_PIECE );
                tr_peerIoWriteUint32( io, out, r->index );
                tr_peerIoWriteUint32( io, out, r->offset );
                tr_peerIoWriteBytes ( io, out, buf, r->length );
                msgs->sendingBlock = 1;
            }

            tr_free( buf );
            tr_free( r );
        }
        else if( ( now - msgs->clientSentAnythingAt ) > KEEPALIVE_INTERVAL_SECS )
        {
            sendKeepalive( msgs );
        }
    }

    return TRUE; /* loop forever */
}

static void
gotError( struct bufferevent * evbuf UNUSED, short what, void * vmsgs )
{
    if( what & EVBUFFER_TIMEOUT )
        dbgmsg( vmsgs, "libevent got a timeout, what=%hd", what );

    if( what & ( EVBUFFER_EOF | EVBUFFER_ERROR ) ) {
        dbgmsg( vmsgs, "libevent got an error! what=%hd, errno=%d (%s)",
                what, errno, strerror(errno) );
        fireGotError( vmsgs );
    }
}

static void
sendBitfield( tr_peermsgs * msgs )
{
    const tr_bitfield * bitfield = tr_cpPieceBitfield( msgs->torrent->completion );
    struct evbuffer * out = msgs->outMessages;

    dbgmsg( msgs, "sending peer a bitfield message" );
    tr_peerIoWriteUint32( msgs->io, out, sizeof(uint8_t) + bitfield->len );
    tr_peerIoWriteUint8 ( msgs->io, out, BT_BITFIELD );
    tr_peerIoWriteBytes ( msgs->io, out, bitfield->bits, bitfield->len );
}

/**
***
**/

/* some peers give us error messages if we send
   more than this many peers in a single pex message */
#define MAX_PEX_DIFFS 200

typedef struct
{
    tr_pex * added;
    tr_pex * dropped;
    tr_pex * elements;
    int addedCount;
    int droppedCount;
    int elementCount;
    int diffCount;
}
PexDiffs;

static void
pexAddedCb( void * vpex, void * userData )
{
    PexDiffs * diffs = (PexDiffs *) userData;
    tr_pex * pex = (tr_pex *) vpex;
    if( diffs->diffCount < MAX_PEX_DIFFS )
    {
        diffs->diffCount++;
        diffs->added[diffs->addedCount++] = *pex;
        diffs->elements[diffs->elementCount++] = *pex;
    }
}

static void
pexRemovedCb( void * vpex, void * userData )
{
    PexDiffs * diffs = (PexDiffs *) userData;
    tr_pex * pex = (tr_pex *) vpex;
    if( diffs->diffCount < MAX_PEX_DIFFS )
    {
        diffs->diffCount++;
        diffs->dropped[diffs->droppedCount++] = *pex;
    }
}

static void
pexElementCb( void * vpex, void * userData )
{
    PexDiffs * diffs = (PexDiffs *) userData;
    tr_pex * pex = (tr_pex *) vpex;
    if( diffs->diffCount < MAX_PEX_DIFFS )
    {
        diffs->diffCount++;
        diffs->elements[diffs->elementCount++] = *pex;
    }
}

static void
sendPex( tr_peermsgs * msgs )
{
    if( msgs->peerSupportsPex && tr_torrentIsPexEnabled( msgs->torrent ) )
    {
        int i;
        tr_pex * newPex = NULL;
        const int newCount = tr_peerMgrGetPeers( msgs->handle->peerMgr, msgs->torrent->info.hash, &newPex );
        PexDiffs diffs;
        benc_val_t val, *added, *dropped, *flags;
        uint8_t *tmp, *walk;
        char * benc;
        int bencLen;

        /* build the diffs */
        diffs.added = tr_new( tr_pex, newCount );
        diffs.addedCount = 0;
        diffs.dropped = tr_new( tr_pex, msgs->pexCount );
        diffs.droppedCount = 0;
        diffs.elements = tr_new( tr_pex, newCount + msgs->pexCount );
        diffs.elementCount = 0;
        diffs.diffCount = 0;
        tr_set_compare( msgs->pex, msgs->pexCount,
                        newPex, newCount,
                        tr_pexCompare, sizeof(tr_pex),
                        pexRemovedCb, pexAddedCb, pexElementCb, &diffs );
        dbgmsg( msgs, "pex: old peer count %d, new peer count %d, added %d, removed %d", msgs->pexCount, newCount, diffs.addedCount, diffs.droppedCount );

        /* update peer */
        tr_free( msgs->pex );
        msgs->pex = diffs.elements;
        msgs->pexCount = diffs.elementCount;

        /* build the pex payload */
        tr_bencInit( &val, TYPE_DICT );
        tr_bencDictReserve( &val, 3 );

        /* "added" */
        added = tr_bencDictAdd( &val, "added" );
        tmp = walk = tr_new( uint8_t, diffs.addedCount * 6 );
        for( i=0; i<diffs.addedCount; ++i ) {
            memcpy( walk, &diffs.added[i].in_addr, 4 ); walk += 4;
            memcpy( walk, &diffs.added[i].port, 2 ); walk += 2;
        }
        assert( ( walk - tmp ) == diffs.addedCount * 6 );
        tr_bencInitStr( added, tmp, walk-tmp, FALSE );

        /* "added.f" */
        flags = tr_bencDictAdd( &val, "added.f" );
        tmp = walk = tr_new( uint8_t, diffs.addedCount );
        for( i=0; i<diffs.addedCount; ++i )
            *walk++ = diffs.added[i].flags;
        assert( ( walk - tmp ) == diffs.addedCount );
        tr_bencInitStr( flags, tmp, walk-tmp, FALSE );

        /* "dropped" */
        dropped = tr_bencDictAdd( &val, "dropped" );
        tmp = walk = tr_new( uint8_t, diffs.droppedCount * 6 );
        for( i=0; i<diffs.droppedCount; ++i ) {
            memcpy( walk, &diffs.dropped[i].in_addr, 4 ); walk += 4;
            memcpy( walk, &diffs.dropped[i].port, 2 ); walk += 2;
        }
        assert( ( walk - tmp ) == diffs.droppedCount * 6 );
        tr_bencInitStr( dropped, tmp, walk-tmp, FALSE );

        /* write the pex message */
        benc = tr_bencSave( &val, &bencLen );
        tr_peerIoWriteUint32( msgs->io, msgs->outMessages, 2*sizeof(uint8_t) + bencLen );
        tr_peerIoWriteUint8 ( msgs->io, msgs->outMessages, BT_LTEP );
        tr_peerIoWriteUint8 ( msgs->io, msgs->outMessages, msgs->ut_pex_id );
        tr_peerIoWriteBytes ( msgs->io, msgs->outMessages, benc, bencLen );

        /* cleanup */
        tr_free( benc );
        tr_bencFree( &val );
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
                struct tr_peer    * info,
                tr_delivery_func    func,
                void              * userData,
                tr_publisher_tag  * setme )
{
    tr_peermsgs * m;

    assert( info != NULL );
    assert( info->io != NULL );

    m = tr_new0( tr_peermsgs, 1 );
    m->publisher = tr_publisherNew( );
    m->info = info;
    m->handle = torrent->handle;
    m->torrent = torrent;
    m->io = info->io;
    m->info->status = TR_PEER_STATUS_HANDSHAKE;
    m->info->clientIsChoked = 1;
    m->info->peerIsChoked = 1;
    m->info->clientIsInterested = 0;
    m->info->peerIsInterested = 0;
    m->info->have = tr_bitfieldNew( torrent->info.pieceCount );
    m->state = AWAITING_BT_LENGTH;
    m->pulseTimer = tr_timerNew( m->handle, pulse, m, PEER_PULSE_INTERVAL );
    m->rateTimer = tr_timerNew( m->handle, ratePulse, m, RATE_PULSE_INTERVAL );
    m->pexTimer = tr_timerNew( m->handle, pexPulse, m, PEX_INTERVAL );
    m->outMessages = evbuffer_new( );
    m->incoming.block = evbuffer_new( );
    m->outBlock = evbuffer_new( );
    m->peerAllowedPieces = NULL;
    m->clientAllowedPieces = NULL;
    m->clientSuggestedPieces = NULL;
    *setme = tr_publisherSubscribe( m->publisher, func, userData );
    
    if ( tr_peerIoSupportsFEXT( m->io ) )
    {
        /* This peer is fastpeer-enabled, generate its allowed set
         * (before registering our callbacks) */
        if ( !m->peerAllowedPieces ) {
            const struct in_addr *peerAddr = tr_peerIoGetAddress( m->io, NULL );
            
            m->peerAllowedPieces = tr_peerMgrGenerateAllowedSet( MAX_FAST_ALLOWED_COUNT,
                                                                 m->torrent->info.pieceCount,
                                                                 m->torrent->info.hash,
                                                                 peerAddr );
        }
        m->clientAllowedPieces = tr_bitfieldNew( m->torrent->info.pieceCount );
        
        m->clientSuggestedPieces = tr_bitfieldNew( m->torrent->info.pieceCount );
    }
    
    tr_peerIoSetTimeoutSecs( m->io, 150 ); /* timeout after N seconds of inactivity */
    tr_peerIoSetIOFuncs( m->io, canRead, didWrite, gotError, m );
    ratePulse( m );

    if ( tr_peerIoSupportsLTEP( m->io ) )
        sendLtepHandshake( m );

    if ( !tr_peerIoSupportsFEXT( m->io ) )
        sendBitfield( m );
    else {
        /* This peer is fastpeer-enabled, send it have-all or have-none if appropriate */
        uint32_t peerProgress;
        float completion = tr_cpPercentComplete( m->torrent->completion );
        if ( completion == 0.0f ) {
            sendFastHave( m, 0 );
        } else if ( completion == 1.0f ) {
            sendFastHave( m, 1 );
        } else {
            sendBitfield( m );
        }
        peerProgress = m->torrent->info.pieceCount * m->info->progress;
        
        if ( peerProgress < MAX_FAST_ALLOWED_COUNT )
            sendFastAllowedSet( m );
    }

    return m;
}

void
tr_peerMsgsFree( tr_peermsgs* msgs )
{
    if( msgs != NULL )
    {
        tr_timerFree( &msgs->pulseTimer );
        tr_timerFree( &msgs->rateTimer );
        tr_timerFree( &msgs->pexTimer );
        tr_publisherFree( &msgs->publisher );
        tr_list_free( &msgs->clientWillAskFor, tr_free );
        tr_list_free( &msgs->clientAskedFor, tr_free );
        tr_list_free( &msgs->peerAskedForFast, tr_free );
        tr_list_free( &msgs->peerAskedFor, tr_free );
        tr_bitfieldFree( msgs->peerAllowedPieces );
        tr_bitfieldFree( msgs->clientAllowedPieces );
        tr_bitfieldFree( msgs->clientSuggestedPieces );
        evbuffer_free( msgs->incoming.block );
        evbuffer_free( msgs->outMessages );
        evbuffer_free( msgs->outBlock );
        tr_free( msgs->pex );

        memset( msgs, ~0, sizeof( tr_peermsgs ) );
        tr_free( msgs );
    }
}

tr_publisher_tag
tr_peerMsgsSubscribe( tr_peermsgs       * peer,
                      tr_delivery_func    func,
                      void              * userData )
{
    return tr_publisherSubscribe( peer->publisher, func, userData );
}

void
tr_peerMsgsUnsubscribe( tr_peermsgs       * peer,
                        tr_publisher_tag    tag )
{
    tr_publisherUnsubscribe( peer->publisher, tag );
}

int
tr_peerMsgsIsPieceFastAllowed( const tr_peermsgs * peer,
                               uint32_t            index )
{
    return tr_bitfieldHas( peer->clientAllowedPieces, index );
}

int
tr_peerMsgsIsPieceSuggested( const tr_peermsgs * peer,
                             uint32_t            index )
{
    return tr_bitfieldHas( peer->clientSuggestedPieces, index );
}

