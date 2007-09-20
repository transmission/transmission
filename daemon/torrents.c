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

#include <libtransmission/bsdtree.h>
#include <libtransmission/bencode.h>
#include <libtransmission/transmission.h>
#include <libtransmission/trcompat.h>

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
    int             pexset;
    int             pex;
    RB_ENTRY( tor ) idlinks;
    RB_ENTRY( tor ) hashlinks;
};

RB_HEAD( tortree, tor );
RB_HEAD( hashtree, tor );

static struct tor * opentor    ( const char *, const char *, uint8_t *, size_t,
                                  const char * );
static void         closetor   ( struct tor *, int );
static void         starttimer ( int );
static void         timerfunc  ( int, short, void * );
static int          loadstate  ( void );
static int          savestate  ( void );
static int          toridcmp   ( struct tor *, struct tor * );
static int          torhashcmp ( struct tor *, struct tor * );
static struct tor * idlookup   ( int );
static struct tor * hashlookup ( const uint8_t * );
static struct tor * iterate    ( struct tor * );

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

RB_GENERATE_STATIC( tortree, tor, idlinks, toridcmp )
RB_GENERATE_STATIC( hashtree, tor, hashlinks, torhashcmp )
INTCMP_FUNC( toridcmp, tor, id )

void
torrent_init( struct event_base * base )
{
    assert( NULL == gl_handle && NULL == gl_base );

    gl_base   = base;
    gl_handle = tr_init( "daemon" );

    confpath( gl_state, sizeof gl_state, CONF_FILE_STATE, 0 );
    strlcpy( gl_newstate, gl_state, sizeof gl_state );
    strlcat( gl_newstate, ".new", sizeof gl_state );
    absolutify( gl_dir, sizeof gl_dir, "." );

    loadstate();
}

int
torrent_add_file( const char * path, const char * dir, int autostart )
{
    struct tor * tor;

    assert( NULL != gl_handle );
    assert( !gl_exiting );

    tor = opentor( path, NULL, NULL, 0, dir );
    if( NULL == tor )
    {
        return -1;
    }

    if( 0 > autostart )
    {
        autostart = gl_autostart;
    }
    if( autostart )
    {
        tr_torrentStart( tor->tor );
    }

    savestate();

    return tor->id;
}

int
torrent_add_data( uint8_t * data, size_t size, const char * dir, int autostart )
{
    struct tor * tor;

    assert( NULL != gl_handle );
    assert( !gl_exiting );

    tor = opentor( NULL, NULL, data, size, dir );
    if( NULL == tor )
    {
        return -1;
    }

    if( 0 > autostart )
    {
        autostart = gl_autostart;
    }
    if( autostart )
    {
        tr_torrentStart( tor->tor );
    }

    savestate();

    return tor->id;
}

void
torrent_start( int id )
{
    struct tor * tor;

    assert( NULL != gl_handle );
    assert( !gl_exiting );

    tor = idlookup( id );
    if( tor != NULL )
    {
        const tr_stat  * st = tr_torrentStat( tor->tor );

        if( TR_STATUS_INACTIVE & st->status )
        {
            tr_torrentStart( tor->tor );
            savestate();
        }
    }

}

void
torrent_stop( int id )
{
    struct tor * tor;

    assert( NULL != gl_handle );
    assert( !gl_exiting );

    tor = idlookup( id );
    if( tor != NULL )
    {
        const tr_stat  * st = tr_torrentStat( tor->tor );

        if( TR_STATUS_ACTIVE & st->status )
        {
            tr_torrentStop( tor->tor );
            savestate();
        }
    }
}

void
torrent_remove( int id )
{
    struct tor * tor;

    assert( NULL != gl_handle );
    assert( !gl_exiting );

    tor = idlookup( id );
    if( tor != NULL )
    {
        closetor( tor, 1 );
        savestate();
    }
}

