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

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "transmission.h"
#include "utils.h"

#include "ipcparse.h"
#include "bsdtree.h"

/* begin copy-paste from daemon/misc.h */

#define ARRAYLEN( ary )         ( sizeof( ary ) / sizeof( (ary)[0] ) )

#ifndef MIN
#define MIN( aa, bb )           ( (aa) < (bb) ? (aa) : (bb) )
#endif
#ifndef MAX
#define MAX( aa, bb )           ( (aa) > (bb) ? (aa) : (bb) )
#endif

#undef NULL
#define NULL                    ( ( void * )0 )

#define SAFEFREE( ptr )                                                       \
    do                                                                        \
    {                                                                         \
        int saved = errno;                                                    \
        free( ptr );                                                          \
        errno = saved;                                                        \
    }                                                                         \
    while( 0 )
#define SAFEFREESTRLIST( ptr )                                                \
    do                                                                        \
    {                                                                         \
        int saved = errno;                                                    \
        FREESTRLIST( ptr );                                                   \
        errno = saved;                                                        \
    }                                                                         \
    while( 0 )
#define SAFEBENCFREE( val )                                                   \
    do                                                                        \
    {                                                                         \
        int saved = errno;                                                    \
        tr_bencFree( val );                                                   \
        errno = saved;                                                        \
    }                                                                         \
    while( 0 )

#define INTCMP_FUNC( name, type, id )                                         \
int                                                                           \
name( struct type * _icf_first, struct type * _icf_second )                   \
{                                                                             \
    if( _icf_first->id < _icf_second->id )                                    \
    {                                                                         \
        return -1;                                                            \
    }                                                                         \
    else if( _icf_first->id > _icf_second->id )                               \
    {                                                                         \
        return 1;                                                             \
    }                                                                         \
    else                                                                      \
    {                                                                         \
        return 0;                                                             \
    }                                                                         \
}

/* end copy-paste from daemon/misc.h */

/* IPC protocol version */
#define PROTO_VERS_MIN          ( 1 )
#define PROTO_VERS_MAX          ( 2 )

#define MSGVALID( id )          ( IPC__MSG_COUNT > (id) )
#define MSGNAME( id )           ( gl_msgs[(id)].name )
#define DICTPAYLOAD( info )     ( 2 > (info)->vers )

struct msg
{
    const char        * name;
    const int          minvers;
    const enum ipc_msg id;
    RB_ENTRY( msg )    link;
};

struct inf
{
    const char    * name;
    const int       type;
    RB_ENTRY( inf ) link;
};

struct msgfunc
{
    int             id;
    trd_msgfunc     func;
    RB_ENTRY( msgfunc ) link;
};

RB_HEAD( msgtree, msg );
RB_HEAD( inftree, inf );
RB_HEAD( functree, msgfunc );

struct ipc_funcs
{
    struct functree msgs;
    trd_msgfunc    def;
};

static struct msg gl_msgs[] =
{
    { "addfiles",            1, IPC_MSG_ADDMANYFILES, RB_ENTRY_INITIALIZER() },
    { "addfile-detailed",    2, IPC_MSG_ADDONEFILE,   RB_ENTRY_INITIALIZER() },
    { "automap",             2, IPC_MSG_AUTOMAP,      RB_ENTRY_INITIALIZER() },
    { "autostart",           2, IPC_MSG_AUTOSTART,    RB_ENTRY_INITIALIZER() },
    { "bad-format",          2, IPC_MSG_BAD,          RB_ENTRY_INITIALIZER() },
    { "encryption",          2, IPC_MSG_CRYPTO,       RB_ENTRY_INITIALIZER() },
    { "directory",           2, IPC_MSG_DIR,          RB_ENTRY_INITIALIZER() },
    { "downlimit",           2, IPC_MSG_DOWNLIMIT,    RB_ENTRY_INITIALIZER() },
    { "failed",              2, IPC_MSG_FAIL,         RB_ENTRY_INITIALIZER() },
    { "get-automap",         2, IPC_MSG_GETAUTOMAP,   RB_ENTRY_INITIALIZER() },
    { "get-autostart",       2, IPC_MSG_GETAUTOSTART, RB_ENTRY_INITIALIZER() },
    { "get-encryption",      2, IPC_MSG_GETCRYPTO,    RB_ENTRY_INITIALIZER() },
    { "get-directory",       2, IPC_MSG_GETDIR,       RB_ENTRY_INITIALIZER() },
    { "get-downlimit",       2, IPC_MSG_GETDOWNLIMIT, RB_ENTRY_INITIALIZER() },
    { "get-info",            2, IPC_MSG_GETINFO,      RB_ENTRY_INITIALIZER() },
    { "get-info-all",        2, IPC_MSG_GETINFOALL,   RB_ENTRY_INITIALIZER() },
    { "get-pex",             2, IPC_MSG_GETPEX,       RB_ENTRY_INITIALIZER() },
    { "get-port",            2, IPC_MSG_GETPORT,      RB_ENTRY_INITIALIZER() },
    { "get-status",          2, IPC_MSG_GETSTAT,      RB_ENTRY_INITIALIZER() },
    { "get-status-all",      2, IPC_MSG_GETSTATALL,   RB_ENTRY_INITIALIZER() },
    { "get-supported",       2, IPC_MSG_GETSUP,       RB_ENTRY_INITIALIZER() },
    { "get-uplimit",         2, IPC_MSG_GETUPLIMIT,   RB_ENTRY_INITIALIZER() },
    { "lookup",              2, IPC_MSG_LOOKUP,       RB_ENTRY_INITIALIZER() },
    { "info",                2, IPC_MSG_INFO,         RB_ENTRY_INITIALIZER() },
    { "noop",                2, IPC_MSG_NOOP,         RB_ENTRY_INITIALIZER() },
    { "not-supported",       2, IPC_MSG_NOTSUP,       RB_ENTRY_INITIALIZER() },
    { "pex",                 2, IPC_MSG_PEX,          RB_ENTRY_INITIALIZER() },
    { "port",                2, IPC_MSG_PORT,         RB_ENTRY_INITIALIZER() },
    { "quit",                1, IPC_MSG_QUIT,         RB_ENTRY_INITIALIZER() },
    { "remove",              2, IPC_MSG_REMOVE,       RB_ENTRY_INITIALIZER() },
    { "remove-all",          2, IPC_MSG_REMOVEALL,    RB_ENTRY_INITIALIZER() },
    { "start",               2, IPC_MSG_START,        RB_ENTRY_INITIALIZER() },
    { "start-all",           2, IPC_MSG_STARTALL,     RB_ENTRY_INITIALIZER() },
    { "status",              2, IPC_MSG_STAT,         RB_ENTRY_INITIALIZER() },
    { "stop",                2, IPC_MSG_STOP,         RB_ENTRY_INITIALIZER() },
    { "stop-all",            2, IPC_MSG_STOPALL,      RB_ENTRY_INITIALIZER() },
    { "succeeded",           2, IPC_MSG_OK,           RB_ENTRY_INITIALIZER() },
    { "supported",           2, IPC_MSG_SUP,          RB_ENTRY_INITIALIZER() },
    { "uplimit",             2, IPC_MSG_UPLIMIT,      RB_ENTRY_INITIALIZER() },
    { "version",             1, IPC_MSG_VERSION,      RB_ENTRY_INITIALIZER() },
};

