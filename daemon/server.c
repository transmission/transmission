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
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <assert.h>
#include <errno.h>
#include <event.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "bsdtree.h"
#include "bencode.h"
#include "errors.h"
#include "ipc.h"
#include "misc.h"
#include "server.h"
#include "torrents.h"

/* time out clients after this many seconds */
#define CLIENT_TIMEOUT          ( 60 )

struct client
{
    int                  fd;
    struct bufferevent * ev;
    struct ipc_info      ipc;
    RB_ENTRY( client )   link;
};

RB_HEAD( allclients, client );

static void newclient( int, short, void * );
static void noop     ( struct bufferevent *, void * );
static void byebye   ( struct bufferevent *, short, void * );
static void doread   ( struct bufferevent *, void * );
static int  queuemsg ( struct client *, uint8_t *, size_t );
static void msgresp  ( struct client *, int64_t, enum ipc_msg );
static void defmsg   ( enum ipc_msg, benc_val_t *, int64_t, void * );
static void noopmsg  ( enum ipc_msg, benc_val_t *, int64_t, void * );
static void addmsg1  ( enum ipc_msg, benc_val_t *, int64_t, void * );
static void addmsg2  ( enum ipc_msg, benc_val_t *, int64_t, void * );
static void quitmsg  ( enum ipc_msg, benc_val_t *, int64_t, void * );
static void intmsg   ( enum ipc_msg, benc_val_t *, int64_t, void * );
static void strmsg   ( enum ipc_msg, benc_val_t *, int64_t, void * );
static void infomsg  ( enum ipc_msg, benc_val_t *, int64_t, void * );
static int  addinfo  ( benc_val_t *, int, int );
static int  addstat  ( benc_val_t *, int, int );
static void tormsg   ( enum ipc_msg, benc_val_t *, int64_t, void * );
static void lookmsg  ( enum ipc_msg, benc_val_t *, int64_t, void * );
static void prefmsg  ( enum ipc_msg, benc_val_t *, int64_t, void * );
static void supmsg   ( enum ipc_msg, benc_val_t *, int64_t, void * );
static int  clientcmp( struct client *, struct client * );

RB_GENERATE_STATIC( allclients, client, link, clientcmp )
INTCMP_FUNC( clientcmp, client, ev )

static struct event_base * gl_base    = NULL;
static struct ipc_funcs  * gl_tree    = NULL;
static int                 gl_exiting = 0;
static struct allclients   gl_clients = RB_INITIALIZER( &gl_clients );

