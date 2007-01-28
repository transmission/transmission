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

/***********************************************************************
 * This file handles all incoming messages
 **********************************************************************/

/***********************************************************************
 * parseChoke
 ***********************************************************************
 *
 **********************************************************************/
static inline int parseChoke( tr_torrent_t * tor, tr_peer_t * peer,
                              int len, int choking )
{
    tr_request_t * r;
    int i;

    if( len != 0 )
    {
        peer_dbg( "GET  %schoke, invalid", choking ? "" : "un" );
        return TR_ERROR_ASSERT;
    }

    peer_dbg( "GET  %schoke", choking ? "" : "un" );

    peer->peerChoking = choking;

    if( choking )
    {
        /* Discard all pending requests */
        for( i = 0; i < peer->inRequestCount; i++ )
        {
            r = &peer->inRequests[i];
            tr_cpDownloaderRem( tor->completion, tr_block(r->index,r->begin) );
        }
        peer->inRequestCount = 0;
    }

    return TR_OK;
}

/***********************************************************************
 * parseInterested
 ***********************************************************************
 *
 **********************************************************************/
static inline int parseInterested( tr_peer_t * peer, int len,
                                   int interested )
{
    if( len != 0 )
    {
        peer_dbg( "GET  %sinterested, invalid", interested ? "" : "un" );
        return TR_ERROR_ASSERT;
    }

    peer_dbg( "GET  %sinterested", interested ? "" : "un" );

    peer->peerInterested = interested;

    return TR_OK;
}

/***********************************************************************
 * parseHave
 ***********************************************************************
 *
 **********************************************************************/
static inline int parseHave( tr_torrent_t * tor, tr_peer_t * peer,
                             uint8_t * p, int len )
{
    uint32_t piece;

    if( len != 4 )
    {
        peer_dbg( "GET  have, invalid" );
        return TR_ERROR_ASSERT;
    }

    TR_NTOHL( p, piece );

    peer_dbg( "GET  have %d", piece );

    if( !peer->bitfield )
    {
        peer->bitfield = calloc( ( tor->info.pieceCount + 7 ) / 8, 1 );
    }
    if( !tr_bitfieldHas( peer->bitfield, piece ) )
    {
        peer->pieceCount++;
        peer->progress = (float) peer->pieceCount / tor->info.pieceCount;
    }
    tr_bitfieldAdd( peer->bitfield, piece );
    updateInterest( tor, peer );

    tr_rcTransferred( tor->swarmspeed, tor->info.pieceSize );

    return TR_OK;
}

static inline int parseBitfield( tr_torrent_t * tor, tr_peer_t * peer,
                                 uint8_t * p, int len )
{
    tr_info_t * inf = &tor->info;
    int bitfieldSize;
    int i;

    bitfieldSize = ( inf->pieceCount + 7 ) / 8;
    
    if( len != bitfieldSize )
    {
        peer_dbg( "GET  bitfield, wrong size" );
        return TR_ERROR_ASSERT;
    }

    /* Make sure the spare bits are unset */
    if( ( inf->pieceCount & 0x7 ) )
    {
        uint8_t lastByte;
        
        lastByte   = p[bitfieldSize-1];
        lastByte <<= inf->pieceCount & 0x7;
        lastByte  &= 0xFF;

        if( lastByte )
        {
            peer_dbg( "GET  bitfield, spare bits set" );
            return TR_ERROR_ASSERT;
        }
    }

    peer_dbg( "GET  bitfield, ok" );

    if( !peer->bitfield )
    {
        peer->bitfield = malloc( bitfieldSize );
    }
    memcpy( peer->bitfield, p, bitfieldSize );

    peer->pieceCount = 0;
    for( i = 0; i < inf->pieceCount; i++ )
    {
        if( tr_bitfieldHas( peer->bitfield, i ) )
        {
            peer->pieceCount++;
        }
    }
    peer->progress = (float) peer->pieceCount / inf->pieceCount;

    updateInterest( tor, peer );

    return TR_OK;
}

static inline int parseRequest( tr_peer_t * peer, uint8_t * p, int len )
{
    int index, begin, length;
    tr_request_t * r;

    if( len != 12 )
    {
        peer_dbg( "GET  request, invalid" );
        return TR_ERROR_ASSERT;
    }

    if( peer->amChoking )
    {
        /* Didn't he get it? */
        sendChoke( peer, 1 );
        return TR_OK;
    }
    
    TR_NTOHL( p,     index );
    TR_NTOHL( &p[4], begin );
    TR_NTOHL( &p[8], length );

    peer_dbg( "GET  request %d/%d (%d bytes)",
              index, begin, length );

    /* TODO sanity checks (do we have the piece, etc) */

    if( length > 16384 )
    {
        /* Sorry mate */
        return TR_ERROR;
    }

    if( peer->outRequestCount >= MAX_REQUEST_COUNT )
    {
        tr_err( "Too many requests" );
        return TR_ERROR;
    }

    r         = &peer->outRequests[peer->outRequestCount];
    r->index  = index;
    r->begin  = begin;
    r->length = length;

    (peer->outRequestCount)++;

    return TR_OK;
}

