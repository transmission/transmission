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
#include "internal.h"
#include "net.h" /* tr_netResolve */
#include "platform.h" /* tr_getPrefsDirectory() */
#include "utils.h" /* tr_buildPath() */

struct tr_ip_range
{
  uint32_t begin;
  uint32_t end;
};

static struct tr_ip_range * blocklist = NULL;
static size_t blocklistItemCount = 0;
static size_t blocklistByteCount = 0;
static int blocklistFd = -1;

void
tr_getBlocklistFilename( char * buf, size_t buflen )
{
    tr_buildPath( buf, buflen, tr_getPrefsDirectory(), "blocklist", NULL ); 
}

static void
closeBlocklist( void )
{
    if( blocklist )
    {
        munmap( blocklist, blocklistByteCount );
        close( blocklistFd );
        blocklist = NULL;
        blocklistItemCount = 0;
        blocklistByteCount = 0;
        blocklistFd  = -1;
    }
}

static void
loadBlocklist( void )
{
    int fd;
    struct stat st;
    char filename[MAX_PATH_LENGTH];
    tr_getBlocklistFilename( filename, sizeof( filename ) );

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

    blocklistByteCount = st.st_size;
    blocklistItemCount = blocklistByteCount / sizeof( struct tr_ip_range );
    blocklistFd = fd;
    tr_inf( _( "Blocklist contains %'d IP ranges" ), blocklistItemCount );
}

static int
compareAddressToRange( const void * va, const void * vb )
{
    const uint32_t * a = va;
    const struct tr_ip_range * b = vb;
    if( *a < b->begin ) return -1;
    if( *a > b->end ) return 1;
    return 0;
}

int
tr_peerIsBlocked( tr_handle * handle UNUSED, const struct in_addr * addr )
{
    uint32_t needle;
    const struct tr_ip_range * range;

    if( !blocklist ) {
        loadBlocklist( );
        if( !blocklist )
            return FALSE;
    }

    needle = ntohl( addr->s_addr );

    range = bsearch( &needle,
                     blocklist,
                     blocklistItemCount,
                     sizeof( struct tr_ip_range ), 
                     compareAddressToRange );

    return range != NULL;
}

static void
deleteBlocklist( tr_handle * handle UNUSED )
{
    char filename[MAX_PATH_LENGTH];
    tr_getBlocklistFilename( filename, sizeof( filename ) );
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
    int lineCount = 0;

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
  
    tr_getBlocklistFilename( outfile, sizeof( outfile ) );
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
        struct tr_ip_range range;

        rangeBegin = strrchr( line, ':' );
        if( !rangeBegin ) { free(line); continue; }
        ++rangeBegin;

        rangeEnd = strchr( rangeBegin, '-' );
        if( !rangeEnd ) { free(line); continue; }
        *rangeEnd++ = '\0';

        if( tr_netResolve( rangeBegin, &in_addr ) )
            tr_err( "blocklist skipped invalid address [%s]\n", rangeBegin );
        range.begin = ntohl( in_addr.s_addr );

        if( tr_netResolve( rangeEnd, &in_addr ) )
            tr_err( "blocklist skipped invalid address [%s]\n", rangeEnd );
        range.end = ntohl( in_addr.s_addr );

        free( line );

        if( fwrite( &range, sizeof(struct tr_ip_range), 1, out ) != 1 ) {
          tr_err( _( "Couldn't save file \"%s\": %s" ), outfile, tr_strerror( errno ) );
          break;
        }

        ++lineCount;
    }

    tr_inf( _( "Blocklist updated with %'d IP ranges" ), lineCount );

    fclose( out );
    fclose( in );

    loadBlocklist( );
}
