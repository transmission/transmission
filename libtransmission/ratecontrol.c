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

#include <string.h> /* memset */

#include "transmission.h"
#include "platform.h"
#include "ratecontrol.h"
#include "utils.h"

#define GRANULARITY_MSEC 100
#define SHORT_INTERVAL_MSEC 3000
#define LONG_INTERVAL_MSEC 6000
#define HISTORY_SIZE (LONG_INTERVAL_MSEC / GRANULARITY_MSEC)

struct tr_transfer
{
    uint64_t date;
    uint64_t size;
};

struct tr_ratecontrol
{
    tr_lock * lock;
    int limit;
    int newest;
    struct tr_transfer transfers[HISTORY_SIZE];
};

/* return the xfer rate over the last `interval' seconds in KiB/sec */
static float
rateForInterval( const tr_ratecontrol * r, int interval_msec )
{
    uint64_t bytes = 0;
    const uint64_t cutoff = tr_date () - interval_msec;
    int i = r->newest;
    for( ;; )
    {
        if( r->transfers[i].date < cutoff )
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

tr_ratecontrol*
tr_rcInit( void )
{
    tr_ratecontrol * r = tr_new0( tr_ratecontrol, 1 );
    r->limit = 0;
    r->lock = tr_lockNew( );
    return r;
}

void
tr_rcClose( tr_ratecontrol * r )
{
    tr_rcReset( r );
    tr_lockFree( r->lock );
    tr_free( r );
}

/***
****
***/

size_t
tr_rcBytesLeft( const tr_ratecontrol * r )
{
    size_t bytes = 0;

    if( r != NULL )
    {
        float cur, max, kb;
 
        tr_lockLock( (tr_lock*)r->lock );

        cur = rateForInterval( r, SHORT_INTERVAL_MSEC );
        max = r->limit;
        kb = max>cur ? max-cur : 0;
        bytes = (size_t)(kb * 1024u);

        tr_lockUnlock( (tr_lock*)r->lock );
    }

    return bytes;
}

float
tr_rcRate( const tr_ratecontrol * r )
{
    float ret;

    if( r == NULL )
        ret = 0.0f;
    else {
        tr_lockLock( (tr_lock*)r->lock );
        ret = rateForInterval( r, LONG_INTERVAL_MSEC );
        tr_lockUnlock( (tr_lock*)r->lock );
    }

    return ret;
}

/***
****
***/

void
tr_rcTransferred( tr_ratecontrol * r, size_t size )
{
    uint64_t now;

    if( size < 100 ) /* don't count small messages */
        return;
    
    tr_lockLock( (tr_lock*)r->lock );

    now = tr_date ();
    if( r->transfers[r->newest].date + GRANULARITY_MSEC >= now )
        r->transfers[r->newest].size += size;
    else {
        if( ++r->newest == HISTORY_SIZE ) r->newest = 0;
        r->transfers[r->newest].date = now;
        r->transfers[r->newest].size = size;
    }

    tr_lockUnlock( (tr_lock*)r->lock );
}

void
tr_rcReset( tr_ratecontrol * r )
{
    tr_lockLock( (tr_lock*)r->lock );
    r->newest = 0;
    memset( r->transfers, 0, sizeof(struct tr_transfer) * HISTORY_SIZE );
    tr_lockUnlock( (tr_lock*)r->lock );
}

void
tr_rcSetLimit( tr_ratecontrol * r, int limit )
{
    tr_lockLock( (tr_lock*)r->lock );
    r->limit = limit;
    tr_lockUnlock( (tr_lock*)r->lock );
}

int
tr_rcGetLimit( const tr_ratecontrol * r )
{
    return r->limit;
}
