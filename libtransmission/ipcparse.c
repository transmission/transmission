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
#include "torrent.h"
#include "utils.h"

#include "ipcparse.h"

#define TR_N_ELEMENTS( ary ) ( sizeof( ary ) / sizeof( (ary)[0] ) )

#define SAFEFREE( ptr )            \
    do                             \
    {                              \
        const int saved = errno;   \
        free( ptr );               \
        errno = saved;             \
    }                              \
    while( 0 )

#define SAFEBENCFREE( val )        \
    do                             \
    {                              \
        int saved = errno;         \
        tr_bencFree( val );        \
        errno = saved;             \
    }                              \
    while( 0 )

/* IPC protocol version */
#define PROTO_VERS_MIN          ( 1 )
#define PROTO_VERS_MAX          ( 2 )

#define MSGVALID( id )          ( IPC__MSG_COUNT > (id) )
#define MSGNAME( id )           ( gl_msgs[(id)].name )
#define DICTPAYLOAD( info )     ( 2 > (info)->vers )

struct ipc_funcs
{
    trd_msgfunc msgs[IPC__MSG_COUNT];
    trd_msgfunc def;
};

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
    { "peer-max",            2, IPC_MSG_PEERMAX       },
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

struct inf
{
    const char    * name;
    const int       type;
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
    { "peers-max",              IPC_ST_PEERMAX   },
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

#define ASSERT_SORTED(A) \
  do { \
    int i, n; \
    for( i=0, n=TR_N_ELEMENTS(A)-1; i<n; ++i ) \
      assert( strcmp( A[i].name, A[i+1].name ) < 0 ); \
  } while( 0 );

struct ipc_funcs *
ipc_initmsgs( void )
{
    ASSERT_SORTED( gl_msgs );
    ASSERT_SORTED( gl_inf );
    ASSERT_SORTED( gl_stat );

    return tr_new0( struct ipc_funcs, 1 );
}

void
ipc_addmsg( struct ipc_funcs  * funcs,
            enum ipc_msg        msg_id,
            trd_msgfunc         func )
{
    assert( MSGVALID( msg_id ) );
    assert( IPC_MSG_VERSION != msg_id );

    funcs->msgs[msg_id] = func;
}

void
ipc_setdefmsg( struct ipc_funcs * funcs, trd_msgfunc func )
{
    funcs->def = func;
}

void
ipc_freemsgs( struct ipc_funcs * funcs )
{
    tr_free( funcs );
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
ipc_freecon( struct ipc_info * session )
{
    tr_free( session );
}

int
ipc_ishandled( const struct ipc_info * session, enum ipc_msg msg_id )
{
    assert( MSGVALID( msg_id ) );

    return session->funcs->msgs[msg_id] != NULL;
}

int
ipc_havetags( const struct ipc_info * session )
{
    return !DICTPAYLOAD( session );
}

static int
sessionSupportsTags( const struct ipc_info * session )
{
    return session->vers >= 2;
}

static int
sessionSupportsMessage( const struct ipc_info * session, enum ipc_msg id )
{
    assert( MSGVALID( id ) );
    assert( ipc_hasvers( session ) );

    return gl_msgs[id].minvers <= session->vers;
}

/**
 * Creates the benc metainfo structure for a message
 * and returns its child where payload should be set.
 *
 * In protocol version 1, the metainfo is a single-entry
 * dictionary with a string from gl_msgs as the key
 * and the return tr_benc pointer as the value.
 *
 * In protocol version 2, the metainfo is a list
 * holding a string from gl_msgs, the return benc pointer,
 * and (optionally) the integer tag.
 */
tr_benc *
ipc_initval( const struct ipc_info * session,
             enum ipc_msg            msg_id,
             int64_t                 tag,
             tr_benc               * pk,
             int                     benc_type )
{
    tr_benc * ret;

    assert( MSGVALID( msg_id ) );

    if( !sessionSupportsMessage( session, msg_id )
        || ( (tag>0) && !sessionSupportsTags( session ) ) )
    {
        errno = EPERM;
        return NULL;
    }

    if( DICTPAYLOAD( session ) )
    {
        tr_bencInitDict( pk, 1 );
        ret = tr_bencDictAdd( pk, MSGNAME( msg_id ) );
    }
    else
    {
        tr_bencInitList( pk, 3 );
        tr_bencListAddStr( pk, MSGNAME( msg_id ) );
        ret = tr_bencListAdd( pk );
        if( 0 < tag )
            tr_bencListAddInt( pk, tag );
    }

    tr_bencInit( ret, benc_type );
    return ret;
}

/**
 * Serialize a benc message into a string appended to a
 * printf()'ed string IPC_MIN_MSG_LEN bytes long that
 * gives the length of the string.
 */
uint8_t *
ipc_serialize( const tr_benc * benc, size_t * setmeSize )
{
    uint8_t * ret = NULL;
    int len = 0;
    char * str = tr_bencSave( benc, &len );

    if( len > IPC_MAX_MSG_LEN )
        errno = EFBIG;
    else {
        const size_t size = IPC_MIN_MSG_LEN + len;
        ret = tr_new( uint8_t, size );
        snprintf( (char*)ret, size, "%0*X", IPC_MIN_MSG_LEN, len );
        memcpy( ret + IPC_MIN_MSG_LEN, str, len );
        *setmeSize = size;
    }

    tr_free( str );
    return ret;
}

/**
 * Create a serialized message whose payload is a NULL string
 */
uint8_t *
ipc_mkempty( const struct ipc_info * session,
             size_t                * setmeSize,
             enum ipc_msg            msg_id,
             int64_t                 tag )
{
    return ipc_mkstr( session, setmeSize, msg_id, tag, NULL );
}

/**
 * Create a serialized message whose payload is an integer
 */
uint8_t *
ipc_mkint( const struct ipc_info  * session,
           size_t                 * setmeSize,
           enum ipc_msg             msg_id,
           int64_t                  tag,
           int64_t                  num )
{
    tr_benc pk, * val;
    uint8_t  * ret = NULL;

    if(( val = ipc_initval( session, msg_id, tag, &pk, TYPE_INT )))
    {
        tr_bencInitInt( val, num );
        ret = ipc_serialize( &pk, setmeSize );
        SAFEBENCFREE( &pk );
    }

    return ret;
}

/**
 * Create a serialized message whose payload is a string
 */
uint8_t *
ipc_mkstr( const struct ipc_info  * session,
           size_t                 * setmeSize,
           enum ipc_msg             msg_id,
           int64_t                  tag,
           const char             * str )
{
    tr_benc pk, * val;
    uint8_t  * ret = NULL;

    if(( val = ipc_initval( session, msg_id, tag, &pk, TYPE_STR )))
    {
        tr_bencInitStrDup( val, str );
        ret = ipc_serialize( &pk, setmeSize );
        SAFEBENCFREE( &pk );
    }

    return ret;
}

/**
 * Create a serialized message whose payload is a dictionary
 * giving the minimum and maximum protocol version we support,
 * and (optionally) the label passed in.
 *
 * Note that this message is just the dictionary payload.
 * It doesn't contain metainfo as the other ipc_mk*() functions do.
 * That's because the metainfo is dependent on the protocol version,
 * and this is a handshake message to negotiate protocol versions.
 *
 * @see handlevers()
 */
uint8_t *
ipc_mkvers( size_t * len, const char * label )
{
    tr_benc pk, * dict;
    uint8_t  * ret;
  
    tr_bencInitDict( &pk, 1 );
    dict = tr_bencDictAddDict( &pk, MSGNAME( IPC_MSG_VERSION ), 3 );
    tr_bencDictAddInt( dict, "min", PROTO_VERS_MIN );
    tr_bencDictAddInt( dict, "max", PROTO_VERS_MAX );
    if( label )
        tr_bencDictAddStr( dict, "label", label );

    ret = ipc_serialize( &pk, len );
    SAFEBENCFREE( &pk );

    return ret;
}

/**
 * Create a serialized message that is used to request
 * torrent information or statistics.
 *
 * msg_id must be one of:
 *   IPC_MSG_GETINFO
 *   IPC_MSG_GETINFOALL
 *   IPC_MSG_GETSTAT
 *   IPC_MSG_GETSTATALL
 *
 * "ids" is an optional array of torrent IDs.
 * The array, if included, must be terminated by a 0 torrent id.
 *
 * "types" is a bitwise-and'ed set of fields from either
 * the IPC_INF_* or IPC_ST_* enums in ipc-parse.h.
 * Which enums are used is dependent on the value of msg_id.
 *
 * If torrent ids are specified in the "ids" array,
 * the payload is a dictionary of two lists, "id" and "type".
 * The "id" list holds the torrent IDs, and
 * the "type" list holds string keys from either
 * gl_inf or gl_stat, depending on the value of msg_id
 *
 * If no torrent ids are specified, the payload is
 * a single list identical to the "type" list described above.
 */
uint8_t *
ipc_createInfoRequest( const struct ipc_info * session,
                       size_t                * setmeSize,
                       enum ipc_msg            msg_id,
                       int64_t                 tag,
                       int                     types,
                       const int             * ids )
{
    tr_benc   pk;
    tr_benc * typelist;
    size_t       i, typecount;
    const struct inf * typearray;
    uint8_t    * ret;

    /* no ID list, send an -all message */
    if( !ids ) {
        typelist = ipc_initval( session, msg_id, tag, &pk, TYPE_LIST );
        if( !typelist )
            return NULL;
    }
    else
    {
        tr_benc * top;
        tr_benc * idlist;

        top = ipc_initval( session, msg_id, tag, &pk, TYPE_DICT );
        if( !top )
            return NULL;

        /* add the requested IDs */
        tr_bencDictReserve( top, 2 );
        typelist = tr_bencDictAdd( top, "type" );
        tr_bencInit( typelist, TYPE_LIST );
        for( i = 0; TORRENT_ID_VALID( ids[i] ); i++ ) { }
        idlist = tr_bencDictAddList( top, "id", i );
        for( i = 0; TORRENT_ID_VALID( ids[i] ); i++ )
            tr_bencListAddInt( idlist, ids[i] );
    }

    /* get the type name array */
    switch( msg_id )
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

    tr_bencListReserve( typelist, typecount );
    for( i=0; i<typecount; ++i )
        if( types & typearray[i].type )
            tr_bencListAddStr( typelist, typearray[i].name );

    /* generate packet */
    ret = ipc_serialize( &pk, setmeSize );
    SAFEBENCFREE( &pk );

    return ret;
}

static void
filltracker( tr_benc * val, const tr_tracker_info * tk )
{
    tr_bencInitDict( val, 4 );
    tr_bencDictAddStr( val, "announce", tk->announce );
    if( tk->scrape )
        tr_bencDictAddStr( val, "scrape", tk->scrape );
}

/**
 * append to "list" a dictionary whose keys are
 * the string keys from gl_inf and whose values are
 * torrent info set from "torrent_id" and "inf".
 *
 * "types" is a bitwise-and'ed set of fields
 * from the IPC_INF_* enum in ipcparse.h.
 * It specifies what to put in the dictionary.
 */
int
ipc_addinfo( tr_benc         * list,
             int               torrent_id,
             tr_torrent      * tor,
             int               types )
{
    const tr_info * inf = tr_torrentInfo( tor );
    const struct inf * it;
    const struct inf * end;
    tr_benc * dict;

    /* always send torrent id */
    types |= IPC_INF_ID;

    /* create a dict to hold the info */
    dict = tr_bencListAddDict( list, TR_N_ELEMENTS( gl_inf ) );

    /* populate the dict */
    for( it=gl_inf, end=it+TR_N_ELEMENTS(gl_inf); it!=end; ++it )
    {
        if( types & it->type )
        {
            tr_benc * val = tr_bencDictAdd( dict, it->name );

            switch( it->type )
            {
                case IPC_INF_COMMENT:
                    tr_bencInitStrDup( val, inf->comment ? inf->comment : "" );
                    break;
                case IPC_INF_CREATOR:
                    tr_bencInitStrDup( val, inf->creator ? inf->creator : "" );
                    break;
                case IPC_INF_DATE:
                    tr_bencInitInt( val, inf->dateCreated );
                    break;
                case IPC_INF_FILES: {
                    tr_file_index_t f;
                    tr_bencInitList( val, inf->fileCount );
                    for( f=0; f<inf->fileCount; ++f ) {
                        tr_benc * file = tr_bencListAddDict( val, 2 );
                        tr_bencDictAddStr( file, "name", inf->files[f].name );
                        tr_bencDictAddInt( file, "size", inf->files[f].length );
                    }
                    break;
                }
                case IPC_INF_HASH:
                    tr_bencInitStr( val, inf->hashString, -1, 1 );
                    break;
                case IPC_INF_ID:
                    tr_bencInitInt( val, torrent_id );
                    break;
                case IPC_INF_NAME:
                    tr_bencInitStrDup( val, inf->name );
                    break;
                case IPC_INF_PATH:
                    tr_bencInitStrDup( val, inf->torrent );
                    break;
                case IPC_INF_PRIVATE:
                    tr_bencInitInt( val, inf->isPrivate ? 1 : 0 );
                    break;
                case IPC_INF_SIZE:
                    tr_bencInitInt( val, inf->totalSize );
                    break;
                case IPC_INF_TRACKERS: {
                    int j;
                    int prevTier = -1;
                    tr_benc * tier = NULL;
                    tr_bencInitList( val, 0 );
                    for( j=0; j<inf->trackerCount; ++j ) {
                        if( prevTier != inf->trackers[j].tier ) {
                            prevTier = inf->trackers[j].tier;
                            tier = tr_bencListAddList( val, 0 );
                        }
                        filltracker( tr_bencListAdd( tier ), &inf->trackers[j] );
                    }
                    break;
                }
                default:
                    assert( 0 );
                    break;
            }
        }
    }

    return 0;
}

/**
 * append to "list" a dictionary whose keys are
 * the string keys from gl_stat and whose values
 * are torrent statistics set from "st".
 *
 * "types" is a bitwise-and'ed set of fields
 * from the IPC_INF_* enum in ipcparse.h.
 * It specifies what to put in the dictionary.
 */
int
ipc_addstat( tr_benc      * list,
             int            torrent_id,
             tr_torrent   * tor,
             int            types )
{
    const tr_stat * st = tr_torrentStatCached( tor );
    const struct inf * it;
    const struct inf * end;
    tr_benc  * dict;

    /* always send torrent id */
    types |= IPC_ST_ID;

    /* add the dictionary child */
    dict = tr_bencListAddDict( list, TR_N_ELEMENTS( gl_stat ) );

    /* populate the dict */
    for( it=gl_stat, end=it+TR_N_ELEMENTS(gl_stat); it!=end; ++it )
    {
        if( types & it->type )
        {
            tr_benc * val = tr_bencDictAdd( dict, it->name );

            switch( it->type )
            {
                case IPC_ST_COMPLETED:
                case IPC_ST_DOWNVALID:
                    tr_bencInitInt( val, st->haveValid );
                    break;
                case IPC_ST_DOWNSPEED:
                    tr_bencInitInt( val, st->rateDownload * 1024 );
                    break;
                case IPC_ST_DOWNTOTAL:
                    tr_bencInitInt( val, st->downloadedEver );
                    break;
                case IPC_ST_ERROR: {
                    const char * s;
                    const tr_errno e = st->error;
                    if( !e ) s = "";
                    else if( e == TR_ERROR_ASSERT )          s = "assert";
                    else if( e == TR_ERROR_IO_PERMISSIONS )  s = "io-permissions";
                    else if( e == TR_ERROR_IO_SPACE )        s = "io-space";
                    else if( e == TR_ERROR_IO_FILE_TOO_BIG ) s = "io-file-too-big";
                    else if( e == TR_ERROR_IO_OPEN_FILES )   s = "io-open-files";
                    else if( TR_ERROR_IS_IO( e ) )           s = "io-other";
                    else if( e == TR_ERROR_TC_ERROR )        s = "tracker-error";
                    else if( e == TR_ERROR_TC_WARNING )      s = "tracker-warning";
                    else if( TR_ERROR_IS_TC( e ) )           s = "tracker-other";
                    else                                     s = "other";
                    tr_bencInitStrDup( val, s );
                    break;
                }
                case IPC_ST_ERRMSG: {
                    const char * s;
                    if( !st->error ) s = "";
                    else if( !*st->errorString ) s = "other";
                    else s = st->errorString;
                    tr_bencInitStrDup( val, s );
                    break;
                }
                case IPC_ST_ETA:
                    tr_bencInitInt( val, st->eta );
                    break;
                case IPC_ST_ID:
                    tr_bencInitInt( val, torrent_id );
                    break;
                case IPC_ST_PEERDOWN:
                    tr_bencInitInt( val, st->peersSendingToUs );
                    break;
                case IPC_ST_PEERFROM: {
                    const int * c = st->peersFrom;
                    tr_bencInitDict( val, 4 );
                    tr_bencDictAddInt( val, "cache",    c[TR_PEER_FROM_CACHE] );
                    tr_bencDictAddInt( val, "incoming", c[TR_PEER_FROM_INCOMING] );
                    tr_bencDictAddInt( val, "pex",      c[TR_PEER_FROM_PEX] );
                    tr_bencDictAddInt( val, "tracker",  c[TR_PEER_FROM_TRACKER] );
                    break;
                }
                case IPC_ST_PEERMAX:
                    tr_bencInitInt( val, tor->maxConnectedPeers );
                    break;
                case IPC_ST_PEERTOTAL:
                    tr_bencInitInt( val, st->peersConnected );
                    break;
                case IPC_ST_PEERUP:
                    tr_bencInitInt( val, st->peersGettingFromUs );
                    break;
                case IPC_ST_RUNNING:
                    tr_bencInitInt( val, TR_STATUS_IS_ACTIVE(st->status) );
                    break;
                case IPC_ST_STATE: {
                    const char * s;
                    if( TR_STATUS_CHECK_WAIT & st->status )    s = "waiting to check";
                    else if( TR_STATUS_CHECK & st->status )    s = "checking";
                    else if( TR_STATUS_DOWNLOAD & st->status ) s = "downloading";
                    else if( TR_STATUS_SEED & st->status )     s = "seeding";
                    else if( TR_STATUS_STOPPED & st->status )  s = "paused";
                    else                                       s = "error";
                    tr_bencInitStr( val, s, -1, 1 );
                    break;
                }
                case IPC_ST_SWARM:
                    tr_bencInitInt( val, st->swarmspeed * 1024 );
                    break;
                case IPC_ST_TRACKER:
                    filltracker( val, st->tracker );
                    break;
                case IPC_ST_TKDONE:
                    tr_bencInitInt( val, st->completedFromTracker );
                    break;
                case IPC_ST_TKLEECH:
                    tr_bencInitInt( val, st->leechers );
                    break;
                case IPC_ST_TKSEED:
                    tr_bencInitInt( val, st->seeders );
                    break;
                case IPC_ST_UPSPEED:
                    tr_bencInitInt( val, st->rateUpload * 1024 );
                    break;
                case IPC_ST_UPTOTAL:
                    tr_bencInitInt( val, st->uploadedEver );
                    break;
                default:
                    assert( 0 );
                    break;
            }
        }
    }

    return 0;
}

/**
 * This reads a handshake message from the client to decide
 * which IPC protocol version to use.
 * Returns 0 on success; otherwise, returns -1 and sets errno.
 *
 * @see ipc_handleMessages()
 * @see ipc_mkvers()
 */
static int
handlevers( struct ipc_info * info, tr_benc * dict )
{
    tr_benc * vers;
    int64_t      min, max;

    if( !tr_bencIsDict( dict ) )
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
            min = max = vers->val.i;
            break;
        case TYPE_DICT:
            min = max = -1;
            tr_bencDictFindInt( vers, "min", &min );
            tr_bencDictFindInt( vers, "max", &max );
            break;
        default:
            min = max = -1;
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

enum ipc_msg
ipc_msgid( const struct ipc_info * info, const char * name )
{
    const struct msg * msg = msglookup( name );

    return msg && sessionSupportsMessage( info, msg->id )
        ? msg->id
        : IPC__MSG_COUNT;
}

/**
 * Invokes the trd_msgfunc for the message passed in.
 * Returns 0 on success; otherwise, returns -1 and sets errno.
 */
static int
callmsgfunc( const struct ipc_info  * info,
             tr_benc                * name,
             tr_benc                * val,
             tr_benc                * tagval,
             void                   * user_data )
{
    const struct msg * msg;
    int64_t            tag;

    /* extract tag from tagval */
    if( !tagval )
        tag = -1;
    else if( tr_bencIsInt( tagval ) )
        tag = tagval->val.i;
    else {
        errno = EINVAL;
        return -1;
    }

    /* find the msg corresponding to `name' */
    if( !tr_bencIsString( name ) ) {
        errno = EINVAL;
        return -1;
    }
    msg = msglookup( name->val.s.s );

    if( msg && msg->minvers <= info->vers )
    {
        if( info->funcs->msgs[msg->id] != NULL )
        {
            (*info->funcs->msgs[msg->id])( msg->id, val, tag, user_data );
        }
        else if( info->funcs->def )
        {
            info->funcs->def( msg->id, val, tag, user_data );
        }
    }
    else if( NULL != info->funcs->def )
        info->funcs->def( IPC__MSG_UNKNOWN, NULL, tag, user_data );

    return 0;
}

static int
handlemsgs( const struct ipc_info  * session,
            tr_benc                * message,
            void                   * user_data )
{
    tr_benc * name, * val, * tag;

    assert( ipc_hasvers( session ) );

    if( DICTPAYLOAD( session ) )
    {
        int ii;

        if( TYPE_DICT != message->type || message->val.l.count % 2 )
        {
            errno = EINVAL;
            return -1;
        }

        for( ii = 0; ii < message->val.l.count; ii += 2 )
        {
            assert( ii + 1 < message->val.l.count );
            name = &message->val.l.vals[ii];
            val  = &message->val.l.vals[ii+1];
            if( 0 > callmsgfunc( session, name, val, NULL, user_data ) )
                return -1;
        }
    }
    else
    {
        if( TYPE_LIST != message->type || 2 > message->val.l.count )
        {
            errno = EINVAL;
            return -1;
        }

        name = &message->val.l.vals[0];
        val  = &message->val.l.vals[1];
        tag  = ( 2 == message->val.l.count ? NULL : &message->val.l.vals[2] );
        if( 0 > callmsgfunc( session, name, val, tag, user_data ) )
            return -1;
    }

    return 0;
}

ssize_t
ipc_handleMessages( struct ipc_info  * info,
                    const uint8_t    * msgs,
                    ssize_t            msgslen,
                    void             * user_data )
{
    char        hex[IPC_MIN_MSG_LEN+1], * end;
    ssize_t     off, len;
    tr_benc  benc;

    for( off = 0; off + IPC_MIN_MSG_LEN < msgslen; off += IPC_MIN_MSG_LEN + len )
    {
        memcpy( hex, msgs + off, IPC_MIN_MSG_LEN );
        hex[IPC_MIN_MSG_LEN] = '\0';
        end = NULL;
        len = strtol( hex, &end, 16 );
        if( hex + IPC_MIN_MSG_LEN != end ||
            0 > len || IPC_MAX_MSG_LEN < len )
        {
            errno = EINVAL;
            return -1;
        }
        if( off + IPC_MIN_MSG_LEN + len > msgslen )
        {
            break;
        }
        errno = 0;
        if( tr_bencLoad( msgs + off + IPC_MIN_MSG_LEN, len, &benc, NULL ) )
        {
            if( 0 == errno )
            {
                errno = EINVAL;
            }
            return -1;
        }
        if( 0 > ( ipc_hasvers( info ) ? handlemsgs( info, &benc, user_data ) :
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
compareNameToInf( const void * a, const void * b )
{
    const struct inf * inf = b;
    return strcmp( a, inf->name );
}

/**
 * Convert a benc list of string keys from gl_inf or gl_stat
 * into a bitwise-or'ed int representation.
 * msg_id must be either IPC_MSG_INFO or IPC_MSG_STAT.
 * @see ipc_infoname()
 */
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

    if( !tr_bencIsList( list ) )
        return ret;

    for( i=0; i<list->val.l.count; ++i )
    {
        const tr_benc * name = &list->val.l.vals[i];
        const struct inf * inf;

        if( !tr_bencIsString( name ) )
            continue;

        inf = bsearch( name->val.s.s,
                       array, len, sizeof( struct inf ),
                       compareNameToInf );
        if( inf )
            ret |= inf->type;
    }

    return ret;
}

/**
 * This function is the reverse of ipc_infotypes:
 * it returns the string key that corresponds to the type passed in.
 * Type is one of the IPC_INF_* or IPC_ST_* enums from ipcparse.h.
 * msg_id must be either IPC_MSG_INFO or IPC_MSG_STAT.
 * @see ipc_infotypes()
 */
const char *
ipc_infoname( enum ipc_msg id, int type )
{
    const struct inf * it;
    const struct inf * end;

    switch( id )
    {
        case IPC_MSG_INFO:
            it = gl_inf;
            end = it + TR_N_ELEMENTS( gl_inf );
            break;
        case IPC_MSG_STAT:
            it = gl_stat;
            end = it + TR_N_ELEMENTS( gl_stat );
            break;
        default:
            assert( 0 );
            break;
    }

    for( ; it!=end; ++it )
        if( it->type == type )
            return it->name;

    assert( 0 );
    return NULL;
}