static struct inf gl_inf[] =
{
    { "comment",                IPC_INF_COMMENT,      RB_ENTRY_INITIALIZER() },
    { "creator",                IPC_INF_CREATOR,      RB_ENTRY_INITIALIZER() },
    { "date",                   IPC_INF_DATE,         RB_ENTRY_INITIALIZER() },
    { "files",                  IPC_INF_FILES,        RB_ENTRY_INITIALIZER() },
    { "hash",                   IPC_INF_HASH,         RB_ENTRY_INITIALIZER() },
    { "id",                     IPC_INF_ID,           RB_ENTRY_INITIALIZER() },
    { "name",                   IPC_INF_NAME,         RB_ENTRY_INITIALIZER() },
    { "path",                   IPC_INF_PATH,         RB_ENTRY_INITIALIZER() },
    { "private",                IPC_INF_PRIVATE,      RB_ENTRY_INITIALIZER() },
    { "size",                   IPC_INF_SIZE,         RB_ENTRY_INITIALIZER() },
    { "trackers",               IPC_INF_TRACKERS,     RB_ENTRY_INITIALIZER() },
};

static struct inf gl_stat[] =
{
    { "completed",              IPC_ST_COMPLETED,     RB_ENTRY_INITIALIZER() },
    { "download-speed",         IPC_ST_DOWNSPEED,     RB_ENTRY_INITIALIZER() },
    { "download-total",         IPC_ST_DOWNTOTAL,     RB_ENTRY_INITIALIZER() },
    { "download-valid",         IPC_ST_DOWNVALID,     RB_ENTRY_INITIALIZER() },
    { "error",                  IPC_ST_ERROR,         RB_ENTRY_INITIALIZER() },
    { "error-message",          IPC_ST_ERRMSG,        RB_ENTRY_INITIALIZER() },
    { "eta",                    IPC_ST_ETA,           RB_ENTRY_INITIALIZER() },
    { "id",                     IPC_ST_ID,            RB_ENTRY_INITIALIZER() },
    { "peers-downloading",      IPC_ST_PEERDOWN,      RB_ENTRY_INITIALIZER() },
    { "peers-from",             IPC_ST_PEERFROM,      RB_ENTRY_INITIALIZER() },
    { "peers-total",            IPC_ST_PEERTOTAL,     RB_ENTRY_INITIALIZER() },
    { "peers-uploading",        IPC_ST_PEERUP,        RB_ENTRY_INITIALIZER() },
    { "running",                IPC_ST_RUNNING,       RB_ENTRY_INITIALIZER() },
    { "state",                  IPC_ST_STATE,         RB_ENTRY_INITIALIZER() },
    { "swarm-speed",            IPC_ST_SWARM,         RB_ENTRY_INITIALIZER() },
    { "tracker",                IPC_ST_TRACKER,       RB_ENTRY_INITIALIZER() },
    { "scrape-completed",       IPC_ST_TKDONE,        RB_ENTRY_INITIALIZER() },
    { "scrape-leechers",        IPC_ST_TKLEECH,       RB_ENTRY_INITIALIZER() },
    { "scrape-seeders",         IPC_ST_TKSEED,        RB_ENTRY_INITIALIZER() },
    { "upload-speed",           IPC_ST_UPSPEED,       RB_ENTRY_INITIALIZER() },
    { "upload-total",           IPC_ST_UPTOTAL,       RB_ENTRY_INITIALIZER() },
};

