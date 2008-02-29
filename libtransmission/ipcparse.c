/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2007-2008 Joshua Elsasser
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

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "transmission.h"
#include "bencode.h"
#include "utils.h"

#include "ipcparse.h"

#define TR_N_ELEMENTS( ary ) ( sizeof( ary ) / sizeof( (ary)[0] ) )

#define SAFEFREE( ptr )      \
    do                       \
    {                        \
        int saved = errno;   \
        free( ptr );         \
        errno = saved;       \
    }                        \
    while( 0 )

#define SAFEBENCFREE( val )  \
    do                       \
    {                        \
        int saved = errno;   \
        tr_bencFree( val );  \
        errno = saved;       \
    }                        \
    while( 0 )

/* IPC protocol version */
#define PROTO_VERS_MIN          ( 1 )
#define PROTO_VERS_MAX          ( 2 )

#define MSGVALID( id )          ( IPC__MSG_COUNT > (id) )
#define MSGNAME( id )           ( gl_msgs[(id)].name )
#define DICTPAYLOAD( info )     ( 2 > (info)->vers )

struct ipc_info
{
    struct ipc_funcs * funcs;
    int                vers;
};

int
ipc_hasvers( const struct ipc_info * inf )
{
    return inf && ( inf->vers > 0 );
}

struct msg
{
    const char        * name;
    const int           minvers;
    const enum ipc_msg  id;
};

struct inf
{
    const char    * name;
    const int       type;
};

struct msgfunc
{
    int             id;
    trd_msgfunc     func;
};

struct ipc_funcs
{
    trd_msgfunc msgs[IPC__MSG_COUNT];
    trd_msgfunc def;
};

/* these names must be sorted for strcmp() */
static const struct msg gl_msgs[] =
{
    { "addfile-detailed",    2, IPC_MSG_ADDONEFILE    },
    { "addfiles",            1, IPC_MSG_ADDMANYFILES  },
    { "automap",             2, IPC_MSG_AUTOMAP       },
    { "autostart",           2, IPC_MSG_AUTOSTART     },
    { "bad-format",          2, IPC_MSG_BAD           },
    { "directory",           2, IPC_MSG_DIR           },
    { "downlimit",           2, IPC_MSG_DOWNLIMIT     },
    { "encryption",          2, IPC_MSG_CRYPTO        },
    { "failed",              2, IPC_MSG_FAIL          },
    { "get-automap",         2, IPC_MSG_GETAUTOMAP    },
    { "get-autostart",       2, IPC_MSG_GETAUTOSTART  },
    { "get-directory",       2, IPC_MSG_GETDIR        },
    { "get-downlimit",       2, IPC_MSG_GETDOWNLIMIT  },
    { "get-encryption",      2, IPC_MSG_GETCRYPTO     },
    { "get-info",            2, IPC_MSG_GETINFO       },
    { "get-info-all",        2, IPC_MSG_GETINFOALL    },
    { "get-pex",             2, IPC_MSG_GETPEX        },
    { "get-port",            2, IPC_MSG_GETPORT       },
    { "get-status",          2, IPC_MSG_GETSTAT       },
    { "get-status-all",      2, IPC_MSG_GETSTATALL    },
    { "get-supported",       2, IPC_MSG_GETSUP        },
    { "get-uplimit",         2, IPC_MSG_GETUPLIMIT    },
    { "info",                2, IPC_MSG_INFO          },
    { "lookup",              2, IPC_MSG_LOOKUP        },
    { "noop",                2, IPC_MSG_NOOP          },
    { "not-supported",       2, IPC_MSG_NOTSUP        },
    { "pex",                 2, IPC_MSG_PEX           },
    { "port",                2, IPC_MSG_PORT          },
    { "quit",                1, IPC_MSG_QUIT          },
    { "remove",              2, IPC_MSG_REMOVE        },
    { "remove-all",          2, IPC_MSG_REMOVEALL     },
    { "start",               2, IPC_MSG_START         },
    { "start-all",           2, IPC_MSG_STARTALL      },
    { "status",              2, IPC_MSG_STAT          },
    { "stop",                2, IPC_MSG_STOP          },
    { "stop-all",            2, IPC_MSG_STOPALL       },
    { "succeeded",           2, IPC_MSG_OK            },
    { "supported",           2, IPC_MSG_SUP           },
    { "uplimit",             2, IPC_MSG_UPLIMIT       },
    { "verify",              2, IPC_MSG_VERIFY        },
    { "version",             1, IPC_MSG_VERSION       }
};

