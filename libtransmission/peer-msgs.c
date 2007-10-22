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

#include <arpa/inet.h>

#include <sys/types.h> /* event.h needs this */
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
#include "trevent.h"
#include "utils.h"

/**
***
**/

#define MAX_ALLOWED_SET_COUNT   10 /* number of pieces generated for allow-fast,
                                      threshold for fast-allowing others */

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

    OUR_LTEP_PEX            = 1,

    MAX_REQUEST_BYTE_COUNT  = (16 * 1024), /* drop requests who want too much */

    KEEPALIVE_INTERVAL_SECS = 90,          /* idle seconds before we send a keepalive */
    PEX_INTERVAL            = (60 * 1000), /* msec between calls to sendPex() */
    PEER_PULSE_INTERVAL     = (133),        /* msec between calls to pulse() */
    RATE_PULSE_INTERVAL     = (333),       /* msec between calls to ratePulse() */
};

enum
{
    AWAITING_BT_LENGTH,
    AWAITING_BT_MESSAGE,
    READING_BT_PIECE
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
    struct peer_request * a = (struct peer_request*) va;
    struct peer_request * b = (struct peer_request*) vb;
    if( a->index != b->index ) return a->index - b->index;
    if( a->offset != b->offset ) return a->offset - b->offset;
    if( a->length != b->length ) return a->length - b->length;
    return 0;
}

struct tr_peermsgs
{
    tr_peer * info;

    tr_handle * handle;
    tr_torrent * torrent;
    tr_peerIo * io;

    tr_publisher_t * publisher;

    struct evbuffer * outMessages; /* buffer of all the non-piece messages */
    struct evbuffer * inBlock;     /* the block we're currently receiving */
    tr_list * peerAskedFor;
    tr_list * clientAskedFor;
    tr_list * clientWillAskFor;

    tr_timer * rateTimer;
    tr_timer * pulseTimer;
    tr_timer * pexTimer;

    struct peer_request blockToUs; /* the block currntly being sent to us */

    time_t lastReqAddedAt;
    time_t clientSentPexAt;
    time_t clientSentAnythingAt;

    unsigned int notListening             : 1;
    unsigned int peerSupportsPex          : 1;
    unsigned int clientSentLtepHandshake  : 1;
    unsigned int peerSentLtepHandshake    : 1;
    
    tr_bitfield * clientAllowedPieces;
    tr_bitfield * peerAllowedPieces;
    
    uint8_t state;
    uint8_t ut_pex_id;
    uint16_t pexCount;
    uint32_t incomingMessageLength;
    uint32_t maxActiveRequests;
    uint32_t minActiveRequests;

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

