/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2006 Transmission authors and contributors
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

#ifndef TR_INTERNAL_H
#define TR_INTERNAL_H 1

/* Standard headers used here and there.
   That is probably ugly to put them all here, but it is sooo
   convenient */
#if ( defined( __unix__ ) || defined( unix ) ) && !defined( USG )
#include <sys/param.h>
#endif
#include <stdio.h>
#include <stdarg.h>
#ifdef SYS_BEOS
/* BeOS doesn't declare vasprintf in its headers, but actually
 * implements it */
int vasprintf( char **, const char *, va_list );
#endif
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifndef __AMIGAOS4__ 
#include <sys/resource.h>
#endif
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <assert.h>
#ifdef SYS_BEOS
#  define socklen_t uint32_t
#endif
#ifdef BEOS_NETSERVER
#  define in_port_t uint16_t
#else
#  include <arpa/inet.h>
#endif

#ifndef INADDR_NONE
#define INADDR_NONE             0xffffffff
#endif

#ifdef __GNUC__
#  define UNUSED __attribute__((unused))
#  define PRINTF( fmt, args ) __attribute__((format (printf, fmt, args)))
#else
#  define UNUSED
#  define PRINTF( fmt, args )
#endif

/* We use OpenSSL whenever possible, since it is likely to be more
   optimized and it is ok to use it with a MIT-licensed application.
   Otherwise, we use the included implementation by vi@nwr.jp. */
#ifdef HAVE_OPENSSL
#  undef SHA_DIGEST_LENGTH
#  include <openssl/sha.h>
#else
#  include "sha1.h"
#  define SHA1(p,i,h) \
   { \
     sha1_state_s pms; \
     sha1_init( &pms ); \
     sha1_update( &pms, (sha1_byte_t *) p, i ); \
     sha1_finish( &pms, (sha1_byte_t *) h ); \
   }
#endif

/* Convenient macros to perform uint32_t endian conversions with
   char pointers */
#define TR_NTOHL(p,a) (a) = tr_ntohl((p))
#define TR_HTONL(a,p) tr_htonl((a), (p))
static inline uint32_t tr_ntohl( uint8_t * p )
{
	uint32_t u;
	memcpy( &u, p, sizeof( uint32_t ) );
	return ntohl( u );
}
static inline void tr_htonl( uint32_t a, uint8_t * p )
{
	uint32_t u;
	u = htonl( a );
	memcpy ( p, &u, sizeof( uint32_t ) );
}

/* Sometimes the system defines MAX/MIN, sometimes not. In the latter
   case, define those here since we will use them */
#ifndef MAX
#define MAX(a,b) ((a)>(b)?(a):(b))
#endif
#ifndef MIN
#define MIN(a,b) ((a)>(b)?(b):(a))
#endif

#define TR_MAX_PEER_COUNT 60

typedef struct tr_completion_s tr_completion_t;
typedef struct tr_shared_s tr_shared_t;

typedef enum { TR_NET_OK, TR_NET_ERROR, TR_NET_WAIT } tr_tristate_t;

#include "platform.h"
#include "bencode.h"
#include "metainfo.h"
#include "tracker.h"
#include "fdlimit.h"
#include "peer.h"
#include "net.h"
#include "inout.h"
#include "ratecontrol.h"
#include "clients.h"
#include "choking.h"
#include "natpmp.h"
#include "upnp.h"
#include "http.h"
#include "xml.h"

void tr_torrentAddCompact( tr_torrent_t * tor, uint8_t * buf, int count );
void tr_torrentAttachPeer( tr_torrent_t * tor, tr_peer_t * peer );

struct tr_torrent_s
{
    tr_handle_t * handle;
    tr_info_t info;

    int                customSpeedLimit;
    tr_ratecontrol_t * upload;
    tr_ratecontrol_t * download;
    tr_ratecontrol_t * swarmspeed;

    int               status;
    int               error;
    char              errorString[128];
    int               finished;

    char            * id;
    char            * key;
    int             * bindPort;

    /* An escaped string used to include the hash in HTTP queries */
    char              escapedHashString[3*SHA_DIGEST_LENGTH+1];

    /* Where to download */
    char            * destination;
    
    /* How many bytes we ask for per request */
    int               blockSize;
    int               blockCount;
    
    tr_completion_t * completion;

    volatile char     die;
    tr_thread_t       thread;
    tr_lock_t         lock;
    tr_cond_t         cond;

    tr_tracker_t    * tracker;
    tr_io_t         * io;
    uint64_t          stopDate;

    int               peerCount;
    tr_peer_t       * peers[TR_MAX_PEER_COUNT];

    uint64_t          date;
    uint64_t          downloadedCur;
    uint64_t          downloadedPrev;
    uint64_t          uploadedCur;
    uint64_t          uploadedPrev;

    tr_stat_t         stats[2];
    int               statCur;

    tr_torrent_t    * prev;
    tr_torrent_t    * next;
};

#include "utils.h"
#include "completion.h"

struct tr_handle_s
{
    int            torrentCount;
    tr_torrent_t * torrentList;

    int            bindPort;
    int            uploadLimit;
    int            downloadLimit;
    tr_shared_t  * shared;

    char           id[21];
    char           key[21];
};

#endif