/* these names must be sorted for strcmp() */
static const struct inf gl_inf[] =
{
    { "comment",                IPC_INF_COMMENT   },
    { "creator",                IPC_INF_CREATOR   },
    { "date",                   IPC_INF_DATE      },
    { "files",                  IPC_INF_FILES     },
    { "hash",                   IPC_INF_HASH      },
    { "id",                     IPC_INF_ID        },
    { "name",                   IPC_INF_NAME      },
    { "path",                   IPC_INF_PATH      },
    { "private",                IPC_INF_PRIVATE   },
    { "size",                   IPC_INF_SIZE      },
    { "trackers",               IPC_INF_TRACKERS  }
};

/* these names must be sorted for strcmp() */
static const struct inf gl_stat[] =
{
    { "completed",              IPC_ST_COMPLETED },
    { "download-speed",         IPC_ST_DOWNSPEED },
    { "download-total",         IPC_ST_DOWNTOTAL },
    { "download-valid",         IPC_ST_DOWNVALID },
    { "error",                  IPC_ST_ERROR     },
    { "error-message",          IPC_ST_ERRMSG    },
    { "eta",                    IPC_ST_ETA       },
    { "id",                     IPC_ST_ID        },
    { "peers-downloading",      IPC_ST_PEERDOWN  },
    { "peers-from",             IPC_ST_PEERFROM  },
    { "peers-total",            IPC_ST_PEERTOTAL },
    { "peers-uploading",        IPC_ST_PEERUP    },
    { "running",                IPC_ST_RUNNING   },
    { "scrape-completed",       IPC_ST_TKDONE    },
    { "scrape-leechers",        IPC_ST_TKLEECH   },
    { "scrape-seeders",         IPC_ST_TKSEED    },
    { "state",                  IPC_ST_STATE     },
    { "swarm-speed",            IPC_ST_SWARM     },
    { "tracker",                IPC_ST_TRACKER   },
    { "upload-speed",           IPC_ST_UPSPEED   },
    { "upload-total",           IPC_ST_UPTOTAL   }
};

struct ipc_funcs *
ipc_initmsgs( void )
{
    return tr_new0( struct ipc_funcs, 1 );
}

void
ipc_addmsg( struct ipc_funcs * tree, enum ipc_msg id, trd_msgfunc func )
{
    assert( MSGVALID( id ) );
    assert( IPC_MSG_VERSION != id );

    tree->msgs[id] = func;
}

void
ipc_setdefmsg( struct ipc_funcs * tree, trd_msgfunc func )
{
    tree->def = func;
}

void
ipc_freemsgs( struct ipc_funcs * tree )
{
    tr_free( tree );
}

struct ipc_info *
ipc_newcon( struct ipc_funcs * funcs )
{
    struct ipc_info * info = tr_new0( struct ipc_info, 1 );
    info->funcs = funcs;
    info->vers  = -1;
    return info;
}

void
ipc_freecon( struct ipc_info * info )
{
    tr_free( info );
}

static int
ipc_havemsg( const struct ipc_info * info, enum ipc_msg id )
{
    assert( MSGVALID( id ) );
    assert( ipc_hasvers( info ) );

    return gl_msgs[id].minvers <= info->vers;
}