static int          handlevers ( struct ipc_info *, benc_val_t * );
static int          handlemsgs ( struct ipc_info *, benc_val_t *, void * );
static int          gotmsg     ( struct ipc_info *, benc_val_t *, benc_val_t *,
                                 benc_val_t *, void * );
static int          msgcmp     ( struct msg *, struct msg * );
static int          infcmp     ( struct inf *, struct inf * );
static struct msg * msglookup  ( const char * );
static int          filltracker( benc_val_t *, const tr_tracker_info * );
static int          handlercmp ( struct msgfunc *, struct msgfunc * );

RB_GENERATE_STATIC( msgtree, msg, link, msgcmp )
RB_GENERATE_STATIC( inftree, inf, link, infcmp )
RB_GENERATE_STATIC( functree, msgfunc, link, handlercmp )
INTCMP_FUNC( handlercmp, msgfunc, id )

struct ipc_funcs *
ipc_initmsgs( void )
{
    struct ipc_funcs * tree;

    tree = malloc( sizeof *tree );
    if( NULL != tree )
    {
        RB_INIT( &tree->msgs );
        tree->def = (trd_msgfunc) NULL;
    }

    return tree;
}

int
ipc_addmsg( struct ipc_funcs * tree, enum ipc_msg id, trd_msgfunc func )
{
    struct msgfunc key, * entry;

    assert( MSGVALID( id ) );
    assert( IPC_MSG_VERSION != id );

    memset( &key, 0, sizeof key );
    key.id = id;
    entry = RB_FIND( functree, &tree->msgs, &key );
    assert( NULL == entry );

    entry = calloc( 1, sizeof *entry );
    if( NULL == entry )
    {
        return -1;
    }

    entry->id   = id;
    entry->func = func;
    entry = RB_INSERT( functree, &tree->msgs, entry );
    assert( NULL == entry );

    return 0;
}

void
ipc_setdefmsg( struct ipc_funcs * tree, trd_msgfunc func )
{
    tree->def = func;
}

void
ipc_freemsgs( struct ipc_funcs * tree )
{
    struct msgfunc * ii, * next;

    for( ii = RB_MIN( functree, &tree->msgs ); NULL != ii; ii = next )
    {
        next = RB_NEXT( functree, &tree->msgs, ii );
        RB_REMOVE( functree, &tree->msgs, ii );
        free( ii );
    }
    free( tree );
}

struct ipc_info *
ipc_newcon( struct ipc_funcs * funcs )
{
    struct ipc_info * info;

    info = calloc( 1, sizeof *info );
    if( NULL != info )
    {
        info->funcs = funcs;
        info->vers  = -1;
    }

    return info;
}

void
ipc_freecon( struct ipc_info * info )
{
    if( NULL != info )
    {
        free( info->label );
        free( info );
    }
}

benc_val_t *
ipc_initval( struct ipc_info * info, enum ipc_msg id, int64_t tag,
             benc_val_t * pk, int type )
{
    benc_val_t * ret;

    assert( MSGVALID( id ) );

    if( !ipc_havemsg( info, id ) || ( 0 < tag && !ipc_havetags( info ) ) )
    {
        errno = EPERM;
        return NULL;
    }

    if( DICTPAYLOAD( info ) )
    {
        tr_bencInit( pk, TYPE_DICT );
        if( tr_bencDictReserve( pk, 1 ) )
        {
            return NULL;
        }
        ret = tr_bencDictAdd( pk, MSGNAME( id ) );
    }
    else
    {
        tr_bencInit( pk, TYPE_LIST );
        if( tr_bencListReserve( pk, ( 0 < tag ? 3 : 2 ) ) )
        {
            return NULL;
        }
        tr_bencInitStr( tr_bencListAdd( pk ), MSGNAME( id ), -1, 1 );
        ret = tr_bencListAdd( pk );
        if( 0 < tag )
        {
            tr_bencInitInt( tr_bencListAdd( pk ), tag );
        }
    }

    tr_bencInit( ret, type );

    return ret;
}

uint8_t *
ipc_mkval( benc_val_t * pk, size_t * setmeSize )
{
    int bencSize = 0;
    char * benc = tr_bencSave( pk, &bencSize );
    uint8_t * ret = NULL;

    if( bencSize > IPC_MAX_MSG_LEN )
        errno = EFBIG;
    else {
        const size_t size = IPC_MIN_MSG_LEN + bencSize;
        ret = tr_new( uint8_t, size );
        snprintf( (char*)ret, size, "%0*X", IPC_MIN_MSG_LEN, bencSize );
        memcpy( ret + IPC_MIN_MSG_LEN, benc, bencSize );
        *setmeSize = size;
    }

    tr_free( benc );
    return ret;
}

uint8_t *
ipc_mkempty( struct ipc_info * info, size_t * len, enum ipc_msg id,
             int64_t tag )
{
    benc_val_t pk;
    uint8_t  * ret;

    if( NULL == ipc_initval( info, id, tag, &pk, TYPE_STR ) )
    {
        return NULL;
    }

    ret = ipc_mkval( &pk, len );
    SAFEBENCFREE( &pk );

    return ret;
}

