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
#include "blocklist.h"
#include "net.h"            /* tr_netResolve() */
#include "platform.h"       /* tr_getPrefsDirectory() */
#include "utils.h"          /* tr_buildPath() */

struct tr_ip_range
{
    uint32_t begin;
    uint32_t end;
};

struct tr_blocklist
{
    struct tr_ip_range * rules;
    size_t ruleCount;
    size_t byteCount;
    int fd;
    int isEnabled;
    char * filename;
};

void
blocklistFilename( char * buf, size_t buflen )
{
    tr_buildPath( buf, buflen, tr_getPrefsDirectory(), "blocklist", NULL ); 
}

static void
blocklistClose( tr_blocklist * b )
{
    if( b->rules )
    {
        munmap( b->rules, b->byteCount );
        close( b->fd );
        b->rules = NULL;
        b->ruleCount = 0;
        b->byteCount = 0;
        b->fd = -1;
    }
}

static void
blocklistLoad( tr_blocklist * b )
{
    int fd;
    struct stat st;

    blocklistClose( b );

    fd = open( b->filename, O_RDONLY );
    if( fd == -1 ) {
        tr_err( _( "Couldn't read file \"%s\": %s" ), b->filename, tr_strerror(errno) );
        return;
    }
    if( fstat( fd, &st ) == -1 ) {
        tr_err( _( "Couldn't read file \"%s\": %s" ), b->filename, tr_strerror(errno) );
        close( fd );
        return;
    }
    b->rules = mmap( 0, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0 );
    if( !b->rules ) {
        tr_err( _( "Couldn't read file \"%s\": %s" ), b->filename, tr_strerror(errno) );
        close( fd );
        return;
    }

    b->byteCount = st.st_size;
    b->ruleCount = st.st_size / sizeof( struct tr_ip_range );
    b->fd = fd;
    tr_inf( _( "Blocklist contains %'d IP ranges" ), b->ruleCount );
}

static void
ensureBlocklistIsLoaded( tr_blocklist * b )
{
    if( !b->rules )
        blocklistLoad( b );
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

static void
blocklistDelete( tr_blocklist * b )
{
    blocklistClose( b );
    unlink( b->filename );
}

/***
****
***/

tr_blocklist *
tr_blocklistNew( int isEnabled )
{
    tr_blocklist * b;
    char filename[MAX_PATH_LENGTH];

    blocklistFilename( filename, sizeof( filename ) );

    b = tr_new0( tr_blocklist, 1 );
    b->fd = -1;
    b->filename = tr_strdup( filename );
    b->isEnabled = isEnabled;

    return b;
}

void
tr_blocklistFree( tr_blocklist * b )
{
    blocklistClose( b );
    tr_free( b->filename );
    tr_free( b );
}


int
tr_blocklistGetRuleCount( tr_handle * handle )
{
    tr_blocklist * b = handle->blocklist;

    ensureBlocklistIsLoaded( b );

    return b->ruleCount;
}

int
tr_blocklistIsEnabled( const tr_handle * handle )
{
    return handle->blocklist->isEnabled;
}

void
tr_blocklistSetEnabled( tr_handle * handle, int isEnabled )
{
    handle->blocklist->isEnabled = isEnabled ? 1 : 0;
}

int
tr_peerIsBlocked( const tr_handle * handle, const struct in_addr * addr )
{
    uint32_t needle;
    const struct tr_ip_range * range;
    tr_blocklist * b = handle->blocklist;

    if( !b->isEnabled )
        return 0;

    ensureBlocklistIsLoaded( b );
    if( !b->rules )
        return 0;

    needle = ntohl( addr->s_addr );

    range = bsearch( &needle,
                     b->rules,
                     b->ruleCount,
                     sizeof( struct tr_ip_range ), 
                     compareAddressToRange );

    return range != NULL;
}

int
tr_blocklistExists( const tr_handle * handle )
{
    struct stat st;
    return !stat( handle->blocklist->filename, &st );
}

int
tr_blocklistSetContent( tr_handle  * handle,
                        const char * filename )
{
    tr_blocklist * b = handle->blocklist;
    FILE * in;
    FILE * out;
    char * line;
    int lineCount = 0;

    if( !filename ) {
        blocklistDelete( b );
        return 0;
    }

    in = fopen( filename, "r" );
    if( !in ) {
        tr_err( _( "Couldn't read file \"%s\": %s" ), filename, tr_strerror(errno) );
        return 0;
    }

    blocklistClose( b );
  
    out = fopen( b->filename, "wb+" );
    if( !out ) {
        tr_err( _( "Couldn't save file \"%s\": %s" ), b->filename, tr_strerror( errno ) );
        fclose( in );
        return 0;
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
          tr_err( _( "Couldn't save file \"%s\": %s" ), b->filename, tr_strerror( errno ) );
          break;
        }

        ++lineCount;
    }

    tr_inf( _( "Blocklist updated with %'d IP ranges" ), lineCount );

    fclose( out );
    fclose( in );

    blocklistLoad( b );

    return lineCount;
}
