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

void tr_msg( int level, char * msg, ... )
{
    char         string[256];
    va_list      args;
    static int   verboseLevel = 0;

    if( !verboseLevel )
    {
        char * env;
        env          = getenv( "TR_DEBUG" );
        verboseLevel = env ? atoi( env ) : -1;
        verboseLevel = verboseLevel ? verboseLevel : -1;
    }

    if( verboseLevel < 1 && level > TR_MSG_ERR )
    {
        return;
    }
    if( verboseLevel < 2 && level > TR_MSG_INF )
    {
        return;
    }

    va_start( args, msg );
    vsnprintf( string, sizeof( string ), msg, args );
    va_end( args );
    fprintf( stderr, "%s\n", string );
}

int tr_rand( int sup )
{
    static int init = 0;
    if( !init )
    {
        srand( tr_date() );
        init = 1;
    }
    return rand() % sup;
}
