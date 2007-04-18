/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2007 Joshua Elsasser
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

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include "errors.h"

static void verrmsg( int, const char *, va_list );

static int  gl_syslog = 0;

void
errsyslog( int useit )
{
    gl_syslog = useit;

    if( useit )
    {
        openlog( getmyname(), 0, LOG_DAEMON );
    }
}

void
errmsg( const char * fmt, ... )
{
    va_list ap;

    va_start( ap, fmt );
    verrmsg( -1, fmt, ap );
    va_end( ap );
}

void
errnomsg( const char * fmt, ... )
{
    va_list ap;

    va_start( ap, fmt );
    verrmsg( errno, fmt, ap );
    va_end( ap );
}

void
verrmsg( int errnum, const char * fmt, va_list ap )
{
    char buf[1024];

    vsnprintf( buf, sizeof buf, fmt, ap );

    if( gl_syslog )
    {
        if( 0 > errnum )
        {
            syslog( LOG_ERR, "%s", buf );
        }
        else
        {
            syslog( LOG_ERR, "%s: %s", buf, strerror( errnum ) );
        }
    }
    else
    {
        if( 0 > errnum )
        {
            fprintf( stderr, "%s: %s\n", getmyname(), buf );
        }
        else
        {
            fprintf( stderr, "%s: %s: %s\n", getmyname(), buf,
                     strerror( errnum ) );
        }
    }
}

void
mallocmsg( size_t size )
{
    if( 0 < size )
    {
        errmsg( "failed to allocate %i bytes of memory", ( int )size );
    }
    else
    {
        errmsg( "failed to allocate memory" );
    }
}
