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

static void updateInterest( tr_torrent_t * tor, tr_peer_t * peer );

/***********************************************************************
 * peerInit
 ***********************************************************************
 * Allocates a new tr_peer_t and returns a pointer to it.
 **********************************************************************/
static tr_peer_t * peerInit()
{
    tr_peer_t * peer;

    peer              = calloc( sizeof( tr_peer_t ), 1 );
    peertreeInit( &peer->sentPeers );
    peer->amChoking   = 1;
    peer->peerChoking = 1;
    peer->date        = tr_date();
    peer->keepAlive   = peer->date;
    peer->download    = tr_rcInit();
    peer->upload      = tr_rcInit();

    return peer;
}

static int peerCmp( tr_peer_t * peer1, tr_peer_t * peer2 )
{
    /* Wait until we got the peers' ids */
    if( peer1->status <= PEER_STATUS_HANDSHAKE ||
        peer2->status <= PEER_STATUS_HANDSHAKE )
    {
        return 1;
    }

    return memcmp( peer1->id, peer2->id, 20 );
}

static int checkPeer( tr_peer_t * peer )
{
    tr_torrent_t * tor = peer->tor;
    uint64_t       now;

    now = tr_date();

    if( peer->status < PEER_STATUS_CONNECTED &&
        now > peer->date + 8000 )
    {
        /* If it has been too long, don't wait for the socket
           to timeout - forget about it now */
        peer_dbg( "connection timeout" );
        return TR_ERROR;
    }

    /* Drop peers who haven't even sent a keep-alive within the
       last 3 minutes */
    if( now > peer->date + 180000 )
    {
        peer_dbg( "read timeout" );
        return TR_ERROR;
    }

    /* Drop peers which are supposed to upload but actually
       haven't sent anything within the last minute */
    if( peer->inRequestCount && now > peer->date + 60000 )
    {
        peer_dbg( "bad uploader" );
        return TR_ERROR;
    }

    if( PEER_STATUS_CONNECTED == peer->status )
    {
        /* Send keep-alive every 2 minutes */
        if( now > peer->keepAlive + 120000 )
        {
            sendKeepAlive( peer );
            peer->keepAlive = now;
        }

        /* Resend extended handshake if our public port changed */
        if( EXTENDED_HANDSHAKE == peer->extStatus && 
            tor->publicPort != peer->advertisedPort )
        {
            sendExtended( tor, peer, EXTENDED_HANDSHAKE_ID );
        }

        /* Send peer list */
        if( !peer->private && 0 < peer->pexStatus )
        {
            if( 0 == peer->lastPex )
            {
                /* randomize time when first pex message is sent */
                peer->lastPex = now - 1000 * tr_rand( PEX_INTERVAL );
            }
            if( now > peer->lastPex + 1000 * PEX_INTERVAL )
            {
                if( ( EXTENDED_HANDSHAKE == peer->extStatus &&
                      !sendExtended( tor, peer, EXTENDED_PEX_ID ) ) ||
                    ( peer->azproto && !sendAZPex( tor, peer ) ) )
                {
                    peer->lastPex = now + 1000 * tr_rand( PEX_INTERVAL / 10 );
                }
            }
        }
    }

    return TR_OK;
}

/***********************************************************************
 * isInteresting
 ***********************************************************************
 * Returns 1 if 'peer' has at least one piece that we haven't completed,
 * or 0 otherwise.
 **********************************************************************/
static int isInteresting( tr_torrent_t * tor, tr_peer_t * peer )
{
    int ii;
    tr_bitfield_t * bitfield = tr_cpPieceBitfield( tor->completion );

    if( !peer->bitfield )
    {
        /* We don't know what this peer has */
        return 0;
    }

    assert( bitfield->len == peer->bitfield->len );
    for( ii = 0; ii < bitfield->len; ii++ )
    {
        if( ( peer->bitfield->bits[ii] & ~(bitfield->bits[ii]) ) & 0xFF )
        {
            return 1;
        }
    }

    return 0;
}
static void updateInterest( tr_torrent_t * tor, tr_peer_t * peer )
{
    int interested = isInteresting( tor, peer );

    if( interested && !peer->amInterested )
    {
        sendInterest( peer, 1 );
    }
    if( !interested && peer->amInterested )
    {
        sendInterest( peer, 0 );
    }
}

