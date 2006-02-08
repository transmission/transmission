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

#ifdef SYS_BEOS
#define lrintf(a) ((int)(0.5+(a)))
#endif

/* We may try to allocate and free tables of size 0. Quick and dirty
   way to handle it... */
void * __malloc( int size )
{
    if( !size )
        return NULL;
    return malloc( size );
}
void __free( void * p )
{
    if( p )
        free( p );
}
#define malloc __malloc
#define free   __free

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
        /* Reckon a number of slots from the upload limit. There is no
           official right way to do this, the formula below e.g. gives:
            10  KB/s -> 4  * 2.50 KB/s
            20  KB/s -> 6  * 3.33 KB/s
            50  KB/s -> 10 * 5.00 KB/s
            100 KB/s -> 14 * 7.14 KB/s */
        c->slots = lrintf( sqrt( 2 * limit ) );
    tr_lockUnlock( &c->lock );
}

#define sortPeersAscending(a,ac,z,zc,n,nc)  sortPeers(a,ac,z,zc,n,nc,0)
#define sortPeersDescending(a,ac,z,zc,n,nc) sortPeers(a,ac,z,zc,n,nc,1)
static inline void sortPeers( tr_peer_t ** all, int allCount,
                              tr_peer_t ** zero, int * zeroCount,
                              tr_peer_t ** nonZero, int * nonZeroCount,
                              int order )
{
    int i, shuffle;

    /* Seperate uploaders from non-uploaders */
    *zeroCount    = 0;
    *nonZeroCount = 0;
    for( i = 0; i < allCount; i++ )
    {
        if( tr_peerDownloadRate( all[i] ) < 0.1 )
            zero[(*zeroCount)++] = all[i];
        else
            nonZero[(*nonZeroCount)++] = all[i];
    }

    /* Randomly shuffle non-uploaders, so they are treated equally */
    if( *zeroCount && ( shuffle = tr_rand( *zeroCount ) ) )
    {
        tr_peer_t ** bak;
        bak = malloc( shuffle * sizeof( tr_peer_t * ) );
        memcpy( bak, zero, shuffle * sizeof( tr_peer_t * ) );
        memmove( zero, &zero[shuffle],
                 ( *zeroCount - shuffle ) * sizeof( tr_peer_t * ) );
        memcpy( &zero[*zeroCount - shuffle], bak,
                 shuffle * sizeof( tr_peer_t * ) );
        free( bak );
    }

    /* Sort uploaders by download rate */
    for( i = *nonZeroCount - 1; i > 0; i-- )
    {
        float rate1, rate2;
        tr_peer_t * tmp;
        int j, sorted;

        sorted = 1;
        for( j = 0; j < i; j++ )
        {
            rate1 = tr_peerDownloadRate( nonZero[j] );
            rate2 = tr_peerDownloadRate( nonZero[j+1] );
            if( order ? ( rate1 < rate2 ) : ( rate1 > rate2 ) )
            {
                tmp          = nonZero[j];
                nonZero[j]   = nonZero[j+1];
                nonZero[j+1] = tmp;
                sorted       = 0;
            }
        }
        if( sorted )
            break;
    }
}