int
server_init( struct event_base * base )
{
    assert( NULL == gl_base && NULL == gl_tree );
    gl_base = base;
    gl_tree = ipc_initmsgs();
    if( NULL == gl_tree )
    {
        return -1;
    }

    if( 0 > ipc_addmsg( gl_tree, IPC_MSG_ADDMANYFILES, addmsg1 ) ||
        0 > ipc_addmsg( gl_tree, IPC_MSG_ADDONEFILE,   addmsg2 ) ||
        0 > ipc_addmsg( gl_tree, IPC_MSG_AUTOMAP,      intmsg  ) ||
        0 > ipc_addmsg( gl_tree, IPC_MSG_AUTOSTART,    intmsg  ) ||
        0 > ipc_addmsg( gl_tree, IPC_MSG_DOWNLIMIT,    intmsg  ) ||
        0 > ipc_addmsg( gl_tree, IPC_MSG_DIR,          strmsg  ) ||
        0 > ipc_addmsg( gl_tree, IPC_MSG_GETAUTOMAP,   prefmsg ) ||
        0 > ipc_addmsg( gl_tree, IPC_MSG_GETAUTOSTART, prefmsg ) ||
        0 > ipc_addmsg( gl_tree, IPC_MSG_GETDOWNLIMIT, prefmsg ) ||
        0 > ipc_addmsg( gl_tree, IPC_MSG_GETDIR,       prefmsg ) ||
        0 > ipc_addmsg( gl_tree, IPC_MSG_GETINFO,      infomsg ) ||
        0 > ipc_addmsg( gl_tree, IPC_MSG_GETINFOALL,   infomsg ) ||
        0 > ipc_addmsg( gl_tree, IPC_MSG_GETPEX,       prefmsg ) ||
        0 > ipc_addmsg( gl_tree, IPC_MSG_GETPORT,      prefmsg ) ||
        0 > ipc_addmsg( gl_tree, IPC_MSG_GETSTAT,      infomsg ) ||
        0 > ipc_addmsg( gl_tree, IPC_MSG_GETSTATALL,   infomsg ) ||
        0 > ipc_addmsg( gl_tree, IPC_MSG_GETUPLIMIT,   prefmsg ) ||
        0 > ipc_addmsg( gl_tree, IPC_MSG_GETSUP,       supmsg  ) ||
        0 > ipc_addmsg( gl_tree, IPC_MSG_LOOKUP,       lookmsg ) ||
        0 > ipc_addmsg( gl_tree, IPC_MSG_NOOP,         noopmsg ) ||
        0 > ipc_addmsg( gl_tree, IPC_MSG_PEX,          intmsg  ) ||
        0 > ipc_addmsg( gl_tree, IPC_MSG_PORT,         intmsg  ) ||
        0 > ipc_addmsg( gl_tree, IPC_MSG_QUIT,         quitmsg ) ||
        0 > ipc_addmsg( gl_tree, IPC_MSG_REMOVE,       tormsg  ) ||
        0 > ipc_addmsg( gl_tree, IPC_MSG_REMOVEALL,    tormsg  ) ||
        0 > ipc_addmsg( gl_tree, IPC_MSG_START,        tormsg  ) ||
        0 > ipc_addmsg( gl_tree, IPC_MSG_STARTALL,     tormsg  ) ||
        0 > ipc_addmsg( gl_tree, IPC_MSG_STOP,         tormsg  ) ||
        0 > ipc_addmsg( gl_tree, IPC_MSG_STOPALL,      tormsg  ) ||
        0 > ipc_addmsg( gl_tree, IPC_MSG_UPLIMIT,      intmsg  ) )
    {
        return -1;
    }

    ipc_setdefmsg( gl_tree, defmsg );

    return 0;
}

int
server_listen( int fd )
{
    struct event * ev;
    int flags;

    assert( NULL != gl_base );

    flags = fcntl( fd, F_GETFL );
    if( 0 > flags )
    {
        errnomsg( "failed to get flags on socket" );
        return -1;
    }
    if( 0 > fcntl( fd, F_SETFL, flags | O_NONBLOCK ) )
    {
        errnomsg( "failed to set flags on socket" );
        return -1;
    }

    if( 0 > listen( fd, 5 ) )
    {
        errnomsg( "failed to listen on socket" );
        return -1;
    }

    ev = malloc( sizeof *ev );
    if( NULL == ev )
    {
        mallocmsg( sizeof *ev );
        return -1;
    }

    event_set( ev, fd, EV_READ | EV_PERSIST, newclient, ev );
    /* XXX event_base_set( gl_base, ev ); */
    event_add( ev, NULL );

    return 0;
}

void
newclient( int fd, short event UNUSED, void * arg )
{
    struct sockaddr_un   sun;
    struct client      * client, * old;
    socklen_t            socklen;
    struct bufferevent * clev;
    int                  clfd;
    size_t               buflen;
    uint8_t            * buf;

    if( gl_exiting )
    {
        event_del( arg );
        return;
    }

    for( ;; )
    {
        client = calloc( 1, sizeof *client );
        if( NULL == client )
        {
            mallocmsg( sizeof *client );
            return;
        }

        socklen = sizeof sun;
        clfd = accept( fd, ( struct sockaddr * )&sun, &socklen );
        if( 0 > clfd )
        {
            if( EWOULDBLOCK != errno && EAGAIN != errno &&
                ECONNABORTED != errno )
            {
                errnomsg( "failed to accept ipc connection" );
            }
            free( client );
            break;
        }

        clev = bufferevent_new( clfd, doread, noop, byebye, client );
        if( NULL == clev )
        {
            close( clfd );
            free( client );
            mallocmsg( -1 );
            return;
        }
        /* XXX bufferevent_base_set( gl_base, clev ); */
        bufferevent_settimeout( clev, CLIENT_TIMEOUT, CLIENT_TIMEOUT );

        client->fd      = clfd;
        ipc_newcon( &client->ipc, gl_tree );
        client->ev      = clev;
        old = RB_INSERT( allclients, &gl_clients, client );
        assert( NULL == old );

        bufferevent_enable( clev, EV_READ );
        buf = ipc_mkvers( &buflen );
        if( 0 > queuemsg( client, buf, buflen ) )
        {
            free( buf );
            return;
        }
        free( buf );
    }
}

