/******************************************************************************
 * Copyright (c) 2005 Eric Petit
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
#include <stdio.h>
#include <stdarg.h>
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
#include <sys/resource.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#ifdef BEOS_NETSERVER
#  define in_port_t uint16_t
#else
#  include <arpa/inet.h>
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
#define TR_NTOHL(p,a) (a) = ntohl(*((uint32_t*)(p)))
#define TR_HTONL(a,p) *((uint32_t*)(p)) = htonl((a))

/* Multithreading support: native threads on BeOS, pthreads elsewhere */
#ifdef SYS_BEOS
#  include <kernel/OS.h>
#  define tr_thread_t             thread_id
#  define tr_threadCreate(pt,f,d) *(pt) = spawn_thread((void*)f,"",10,d); \
                                          resume_thread(*(pt));
#  define tr_threadJoin(t)        { long e; wait_for_thread(t,&e); }
#  define tr_lock_t               sem_id
#  define tr_lockInit(pl)         *(pl) = create_sem(1,"")
#  define tr_lockLock(l)          acquire_sem(l)
#  define tr_lockUnlock(l)        release_sem(l)
#  define tr_lockClose(l)         delete_sem(l)
#else
#  include <pthread.h>
#  define tr_thread_t             pthread_t
#  define tr_threadCreate(pt,f,d) pthread_create(pt,NULL,(void*)f,d)
#  define tr_threadJoin(t)        pthread_join(t,NULL)
#  define tr_lock_t               pthread_mutex_t
#  define tr_lockInit(pl)         pthread_mutex_init(pl,NULL)
#  define tr_lockLock(l)          pthread_mutex_lock(&l)
#  define tr_lockUnlock(l)        pthread_mutex_unlock(&l)
#  define tr_lockClose(l)         pthread_mutex_destroy(&l)
#endif

/* Sometimes the system defines MAX/MIN, sometimes not. In the latter
   case, define those here since we will use them */
#ifndef MAX
#define MAX(a,b) ((a)>(b)?(a):(b))
#endif
#ifndef MIN
#define MIN(a,b) ((a)>(b)?(b):(a))
#endif

#define TR_MAX_PEER_COUNT 60

typedef struct tr_torrent_s tr_torrent_t;

#include "bencode.h"
#include "metainfo.h"
#include "tracker.h"
#include "peer.h"
#include "net.h"
#include "inout.h"
#include "upload.h"
#include "fdlimit.h"
#include "clients.h"

struct tr_torrent_s
{
    tr_info_t info;

    tr_upload_t     * upload;
    tr_fd_t         * fdlimit;

    int               status;
    char              error[128];

    char            * id;

    /* An escaped string used to include the hash in HTTP queries */
    char              hashString[3*SHA_DIGEST_LENGTH+1];

    char              scrape[MAX_PATH_LENGTH];

    /* Where to download */
    char            * destination;
    
    /* How many bytes we ask for per request */
    int               blockSize;
    int               blockCount;
    
    /* Status for each block
       -1 = we have it
        n = we are downloading it from n peers */
    char            * blockHave;
    int               blockHaveCount;
    uint8_t         * bitfield;

    volatile char     die;
    tr_thread_t       thread;
    tr_lock_t         lock;

    tr_tracker_t    * tracker;
    tr_io_t         * io;
    uint64_t          stopDate;

    int               bindSocket;
    int               bindPort;
    int               peerCount;
    tr_peer_t       * peers[TR_MAX_PEER_COUNT];

    uint64_t          dates[10];
    uint64_t          downloaded[10];
    uint64_t          uploaded[10];

    char            * prefsDirectory;
};

#include "utils.h"

struct tr_handle_s
{
    int            torrentCount;
    tr_torrent_t * torrents[TR_MAX_TORRENT_COUNT];

    tr_upload_t  * upload;
    tr_fd_t      * fdlimit;

    int            bindPort;

    char           id[21];
    char           prefsDirectory[256];
};

#endif
