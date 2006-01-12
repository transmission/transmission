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

#define FOO 10

struct tr_upload_s
{
    tr_lock_t lock;
    int       limit;      /* Max upload rate in KB/s */
    int       count;      /* Number of peers currently unchoked */
    uint64_t  dates[FOO]; /* The last times we uploaded something */
    int       sizes[FOO]; /* How many bytes we uploaded */
};

tr_upload_t * tr_uploadInit()
{
    tr_upload_t * u;

    u = calloc( sizeof( tr_upload_t ), 1 );
    tr_lockInit( &u->lock );

    return u;
}

void tr_uploadSetLimit( tr_upload_t * u, int limit )
{
    tr_lockLock( u->lock );
    u->limit = limit;
    tr_lockUnlock( u->lock );
}

int tr_uploadCanUnchoke( tr_upload_t * u )
{
    int ret;

    tr_lockLock( u->lock );
    if( u->limit < 0 )
    {
        /* Infinite number of slots */
        ret = 1;
    }
    else
    {
        /* One slot per 2 KB/s */
        ret = ( u->count < ( u->limit + 1 ) / 2 );
    }
    tr_lockUnlock( u->lock );

    return ret;
}

void tr_uploadChoked( tr_upload_t * u )
{
    tr_lockLock( u->lock );
    (u->count)--;
    tr_lockUnlock( u->lock );
}

void tr_uploadUnchoked( tr_upload_t * u )
{
    tr_lockLock( u->lock );
    (u->count)++;
    tr_lockUnlock( u->lock );
}

int tr_uploadCanUpload( tr_upload_t * u )
{
    int ret, i, size;
    uint64_t now;

    tr_lockLock( u->lock );
    if( u->limit < 0 )
    {
        /* No limit */
        ret = 1;
    }
    else
    {
        ret  = 0;
        size = 0;
        now  = tr_date();

        /* Check the last four times we sent something and decide if
           we must wait */
        for( i = 0; i < FOO; i++ )
        {
            size += u->sizes[i];
            if( (uint64_t) size < 1024ULL *
                    u->limit * ( now - u->dates[i] ) / 1000 )
            {
                ret = 1;
                break;
            }
        }
    }
    tr_lockUnlock( u->lock );

    return ret;
}

void tr_uploadUploaded( tr_upload_t * u, int size )
{
    tr_lockLock( u->lock );
    memmove( &u->dates[1], &u->dates[0], (FOO-1) * sizeof( uint64_t ) );
    memmove( &u->sizes[1], &u->sizes[0], (FOO-1) * sizeof( int ) );
    u->dates[0] = tr_date();
    u->sizes[0] = size;
    tr_lockUnlock( u->lock );
}

void tr_uploadClose( tr_upload_t * u )
{
    tr_lockClose( u->lock );
    free( u );
}