void
noop( struct bufferevent * ev UNUSED, void * arg UNUSED )
{
    /* libevent prior to 1.2 couldn't handle a NULL write callback */
}

void
byebye( struct bufferevent * ev, short what, void * arg UNUSED )
{
    struct client * client, key;

    if( !( EVBUFFER_EOF & what ) )
    {
        if( EVBUFFER_TIMEOUT & what )
        {
            errmsg( "client connection timed out" );
        }
        else if( EVBUFFER_READ & what )
        {
            errmsg( "read error on client connection" );
        }
        else if( EVBUFFER_WRITE & what )
        {
            errmsg( "write error on client connection" );
        }
        else if( EVBUFFER_ERROR & what )
        {
            errmsg( "error on client connection" );
        }
        else
        {
            errmsg( "unknown error on client connection: 0x%x", what );
        }
    }

    bzero( &key, sizeof key );
    key.ev = ev;
    client = RB_FIND( allclients, &gl_clients, &key );
    assert( NULL != client );
    RB_REMOVE( allclients, &gl_clients, client );
    bufferevent_free( ev );
    close( client->fd );
    free( client );
}

void
doread( struct bufferevent * ev, void * arg )
{
    struct client * client = arg;
    ssize_t         res;
    uint8_t       * buf;
    size_t          len;

    assert( !gl_exiting );

    buf = EVBUFFER_DATA( EVBUFFER_INPUT( ev ) );
    len = EVBUFFER_LENGTH( EVBUFFER_INPUT( ev ) );

    if( IPC_MIN_MSG_LEN > len )
    {
        return;
    }

    res = ipc_parse( &client->ipc, buf, len, client );

    if( gl_exiting )
    {
        return;
    }

    if( 0 > res )
    {
        switch( errno )
        {
            case EPERM:
                errmsg( "unsupported protocol version" );
                break;
            case EINVAL:
                errmsg( "protocol parse error" );
                break;
            default:
                errnomsg( "parsing failed" );
                break;
        }
        byebye( ev, EVBUFFER_ERROR, NULL );
    }
    else if( 0 < res )
    {
        evbuffer_drain( EVBUFFER_INPUT( ev ), res );
    }
}

int
queuemsg( struct client * client, uint8_t * buf, size_t buflen )
{
    if( NULL == buf )
    {
        if( EPERM != errno )
        {
            errnomsg( "failed to build message" );
            byebye( client->ev, EVBUFFER_EOF, NULL );
        }
        return -1;
    }

    if( 0 > bufferevent_write( client->ev, buf, buflen ) )
    {
        errnomsg( "failed to buffer %zd bytes of data for write", buflen );
        return -1;
    }

    return 0;
}

void
msgresp( struct client * client, int64_t tag, enum ipc_msg id )
{
    uint8_t * buf;
    size_t    buflen;

    if( 0 >= tag )
    {
        return;
    }

    buf = ipc_mkempty( &client->ipc, &buflen, id, tag );
    queuemsg( client, buf, buflen );
    free( buf );
}

void
defmsg( enum ipc_msg id UNUSED, benc_val_t * val UNUSED, int64_t tag,
        void * arg )
{
    struct client * client = arg;

    msgresp( client, tag, IPC_MSG_NOTSUP );
}

void
noopmsg( enum ipc_msg id UNUSED, benc_val_t * val UNUSED, int64_t tag,
         void * arg )
{
    struct client * client = arg;

    msgresp( client, tag, IPC_MSG_OK );
}

