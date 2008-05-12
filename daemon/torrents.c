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

#include <sys/types.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <event.h>
#include <fcntl.h>
#include <time.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libtransmission/bencode.h>
#include <libtransmission/transmission.h>
#include <libtransmission/trcompat.h>

#include "bsdtree.h"
#include "errors.h"
#include "misc.h"
#include "torrents.h"

#define EXIT_TIMEOUT 10         /* how many seconds to wait on exit */
#define TIMER_SECS   1          /* timer interval seconds */
#define TIMER_USECS  0          /* timer interval microseconds */

struct tor
{
    int             id;
    uint8_t         hash[SHA_DIGEST_LENGTH];
    tr_torrent    * tor;
    RB_ENTRY( tor ) idlinks;
    RB_ENTRY( tor ) hashlinks;
};

RB_HEAD( tortree, tor );
RB_HEAD( hashtree, tor );

static struct tor * opentor    ( const char *, const char *, uint8_t *, size_t,
                                 const char *, int start );
static void         closetor   ( struct tor *, int );
static void         starttimer ( int );
static void         timerfunc  ( int, short, void * );
static int          savestate  ( void );

static struct event_base * gl_base      = NULL;
static tr_handle         * gl_handle    = NULL;
static struct tortree      gl_tree      = RB_INITIALIZER( &gl_tree );
static struct hashtree     gl_hashes    = RB_INITIALIZER( &gl_hashes );
static int                 gl_lastid    = 0;
static struct event        gl_event;
static time_t              gl_exiting   = 0;
static int                 gl_exitval   = 0;
static char                gl_state[MAXPATHLEN];
static char                gl_newstate[MAXPATHLEN];

static int                 gl_autostart = 1;
static int                 gl_pex       = 1;
static int                 gl_port      = TR_DEFAULT_PORT;
static int                 gl_mapping   = 0;
static int                 gl_uplimit   = -1;
static int                 gl_downlimit = -1;
static char                gl_dir[MAXPATHLEN];
static tr_encryption_mode  gl_crypto    = TR_ENCRYPTION_PREFERRED;

static int
torhashcmp( struct tor * left, struct tor * right )
{
    return memcmp( left->hash, right->hash, sizeof left->hash );
}

RB_GENERATE_STATIC( hashtree, tor, hashlinks, torhashcmp )

INTCMP_FUNC( toridcmp, tor, id )
RB_GENERATE_STATIC( tortree, tor, idlinks, toridcmp )

void
torrent_init( const char * configdir, struct event_base * base )
{
    tr_benc state, * torrents;
    int have_state;
    assert( !gl_handle && !gl_base );

    confpath( gl_state, sizeof gl_state, configdir, CONF_FILE_STATE, 0 );
    snprintf( gl_newstate, sizeof( gl_newstate ), "%s.new", gl_state );
    absolutify( gl_dir, sizeof gl_dir, "." );

    /* initialize the session variables */
    if(( have_state = !tr_bencLoadFile( gl_state, &state )))
    {
        int64_t i;
        const char * str;

        if( tr_bencDictFindInt( &state, "autostart", &i ) )
            gl_autostart = i != 0;
        if( tr_bencDictFindInt( &state, "port", &i ) && ( 0<i ) && ( i<=0xffff ) )
            gl_port = i;
        if( tr_bencDictFindInt( &state, "default-pex", &i ) )
            gl_pex = i != 0;
        if( tr_bencDictFindInt( &state, "port-mapping", &i ) )
            gl_mapping = i != 0;
        if( tr_bencDictFindInt( &state, "upload-limit", &i ) )
            gl_uplimit = i;
        if( tr_bencDictFindInt( &state, "download-limit", &i ) )
            gl_downlimit = i;
        if( tr_bencDictFindStr( &state, "default-directory", &str ) )
            strlcpy( gl_dir, str, sizeof gl_dir );
        if( tr_bencDictFindStr( &state, "encryption-mode", &str ) ) {
            if( !strcmp( str, "required" ) )
                gl_crypto = TR_ENCRYPTION_REQUIRED;
            else
                gl_crypto = TR_ENCRYPTION_PREFERRED;
        }
    }

    /* start the session */
    gl_base = base;
    gl_handle = tr_initFull( configdir, "daemon", gl_pex,
                             gl_mapping, gl_port,
                             gl_crypto,
                             gl_uplimit >= 0, gl_uplimit,
                             gl_downlimit >= 0, gl_downlimit,
                             TR_DEFAULT_GLOBAL_PEER_LIMIT,
                             TR_MSG_INF, 0,
                             0, /* is the blocklist enabled? */
                             TR_DEFAULT_PEER_SOCKET_TOS );

    /* now load the torrents */
    if( have_state && tr_bencDictFindList( &state, "torrents", &torrents ) )
    {
        int i, n;
        for( i=0, n=tr_bencListSize(torrents); i<n; ++i )
        {
            int start;
            int64_t paused;
            const char * directory = NULL;
            const char * hash = NULL;

            tr_benc * dict = tr_bencListChild( torrents, i );
            if( !tr_bencIsDict( dict ) ||
                !tr_bencDictFindStr( dict, "directory", &directory ) ||
                !tr_bencDictFindStr( dict, "hash", &hash ) )
                continue;

            if( tr_bencDictFindInt( dict, "paused", &paused ) )
                start = !paused;
            else
                start = gl_autostart;

            opentor( NULL, hash, NULL, 0, directory, start );
        }
    }

    /* cleanup */
    if( have_state )
        tr_bencFree( &state );
}