uint8_t *
ipc_mkint( struct ipc_info * info, size_t * len, enum ipc_msg id, int64_t tag,
           int64_t num )
{
    benc_val_t pk, * val;
    uint8_t  * ret;

    val = ipc_initval( info, id, tag, &pk, TYPE_INT );
    if( NULL == val )
    {
        return NULL;
    }

    val->val.i = num;
    ret = ipc_mkval( &pk, len );
    SAFEBENCFREE( &pk );

    return ret;
}

uint8_t *
ipc_mkstr( struct ipc_info * info, size_t * len, enum ipc_msg id, int64_t tag,
           const char * str )
{
    benc_val_t pk, * val;
    uint8_t  * ret;

    val = ipc_initval( info, id, tag, &pk, TYPE_STR );
    if( NULL == val )
    {
        return NULL;
    }

    tr_bencInitStr( val, str, -1, 1 );
    ret = ipc_mkval( &pk, len );
    SAFEBENCFREE( &pk );

    return ret;
}

uint8_t *
ipc_mkvers( size_t * len, const char * label )
{
    benc_val_t pk, * dict;
    uint8_t  * ret;
  
    tr_bencInit( &pk, TYPE_DICT );
    if( tr_bencDictReserve( &pk, 1 ) )
    {
        return NULL;
    }
    dict = tr_bencDictAdd( &pk, MSGNAME( IPC_MSG_VERSION ) );

    tr_bencInit( dict, TYPE_DICT );
    if( tr_bencDictReserve( dict, ( NULL == label ? 2 : 3 ) ) )
    {
        SAFEBENCFREE( &pk );
        return NULL;
    }
    tr_bencInitInt( tr_bencDictAdd( dict, "min" ), PROTO_VERS_MIN );
    tr_bencInitInt( tr_bencDictAdd( dict, "max" ), PROTO_VERS_MAX );
    if( NULL != label )
        tr_bencInitStr( tr_bencDictAdd( dict, "label" ), label, -1, 1 );

    ret = ipc_mkval( &pk, len );
    SAFEBENCFREE( &pk );

    return ret;
}

uint8_t *
ipc_mkgetinfo( struct ipc_info * info, size_t * len, enum ipc_msg id,
               int64_t tag, int types, const int * ids )
{
    benc_val_t   pk, * top, * idlist, * typelist;
    size_t       ii, typecount, used;
    struct inf * typearray;
    uint8_t    * ret;

    /* no ID list, send an -all message */
    if( NULL == ids )
    {
        typelist = ipc_initval( info, id, tag, &pk, TYPE_LIST );
        if( NULL == typelist )
        {
            return NULL;
        }
    }
    else
    {
        top = ipc_initval( info, id, tag, &pk, TYPE_DICT );
        if( NULL == top )
        {
            return NULL;
        }
        /* add the requested IDs */
        if( tr_bencDictReserve( top, 2 ) )
        {
            SAFEBENCFREE( &pk );
            return NULL;
        }
        idlist   = tr_bencDictAdd( top, "id" );
        typelist = tr_bencDictAdd( top, "type" );
        tr_bencInit( idlist, TYPE_LIST );
        tr_bencInit( typelist, TYPE_LIST );
        for( ii = 0; TORRENT_ID_VALID( ids[ii] ); ii++ )
        {
        }
        if( tr_bencListReserve( idlist, ii ) )
        {
            SAFEBENCFREE( &pk );
            return NULL;
        }
        for( ii = 0; TORRENT_ID_VALID( ids[ii] ); ii++ )
        {
            tr_bencInitInt( tr_bencListAdd( idlist ), ids[ii] );
        }
    }

    /* get the type name array */
    switch( id )
    {
        case IPC_MSG_GETINFO:
        case IPC_MSG_GETINFOALL:
            typecount = ARRAYLEN( gl_inf );
            typearray = gl_inf;
            break;
        case IPC_MSG_GETSTAT:
        case IPC_MSG_GETSTATALL:
            typecount = ARRAYLEN( gl_stat );
            typearray = gl_stat;
            break;
        default:
            assert( 0 );
            break;
    }        

    /* add the type names */
    for( ii = used = 0; typecount > ii; ii++ )
    {
        if( types & ( 1 << ii ) )
        {
            used++;
        }
    }
    if( tr_bencListReserve( typelist, used ) )
    {
        SAFEBENCFREE( &pk );
        return NULL;
    }
    for( ii = 0; typecount > ii; ii++ )
    {
        if( !( types & ( 1 << ii ) ) )
        {
            continue;
        }
        assert( typearray[ii].type == ( 1 << ii ) );
        tr_bencInitStr( tr_bencListAdd( typelist ),
                        typearray[ii].name, -1, 1 );
    }

    /* generate packet */
    ret = ipc_mkval( &pk, len );
    SAFEBENCFREE( &pk );

    return ret;
}