void
addmsg1( enum ipc_msg id UNUSED, benc_val_t * val, int64_t tag, void * arg )
{
    struct client * client = arg;
    benc_val_t      pk, * added, * file;
    int             ii, tor;
    size_t          buflen;
    uint8_t       * buf;
    tr_info_t     * inf;

    if( NULL == val || TYPE_LIST != val->type )
    {
        msgresp( client, tag, IPC_MSG_NOTSUP );
        return;
    }

    added = ipc_initval( &client->ipc, IPC_MSG_INFO, tag, &pk, TYPE_LIST );
    if( NULL == added )
    {
        errnomsg( "failed to build message" );
        byebye( client->ev, EVBUFFER_EOF, NULL );
        return;
    }

    for( ii = 0; ii < val->val.l.count; ii++ )
    {
        file = &val->val.l.vals[ii];
        if( TYPE_STR != file->type )
        {
            continue;
        }
        /* XXX need to somehow inform client of skipped or failed files */
        tor = torrent_add_file( file->val.s.s, NULL, -1 );
        if( TORRENT_ID_VALID( tor ) )
        {
            inf = torrent_info( tor );
            if( 0 > ipc_addinfo( added, tor, inf, 0 ) )
            {
                errnomsg( "failed to build message" );
                tr_bencFree( &pk );
                byebye( client->ev, EVBUFFER_EOF, NULL );
                return;
            }
        }
    }

    buf = ipc_mkval( &pk, &buflen );
    tr_bencFree( &pk );
    queuemsg( client, buf, buflen );
    free( buf );
}

void
addmsg2( enum ipc_msg id UNUSED, benc_val_t * dict, int64_t tag, void * arg )
{
    struct client * client = arg;
    benc_val_t    * val, pk;
    int             tor, start;
    size_t          buflen;
    uint8_t       * buf;
    const char    * dir;
    tr_info_t     * inf;

    if( NULL == dict || TYPE_DICT != dict->type )
    {
        msgresp( client, tag, IPC_MSG_NOTSUP );
        return;
    }

    val   = tr_bencDictFind( dict, "directory" );
    dir   = ( NULL == val || TYPE_STR != val->type ? NULL : val->val.s.s );
    val   = tr_bencDictFind( dict, "autostart" );
    start = ( NULL == val || TYPE_INT != val->type ? -1 :
              ( val->val.i ? 1 : 0 ) );
    val   = tr_bencDictFind( dict, "data" );
    if( NULL != val && TYPE_STR == val->type )
    {
        /* XXX detect duplicates and return a message indicating so */
        tor = torrent_add_data( ( uint8_t * )val->val.s.s, val->val.s.i,
                                dir, start );
    }
    else
    {
        val = tr_bencDictFind( dict, "file" );
        if( NULL == val || TYPE_STR != val->type )
        {
            msgresp( client, tag, IPC_MSG_NOTSUP );
            return;
        }
        /* XXX detect duplicates and return a message indicating so */
        tor = torrent_add_file( val->val.s.s, dir, start );
    }

    if( TORRENT_ID_VALID( tor ) )
    {
        val = ipc_initval( &client->ipc, IPC_MSG_INFO, tag, &pk, TYPE_LIST );
        if( NULL == val )
        {
            errnomsg( "failed to build message" );
            byebye( client->ev, EVBUFFER_EOF, NULL );
            return;
        }
        inf = torrent_info( tor );
        if( 0 > ipc_addinfo( val, tor, inf, 0 ) )
        {
            errnomsg( "failed to build message" );
            tr_bencFree( &pk );
            byebye( client->ev, EVBUFFER_EOF, NULL );
            return;
        }
        buf = ipc_mkval( &pk, &buflen );
        tr_bencFree( &pk );
        queuemsg( client, buf, buflen );
        free( buf );
    }
    else
    {
        msgresp( client, tag, IPC_MSG_FAIL );
    }
}

void
quitmsg( enum ipc_msg id UNUSED, benc_val_t * val UNUSED, int64_t tag UNUSED,
         void * arg UNUSED )
{
    struct client * ii, * next;

    torrent_exit( 0 );
    gl_exiting = 1;

    for( ii = RB_MIN( allclients, &gl_clients ); NULL != ii; ii = next )
    {
        next = RB_NEXT( allclients, &gl_clients, ii );
        byebye( ii->ev, EVBUFFER_EOF, NULL );
    }
}

