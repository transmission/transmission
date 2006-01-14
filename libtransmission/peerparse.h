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

    if( len != 1 )
    {
        peer_dbg( "GET  %schoke, invalid", choking ? "" : "un" );
        return 1;
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

    return 0;
}

/***********************************************************************
 * parseInterested
 ***********************************************************************
 *
 **********************************************************************/
static inline int parseInterested( tr_peer_t * peer, int len,
                                   int interested )
{
    if( len != 1 )
    {
        peer_dbg( "GET  %sinterested, invalid", interested ? "" : "un" );
        return 1;
    }

    peer_dbg( "GET  %sinterested", interested ? "" : "un" );

    peer->peerInterested = interested;

    return 0;
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

    if( len != 5 )
    {
        peer_dbg( "GET  have, invalid" );
        return 1;
    }

    TR_NTOHL( p, piece );

    peer_dbg( "GET  have %d", piece );

    if( !peer->bitfield )
    {
        peer->bitfield = calloc( ( tor->info.pieceCount + 7 ) / 8, 1 );
    }
    tr_bitfieldAdd( peer->bitfield, piece );
    updateInterest( tor, peer );

    return 0;
}

static inline int parseBitfield( tr_torrent_t * tor, tr_peer_t * peer,
                                 uint8_t * p, int len )
{
    tr_info_t * inf = &tor->info;
    int bitfieldSize;

    bitfieldSize = ( inf->pieceCount + 7 ) / 8;
    
    if( len != 1 + bitfieldSize )
    {
        peer_dbg( "GET  bitfield, wrong size" );
        return 1;
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
            return 1;
        }
    }

    peer_dbg( "GET  bitfield, ok" );

    if( !peer->bitfield )
    {
        peer->bitfield = malloc( bitfieldSize );
    }
    memcpy( peer->bitfield, p, bitfieldSize );
    updateInterest( tor, peer );

    return 0;
}

static inline int parseRequest( tr_peer_t * peer, uint8_t * p, int len )
{
    int index, begin, length;
    tr_request_t * r;

    if( len != 13 )
    {
        peer_dbg( "GET  request, invalid" );
        return 1;
    }

    if( peer->amChoking )
    {
        /* Didn't he get it? */
        sendChoke( peer, 1 );
        return 0;
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
        return 1;
    }

    if( peer->outRequestCount >= MAX_REQUEST_COUNT )
    {
        tr_err( "Too many requests" );
        return 1;
    }

    r         = &peer->outRequests[peer->outRequestCount];
    r->index  = index;
    r->begin  = begin;
    r->length = length;

    (peer->outRequestCount)++;

    return 0;
}

static inline int parsePiece( tr_torrent_t * tor, tr_peer_t * peer,
                              uint8_t * p, int len )
{
    int index, begin, block, i, j;
    tr_request_t * r;

    TR_NTOHL( p,     index );
    TR_NTOHL( &p[4], begin );

    peer_dbg( "GET  piece %d/%d (%d bytes)",
              index, begin, len - 9 );

    if( peer->inRequestCount < 1 )
    {
        /* Our "cancel" was probably late */
        peer_dbg( "not expecting a block" );
        return 0;
    }
    
    r = &peer->inRequests[0];
    if( index != r->index || begin != r->begin )
    {
        int suckyClient;

        /* Either our "cancel" was late, or this is a sucky
           client that cannot deal with multiple requests */
        suckyClient = 0;
        for( i = 0; i < peer->inRequestCount; i++ )
        {
            r = &peer->inRequests[i];

            if( index != r->index || begin != r->begin )
            {
                continue;
            }

            /* Sucky client, he dropped the previous requests */
            peer_dbg( "block was expected later" );
            for( j = 0; j < i; j++ )
            {
                r = &peer->inRequests[j];
                tr_cpDownloaderRem( tor->completion,
                                    tr_block(r->index,r->begin) );
            }
            suckyClient = 1;
            peer->inRequestCount -= i;
            memmove( &peer->inRequests[0], &peer->inRequests[i],
                     peer->inRequestCount * sizeof( tr_request_t ) );
            r = &peer->inRequests[0];
            break;
        }

        if( !suckyClient )
        {
            r = &peer->inRequests[0];
            peer_dbg( "wrong block (expecting %d/%d)",
                      r->index, r->begin );
            return 0;
        }
    }

    if( len - 9 != r->length )
    {
        peer_dbg( "wrong size (expecting %d)", r->length );
        return 1;
    }

    block = tr_block( r->index, r->begin );
    if( tr_cpBlockIsComplete( tor->completion, block ) )
    {
        peer_dbg( "have this block already" );
        (peer->inRequestCount)--;
        memmove( &peer->inRequests[0], &peer->inRequests[1],
                 peer->inRequestCount * sizeof( tr_request_t ) );
        return 0;
    }

    tr_cpBlockAdd( tor->completion, block );
    tr_ioWrite( tor->io, index, begin, len - 9, &p[8] );
    tr_cpDownloaderRem( tor->completion, block );

    sendCancel( tor, block );

    if( tr_cpPieceIsComplete( tor->completion, index ) )
    {
        tr_peer_t * otherPeer;

        for( i = 0; i < tor->peerCount; i++ )
        {
            otherPeer = tor->peers[i];

            if( otherPeer->status < PEER_STATUS_CONNECTED )
            {
                continue;
            }

            sendHave( otherPeer, index );
            updateInterest( tor, otherPeer );
        }
    }

    (peer->inRequestCount)--;
    memmove( &peer->inRequests[0], &peer->inRequests[1],
             peer->inRequestCount * sizeof( tr_request_t ) );

    return 0;
}

