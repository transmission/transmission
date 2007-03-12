/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2006 Transmission authors and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *****************************************************************************/

#include "transmission.h"

#define MAX_REQUEST_COUNT 32
#define OUR_REQUEST_COUNT 8  /* TODO: we should detect if we are on a
                                high-speed network and adapt */

typedef struct tr_request_s
{
    int index;
    int begin;
    int length;

} tr_request_t;

struct tr_peer_s
{
    tr_torrent_t      * tor;

    struct in_addr      addr;
    in_port_t           port;  /* peer's listening port, 0 if not known */

#define PEER_STATUS_IDLE        1 /* Need to connect */
#define PEER_STATUS_CONNECTING  2 /* Trying to send handshake */
#define PEER_STATUS_HANDSHAKE   4 /* Waiting for peer's handshake */
#define PEER_STATUS_CONNECTED   8 /* Got peer's handshake */
    int                 status;
    int                 socket;
    char                incoming;
    uint64_t            date;
    uint64_t            keepAlive;

    char                amChoking;
    char                amInterested;
    char                peerChoking;
    char                peerInterested;

    int                 optimistic;
    uint64_t            lastChoke;

    uint8_t             id[20];

    /* The pieces that the peer has */
    tr_bitfield_t     * bitfield;
    int                 pieceCount;
    float               progress;

    int                 goodPcs;
    int                 badPcs;
    int                 banned;
    /* The pieces that the peer is contributing to */
    tr_bitfield_t     * blamefield;
    /* The bad pieces that the peer has contributed to */
    tr_bitfield_t     * banfield;

    uint8_t           * buf;
    int                 size;
    int                 pos;

    uint8_t           * outMessages;
    int                 outMessagesSize;
    int                 outMessagesPos;
    uint8_t             outBlock[13+16384];
    int                 outBlockSize;
    int                 outBlockLoaded;
    int                 outBlockSending;

    int                 inRequestCount;
    tr_request_t        inRequests[OUR_REQUEST_COUNT];
    int                 inIndex;
    int                 inBegin;
    int                 inLength;
    uint64_t            inTotal;

    int                 outRequestCount;
    tr_request_t        outRequests[MAX_REQUEST_COUNT];
    uint64_t            outTotal;
    uint64_t            outDate;

    tr_ratecontrol_t  * download;
    tr_ratecontrol_t  * upload;
};

#define peer_dbg( a... ) __peer_dbg( peer, ## a )
static void __peer_dbg( tr_peer_t * peer, char * msg, ... ) PRINTF( 2, 3 );

static void __peer_dbg( tr_peer_t * peer, char * msg, ... )
{
    char    string[256];
    va_list args;

    va_start( args, msg );
    snprintf( string, sizeof string, "%08x:%04x ",
             (uint32_t) peer->addr.s_addr, peer->port );
    vsnprintf( &string[14], sizeof( string ) - 14, msg, args );
    va_end( args ); 

    tr_dbg( "%s", string );
}

#include "peermessages.h"
#include "peerutils.h"
#include "peerparse.h"

/***********************************************************************
 * tr_peerInit
 ***********************************************************************
 * Initializes a new peer.
 **********************************************************************/
tr_peer_t * tr_peerInit( struct in_addr addr, in_port_t port, int s )
{
    tr_peer_t * peer = peerInit();

    peer->socket = s;
    peer->addr = addr;
    peer->port = port;
    if( s >= 0 )
    {
        peer->status = PEER_STATUS_CONNECTING;
        peer->incoming = 1;
    }
    else
    {
        peer->status = PEER_STATUS_IDLE;
    }

    return peer;
}

