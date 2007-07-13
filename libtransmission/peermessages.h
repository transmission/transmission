/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2007 Transmission authors and contributors
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

/***********************************************************************
 * This file handles all outgoing messages
 **********************************************************************/

#define PEER_MSG_CHOKE          0
#define PEER_MSG_UNCHOKE        1
#define PEER_MSG_INTERESTED     2
#define PEER_MSG_UNINTERESTED   3
#define PEER_MSG_HAVE           4
#define PEER_MSG_BITFIELD       5
#define PEER_MSG_REQUEST        6
#define PEER_MSG_PIECE          7
#define PEER_MSG_CANCEL         8
#define PEER_MSG_PORT           9

static uint8_t * messagesPending( tr_peer_t * peer, int * size )
{
    if( peer->outBlockSending || peer->outMessagesPos < 1 )
    {
        return NULL;
    }

    *size = MIN( peer->outMessagesPos, 1024 );

    return peer->outMessages;
}

static void messagesSent( tr_peer_t * peer, int size )
{
    peer->outMessagesPos -= size;
    memmove( peer->outMessages, &peer->outMessages[size],
             peer->outMessagesPos );
}

static int
getHeaderSize( tr_peer_t * peer, int id )
{
    int size, index;

    size = 4;
    if( peer->azproto )
    {
        index = azmsgIdIndex( id );
        assert( 0 <= index );
        size += 4 + azmsgLen( index ) + 1;
    }
    else if( 0 <= id )
    {
        size++;
    }

    return size;
}

static uint8_t *
fillHeader( tr_peer_t * peer, int size, int id, uint8_t * buf )
{
    int index;

    TR_HTONL( size - 4, buf );
    buf += 4;
    if( peer->azproto )
    {
        index = azmsgIdIndex( id );
        assert( 0 <= index );
        TR_HTONL( azmsgLen( index ), buf );
        buf += 4;
        memcpy( buf, azmsgStr( index ), azmsgLen( index ) );
        buf += azmsgLen( index );
        buf[0] = AZ_EXT_VERSION;
        buf++;
    }
    else if( 0 <= id )
    {
        assert( 0 <= id && 0xff > id );
        *buf = id;
        buf++;
    }

    return buf;
}

static uint8_t *
blockPending( tr_torrent_t  * tor,
              tr_peer_t     * peer,
              int           * size )
{
    if( !peer->outBlockLoaded ) /* we need to load the block for the next request */
    {
        uint8_t      * buf;
        tr_request_t * r;
        int            hdrlen;

        if( peer->isChokedByUs ) /* we don't want to send them anything */
            return NULL;

        if( !peer->outRequests ) /* nothing to send */
            return NULL;

        r = (tr_request_t*) peer->outRequests->data;
        assert( r != NULL );
        peer->outRequests = tr_list_remove_data( peer->outRequests, r );

        if( !tr_cpPieceIsComplete( tor->completion, r->index ) ) /* sanity clause */
        {
            /* We've been asked for something we don't have.  buggy client? */
            tr_inf( "Block %d/%d/%d was requested but we don't have it",
                    r->index, r->begin, r->length );
            tr_free( r );
            return NULL;
        }

        hdrlen = 4 + 4 + r->length + getHeaderSize( peer, PEER_MSG_PIECE );
        assert( hdrlen <= ( signed )sizeof peer->outBlock );
        buf = fillHeader( peer, hdrlen, PEER_MSG_PIECE, peer->outBlock );

        TR_HTONL( r->index, buf );
        buf += 4;
        TR_HTONL( r->begin, buf );
        buf += 4;

        tr_ioRead( tor->io, r->index, r->begin, r->length, buf );

        peer_dbg( "SEND piece %d/%d (%d bytes)",
                  r->index, r->begin, r->length );
        peer->outBlockSize   = hdrlen;
        peer->outBlockLoaded = 1;

        tr_free( r );
    }

    *size = MIN( 1024, peer->outBlockSize );

    return (uint8_t *) peer->outBlock;
}