int
ipc_addinfo( benc_val_t * list, int tor, const tr_info * inf, int types )
{
    benc_val_t * dict, * item, * file, * tier;
    int          ii, jj, kk;

    /* always send torrent id */
    types |= IPC_INF_ID;

    if( tr_bencListReserve( list, 1 ) )
    {
        return -1;
    }

    dict = tr_bencListAdd( list );
    tr_bencInit( dict, TYPE_DICT );
    for( ii = jj = 0; IPC_INF__MAX > 1 << ii; ii++ )
    {
        if( !( types & ( 1 << ii ) ) )
        {
            continue;
        }
        assert( ARRAYLEN( gl_inf ) > ( unsigned )ii );
        assert( gl_inf[ii].type == ( 1 << ii ) );
        /* check for missing optional info */
        if( ( IPC_INF_COMMENT == ( 1 << ii ) && '\0' == inf->comment ) ||
            ( IPC_INF_CREATOR == ( 1 << ii ) && '\0' == inf->creator ) ||
            ( IPC_INF_DATE    == ( 1 << ii ) &&   0  >= inf->dateCreated ) )
        {
            continue;
        }
        jj++;
    }
    if( tr_bencDictReserve( dict, jj ) )
    {
        return -1;
    }

    for( ii = 0; IPC_INF__MAX > 1 << ii; ii++ )
    {
        if( !( types & ( 1 << ii ) ) )
        {
            continue;
        }
        /* check for missing optional info */
        if( ( IPC_INF_COMMENT == ( 1 << ii ) && '\0' == inf->comment ) ||
            ( IPC_INF_CREATOR == ( 1 << ii ) && '\0' == inf->creator ) ||
            ( IPC_INF_DATE    == ( 1 << ii ) && 0 >= inf->dateCreated ) )
        {
            continue;
        }

        item = tr_bencDictAdd( dict, gl_inf[ii].name );
        switch( 1 << ii )
        {
            case IPC_INF_COMMENT:
                tr_bencInitStr( item, inf->comment, -1, 1 );
                break;
            case IPC_INF_CREATOR:
                tr_bencInitStr( item, inf->creator, -1, 1 );
                break;
            case IPC_INF_DATE:
                tr_bencInitInt( item, inf->dateCreated );
                break;
            case IPC_INF_FILES:
                tr_bencInit( item, TYPE_LIST );
                if( tr_bencListReserve( item, inf->fileCount ) )
                {
                    return -1;
                }
                for( jj = 0; inf->fileCount > jj; jj++ )
                {
                    file = tr_bencListAdd( item );
                    tr_bencInit( file, TYPE_DICT );
                    if( tr_bencDictReserve( file, 2 ) )
                    {
                        return -1;
                    }
                    tr_bencInitStr( tr_bencDictAdd( file, "name" ),
                                    inf->files[jj].name, -1, 1 );
                    tr_bencInitInt( tr_bencDictAdd( file, "size" ),
                                    inf->files[jj].length );
                }
                break;
            case IPC_INF_HASH:
                tr_bencInitStr( item, inf->hashString, -1, 1 );
                break;
            case IPC_INF_ID:
                tr_bencInitInt( item, tor );
                break;
            case IPC_INF_NAME:
                tr_bencInitStr( item, inf->name, -1, 1 );
                break;
            case IPC_INF_PATH:
                tr_bencInitStr( item, inf->torrent, -1, 1 );
                break;
            case IPC_INF_PRIVATE:
                tr_bencInitInt( item, inf->isPrivate ? 1 : 0 );
                break;
            case IPC_INF_SIZE:
                tr_bencInitInt( item, inf->totalSize );
                break;
            case IPC_INF_TRACKERS:
                tr_bencInit( item, TYPE_LIST );
                if( tr_bencListReserve( item, inf->trackerTiers ) )
                {
                    return -1;
                }
                for( jj = 0; inf->trackerTiers > jj; jj++ )
                {
                    tier = tr_bencListAdd( item );
                    tr_bencInit( tier, TYPE_LIST );
                    if( tr_bencListReserve( tier,
                                            inf->trackerList[jj].count ) )
                    {
                        return -1;
                    }
                    for( kk = 0; inf->trackerList[jj].count > kk; kk++ )
                    {
                        if( 0 > filltracker( tr_bencListAdd( tier ),
                                             &inf->trackerList[jj].list[kk] ) )
                        {
                            return -1;
                        }
                    }
                }
                break;
            default:
                assert( 0 );
                break;
        }
    }

    return 0;
}