void tr_peerDestroy( tr_peer_t * peer )
{
    tr_torrent_t * tor = peer->tor;
    tr_request_t * r;
    int i, block;

    for( i = 0; i < peer->inRequestCount; i++ )
    {
        r = &peer->inRequests[i];
        block = tr_block( r->index, r->begin );
        tr_cpDownloaderRem( tor->completion, block );
    }
    tr_bitfieldFree( peer->bitfield );
    tr_bitfieldFree( peer->blamefield );
    tr_bitfieldFree( peer->banfield );
    if( peer->buf )
    {
        free( peer->buf );
    }
    if( peer->outMessages )
    {
        free( peer->outMessages );
    }
    if( peer->status > PEER_STATUS_IDLE )
    {
        tr_netClose( peer->socket );
    }
    tr_rcClose( peer->download );
    tr_rcClose( peer->upload );
    free( peer );
}

void tr_peerSetTorrent( tr_peer_t * peer, tr_torrent_t * tor )
{
    peer->tor = tor;
}

/***********************************************************************
 * tr_peerRead
 ***********************************************************************
 *
 **********************************************************************/
int tr_peerRead( tr_peer_t * peer )
{
    tr_torrent_t * tor = peer->tor;
    int ret;

    /* Try to read */
    for( ;; )
    {
        if( tor )
        {
            if( tor->customDownloadLimit )
            {
                if( !tr_rcCanTransfer( tor->download ) )
                {
                    break;
                }
            }
            else
            {
                tr_lockUnlock( &tor->lock );
                if( !tr_rcCanGlobalTransfer( tor->handle, 0 ) )
                {
                    tr_lockLock( &tor->lock );
                    break;
                }
                tr_lockLock( &tor->lock );
            }
        }

        if( peer->size < 1 )
        {
            peer->size = 1024;
            peer->buf  = malloc( peer->size );
        }
        else if( peer->pos >= peer->size )
        {
            peer->size *= 2;
            peer->buf   = realloc( peer->buf, peer->size );
        }
        /* Never read more than 1K each time, otherwise the rate
           control is no use */
        ret = tr_netRecv( peer->socket, &peer->buf[peer->pos],
                          MIN( 1024, peer->size - peer->pos ) );
        if( ret & TR_NET_CLOSE )
        {
            peer_dbg( "connection closed" );
            return TR_ERROR;
        }
        else if( ret & TR_NET_BLOCK )
        {
            break;
        }
        peer->date  = tr_date();
        peer->pos  += ret;
        if( NULL != tor )
        {
            tr_rcTransferred( peer->download, ret );
            tr_rcTransferred( tor->download, ret );
            if( ( ret = parseBuf( tor, peer ) ) )
            {
                return ret;
            }
        }
        else
        {
            if( ( ret = parseBufHeader( peer ) ) )
            {
                return ret;
            }
        }
    }

    return TR_OK;
}

uint64_t tr_peerDate( tr_peer_t * peer )
{
    return peer->date;
}

/***********************************************************************
 * tr_peerId
 ***********************************************************************
 *
 **********************************************************************/
uint8_t * tr_peerId( tr_peer_t * peer )
{
    return & peer->id[0];
}

/***********************************************************************
 * tr_peerAddress
 ***********************************************************************
 * 
 **********************************************************************/
struct in_addr * tr_peerAddress( tr_peer_t * peer )
{
    return &peer->addr;
}

/***********************************************************************
 * tr_peerHash
 ***********************************************************************
 *
 **********************************************************************/
uint8_t * tr_peerHash( tr_peer_t * peer )
{
    return parseBufHash( peer );
}

/***********************************************************************
 * tr_peerPulse
 ***********************************************************************
 *
 **********************************************************************/
