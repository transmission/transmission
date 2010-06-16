/*
 * This file Copyright (C) 2010 Mnemosyne LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 *
 * $Id$
 */

#include <stdio.h>

#include <event.h> /* evbuffer */

#include <libtransmission/transmission.h>
#include <libtransmission/bencode.h>
#include <libtransmission/tr-getopt.h>
#include <libtransmission/utils.h>

#define MY_NAME "transmission-edit"

int fileCount = 0;
const char ** files = NULL;
const char * add = NULL;
const char * deleteme = NULL;
const char * replace[2] = { NULL, NULL };

static tr_option options[] =
{
  { 'a', "add", "Add a tracker's announce URL", "a", 1, "<url>" },
  { 'd', "delete", "Delete a tracker's announce URL", "d", 1, "<url>" },
  { 'r', "replace", "Search and replace a substring in the announce URLs", "r", 1, "<old> <new>" },
  { 0, NULL, NULL, NULL, 0, NULL }
};

static const char *
getUsage( void )
{
    return "Usage: " MY_NAME " [options] torrent-file(s)";
}

static int
parseCommandLine( int argc, const char ** argv )
{
    int c;
    const char * optarg;

    while(( c = tr_getopt( getUsage( ), argc, argv, options, &optarg )))
    {
        switch( c )
        {
            case 'a': add = optarg;
                      break;
            case 'd': deleteme = optarg;
                      break;
            case 'r': replace[0] = optarg;
                      c = tr_getopt( getUsage( ), argc, argv, options, &optarg );
                      if( c != TR_OPT_UNK ) return 1;
                      replace[1] = optarg;
                      break;
            case TR_OPT_UNK: files[fileCount++] = optarg; break;
            default: return 1;
        }
    }

    return 0;
}

static tr_bool
removeURL( tr_benc * metainfo, const char * url )
{
    const char * str;
    tr_benc * announce_list;
    tr_bool changed = FALSE;

    if( tr_bencDictFindStr( metainfo, "announce", &str ) && !strcmp( str, url ) )
    {
        printf( "\tRemoved \"%s\" from \"announce\"\n", str );
        tr_bencDictRemove( metainfo, "announce" );
        changed = TRUE;
    }

    if( tr_bencDictFindList( metainfo, "announce-list", &announce_list ) )
    {
        tr_benc * tier;
        int tierIndex = 0;
        while(( tier = tr_bencListChild( announce_list, tierIndex )))
        {
            tr_benc * node;
            int nodeIndex = 0;
            while(( node = tr_bencListChild( tier, nodeIndex )))
            {
                if( tr_bencGetStr( node, &str ) && !strcmp( str, url ) )
                {
                    printf( "\tRemoved \"%s\" from \"announce-list\" tier #%d\n", str, (tierIndex+1) );
                    tr_bencListRemove( tier, nodeIndex );
                    changed = TRUE;
                }
                else ++nodeIndex;
            }

            if( tr_bencListSize( tier ) == 0 )
            {
                printf( "\tNo URLs left in tier #%d... removing tier\n", (tierIndex+1) );
                tr_bencListRemove( announce_list, tierIndex );
            }
            else ++tierIndex;
        }

        if( tr_bencListSize( announce_list ) == 0 )
        {
            printf( "\tNo tiers left... removing announce-list\n" );
            tr_bencDictRemove( metainfo, "announce-list" );
        }
    }

    /* if we removed the "announce" field and there's still another track left,
     * use it as the "announce" field */
    if( changed && !tr_bencDictFindStr( metainfo, "announce", &str ) )
    {
        tr_benc * tier;
        tr_benc * node;

        if(( tier = tr_bencListChild( announce_list, 0 ))) {
            if(( node = tr_bencListChild( tier, 0 ))) {
                if( tr_bencGetStr( node, &str ) ) {
                    tr_bencDictAddStr( metainfo, "announce", str );
                    printf( "\tAdded \"%s\" to announce\n", str );
                }
            }
        }
    }
        

    return changed;
}

static char*
replaceSubstr( const char * str, const char * in, const char * out )
{
    char * walk;
    struct evbuffer * buf = evbuffer_new( );
    const size_t inlen = strlen( in );
    const size_t outlen = strlen( out );

    while(( walk = strstr( str, in )))
    {
        evbuffer_add( buf, str, walk-str );
        evbuffer_add( buf, out, outlen );
        str = walk + inlen;
    }

    walk = tr_strndup( EVBUFFER_DATA( buf ), EVBUFFER_LENGTH( buf ) );
    evbuffer_free( buf );
    return walk;
}