void
intmsg( enum ipc_msg id, benc_val_t * val, int64_t tag, void * arg )
{
    struct client * client = arg;
    int             num;

    if( NULL == val || TYPE_INT != val->type )
    {
        msgresp( client, tag, IPC_MSG_NOTSUP );
        return;
    }

    num = MAX( INT_MIN, MIN( INT_MAX, val->val.i ) );
    switch( id )
    {
        case IPC_MSG_AUTOMAP:
            torrent_enable_port_mapping( num ? 1 : 0 );
            break;
        case IPC_MSG_AUTOSTART:
            torrent_set_autostart( num ? 1 : 0 );
            break;
        case IPC_MSG_DOWNLIMIT:
            torrent_set_downlimit( num );
            break;
        case IPC_MSG_PEX:
            torrent_set_pex( num ? 1 : 0 );
            break;
        case IPC_MSG_PORT:
            torrent_set_port( num );
            break;
        case IPC_MSG_UPLIMIT:
            torrent_set_uplimit( num );
            break;
        default:
            assert( 0 );
            return;
    }

    msgresp( client, tag, IPC_MSG_OK );
}

void
strmsg( enum ipc_msg id, benc_val_t * val, int64_t tag, void * arg )
{
    struct client * client = arg;

    if( NULL == val || TYPE_STR != val->type )
    {
        msgresp( client, tag, IPC_MSG_NOTSUP );
        return;
    }

    switch( id )
    {
        case IPC_MSG_DIR:
            torrent_set_directory( val->val.s.s );
            break;
        default:
            assert( 0 );
            return;
    }

    msgresp( client, tag, IPC_MSG_OK );
}

void
infomsg( enum ipc_msg id, benc_val_t * val, int64_t tag, void * arg )
{
    struct client * client = arg;
    uint8_t       * buf;
    size_t          buflen;
    benc_val_t      pk, * pkinf, * typelist, * idlist, * idval;
    int             all, types, ii, tor;
    void          * iter;
    enum ipc_msg    respid;
    int         ( * addfunc )( benc_val_t *, int, int );

    all = 0;
    switch( id )
    {
        case IPC_MSG_GETINFOALL:
            all = 1;
            /* FALLTHROUGH; */
        case IPC_MSG_GETINFO:
            respid = IPC_MSG_INFO;
            addfunc = addinfo;
            break;
        case IPC_MSG_GETSTATALL:
            all = 1;
            /* FALLTHROUGH */
        case IPC_MSG_GETSTAT:
            respid = IPC_MSG_STAT;
            addfunc = addstat;
            break;
        default:
            assert( 0 );
            return;
    }

    /* initialize packet */
    pkinf = ipc_initval( &client->ipc, respid, tag, &pk, TYPE_LIST );
    if( NULL == pkinf )
    {
        errnomsg( "failed to build message" );
        byebye( client->ev, EVBUFFER_EOF, NULL );
        return;
    }

    /* add info/status for all torrents */
    if( all )
    {
        if( NULL == val || TYPE_LIST != val->type )
        {
            msgresp( client, tag, IPC_MSG_NOTSUP );
            tr_bencFree( &pk );
            return;
        }
        types = ipc_infotypes( respid, val );
        iter = NULL;
        while( NULL != ( iter = torrent_iter( iter, &tor ) ) )
        {
            if( 0 > addfunc( pkinf, tor, types ) )
            {
                errnomsg( "failed to build message" );
                tr_bencFree( &pk );
                byebye( client->ev, EVBUFFER_EOF, NULL );
                return;
            }
        }
    }
    /* add info/status for the requested IDs */
    else
    {
        if( NULL == val || TYPE_DICT != val->type )
        {
            msgresp( client, tag, IPC_MSG_NOTSUP );
            tr_bencFree( &pk );
            return;
        }
        typelist = tr_bencDictFind( val, "type" );
        idlist   = tr_bencDictFind( val, "id" );
        if( NULL == typelist || TYPE_LIST != typelist->type ||
            NULL == idlist   || TYPE_LIST != idlist->type )
        {
            msgresp( client, tag, IPC_MSG_NOTSUP );
            tr_bencFree( &pk );
            return;
        }
        types = ipc_infotypes( respid, typelist );
        for( ii = 0; idlist->val.l.count > ii; ii++ )
        {
            idval = &idlist->val.l.vals[ii];
            if( TYPE_INT != idval->type || !TORRENT_ID_VALID( idval->val.i ) )
            {
                continue;
            }
            tor = idval->val.i;
            if( 0 > addfunc( pkinf, idval->val.i, types ) )
            {
                errnomsg( "failed to build message" );
                tr_bencFree( &pk );
                byebye( client->ev, EVBUFFER_EOF, NULL );
                return;
            }
        }
    }

    /* generate packet data and send it */
    buf = ipc_mkval( &pk, &buflen );
    tr_bencFree( &pk );
    queuemsg( client, buf, buflen );
    free( buf );
}

