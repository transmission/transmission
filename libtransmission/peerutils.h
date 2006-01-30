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
    peer->amChoking   = 1;
    peer->peerChoking = 1;
    peer->date        = tr_date();
    peer->keepAlive   = peer->date;

    return peer;
}

/***********************************************************************
 * peerAttach
 ***********************************************************************
 * Deallocates the tr_peer_t and returns 0 if we reached the maximum
 * authorized number of peers. Otherwise, adds the tr_peer_t to the
 * peers list.
 **********************************************************************/
static int peerAttach( tr_torrent_t * tor, tr_peer_t * peer )
{
    if( tor->peerCount >= TR_MAX_PEER_COUNT )
    {
        tr_peerDestroy( tor->fdlimit, peer );
        return 0;
    }

    tor->peers[tor->peerCount++] = peer;
    return 1;
}

static int peerCmp( tr_peer_t * peer1, tr_peer_t * peer2 )
{
    /* Wait until we got the peers' ids */
    if( peer1->status < PEER_STATUS_CONNECTED ||
        peer2->status < PEER_STATUS_CONNECTED )
    {
        return 1;
    }

    return memcmp( peer1->id, peer2->id, 20 );
}

/***********************************************************************
 * addWithAddr
 ***********************************************************************
 * Does nothing if we already have a peer matching 'addr' and 'port'.
 * Otherwise adds such a new peer.
 **********************************************************************/
static void addWithAddr( tr_torrent_t * tor, struct in_addr addr,
                         in_port_t port )
{
    int i;
    tr_peer_t * peer;

    for( i = 0; i < tor->peerCount; i++ )
    {
        peer = tor->peers[i];
        if( peer->addr.s_addr == addr.s_addr &&
            peer->port        == port )
        {
            /* We are already connected to this peer */
            return;
        }
    }

    peer = peerInit();
    if( !peerAttach( tor, peer ) )
    {
        return;
    }

    peer->addr   = addr;
    peer->port   = port;
    peer->status = PEER_STATUS_IDLE;
}

static int checkPeer( tr_torrent_t * tor, int i )
{
    tr_peer_t * peer = tor->peers[i];

    if( peer->status < PEER_STATUS_CONNECTED &&
        tr_date() > peer->date + 8000 )
    {
        /* If it has been too long, don't wait for the socket
           to timeout - forget about it now */
        peer_dbg( "connection timeout" );
        return 1;
    }

    /* Drop peers who haven't even sent a keep-alive within the
       last 3 minutes */
    if( tr_date() > peer->date + 180000 )
    {
        peer_dbg( "read timeout" );
        return 1;
    }

    /* Drop peers which are supposed to upload but actually
       haven't sent anything within the last minute */
    if( peer->inRequestCount && tr_date() > peer->date + 60000 )
    {
        peer_dbg( "bad uploader" );
        return 1;
    }

    /* TODO: check for bad downloaders */

#if 0
    /* Choke unchoked peers we are not sending anything to */
    if( !peer->amChoking && tr_date() > peer->outDate + 10000 )
    {
        peer_dbg( "not worth the unchoke" );
        if( sendChoke( peer, 1 ) )
        {
            goto dropPeer;
        }
        peer->outSlow = 1;
        tr_uploadChoked( tor->upload );
    }
#endif

    if( peer->status & PEER_STATUS_CONNECTED )
    {
        /* Send keep-alive every 2 minutes */
        if( tr_date() > peer->keepAlive + 120000 )
        {
            sendKeepAlive( peer );
            peer->keepAlive = tr_date();
        }

        /* Choke or unchoke some people */
        /* TODO: prefer people who upload to us */
        if( !peer->amChoking && !peer->peerInterested )
        {
            /* He doesn't need us */
            sendChoke( peer, 1 );
            //tr_uploadChoked( tor->upload );
        }
        if( peer->amChoking && peer->peerInterested /* &&
            !peer->outSlow && tr_uploadCanUnchoke( tor->upload ) */ )
        {
            sendChoke( peer, 0 );
            //tr_uploadUnchoked( tor->upload );
        }
    }

    /* Connect */
    if( ( peer->status & PEER_STATUS_IDLE ) &&
        !tr_fdSocketWillCreate( tor->fdlimit, 0 ) )
    {
        peer->socket = tr_netOpen( peer->addr, peer->port );
        if( peer->socket < 0 )
        {
            peer_dbg( "connection failed" );
            tr_fdSocketClosed( tor->fdlimit, 0 );
            return 1;
        }
        peer->status = PEER_STATUS_CONNECTING;
    }

    /* Try to send handshake */
    if( peer->status & PEER_STATUS_CONNECTING )
    {
        uint8_t buf[68];
        tr_info_t * inf = &tor->info;
        int ret;

        buf[0] = 19;
        memcpy( &buf[1], "BitTorrent protocol", 19 );
        memset( &buf[20], 0, 8 );
        memcpy( &buf[28], inf->hash, 20 );
        memcpy( &buf[48], tor->id, 20 );

        ret = tr_netSend( peer->socket, buf, 68 );
        if( ret & TR_NET_CLOSE )
        {
            peer_dbg( "connection closed" );
            return 1;
        }
        else if( !( ret & TR_NET_BLOCK ) )
        {
            peer_dbg( "SEND handshake" );
            peer->status = PEER_STATUS_HANDSHAKE;
        }
    }

    return 0;
}

/***********************************************************************
 * isInteresting
 ***********************************************************************
 * Returns 1 if 'peer' has at least one piece that we haven't completed,
 * or 0 otherwise.
 **********************************************************************/
static int isInteresting( tr_torrent_t * tor, tr_peer_t * peer )
{
    tr_info_t * inf = &tor->info;

    int i;
    int bitfieldSize = ( inf->pieceCount + 7 ) / 8;
    uint8_t * bitfield = tr_cpPieceBitfield( tor->completion );

    if( !peer->bitfield )
    {
        /* We don't know what this peer has */
        return 0;
    }

    for( i = 0; i < bitfieldSize; i++ )
    {
        if( ( peer->bitfield[i] & ~(bitfield[i]) ) & 0xFF )
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
        uint8_t * bitfield;
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
