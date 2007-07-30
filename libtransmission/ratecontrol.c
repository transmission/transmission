/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2006 Transmission authors and contributors
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

#include <string.h>

#include "transmission.h"
#include "shared.h"

#define GRANULARITY_MSEC 250
#define SHORT_INTERVAL_MSEC 3000
#define LONG_INTERVAL_MSEC 20000
#define HISTORY_SIZE (LONG_INTERVAL_MSEC / GRANULARITY_MSEC)

typedef struct
{
    uint64_t date;
    int      size;
}
tr_transfer_t;

struct tr_ratecontrol_s
{
    tr_rwlock_t * lock;
    int limit;
    int newest;
    tr_transfer_t transfers[HISTORY_SIZE];
};

/* return the xfer rate over the last `interval' seconds in KiB/sec */
static float
rateForInterval( const tr_ratecontrol_t * r, int interval_msec )
{
    uint64_t bytes = 0;
    const uint64_t now = tr_date ();
    int i = r->newest;
    for( ;; )
    {
        if( r->transfers[i].date + interval_msec < now )
            break;

        bytes += r->transfers[i].size;

        if( --i == -1 ) i = HISTORY_SIZE - 1; /* circular history */
        if( i == r->newest ) break; /* we've come all the way around */
    }

    return (bytes/1024.0) * (1000.0/interval_msec);
}

/***
****
***/

tr_ratecontrol_t*
tr_rcInit( void )
{
    tr_ratecontrol_t * r = tr_new0( tr_ratecontrol_t, 1 );
    r->limit = 0;
    r->lock = tr_rwNew( );
    return r;
}

void
tr_rcClose( tr_ratecontrol_t * r )
{
    tr_rcReset( r );
    tr_rwFree( r->lock );
    tr_free( r );
}

/***
****
***/

int
tr_rcCanTransfer( const tr_ratecontrol_t * r )
{
    int ret;
    tr_rwReaderLock( (tr_rwlock_t*)r->lock );

    ret = rateForInterval( r, SHORT_INTERVAL_MSEC ) < r->limit;

    tr_rwReaderUnlock( (tr_rwlock_t*)r->lock );
    return ret;
}

float
tr_rcRate( const tr_ratecontrol_t * r )
{
    float ret;
    tr_rwReaderLock( (tr_rwlock_t*)r->lock );

    ret = rateForInterval( r, LONG_INTERVAL_MSEC );

    tr_rwReaderUnlock( (tr_rwlock_t*)r->lock );
    return ret;
}

/***
****
***/

void
tr_rcTransferred( tr_ratecontrol_t * r, int size )
{
    uint64_t now;

    if( size < 100 ) /* don't count small messages */
        return;
    
    tr_rwWriterLock( r->lock );

    now = tr_date ();
    if( r->transfers[r->newest].date + GRANULARITY_MSEC >= now )
        r->transfers[r->newest].size += size;
    else {
        if( ++r->newest == HISTORY_SIZE ) r->newest = 0;
        r->transfers[r->newest].date = now;
        r->transfers[r->newest].size = size;
    }

    tr_rwWriterUnlock( r->lock );
}

void
tr_rcReset( tr_ratecontrol_t * r )
{
    tr_rwWriterLock( r->lock );
    r->newest = 0;
    memset( r->transfers, 0, sizeof(tr_transfer_t) * HISTORY_SIZE );
    tr_rwWriterUnlock( r->lock );
}

void
tr_rcSetLimit( tr_ratecontrol_t * r, int limit )
{
    tr_rwWriterLock( r->lock );
    r->limit = limit;
    tr_rwWriterUnlock( r->lock );
}

int
tr_rcGetLimit( const tr_ratecontrol_t * r )
{
    return r->limit;
}
