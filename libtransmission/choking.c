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

#include "transmission.h"

#define MAX_HISTORY 30

struct tr_choking_s
{
    tr_lock_t     lock;
    tr_handle_t * h;
    int           slotsMax;
    int           slotsUsed;
};

tr_choking_t * tr_chokingInit( tr_handle_t * h )
{
    tr_choking_t * c;

    c           = calloc( sizeof( tr_choking_t ), 1 );
    c->h        = h;
    c->slotsMax = 4242;
    tr_lockInit( &c->lock );

    return c;
}

void tr_chokingSetLimit( tr_choking_t * c, int limit )
{
    tr_lockLock( &c->lock );
    if( limit < 0 )
        c->slotsMax = 4242;
    else
        c->slotsMax = lrintf( sqrt( 2 * limit ) );
    tr_lockUnlock( &c->lock );
}

void tr_chokingPulse( tr_choking_t * c )
{
    tr_lockLock( &c->lock );
    tr_lockUnlock( &c->lock );
}

void tr_chokingClose( tr_choking_t * c )
{
    tr_lockClose( &c->lock );
    free( c );
}