int tr_peerPulse( tr_peer_t * peer )
{
    tr_torrent_t * tor = peer->tor;
    int ret, size;
    uint8_t * p;

    if( ( ret = checkPeer( peer ) ) )
    {
        return ret;
    }

    /* Connect */
    if( peer->status & PEER_STATUS_IDLE )
    {
        peer->socket = tr_netOpenTCP( peer->addr, peer->port, 0 );
        if( peer->socket < 0 )
        {
            return TR_ERROR;
        }
        peer->status = PEER_STATUS_CONNECTING;
    }

    /* Try to send handshake */
    if( peer->status & PEER_STATUS_CONNECTING )
    {
        uint8_t buf[68];
        tr_info_t * inf = &tor->info;

        buf[0] = 19;
        memcpy( &buf[1], "BitTorrent protocol", 19 );
        memset( &buf[20], 0, 8 );
        memcpy( &buf[28], inf->hash, 20 );
        memcpy( &buf[48], tor->id, 20 );

        switch( tr_netSend( peer->socket, buf, 68 ) )
        {
            case 68:
                peer_dbg( "SEND handshake" );
                peer->status = PEER_STATUS_HANDSHAKE;
                break;
            case TR_NET_BLOCK:
                break;
            default:
                peer_dbg( "connection closed" );
                return TR_ERROR;
        }
    }
    if( peer->status < PEER_STATUS_HANDSHAKE )
    {
        /* Nothing more we can do till we sent the handshake */
        return TR_OK;
    }

    /* Read incoming messages */
    if( ( ret = tr_peerRead( peer ) ) )
    {
        return ret;
    }
    if( peer->status < PEER_STATUS_CONNECTED )
    {
        /* Nothing more we can do till we got the other guy's handshake */
        return TR_OK;
    }

    /* Try to write */
writeBegin:

    /* Send all smaller messages regardless of the upload cap */
    while( ( p = messagesPending( peer, &size ) ) )
    {
        ret = tr_netSend( peer->socket, p, size );
        if( ret & TR_NET_CLOSE )
        {
            return TR_ERROR;
        }
        else if( ret & TR_NET_BLOCK )
        {
            goto writeEnd;
        }
        messagesSent( peer, ret );
    }

    /* Send pieces if we can */
    while( ( p = blockPending( tor, peer, &size ) ) )
    {
        if( tor->customUploadLimit )
        {
            if( !tr_rcCanTransfer( tor->upload ) )
            {
                break;
            }
        }
        else
        {
            tr_lockUnlock( &tor->lock );
            if( !tr_rcCanGlobalTransfer( tor->handle, 1 ) )
            {
                tr_lockLock( &tor->lock );
                break;
            }
            tr_lockLock( &tor->lock );
        }

        ret = tr_netSend( peer->socket, p, size );
        if( ret & TR_NET_CLOSE )
        {
            return TR_ERROR;
        }
        else if( ret & TR_NET_BLOCK )
        {
            break;
        }

        blockSent( peer, ret );
        tr_rcTransferred( peer->upload, ret );
        tr_rcTransferred( tor->upload, ret );

        tor->uploadedCur += ret;
        peer->outTotal   += ret;
        peer->outDate     = tr_date();

        /* In case this block is done, you may have messages
           pending. Send them before we start the next block */
        goto writeBegin;
    }
writeEnd:

    /* Ask for a block whenever possible */
    if( !tr_cpIsSeeding( tor->completion ) &&
        !peer->amInterested && tor->peerCount > TR_MAX_PEER_COUNT - 2 )
    {
        /* This peer is no use to us, and it seems there are
           more */
        peer_dbg( "not interesting" );
        return TR_ERROR;
    }

    if( peer->amInterested && !peer->peerChoking && !peer->banned )
    {
        int block;
        while( peer->inRequestCount < OUR_REQUEST_COUNT )
        {
            block = chooseBlock( tor, peer );
            if( block < 0 )
            {
                break;
            }
            sendRequest( tor, peer, block );
        }
    }

    return TR_OK;
}

/***********************************************************************
 * tr_peerIsConnected
 ***********************************************************************
 *
 **********************************************************************/
int tr_peerIsConnected( tr_peer_t * peer )
{
    return peer->status & PEER_STATUS_CONNECTED;
}

/***********************************************************************
 * tr_peerIsIncoming
 ***********************************************************************
 *
 **********************************************************************/
int tr_peerIsIncoming( tr_peer_t * peer )
{
    return peer->incoming;
}