/***********************************************************************
 * chooseBlock
 ***********************************************************************
 * At this point, we know the peer has at least one block we have an
 * interest in. If he has more than one, we choose which one we are
 * going to ask first.
 * Our main goal is to complete pieces, so we look the pieces which are
 * missing less blocks.
 **********************************************************************/
static inline int chooseBlock( tr_torrent_t * tor, tr_peer_t * peer )
{
    tr_info_t * inf = &tor->info;

    int i;
    int missingBlocks, minMissing;
    int poolSize, * pool;
    int block, minDownloading;

    /* Choose a piece */
    pool       = malloc( inf->pieceCount * sizeof( int ) );
    poolSize   = 0;
    minMissing = tor->blockCount + 1;
    for( i = 0; i < inf->pieceCount; i++ )
    {
        missingBlocks = tr_cpMissingBlocksForPiece( tor->completion, i );
        if( missingBlocks < 1 )
        {
            /* We already have or are downloading all blocks */
            continue;
        }
        if( !tr_bitfieldHas( peer->bitfield, i ) )
        {
            /* The peer doesn't have this piece */
            continue;
        }
        if( peer->banfield && tr_bitfieldHas( peer->banfield, i ) )
        {
            /* The peer is banned for this piece */
            continue;
        }

        /* We are interested in this piece, remember it */
        if( missingBlocks < minMissing )
        {
            minMissing = missingBlocks;
            poolSize   = 0;
        }
        if( missingBlocks <= minMissing )
        {
            pool[poolSize++] = i;
        }
    }

    if( poolSize )
    {
        /* All pieces in 'pool' have 'minMissing' missing blocks. Find
           the rarest ones. */
        tr_bitfield_t * bitfield;
        int piece;
        int min, foo, j;
        int * pool2;
        int   pool2Size;

        pool2     = malloc( poolSize * sizeof( int ) );
        pool2Size = 0;
        min       = TR_MAX_PEER_COUNT + 1;
        for( i = 0; i < poolSize; i++ )
        {
            foo = 0;
            for( j = 0; j < tor->peerCount; j++ )
            {
                bitfield = tor->peers[j]->bitfield;
                if( bitfield && tr_bitfieldHas( bitfield, pool[i] ) )
                {
                    foo++;
                }
            }
            if( foo < min )
            {
                min       = foo;
                pool2Size = 0;
            }
            if( foo <= min )
            {
                pool2[pool2Size++] = pool[i];
            }
        }
        free( pool );

        if( pool2Size < 1 )
        {
            /* Shouldn't happen */
            free( pool2 );
            return -1;
        }

        /* All pieces in pool2 have the same number of missing blocks,
           and are availabme from the same number of peers. Pick a
           random one */
        piece = pool2[tr_rand(pool2Size)];
        free( pool2 );

        /* Pick a block in this piece */
        block = tr_cpMissingBlockInPiece( tor->completion, piece );
        goto check;
    }

    free( pool );

    /* "End game" mode */
    minDownloading = 255;
    block = -1;
    for( i = 0; i < inf->pieceCount; i++ )
    {
        int downloaders, block2;
        if( !tr_bitfieldHas( peer->bitfield, i ) )
        {
            /* The peer doesn't have this piece */
            continue;
        }
        if( peer->banfield && tr_bitfieldHas( peer->banfield, i ) )
        {
            /* The peer is banned for this piece */
            continue;
        }
        if( tr_cpPieceIsComplete( tor->completion, i ) )
        {
            /* We already have it */
            continue;
        }
        block2 = tr_cpMostMissingBlockInPiece( tor->completion, i, &downloaders );
        if( block2 > -1 && downloaders < minDownloading )
        {
            block = block2;
            minDownloading = downloaders;
        }
    }

check:
    if( block < 0 )
    {
        /* Shouldn't happen */
        return -1;
    }

    for( i = 0; i < peer->inRequestCount; i++ )
    {
        tr_request_t * r;
        r = &peer->inRequests[i];
        if( tr_block( r->index, r->begin ) == block )
        {
            /* We are already asking this peer for this block */
            return -1;
        }
    }

    return block;
}
