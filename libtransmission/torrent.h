/*
 * This file Copyright (C) 2009 Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#ifndef __TRANSMISSION__
 #error only libtransmission should #include this header.
#endif

#ifndef TR_TORRENT_H
#define TR_TORRENT_H 1

#include "completion.h" /* tr_completion */
#include "session.h" /* tr_globalLock(), tr_globalUnlock() */
#include "utils.h" /* TR_GNUC_PRINTF */

struct tr_bandwidth;
struct tr_torrent_tiers;
struct tr_magnet_info;

/**
***  Package-visible ctor API
**/

void        tr_ctorSetSave( tr_ctor * ctor,
                            tr_bool   saveMetadataInOurTorrentsDir );

int         tr_ctorGetSave( const tr_ctor * ctor );

int         tr_ctorGetMagnet( const tr_ctor * ctor, const struct tr_magnet_info ** setme );

void        tr_ctorInitTorrentPriorities( const tr_ctor * ctor, tr_torrent * tor );

void        tr_ctorInitTorrentWanted( const tr_ctor * ctor, tr_torrent * tor );

/**
***
**/

/* just like tr_torrentSetFileDLs but doesn't trigger a fastresume save */
void        tr_torrentInitFileDLs( tr_torrent *      tor,
                                   tr_file_index_t * files,
                                   tr_file_index_t   fileCount,
                                   tr_bool           do_download );

void        tr_torrentRecheckCompleteness( tr_torrent * );

void        tr_torrentSetHasPiece( tr_torrent *     tor,
                                   tr_piece_index_t pieceIndex,
                                   tr_bool          has );

void        tr_torrentChangeMyPort( tr_torrent * session );

tr_torrent* tr_torrentFindFromHashString( tr_session * session,
                                          const char * hashString );

tr_torrent* tr_torrentFindFromObfuscatedHash( tr_session    * session,
                                              const uint8_t * hash );

tr_bool     tr_torrentIsPieceTransferAllowed( const tr_torrent * torrent,
                                              tr_direction       direction );



#define tr_block( a, b ) _tr_block( tor, a, b )
tr_block_index_t _tr_block( const tr_torrent * tor,
                            tr_piece_index_t   index,
                            uint32_t           offset );

tr_bool          tr_torrentReqIsValid( const tr_torrent * tor,
                                       tr_piece_index_t   index,
                                       uint32_t           offset,
                                       uint32_t           length );

uint64_t         tr_pieceOffset( const tr_torrent * tor,
                                 tr_piece_index_t   index,
                                 uint32_t           offset,
                                 uint32_t           length );

void             tr_torrentInitFilePriority( tr_torrent       * tor,
                                             tr_file_index_t    fileIndex,
                                             tr_priority_t      priority );

int              tr_torrentCountUncheckedPieces( const tr_torrent * );

tr_bool          tr_torrentIsFileChecked( const tr_torrent  * tor,
                                          tr_file_index_t     file );

void             tr_torrentSetPieceChecked( tr_torrent       * tor,
                                            tr_piece_index_t   piece,
                                            tr_bool            isChecked );

void             tr_torrentSetFileChecked( tr_torrent       * tor,
                                           tr_file_index_t    file,
                                           tr_bool            isChecked );

void             tr_torrentUncheck( tr_torrent * tor );

int              tr_torrentPromoteTracker( tr_torrent   * tor,
                                           int            trackerIndex );

time_t*          tr_torrentGetMTimes( const tr_torrent  * tor,
                                      size_t            * setmeCount );

tr_torrent*      tr_torrentNext( tr_session  * session,
                                 tr_torrent  * current );

void             tr_torrentCheckSeedRatio( tr_torrent * tor );

/** save a torrent's .resume file if it's changed since the last time it was saved */
void             tr_torrentSave( tr_torrent * tor );

void             tr_torrentSetLocalError( tr_torrent * tor, const char * fmt, ... ) TR_GNUC_PRINTF( 2, 3 );



typedef enum
{
    TR_VERIFY_NONE,
    TR_VERIFY_WAIT,
    TR_VERIFY_NOW
}
tr_verify_state;

void             tr_torrentSetVerifyState( tr_torrent      * tor,
                                           tr_verify_state   state );

struct tr_incomplete_metadata;

