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

#include <stdio.h>
#include <stdlib.h> /* free */
#include <string.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "ggets.h"

#include "transmission.h"
#include "net.h" /* inet_aton() */
#include "platform.h" /* tr_getPrefsDirectory() */
#include "utils.h" /* tr_buildPath() */

static void * blocklist = NULL;
static size_t blocklistSize = 0;
static int blocklistFd = -1;

static void
getFilename( char * buf, size_t buflen )
{
    tr_buildPath( buf, buflen, tr_getPrefsDirectory(), "blocklist", NULL ); 
}

static void
closeBlocklist( void )
{
    if( blocklist )
    {
        munmap( blocklist, blocklistSize );
        close( blocklistFd );
        blocklist = NULL;
        blocklistSize = 0;
        blocklistFd  = -1;
    }
}

static void
loadBlocklist( void )
{
    int fd;
    struct stat st;
    char filename[MAX_PATH_LENGTH];
    getFilename( filename, sizeof( filename ) );

    closeBlocklist( );

    fd = open( filename, O_RDONLY );
    if( fd == -1 ) {
        tr_err( _( "Couldn't read file \"%s\": %s" ), filename, tr_strerror(errno) );
        return;
    }
    if( fstat( fd, &st ) == -1 ) {
        tr_err( _( "Couldn't read file \"%s\": %s" ), filename, tr_strerror(errno) );
        close( fd );
        return;
    }
    blocklist = mmap( 0, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0 );
    if( !blocklist ) {
        tr_err( _( "Couldn't read file \"%s\": %s" ), filename, tr_strerror(errno) );
        close( fd );
        return;
    }

    blocklistSize = st.st_size;
    blocklistFd = fd;
}

int
tr_isInBlocklist( const struct in_addr * addr UNUSED )
{
    if( !blocklist )
        loadBlocklist( );

    return FALSE; /* FIXME */
}


static void
deleteBlocklist( tr_handle * handle UNUSED )
{
    char filename[MAX_PATH_LENGTH];
    getFilename( filename, sizeof( filename ) );
    closeBlocklist( );
    unlink( filename );
}

void
tr_setBlocklist( tr_handle  * handle,
                 const char * filename )
{
    FILE * in;
    FILE * out;
    char * line;
    char outfile[MAX_PATH_LENGTH];

    if( filename == NULL ) {
        deleteBlocklist( handle );
        return;
    }

    in = fopen( filename, "r" );
    if( !in ) {
        tr_err( _( "Couldn't read file \"%s\": %s" ), filename, tr_strerror(errno) );
        return;
    }

    closeBlocklist( );
  
    getFilename( outfile, sizeof( outfile ) );
fprintf( stderr, "outfile is [%s]\n", outfile );
    out = fopen( outfile, "wb+" );
    if( !out ) {
        tr_err( _( "Couldn't save file \"%s\": %s" ), outfile, tr_strerror( errno ) );
        fclose( in );
        return;
    }

    while( !fggets( &line, in ) )
    {
        char * rangeBegin;
        char * rangeEnd;
        struct in_addr in_addr;
        uint32_t range[2];
//fprintf( stderr, "got line [%s]\n", line );

        rangeBegin = strrchr( line, ':' );
        if( !rangeBegin ) continue;
        ++rangeBegin;

        rangeEnd = strchr( rangeBegin, '-' );
        if( !rangeEnd ) continue;
        *rangeEnd++ = '\0';

        //fprintf( stderr, "rangeBegin [%s] rangeEnd [%s]\n", rangeBegin, rangeEnd );
        if( !inet_aton( rangeBegin, &in_addr ) )
            fprintf( stderr, "skipping invalid address [%s]\n", rangeBegin );
        range[0] = ntohl( in_addr.s_addr );
        if( !inet_aton( rangeEnd, &in_addr ) )
            fprintf( stderr, "skipping invalid address [%s]\n", rangeEnd );
        range[1] = ntohl( in_addr.s_addr );

        free( line );

        if( fwrite( range, sizeof(uint32_t), 2, out ) != 2 ) {
          tr_err( _( "Couldn't save file \"%s\": %s" ), outfile, tr_strerror( errno ) );
          break;
        }
    }

    fclose( out );
    fclose( in );
}
