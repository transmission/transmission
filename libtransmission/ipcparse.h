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

#ifndef TR_DAEMON_IPC_H
#define TR_DAEMON_IPC_H

#include <inttypes.h>

/* yay for typedefs, we can't forward declare benc_val_t or tr_info_t
   like with structs */
#include "bencode.h"
#include "transmission.h"

#define IPC_MIN_MSG_LEN         ( 8 )
#define IPC_MAX_MSG_LEN         ( 0x7fffffff - IPC_MIN_MSG_LEN )

enum ipc_msg
{
    IPC_MSG_ADDMANYFILES = 0,
    IPC_MSG_ADDONEFILE,
    IPC_MSG_AUTOMAP,
    IPC_MSG_AUTOSTART,
    IPC_MSG_BAD,
    IPC_MSG_DIR,
    IPC_MSG_DOWNLIMIT,
    IPC_MSG_FAIL,
    IPC_MSG_GETAUTOMAP,
    IPC_MSG_GETAUTOSTART,
    IPC_MSG_GETDIR,
    IPC_MSG_GETDOWNLIMIT,
    IPC_MSG_GETINFO,
    IPC_MSG_GETINFOALL,
    IPC_MSG_GETPEX,
    IPC_MSG_GETPORT,
    IPC_MSG_GETSTAT,
    IPC_MSG_GETSTATALL,
    IPC_MSG_GETSUP,
    IPC_MSG_GETUPLIMIT,
    IPC_MSG_LOOKUP,
    IPC_MSG_INFO,
    IPC_MSG_NOOP,
    IPC_MSG_NOTSUP,
    IPC_MSG_PEX,
    IPC_MSG_PORT,
    IPC_MSG_QUIT,
    IPC_MSG_REMOVE,
    IPC_MSG_REMOVEALL,
    IPC_MSG_START,
    IPC_MSG_STARTALL,
    IPC_MSG_STAT,
    IPC_MSG_STOP,
    IPC_MSG_STOPALL,
    IPC_MSG_OK,
    IPC_MSG_SUP,
    IPC_MSG_UPLIMIT,
    IPC_MSG_VERSION,
    IPC__MSG_COUNT
};

#define IPC_INF_COMMENT         ( 1 << 0 )
#define IPC_INF_CREATOR         ( 1 << 1 )
#define IPC_INF_DATE            ( 1 << 2 )
#define IPC_INF_FILES           ( 1 << 3 )
#define IPC_INF_HASH            ( 1 << 4 )
#define IPC_INF_ID              ( 1 << 5 )
#define IPC_INF_NAME            ( 1 << 6 )
#define IPC_INF_PATH            ( 1 << 7 )
#define IPC_INF_PRIVATE         ( 1 << 8 )
#define IPC_INF_SAVED           ( 1 << 9 )
#define IPC_INF_SIZE            ( 1 << 10 )
#define IPC_INF_TRACKERS        ( 1 << 11 )
#define IPC_INF__MAX            ( 1 << 12 )

#define IPC_ST_COMPLETED        ( 1 << 0 )
#define IPC_ST_DOWNSPEED        ( 1 << 1 )
#define IPC_ST_DOWNTOTAL        ( 1 << 2 )
#define IPC_ST_ERROR            ( 1 << 3 )
#define IPC_ST_ERRMSG           ( 1 << 4 )
#define IPC_ST_ETA              ( 1 << 5 )
#define IPC_ST_ID               ( 1 << 6 )
#define IPC_ST_PEERDOWN         ( 1 << 7 )
#define IPC_ST_PEERFROM         ( 1 << 8 )
#define IPC_ST_PEERTOTAL        ( 1 << 9 )
#define IPC_ST_PEERUP           ( 1 << 10 )
#define IPC_ST_RUNNING          ( 1 << 11 )
#define IPC_ST_STATE            ( 1 << 12 )
#define IPC_ST_SWARM            ( 1 << 13 )
#define IPC_ST_TRACKER          ( 1 << 14 )
#define IPC_ST_TKDONE           ( 1 << 15 )
#define IPC_ST_TKLEECH          ( 1 << 16 )
#define IPC_ST_TKSEED           ( 1 << 17 )
#define IPC_ST_UPSPEED          ( 1 << 18 )
#define IPC_ST_UPTOTAL          ( 1 << 19 )
#define IPC_ST__MAX             ( 1 << 20 )

struct ipc_funcs;
struct ipc_info;
struct strlist;

struct ipc_info
{
    struct ipc_funcs * funcs;
    int                vers;
};

#define HASVERS( info )         ( 0 < (info)->vers )

#define TORRENT_ID_VALID( id )  ( 0 < (id) && INT_MAX > (id) )

typedef void ( *trd_msgfunc )( enum ipc_msg, benc_val_t *, int64_t, void * );

/* any of these functions that can fail may set errno for any of the
   errors set by malloc() or calloc() */

/* setup */
struct ipc_funcs * ipc_initmsgs ( void );
int          ipc_addmsg   ( struct ipc_funcs *, enum ipc_msg, trd_msgfunc );
void         ipc_setdefmsg( struct ipc_funcs *, trd_msgfunc );
void         ipc_freemsgs ( struct ipc_funcs * );
void         ipc_newcon   ( struct ipc_info *, struct ipc_funcs * );

/* message creation */
/* sets errno to EPERM if requested message not supported by protocol vers */
benc_val_t * ipc_initval  ( struct ipc_info *, enum ipc_msg, int64_t,
                            benc_val_t *, int );
uint8_t *    ipc_mkval    ( benc_val_t *, size_t * );
uint8_t *    ipc_mkempty  ( struct ipc_info *, size_t *, enum ipc_msg,
                            int64_t );
uint8_t *    ipc_mkint    ( struct ipc_info *, size_t *, enum ipc_msg, int64_t,
                            int64_t );
uint8_t *    ipc_mkstr    ( struct ipc_info *, size_t *, enum ipc_msg, int64_t,
                            const char * );
uint8_t *    ipc_mkvers   ( size_t * );
uint8_t *    ipc_mkgetinfo( struct ipc_info *, size_t *, enum ipc_msg, int64_t,
                            int, const int * );
int          ipc_addinfo  ( benc_val_t *, int, tr_info_t *, int );
int          ipc_addstat  ( benc_val_t *, int, tr_stat_t *, int );

/* sets errno to EINVAL on parse error or
   EPERM for unsupported protocol version */
ssize_t      ipc_parse    ( struct ipc_info *, uint8_t *, ssize_t, void * );

/* misc info functions, these will always succeed */
int          ipc_havemsg  ( struct ipc_info *, enum ipc_msg );
enum ipc_msg ipc_msgid    ( struct ipc_info *, const char * );
int          ipc_ishandled( struct ipc_info *, enum ipc_msg );
int          ipc_havetags ( struct ipc_info * );
int          ipc_infotypes( enum ipc_msg, benc_val_t * );
const char * ipc_infoname ( enum ipc_msg, int );

#endif /* TR_DAEMON_IPC_H */