struct tr_torrent
{
    tr_session *             session;
    tr_info                  info;

    int                      magicNumber;

    tr_stat_errtype          error;
    char                     errorString[128];

    uint8_t                  obfuscatedHash[SHA_DIGEST_LENGTH];

    /* Used when the torrent has been created with a magnet link
     * and we're in the process of downloading the metainfo from
     * other peers */
    struct tr_incomplete_metadata  * incompleteMetadata;

    /* If the initiator of the connection receives a handshake in which the
     * peer_id does not match the expected peerid, then the initiator is
     * expected to drop the connection. Note that the initiator presumably
     * received the peer information from the tracker, which includes the
     * peer_id that was registered by the peer. The peer_id from the tracker
     * and in the handshake are expected to match.
     */
    uint8_t * peer_id;

    /* Where the files will be when it's complete */
    char * downloadDir;

    /* Where the files are when the torrent is incomplete */
    char * incompleteDir;

    /* Length, in bytes, of the "info" dict in the .torrent file */
    int infoDictLength;

    /* Offset, in bytes, of the beginning of the "info" dict in the .torrent file */
    int infoDictOffset;

    /* Where the files are now.
     * This pointer will be equal to downloadDir or incompleteDir */
    const char * currentDir;

    /* How many bytes we ask for per request */
    uint32_t                   blockSize;
    tr_block_index_t           blockCount;

    uint32_t                   lastBlockSize;
    uint32_t                   lastPieceSize;

    uint32_t                   blockCountInPiece;
    uint32_t                   blockCountInLastPiece;

    struct tr_completion       completion;

    struct tr_bitfield         checkedPieces;
    tr_completeness            completeness;

    struct tr_torrent_tiers  * tiers;
    struct tr_publisher_tag  * tiersSubscription;

    time_t                     dhtAnnounceAt;
    time_t                     dhtAnnounce6At;
    tr_bool                    dhtAnnounceInProgress;
    tr_bool                    dhtAnnounce6InProgress;

    uint64_t                   downloadedCur;
    uint64_t                   downloadedPrev;
    uint64_t                   uploadedCur;
    uint64_t                   uploadedPrev;
    uint64_t                   corruptCur;
    uint64_t                   corruptPrev;

    uint64_t                   etaDLSpeedCalculatedAt;
    double                     etaDLSpeed;
    uint64_t                   etaULSpeedCalculatedAt;
    double                     etaULSpeed;

    time_t                     addedDate;
    time_t                     activityDate;
    time_t                     doneDate;
    time_t                     startDate;
    time_t                     anyDate;

    tr_torrent_metadata_func  * metadata_func;
    void                      * metadata_func_user_data;

    tr_torrent_completeness_func  * completeness_func;
    void                          *  completeness_func_user_data;

    tr_torrent_ratio_limit_hit_func  * ratio_limit_hit_func;
    void                             * ratio_limit_hit_func_user_data;

    tr_bool                    isRunning;
    tr_bool                    isDeleting;
    tr_bool                    needsSeedRatioCheck;
    tr_bool                    startAfterVerify;
    tr_bool                    isDirty;

    uint16_t                   maxConnectedPeers;

    tr_verify_state            verifyState;

    time_t                     lastStatTime;
    tr_stat                    stats;

    tr_torrent *               next;

    int                        uniqueId;

    struct tr_bandwidth      * bandwidth;

    struct tr_torrent_peers  * torrentPeers;

    double                     desiredRatio;
    tr_ratiolimit              ratioLimitMode;

    uint64_t                   preVerifyTotal;
};

/* get the index of this piece's first block */
static TR_INLINE tr_block_index_t
tr_torPieceFirstBlock( const tr_torrent * tor, const tr_piece_index_t piece )
{
    return piece * tor->blockCountInPiece;
}

/* what piece index is this block in? */
static TR_INLINE tr_piece_index_t
tr_torBlockPiece( const tr_torrent * tor, const tr_block_index_t block )
{
    return block / tor->blockCountInPiece;
}

/* how many blocks are in this piece? */
static TR_INLINE uint32_t
tr_torPieceCountBlocks( const tr_torrent * tor, const tr_piece_index_t piece )
{
    return piece == tor->info.pieceCount - 1 ? tor->blockCountInLastPiece
                                             : tor->blockCountInPiece;
}

