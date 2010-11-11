/*
 * This file Copyright (C) 2008-2010 Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h> /* free() */
#include <string.h>

#ifdef WIN32
 #include <w32api.h>
 #define WINVER  WindowsXP
 #include <windows.h>
 #define PROT_READ      PAGE_READONLY
 #define MAP_PRIVATE    FILE_MAP_COPY
#endif

#ifndef WIN32
 #include <sys/mman.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>

#include "transmission.h"
#include "platform.h"
#include "blocklist.h"
#include "net.h"
#include "utils.h"

#ifndef O_BINARY
 #define O_BINARY 0
#endif


/***
****  PRIVATE
***/

struct tr_ip_range
{
    uint32_t    begin;
    uint32_t    end;
};

struct tr_blocklist
{
    tr_bool               isEnabled;
    int                   fd;
    size_t                ruleCount;
    size_t                byteCount;
    char *                filename;
    struct tr_ip_range *  rules;
};

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
    size_t byteCount;
    struct stat st;
    const char * err_fmt = _( "Couldn't read \"%1$s\": %2$s" );

    blocklistClose( b );

    if( stat( b->filename, &st ) == -1 )
        return;

    fd = open( b->filename, O_RDONLY | O_BINARY );
    if( fd == -1 )
    {
        tr_err( err_fmt, b->filename, tr_strerror( errno ) );
        return;
    }

    byteCount = (size_t) st.st_size;
    b->rules = mmap( NULL, byteCount, PROT_READ, MAP_PRIVATE, fd, 0 );
    if( !b->rules )
    {
        tr_err( err_fmt, b->filename, tr_strerror( errno ) );
        close( fd );
        return;
    }

    b->fd = fd;
    b->byteCount = byteCount;
    b->ruleCount = byteCount / sizeof( struct tr_ip_range );

    {
        char * base = tr_basename( b->filename );
        tr_inf( _( "Blocklist \"%s\" contains %zu entries" ), base, b->ruleCount );
        tr_free( base );
    }
}

static void
blocklistEnsureLoaded( tr_blocklist * b )
{
    if( !b->rules )
        blocklistLoad( b );
}