int
torrent_add_file( const char * path, const char * dir, int start )
{
    struct tor * tor;

    assert( gl_handle );
    assert( !gl_exiting );

    if( start < 0 )
        start = gl_autostart;

    tor = opentor( path, NULL, NULL, 0, dir, start );
    if( !tor )
        return -1;

    savestate();

    return tor->id;
}

int
torrent_add_data( uint8_t * data, size_t size, const char * dir, int start )
{
    struct tor * tor;

    assert( gl_handle );
    assert( !gl_exiting );

    if( start < 0 )
        start = gl_autostart;

    tor = opentor( NULL, NULL, data, size, dir, start );
    if( !tor )
        return -1;

    savestate();

    return tor->id;
}

static struct tor *
idlookup( int id )
{
    struct tor * found = NULL;

    if( gl_handle && !gl_exiting )
    {
        struct tor key;
        memset( &key, 0, sizeof key );
        key.id = id;
        found = RB_FIND( tortree, &gl_tree, &key );
    }

    return found;
}

void
torrent_start( int id )
{
    struct tor * tor = idlookup( id );
    if( tor && !TR_STATUS_IS_ACTIVE( tr_torrentStat( tor->tor )->status ) ) {
        tr_torrentStart( tor->tor );
        savestate();
    }

}

void
torrent_stop( int id )
{
    struct tor * tor = idlookup( id );
    if( tor && TR_STATUS_IS_ACTIVE( tr_torrentStat( tor->tor )->status ) ) {
        tr_torrentStop( tor->tor );
        savestate( );
    }
}

void
torrent_verify( int id )
{
    struct tor * tor = idlookup( id );
    if( tor )
        tr_torrentVerify( tor->tor );
}

void
torrent_remove( int id )
{
    struct tor * tor = idlookup( id );
    if( tor ) {
        closetor( tor, 1 );
        savestate();
    }
}

tr_torrent *
torrent_handle( int id )
{
    const struct tor * tor = idlookup( id );
    return tor ? tor->tor : NULL;
}
   
const tr_info *
torrent_info( int id )
{
    return tr_torrentInfo( torrent_handle( id ) );
}

const tr_stat *
torrent_stat( int id )
{
    return tr_torrentStat( torrent_handle( id ) );
}

static struct tor *
hashlookup( const uint8_t * hash )
{
    struct tor key, * found;

    memset( &key, 0, sizeof key );
    memcpy( key.hash, hash, sizeof key.hash );
    found = RB_FIND( hashtree, &gl_hashes, &key );

    return found;
}

int
torrent_lookup( const uint8_t * hashstr )
{
    uint8_t      hash[SHA_DIGEST_LENGTH];
    size_t       ii;
    struct tor * tor;
    char         buf[3];

    assert( NULL != gl_handle );
    assert( !gl_exiting );

    memset( buf, 0, sizeof buf );
    for( ii = 0; sizeof( hash ) > ii; ii++ )
    {
        if( !isxdigit( hashstr[2*ii] ) || !isxdigit( hashstr[1+2*ii] ) )
        {
            return -1;
        }
        memcpy( buf, &hashstr[2*ii], 2 );
        hash[ii] = strtol( buf, NULL, 16 );
    }

    tor = hashlookup( hash );
    if( NULL == tor )
    {
        return -1;
    }

    return tor->id;
}

static struct tor *
iterate( struct tor * tor )
{
    struct tor * next = NULL;

    if( gl_handle && !gl_exiting )
        next = tor ? RB_NEXT( tortree, &gl_tree, tor )
                   : RB_MIN( tortree, &gl_tree );

    return next;
}

void *
torrent_iter( void * cur, int * id )
{
    struct tor * next = iterate( cur );
    if( next )
        *id = next->id;
    return next;
}

void
torrent_exit( int exitval )
{
    struct tor * tor;

    assert( NULL != gl_handle );
    assert( !gl_exiting );
    gl_exiting = time( NULL );
    gl_exitval = exitval;

    RB_FOREACH( tor, tortree, &gl_tree )
    {
        closetor( tor, 0 );
    }

    tr_sessionSetPortForwardingEnabled( gl_handle, 0 );
    starttimer( 1 );
}