static tr_bool
replaceURL( tr_benc * metainfo, const char * in, const char * out )
{
    const char * str;
    tr_benc * announce_list;
    tr_bool changed = FALSE;

    if( tr_bencDictFindStr( metainfo, "announce", &str ) && strstr( str, in ) )
    {
        char * newstr = replaceSubstr( str, in, out );
        printf( "\tReplaced in \"announce\": \"%s\" --> \"%s\"\n", str, newstr );
        tr_bencDictAddStr( metainfo, "announce", newstr );
        tr_free( newstr );
        changed = TRUE;
    }

    if( tr_bencDictFindList( metainfo, "announce-list", &announce_list ) )
    {
        tr_benc * tier;
        int tierCount = 0;
        while(( tier = tr_bencListChild( announce_list, tierCount++ )))
        {
            tr_benc * node;
            int nodeCount = 0;
            while(( node = tr_bencListChild( tier, nodeCount++ )))
            {
                if( tr_bencGetStr( node, &str ) && strstr( str, in ) )
                {
                    char * newstr = replaceSubstr( str, in, out );
                    printf( "\tReplaced in \"announce-list\" tier %d: \"%s\" --> \"%s\"\n", tierCount, str, newstr );
                    tr_bencFree( node );
                    tr_bencInitStr( node, newstr, -1 );
                    tr_free( newstr );
                    changed = TRUE;
                }
            }
        }
    }

    return changed;
}

static tr_bool
addURL( tr_benc * metainfo, const char * url )
{
    const char * str;
    tr_benc * announce_list;
    tr_bool changed = FALSE;
    tr_bool match = FALSE;

    /* maybe add it to "announce" */
    if( !tr_bencDictFindStr( metainfo, "announce", &str ) )
    {
        printf( "\tAdded \"%s\" in \"announce\"\n", url );
        tr_bencDictAddStr( metainfo, "announce", url );
        changed = TRUE;
    }

    /* see if it's already in announce-list */
    if( tr_bencDictFindList( metainfo, "announce-list", &announce_list ) ) {
        tr_benc * tier;
        int tierCount = 0;
        while(( tier = tr_bencListChild( announce_list, tierCount++ ))) {
            tr_benc * node;
            int nodeCount = 0;
            while(( node = tr_bencListChild( tier, nodeCount++ )))
                if( tr_bencGetStr( node, &str ) && !strcmp( str, url ) )
                    match = TRUE;
        }
    }

    /* if it's not in announce-list, add it now */
    if( !match )
    {
        tr_benc * tier;

        if( !tr_bencDictFindList( metainfo, "announce-list", &announce_list ) )
            announce_list = tr_bencDictAddList( metainfo, "announce-list", 1 );

        tier = tr_bencListAddList( announce_list, 1 );
        tr_bencListAddStr( tier, url );
        printf( "\tAdded \"%s\" to \"announce-list\" tier %d\n", url, tr_bencListSize( announce_list ) );
        changed = TRUE;
    }

    return changed;
}

int
main( int argc, char * argv[] )
{
    int i;
    int changedCount = 0;

    files = tr_new0( const char*, argc );

    tr_setMessageLevel( TR_MSG_ERR );

    if( parseCommandLine( argc, (const char**)argv ) )
        return EXIT_FAILURE;

    if( fileCount < 1 )
    {
        fprintf( stderr, "ERROR: No torrent files specified.\n" );
        tr_getopt_usage( MY_NAME, getUsage( ), options );
        fprintf( stderr, "\n" );
        return EXIT_FAILURE;
    }

    if( !add && !deleteme && !replace[0] )
    {
        fprintf( stderr, "ERROR: Must specify -a, -d or -r\n" );
        tr_getopt_usage( MY_NAME, getUsage( ), options );
        fprintf( stderr, "\n" );
        return EXIT_FAILURE;
    }

    for( i=0; i<fileCount; ++i )
    {
        tr_benc top;
        tr_bool changed = FALSE;
        const char * filename = files[i];

        printf( "%s\n", filename );

        if( tr_bencLoadFile( &top, TR_FMT_BENC, filename ) )
        {
            printf( "\tError reading file\n" );
            continue;
        }

        if( deleteme != NULL )
            changed |= removeURL( &top, deleteme );

        if( add != NULL )
            changed = addURL( &top, add );

        if( replace[0] && replace[1] )
            changed |= replaceURL( &top, replace[0], replace[1] );

        if( changed )
        {
            ++changedCount;
            tr_bencToFile( &top, TR_FMT_BENC, filename );
        }

        tr_bencFree( &top );
    }

    printf( "Changed %d files\n", changedCount );

    tr_free( files );
    return 0;
}