int
ipc_addstat( benc_val_t * list, int tor,
             const tr_stat * st, int types )
{
    benc_val_t  * dict, * item;
    int           ii, used;
    unsigned int  error;

    /* always send torrent id */
    types |= IPC_ST_ID;

    if( tr_bencListReserve( list, 1 ) )
    {
        return -1;
    }
    dict = tr_bencListAdd( list );

    for( ii = used = 0; IPC_ST__MAX > 1 << ii; ii++ )
    {
        if( types & ( 1 << ii ) )
        {
            used++;
        }
    }
    tr_bencInit( dict, TYPE_DICT );
    if( tr_bencDictReserve( dict, used ) )
    {
        return -1;
    }

    for( ii = 0; IPC_ST__MAX > 1 << ii; ii++ )
    {
        if( !( types & ( 1 << ii ) ) )
        {
            continue;
        }
        assert( ARRAYLEN( gl_stat ) > ( unsigned )ii );
        assert( gl_stat[ii].type == ( 1 << ii ) );
        item = tr_bencDictAdd( dict, gl_stat[ii].name );
        switch( 1 << ii )
        {
            case IPC_ST_COMPLETED:
            case IPC_ST_DOWNVALID:
                tr_bencInitInt( item, st->haveValid );
                break;
            case IPC_ST_DOWNSPEED:
                tr_bencInitInt( item, st->rateDownload * 1024 );
                break;
            case IPC_ST_DOWNTOTAL:
                tr_bencInitInt( item, st->downloadedEver );
                break;
            case IPC_ST_ERROR:
                error = st->error;
                if( TR_OK == error )
                {
                    tr_bencInitStr( item, NULL, 0, 1 );
                }
                else if( TR_ERROR_ISSET( TR_ERROR_ASSERT, error ) )
                {
                    tr_bencInitStr( item, "assert", -1, 1 );
                }
                else if( TR_ERROR_ISSET( TR_ERROR_IO_PERMISSIONS, error ) )
                {
                    tr_bencInitStr( item, "io-permissions", -1, 1 );
                }
                else if( TR_ERROR_ISSET( TR_ERROR_IO_SPACE, error ) )
                {
                    tr_bencInitStr( item, "io-space", -1, 1 );
                }
                else if( TR_ERROR_ISSET( TR_ERROR_IO_FILE_TOO_BIG, error ) )
                {
                    tr_bencInitStr( item, "io-file-too-big", -1, 1 );
                }
                else if( TR_ERROR_ISSET( TR_ERROR_IO_OPEN_FILES, error ) )
                {
                    tr_bencInitStr( item, "io-open-files", -1, 1 );
                }
                else if( TR_ERROR_ISSET( TR_ERROR_IO_MASK, error ) )
                {
                    tr_bencInitStr( item, "io-other", -1, 1 );
                }
                else if( TR_ERROR_ISSET( TR_ERROR_TC_ERROR, error ) )
                {
                    tr_bencInitStr( item, "tracker-error", -1, 1 );
                }
                else if( TR_ERROR_ISSET( TR_ERROR_TC_WARNING, error ) )
                {
                    tr_bencInitStr( item, "tracker-warning", -1, 1 );
                }
                else
                {
                    tr_bencInitStr( item, "other", -1, 1 );
                }
                break;
            case IPC_ST_ERRMSG:
                if( '\0' == st->errorString[0] )
                {
                    tr_bencInitStr( item, NULL, 0, 1 );
                }
                else if( tr_bencInitStrDup( item, st->errorString ) )
                {
                    return -1;
                }
                break;
            case IPC_ST_ETA:
                tr_bencInitInt( item, st->eta );
                break;
            case IPC_ST_ID:
                tr_bencInitInt( item, tor );
                break;
            case IPC_ST_PEERDOWN:
                tr_bencInitInt( item, st->peersSendingToUs );
                break;
            case IPC_ST_PEERFROM:
                tr_bencInit( item, TYPE_DICT );
                if( tr_bencDictReserve( item, 4 ) )
                {
                    return -1;
                }
                tr_bencInitInt( tr_bencDictAdd( item, "incoming" ),
                                st->peersFrom[TR_PEER_FROM_INCOMING] );
                tr_bencInitInt( tr_bencDictAdd( item, "tracker" ),
                                st->peersFrom[TR_PEER_FROM_TRACKER] );
                tr_bencInitInt( tr_bencDictAdd( item, "cache" ),
                                st->peersFrom[TR_PEER_FROM_CACHE] );
                tr_bencInitInt( tr_bencDictAdd( item, "pex" ),
                                st->peersFrom[TR_PEER_FROM_PEX] );
                break;
            case IPC_ST_PEERTOTAL:
                tr_bencInitInt( item, st->peersKnown );
                break;
            case IPC_ST_PEERUP:
                tr_bencInitInt( item, st->peersGettingFromUs );
                break;
            case IPC_ST_RUNNING:
                tr_bencInitInt( item, TR_STATUS_IS_ACTIVE(st->status) );
                break;
            case IPC_ST_STATE:
                if( TR_STATUS_CHECK_WAIT & st->status )
                {
                    tr_bencInitStr( item, "waiting to checking", -1, 1 );
                }
                else if( TR_STATUS_CHECK & st->status )
                {
                    tr_bencInitStr( item, "checking", -1, 1 );
                }
                else if( TR_STATUS_DOWNLOAD & st->status )
                {
                    tr_bencInitStr( item, "downloading", -1, 1 );
                }
                else if( TR_STATUS_SEED & st->status )
                {
                    tr_bencInitStr( item, "seeding", -1, 1 );
                }
                else if( TR_STATUS_STOPPED & st->status )
                {
                    tr_bencInitStr( item, "paused", -1, 1 );
                }
                else
                {
                    assert( 0 );
                }
                break;
            case IPC_ST_SWARM:
                tr_bencInitInt( item, st->swarmspeed * 1024 );
                break;
            case IPC_ST_TRACKER:
                if( 0 > filltracker( item, st->tracker ) )
                {
                    return -1;
                }
                break;
            case IPC_ST_TKDONE:
                tr_bencInitInt( item, st->completedFromTracker );
                break;
            case IPC_ST_TKLEECH:
                tr_bencInitInt( item, st->leechers );
                break;
            case IPC_ST_TKSEED:
                tr_bencInitInt( item, st->seeders );
                break;
            case IPC_ST_UPSPEED:
                tr_bencInitInt( item, st->rateUpload * 1024 );
                break;
            case IPC_ST_UPTOTAL:
                tr_bencInitInt( item, st->uploadedEver );
                break;
            default:
                assert( 0 );
                break;
        }
    }

    return 0;
}

