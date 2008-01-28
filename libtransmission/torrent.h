/******************************************************************************
 * $Id:$
 *
 * Copyright (c) 2005-2008 Transmission authors and contributors
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

#ifndef TR_TORRENT_H
#define TR_TORRENT_H 1

/* just like tr_torrentSetFileDLs but doesn't trigger a fastresume save */
void tr_torrentInitFileDLs( tr_torrent   * tor,
                            int          * files,
                            int            fileCount,
                            int            do_download );

int tr_torrentIsPrivate( const tr_torrent * );

void tr_torrentRecheckCompleteness( tr_torrent * );

void tr_torrentResetTransferStats( tr_torrent * );

void tr_torrentSetHasPiece( tr_torrent * tor, int pieceIndex, int has );

void tr_torrentLock    ( const tr_torrent * );
void tr_torrentUnlock  ( const tr_torrent * );

int  tr_torrentIsSeed  ( const tr_torrent * );

void tr_torrentChangeMyPort  ( tr_torrent * );

int tr_torrentExists( tr_handle *, const uint8_t * );
tr_torrent* tr_torrentFindFromHash( tr_handle *, const uint8_t * );
tr_torrent* tr_torrentFindFromObfuscatedHash( tr_handle *, const uint8_t* );

void tr_torrentGetRates( const tr_torrent *, float * toClient, float * toPeer );

int tr_torrentAllowsPex( const tr_torrent * );

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

void tr_torrentInitFilePriority( tr_torrent   * tor,
                                 int            fileIndex,
                                 tr_priority_t  priority );


int  tr_torrentCountUncheckedPieces( const tr_torrent * );
int  tr_torrentIsPieceChecked      ( const tr_torrent *, int piece );
void tr_torrentSetPieceChecked     ( tr_torrent *, int piece, int isChecked );
void tr_torrentSetFileChecked      ( tr_torrent *, int file, int isChecked );
void tr_torrentUncheck             ( tr_torrent * );

typedef enum
{
   TR_RECHECK_NONE,
   TR_RECHECK_WAIT,
   TR_RECHECK_NOW
}
tr_recheck_state;

struct tr_torrent
{
    tr_handle                 * handle;
    tr_info                     info;

    tr_speedlimit               uploadLimitMode;
    tr_speedlimit               downloadLimitMode;
    struct tr_ratecontrol     * upload;
    struct tr_ratecontrol     * download;
    struct tr_ratecontrol     * swarmspeed;

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

    struct tr_bitfield       * checkedPieces;
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

    unsigned int               statCur : 1;
    unsigned int               isRunning : 1;

    uint16_t                   maxConnectedPeers;

    tr_recheck_state           recheckState;

    time_t                     lastStatTime;
    tr_stat                    stats[2];

    tr_torrent               * next;
};

#endif
