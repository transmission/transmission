/*
 * This file Copyright (C) 2008 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license. 
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id:$
 */

#include <stdio.h> /* fprintf() */
#include <string.h> /* strlen() */
#include <libtransmission/utils.h> /* tr_free() */
#include <libtransmission/bencode.h> /* tr_bencLoad() */

int
main( int argc, char *argv[] )
{
    if( argc != 2 )
    {
        fprintf( stderr, "Usage: benc2php bencoded-text\n" );
        return -1;
    }
    else
    {
        tr_benc top;
        const char * benc_str = argv[1];
        char * serialized;
        tr_bencLoad( benc_str, strlen( benc_str ), &top, NULL );
        serialized = tr_bencSaveAsSerializedPHP( &top, NULL );
        puts( serialized );
        tr_free( serialized );
        return 0;
    }
}
