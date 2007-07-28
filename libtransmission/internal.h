/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2007 Transmission authors and contributors
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
#ifdef SYS_BEOS
/* BeOS doesn't declare vasprintf in its headers, but actually
 * implements it */
int vasprintf( char **, const char *, va_list );
#endif
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#ifndef __AMIGAOS4__ 
#include <sys/resource.h>
#endif
#include <assert.h>
#ifdef SYS_BEOS
#  define socklen_t uint32_t
#endif
#ifdef BEOS_NETSERVER
#  define in_port_t uint16_t
#else
#  include <arpa/inet.h>
#endif

#define TR_NAME                 "Transmission"

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
#if defined(HAVE_OPENSSL) || defined(HAVE_LIBSSL)
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

#define TR_MAX_PEER_COUNT 60

typedef struct tr_completion_s tr_completion_t;
typedef struct tr_shared_s tr_shared_t;

typedef enum { TR_NET_OK, TR_NET_ERROR, TR_NET_WAIT } tr_tristate_t;

#include "platform.h"
#include "tracker.h"
#include "peer.h"
#include "inout.h"
#include "ratecontrol.h"
#include "utils.h"

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

void tr_peerIdNew ( char* buf, int buflen );

void tr_torrentResetTransferStats( tr_torrent_t * );

int tr_torrentAddCompact( tr_torrent_t * tor, int from,
                           uint8_t * buf, int count );
int tr_torrentAttachPeer( tr_torrent_t * tor, tr_peer_t * peer );

void tr_torrentSetHasPiece( tr_torrent_t * tor, int pieceIndex, int has );

void tr_torrentReaderLock    ( const tr_torrent_t * );
void tr_torrentReaderUnlock  ( const tr_torrent_t * );
void tr_torrentWriterLock    ( tr_torrent_t * );
void tr_torrentWriterUnlock  ( tr_torrent_t * );


#define TOR_BLOCK_PIECE(tor,block) \
    ( (block) / ( (tor)->info.pieceSize / (tor)->blockSize ) )

#define TOR_PIECE_FIRST_BLOCK(tor,piece) \
    ( (tor)->info.pieces[(piece)].firstBlock )

#define TR_BLOCKS_IN_PIECE(tor,piece) \
    ( (tor)->info.pieces[(piece)].blockCount )

#define tr_blockSize(a) _tr_blockSize(tor,a)
int _tr_blockSize( const tr_torrent_t * tor, int block );

#define tr_pieceSize(a) _tr_pieceSize(tor,a)
int _tr_pieceSize( const tr_torrent_t * tor, int piece );

#define tr_block(a,b) _tr_block(tor,a,b)
int _tr_block( const tr_torrent_t * tor, int index, int begin );


typedef enum
{
    TR_RUN_CHECKING           = (1<<0), /* checking files' checksums */
    TR_RUN_RUNNING            = (1<<1), /* seeding or leeching */
    TR_RUN_STOPPING           = (1<<2), /* stopping */
    TR_RUN_STOPPING_NET_WAIT  = (1<<3), /* waiting on network -- we're
                                           telling tracker we've stopped */
    TR_RUN_STOPPED            = (1<<4)  /* stopped */
}
run_status_t;

#define TR_ID_LEN               20
#define TR_KEY_LEN              20

struct tr_torrent_s
{
    tr_handle_t * handle;
    tr_info_t info;

    tr_speedlimit_t    uploadLimitMode;
    tr_speedlimit_t    downloadLimitMode;
    tr_ratecontrol_t * upload;
    tr_ratecontrol_t * download;
    tr_ratecontrol_t * swarmspeed;

    int               error;
    char              errorString[128];
    int               hasChangedState;

    char              peer_id[TR_ID_LEN+1];
    char            * key;
    uint8_t         * azId;
    int               publicPort;

    /* An escaped string used to include the hash in HTTP queries */
    char              escapedHashString[3*SHA_DIGEST_LENGTH+1];

    /* Where to download */
    char            * destination;
    
    /* How many bytes we ask for per request */
    int               blockSize;
    int               blockCount;
    
    tr_completion_t * completion;

    volatile char     dieFlag;
    tr_bitfield_t   * uncheckedPieces;
    run_status_t      runStatus;
    cp_status_t       cpStatus;
    tr_thread_t       thread;
    tr_rwlock_t       lock;

    tr_tracker_t    * tracker;
    tr_io_t         * io;
    uint64_t          startDate;
    uint64_t          stopDate;
    char              ioLoaded;
    char              fastResumeDirty;

    int               peerCount;
    tr_peer_t       * peers[TR_MAX_PEER_COUNT];

    uint64_t          downloadedCur;
    uint64_t          downloadedPrev;
    uint64_t          uploadedCur;
    uint64_t          uploadedPrev;
    uint64_t          activityDate;

    uint8_t           pexDisabled;

    int8_t            statCur;
    tr_stat_t         stats[2];

    tr_torrent_t    * next;
};

#include "utils.h"
#include "completion.h"

struct tr_handle_s
{
    int                torrentCount;
    tr_torrent_t     * torrentList;

    char             * tag;
    int                isPortSet;

    char               useUploadLimit;
    tr_ratecontrol_t * upload;
    char               useDownloadLimit;
    tr_ratecontrol_t * download;

    tr_shared_t      * shared;

    char           key[TR_KEY_LEN+1];

    tr_handle_status_t stats[2];
    int                statCur;
#define TR_AZ_ID_LEN            20
    uint8_t        azId[TR_AZ_ID_LEN];
};

#endif
