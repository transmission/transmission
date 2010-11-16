/*
 * This file Copyright (C) 2010 Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#include <assert.h>

#include "transmission.h"
#include "history.h"
#include "utils.h"

struct history_slice
{
    unsigned int n;
    time_t date;
};

struct tr_recentHistory
{
    int newest;
    int sliceCount;
    unsigned int precision;
    struct history_slice * slices;
};

void
tr_historyAdd( tr_recentHistory * h, time_t now, unsigned int n )
{
    if( h->slices[h->newest].date + h->precision >= now )
        h->slices[h->newest].n += n;
    else {
        if( ++h->newest == h->sliceCount ) h->newest = 0;
        h->slices[h->newest].date = now;
        h->slices[h->newest].n = n;
    }
}

unsigned int
tr_historyGet( const tr_recentHistory * h, time_t now, unsigned int sec )
{
    unsigned int n = 0;
    const time_t cutoff = (now?now:tr_time()) - sec;
    int i = h->newest;

    for( ;; )
    {
        if( h->slices[i].date <= cutoff )
            break;

        n += h->slices[i].n;

        if( --i == -1 ) i = h->sliceCount - 1; /* circular history */
        if( i == h->newest ) break; /* we've come all the way around */
    }

    return n;
}

tr_recentHistory *
tr_historyNew( unsigned int seconds, unsigned int precision )
{
    tr_recentHistory * h;

    assert( precision <= seconds );

    h = tr_new0( tr_recentHistory, 1 );
    h->precision = precision;
    h->sliceCount = seconds / precision;
    h->slices = tr_new0( struct history_slice, h->sliceCount );

    return h;
}

void
tr_historyFree( tr_recentHistory * h )
{
    tr_free( h->slices );
    tr_free( h );
}
