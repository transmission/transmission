/******************************************************************************
 * Copyright (c) 2005 Eric Petit
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
    struct in_addr addr;
    in_port_t      port;

#define PEER_STATUS_IDLE       1 /* Need to connect */
#define PEER_STATUS_CONNECTING 2 /* Trying to send handshake */
#define PEER_STATUS_HANDSHAKE  4 /* Waiting for peer's handshake */
#define PEER_STATUS_CONNECTED  8 /* Got peer's handshake */
    int            status;
    int            socket;
    uint64_t       date;
    uint64_t       keepAlive;

    char           amChoking;
    char           amInterested;
    char           peerChoking;
    char           peerInterested;

    int            optimistic;
    uint64_t       lastChoke;

    uint8_t        id[20];

    uint8_t      * bitfield;

    uint8_t      * buf;
    int            size;
    int            pos;

    uint8_t      * outMessages;
    int            outMessagesSize;
    int            outMessagesPos;
    uint8_t        outBlock[13+16384];
    int            outBlockSize;
    int            outBlockLoaded;
    int            outBlockSending;

    int            inRequestCount;
    tr_request_t   inRequests[OUR_REQUEST_COUNT];
    int            inIndex;
    int            inBegin;
    int            inLength;
    uint64_t       inTotal;

    int            outRequestCount;
    tr_request_t   outRequests[MAX_REQUEST_COUNT];
    uint64_t       outTotal;
    uint64_t       outDate;
    int            outSlow;

    tr_ratecontrol_t * download;
};

#define peer_dbg( a... ) __peer_dbg( peer, ## a )
static void __peer_dbg( tr_peer_t * peer, char * msg, ... )
{
    char    string[256];
    va_list args;

    va_start( args, msg );
    sprintf( string, "%08x:%04x ",
             (uint32_t) peer->addr.s_addr, peer->port );
    vsnprintf( &string[14], sizeof( string ) - 14, msg, args );
    va_end( args ); 

    tr_dbg( "%s", string );
}

#include "peermessages.h"
#include "peerutils.h"
#include "peerparse.h"

/***********************************************************************
 * tr_peerAddOld
 ***********************************************************************
 * Tries to add a peer given its IP and port (received from a tracker
 * which doesn't support the "compact" extension).
 **********************************************************************/
void tr_peerAddOld( tr_torrent_t * tor, char * ip, int port )
{
    struct in_addr addr;

    if( tr_netResolve( ip, &addr ) )
    {
        return;
    }

    addWithAddr( tor, addr, htons( port ) );
}

/***********************************************************************
 * tr_peerAddCompact
 ***********************************************************************
 * Tries to add a peer, using 'addr' and 'port' to connect to the peer.
 **********************************************************************/
void tr_peerAddCompact( tr_torrent_t * tor, struct in_addr addr,
                        in_port_t port )
{
    addWithAddr( tor, addr, port );
}

/***********************************************************************
 * tr_peerInit
 ***********************************************************************
 * Initializes a new peer.
 **********************************************************************/
tr_peer_t * tr_peerInit( struct in_addr addr, in_port_t port, int s )
{
    tr_peer_t * peer = peerInit();

    peer->socket = s;
    peer->addr   = addr;
    peer->port   = port;
    peer->status = PEER_STATUS_CONNECTING;

    return peer;
}

void tr_peerAttach( tr_torrent_t * tor, tr_peer_t * peer )
{
    peerAttach( tor, peer );
}

void tr_peerDestroy( tr_fd_t * fdlimit, tr_peer_t * peer )
{
    if( peer->bitfield )
    {
        free( peer->bitfield );
    }
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
        tr_fdSocketClosed( fdlimit, 0 );
    }
    tr_rcClose( peer->download );
    free( peer );
}

/***********************************************************************
 * tr_peerRem
 ***********************************************************************
 * Frees and closes everything related to the peer at index 'i', and
 * removes it from the peers list.
 **********************************************************************/
void tr_peerRem( tr_torrent_t * tor, int i )
{
    tr_peer_t * peer = tor->peers[i];
    int j;

    for( j = 0; j < peer->inRequestCount; j++ )
    {
        tr_request_t * r;
        int            block;

        r     = &peer->inRequests[j];
        block = tr_block( r->index,r->begin );
        tr_cpDownloaderRem( tor->completion, block );
    }
    tr_peerDestroy( tor->fdlimit, peer );
    tor->peerCount--;
    memmove( &tor->peers[i], &tor->peers[i+1],
             ( tor->peerCount - i ) * sizeof( tr_peer_t * ) );
}