int tr_peerAmChoking( tr_peer_t * peer )
{
    return peer->amChoking;
}
int tr_peerAmInterested( tr_peer_t * peer )
{
    return peer->amInterested;
}
int tr_peerIsChoking( tr_peer_t * peer )
{
    return peer->peerChoking;
}
int tr_peerIsInterested( tr_peer_t * peer )
{
    return peer->peerInterested;
}

/***********************************************************************
 * tr_peerProgress
 ***********************************************************************
 *
 **********************************************************************/
float tr_peerProgress( tr_peer_t * peer )
{
    return peer->progress;
}

/***********************************************************************
 * tr_peerPort
 ***********************************************************************
 * Returns peer's listening port in host byte order
 **********************************************************************/
int tr_peerPort( tr_peer_t * peer )
{
    return ntohs( peer->port );
}

/***********************************************************************
 * tr_peerBitfield
 ***********************************************************************
 *
 **********************************************************************/
tr_bitfield_t * tr_peerBitfield( tr_peer_t * peer )
{
    return peer->bitfield;
}

float tr_peerDownloadRate( tr_peer_t * peer )
{
    return tr_rcRate( peer->download );
}

float tr_peerUploadRate( tr_peer_t * peer )
{
    return tr_rcRate( peer->upload );
}

void tr_peerChoke( tr_peer_t * peer )
{
    sendChoke( peer, 1 );
    peer->lastChoke = tr_date();
}

void tr_peerUnchoke( tr_peer_t * peer )
{
    sendChoke( peer, 0 );
    peer->lastChoke = tr_date();
}

uint64_t tr_peerLastChoke( tr_peer_t * peer )
{
    return peer->lastChoke;
}

void tr_peerSetOptimistic( tr_peer_t * peer, int o )
{
    peer->optimistic = o;
}

int tr_peerIsOptimistic( tr_peer_t * peer )
{
    return peer->optimistic;
}

static inline int peerIsBad( tr_peer_t * peer )
{
    return ( peer->badPcs > 4 + 2 * peer->goodPcs );
}

static inline int peerIsGood( tr_peer_t * peer )
{
    return ( peer->goodPcs > 3 * peer->badPcs );
}

void tr_peerBlame( tr_peer_t * peer, int piece, int success )
{
    tr_torrent_t * tor = peer->tor;

    if( !peer->blamefield || !tr_bitfieldHas( peer->blamefield, piece ) )
    {
        return;
    }

    if( success )
    {
        peer->goodPcs++;

        if( peer->banfield && peerIsGood( peer ) )
        {
            /* Assume the peer wasn't responsible for the bad pieces
               we was banned for */
            tr_bitfieldClear( peer->banfield );
        }
    }
    else
    {
        peer->badPcs++;

        /* Ban the peer for this piece */
        if( !peer->banfield )
        {
            peer->banfield = tr_bitfieldNew( tor->info.pieceCount );
        }
        tr_bitfieldAdd( peer->banfield, piece );

        if( peerIsBad( peer ) )
        {
            /* Full ban */
            peer_dbg( "banned (%d / %d)", peer->goodPcs, peer->badPcs );
            peer->banned = 1;
            peer->peerInterested = 0;
        }
    }
    tr_bitfieldRem( peer->blamefield, piece );
}

int tr_peerGetConnectable( tr_torrent_t * tor, uint8_t ** _buf )
{
    int count = 0;
    uint8_t * buf;
    tr_peer_t * peer;
    int i;

    if( tor->peerCount < 1 )
    {
        *_buf = NULL;
        return 0;
    }

    buf = malloc( 6 * tor->peerCount );
    for( i = 0; i < tor->peerCount; i++ )
    {
        peer = tor->peers[i];

        /* Skip peers with no known listening port */
        if( 0 == peer->port )
            continue;

        memcpy( &buf[count*6], &peer->addr, 4 );
        memcpy( &buf[count*6+4], &peer->port, 2 );
        count++;
    }

    if( count < 1 )
    {
        free( buf ); buf = NULL;
    }
    *_buf = buf;

    return count * 6;
}