static void blockSent( tr_peer_t * peer, int size )
{
    peer->outBlockSize -= size;
    memmove( peer->outBlock, &peer->outBlock[size], peer->outBlockSize );

    if( peer->outBlockSize > 0 )
    {
        /* We can't send messages until we are done sending the block */
        peer->outBlockSending = 1;
    }
    else
    {
        /* Block fully sent */
        peer->outBlockSending = 0;
        peer->outBlockLoaded  = 0;
    }
}

static uint8_t * getMessagePointer( tr_peer_t * peer, int size, int id )
{
    uint8_t * buf;

    size += getHeaderSize( peer, id );

    if( peer->outMessagesPos + size > peer->outMessagesSize )
    {
        peer->outMessagesSize = peer->outMessagesPos + size;
        peer->outMessages     = realloc( peer->outMessages,
                                         peer->outMessagesSize );
    }

    buf                   = &peer->outMessages[peer->outMessagesPos];
    peer->outMessagesPos += size;

    return fillHeader( peer, size, id, buf );
}

/***********************************************************************
 * sendKeepAlive
 ***********************************************************************
 * 
 **********************************************************************/
static void sendKeepAlive( tr_peer_t * peer )
{
    getMessagePointer( peer, 0, AZ_MSG_BT_KEEP_ALIVE );

    peer_dbg( "SEND keep-alive" );
}


/***********************************************************************
 * sendChoke
 ***********************************************************************
 * 
 **********************************************************************/
static void sendChoke( tr_peer_t * peer, int yes )
{
    int id;

    id = ( yes ? PEER_MSG_CHOKE : PEER_MSG_UNCHOKE );
    getMessagePointer( peer, 0, id );

    peer->isChokedByUs = yes;

    if( !yes )
    {
        /* Drop older requests from the last time it was unchoked, if any */
        tr_list_foreach( peer->outRequests, tr_free );
        tr_list_free( peer->outRequests );
        peer->outRequests = NULL;
    }

    peer_dbg( "SEND %schoke", yes ? "" : "un" );
}

/***********************************************************************
 * sendInterest
 ***********************************************************************
 * 
 **********************************************************************/
static void sendInterest( tr_peer_t * peer, int yes )
{
    int id;

    id = ( yes ? PEER_MSG_INTERESTED : PEER_MSG_UNINTERESTED );
    getMessagePointer( peer, 0, id );

    peer->isInteresting = yes;

    peer_dbg( "SEND %sinterested", yes ? "" : "un" );
}

/***********************************************************************
 * sendHave
 ***********************************************************************
 * 
 **********************************************************************/
static void sendHave( tr_peer_t * peer, int piece )
{
    uint8_t * p;

    p = getMessagePointer( peer, 4, PEER_MSG_HAVE );

    TR_HTONL( piece, p );

    peer_dbg( "SEND have %d", piece );
}

/***********************************************************************
 * sendBitfield
 ***********************************************************************
 * Builds a 'bitfield' message:
 *  - size = 5 + X (4 bytes)
 *  - id   = 5     (1 byte)
 *  - bitfield     (X bytes)
 **********************************************************************/
static void sendBitfield( tr_torrent_t * tor, tr_peer_t * peer )
{
    uint8_t             * p;
    const tr_bitfield_t * bitfield;

    bitfield = tr_cpPieceBitfield( tor->completion );
    p = getMessagePointer( peer, bitfield->len, PEER_MSG_BITFIELD );

    memcpy( p, bitfield->bits, bitfield->len );

    peer_dbg( "SEND bitfield" );
}

/***********************************************************************
 * sendRequest
 ***********************************************************************
 *
 **********************************************************************/