const tr_info *
torrent_info( int id )
{
    struct tor * tor;

    assert( NULL != gl_handle );
    assert( !gl_exiting );

    tor = idlookup( id );
    if( NULL == tor )
    {
        return NULL;
    }

    return tr_torrentInfo( tor->tor );
}

const tr_stat *
torrent_stat( int id )
{
    struct tor * tor;

    assert( NULL != gl_handle );
    assert( !gl_exiting );

    tor = idlookup( id );
    if( NULL == tor )
    {
        return NULL;
    }

    return tr_torrentStat( tor->tor );
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

void *
torrent_iter( void * iter, int * id )
{
    struct tor * tor = iter;

    assert( NULL != gl_handle );
    assert( !gl_exiting );

    tor = iterate( tor );

    if( NULL != tor )
    {
        *id = tor->id;
    }

    return tor;
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

    tr_natTraversalEnable( gl_handle, 0 );
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
        tr_setBindPort( gl_handle, port );
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
    struct tor * tor;

    assert( NULL != gl_handle );
    assert( !gl_exiting );

    if( pex == gl_pex )
    {
        return;
    }
    gl_pex = pex;

    for( tor = iterate( NULL ); NULL != tor; tor = iterate( tor ) )
    {
        if( tor->pexset )
        {
            continue;
        }
        tr_torrentDisablePex( tor->tor, !gl_pex );
    }

    savestate();
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
    tr_natTraversalEnable( gl_handle, gl_mapping );
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
    tr_setGlobalSpeedLimit   ( gl_handle, TR_UP, uplimit );
    tr_setUseGlobalSpeedLimit( gl_handle, TR_UP, uplimit > 0 );
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
    tr_setGlobalSpeedLimit   ( gl_handle, TR_DOWN, downlimit );
    tr_setUseGlobalSpeedLimit( gl_handle, TR_DOWN, downlimit > 0 );
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

struct tor *
opentor( const char * path, const char * hash, uint8_t * data, size_t size,
         const char * dir )
{
    struct tor * tor, * found;
    int          errcode;
    const tr_info  * inf;

    assert( ( NULL != path && NULL == hash && NULL == data ) ||
            ( NULL == path && NULL != hash && NULL == data ) ||
            ( NULL == path && NULL == hash && NULL != data ) );

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

    if( NULL != path )
    {
        tor->tor = tr_torrentInit( gl_handle, path, dir,
                                   TR_FLAG_SAVE | TR_FLAG_PAUSED, &errcode );
    }
    else if( NULL != hash )
    {
        tor->tor = tr_torrentInitSaved( gl_handle, hash, dir, TR_FLAG_PAUSED,
                                        &errcode );
    }
    else
    {
        tor->tor = tr_torrentInitData( gl_handle, data, size, dir, 
                                       TR_FLAG_SAVE | TR_FLAG_PAUSED, &errcode );
    }

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
            case TR_EUNSUPPORTED:
                if( NULL == path )
                {
                    errmsg( "unsupported torrent file" );
                }
                else
                {
                    errmsg( "unsupported torrent file: %s", path );
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

    if( TR_FLAG_PRIVATE & inf->flags )
    {
        tor->pexset = 1;
        tor->pex    = 0;
    }
    else
    {
        tr_torrentDisablePex( tor->tor, !gl_pex );
    }

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
        /* XXX event_base_set( gl_base, &gl_event ); */
    }

    if( callnow )
    {
        timerfunc( -1, EV_TIMEOUT, NULL );
    }
}

static void
timerfunc( int fd UNUSED, short event UNUSED, void * arg UNUSED )
{
    struct tor       * tor, * next;
    tr_handle_status * hs;
    int                stillmore;
    struct timeval     tv;

    /* true if we've still got live torrents... */
    stillmore = tr_torrentCount( gl_handle ) != 0;

    if( gl_exiting )
    {
        if( !stillmore )
        {
            hs = tr_handleStatus( gl_handle );
            if( TR_NAT_TRAVERSAL_DISABLED != hs->natTraversalStatus )
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
loadstate( void )
{
    uint8_t   *  buf;
    size_t       len;
    benc_val_t   top, * num, * str, * list, * dict;
    int          ii;
    struct tor * tor;
    const char * dir;

    buf = readfile( gl_state, &len );
    if( NULL == buf )
    {
        return -1;
    }

    if( tr_bencLoad( buf, len, &top, NULL ) )
    {
        free( buf );
        errmsg( "failed to load bencoded data from %s", gl_state );
        return -1;
    }
    free( buf );

    num = tr_bencDictFind( &top, "autostart" );
    if( NULL != num && TYPE_INT == num->type )
    {
        gl_autostart = ( num->val.i ? 1 : 0 );
    }

    num = tr_bencDictFind( &top, "port" );
    if( NULL != num && TYPE_INT == num->type &&
        0 < num->val.i && 0xffff > num->val.i )
    {
        gl_port = num->val.i;
    }
    tr_setBindPort( gl_handle, gl_port );

    num = tr_bencDictFind( &top, "default-pex" );
    if( NULL != num && TYPE_INT == num->type )
    {
        gl_pex = ( num->val.i ? 1 : 0 );
    }

    num = tr_bencDictFind( &top, "port-mapping" );
    if( NULL != num && TYPE_INT == num->type )
    {
        gl_mapping = ( num->val.i ? 1 : 0 );
    }
    tr_natTraversalEnable( gl_handle, gl_mapping );

    num = tr_bencDictFind( &top, "upload-limit" );
    if( NULL != num && TYPE_INT == num->type )
    {
        gl_uplimit = num->val.i;
    }
    tr_setGlobalSpeedLimit( gl_handle, TR_UP, gl_uplimit );
    tr_setUseGlobalSpeedLimit( gl_handle, TR_UP, gl_uplimit > 0 );

    num = tr_bencDictFind( &top, "download-limit" );
    if( NULL != num && TYPE_INT == num->type )
    {
        gl_downlimit = num->val.i;
    }
    tr_setGlobalSpeedLimit( gl_handle, TR_DOWN, gl_downlimit );
    tr_setUseGlobalSpeedLimit( gl_handle, TR_DOWN, gl_downlimit > 0 );

    str = tr_bencDictFind( &top, "default-directory" );
    if( NULL != str && TYPE_STR == str->type )
    {
        strlcpy( gl_dir, str->val.s.s, sizeof gl_dir );
    }

    list = tr_bencDictFind( &top, "torrents" );
    if( NULL == list || TYPE_LIST != list->type )
    {
        return 0;
    }

    for( ii = 0; ii < list->val.l.count; ii++ )
    {
        dict = &list->val.l.vals[ii];
        if( TYPE_DICT != dict->type )
        {
            continue;
        }

        str = tr_bencDictFind( dict, "directory" );
        dir = ( NULL != str && TYPE_STR == str->type ? str->val.s.s : NULL );

        str = tr_bencDictFind( dict, "hash" );
        if( NULL == str || TYPE_STR != str->type ||
            2 * SHA_DIGEST_LENGTH != str->val.s.i )
        {
            continue;
        }

        tor = opentor( NULL, str->val.s.s, NULL, 0, dir );
        if( NULL == tor )
        {
            continue;
        }

        num = tr_bencDictFind( dict, "pex" );
        if( NULL != num && TYPE_INT == num->type )
        {
            tor->pexset = 1;
            tor->pex = ( num->val.i ? 1 : 0 );
        }
        tr_torrentDisablePex( tor->tor, !( tor->pexset ? tor->pex : gl_pex ) );

        num = tr_bencDictFind( dict, "paused" );
        if( NULL != num && TYPE_INT == num->type && !num->val.i )
        {
            tr_torrentStart( tor->tor );
        }
    }

    return 0;
}

int
savestate( void )
{
    benc_val_t   top, * list, * tor;
    struct tor * ii;
    uint8_t    * buf;
    int          len, pexset;

    tr_bencInit( &top, TYPE_DICT );
    if( tr_bencDictReserve( &top, 8 ) )
    {
      nomem:
        tr_bencFree( &top );
        errmsg( "failed to save state: failed to allocate memory" );
        return -1;
    }
    tr_bencInitInt( tr_bencDictAdd( &top, "autostart" ),      gl_autostart );
    tr_bencInitInt( tr_bencDictAdd( &top, "port" ),           gl_port );
    tr_bencInitInt( tr_bencDictAdd( &top, "default-pex" ),    gl_pex );
    tr_bencInitInt( tr_bencDictAdd( &top, "port-mapping" ),   gl_mapping );
    tr_bencInitInt( tr_bencDictAdd( &top, "upload-limit" ),   gl_uplimit );
    tr_bencInitInt( tr_bencDictAdd( &top, "download-limit" ), gl_downlimit );
    tr_bencInitStr( tr_bencDictAdd( &top, "default-directory" ),
                    gl_dir, -1, 1 );
    list = tr_bencDictAdd( &top, "torrents" );
    tr_bencInit( list, TYPE_LIST );

    len = 0;
    RB_FOREACH( ii, tortree, &gl_tree )
    {
        len++;
    }
    if( tr_bencListReserve( list, len ) )
    {
        goto nomem;
    }

    RB_FOREACH( ii, tortree, &gl_tree )
    {
        const tr_info * inf;
        const tr_stat * st;
        tor = tr_bencListAdd( list );
        assert( NULL != tor );
        tr_bencInit( tor, TYPE_DICT );
        inf    = tr_torrentInfo( ii->tor );
        st     = tr_torrentStat( ii->tor );
        pexset = ( ii->pexset && !( TR_FLAG_PRIVATE & inf->flags ) );
        if( tr_bencDictReserve( tor, ( pexset ? 4 : 3 ) ) )
        {
            goto nomem;
        }
        tr_bencInitStr( tr_bencDictAdd( tor, "hash" ),
                        inf->hashString, 2 * SHA_DIGEST_LENGTH, 1 );
        tr_bencInitInt( tr_bencDictAdd( tor, "paused" ),
                        ( TR_STATUS_INACTIVE & st->status ? 1 : 0 ) );
        tr_bencInitStr( tr_bencDictAdd( tor, "directory" ),
                        tr_torrentGetFolder( ii->tor ), -1, 1 );
        if( pexset )
        {
            tr_bencInitInt( tr_bencDictAdd( tor, "pex" ), ii->pex );
        }
    }

    buf = ( uint8_t * )tr_bencSaveMalloc( &top, &len );
    SAFEBENCFREE( &top );
    if( NULL == buf )
    {
        errnomsg( "failed to save state: bencoding failed" );
        return -1;
    }

    if( 0 > writefile( gl_newstate, buf, len ) )
    {
        free( buf );
        return -1;
    }
    free( buf );

    if( 0 > rename( gl_newstate, gl_state ) )
    {
        errnomsg( "failed to save state: failed to rename %s to %s",
                  gl_newstate, CONF_FILE_STATE );
        return -1;
    }

    return 0;
}

int
torhashcmp( struct tor * left, struct tor * right )
{
    return memcmp( left->hash, right->hash, sizeof left->hash );
}

struct tor *
idlookup( int id )
{
    struct tor key, * found;

    memset( &key, 0, sizeof key );
    key.id = id;
    found = RB_FIND( tortree, &gl_tree, &key );

    return found;
}

struct tor *
hashlookup( const uint8_t * hash )
{
    struct tor key, * found;

    memset( &key, 0, sizeof key );
    memcpy( key.hash, hash, sizeof key.hash );
    found = RB_FIND( hashtree, &gl_hashes, &key );

    return found;
}

struct tor *
iterate( struct tor * tor )
{
    assert( NULL != gl_handle );
    assert( !gl_exiting );

    if( NULL == tor )
    {
        tor = RB_MIN( tortree, &gl_tree );
    }
    else
    {
        tor = RB_NEXT( tortree, &gl_tree, tor );
    }

    return tor;
}