ssize_t
ipc_parse( struct ipc_info * info, uint8_t * buf, ssize_t total, void * arg )
{
    char        hex[IPC_MIN_MSG_LEN+1], * end;
    ssize_t     off, len;
    benc_val_t  benc;

    for( off = 0; off + IPC_MIN_MSG_LEN < total; off += IPC_MIN_MSG_LEN + len )
    {
        memcpy( hex, buf + off, IPC_MIN_MSG_LEN );
        hex[IPC_MIN_MSG_LEN] = '\0';
        end = NULL;
        len = strtol( hex, &end, 16 );
        if( hex + IPC_MIN_MSG_LEN != end ||
            0 > len || IPC_MAX_MSG_LEN < len )
        {
            errno = EINVAL;
            return -1;
        }
        if( off + IPC_MIN_MSG_LEN + len > total )
        {
            break;
        }
        errno = 0;
        if( tr_bencLoad( buf + off + IPC_MIN_MSG_LEN, len, &benc, NULL ) )
        {
            if( 0 == errno )
            {
                errno = EINVAL;
            }
            return -1;
        }
        if( 0 > ( HASVERS( info ) ? handlemsgs( info, &benc, arg ) :
                                    handlevers( info, &benc ) ) )
        {
            SAFEBENCFREE( &benc );
            return -1;
        }
        tr_bencFree( &benc );
    }

    return off;
}

static int
handlevers( struct ipc_info * info, benc_val_t * dict )
{
    benc_val_t * vers, * num;
    int64_t      min, max;

    if( TYPE_DICT != dict->type )
    {
        errno = EINVAL;
        return -1;
    }

    vers = tr_bencDictFind( dict, MSGNAME( IPC_MSG_VERSION ) );
    if( NULL == vers )
    {
        errno = EINVAL;
        return -1;
    }

    switch( vers->type )
    {
        case TYPE_INT:
            min = vers->val.i;
            max = vers->val.i;
            break;
        case TYPE_DICT:
            num = tr_bencDictFind( vers, "min" );
            min = ( NULL == num || TYPE_INT != num->type ? -1 : num->val.i );
            num = tr_bencDictFind( vers, "max" );
            max = ( NULL == num || TYPE_INT != num->type ? -1 : num->val.i );
            break;
        default:
            min = -1;
            max = -1;
            break;
    }

    if( 0 >= min || 0 >= max || INT_MAX < min || INT_MAX < max )
    {
        errno = EINVAL;
        return -1;
    }

    assert( PROTO_VERS_MIN <= PROTO_VERS_MAX );
    if( min > max )
    {
        errno = EINVAL;
        return -1;
    }
    if( PROTO_VERS_MAX < min || PROTO_VERS_MIN > max )
    {
        errno = EPERM;
        return -1;
    }

    info->vers = MIN( PROTO_VERS_MAX, max );

    return 0;
}

static int
handlemsgs( struct ipc_info * info, benc_val_t * pay, void * arg )
{
    benc_val_t * name, * val, * tag;
    int          ii;

    assert( HASVERS( info ) );

    if( DICTPAYLOAD( info ) )
    {
        if( TYPE_DICT != pay->type || pay->val.l.count % 2 )
        {
            errno = EINVAL;
            return -1;
        }

        for( ii = 0; ii < pay->val.l.count; ii += 2 )
        {
            assert( ii + 1 < pay->val.l.count );
            name = &pay->val.l.vals[ii];
            val  = &pay->val.l.vals[ii+1];
            if( 0 > gotmsg( info, name, val, NULL, arg ) )
            {
                return -1;
            }
        }
    }
    else
    {
        if( TYPE_LIST != pay->type || 2 > pay->val.l.count )
        {
            errno = EINVAL;
            return -1;
        }

        name = &pay->val.l.vals[0];
        val  = &pay->val.l.vals[1];
        tag  = ( 2 == pay->val.l.count ? NULL : &pay->val.l.vals[2] );
        if( 0 > gotmsg( info, name, val, tag, arg ) )
        {
            return -1;
        }
    }

    return 0;
}

static int
gotmsg( struct ipc_info * info, benc_val_t * name, benc_val_t * val,
        benc_val_t * tagval, void * arg )
{
    struct msgfunc key, * handler;
    struct msg   * msg;
    int64_t        tag;

    if( TYPE_STR != name->type )
    {
        errno = EINVAL;
        return -1;
    }

    if( NULL == tagval )
    {
        tag = -1;
    }
    else
    {
        if( TYPE_INT != tagval->type )
        {
            errno = EINVAL;
            return -1;
        }
        tag = tagval->val.i;
    }

    msg = msglookup( name->val.s.s );
    if( NULL != msg && msg->minvers <= info->vers )
    {
        memset( &key, 0, sizeof key );
        key.id  = msg->id;
        handler = RB_FIND( functree, &info->funcs->msgs, &key );
        if( NULL != handler )
        {
            handler->func( msg->id, val, tag, arg );
        }
        else if( NULL != info->funcs->def )
        {
            info->funcs->def( msg->id, val, tag, arg );
        }
    }
    else if( NULL != info->funcs->def )
        info->funcs->def( IPC__MSG_UNKNOWN, NULL, tag, arg );

    return 0;
}

