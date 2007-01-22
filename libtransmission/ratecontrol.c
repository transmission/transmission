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

#include "transmission.h"

/* Maximum number of packets we keep track of. Since most packets are
 * 1 KB, it means we remember the last 2 MB transferred */
#define HISTORY_SIZE 2048

/* How far back we go to calculate rates to be displayed in the 
 * interface */
#define LONG_INTERVAL 30000 /* 30 secs */

/* How far back we go to calculate pseudo-instantaneous transfer rates,
 * for the actual rate control */
#define SHORT_INTERVAL 1000 /* 1 sec */


/***********************************************************************
 * Structures
 **********************************************************************/
typedef struct tr_transfer_s
{
    uint64_t date;
    int      size;
}
tr_transfer_t;

struct tr_ratecontrol_s
{
    tr_lock_t     lock;
    int           limit;

    /* Circular history: it's empty if transferStop == transferStart,
     * full if ( transferStop + 1 ) % HISTORY_SIZE == transferStart */
    tr_transfer_t transfers[HISTORY_SIZE];
    int           transferStart;
    int           transferStop;
};


/***********************************************************************
 * Local prototypes
 **********************************************************************/
static float rateForInterval( tr_ratecontrol_t * r, int interval );


/***********************************************************************
 * Exported functions
 **********************************************************************/

tr_ratecontrol_t * tr_rcInit()
{
    tr_ratecontrol_t * r;

    r        = calloc( sizeof( tr_ratecontrol_t ), 1 );
    r->limit = -1;
    tr_lockInit( &r->lock );

    return r;
}

int tr_rcCanGlobalTransfer( tr_handle_t * h, int isUpload )
{
    tr_torrent_t * tor;
    tr_ratecontrol_t * r;
    float rate = 0;
    int limit = isUpload ? h->uploadLimit : h->downloadLimit;
    
    if( limit <= 0 )
    {
        return limit < 0;
    }
    
    for( tor = h->torrentList; tor; tor = tor->next )
    {
        if( tor->customSpeedLimit )
        {
            continue;
        }
        
        r = isUpload ? tor->upload : tor->download;
        tr_lockLock( &r->lock );
        rate += rateForInterval( r, SHORT_INTERVAL );
        tr_lockUnlock( &r->lock );
        
        if( rate >= (float)limit )
        {
            return 0;
        }
    }
    
    return 1;
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
    ret = ( r->limit <= 0 ) ? ( r->limit < 0 ) :
            ( rateForInterval( r, SHORT_INTERVAL ) < r->limit );
    tr_lockUnlock( &r->lock );

    return ret;
}

void tr_rcTransferred( tr_ratecontrol_t * r, int size )
{
    tr_transfer_t * t;

    if( size < 100 )
    {
        /* Don't count small messages */
        return;
    }
    
    tr_lockLock( &r->lock );

    r->transferStop = ( r->transferStop + 1 ) % HISTORY_SIZE;
    if( r->transferStop == r->transferStart )
        /* History is full, forget about the first (oldest) item */
        r->transferStart = ( r->transferStart + 1 ) % HISTORY_SIZE;

    t = &r->transfers[r->transferStop];
    t->date = tr_date();
    t->size = size;

    tr_lockUnlock( &r->lock );
}

float tr_rcRate( tr_ratecontrol_t * r )
{
    float ret;

    tr_lockLock( &r->lock );
    ret = rateForInterval( r, LONG_INTERVAL );
    tr_lockUnlock( &r->lock );

    return ret;
}

void tr_rcReset( tr_ratecontrol_t * r )
{
    tr_lockLock( &r->lock );
    r->transferStart = 0;
    r->transferStop = 0;
    tr_lockUnlock( &r->lock );
}

void tr_rcClose( tr_ratecontrol_t * r )
{
    tr_rcReset( r );
    tr_lockClose( &r->lock );
    free( r );
}


/***********************************************************************
 * Local functions
 **********************************************************************/

/***********************************************************************
 * rateForInterval
 ***********************************************************************
 * Returns the transfer rate in KB/s on the last 'interval'
 * milliseconds
 **********************************************************************/
static float rateForInterval( tr_ratecontrol_t * r, int interval )
{
    tr_transfer_t * t = NULL;
    uint64_t now, start;
    int i, total;

    now = tr_date();
    start = now - interval;

    /* Browse the history back in time */
    total = 0;
    for( i = r->transferStop; i != r->transferStart; i-- )
    {
        t = &r->transfers[i];
        if( t->date < start )
            break;

        total += t->size;

        if( !i )
            i = HISTORY_SIZE; /* Loop */
    }
    if( ( r->transferStop + 1 ) % HISTORY_SIZE == r->transferStart
        && i == r->transferStart )
    {
        /* High bandwidth -> the history isn't big enough to remember
         * everything transferred since 'interval' ms ago. Correct the
         * interval so that we return the correct rate */
        interval = now - t->date;
    }

    return ( 1000.0f / 1024.0f ) * total / interval;
}