/***********************************************************************
 * tr_peerRead
 ***********************************************************************
 *
 **********************************************************************/
int tr_peerRead( tr_torrent_t * tor, tr_peer_t * peer )
{
    int ret;

    /* Try to read */
    for( ;; )
    {
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
        ret = tr_netRecv( peer->socket, &peer->buf[peer->pos],
                          peer->size - peer->pos );
        if( ret & TR_NET_CLOSE )
        {
            peer_dbg( "connection closed" );
            return 1;
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
            tr_rcTransferred( tor->globalDownload, ret );
            if( parseBuf( tor, peer ) )
            {
                return 1;
            }
        }
        else
        {
            if( parseBufHeader( peer ) )
            {
                return 1;
            }
        }
    }

    return 0;
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
void tr_peerPulse( tr_torrent_t * tor )
{
    int i, ret, size;
    uint8_t * p;
    tr_peer_t * peer;

    if( tr_date() > tor->date + 1000 )
    {
        tor->date = tr_date();

        for( i = 0; i < tor->peerCount; )
        {
            if( checkPeer( tor, i ) )
            {
                tr_peerRem( tor, i );
                continue;
            }
            i++;
        }
    }

    if( tor->status & TR_STATUS_STOPPING )
    {
        return;
    }
    
    /* Shuffle peers */
    if( tor->peerCount > 1 )
    {
        peer = tor->peers[0];
        memmove( &tor->peers[0], &tor->peers[1],
                 ( tor->peerCount - 1 ) * sizeof( void * ) );
        tor->peers[tor->peerCount - 1] = peer;
    }

    /* Handle peers */
    for( i = 0; i < tor->peerCount; )
    {
        peer = tor->peers[i];

        if( peer->status < PEER_STATUS_HANDSHAKE )
        {
            i++;
            continue;
        }

        if( tr_peerRead( tor, tor->peers[i] ) )
        {
            goto dropPeer;
        }

        if( peer->status < PEER_STATUS_CONNECTED )
        {
            i++;
            continue;
        }

        /* Try to write */
writeBegin:

        /* Send all smaller messages regardless of the upload cap */
        while( ( p = messagesPending( peer, &size ) ) )
        {
            ret = tr_netSend( peer->socket, p, size );
            if( ret & TR_NET_CLOSE )
            {
                goto dropPeer;
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
            if( !tr_rcCanTransfer( tor->globalUpload ) )
            {
                break;
            }

            ret = tr_netSend( peer->socket, p, size );
            if( ret & TR_NET_CLOSE )
            {
                goto dropPeer;
            }
            else if( ret & TR_NET_BLOCK )
            {
                break;
            }

            blockSent( peer, ret );
            tr_rcTransferred( tor->upload, ret );
            tr_rcTransferred( tor->globalUpload, ret );

            tor->uploaded  += ret;
            peer->outTotal += ret;
            peer->outDate   = tr_date();

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
            tr_peerRem( tor, i );
            continue;
        }

        if( peer->amInterested && !peer->peerChoking )
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
        
        i++;
        continue;

dropPeer:
        tr_peerRem( tor, i );
    }
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
 * tr_peerIsUploading
 ***********************************************************************
 *
 **********************************************************************/
int tr_peerIsUploading( tr_peer_t * peer )
{
    return ( peer->inRequestCount > 0 );
}

/***********************************************************************
 * tr_peerIsDownloading
 ***********************************************************************
 *
 **********************************************************************/
int tr_peerIsDownloading( tr_peer_t * peer )
{
    return peer->outBlockSending;
}

/***********************************************************************
 * tr_peerBitfield
 ***********************************************************************
 *
 **********************************************************************/
uint8_t * tr_peerBitfield( tr_peer_t * peer )
{
    return peer->bitfield;
}

float tr_peerDownloadRate( tr_peer_t * peer )
{
    return tr_rcRate( peer->download );
}

int tr_peerIsUnchoked( tr_peer_t * peer )
{
    return !peer->amChoking;
}

int tr_peerIsInterested  ( tr_peer_t * peer )
{
    return peer->peerInterested;
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