static inline void updateRequests( tr_torrent_t * tor, tr_peer_t * peer,
                                   int index, int begin )
{
    tr_request_t * r;
    int i, j;

    /* Find this block in the requests list */
    for( i = 0; i < peer->inRequestCount; i++ )
    {
        r = &peer->inRequests[i];
        if( index == r->index && begin == r->begin )
        {
            break;
        }
    }

    /* Usually i should be 0, but some clients don't handle multiple
       request well and drop previous requests */
    if( i < peer->inRequestCount )
    {
        if( i > 0 )
        {
            peer_dbg( "not expecting this block yet (%d requests dropped)", i );
        }
        i++;
        for( j = 0; j < i; j++ )
        {
            r = &peer->inRequests[j];
            tr_cpDownloaderRem( tor->completion,
                                tr_block( r->index, r->begin ) );
        }
        peer->inRequestCount -= i;
        memmove( &peer->inRequests[0], &peer->inRequests[i],
                 peer->inRequestCount * sizeof( tr_request_t ) );
    }
    else
    {
        /* Not in the list. Probably because of a cancel that arrived
           too late */
    }
}

static inline int parsePiece( tr_torrent_t * tor, tr_peer_t * peer,
                              uint8_t * p, int len )
{
    int index, begin, block, i, ret;

    TR_NTOHL( p,     index );
    TR_NTOHL( &p[4], begin );
    block = tr_block( index, begin );

    peer_dbg( "GET  piece %d/%d (%d bytes)",
              index, begin, len - 8 );

    updateRequests( tor, peer, index, begin );
    tor->downloadedCur += len;

    /* Sanity checks */
    if( len - 8 != tr_blockSize( block ) )
    {
        peer_dbg( "wrong size (expecting %d)", tr_blockSize( block ) );
        return TR_ERROR_ASSERT;
    }
    if( tr_cpBlockIsComplete( tor->completion, block ) )
    {
        peer_dbg( "have this block already" );
        return TR_OK;
    }

    /* Set blame/credit for this piece */
    if( !peer->blamefield )
    {
        peer->blamefield = calloc( ( tor->info.pieceCount + 7 ) / 8, 1 );
    }
    tr_bitfieldAdd( peer->blamefield, index );

    /* Write to disk */
    if( ( ret = tr_ioWrite( tor->io, index, begin, len - 8, &p[8] ) ) )
    {
        return ret;
    }
    tr_cpBlockAdd( tor->completion, block );
    sendCancel( tor, block );

    if( !tr_cpPieceHasAllBlocks( tor->completion, index ) )
    {
        return TR_OK;
    }

    /* Piece is complete, check it */
    if( ( ret = tr_ioHash( tor->io, index ) ) )
    {
        return ret;
    }
    if( !tr_cpPieceIsComplete( tor->completion, index ) )
    {
        return TR_OK;
    }

    /* Hash OK */
    for( i = 0; i < tor->peerCount; i++ )
    {
        tr_peer_t * otherPeer;
        otherPeer = tor->peers[i];

        if( otherPeer->status < PEER_STATUS_CONNECTED )
            continue;

        sendHave( otherPeer, index );
        updateInterest( tor, otherPeer );
    }

    return TR_OK;
}

static inline int parseCancel( tr_peer_t * peer, uint8_t * p, int len )
{
    int index, begin, length;
    int i;
    tr_request_t * r;

    if( len != 12 )
    {
        peer_dbg( "GET  cancel, invalid" );
        return TR_ERROR_ASSERT;
    }

    TR_NTOHL( p,     index );
    TR_NTOHL( &p[4], begin );
    TR_NTOHL( &p[8], length );

    peer_dbg( "GET  cancel %d/%d (%d bytes)",
              index, begin, length );

    for( i = 0; i < peer->outRequestCount; i++ )
    {
        r = &peer->outRequests[i];
        if( r->index == index && r->begin == begin &&
            r->length == length )
        {
            (peer->outRequestCount)--;
            memmove( &r[0], &r[1], sizeof( tr_request_t ) *
                    ( peer->outRequestCount - i ) );
            break;
        }
    }

    return TR_OK;
}

static inline int parsePort( tr_peer_t * peer, uint8_t * p, int len )
{
    in_port_t port;

    if( len != 2 )
    {
        peer_dbg( "GET  port, invalid" );
        return TR_ERROR_ASSERT;
    }

    port = *( (in_port_t *) p );
    peer_dbg( "GET  port %d", ntohs( port ) );

    return TR_OK;
}