void tr_chokingPulse( tr_choking_t * c )
{
    int i, peersTotalCount, unchoked;
    tr_peer_t ** canChoke, ** canUnchoke;
    tr_peer_t ** canChokeZero, ** canUnchokeZero;
    tr_peer_t ** canChokeNonZero, ** canUnchokeNonZero;
    int canChokeCount, canUnchokeCount;
    int canChokeZeroCount, canUnchokeZeroCount;
    int canChokeNonZeroCount, canUnchokeNonZeroCount;
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

    canChoke   = malloc( peersTotalCount * sizeof( tr_peer_t * ) );
    canUnchoke = malloc( peersTotalCount * sizeof( tr_peer_t * ) );
    canChokeCount   = 0;
    canUnchokeCount = 0;
    unchoked        = 0;

    for( i = 0; i < c->h->torrentCount; i++ )
    {
        tr_peer_t * peer;
        int j;

        tor = c->h->torrents[i];
        for( j = 0; j < tor->peerCount; j++ )
        {
            peer = tor->peers[j];

            if( !tr_peerIsConnected( peer ) )
                continue;

            /* Choke peers who have lost their interest in us */
            if( !tr_peerIsInterested( peer ) )
            {
                if( tr_peerIsUnchoked( peer ) )
                    tr_peerChoke( peer );
                continue;
            }

            /* Build two lists of interested peers: those we may choke,
               those we may unchoke. Whatever happens, we never choke a
               peer less than 10 seconds after the time we unchoked him
               (or the other way around). */
            if( tr_peerIsUnchoked( peer ) )
            {
                unchoked++;
                if( tr_peerLastChoke( peer ) + 10000 < now )
                    canChoke[canChokeCount++] = peer;
            }
            else
            {
                if( tr_peerLastChoke( peer ) + 10000 < now )
                    canUnchoke[canUnchokeCount++] = peer;
            }
        }
    }

    canChokeZero      = malloc( canChokeCount * sizeof( tr_peer_t * ) );
    canChokeNonZero   = malloc( canChokeCount * sizeof( tr_peer_t * ) );
    canUnchokeZero    = malloc( canUnchokeCount * sizeof( tr_peer_t * ) );
    canUnchokeNonZero = malloc( canUnchokeCount * sizeof( tr_peer_t * ) );

    sortPeersDescending( canChoke, canChokeCount,
                         canChokeZero, &canChokeZeroCount,
                         canChokeNonZero, &canChokeNonZeroCount);
    sortPeersAscending( canUnchoke, canUnchokeCount,
                        canUnchokeZero, &canUnchokeZeroCount,
                        canUnchokeNonZero, &canUnchokeNonZeroCount);

    free( canChoke );
    free( canUnchoke );

    /* If we have more open slots than what we should have (the user has
       just lowered his upload limit), we need to choke some of the
       peers we are uploading to. We start with the peers who aren't
       uploading to us, then those we upload the least. */
    while( unchoked > c->slots && canChokeZeroCount > 0 )
    {
        tr_peerChoke( canChokeZero[--canChokeZeroCount] );
        unchoked--;
    }
    while( unchoked > c->slots && canChokeNonZeroCount > 0 )
    {
        tr_peerChoke( canChokeNonZero[--canChokeNonZeroCount] );
        unchoked--;
    }

    /* If we have unused open slots, let's unchoke some people. We start
       with the peers who are uploading to us the most. */
    while( unchoked < c->slots && canUnchokeNonZeroCount > 0 )
    {
        tr_peerUnchoke( canUnchokeNonZero[--canUnchokeNonZeroCount] );
        unchoked++;
    }
    while( unchoked < c->slots && canUnchokeZeroCount > 0 )
    {
        tr_peerUnchoke( canUnchokeZero[--canUnchokeZeroCount] );
        unchoked++;
    }

    /* Choke peers who aren't uploading if there are good peers waiting
       for an unchoke */
    while( canChokeZeroCount > 0 && canUnchokeNonZeroCount > 0 )
    {
        tr_peerChoke( canChokeZero[--canChokeZeroCount] );
        tr_peerUnchoke( canUnchokeNonZero[--canUnchokeNonZeroCount] );
    }

    /* Choke peers who aren't uploading that much if there are choked
       peers who are uploading more */
    while( canChokeNonZeroCount > 0 && canUnchokeNonZeroCount > 0 )
    {
        if( tr_peerDownloadRate( canUnchokeNonZero[canUnchokeNonZeroCount - 1] )
            < tr_peerDownloadRate( canChokeNonZero[canChokeNonZeroCount - 1] ) )
            break;

        tr_peerChoke( canChokeNonZero[--canChokeNonZeroCount] );
        tr_peerUnchoke( canUnchokeNonZero[--canUnchokeNonZeroCount] );
    }

    /* Some unchoked peers still aren't uploading to us, let's give a
       chance to other non-uploaders */
    while( canChokeZeroCount > 0 && canUnchokeZeroCount > 0 )
    {
        tr_peerChoke( canChokeZero[--canChokeZeroCount] );
        tr_peerUnchoke( canUnchokeZero[--canUnchokeZeroCount] );
    }

    free( canChokeZero );
    free( canChokeNonZero );
    free( canUnchokeZero );
    free( canUnchokeNonZero );

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
