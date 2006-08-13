/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005 Transmission authors and contributors
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

char * tr_clientForId( uint8_t * id )
{
    char * ret = NULL;

    if( id[0] == '-' && id[7] == '-' )
    {
        if( !memcmp( &id[1], "TR", 2 ) )
        {
            asprintf( &ret, "Transmission %d.%d",
                      ( id[3] - '0' ) * 10 + ( id[4] - '0' ),
                      ( id[5] - '0' ) * 10 + ( id[6] - '0' ) );
        }
        else if( !memcmp( &id[1], "AZ", 2 ) )
        {
            asprintf( &ret, "Azureus %c.%c.%c.%c",
                      id[3], id[4], id[5], id[6] );
        }
        else if( !memcmp( &id[1], "TS", 2 ) )
        {
            asprintf( &ret, "TorrentStorm (%c%c%c%c)",
                      id[3], id[4], id[5], id[6] );
        }
        else if( !memcmp( &id[1], "BC", 2 ) )
        {
            asprintf( &ret, "BitComet %d.%c%c",
                      ( id[3] - '0' ) * 10 + ( id[4] - '0' ),
                      id[5], id[6] );
        }
        else if( !memcmp( &id[1], "SZ", 2 ) )
        {
            asprintf( &ret, "Shareaza %c.%c.%c.%c",
                      id[3], id[4], id[5], id[6] );
        }
        else if( !memcmp( &id[1], "UT", 2 ) )
        {
            asprintf( &ret, "\xc2\xb5Torrent %c.%d", id[3],
                        ( id[4] - '0' ) * 10 + ( id[5] - '0' )  );
        }
    }
    else if( !memcmp( &id[4], "----", 4 ) )
    {
        if( id[0] == 'T' )
        {
            asprintf( &ret, "BitTornado (%c%c%c)", id[1], id[2], id[3] );
        }
        else if( id[0] == 'A' )
        {
            asprintf( &ret, "ABC (%c%c%c)", id[1], id[2], id[3] );
        }
    }
    else if( id[0] == 'M' && id[2] == '-' &&
             id[4] == '-' && id[6] == '-' &&
             id[7] == '-' )
    {
        asprintf( &ret, "BitTorrent %c.%c.%c", id[1], id[3], id[5] );
    }
    else if( !memcmp( id, "exbc", 4 ) )
    {
        asprintf( &ret, "BitComet %d.%02d", id[4], id[5] );
    }

    if( !ret )
    {
        asprintf( &ret, "unknown client (%c%c%c%c%c%c%c%c)",
                  id[0], id[1], id[2], id[3], id[4], id[5], id[6], id[7] );
    }

    return ret;
}
