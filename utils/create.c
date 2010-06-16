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
#include <unistd.h> /* getcwd() */

#include <libtransmission/transmission.h>
#include <libtransmission/makemeta.h>
#include <libtransmission/tr-getopt.h>
#include <libtransmission/utils.h>

#define MY_NAME "transmission-create"

#define MAX_TRACKERS 128
static tr_tracker_info trackers[MAX_TRACKERS];
static int trackerCount = 0;
static tr_bool isPrivate = FALSE;
const char * comment = NULL;
const char * outfile = NULL;
const char * infile = NULL;

static tr_option options[] =
{
  { 'p', "private", "Allow this torrent to only be used with the specified tracker(s)", "p", 0, NULL },
  { 'o', "outfile", "Save the generated .torrent to this filename", "o", 1, "<file>" },
  { 'c', "comment", "Add a comment", "c", 1, "<comment>" },
  { 't', "tracker", "Add a tracker's announce URL", "t", 1, "<url>" },
  { 0, NULL, NULL, NULL, 0, NULL }
};

static const char *
getUsage( void )
{
    return "Usage: " MY_NAME " [options] <file|directory>";
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
            case 'p': isPrivate = TRUE; break;
            case 'o': outfile = optarg; break;
            case 'c': comment = optarg; break;
            case 't': if( trackerCount + 1 < MAX_TRACKERS ) {
                          trackers[trackerCount].tier = trackerCount;
                          trackers[trackerCount].announce = (char*) optarg;
                          ++trackerCount;
                      }
                      break;
            case TR_OPT_UNK: infile = optarg; break;
            default: return 1;
        }
    }

    return 0;
}

static char*
tr_getcwd( void )
{
    char buf[2048];
    *buf = '\0';
#ifdef WIN32
    _getcwd( buf, sizeof( buf ) );
#else
    getcwd( buf, sizeof( buf ) );
#endif
    return tr_strdup( buf );
}

int
main( int argc, char * argv[] )
{
    char * out2 = NULL;
    tr_metainfo_builder * b = NULL;

    tr_setMessageLevel( TR_MSG_ERR );

    if( parseCommandLine( argc, (const char**)argv ) )
        return EXIT_FAILURE;

    if( !infile )
    {
        fprintf( stderr, "ERROR: No input file or directory specified.\n" );
        tr_getopt_usage( MY_NAME, getUsage( ), options );
        fprintf( stderr, "\n" );
        return EXIT_FAILURE;
    }

    if( outfile == NULL )
    {
        char * base = tr_basename( infile );
        char * end = tr_strdup_printf( "%s.torrent", base );
        char * cwd = tr_getcwd( );
        outfile = out2 = tr_buildPath( cwd, end, NULL );
        tr_free( cwd );
        tr_free( end );
        tr_free( base );
    }

    if( !trackerCount )
    {
        if( isPrivate )
        {
            fprintf( stderr, "ERROR: no trackers specified for a private torrent\n" );
            return EXIT_FAILURE;
        }
        else
        {
            printf( "WARNING: no trackers specified\n" );
        }
    }

    printf( "Creating torrent \"%s\" ...", outfile );
    fflush( stdout );

    b = tr_metaInfoBuilderCreate( infile );
    tr_makeMetaInfo( b, outfile, trackers, trackerCount, comment, isPrivate );
    while( !b->isDone ) {
        tr_wait_msec( 500 );
        putc( '.', stdout );
        fflush( stdout );
    }

    putc( ' ', stdout );
    switch( b->result ) {
        case TR_MAKEMETA_OK:        printf( "done!" ); break;
        case TR_MAKEMETA_URL:       printf( "bad announce URL: \"%s\"", b->errfile ); break;
        case TR_MAKEMETA_IO_READ:   printf( "error reading \"%s\": %s", b->errfile, tr_strerror(b->my_errno) ); break;
        case TR_MAKEMETA_IO_WRITE:  printf( "error writing \"%s\": %s", b->errfile, tr_strerror(b->my_errno) ); break;
        case TR_MAKEMETA_CANCELLED: printf( "cancelled" ); break;
    }
    putc( '\n', stdout );

    tr_free( out2 );
    return 0;
}