int
ipc_havemsg( struct ipc_info * info, enum ipc_msg id )
{
    assert( MSGVALID( id ) );
    assert( HASVERS( info ) );

    return ( gl_msgs[id].minvers <= info->vers );
}

enum ipc_msg
ipc_msgid( struct ipc_info * info, const char * name )
{
    struct msg * msg;

    msg = msglookup( name );
    if( NULL == msg || !ipc_havemsg( info, msg->id ) )
    {
        return IPC__MSG_COUNT;
    }

    return msg->id;
}

int
ipc_ishandled( struct ipc_info * info, enum ipc_msg id )
{
    struct msgfunc key;

    assert( MSGVALID( id ) );

    memset( &key, 0, sizeof key );
    key.id = id;
    return ( NULL != RB_FIND( functree, &info->funcs->msgs, &key ) );
}

int
ipc_havetags( struct ipc_info * info )
{
    return !DICTPAYLOAD( info );
}

int
ipc_infotypes( enum ipc_msg id, benc_val_t * list )
{
    static struct inftree infotree = RB_INITIALIZER( &tree );
    static struct inftree stattree = RB_INITIALIZER( &tree );
    struct inftree * tree;
    benc_val_t     * name;
    struct inf     * array, * inf, key;
    size_t           len, ii;
    int              ret, jj;

    switch( id )
    {
        case IPC_MSG_INFO:
            tree  = &infotree;
            array = gl_inf;
            len   = ARRAYLEN( gl_inf );
            break;
        case IPC_MSG_STAT:
            tree  = &stattree;
            array = gl_stat;
            len   = ARRAYLEN( gl_stat );
            break;
        default:
            assert( 0 );
            break;
    }

    if( RB_EMPTY( tree ) )
    {
        for( ii = 0; len > ii; ii++ )
        {
            assert( 1 << ii == array[ii].type );
            inf = RB_INSERT( inftree, tree, &array[ii] );
            assert( NULL == inf );
        }
    }

    ret = IPC_INF_ID;

    if( NULL == list || TYPE_LIST != list->type )
    {
        return ret;
    }

    memset( &key, 0, sizeof key );
    for( jj = 0; list->val.l.count > jj; jj++ )
    {
        name = &list->val.l.vals[jj];
        if( TYPE_STR != name->type )
        {
            continue;
        }
        key.name = name->val.s.s;
        inf = RB_FIND( inftree, tree, &key );
        if( NULL != inf )
        {
            ret |= inf->type;
        }
    }

    return ret;
}

const char *
ipc_infoname( enum ipc_msg id, int type )
{
    struct inf * array;
    size_t len, ii;

    switch( id )
    {
        case IPC_MSG_INFO:
            array = gl_inf;
            len   = ARRAYLEN( gl_inf );
            break;
        case IPC_MSG_STAT:
            array = gl_stat;
            len   = ARRAYLEN( gl_stat );
            break;
        default:
            assert( 0 );
            break;
    }

    for( ii = 0; len > ii; ii++ )
    {
        if( array[ii].type == type )
        {
            return array[ii].name;
        }
    }

    assert( 0 );

    return NULL;
}

static int
msgcmp( struct msg * first, struct msg * second )
{
    return strcmp( first->name, second->name );
}

static int
infcmp( struct inf * first, struct inf * second )
{
    return strcmp( first->name, second->name );
}

static struct msg *
msglookup( const char * name )
{
    static struct msgtree tree = RB_INITIALIZER( &tree );
    struct msg          * ret, key;
    size_t                ii;

    assert( IPC__MSG_COUNT == ARRAYLEN( gl_msgs ) );

    if( RB_EMPTY( &tree ) )
    {
        for( ii = 0; ARRAYLEN( gl_msgs ) > ii; ii++ )
        {
            assert( ii == gl_msgs[ii].id );
            ret = RB_INSERT( msgtree, &tree, &gl_msgs[ii] );
            assert( NULL == ret );
        }
    }

    memset( &key, 0, sizeof key );
    key.name = name;
    return RB_FIND( msgtree, &tree, &key );
}

static int
filltracker( benc_val_t * val, const tr_tracker_info * tk )
{
    tr_bencInit( val, TYPE_DICT );
    if( tr_bencDictReserve( val, ( NULL == tk->scrape ? 3 : 4 ) ) )
    {
        return -1;
    }

    tr_bencInitStr( tr_bencDictAdd( val, "address" ),  tk->address,  -1, 1 );
    tr_bencInitInt( tr_bencDictAdd( val, "port" ),     tk->port );
    tr_bencInitStr( tr_bencDictAdd( val, "announce" ), tk->announce, -1, 1 );
    if( NULL != tk->scrape )
    {
        tr_bencInitStr( tr_bencDictAdd( val, "scrape" ), tk->scrape, -1, 1 );
    }

    return 0;
}