        evbuffer_add_printf( buf, "[%s] %s [%s]: ",
                             tr_getLogTimeStr( timestr, sizeof(timestr) ),
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

static void
protocolSendPiece( tr_peermsgs                * msgs,
                   const struct peer_request  * r,
                   const uint8_t              * pieceData )
{
    tr_peerIo * io = msgs->io;
    struct evbuffer * out = evbuffer_new( );

    dbgmsg( msgs, "sending block %u:%u->%u", r->index, r->offset, r->length );
    tr_peerIoWriteUint32( io, out, sizeof(uint8_t) + 2*sizeof(uint32_t) + r->length );
    tr_peerIoWriteUint8 ( io, out, BT_PIECE );
    tr_peerIoWriteUint32( io, out, r->index );
    tr_peerIoWriteUint32( io, out, r->offset );
    tr_peerIoWriteBytes ( io, out, pieceData, r->length );
    tr_peerIoWriteBuf   ( io, out );

    evbuffer_free( out );
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
fireGotBlock( tr_peermsgs * msgs, uint32_t pieceIndex, uint32_t offset, uint32_t length )
{
    tr_peermsgs_event e = blankEvent;
    e.eventType = TR_PEERMSG_CLIENT_BLOCK;
    e.pieceIndex = pieceIndex;
    e.offset = offset;
    e.length = length;
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
        {
            tr_list * walk;
            for( walk = msgs->peerAskedFor; walk != NULL; )
            {
                tr_list * next = walk->next;
                /* don't reject a peer's fast allowed requests at choke */
                struct peer_request *req = walk->data;
                if ( !tr_bitfieldHas( msgs->peerAllowedPieces, req->index ) )
                {
                    tr_list_remove_data( &msgs->peerAskedFor, req );
                    tr_free( req );
                }
                walk = next;
            }
        }

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
    dbgmsg( msgs, "w00t SUGGESTing them piece #%d", pieceIndex );
    tr_peerIoWriteUint32( msgs->io, msgs->outMessages, sizeof(uint8_t) + sizeof(uint32_t) );
    tr_peerIoWriteUint8( msgs->io, msgs->outMessages, BT_SUGGEST );
    tr_peerIoWriteUint32( msgs->io, msgs->outMessages, pieceIndex );
    
    updateInterest( msgs );
}
#endif
static void
sendFastHave( tr_peermsgs * msgs,
              int           all)
{
    dbgmsg( msgs, "w00t telling them we %s pieces", (all ? "HAVE_ALL" : "HAVE_NONE" ) );
    tr_peerIoWriteUint32( msgs->io, msgs->outMessages, sizeof(uint8_t) );
    tr_peerIoWriteUint8( msgs->io, msgs->outMessages, ( all ? BT_HAVE_ALL : BT_HAVE_NONE ) );
    
    updateInterest( msgs );
}

static void
sendFastReject( tr_peermsgs * msgs,
                uint32_t      pieceIndex,
                uint32_t      offset,
                uint32_t      length )
{
    assert( msgs != NULL );
    assert( length > 0 );
    
    /* reject the request */
    const uint32_t len = sizeof(uint8_t) + 3 * sizeof(uint32_t);
    tr_peerIoWriteUint32( msgs->io, msgs->outMessages, len );
    tr_peerIoWriteUint8( msgs->io, msgs->outMessages, BT_REJECT );
    tr_peerIoWriteUint32( msgs->io, msgs->outMessages, pieceIndex );
    tr_peerIoWriteUint32( msgs->io, msgs->outMessages, offset );
    tr_peerIoWriteUint32( msgs->io, msgs->outMessages, length );
}

static void
sendFastAllowed( tr_peermsgs * msgs,
                 uint32_t      pieceIndex)
{
    dbgmsg( msgs, "w00t telling them we ALLOW_FAST piece #%d", pieceIndex );
    tr_peerIoWriteUint32( msgs->io, msgs->outMessages, sizeof(uint8_t) + sizeof(uint32_t) );
    tr_peerIoWriteUint8( msgs->io, msgs->outMessages, BT_ALLOWED_FAST );
    tr_peerIoWriteUint32( msgs->io, msgs->outMessages, pieceIndex );
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
reqIsValid( const tr_peermsgs * msgs, uint32_t index, uint32_t offset, uint32_t length )
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
requestIsValid( const tr_peermsgs * msgs, struct peer_request * req )
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
        struct peer_request * req = tr_list_pop_front( &msgs->clientWillAskFor );
        protocolSendRequest( msgs, req );
        req->time_requested = msgs->lastReqAddedAt = time( NULL );
        tr_list_append( &msgs->clientAskedFor, req );
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
tr_peerMsgsCancelAllRequests( tr_peermsgs * msgs )
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
        tr_bencInitInt( tr_bencDictAdd( m, "ut_pex" ), OUR_LTEP_PEX );
    }
    if( port > 0 )
        tr_bencInitInt( tr_bencDictAdd( &val, "p" ), port );
    tr_bencInitStr( tr_bencDictAdd( &val, "v" ), v, 0, 1 );
    buf = tr_bencSaveMalloc( &val,  &len );

    tr_peerIoWriteUint32( msgs->io, outbuf, 2*sizeof(uint8_t) + len );
    tr_peerIoWriteUint8 ( msgs->io, outbuf, BT_LTEP );
    tr_peerIoWriteUint8 ( msgs->io, outbuf, LTEP_HANDSHAKE );
    tr_peerIoWriteBytes ( msgs->io, outbuf, buf, len );

    tr_peerIoWriteBuf( msgs->io, outbuf );

#if 0
    dbgmsg( msgs, "here is the ltep handshake we sent:" );
    tr_bencPrint( &val );
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
#endif

    /* does the peer prefer encrypted connections? */
    sub = tr_bencDictFind( &val, "e" );
    if( tr_bencIsInt( sub ) )
        msgs->info->encryption_preference = sub->val.i
                                      ? ENCRYPTION_PREFERENCE_YES
                                      : ENCRYPTION_PREFERENCE_NO;

    /* check supported messages for utorrent pex */
    sub = tr_bencDictFind( &val, "m" );
    if( tr_bencIsDict( sub ) ) {
        sub = tr_bencDictFind( sub, "ut_pex" );
        if( tr_bencIsInt( sub ) ) {
            msgs->peerSupportsPex = 1;
            msgs->ut_pex_id = (uint8_t) sub->val.i;
            dbgmsg( msgs, "msgs->ut_pex is %d", (int)msgs->ut_pex_id );
        }
    }

    /* get peer's listening port */
    sub = tr_bencDictFind( &val, "p" );
    if( tr_bencIsInt( sub ) ) {
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

    if( tr_bencLoad( tmp, msglen, &val, NULL ) || !tr_bencIsDict( &val ) ) {
        dbgmsg( msgs, "GET can't read extended-pex dictionary" );
        tr_free( tmp );
        return;
    }

    sub = tr_bencDictFind( &val, "added" );
    if( tr_bencIsStr(sub) && ((sub->val.s.i % 6) == 0)) {
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
        sendLtepHandshake( msgs );
        sendPex( msgs );
    }
    else if( ltep_msgid == msgs->ut_pex_id )
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
readBtLength( tr_peermsgs * msgs, struct evbuffer * inbuf )
{
    uint32_t len;
    const size_t needlen = sizeof(uint32_t);

    if( EVBUFFER_LENGTH(inbuf) < needlen )
        return READ_MORE;

    tr_peerIoReadUint32( msgs->io, inbuf, &len );

    if( len == 0 ) /* peer sent us a keepalive message */
        dbgmsg( msgs, "got KeepAlive" );
    else {
        msgs->incomingMessageLength = len;
        msgs->state = AWAITING_BT_MESSAGE;
    }

    return READ_AGAIN;
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
readBtMessage( tr_peermsgs * msgs, struct evbuffer * inbuf )
{
    uint8_t id;
    uint32_t ui32;
    uint32_t msglen = msgs->incomingMessageLength;

    if( EVBUFFER_LENGTH(inbuf) < msglen )
        return READ_MORE;

    tr_peerIoReadUint8( msgs->io, inbuf, &id );
    msglen--;
    dbgmsg( msgs, "got BT id %d, len %d", (int)id, (int)msglen );

    switch( id )
    {
        case BT_CHOKE:
            dbgmsg( msgs, "got Choke" );
            assert( msglen == 0 );
            msgs->info->clientIsChoked = 1;
#if 0
            tr_list * walk;
            for( walk = msgs->peerAskedFor; walk != NULL; )
            {
                tr_list * next = walk->next;
                /* We shouldn't reject a peer's fast allowed requests at choke */
                struct peer_request *req = walk->data;
                if ( !tr_bitfieldHas( msgs->peerAllowedPieces, req->index ) )
                {
                    tr_list_remove_data( &msgs->peerAskedFor, req );
                    tr_free( req );
                }
                walk = next;
            }
#endif
            tr_peerMsgsCancelAllRequests( msgs );
            break;

        case BT_UNCHOKE:
            dbgmsg( msgs, "got Unchoke" );
            assert( msglen == 0 );
            msgs->info->clientIsChoked = 0;
            fireNeedReq( msgs );
            break;

        case BT_INTERESTED:
            dbgmsg( msgs, "got Interested" );
            assert( msglen == 0 );
            msgs->info->peerIsInterested = 1;
            tr_peerMsgsSetChoke( msgs, 0 );
            break;

        case BT_NOT_INTERESTED:
            dbgmsg( msgs, "got Not Interested" );
            assert( msglen == 0 );
            msgs->info->peerIsInterested = 0;
            break;

        case BT_HAVE:
            assert( msglen == 4 );
            tr_peerIoReadUint32( msgs->io, inbuf, &ui32 );
            tr_bitfieldAdd( msgs->info->have, ui32 );
            updatePeerProgress( msgs );
            tr_rcTransferred( msgs->torrent->swarmspeed, msgs->torrent->info.pieceSize );
            dbgmsg( msgs, "got Have: %u", ui32 );
            break;

        case BT_BITFIELD: {
            const int clientIsSeed = tr_torrentIsSeed( msgs->torrent );
            dbgmsg( msgs, "got a bitfield" );
            assert( msglen == msgs->info->have->len );
            tr_peerIoReadBytes( msgs->io, inbuf, msgs->info->have->bits, msglen );
            updatePeerProgress( msgs );
            tr_peerMsgsSetChoke( msgs, !clientIsSeed || (msgs->info->progress<1.0) );
            fireNeedReq( msgs );
            break;
        }

        case BT_REQUEST: {
            struct peer_request * req;
            assert( msglen == 12 );
            req = tr_new( struct peer_request, 1 );
            tr_peerIoReadUint32( msgs->io, inbuf, &req->index );
            tr_peerIoReadUint32( msgs->io, inbuf, &req->offset );
            tr_peerIoReadUint32( msgs->io, inbuf, &req->length );
            dbgmsg( msgs, "got Request: %u:%u->%u", req->index, req->offset, req->length );
            
            if ( !requestIsValid( msgs, req ) )
            {
                dbgmsg( msgs, "BT_REQUEST: invalid request, ignoring" );
                tr_free( req );
                break;
            }
            /* 
                If we're not choking him -> continue
                If we're choking him
                    it doesn't support FPE -> He's deaf, reCHOKE and bail...
                    it support FPE
                        If the asked piece is not allowed
                            OR he's above our threshold
                            OR we don't have the requested piece -> Reject
                        Else
                        Asked piece allowed AND he's below our threshold -> continue...
             */
    

            if ( msgs->info->peerIsChoked )
            {
                if ( !tr_peerIoSupportsFEXT( msgs->io ) )
                {
                    dbgmsg( msgs, "BT_REQUEST: peer is choked, ignoring" );
                    /* Didn't he get it? */
                    tr_peerMsgsSetChoke( msgs, 1 );
                    tr_free( req );
                    break;
                }
                else
                {
                    if ( !tr_bitfieldHas( msgs->peerAllowedPieces, req->index )
                         || ( msgs->info->progress * (float)msgs->torrent->info.pieceCount) >= MAX_ALLOWED_SET_COUNT
                         || !tr_cpPieceIsComplete( msgs->torrent->completion, req->index ) )
                    {
                        dbgmsg( msgs, "BT_REQUEST: peer requests an un-fastallowed piece" );
                        sendFastReject( msgs, req->index, req->offset, req->length );
                        tr_free( req );
                        break;
                    }
                    dbgmsg( msgs, "BT_REQUEST: fast allowed piece, accepting request" );
                }    
            }
            
            tr_list_append( &msgs->peerAskedFor, req );
            break;
        }

        case BT_CANCEL: {
            struct peer_request req;
            void * data;
            assert( msglen == 12 );
            tr_peerIoReadUint32( msgs->io, inbuf, &req.index );
            tr_peerIoReadUint32( msgs->io, inbuf, &req.offset );
            tr_peerIoReadUint32( msgs->io, inbuf, &req.length );
            dbgmsg( msgs, "got a Cancel %u:%u->%u", req.index, req.offset, req.length );
            data = tr_list_remove( &msgs->peerAskedFor, &req, compareRequest );
            tr_free( data );
            break;
        }

        case BT_PIECE: {
            dbgmsg( msgs, "got a Piece!" );
            assert( msgs->blockToUs.length == 0 );
            tr_peerIoReadUint32( msgs->io, inbuf, &msgs->blockToUs.index );
            tr_peerIoReadUint32( msgs->io, inbuf, &msgs->blockToUs.offset );
            msgs->blockToUs.length = msglen - 8;
            assert( EVBUFFER_LENGTH(msgs->inBlock) == 0 );
            msgs->state = msgs->blockToUs.length ? READING_BT_PIECE : AWAITING_BT_LENGTH;
            return READ_AGAIN;
            break;
        }

        case BT_PORT: {
            dbgmsg( msgs, "Got a BT_PORT" );
            assert( msglen == 2 );
            tr_peerIoReadUint16( msgs->io, inbuf, &msgs->info->port );
            break;
        }
        
        case BT_SUGGEST: {
            /* tiennou TODO */
            break;
        }
            
        case BT_HAVE_ALL: {
            assert( msglen == 0 );
            dbgmsg( msgs, "Got a BT_HAVE_ALL" );
            memset( msgs->info->have->bits, 1, msgs->info->have->len );
            updatePeerProgress( msgs );
            break;
        }
            
        case BT_HAVE_NONE: {
            assert( msglen == 0 );
            dbgmsg( msgs, "Got a BT_HAVE_NONE" );
            tr_bitfieldClear( msgs->info->have );
            updatePeerProgress( msgs );
            break;
        }
            
        case BT_REJECT: {
            struct peer_request req;
            tr_list * node;
            assert( msglen == 12 );
            dbgmsg( msgs, "Got a BT_REJECT" );
            tr_peerIoReadUint32( msgs->io, inbuf, &req.index );
            tr_peerIoReadUint32( msgs->io, inbuf, &req.offset );
            tr_peerIoReadUint32( msgs->io, inbuf, &req.length );
            node = tr_list_find( msgs->peerAskedFor, &req, compareRequest );
            if( node != NULL ) {
                void * data = node->data;
                tr_list_remove_data( &msgs->peerAskedFor, data );
                tr_free( data );
                dbgmsg( msgs, "found the req that peer has rejected... cancelled." );
            }
            break;
        }
            
        case BT_ALLOWED_FAST: {
            assert( msglen == 4 );
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
            assert( 0 );
    }

    msgs->incomingMessageLength = -1;
    msgs->state = AWAITING_BT_LENGTH;
    return READ_AGAIN;
}

static void
clientGotBytes( tr_peermsgs * msgs, uint32_t byteCount )
{
    tr_torrent * tor = msgs->torrent;
    tor->activityDate = tr_date( );
    tor->downloadedCur += byteCount;
    msgs->info->pieceDataActivityDate = time( NULL );
    tr_rcTransferred( msgs->info->rcToClient, byteCount );
    tr_rcTransferred( tor->download, byteCount );
    tr_rcTransferred( tor->handle->download, byteCount );
}

static void
peerGotBytes( tr_peermsgs * msgs, uint32_t byteCount )
{
    tr_torrent * tor = msgs->torrent;
    tor->activityDate = tr_date( );
    tor->uploadedCur += byteCount;
    msgs->info->pieceDataActivityDate = time( NULL );
    tr_rcTransferred( msgs->info->rcToPeer, byteCount );
    tr_rcTransferred( tor->upload, byteCount );
    tr_rcTransferred( tor->handle->upload, byteCount );
}

static int
canDownload( const tr_peermsgs * msgs )
{
    tr_torrent * tor = msgs->torrent;

    if( tor->downloadLimitMode == TR_SPEEDLIMIT_GLOBAL )
        return !tor->handle->useDownloadLimit || tr_rcCanTransfer( tor->handle->download );

    if( tor->downloadLimitMode == TR_SPEEDLIMIT_SINGLE )
        return tr_rcCanTransfer( tor->download );

    return TRUE;
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
    const uint32_t byteCount = tr_torPieceCountBytes( msgs->torrent, (int)pieceIndex );
    reassignBytesToCorrupt( msgs, byteCount );
}

static void
gotUnwantedBlock( tr_peermsgs * msgs,
                  uint32_t      index UNUSED,
                  uint32_t      offset UNUSED,
                  uint32_t      length )
{
    reassignBytesToCorrupt( msgs, length );
}

static void
addUsToBlamefield( tr_peermsgs * msgs, uint32_t index )
{
    if( !msgs->info->blame )
         msgs->info->blame = tr_bitfieldNew( msgs->torrent->info.pieceCount );
    tr_bitfieldAdd( msgs->info->blame, index );
}

static void
gotBlock( tr_peermsgs      * msgs,
          struct evbuffer  * inbuf,
          uint32_t           index,
          uint32_t           offset,
          uint32_t           length )
{
    tr_torrent * tor = msgs->torrent;
    const int block = _tr_block( tor, index, offset );
    struct peer_request key, *req;

    /**
    *** Remove the block from our `we asked for this' list
    **/

    key.index = index;
    key.offset = offset;
    key.length = length;
    req = (struct peer_request*) tr_list_remove( &msgs->clientAskedFor, &key,
                                                 compareRequest );
    if( req == NULL ) {
        gotUnwantedBlock( msgs, index, offset, length );
        dbgmsg( msgs, "we didn't ask for this message..." );
        return;
    }
    dbgmsg( msgs, "Got block %u:%u->%u (turnaround time %d secs)", 
                     req->index, req->offset, req->length,
                     (int)(time(NULL) - req->time_requested) );
    tr_free( req );
    dbgmsg( msgs, "peer has %d more blocks we've asked for",
                  tr_list_size(msgs->clientAskedFor));

    /**
    *** Error checks
    **/

    if( tr_cpBlockIsComplete( tor->completion, block ) ) {
        dbgmsg( msgs, "have this block already..." );
        tr_dbg( "have this block already..." );
        gotUnwantedBlock( msgs, index, offset, length );
        return;
    }

    if( (int)length != tr_torBlockCountBytes( tor, block ) ) {
        dbgmsg( msgs, "block is the wrong length..." );
        tr_dbg( "block is the wrong length..." );
        gotUnwantedBlock( msgs, index, offset, length );
        return;
    }

    /**
    ***  Write the block
    **/

    if( tr_ioWrite( tor, index, offset, length, EVBUFFER_DATA( inbuf )))
        return;

    tr_cpBlockAdd( tor->completion, block );

    addUsToBlamefield( msgs, index );

    fireGotBlock( msgs, index, offset, length );

    /**
    ***  Handle if this was the last block in the piece
    **/

    if( tr_cpPieceIsComplete( tor->completion, index ) )
    {
        if( tr_ioHash( tor, index ) )
        {
            gotBadPiece( msgs, index );
            return;
        }

        fireClientHave( msgs, index );
    }
}


static ReadState
readBtPiece( tr_peermsgs * msgs, struct evbuffer * inbuf )
{
    uint32_t inlen;
    uint8_t * tmp;

    assert( msgs != NULL );
    assert( msgs->blockToUs.length > 0 );
    assert( inbuf != NULL );
    assert( EVBUFFER_LENGTH( inbuf ) > 0 );

    /* read from the inbuf into our block buffer */
    inlen = MIN( EVBUFFER_LENGTH(inbuf), msgs->blockToUs.length );
    tmp = tr_new( uint8_t, inlen );
    tr_peerIoReadBytes( msgs->io, inbuf, tmp, inlen );
    evbuffer_add( msgs->inBlock, tmp, inlen );

    /* update our tables accordingly */
    assert( inlen >= msgs->blockToUs.length );
    msgs->blockToUs.length -= inlen;
    msgs->info->peerSentPieceDataAt = time( NULL );
    clientGotBytes( msgs, inlen );

    /* if this was the entire block, save it */
    if( !msgs->blockToUs.length )
    {
        dbgmsg( msgs, "got block %u:%u", msgs->blockToUs.index, msgs->blockToUs.offset );
        assert( (int)EVBUFFER_LENGTH( msgs->inBlock ) == tr_torBlockCountBytes( msgs->torrent, _tr_block(msgs->torrent,msgs->blockToUs.index, msgs->blockToUs.offset) ) );
        gotBlock( msgs, msgs->inBlock,
                        msgs->blockToUs.index,
                        msgs->blockToUs.offset,
                        EVBUFFER_LENGTH( msgs->inBlock ) );
        evbuffer_drain( msgs->inBlock, ~0 );
        msgs->state = AWAITING_BT_LENGTH;
    }

    /* cleanup */
    tr_free( tmp );
    return READ_AGAIN;
}

static ReadState
canRead( struct bufferevent * evin, void * vmsgs )
{
    ReadState ret;
    tr_peermsgs * msgs = (tr_peermsgs *) vmsgs;
    struct evbuffer * inbuf = EVBUFFER_INPUT ( evin );

    if( !canDownload( msgs ) )
    {
        msgs->notListening = 1;
        tr_peerIoSetIOMode ( msgs->io, 0, EV_READ );
        ret = READ_DONE;
    }
    else switch( msgs->state )
    {
        case AWAITING_BT_LENGTH:  ret = readBtLength  ( msgs, inbuf ); break;
        case AWAITING_BT_MESSAGE: ret = readBtMessage ( msgs, inbuf ); break;
        case READING_BT_PIECE:    ret = readBtPiece   ( msgs, inbuf ); break;
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
canWrite( const tr_peermsgs * msgs )
{
    /* don't let our outbuffer get too large */
    if( tr_peerIoWriteBytesWaiting( msgs->io ) > 8192 )
        return FALSE;

    return TRUE;
}

static int
canUpload( const tr_peermsgs * msgs )
{
    const tr_torrent * tor = msgs->torrent;

    if( !canWrite( msgs ) )
        return FALSE;

    if( tor->uploadLimitMode == TR_SPEEDLIMIT_GLOBAL )
        return !tor->handle->useUploadLimit || tr_rcCanTransfer( tor->handle->upload );

    if( tor->uploadLimitMode == TR_SPEEDLIMIT_SINGLE )
        return tr_rcCanTransfer( tor->upload );

    return TRUE;
}

static int
ratePulse( void * vmsgs )
{
    tr_peermsgs * msgs = (tr_peermsgs *) vmsgs;
    msgs->info->rateToClient = tr_rcRate( msgs->info->rcToClient );
    msgs->info->rateToPeer = tr_rcRate( msgs->info->rcToPeer );
    msgs->maxActiveRequests = MIN( 8 + (int)(msgs->info->rateToClient/10), 100 );
    msgs->minActiveRequests = msgs->maxActiveRequests / 2;
    return TRUE;
}

static int
pulse( void * vmsgs )
{
    const time_t now = time( NULL );
    tr_peermsgs * msgs = (tr_peermsgs *) vmsgs;
    size_t len;

    /* if we froze out a downloaded block because of speed limits,
       start listening to the peer again */
    if( msgs->notListening && canDownload( msgs ) )
    {
        msgs->notListening = 0;
        tr_peerIoSetIOMode ( msgs->io, EV_READ, 0 );
    }

    pumpRequestQueue( msgs );

    if( !canWrite( msgs ) )
    {
    }
    else if(( len = EVBUFFER_LENGTH( msgs->outMessages ) ))
    {
        tr_peerIoWriteBuf( msgs->io, msgs->outMessages );
        msgs->clientSentAnythingAt = now;
    }
    else if(( msgs->peerAskedFor ))
    {
        if( canUpload( msgs ) )
        {
            struct peer_request * r = tr_list_pop_front( &msgs->peerAskedFor );
            uint8_t * buf = tr_new( uint8_t, r->length );

            if( requestIsValid( msgs, r )
                && tr_cpPieceIsComplete( msgs->torrent->completion, r->index )
                && !tr_ioRead( msgs->torrent, r->index, r->offset, r->length, buf ) )
            {
                protocolSendPiece( msgs, r, buf );
                peerGotBytes( msgs, r->length );
                msgs->clientSentAnythingAt = now;
            }

            tr_free( buf );
            tr_free( r );
        }
    }
    else if( ( now - msgs->clientSentAnythingAt ) > KEEPALIVE_INTERVAL_SECS )
    {
        sendKeepalive( msgs );
    }

    return TRUE; /* loop forever */
}

static void
didWrite( struct bufferevent * evin UNUSED, void * vmsgs )
{
    pulse( vmsgs );
}

static void
gotError( struct bufferevent * evbuf UNUSED, short what, void * vmsgs )
{
    dbgmsg( vmsgs, "libevent got an error! what=%d, errno=%d (%s)",
            (int)what, errno, strerror(errno) );
    fireGotError( vmsgs );
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
        benc = tr_bencSaveMalloc( &val, &bencLen );
        tr_peerIoWriteUint32( msgs->io, msgs->outMessages, 2*sizeof(uint8_t) + bencLen );
        tr_peerIoWriteUint8 ( msgs->io, msgs->outMessages, BT_LTEP );
        tr_peerIoWriteUint8 ( msgs->io, msgs->outMessages, OUR_LTEP_PEX );
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
    m->info->clientIsChoked = 1;
    m->info->peerIsChoked = 1;
    m->info->clientIsInterested = 0;
    m->info->peerIsInterested = 0;
    m->info->have = tr_bitfieldNew( torrent->info.pieceCount );
    m->pulseTimer = tr_timerNew( m->handle, pulse, m, PEER_PULSE_INTERVAL );
    m->rateTimer = tr_timerNew( m->handle, ratePulse, m, RATE_PULSE_INTERVAL );
    m->pexTimer = tr_timerNew( m->handle, pexPulse, m, PEX_INTERVAL );
    m->outMessages = evbuffer_new( );
    m->inBlock = evbuffer_new( );
    m->peerAllowedPieces = NULL;
    m->clientAllowedPieces = NULL;
    *setme = tr_publisherSubscribe( m->publisher, func, userData );
    
    if ( tr_peerIoSupportsFEXT( m->io ) )
    {
        /* This peer is fastpeer-enabled, generate its allowed set
         * (before registering our callbacks) */
        if ( !m->peerAllowedPieces ) {
            const struct in_addr *peerAddr = tr_peerIoGetAddress( m->io, NULL );
            
            m->peerAllowedPieces = tr_peerMgrGenerateAllowedSet( MAX_ALLOWED_SET_COUNT,
                                                                 m->torrent->info.pieceCount,
                                                                 m->torrent->info.hash,
                                                                 peerAddr );
        }
        m->clientAllowedPieces = tr_bitfieldNew( m->torrent->info.pieceCount );
    }
    
    tr_peerIoSetTimeoutSecs( m->io, 150 ); /* error if we don't read or write for 2.5 minutes */
    tr_peerIoSetIOFuncs( m->io, canRead, didWrite, gotError, m );
    tr_peerIoSetIOMode( m->io, EV_READ|EV_WRITE, 0 );
    ratePulse( m );

    /**
    ***  If we initiated this connection,
    ***  we may need to send LTEP/AZMP handshakes.
    ***  Otherwise we'll wait for the peer to send theirs first.
    **/
    if( !tr_peerIoIsIncoming( m->io ) )
    {
        if ( tr_peerIoSupportsLTEP( m->io ) ) {
            sendLtepHandshake( m );
            
        } else if ( tr_peerIoSupportsAZMP( m->io ) ) {
            dbgmsg( m, "FIXME: need to send AZMP handshake" );
            
        } else {
            /* no-op */
        }
    }
    
    if ( tr_peerIoSupportsFEXT( m->io ) )
    {
        /* This peer is fastpeer-enabled, send it have-all or have-none if appropriate */
        float completion = tr_cpPercentComplete( m->torrent->completion );
        if ( completion == 0.0f ) {
            sendFastHave( m, 0 );
        } else if ( completion == 1.0f ) {
            sendFastHave( m, 1 );
        } else {
            sendBitfield( m );
        }
        uint32_t peerProgress = m->torrent->info.pieceCount * m->info->progress;
        
        if ( peerProgress < MAX_ALLOWED_SET_COUNT )
            sendFastAllowedSet( m );
    } else {
        sendBitfield( m );
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
        tr_list_free( &msgs->peerAskedFor, tr_free );
        evbuffer_free( msgs->outMessages );
        evbuffer_free( msgs->inBlock );
        tr_free( msgs->pex );
        msgs->pexCount = 0;
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
tr_peerMsgIsPieceFastAllowed( const tr_peermsgs * peer,
                              uint32_t            index )
{
    return tr_bitfieldHas( peer->clientAllowedPieces, index );
}
