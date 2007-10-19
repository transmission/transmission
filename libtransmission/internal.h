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

#define TR_NAME "Transmission"

#ifdef __GNUC__
#define UNUSED __attribute__((unused))
#else
#define UNUSED
#endif

typedef enum { TR_NET_OK, TR_NET_ERROR, TR_NET_WAIT } tr_tristate_t;

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif


void tr_torrentRecheckCompleteness( tr_torrent * );

int tr_trackerInfoInit( struct tr_tracker_info  * info,
                        const char              * address,
                        int                       address_len );

void tr_trackerInfoClear( struct tr_tracker_info * info );

void tr_peerIdNew ( char* buf, int buflen );

void tr_torrentResetTransferStats( tr_torrent * );

void tr_torrentSetHasPiece( tr_torrent * tor, int pieceIndex, int has );

void tr_torrentLock    ( const tr_torrent * );
void tr_torrentUnlock  ( const tr_torrent * );

int  tr_torrentIsSeed  ( const tr_torrent * );

void tr_torrentChangeMyPort  ( tr_torrent * );

int tr_torrentExists( tr_handle *, const uint8_t * );
tr_torrent* tr_torrentFindFromHash( tr_handle *, const uint8_t * );
tr_torrent* tr_torrentFindFromObfuscatedHash( tr_handle *, const uint8_t* );

/* get the index of this piece's first block */
#define tr_torPieceFirstBlock(tor,piece) ( (piece) * (tor)->blockCountInPiece )

/* what piece index is this block in? */
#define tr_torBlockPiece(tor,block) ( (block) / (tor)->blockCountInPiece )

/* how many blocks are in this piece? */
#define tr_torPieceCountBlocks(tor,piece) \
    ( ((piece)==((tor)->info.pieceCount-1)) ? (tor)->blockCountInLastPiece : (tor)->blockCountInPiece )

/* how many bytes are in this piece? */
#define tr_torPieceCountBytes(tor,piece) \
    ( ((piece)==((tor)->info.pieceCount-1)) ? (tor)->lastPieceSize : (tor)->info.pieceSize )

/* how many bytes are in this block? */
#define tr_torBlockCountBytes(tor,block) \
    ( ((block)==((tor)->blockCount-1)) ? (tor)->lastBlockSize : (tor)->blockSize )

#define tr_block(a,b) _tr_block(tor,a,b)
int _tr_block( const tr_torrent * tor, int index, int begin );

uint64_t tr_pieceOffset( const tr_torrent * tor, int index, int begin, int length );

typedef enum
{
   TR_RECHECK_NONE,
   TR_RECHECK_WAIT,
   TR_RECHECK_NOW
}
tr_recheck_state;

#define TR_ID_LEN  20

struct tr_torrent
{
    tr_handle                 * handle;
    tr_info                     info;

    tr_speedlimit               uploadLimitMode;
    tr_speedlimit               downloadLimitMode;
    struct tr_ratecontrol     * upload;
    struct tr_ratecontrol     * download;
    struct tr_ratecontrol     * swarmspeed;

    struct tr_timer           * saveTimer;

    int                        error;
    char                       errorString[128];

    uint8_t                    obfuscatedHash[SHA_DIGEST_LENGTH];

    /* Where to download */
    char                     * destination;
    
    /* How many bytes we ask for per request */
    int                        blockSize;
    int                        blockCount;

    int                        lastBlockSize;
    int                        lastPieceSize;

    int                        blockCountInPiece;
    int                        blockCountInLastPiece;
    
    struct tr_completion     * completion;

    struct tr_bitfield       * uncheckedPieces;
    cp_status_t                cpStatus;

    struct tr_tracker        * tracker;
    struct tr_publisher_tag  * trackerSubscription;

    uint64_t                   downloadedCur;
    uint64_t                   downloadedPrev;
    uint64_t                   uploadedCur;
    uint64_t                   uploadedPrev;
    uint64_t                   corruptCur;
    uint64_t                   corruptPrev;

    uint64_t                   startDate;
    uint64_t                   stopDate;
    uint64_t                   activityDate;

    tr_torrent_status_func   * status_func;
    void                     * status_func_user_data;

    unsigned int               pexDisabled : 1;
    unsigned int               statCur : 1;
    unsigned int               isRunning : 1;

    tr_recheck_state           recheckState;

    tr_stat                    stats[2];

    tr_torrent               * next;
};

struct tr_handle
{
    tr_encryption_mode         encryptionMode;

    struct tr_event_handle   * events;

    int                        torrentCount;
    tr_torrent               * torrentList;

    char                     * tag;
    int                        isPortSet;

    char                       useUploadLimit;
    char                       useDownloadLimit;
    struct tr_ratecontrol    * upload;
    struct tr_ratecontrol    * download;

    struct tr_peerMgr        * peerMgr;
    struct tr_shared         * shared;

    struct tr_lock           * lock;

    tr_handle_status           stats[2];
    int                        statCur;

    uint8_t                    isClosed;

#define TR_AZ_ID_LEN 20
    uint8_t                    azId[TR_AZ_ID_LEN];
};

void tr_globalLock       ( struct tr_handle * );
void tr_globalUnlock     ( struct tr_handle * );
int  tr_globalIsLocked   ( const struct tr_handle * );


#endif
