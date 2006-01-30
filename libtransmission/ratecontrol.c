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

#include "transmission.h"

#define MAX_HISTORY 30

typedef struct tr_transfer_s tr_transfer_t;
struct tr_ratecontrol_s
{
    tr_lock_t       lock;
    int             limit;
    tr_transfer_t * first;
    tr_transfer_t * last;
};
struct tr_transfer_s
{
    uint64_t        date;
    int             size;
    tr_transfer_t * next;
    tr_transfer_t * prev;
};

/***********************************************************************
 * rateForInterval
 ***********************************************************************
 * Returns the transfer rate on the last 'interval' milliseconds
 **********************************************************************/
static inline float rateForInterval( tr_ratecontrol_t * r, int interval )
{
    tr_transfer_t * t;
    uint64_t        start = tr_date() - interval;
    int             total = 0;

    for( t = r->first; t && t->date > start; t = t->next )
    {
        total += t->size;
    }

    return ( 1000.0 / 1024.0 ) * total / interval;
}

static inline void cleanOldTransfers( tr_ratecontrol_t * r )
{
    tr_transfer_t * t, * prev;
    uint64_t        old = tr_date() - MAX_HISTORY * 1000;

    for( t = r->last; t && t->date < old; )
    {
        prev = t->prev;
        prev->next = NULL;
        free( t );
        t = prev;
    }
}

tr_ratecontrol_t * tr_rcInit()
{
    tr_ratecontrol_t * r;

    r        = calloc( sizeof( tr_ratecontrol_t ), 1 );
    r->limit = -1;
    tr_lockInit( &r->lock );

    return r;
}

void tr_rcSetLimit( tr_ratecontrol_t * r, int limit )
{
    tr_lockLock( &r->lock );
    r->limit = limit;
    tr_lockUnlock( &r->lock );
}

int tr_rcCanTransfer( tr_ratecontrol_t * r )
{
    int ret;

    tr_lockLock( &r->lock );
    ret = ( r->limit < -1 ) || ( rateForInterval( r, 1000 ) < r->limit );
    tr_lockUnlock( &r->lock );

    return ret;
}

void tr_rcTransferred( tr_ratecontrol_t * r, int size )
{
    tr_transfer_t * t;
    
    tr_lockLock( &r->lock );
    t = malloc( sizeof( tr_transfer_t ) );

    if( r->first )
        r->first->prev = t;
    t->next  = r->first;
    t->prev  = NULL;
    r->first = t;

    t->date  = tr_date();
    t->size  = size;

    cleanOldTransfers( r );
    tr_lockUnlock( &r->lock );
}

float tr_rcRate( tr_ratecontrol_t * r )
{
    float ret;

    tr_lockLock( &r->lock );
    ret = rateForInterval( r, MAX_HISTORY * 1000 );
    tr_lockUnlock( &r->lock );

    return ret;
}

void tr_rcClose( tr_ratecontrol_t * r )
{
    tr_transfer_t * t, * next;
    for( t = r->first; t; )
    {
        next = t->next;
        free( t );
        t = next;
    }
    tr_lockClose( &r->lock );
    free( r );
}