void
torrent_set_autostart( int autostart )
{
    assert( NULL != gl_handle );
    assert( !gl_exiting );
    gl_autostart = autostart;
    savestate();
}

int
torrent_get_autostart( void )
{
    return gl_autostart;
}

void
torrent_set_port( int port )
{
    assert( NULL != gl_handle );
    assert( !gl_exiting );
    if( 0 < port && 0xffff > port )
    {
        gl_port = port;
        tr_sessionSetPublicPort( gl_handle, port );
        savestate();
    }
}

int
torrent_get_port( void )
{
    return gl_port;
}

void
torrent_set_pex( int pex )
{
    assert( NULL != gl_handle );
    assert( !gl_exiting );

    if( gl_pex != pex )
    {
        gl_pex = pex;

        tr_sessionSetPexEnabled( gl_handle, gl_pex );

        savestate( );
    }
}

int
torrent_get_pex( void )
{
    return gl_pex;
}

void
torrent_enable_port_mapping( int automap )
{
    assert( NULL != gl_handle );
    assert( !gl_exiting );
    gl_mapping = ( automap ? 1 : 0 );
    tr_sessionSetPortForwardingEnabled( gl_handle, gl_mapping );
    savestate();
}

int
torrent_get_port_mapping( void )
{
    return gl_mapping;
}

void
torrent_set_uplimit( int uplimit )
{
    assert( NULL != gl_handle );
    assert( !gl_exiting );
    gl_uplimit = uplimit;
    tr_sessionSetSpeedLimit( gl_handle, TR_UP, uplimit );
    tr_sessionSetSpeedLimitEnabled( gl_handle, TR_UP, uplimit > 0 );
    savestate();
}

int
torrent_get_uplimit( void )
{
    return gl_uplimit;
}

void
torrent_set_downlimit( int downlimit )
{
    assert( NULL != gl_handle );
    assert( !gl_exiting );
    gl_downlimit = downlimit;
    tr_sessionSetSpeedLimit( gl_handle, TR_DOWN, downlimit );
    tr_sessionSetSpeedLimitEnabled( gl_handle, TR_DOWN, downlimit > 0 );
    savestate();
}

int
torrent_get_downlimit( void )
{
    return gl_downlimit;
}

void
torrent_set_directory( const char * path )
{
    assert( NULL != gl_handle );
    assert( !gl_exiting );

    absolutify( gl_dir, sizeof gl_dir, path );
    savestate();
}

const char *
torrent_get_directory( void )
{
    return gl_dir;
}

void
torrent_set_encryption(tr_encryption_mode mode)
{
    tr_sessionSetEncryption( gl_handle, mode );
    gl_crypto = mode;
    savestate();
}

tr_encryption_mode
torrent_get_encryption(void)
{
    return tr_sessionGetEncryption( gl_handle );
}

struct tor *
opentor( const char * path,
         const char * hash,
         uint8_t    * data,
         size_t       size,
         const char * dir, 
         int          start )
{
    struct tor * tor, * found;
    int          errcode;
    const tr_info  * inf;
    tr_ctor        * ctor;

    assert( (path?1:0) + (hash?1:0) + (data?1:0) == 1 );

    /* XXX should probably wrap around back to 1 and avoid duplicates */
    if( INT_MAX == gl_lastid )
    {
        errmsg( "Congratulations, you're the %ith torrent! Your prize the "
                "inability to load any more torrents, enjoy!", INT_MAX );
        return NULL;
    }

    tor = calloc( 1, sizeof *tor );
    if( NULL == tor )
    {
        mallocmsg( sizeof *tor );
        return NULL;
    }

    if( dir == NULL )
        dir = gl_dir;

    ctor = tr_ctorNew( gl_handle );
    tr_ctorSetPaused( ctor, TR_FORCE, !start );
    tr_ctorSetDestination( ctor, TR_FORCE, dir );
    if( path != NULL )
        tr_ctorSetMetainfoFromFile( ctor, path );
    else if( hash != NULL )
        tr_ctorSetMetainfoFromHash( ctor, hash );
    else
        tr_ctorSetMetainfo( ctor, data, size );
    tor->tor = tr_torrentNew( gl_handle, ctor, &errcode );
    tr_ctorFree( ctor );

    if( NULL == tor->tor )
    {
        found = NULL;
        switch( errcode )
        {
            case TR_EINVALID:
                if( NULL == path )
                {
                    errmsg( "invalid torrent file" );
                }
                else
                {
                    errmsg( "invalid torrent file: %s", path );
                }
                break;
            case TR_EDUPLICATE:
                /* XXX not yet
                found = hashlookup( tor->hash, 1 );
                assert( NULL != found );
                found->deleting = 0;
                */
                errmsg( "XXX loaded duplicate torrent" );
                break;
            default:
                if( NULL == path )
                {
                    errmsg( "torrent file failed to load" );
                }
                else
                {
                    errmsg( "torrent file failed to load: %s", path );
                }
                break;
        }
        free( tor );
        return found;
    }
    gl_lastid++;
    tor->id       = gl_lastid;

    assert( sizeof( inf->hash ) == sizeof( tor->hash ) );
    inf = tr_torrentInfo( tor->tor );
    memcpy( tor->hash, inf->hash, sizeof tor->hash );

    found = RB_INSERT( tortree, &gl_tree, tor );
    assert( NULL == found );
    found = RB_INSERT( hashtree, &gl_hashes, tor );
    assert( NULL == found );

    return tor;
}

