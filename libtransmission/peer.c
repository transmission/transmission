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
 * Tries to add a peer. If 's' is a negative value, will use 'addr' and
 * 'port' to connect to the peer. Otherwise, use the already connected
 * socket 's'.
 **********************************************************************/
void tr_peerAddCompact( tr_torrent_t * tor, struct in_addr addr,
                        in_port_t port, int s )
{
    tr_peer_t * peer;

    if( s < 0 )
    {
        addWithAddr( tor, addr, port );
        return;
    }

    if( !( peer = peerInit( tor ) ) )
    {
        tr_netClose( s );
        tr_fdSocketClosed( tor->fdlimit, 0 );
        return;
    }

    peer->socket = s;
    peer->addr   = addr;
    peer->port   = port;
    peer->status = PEER_STATUS_CONNECTING;
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
        if( tor->blockHave[block] > 0 )
        {
          (tor->blockHave[block])--;
        }
    }
    if( !peer->amChoking )
    {
        tr_uploadChoked( tor->upload );
    }
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
        tr_fdSocketClosed( tor->fdlimit, 0 );
    }
    free( peer );
    tor->peerCount--;
    memmove( &tor->peers[i], &tor->peers[i+1],
             ( tor->peerCount - i ) * sizeof( tr_peer_t * ) );
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

    tor->dates[9] = tr_date();
    if( tor->dates[9] > tor->dates[8] + 1000 )
    {
        memmove( &tor->downloaded[0], &tor->downloaded[1],
                 9 * sizeof( uint64_t ) );
        memmove( &tor->uploaded[0], &tor->uploaded[1],
                 9 * sizeof( uint64_t ) );
        memmove( &tor->dates[0], &tor->dates[1],
                 9 * sizeof( uint64_t ) );

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

    /* Check for incoming connections */
    if( tor->bindSocket > -1 &&
        tor->peerCount < TR_MAX_PEER_COUNT &&
        !tr_fdSocketWillCreate( tor->fdlimit, 0 ) )
    {
        int            s;
        struct in_addr addr;
        in_port_t      port;
        s = tr_netAccept( tor->bindSocket, &addr, &port );
        if( s > -1 )
        {
            tr_peerAddCompact( tor, addr, port, s );
        }
        else
        {
            tr_fdSocketClosed( tor->fdlimit, 0 );
        }
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

        /* Connect */
        if( ( peer->status & PEER_STATUS_IDLE ) &&
            !tr_fdSocketWillCreate( tor->fdlimit, 0 ) )
        {
            peer->socket = tr_netOpen( peer->addr, peer->port );
            if( peer->socket < 0 )
            {
                peer_dbg( "connection failed" );
                goto dropPeer;
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

            ret = tr_netSend( peer->socket, buf, 68 );
            if( ret & TR_NET_CLOSE )
            {
                peer_dbg( "connection closed" );
                goto dropPeer;
            }
            else if( !( ret & TR_NET_BLOCK ) )
            {
                peer_dbg( "SEND handshake" );
                peer->status = PEER_STATUS_HANDSHAKE;
            }
        }

        /* Try to read */
        if( peer->status >= PEER_STATUS_HANDSHAKE )
        {
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
                    goto dropPeer;
                }
                else if( ret & TR_NET_BLOCK )
                {
                    break;
                }
                peer->date  = tr_date();
                peer->pos  += ret;
                if( parseBuf( tor, peer, ret ) )
                {
                    goto dropPeer;
                }
            }
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
            if( !tr_uploadCanUpload( tor->upload ) )
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
            tr_uploadUploaded( tor->upload, ret );

            tor->uploaded[9] += ret;
            peer->outTotal   += ret;
            peer->outDate     = tr_date();

            /* In case this block is done, you may have messages
               pending. Send them before we start the next block */
            goto writeBegin;
        }
writeEnd:

        /* Connected peers: ask for a block whenever possible */
        if( peer->status & PEER_STATUS_CONNECTED )
        {
            if( tor->blockHaveCount < tor->blockCount &&
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