tr_benc *
ipc_initval( const struct ipc_info * info, enum ipc_msg id, int64_t tag,
             tr_benc * pk, int type )
{
    tr_benc * ret;

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
ipc_mkval( const tr_benc * pk, size_t * setmeSize )
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
ipc_mkempty( const struct ipc_info * info, size_t * len, enum ipc_msg id,
             int64_t tag )
{
    tr_benc pk;
    uint8_t  * ret = NULL;

    if( ipc_initval( info, id, tag, &pk, TYPE_STR ) )
    {
        ret = ipc_mkval( &pk, len );
        SAFEBENCFREE( &pk );
    }

    return ret;
}

uint8_t *
ipc_mkint( const struct ipc_info * info, size_t * len, enum ipc_msg id,
           int64_t tag, int64_t num )
{
    tr_benc pk, * val;
    uint8_t  * ret = NULL;

    if(( val = ipc_initval( info, id, tag, &pk, TYPE_INT )))
    {
        val->val.i = num;
        ret = ipc_mkval( &pk, len );
        SAFEBENCFREE( &pk );
    }

    return ret;
}

uint8_t *
ipc_mkstr( const struct ipc_info * info, size_t * len, enum ipc_msg id,
           int64_t tag, const char * str )
{
    tr_benc pk, * val;
    uint8_t  * ret = NULL;

    if(( val = ipc_initval( info, id, tag, &pk, TYPE_STR )))
    {
        tr_bencInitStr( val, str, -1, 1 );
        ret = ipc_mkval( &pk, len );
        SAFEBENCFREE( &pk );
    }

    return ret;
}

uint8_t *
ipc_mkvers( size_t * len, const char * label )
{
    tr_benc pk, * dict;
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
ipc_mkgetinfo( const struct ipc_info * info, size_t * len, enum ipc_msg id,
               int64_t tag, int types, const int * ids )
{
    tr_benc   pk, * top, * idlist, * typelist;
    size_t       ii, typecount, used;
    const struct inf * typearray;
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
            typecount = TR_N_ELEMENTS( gl_inf );
            typearray = gl_inf;
            break;
        case IPC_MSG_GETSTAT:
        case IPC_MSG_GETSTATALL:
            typecount = TR_N_ELEMENTS( gl_stat );
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

static int
filltracker( tr_benc * val, const tr_tracker_info * tk )
{
    tr_bencInit( val, TYPE_DICT );
    if( tr_bencDictReserve( val, ( NULL == tk->scrape ? 3 : 4 ) ) )
        return -1;

    tr_bencInitStr( tr_bencDictAdd( val, "address" ),  tk->address,  -1, 1 );
    tr_bencInitInt( tr_bencDictAdd( val, "port" ),     tk->port );
    tr_bencInitStr( tr_bencDictAdd( val, "announce" ), tk->announce, -1, 1 );
    if( NULL != tk->scrape )
        tr_bencInitStr( tr_bencDictAdd( val, "scrape" ), tk->scrape, -1, 1 );

    return 0;
}

int
ipc_addinfo( tr_benc * list, int tor, const tr_info * inf, int types )
{
    tr_benc * dict, * item, * file, * tier;
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
        assert( TR_N_ELEMENTS( gl_inf ) > ( unsigned )ii );
        assert( gl_inf[ii].type == ( 1 << ii ) );
        /* check for missing optional info */
        if( ( IPC_INF_COMMENT == ( 1 << ii ) && !*inf->comment ) ||
            ( IPC_INF_CREATOR == ( 1 << ii ) && !*inf->creator ) ||
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
        if( ( IPC_INF_COMMENT == ( 1 << ii ) && !*inf->comment ) ||
            ( IPC_INF_CREATOR == ( 1 << ii ) && !*inf->creator ) ||
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
ipc_addstat( tr_benc * list, int tor,
             const tr_stat * st, int types )
{
    tr_benc  * dict, * item;
    int           ii, used;
    tr_errno      error;

    /* always send torrent id */
    types |= IPC_ST_ID;

    if( tr_bencListReserve( list, 1 ) )
        return -1;

    dict = tr_bencListAdd( list );

    for( ii = used = 0; IPC_ST__MAX > 1 << ii; ii++ )
        if( types & ( 1 << ii ) )
            used++;

    tr_bencInit( dict, TYPE_DICT );
    if( tr_bencDictReserve( dict, used ) )
        return -1;

    for( ii = 0; IPC_ST__MAX > 1 << ii; ii++ )
    {
        if( !( types & ( 1 << ii ) ) )
            continue;

        assert( TR_N_ELEMENTS( gl_stat ) > ( unsigned )ii );
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
                    tr_bencInitStr( item, "", -1, 1 );
                }
                else if( error == TR_ERROR_ASSERT )
                {
                    tr_bencInitStr( item, "assert", -1, 1 );
                }
                else if( error == TR_ERROR_IO_PERMISSIONS )
                {
                    tr_bencInitStr( item, "io-permissions", -1, 1 );
                }
                else if( error == TR_ERROR_IO_SPACE )
                {
                    tr_bencInitStr( item, "io-space", -1, 1 );
                }
                else if( error == TR_ERROR_IO_FILE_TOO_BIG )
                {
                    tr_bencInitStr( item, "io-file-too-big", -1, 1 );
                }
                else if( error == TR_ERROR_IO_OPEN_FILES )
                {
                    tr_bencInitStr( item, "io-open-files", -1, 1 );
                }
                else if( TR_ERROR_IS_IO( error ) )
                {
                    tr_bencInitStr( item, "io-other", -1, 1 );
                }
                else if( error == TR_ERROR_TC_ERROR )
                {
                    tr_bencInitStr( item, "tracker-error", -1, 1 );
                }
                else if( error == TR_ERROR_TC_WARNING )
                {
                    tr_bencInitStr( item, "tracker-warning", -1, 1 );
                }
                else if( TR_ERROR_IS_TC( error ) )
                {
                    tr_bencInitStr( item, "tracker-other", -1, 1 );
                }
                else
                {
                    tr_bencInitStr( item, "other", -1, 1 );
                }
                break;
            case IPC_ST_ERRMSG:
                if( TR_OK == st->error )
                {
                    tr_bencInitStr( item, "", -1, 1 );
                }
                else if( '\0' == st->errorString[0] )
                {
                    tr_bencInitStr( item, "other", -1, 1 );
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
                tr_bencInitInt( item, st->peersConnected );
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
                else if( ( TR_STATUS_DONE | TR_STATUS_SEED ) & st->status )
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

static int
handlevers( struct ipc_info * info, tr_benc * dict )
{
    tr_benc * vers, * num;
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
compareNameToMsg( const void * a, const void * b )
{
    const struct msg * msg = b;
    return strcmp( a, msg->name );
}

static const struct msg *
msglookup( const char * name )
{
    return bsearch( name,
                    gl_msgs, TR_N_ELEMENTS( gl_msgs ), sizeof( struct msg ),
                    compareNameToMsg );
}

static int
gotmsg( const struct ipc_info * info, tr_benc * name, tr_benc * val,
        tr_benc * tagval, void * arg )
{
    const struct msg * msg;
    int64_t            tag;

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
    if( msg && msg->minvers <= info->vers )
    {
        if( info->funcs->msgs[msg->id] != NULL )
        {
            (*info->funcs->msgs[msg->id])( msg->id, val, tag, arg );
        }
        else if( info->funcs->def )
        {
            info->funcs->def( msg->id, val, tag, arg );
        }
    }
    else if( NULL != info->funcs->def )
        info->funcs->def( IPC__MSG_UNKNOWN, NULL, tag, arg );

    return 0;
}

static int
handlemsgs( const struct ipc_info * info, tr_benc * pay, void * arg )
{
    tr_benc * name, * val, * tag;
    int          ii;

    assert( ipc_hasvers( info ) );

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

ssize_t
ipc_parse( struct ipc_info * info, const uint8_t * buf, ssize_t total, void * arg )
{
    char        hex[IPC_MIN_MSG_LEN+1], * end;
    ssize_t     off, len;
    tr_benc  benc;

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
        if( 0 > ( ipc_hasvers( info ) ? handlemsgs( info, &benc, arg ) :
                                        handlevers( info, &benc ) ) )
        {
            SAFEBENCFREE( &benc );
            return -1;
        }
        tr_bencFree( &benc );
    }

    return off;
}

enum ipc_msg
ipc_msgid( const struct ipc_info * info, const char * name )
{
    const struct msg * msg = msglookup( name );

    return msg && ipc_havemsg( info, msg->id )
        ? msg->id
        : IPC__MSG_COUNT;
}

int
ipc_ishandled( const struct ipc_info * info, enum ipc_msg id )
{
    assert( MSGVALID( id ) );

    return info->funcs->msgs[id] != NULL;
}

int
ipc_havetags( const struct ipc_info * info )
{
    return !DICTPAYLOAD( info );
}

static int
compareNameToInf( const void * a, const void * b )
{
    const struct inf * inf = b;
    return strcmp( a, inf->name );
}

int
ipc_infotypes( enum ipc_msg id, const tr_benc * list )
{
    const struct inf     * array;
    size_t                 len;
    int                    i;
    int                    ret;

    switch( id )
    {
        case IPC_MSG_INFO:
            array = gl_inf;
            len   = TR_N_ELEMENTS( gl_inf );
            break;
        case IPC_MSG_STAT:
            array = gl_stat;
            len   = TR_N_ELEMENTS( gl_stat );
            break;
        default:
            assert( 0 );
            break;
    }

    ret = IPC_INF_ID;

    if( NULL == list || TYPE_LIST != list->type )
    {
        return ret;
    }

    for( i=0; i<list->val.l.count; ++i )
    {
        const tr_benc * name = &list->val.l.vals[i];
        const struct inf * inf;

        if( TYPE_STR != name->type )
            continue;

        inf = bsearch( name->val.s.s,
                       array, len, sizeof( struct inf ),
                       compareNameToInf );
        if( inf )
            ret |= inf->type;
    }

    return ret;
}

const char *
ipc_infoname( enum ipc_msg id, int type )
{
    const struct inf * array;
    size_t len, ii;

    switch( id )
    {
        case IPC_MSG_INFO:
            array = gl_inf;
            len   = TR_N_ELEMENTS( gl_inf );
            break;
        case IPC_MSG_STAT:
            array = gl_stat;
            len   = TR_N_ELEMENTS( gl_stat );
            break;
        default:
            assert( 0 );
            break;
    }

    for( ii = 0; len > ii; ii++ )
        if( array[ii].type == type )
            return array[ii].name;

    assert( 0 );
    return NULL;
}