static void
freetor( struct tor * tor )
{
    tr_torrentClose( tor->tor );
    RB_REMOVE( tortree, &gl_tree, tor );
    RB_REMOVE( hashtree, &gl_hashes, tor );
    free( tor );
}

void
closetor( struct tor * tor, int calltimer )
{
    if( NULL != tor )
    {
        freetor( tor );

        starttimer( calltimer );
    }
}

void
starttimer( int callnow )
{
    if( !evtimer_initialized( &gl_event ) )
    {
        evtimer_set( &gl_event, timerfunc, NULL );
        event_base_set( gl_base, &gl_event );
    }

    if( callnow )
    {
        timerfunc( -1, EV_TIMEOUT, NULL );
    }
}

static void
timerfunc( int fd UNUSED, short event UNUSED, void * arg UNUSED )
{
    struct tor             * tor, * next;
    const tr_handle_status * hs;
    int                      stillmore;
    struct timeval           tv;

    /* true if we've still got live torrents... */
    stillmore = tr_torrentCount( gl_handle ) != 0;

    if( gl_exiting )
    {
        if( !stillmore )
        {
            hs = tr_handleStatus( gl_handle );
            if( TR_NAT_TRAVERSAL_UNMAPPED != hs->natTraversalStatus )
            {
                stillmore = 1;
            }
        }

        if( !stillmore || EXIT_TIMEOUT <= time( NULL ) - gl_exiting )
        {
            if( stillmore )
            {
                errmsg( "timing out trackers and/or port mapping on exit" );
            }
            for( tor = RB_MIN( tortree, &gl_tree ); NULL != tor; tor = next )
            {
                next = RB_NEXT( tortree, &gl_tree, tor );
                freetor( tor );
            }
            tr_close( gl_handle );
            exit( gl_exitval );
        }
    }

    if( stillmore )
    {
        memset( &tv, 0, sizeof tv );
        tv.tv_sec  = TIMER_SECS;
        tv.tv_usec = TIMER_USECS;
        evtimer_add( &gl_event, &tv );
    }
}

int
savestate( void )
{
    benc_val_t   top, * list;
    struct tor * ii;
    int          torrentCount;
 
    torrentCount = 0;
    RB_FOREACH( ii, tortree, &gl_tree )
        ++torrentCount;

    tr_bencInitDict( &top, 9 );
    tr_bencDictAddInt( &top, "autostart",         gl_autostart );
    tr_bencDictAddInt( &top, "port",              gl_port );
    tr_bencDictAddInt( &top, "default-pex",       gl_pex );
    tr_bencDictAddInt( &top, "port-mapping",      gl_mapping );
    tr_bencDictAddInt( &top, "upload-limit",      gl_uplimit );
    tr_bencDictAddInt( &top, "download-limit",    gl_downlimit );
    tr_bencDictAddStr( &top, "default-directory", gl_dir );
    tr_bencDictAddStr( &top, "encryption-mode",   TR_ENCRYPTION_REQUIRED == gl_crypto
                                                  ? "required" : "preferred" );
    list = tr_bencDictAddList( &top, "torrents", torrentCount );

    RB_FOREACH( ii, tortree, &gl_tree )
    {
        const tr_info * inf = tr_torrentInfo( ii->tor );
        const tr_stat * st  = tr_torrentStat( ii->tor );
        tr_benc * tor = tr_bencListAddDict( list, 3 );
        tr_bencDictAddStr( tor, "hash", inf->hashString );
        tr_bencDictAddInt( tor, "paused", !TR_STATUS_IS_ACTIVE( st->status ) );
        tr_bencDictAddStr( tor, "directory", tr_torrentGetFolder( ii->tor ) );
    }

    if( tr_bencSaveFile( gl_newstate, &top ) )
    {
        errnomsg( "failed to save state: failed to write to %s", gl_newstate );
        return -1;
    }

    if( 0 > rename( gl_newstate, gl_state ) )
    {
        errnomsg( "failed to save state: failed to rename %s to %s",
                  gl_newstate, CONF_FILE_STATE );
        return -1;
    }

    return 0;
}
