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

#ifndef TR_DAEMON_IPC_H
#define TR_DAEMON_IPC_H

#include <inttypes.h>
#include <unistd.h> /* for ssize_t */

#define IPC_MIN_MSG_LEN         ( 8 )
#define IPC_MAX_MSG_LEN         ( 0x7fffffff - IPC_MIN_MSG_LEN )

/* These need to be in the same order as ipcparse.c's gl_msgs array. */
enum ipc_msg
{
    IPC_MSG_ADDONEFILE = 0,
    IPC_MSG_ADDMANYFILES,
    IPC_MSG_AUTOMAP,
    IPC_MSG_AUTOSTART,
    IPC_MSG_BAD,
    IPC_MSG_DIR,
    IPC_MSG_DOWNLIMIT,
    IPC_MSG_CRYPTO,
    IPC_MSG_FAIL,
    IPC_MSG_GETAUTOMAP,
    IPC_MSG_GETAUTOSTART,
    IPC_MSG_GETDIR,
    IPC_MSG_GETDOWNLIMIT,
    IPC_MSG_GETCRYPTO,
    IPC_MSG_GETINFO,
    IPC_MSG_GETINFOALL,
    IPC_MSG_GETPEX,
    IPC_MSG_GETPORT,
    IPC_MSG_GETSTAT,
    IPC_MSG_GETSTATALL,
    IPC_MSG_GETSUP,
    IPC_MSG_GETUPLIMIT,
    IPC_MSG_INFO,
    IPC_MSG_LOOKUP,
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
    IPC__MSG_COUNT,
    IPC__MSG_UNKNOWN
};

/* If you add or delete a constant here, to renumber the ones after it.
 * They need to be in ascending order starting at zero, with no gaps.
 * They also need to be in the same order as ipcparse.c's gl_inf array. */
#define IPC_INF_COMMENT         ( 1 << 0 )
#define IPC_INF_CREATOR         ( 1 << 1 )
#define IPC_INF_DATE            ( 1 << 2 )
#define IPC_INF_FILES           ( 1 << 3 )
#define IPC_INF_HASH            ( 1 << 4 )
#define IPC_INF_ID              ( 1 << 5 )
#define IPC_INF_NAME            ( 1 << 6 )
#define IPC_INF_PATH            ( 1 << 7 )
#define IPC_INF_PRIVATE         ( 1 << 8 )
#define IPC_INF_SIZE            ( 1 << 9 )
#define IPC_INF_TRACKERS        ( 1 << 10 )
#define IPC_INF__MAX            ( 1 << 11 )

/* If you add or delete a constant here, to renumber the ones after it.
 * They need to be in ascending order starting at zero, with no gaps.
 * They also need to be in the same order as ipcparse.c's gl_stat array. */
#define IPC_ST_COMPLETED        ( 1 << 0 )
#define IPC_ST_DOWNSPEED        ( 1 << 1 )
#define IPC_ST_DOWNTOTAL        ( 1 << 2 )
#define IPC_ST_DOWNVALID        ( 1 << 3 )
#define IPC_ST_ERROR            ( 1 << 4 )
#define IPC_ST_ERRMSG           ( 1 << 5 )
#define IPC_ST_ETA              ( 1 << 6 )
#define IPC_ST_ID               ( 1 << 7 )
#define IPC_ST_PEERDOWN         ( 1 << 8 )
#define IPC_ST_PEERFROM         ( 1 << 9 )
#define IPC_ST_PEERTOTAL        ( 1 << 10 )
#define IPC_ST_PEERUP           ( 1 << 11 )
#define IPC_ST_RUNNING          ( 1 << 12 )
#define IPC_ST_TKDONE           ( 1 << 13 )
#define IPC_ST_TKLEECH          ( 1 << 14 )
#define IPC_ST_TKSEED           ( 1 << 15 )
#define IPC_ST_STATE            ( 1 << 16 )
#define IPC_ST_SWARM            ( 1 << 17 )
#define IPC_ST_TRACKER          ( 1 << 18 )
#define IPC_ST_UPSPEED          ( 1 << 19 )
#define IPC_ST_UPTOTAL          ( 1 << 20 )
#define IPC_ST__MAX             ( 1 << 21 )

struct ipc_funcs;
struct ipc_info;
struct tr_info;
struct tr_benc;
struct tr_stat;


#define TORRENT_ID_VALID( id )  ( ( 0 < (id) ) && ( (id) < INT_MAX ) )

typedef void ( *trd_msgfunc )( enum ipc_msg      msg_id,
                               struct tr_benc  * benc,
                               int64_t           tag,
                               void            * arg );

/* any of these functions that can fail may set errno for any of the
   errors set by malloc() or calloc() */

/* setup */
struct ipc_funcs * ipc_initmsgs ( void );
void               ipc_addmsg   ( struct ipc_funcs *, enum ipc_msg,
                                  trd_msgfunc );
void               ipc_setdefmsg( struct ipc_funcs *, trd_msgfunc );
void               ipc_freemsgs ( struct ipc_funcs * );
struct ipc_info *  ipc_newcon   ( struct ipc_funcs * );
void               ipc_freecon  ( struct ipc_info * );
int                ipc_hasvers  ( const struct ipc_info * );

/* message creation */
/* sets errno to EPERM if requested message not supported by protocol vers */
struct tr_benc * ipc_initval  ( const struct ipc_info *, enum ipc_msg,
                                int64_t tag, struct tr_benc *, int );
uint8_t *    ipc_mkval    ( const struct tr_benc *, size_t * );
uint8_t *    ipc_mkempty  ( const struct ipc_info *, size_t *, enum ipc_msg,
                            int64_t );
uint8_t *    ipc_mkint    ( const struct ipc_info *, size_t *, enum ipc_msg,
                            int64_t tag, int64_t val );
uint8_t *    ipc_mkstr    ( const struct ipc_info *, size_t *, enum ipc_msg,
                            int64_t tag, const char * val );
uint8_t *    ipc_mkvers   ( size_t *, const char * );
uint8_t *    ipc_mkgetinfo( const struct ipc_info *, size_t *, enum ipc_msg,
                            int64_t, int, const int * );
int          ipc_addinfo  ( struct tr_benc *, int,
                            const struct tr_info *, int );
int          ipc_addstat  ( struct tr_benc *, int,
                            const struct tr_stat *, int );

/* sets errno to EINVAL on parse error or
   EPERM for unsupported protocol version */
ssize_t      ipc_parse    ( struct ipc_info *, const uint8_t *,
                            ssize_t, void * );

/* misc info functions, these will always succeed */
enum ipc_msg ipc_msgid    ( const struct ipc_info *, const char * );
int          ipc_ishandled( const struct ipc_info *, enum ipc_msg );
int          ipc_havetags ( const struct ipc_info * );
int          ipc_infotypes( enum ipc_msg, const struct tr_benc * );
const char * ipc_infoname ( enum ipc_msg, int );

#endif /* TR_DAEMON_IPC_H */