static int
compareAddressToRange( const void * va,
                       const void * vb )
{
    const uint32_t *           a = va;
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
****  PACKAGE-VISIBLE
***/

tr_blocklist *
_tr_blocklistNew( const char * filename, tr_bool isEnabled )
{
    tr_blocklist * b;

    b = tr_new0( tr_blocklist, 1 );
    b->fd = -1;
    b->filename = tr_strdup( filename );
    b->isEnabled = isEnabled;

    return b;
}

const char*
_tr_blocklistGetFilename( const tr_blocklist * b )
{
    return b->filename;
}

void
_tr_blocklistFree( tr_blocklist * b )
{
    blocklistClose( b );
    tr_free( b->filename );
    tr_free( b );
}

int
_tr_blocklistExists( const tr_blocklist * b )
{
    struct stat st;

    return !stat( b->filename, &st );
}

int
_tr_blocklistGetRuleCount( const tr_blocklist * b )
{
    blocklistEnsureLoaded( (tr_blocklist*)b );

    return b->ruleCount;
}

int
_tr_blocklistIsEnabled( tr_blocklist * b )
{
    return b->isEnabled;
}

void
_tr_blocklistSetEnabled( tr_blocklist * b,
                         int            isEnabled )
{
    b->isEnabled = isEnabled ? 1 : 0;
}

int
_tr_blocklistHasAddress( tr_blocklist     * b,
                         const tr_address * addr )
{
    uint32_t                   needle;
    const struct tr_ip_range * range;

    assert( tr_isAddress( addr ) );

    if( !b->isEnabled || addr->type == TR_AF_INET6 )
        return 0;

    blocklistEnsureLoaded( b );

    if( !b->rules || !b->ruleCount )
        return 0;

    needle = ntohl( addr->addr.addr4.s_addr );

    range = bsearch( &needle,
                     b->rules,
                     b->ruleCount,
                     sizeof( struct tr_ip_range ),
                     compareAddressToRange );

    return range != NULL;
}

/*
 * P2P plaintext format: "comment:x.x.x.x-y.y.y.y"
 * http://wiki.phoenixlabs.org/wiki/P2P_Format
 * http://en.wikipedia.org/wiki/PeerGuardian#P2P_plaintext_format
 */
static tr_bool
parseLine1( const char * line, struct tr_ip_range * range )
{
    char * walk;
    int b[4];
    int e[4];
    char str[64];
    tr_address addr;

    walk = strrchr( line, ':' );
    if( !walk )
        return FALSE;
    ++walk; /* walk past the colon */

    if( sscanf( walk, "%d.%d.%d.%d-%d.%d.%d.%d",
                &b[0], &b[1], &b[2], &b[3],
                &e[0], &e[1], &e[2], &e[3] ) != 8 )
        return FALSE;

    tr_snprintf( str, sizeof( str ), "%d.%d.%d.%d", b[0], b[1], b[2], b[3] );
    if( tr_pton( str, &addr ) == NULL )
        return FALSE;
    range->begin = ntohl( addr.addr.addr4.s_addr );

    tr_snprintf( str, sizeof( str ), "%d.%d.%d.%d", e[0], e[1], e[2], e[3] );
    if( tr_pton( str, &addr ) == NULL )
        return FALSE;
    range->end = ntohl( addr.addr.addr4.s_addr );

    return TRUE;
}

/*
 * DAT format: "000.000.000.000 - 000.255.255.255 , 000 , invalid ip"
 * http://wiki.phoenixlabs.org/wiki/DAT_Format
 */
static tr_bool
parseLine2( const char * line, struct tr_ip_range * range )
{
    int unk;
    int a[4];
    int b[4];
    char str[32];
    tr_address addr;

    if( sscanf( line, "%3d.%3d.%3d.%3d - %3d.%3d.%3d.%3d , %3d , ",
                &a[0], &a[1], &a[2], &a[3],
                &b[0], &b[1], &b[2], &b[3],
                &unk ) != 9 )
        return FALSE;

    tr_snprintf( str, sizeof(str), "%d.%d.%d.%d", a[0], a[1], a[2], a[3] );
    if( tr_pton( str, &addr ) == NULL )
        return FALSE;
    range->begin = ntohl( addr.addr.addr4.s_addr );

    tr_snprintf( str, sizeof(str), "%d.%d.%d.%d", b[0], b[1], b[2], b[3] );
    if( tr_pton( str, &addr ) == NULL )
        return FALSE;
    range->end = ntohl( addr.addr.addr4.s_addr );

    return TRUE;
}

static int
parseLine( const char * line, struct tr_ip_range * range )
{
    return parseLine1( line, range )
        || parseLine2( line, range );
}

int
_tr_blocklistSetContent( tr_blocklist * b,
                         const char *   filename )
{
    FILE * in;
    FILE * out;
    int inCount = 0;
    int outCount = 0;
    char line[2048];
    const char * err_fmt = _( "Couldn't read \"%1$s\": %2$s" );

    if( !filename )
    {
        blocklistDelete( b );
        return 0;
    }

    in = fopen( filename, "rb" );
    if( !in )
    {
        tr_err( err_fmt, filename, tr_strerror( errno ) );
        return 0;
    }

    blocklistClose( b );

    out = fopen( b->filename, "wb+" );
    if( !out )
    {
        tr_err( err_fmt, b->filename, tr_strerror( errno ) );
        fclose( in );
        return 0;
    }

    while( fgets( line, sizeof( line ), in ) != NULL )
    {
        char * walk;
        struct tr_ip_range range;

        ++inCount;

        /* zap the linefeed */
        if(( walk = strchr( line, '\r' ))) *walk = '\0';
        if(( walk = strchr( line, '\n' ))) *walk = '\0';

        if( !parseLine( line, &range ) )
        {
            /* don't try to display the actual lines - it causes issues */
            tr_err( _( "blocklist skipped invalid address at line %d" ), inCount );
            continue;
        }

        if( fwrite( &range, sizeof( struct tr_ip_range ), 1, out ) != 1 )
        {
            tr_err( _( "Couldn't save file \"%1$s\": %2$s" ), b->filename,
                   tr_strerror( errno ) );
            break;
        }

        ++outCount;
    }

    {
        char * base = tr_basename( b->filename );
        tr_inf( _( "Blocklist \"%s\" updated with %d entries" ), base, outCount );
        tr_free( base );
    }

    fclose( out );
    fclose( in );

    blocklistLoad( b );

    return outCount;
}