static inline int parseCancel( tr_peer_t * peer, uint8_t * p, int len )
{
    int index, begin, length;
    int i;
    tr_request_t * r;

    if( len != 13 )
    {
        peer_dbg( "GET  cancel, invalid" );
        return 1;
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

    return 0;
}

static inline int parsePort( tr_peer_t * peer, uint8_t * p, int len )
{
    in_port_t port;

    if( len != 3 )
    {
        peer_dbg( "GET  port, invalid" );
        return 1;
    }

    port = *( (in_port_t *) p );
    peer_dbg( "GET  port %d", ntohs( port ) );

    return 0;
}

static inline int parseMessage( tr_torrent_t * tor, tr_peer_t * peer,
                                uint8_t * p, int len )
{
    char id;

    /* Type of the message */
    id = *(p++);

    switch( id )
    {
        case 0:
            return parseChoke( tor, peer, len, 1 );
        case 1:
            return parseChoke( tor, peer, len, 0 );
        case 2:
            return parseInterested( peer, len, 1 );
        case 3:
            return parseInterested( peer, len, 0 );
        case 4:
            return parseHave( tor, peer, p, len );
        case 5:
            return parseBitfield( tor, peer, p, len );
        case 6:
            return parseRequest( peer, p, len );
        case 7:
            return parsePiece( tor, peer, p, len );
        case 8:
            return parseCancel( peer, p, len );
        case 9:
            return parsePort( peer, p, len );
    }

    peer_dbg( "Unknown message '%d'", id );
    return 1;
}

static inline int parseBufHeader( tr_peer_t * peer )
{
    uint8_t * p   = peer->buf;

    if( 4 > peer->pos )
    {
        return 0;
    }

    if( p[0] != 19 || memcmp( &p[1], "Bit", 3 ) )
    {
        /* Don't wait until we get 68 bytes, this is wrong
           already */
        peer_dbg( "GET  handshake, invalid" );
        tr_netSend( peer->socket, (uint8_t *) "Nice try...\r\n", 13 );
        return 1;
    }
    if( peer->pos < 68 )
    {
        return 0;
    }
    if( memcmp( &p[4], "Torrent protocol", 16 ) )
    {
        peer_dbg( "GET  handshake, invalid" );
        return 1;
    }

    return 0;
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

static inline int parseBuf( tr_torrent_t * tor, tr_peer_t * peer,
                            int newBytes )
{
    tr_info_t * inf = &tor->info;

    int       i;
    int       len;
    uint8_t * p   = peer->buf;
    uint8_t * end = &p[peer->pos];

    while( peer->pos >= 4 )
    {
        if( peer->status & PEER_STATUS_HANDSHAKE )
        {
            char * client;

            if( parseBufHeader( peer ) )
            {
                return 1;
            }

            if( peer->pos < 68 )
            {
                break;
            }

            if( memcmp( &p[28], inf->hash, 20 ) )
            {
                peer_dbg( "GET  handshake, wrong torrent hash" );
                return 1;
            }

            if( !memcmp( &p[48], tor->id, 20 ) )
            {
                /* We are connected to ourselves... */
                peer_dbg( "GET  handshake, that is us" );
                return 1;
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
                    return 1;
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
            return 1;
        }

        if( !len )
        {
            /* keep-alive */
            peer_dbg( "GET  keep-alive" );
            peer->pos -= 4;
            continue;
        }

        /* That's a piece coming */
        if( p < end && *p == 7 )
        {
            /* XXX */
            tor->downloaded[9] += newBytes;
            peer->inTotal      += newBytes;
            newBytes            = 0;
        }

        if( &p[len] > end )
        {
            /* We do not have the entire message */
            p -= 4;
            break;
        }

        /* Remaining data after this message */
        peer->pos -= 4 + len;

        if( parseMessage( tor, peer, p, len ) )
        {
            return 1;
        }

        p += len;
    }

    memmove( peer->buf, p, peer->pos );

    return 0;
}
