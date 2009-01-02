/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2006-2008 Transmission authors and contributors
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

/* return the xfer rate over the last `interval' seconds in KiB/sec */
static float
rateForInterval( const tr_ratecontrol * r,
                 int                    interval_msec )
{
    uint64_t       bytes = 0;
    const uint64_t cutoff = tr_date ( ) - interval_msec;
    int            i = r->newest;

    for( ; ; )
    {
        if( r->transfers[i].date <= cutoff )
            break;

        bytes += r->transfers[i].size;

        if( --i == -1 ) i = TR_RC_HISTORY_SIZE - 1; /* circular history */
        if( i == r->newest ) break; /* we've come all the way around */
    }

    return ( bytes / 1024.0 ) * ( 1000.0 / interval_msec );
}

/***
****
***/

float
tr_rcRate( const tr_ratecontrol * r )
{
    float ret = 0.0f;

    if( r )
        ret = rateForInterval( r, TR_RC_HISTORY_MSEC );

    return ret;
}

/***
****
***/

void
tr_rcTransferred( tr_ratecontrol * r,
                  size_t           size )
{
    const uint64_t now = tr_date ( );

    if( r->transfers[r->newest].date + TR_RC_GRANULARITY_MSEC >= now )
        r->transfers[r->newest].size += size;
    else
    {
        if( ++r->newest == TR_RC_HISTORY_SIZE ) r->newest = 0;
        r->transfers[r->newest].date = now;
        r->transfers[r->newest].size = size;
    }
}