int
addinfo( benc_val_t * list, int id, int types )
{
    tr_info_t * inf;

    inf = torrent_info( id );
    if( NULL == inf )
    {
        return 0;
    }

    return ipc_addinfo( list, id, inf, types );
}

int
addstat( benc_val_t * list, int id, int types )
{
    tr_stat_t * st;
    tr_info_t * inf;

    st = torrent_stat( id );
    if( NULL == st )
    {
        return 0;
    }
    inf = torrent_info( id );
    assert( NULL != inf );

    return ipc_addstat( list, id, inf, st, types );
}

void
tormsg( enum ipc_msg id, benc_val_t * val, int64_t tag, void * arg )
{
    struct client * client = arg;
    benc_val_t    * idval;
    int             ii, all;
    void          * iter;
    void        ( * func )( int );

    all = 0;
    switch( id )
    {
        case IPC_MSG_REMOVEALL:
            all = 1;
            /* FALLTHROUGH */
        case IPC_MSG_REMOVE:
            func = torrent_remove;
            break;
        case IPC_MSG_STARTALL:
            all = 1;
            /* FALLTHROUGH */
        case IPC_MSG_START:
            func = torrent_start;
            break;
        case IPC_MSG_STOPALL:
            all = 1;
            /* FALLTHROUGH */
        case IPC_MSG_STOP:
            func = torrent_stop;
            break;
        default:
            assert( 0 );
            return;
    }

    /* remove/start/stop all torrents */
    if( all )
    {
        iter = NULL;
        while( NULL != ( iter = torrent_iter( iter, &ii ) ) )
        {
            func( ii );
        }
    }
    /* remove/start/stop requested list of torrents */
    else
    {
        if( NULL == val || TYPE_LIST != val->type )
        {
            msgresp( client, tag, IPC_MSG_NOTSUP );
            return;
        }
        for( ii = 0; val->val.l.count > ii; ii++ )
        {
            idval = &val->val.l.vals[ii];
            if( TYPE_INT != idval->type || !TORRENT_ID_VALID( idval->val.i ) )
            {
                continue;
            }
            func( idval->val.i );
        }
    }

    msgresp( client, tag, IPC_MSG_OK );
}

void
lookmsg( enum ipc_msg id UNUSED, benc_val_t * val, int64_t tag, void * arg )
{
    struct client * client = arg;
    uint8_t       * buf;
    size_t          buflen;
    int             ii;
    benc_val_t    * hash, pk, * pkinf;
    int64_t         found;
    tr_info_t     * inf;

    if( NULL == val || TYPE_LIST != val->type )
    {
        msgresp( client, tag, IPC_MSG_NOTSUP );
        return;
    }

    pkinf = ipc_initval( &client->ipc, IPC_MSG_INFO, tag, &pk, TYPE_LIST );
    if( NULL == pkinf )
    {
        errnomsg( "failed to build message" );
        byebye( client->ev, EVBUFFER_EOF, NULL );
        return;
    }

    for( ii = 0; val->val.l.count > ii; ii++ )
    {
        hash = &val->val.l.vals[ii];
        if( NULL == hash || TYPE_STR != hash->type ||
            SHA_DIGEST_LENGTH * 2 != hash->val.s.i )
        {
            tr_bencFree( &pk );
            msgresp( client, tag, IPC_MSG_NOTSUP );
            return;
        }
        found = torrent_lookup( ( uint8_t * )hash->val.s.s );
        if( !TORRENT_ID_VALID( found ) )
        {
            continue;
        }
        inf = torrent_info( found );
        assert( NULL != inf );
        if( 0 > ipc_addinfo( pkinf, found, inf, IPC_INF_HASH ) )
        {
            errnomsg( "failed to build message" );
            tr_bencFree( &pk );
            byebye( client->ev, EVBUFFER_EOF, NULL );
            return;
        }
    }

    buf = ipc_mkval( &pk, &buflen );
    tr_bencFree( &pk );
    queuemsg( client, buf, buflen );
    free( buf );
}

