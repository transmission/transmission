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
 * This file handles all incoming messages
 **********************************************************************/

/***********************************************************************
 * parseChoke
 ***********************************************************************
 *
 **********************************************************************/

static int parseChoke( tr_torrent_t  * tor,
                       tr_peer_t     * peer,
                       int             len,
                       int             choking )
{
    tr_request_t * r;
    int i;

    if( len != 0 )
    {
        peer_dbg( "GET  %schoke, invalid", choking ? "" : "un" );
        return TR_ERROR_ASSERT;
    }

    peer_dbg( "GET  %schoke", choking ? "" : "un" );

    peer->isChokingUs = choking;

    if( choking )
    {
        /* Discard all pending requests */
        for( i = 0; i < peer->inRequestCount; i++ )
        {
            r = &peer->inRequests[i];
            tr_cpDownloaderRem( tor->completion, tr_block(r->index,r->begin) );

            /* According to the spec, all requests are dropped when you
               are choked, however some clients seem to remember those
               the next time they unchoke you. Also, if you get quickly
               choked and unchoked while you are sending requests, you
               can't know when the other peer received them and how it
               handled it.
               This can cause us to receive blocks multiple times and
               overdownload, so we send 'cancel' messages to try and
               reduce that. */
            sendCancel( peer, r->index, r->begin, r->length );
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
static int parseInterested( tr_peer_t * peer, int len,
                                   int interested )
{
    if( len != 0 )
    {
        peer_dbg( "GET  %sinterested, invalid", interested ? "" : "un" );
        return TR_ERROR_ASSERT;
    }

    peer_dbg( "GET  %sinterested", interested ? "" : "un" );

    peer->isInterested = interested;

    return TR_OK;
}

/***********************************************************************
 * parseHave
 ***********************************************************************
 *
 **********************************************************************/
static int parseHave( tr_torrent_t * tor, tr_peer_t * peer,
                             uint8_t * p, int len )
{
    tr_info_t * inf = &tor->info;
    uint32_t piece;

    if( len != 4 )
    {
        peer_dbg( "GET  have, invalid" );
        return TR_ERROR_ASSERT;
    }

    piece = tr_ntohl( p );
    if( ( uint32_t )inf->pieceCount <= piece )
    {
        peer_dbg( "GET  have, invalid piece" );
        return TR_ERROR_ASSERT;
    }

    peer_dbg( "GET  have %d", piece );

    if( !peer->bitfield )
    {
        peer->bitfield = tr_bitfieldNew( inf->pieceCount );
    }
    if( !tr_bitfieldHas( peer->bitfield, piece ) )
    {
        peer->pieceCount++;
        peer->progress = (float) peer->pieceCount / inf->pieceCount;
    }
    tr_bitfieldAdd( peer->bitfield, piece );
    updateInterest( tor, peer );

    tr_rcTransferred( tor->swarmspeed, tor->info.pieceSize );

    return TR_OK;
}

static int parseBitfield( tr_torrent_t * tor, tr_peer_t * peer,
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
        peer->bitfield = tr_bitfieldNew( inf->pieceCount );
    }
    assert( (unsigned)bitfieldSize == peer->bitfield->len );
    memcpy( peer->bitfield->bits, p, bitfieldSize );

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

static int parseRequest( tr_torrent_t * tor, tr_peer_t * peer,
                                uint8_t * p, int len )
{
    tr_info_t * inf = &tor->info;
    int index, begin, length;
    tr_request_t * r;

    if( len != 12 )
    {
        peer_dbg( "GET  request, invalid" );
        return TR_ERROR_ASSERT;
    }

    if( peer->isChokedByUs )
    {
        /* Didn't he get it? */
        sendChoke( peer, 1 );
        return TR_OK;
    }
    
    index = tr_ntohl( p );
    begin = tr_ntohl( &p[4] );
    length = tr_ntohl( &p[8] );

    if( inf->pieceCount <= index )
    {
        peer_dbg( "GET  request, invalid index" );
        return TR_ERROR_ASSERT;
    }
    if( tr_pieceSize( index ) < begin + length )
    {
        peer_dbg( "GET  request, invalid begin/length" );
        return TR_ERROR_ASSERT;
    }

    peer_dbg( "GET  request %d/%d (%d bytes)",
              index, begin, length );

    /* TODO sanity checks (do we have the piece, etc) */

    if( length > 16384 )
    {
        /* Sorry mate */
        return TR_ERROR;
    }

    r = tr_new0( tr_request_t, 1 );
    r->index = index;
    r->begin = begin;
    r->length = length;
    peer->outRequests = tr_list_append( peer->outRequests, r );

    return TR_OK;
}

static void updateRequests( tr_peer_t * peer, int index, int begin )
{
    tr_request_t * r;
    int i;

    /* Find this block in the requests list */
    for( i = 0; i < peer->inRequestCount; i++ )
    {
        r = &peer->inRequests[i];
        if( index == r->index && begin == r->begin )
        {
            break;
        }
    }

    /* Usually 'i' would be 0, but some clients don't handle multiple
       requests and drop previous requests, some other clients don't
       send blocks in the same order we sent the requests */
    if( i < peer->inRequestCount )
    {
        peer->inRequestCount--;
        memmove( &peer->inRequests[i], &peer->inRequests[i+1],
                 ( peer->inRequestCount - i ) * sizeof( tr_request_t ) );
    }
    else
    {
        /* Not in the list. Probably because of a cancel that arrived
           too late */
        peer_dbg( "wasn't expecting this block" );
    }
}

static int parsePiece( tr_torrent_t * tor, tr_peer_t * peer,
                              uint8_t * p, int len )
{
    tr_info_t * inf = &tor->info;
    int index, begin, block, i, ret;

    if( 8 > len )
    {
        peer_dbg( "GET  piece, too short (8 > %i)", len );
        return TR_ERROR_ASSERT;
    }

    index = tr_ntohl( p );
    begin = tr_ntohl( &p[4] );

    if( inf->pieceCount <= index )
    {
        peer_dbg( "GET  piece, invalid index" );
        return TR_ERROR_ASSERT;
    }
    if( tr_pieceSize( index ) < begin + len - 8 )
    {
        peer_dbg( "GET  piece, invalid begin/length" );
        return TR_ERROR_ASSERT;
    }

    block = tr_block( index, begin );

    peer_dbg( "GET  piece %d/%d (%d bytes)",
              index, begin, len - 8 );

    updateRequests( peer, index, begin );

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
        peer->blamefield = tr_bitfieldNew( inf->pieceCount );
    }
    tr_bitfieldAdd( peer->blamefield, index );

    /* Write to disk */
    if( ( ret = tr_ioWrite( tor->io, index, begin, len - 8, &p[8] ) ) )
    {
        return ret;
    }
    tr_cpBlockAdd( tor->completion, block );
    tr_peerSentBlockToUs( peer, len-8 );
    broadcastCancel( tor, index, begin, len - 8 );

    if( !tr_cpPieceHasAllBlocks( tor->completion, index ) )
    {
        return TR_OK;
    }

    /* Piece is complete, check it */
    if( ( ret = tr_ioHash( tor->io, index ) ) )
    {
        /* Follow Azureus' and uTorrent's lead in not counting corrupt
           pieces in our announce data, as it could give us a misleadingly
           bad share ratio . (Ticket #263) */
        const int byteCount = tr_pieceSize( index );
        if( tor->downloadedCur >= (uint64_t)byteCount )
            tor->downloadedCur -= byteCount;

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

static int reqCompare( const void * va, const void * vb )
{
    const tr_request_t * a = (const tr_request_t *) va;
    const tr_request_t * b = (const tr_request_t *) vb;
    if( a->index != b->index ) return a->index - b->index;
    if( a->begin != b->begin ) return a->begin - b->begin;
    return a->length - b->length;
}

static int parseCancel( tr_torrent_t * tor, tr_peer_t * peer,
                               uint8_t * p, int len )
{
    tr_info_t * inf = &tor->info;
    int index, begin, length;
    tr_request_t req;
    tr_list_t * l;

    if( len != 12 )
    {
        peer_dbg( "GET  cancel, invalid" );
        return TR_ERROR_ASSERT;
    }

    index = tr_ntohl( p );
    begin = tr_ntohl( &p[4] );
    length = tr_ntohl( &p[8] );

    if( inf->pieceCount <= index )
    {
        peer_dbg( "GET  cancel, invalid index" );
        return TR_ERROR_ASSERT;
    }
    if( tr_pieceSize( index ) < begin + length )
    {
        peer_dbg( "GET  cancel, invalid begin/length" );
        return TR_ERROR_ASSERT;
    }

    peer_dbg( "GET  cancel %d/%d (%d bytes)",
              index, begin, length );

    /* remove it from the outRequests list */
    req.index = index;
    req.begin = begin;
    req.length = length;
    while(( l = tr_list_find( peer->outRequests, reqCompare, &req ) )) {
        tr_request_t * r = (tr_request_t *) l->data;
        peer->outRequests = tr_list_remove_data( peer->outRequests, r );
        tr_free( r );
    }

    return TR_OK;
}

static int parsePort( tr_peer_t * peer, uint8_t * p, int len )
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

static int
parseMessageHeader( tr_peer_t * peer, uint8_t * buf, int buflen,
                    int * msgid, int * msglen )
{
    if( 4 > buflen )
    {
        return TR_NET_BLOCK;
    }

    /* Get payload size */
    *msglen = tr_ntohl( buf );

    if( 4 + *msglen > buflen )
    {
        /* We do not have the entire message */
        return TR_NET_BLOCK;
    }

    if( 0 == *msglen )
    {
        /* keep-alive */
        peer_dbg( "GET  keep-alive" );
        *msgid = AZ_MSG_BT_KEEP_ALIVE;
        return 4;
    }
    else
    {
        /* Type of the message */
        *msgid = buf[4];
        (*msglen)--;
        return 5;
    }
}

static int parseMessage( tr_torrent_t * tor, tr_peer_t * peer,
                                int id, uint8_t * p, int len )
{
    int extid;

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
            return parseRequest( tor, peer, p, len );
        case PEER_MSG_PIECE:
            return parsePiece( tor, peer, p, len );
        case PEER_MSG_CANCEL:
            return parseCancel( tor, peer, p, len );
        case PEER_MSG_PORT:
            return parsePort( peer, p, len );
        case PEER_MSG_EXTENDED:
            if( EXTENDED_NOT_SUPPORTED == peer->extStatus )
            {
                break;
            }
            if( 0 < len )
            {
                extid = p[0];
                p++;
                len--;
                if( EXTENDED_HANDSHAKE_ID == extid )
                {
                    return parseExtendedHandshake( peer, p, len );
                }
                else if( 0 < peer->pexStatus && extid == peer->pexStatus )
                {
                    return parseUTPex( tor, peer, p, len );
                }
                peer_dbg( "GET  unknown extended message '%hhu'", extid );
            }
            /* ignore the unknown extension */
            return 0;
        case AZ_MSG_BT_KEEP_ALIVE:
            return TR_OK;
        case AZ_MSG_AZ_PEER_EXCHANGE:
            if( peer->azproto && peer->pexStatus )
            {
                return parseAZPex( tor, peer, p, len );
            }
            break;
        case AZ_MSG_INVALID:
            return 0;
    }

    tr_err( "GET  unknown message '%d'", id );
    return TR_ERROR;
}

static int parseBufHeader( tr_peer_t * peer )
{
    static uint8_t badproto_http[] =
"HTTP/1.0 400 Nice try...\015\012"
"Content-type: text/plain\015\012"
"\015\012";
    static uint8_t badproto_tinfoil[] =
"This is not a rootkit or other backdoor, it's a BitTorrent\015\012"
"client. Really. Why should you be worried, can't you read this\015\012"
"reassuring message? Now just listen to this social engi, er, I mean,\015\012"
"completely truthful statement, and go about your business. Your box is\015\012"
"safe and completely impregnable, the marketing hype for your OS even\015\012"
"says so. You can believe everything you read. Now move along, nothing\015\012"
"to see here.";
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
        if( 0 == memcmp( p, "GET ", 4 ) || 0 == memcmp( p, "HEAD", 4 ) )
        {
            tr_netSend( peer->socket, badproto_http, sizeof badproto_http - 1 );
        }
        tr_netSend( peer->socket, badproto_tinfoil, sizeof badproto_tinfoil - 1 );
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

static const uint8_t * parseBufHash( const tr_peer_t * peer )
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

static int parseHandshake( tr_torrent_t * tor, tr_peer_t * peer )
{
    tr_info_t * inf = &tor->info;
    int         ii;

    if( memcmp( &peer->buf[28], inf->hash, SHA_DIGEST_LENGTH ) )
    {
        peer_dbg( "GET  handshake, wrong torrent hash" );
        return TR_ERROR;
    }

    if( !memcmp( &peer->buf[48], tor->peer_id, TR_ID_LEN ) )
    {
        /* We are connected to ourselves... */
        peer_dbg( "GET  handshake, that is us" );
        return TR_ERROR;
    }

    memcpy( peer->id, &peer->buf[48], TR_ID_LEN );

    for( ii = 0; ii < tor->peerCount; ii++ )
    {
        if( tor->peers[ii] == peer )
        {
            continue;
        }
        if( !peerCmp( peer, tor->peers[ii] ) )
        {
            peer_dbg( "GET  handshake, duplicate" );
            return TR_ERROR;
        }
    }

    if( PEER_SUPPORTS_EXTENDED_MESSAGES( &peer->buf[20] ) )
    {
        peer->status = PEER_STATUS_CONNECTED;
        peer->extStatus = EXTENDED_SUPPORTED;
        peer_dbg( "GET  handshake, ok (%s) extended messaging supported",
                  tr_peerClient( peer ) );
    }
    else if( PEER_SUPPORTS_AZUREUS_PROTOCOL( &peer->buf[20] ) )
    {
        peer->status  = PEER_STATUS_AZ_GIVER;
        peer->azproto = 1;
        peer->date    = tr_date();
        peer_dbg( "GET  handshake, ok (%s) will use azureus protocol",
                  tr_peerClient( peer ) );
    }
    else
    {
        peer->status = PEER_STATUS_CONNECTED;
        peer_dbg( "GET  handshake, ok (%s)", tr_peerClient( peer ) );
    }

    return TR_OK;
}

static int sendInitial( tr_torrent_t * tor, tr_peer_t * peer )
{
    if( PEER_STATUS_CONNECTED != peer->status )
    {
        return TR_OK;
    }

    if( EXTENDED_SUPPORTED == peer->extStatus )
    {
        if( sendExtended( tor, peer, EXTENDED_HANDSHAKE_ID ) )
        {
            return TR_ERROR;
        }
        peer->extStatus = EXTENDED_HANDSHAKE;
    }

    sendBitfield( tor, peer );

    return TR_OK;
}

static int parseBuf( tr_torrent_t * tor, tr_peer_t * peer )
{
    int       len, ret, msgid;
    uint8_t * buf;

    buf = peer->buf;

    if( peer->banned )
    {
        /* Don't even parse, we only stay connected */
        peer->pos = 0;
        return TR_OK;
    }

    while( peer->pos >= 4 )
    {
        if( PEER_STATUS_HANDSHAKE == peer->status )
        {
            ret = parseBufHeader( peer );
            if( ret )
            {
                return ret;
            }

            if( peer->pos < 68 )
            {
                break;
            }

            ret = parseHandshake( tor, peer );
            if( 0 > ret )
            {
                return ret;
            }
            buf       += 68;
            peer->pos -= 68;

            ret = sendInitial( tor, peer );
            if( ret )
            {
                return ret;
            }

            continue;
        }

        if( PEER_STATUS_AZ_RECEIVER == peer->status )
        {
            ret = parseAZMessageHeader( peer, buf, peer->pos, &msgid, &len );
            if( TR_NET_BLOCK & ret )
            {
                break;
            }
            else if( TR_NET_CLOSE & ret )
            {
                return TR_ERROR;
            }

            buf       += ret;
            peer->pos -= ret;
            assert( len <= peer->pos );
            if( AZ_MSG_AZ_HANDSHAKE != msgid ||
                parseAZHandshake( peer, buf, len ) )
            {
                return TR_ERROR;
            }
            buf         += len;
            peer->pos   -= len;
            assert( 0 <= peer->pos );
            peer->status = PEER_STATUS_CONNECTED;

            ret = sendInitial( tor, peer );
            if( ret )
            {
                return ret;
            }

            continue;
        }

        if( PEER_STATUS_CONNECTED != peer->status )
        {
            break;
        }

        if( peer->azproto )
        {
            ret = parseAZMessageHeader( peer, buf, peer->pos, &msgid, &len );
        }
        else
        {
            ret = parseMessageHeader( peer, buf, peer->pos, &msgid, &len );
        }
        if( TR_NET_BLOCK & ret )
        {
            break;
        }
        else if( TR_NET_CLOSE & ret )
        {
            return TR_ERROR;
        }

#if 0
        if( len > 8 + tor->blockSize )
        {
            /* This should never happen. Drop that peer */
            /* XXX could an extended message be longer than this? */
            peer_dbg( "message too large (%d bytes)", len );
            return TR_ERROR;
        }
#endif

        buf       += ret;
        peer->pos -= ret;
        assert( 0 <= peer->pos );

        if( ( ret = parseMessage( tor, peer, msgid, buf, len ) ) )
        {
            return ret;
        }

        buf       += len;
        peer->pos -= len;
        assert( 0 <= peer->pos );
    }

    if( 0 < peer->pos )
    {
        memmove( peer->buf, buf, peer->pos );
    }

    return TR_OK;
}