/* how many bytes are in this piece? */
static TR_INLINE uint32_t
tr_torPieceCountBytes( const tr_torrent * tor, const tr_piece_index_t piece )
{
    return piece == tor->info.pieceCount - 1 ? tor->lastPieceSize
                                             : tor->info.pieceSize;
}

/* how many bytes are in this block? */
static TR_INLINE uint32_t
tr_torBlockCountBytes( const tr_torrent * tor, const tr_block_index_t block )
{
    return block == tor->blockCount - 1 ? tor->lastBlockSize
                                        : tor->blockSize;
}

static TR_INLINE void tr_torrentLock( const tr_torrent * tor )
{
    tr_globalLock( tor->session );
}

static TR_INLINE void tr_torrentUnlock( const tr_torrent * tor )
{
    tr_globalUnlock( tor->session );
}

static TR_INLINE tr_bool
tr_torrentExists( const tr_session * session, const uint8_t *   torrentHash )
{
    return tr_torrentFindFromHash( (tr_session*)session, torrentHash ) != NULL;
}

static TR_INLINE tr_bool
tr_torrentIsSeed( const tr_torrent * tor )
{
    return tor->completeness != TR_LEECH;
}

static TR_INLINE tr_bool tr_torrentIsPrivate( const tr_torrent * tor )
{
    return ( tor != NULL ) && tor->info.isPrivate;
}

static TR_INLINE tr_bool tr_torrentAllowsPex( const tr_torrent * tor )
{
    return ( tor != NULL )
        && ( tor->session->isPexEnabled )
        && ( !tr_torrentIsPrivate( tor ) );
}

static TR_INLINE tr_bool tr_torrentAllowsDHT( const tr_torrent * tor )
{
    return ( tor != NULL )
        && ( tr_sessionAllowsDHT( tor->session ) )
        && ( !tr_torrentIsPrivate( tor ) );
}

static TR_INLINE tr_bool tr_torrentIsPieceChecked( const tr_torrent  * tor,
                                                   tr_piece_index_t    i )
{
    return tr_bitfieldHasFast( &tor->checkedPieces, i );
}

/***
****
***/

enum
{
    TORRENT_MAGIC_NUMBER = 95549
};

static TR_INLINE tr_bool tr_isTorrent( const tr_torrent * tor )
{
    return ( tor != NULL )
        && ( tor->magicNumber == TORRENT_MAGIC_NUMBER )
        && ( tr_isSession( tor->session ) );
}

/* set a flag indicating that the torrent's .resume file
 * needs to be saved when the torrent is closed */
static TR_INLINE
void tr_torrentSetDirty( tr_torrent * tor )
{
    assert( tr_isTorrent( tor ) );

    tor->isDirty = TRUE;
}

static TR_INLINE
const char * tr_torrentName( const tr_torrent * tor )
{
    assert( tr_isTorrent( tor ) );

    return tor->info.name;
}

/**
 * Tell the tr_torrent that one of its files has become complete
 */
void tr_torrentFileCompleted( tr_torrent * tor, tr_file_index_t fileNo );


/**
 * @brief Like tr_torrentFindFile(), but splits the filename into base and subpath;
 *
 * If the file is found, "tr_buildPath( base, subpath, NULL )"
 * will generate the complete filename.
 *
 * @return true if the file is found, false otherwise.
 *
 * @param base if the torrent is found, this will be either
 *             tor->downloadDir or tor->incompleteDir
 * @param subpath on success, this pointer is assigned a newly-allocated
 *                string holding the second half of the filename.
 */
tr_bool tr_torrentFindFile2( const tr_torrent *, tr_file_index_t fileNo,
                             const char ** base, char ** subpath );


/* Returns a newly-allocated version of the tr_file.name string
 * that's been modified to denote that it's not a complete file yet.
 * In the current implementation this is done by appending ".part"
 * a la Firefox. */
char* tr_torrentBuildPartial( const tr_torrent *, tr_file_index_t fileNo );

/* for when the info dict has been fundamentally changed wrt files,
 * piece size, etc. such as in BEP 9 where peers exchange metadata */
void tr_torrentGotNewInfoDict( tr_torrent * tor );

#endif