static inline int parseMessage( tr_torrent_t * tor, tr_peer_t * peer,
                                uint8_t * p, int len )
{
    char id;

    /* Type of the message */
    id = *(p++);
    len--;

    switch( id )
    {
        case PEER_MSG_CHOKE:
            return parseChoke( tor, peer, len, 1 );
        case PEER_MSG_UNCHOKE:
            return parseChoke( tor, peer, len, 0 );
        case PEER_MSG_INTERESTED:
            return parseInterested( peer, len, 1 );
        case PEER_MSG_UNINTERESTED:
            return parseInterested( peer, len, 0 );
        case PEER_MSG_HAVE:
            return parseHave( tor, peer, p, len );
        case PEER_MSG_BITFIELD:
            return parseBitfield( tor, peer, p, len );
        case PEER_MSG_REQUEST:
            return parseRequest( peer, p, len );
        case PEER_MSG_PIECE:
            return parsePiece( tor, peer, p, len );
        case PEER_MSG_CANCEL:
            return parseCancel( peer, p, len );
        case PEER_MSG_PORT:
            return parsePort( peer, p, len );
    }

    peer_dbg( "Unknown message '%d'", id );
    return TR_ERROR;
}

static inline int parseBufHeader( tr_peer_t * peer )
{
    uint8_t * p   = peer->buf;

    if( 4 > peer->pos )
    {
        return TR_OK;
    }

    if( p[0] != 19 || memcmp( &p[1], "Bit", 3 ) )
    {
        /* Don't wait until we get 68 bytes, this is wrong
           already */
        peer_dbg( "GET  handshake, invalid" );
        tr_netSend( peer->socket, (uint8_t *) "Nice try...\r\n", 13 );
        return TR_ERROR;
    }
    if( peer->pos < 68 )
    {
        return TR_OK;
    }
    if( memcmp( &p[4], "Torrent protocol", 16 ) )
    {
        peer_dbg( "GET  handshake, invalid" );
        return TR_ERROR;
    }

    return TR_OK;
}

static uint8_t * parseBufHash( tr_peer_t * peer )
{
    if( 48 > peer->pos )
    {
        return NULL;
    }
    else
    {
        return peer->buf + 28;
    }
}

static inline int parseBuf( tr_torrent_t * tor, tr_peer_t * peer )
{
    tr_info_t * inf = &tor->info;

    int       i;
    int       len;
    uint8_t * p   = peer->buf;
    uint8_t * end = &p[peer->pos];
    int       ret;

    if( peer->banned )
    {
        /* Don't even parse, we only stay connected */
        peer->pos = 0;
        return TR_OK;
    }

    while( peer->pos >= 4 )
    {
        if( peer->status & PEER_STATUS_HANDSHAKE )
        {
            char * client;

            if( ( ret = parseBufHeader( peer ) ) )
            {
                return ret;
            }

            if( peer->pos < 68 )
            {
                break;
            }

            if( memcmp( &p[28], inf->hash, 20 ) )
            {
                peer_dbg( "GET  handshake, wrong torrent hash" );
                return TR_ERROR;
            }

            if( !memcmp( &p[48], tor->id, 20 ) )
            {
                /* We are connected to ourselves... */
                peer_dbg( "GET  handshake, that is us" );
                return TR_ERROR;
            }

            peer->status  = PEER_STATUS_CONNECTED;
            memcpy( peer->id, &p[48], 20 );
            p            += 68;
            peer->pos    -= 68;

            for( i = 0; i < tor->peerCount; i++ )
            {
                if( tor->peers[i] == peer )
                {
                    continue;
                }
                if( !peerCmp( peer, tor->peers[i] ) )
                {
                    peer_dbg( "GET  handshake, duplicate" );
                    return TR_ERROR;
                }
            }

            client = tr_clientForId( (uint8_t *) peer->id );
            peer_dbg( "GET  handshake, ok (%s)", client );
            free( client );

            sendBitfield( tor, peer );

            continue;
        }
        
        /* Get payload size */
        TR_NTOHL( p, len );
        p += 4;

        if( len > 9 + tor->blockSize )
        {
            /* This should never happen. Drop that peer */
            peer_dbg( "message too large (%d bytes)", len );
            return TR_ERROR;
        }

        if( !len )
        {
            /* keep-alive */
            peer_dbg( "GET  keep-alive" );
            peer->pos -= 4;
            continue;
        }

        if( &p[len] > end )
        {
            /* We do not have the entire message */
            p -= 4;
            break;
        }

        /* Remaining data after this message */
        peer->pos -= 4 + len;

        if( ( ret = parseMessage( tor, peer, p, len ) ) )
        {
            return ret;
        }

        p += len;
    }

    memmove( peer->buf, p, peer->pos );

    return TR_OK;
}
