/******************************************************************************
 * Copyright (c) 2006 Eric Petit
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

#include <math.h>
#include "transmission.h"

struct tr_choking_s
{
    tr_lock_t     lock;
    tr_handle_t * h;
    int           slots;
};

tr_choking_t * tr_chokingInit( tr_handle_t * h )
{
    tr_choking_t * c;

    c        = calloc( sizeof( tr_choking_t ), 1 );
    c->h     = h;
    c->slots = 4242;
    tr_lockInit( &c->lock );

    return c;
}

void tr_chokingSetLimit( tr_choking_t * c, int limit )
{
    tr_lockLock( &c->lock );
    if( limit < 0 )
        c->slots = 4242;
    else
        c->slots = lrintf( sqrt( 2 * limit ) );
    tr_lockUnlock( &c->lock );
}

static inline void sortPeers( tr_peer_t ** peers, int count )
{
    int i, j;
    tr_peer_t * tmp;

    for( i = count - 1; i > 0; i-- )
        for( j = 0; j < i; j++ )
        {
            if( tr_peerDownloadRate( peers[j] ) >
                    tr_peerDownloadRate( peers[j+1] ) )
            {
                tmp        = peers[j];
                peers[j]   = peers[j+1];
                peers[j+1] = tmp;
            }
        }
}

void tr_chokingPulse( tr_choking_t * c )
{
    int i, j, peersTotalCount;
    tr_peer_t * peer;
    tr_peer_t ** peersCanChoke, ** peersCanUnchoke;
    int peersCanChokeCount, peersCanUnchokeCount, unchokedCount;
    tr_torrent_t * tor;
    uint64_t now = tr_date();

    tr_lockLock( &c->lock );

    /* Lock all torrents and get the total number of peers */
    peersTotalCount = 0;
    for( i = 0; i < c->h->torrentCount; i++ )
    {
        tor = c->h->torrents[i];
        tr_lockLock( &tor->lock );
        peersTotalCount += tor->peerCount;
    }

    peersCanChoke   = malloc( peersTotalCount * sizeof( tr_peer_t * ) );
    peersCanUnchoke = malloc( peersTotalCount * sizeof( tr_peer_t * ) );
    peersCanChokeCount   = 0;
    peersCanUnchokeCount = 0;
    unchokedCount        = 0;

    /* Build two lists of interested peers: those who may choke,
       those who may unchoke */
    for( i = 0; i < c->h->torrentCount; i++ )
    {
        tor = c->h->torrents[i];
        for( j = 0; j < tor->peerCount; j++ )
        {
            peer = tor->peers[j];

            if( !tr_peerIsConnected( peer ) )
                continue;

            if( !tr_peerIsInterested( peer ) )
            {
                if( tr_peerIsUnchoked( peer ) )
                    tr_peerChoke( peer );
                continue;
            }

            if( tr_peerIsUnchoked( peer ) )
            {
                unchokedCount++;
                if( tr_peerLastChoke( peer ) + 10000 < now )
                    peersCanChoke[peersCanChokeCount++] = peer;
            }
            else
            {
                if( tr_peerLastChoke( peer ) + 10000 < now )
                    peersCanUnchoke[peersCanUnchokeCount++] = peer;
            }
        }
    }

    sortPeers( peersCanChoke, peersCanChokeCount );
    sortPeers( peersCanUnchoke, peersCanUnchokeCount );

    if( unchokedCount > c->slots && peersCanChokeCount > 0 )
    {
        int willChoke;
        willChoke = MIN( peersCanChokeCount, unchokedCount - c->slots );
        for( i = 0; i < willChoke; i++ )
            tr_peerChoke( peersCanChoke[i] );
        peersCanChokeCount -= willChoke;
        memmove( &peersCanChoke[0], &peersCanChoke[willChoke],
                 peersCanChokeCount );
    }
    else if( unchokedCount < c->slots && peersCanUnchokeCount > 0 )
    {
        int willUnchoke;
        willUnchoke = MIN( peersCanUnchokeCount, c->slots - unchokedCount );
        for( i = 0; i < willUnchoke; i++ )
            tr_peerUnchoke( peersCanUnchoke[peersCanUnchokeCount - i - 1] );
        peersCanUnchokeCount -= willUnchoke;
    }

    while( peersCanChokeCount > 0 && peersCanUnchokeCount > 0 )
    {
        if( tr_peerDownloadRate( peersCanUnchoke[peersCanUnchokeCount - 1] )
                < tr_peerDownloadRate( peersCanChoke[0] ) )
            break;

        tr_peerChoke( peersCanChoke[0] );
        tr_peerUnchoke( peersCanUnchoke[peersCanUnchokeCount - 1] );
        peersCanChokeCount--;
        peersCanUnchokeCount--;
        memmove( &peersCanChoke[0], &peersCanChoke[1], peersCanChokeCount );
    }

    free( peersCanChoke );
    free( peersCanUnchoke );

    /* Unlock all torrents */
    for( i = 0; i < c->h->torrentCount; i++ )
    {
        tr_lockUnlock( &c->h->torrents[i]->lock );
    }

    tr_lockUnlock( &c->lock );
}

void tr_chokingClose( tr_choking_t * c )
{
    tr_lockClose( &c->lock );
    free( c );
}