static void sendRequest( tr_torrent_t * tor, tr_peer_t * peer, int block )
{
    tr_info_t * inf = &tor->info;
    tr_request_t * r;
    uint8_t * p;

    /* Get the piece the block is a part of, its position in the piece
       and its size */
    r         = &peer->inRequests[peer->inRequestCount];
    r->index  = block / ( inf->pieceSize / tor->blockSize );
    r->begin  = ( block % ( inf->pieceSize / tor->blockSize ) ) *
                    tor->blockSize;
    r->length = tr_blockSize( block );
    (peer->inRequestCount)++;

    /* Build the "ask" message */
    p = getMessagePointer( peer, 12, PEER_MSG_REQUEST );

    TR_HTONL( r->index,  p     );
    TR_HTONL( r->begin,  p + 4 );
    TR_HTONL( r->length, p + 8 );

    tr_cpDownloaderAdd( tor->completion, block );

    peer_dbg( "SEND request %d/%d (%d bytes)",
              r->index, r->begin, r->length );
}

/***********************************************************************
 * sendCancel
 ***********************************************************************
 *
 **********************************************************************/
static void sendCancel( tr_peer_t * peer, int index, int begin,
                        int length )
{
    uint8_t * p;
    p = getMessagePointer( peer, 12, PEER_MSG_CANCEL );

    TR_HTONL( index,  p     );
    TR_HTONL( begin,  p + 4 );
    TR_HTONL( length, p + 8 );

    peer_dbg( "SEND cancel %d/%d (%d bytes)", index, begin, length );
}

/***********************************************************************
 * broadcastCancel
 ***********************************************************************
 *
 **********************************************************************/
static void broadcastCancel( tr_torrent_t * tor, int index, int begin,
                             int length )
{
    int i, j;
    tr_peer_t * peer;
    tr_request_t * r;

    for( i = 0; i < tor->peerCount; i++ )
    {
        peer = tor->peers[i];

        for( j = 0; j < peer->inRequestCount; j++ )
        {
            r = &peer->inRequests[j];

            if( r->index != index || r->begin != begin ||
                r->length != length )
            {
                continue;
            }

            sendCancel( peer, index, begin, length );

            (peer->inRequestCount)--;
            memmove( &peer->inRequests[j], &peer->inRequests[j+1],
                     ( peer->inRequestCount - j ) * sizeof( tr_request_t ) );
            break;
        }
    }
}



/***********************************************************************
 * sendExtended
 ***********************************************************************
 * Builds an extended message:
 *  - size = 6 + X (4 bytes)
 *  - id   = 20    (1 byte)
 *  - eid  = Y     (1 byte)
 *  - data         (X bytes)
 **********************************************************************/
static int sendExtended( tr_torrent_t * tor, tr_peer_t * peer, int id )
{
    uint8_t * p;
    char    * buf;
    int       len;

    buf = NULL;
    switch( id )
    {
        case EXTENDED_HANDSHAKE_ID:
            buf = makeExtendedHandshake( tor, peer, &len );
            if( NULL == buf )
            {
                return TR_ERROR;
            }
            peer_dbg( "SEND extended-handshake, %s pex",
                      ( peer->private ? "without" : "with" ) );
            break;
        case EXTENDED_PEX_ID:
            if( makeUTPex( tor, peer, &buf, &len ) )
            {
                return TR_ERROR;
            }
            else if( NULL == buf )
            {
                return TR_OK;
            }
            peer_dbg( "SEND extended-pex" );
            break;
        default:
            assert( 0 );
            break;
    }

    /* add header and queue it to be sent */
    p = getMessagePointer( peer, 1 + len, PEER_MSG_EXTENDED );
    p[0] = id;
    memcpy( p + 1, buf, len );
    free( buf );

    return TR_OK;
}

/***********************************************************************
 * sendAZPex
 ***********************************************************************
 *
 **********************************************************************/
static int sendAZPex( tr_torrent_t * tor, tr_peer_t * peer )
{
    uint8_t * p;
    char    * buf;
    int       len;

    if( makeAZPex( tor, peer, &buf, &len ) )
    {
        return TR_ERROR;
    }
    else if( NULL == buf )
    {
        return TR_OK;
    }

    /* add header and queue it to be sent */
    p = getMessagePointer( peer, len, AZ_MSG_AZ_PEER_EXCHANGE );
    memcpy( p, buf, len );
    free( buf );

    peer_dbg( "SEND azureus-pex" );

    return TR_OK;
}