void
prefmsg( enum ipc_msg id, benc_val_t * val UNUSED, int64_t tag, void * arg )
{
    struct client * client = arg;
    uint8_t       * buf;
    size_t          buflen;

    switch( id )
    {
        case IPC_MSG_GETAUTOMAP:
            buf = ipc_mkint( &client->ipc, &buflen, IPC_MSG_AUTOMAP, tag,
                             torrent_get_port_mapping() );
            break;
        case IPC_MSG_GETAUTOSTART:
            buf = ipc_mkint( &client->ipc, &buflen, IPC_MSG_AUTOSTART, tag,
                             torrent_get_autostart() );
            break;
        case IPC_MSG_GETDIR:
            buf = ipc_mkstr( &client->ipc, &buflen, IPC_MSG_DIR, tag,
                             torrent_get_directory() );
            break;
        case IPC_MSG_GETDOWNLIMIT:
            buf = ipc_mkint( &client->ipc, &buflen, IPC_MSG_DOWNLIMIT, tag,
                             torrent_get_downlimit() );
            break;
        case IPC_MSG_GETPEX:
            buf = ipc_mkint( &client->ipc, &buflen, IPC_MSG_PEX, tag,
                             torrent_get_pex() );
            break;
        case IPC_MSG_GETPORT:
            buf = ipc_mkint( &client->ipc, &buflen, IPC_MSG_PORT, tag,
                             torrent_get_port() );
            break;
        case IPC_MSG_GETUPLIMIT:
            buf = ipc_mkint( &client->ipc, &buflen, IPC_MSG_UPLIMIT, tag,
                             torrent_get_uplimit() );
            break;
        default:
            assert( 0 );
            return;
    }

    queuemsg( client, buf, buflen );
    free( buf );
}

void
supmsg( enum ipc_msg id UNUSED, benc_val_t * val, int64_t tag, void * arg )
{
    struct client  * client = arg;
    uint8_t        * buf;
    size_t           buflen;
    int              ii;
    benc_val_t       pk, *pkval, * name;
    enum ipc_msg     found;

    if( NULL == val || TYPE_LIST != val->type )
    {
        msgresp( client, tag, IPC_MSG_NOTSUP );
        return;
    }

    pkval = ipc_initval( &client->ipc, IPC_MSG_SUP, tag, &pk, TYPE_LIST );
    if( NULL == pkval )
    {
        errnomsg( "failed to build message" );
        byebye( client->ev, EVBUFFER_EOF, NULL );
        return;
    }
    /* XXX look at other initval to make sure we free pk */
    if( tr_bencListReserve( pkval, val->val.l.count ) )
    {
        errnomsg( "failed to build message" );
        tr_bencFree( &pk );
        byebye( client->ev, EVBUFFER_EOF, NULL );
        return;
    }

    for( ii = 0; val->val.l.count > ii; ii++ )
    {
        name = &val->val.l.vals[ii];
        if( NULL == name || TYPE_STR != name->type )
        {
            tr_bencFree( &pk );
            msgresp( client, tag, IPC_MSG_NOTSUP );
            return;
        }
        found = ipc_msgid( &client->ipc, name->val.s.s );
        if( IPC__MSG_COUNT == found )
        {
            continue;
        }
        tr_bencInitStr( tr_bencListAdd( pkval ),
                        name->val.s.s, name->val.s.i, 1 );
    }

    buf = ipc_mkval( &pk, &buflen );
    tr_bencFree( &pk );
    queuemsg( client, buf, buflen );
    free( buf );
}
