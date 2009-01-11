/*
 * This file Copyright (C) 2009 Charles Kerr <charles@transmissionbt.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#include <sys/types.h> /* stat */
#include <sys/stat.h> /* stat */
#include <unistd.h> /* stat */
#include <dirent.h>

#include <assert.h>
#include <limits.h> /* INT_MAX */
#include <string.h> /* memcmp */
#include <stdlib.h> /* qsort */

#include "transmission.h"
#include "session.h"
#include "bandwidth.h"
#include "bencode.h"
#include "completion.h"
#include "crypto.h" /* for tr_sha1 */
#include "resume.h"
#include "fdlimit.h" /* tr_fdFileClose */
#include "metainfo.h"
#include "peer-mgr.h"
#include "platform.h" /* TR_PATH_DELIMITER_STR */
#include "ptrarray.h"
#include "ratecontrol.h"
#include "torrent.h"
#include "tracker.h"
#include "trevent.h"
#include "utils.h"
#include "verify.h"

#define MAX_BLOCK_SIZE ( 1024 * 16 )

/***
****
***/

int
tr_torrentId( const tr_torrent * tor )
{
    return tor->uniqueId;
}

tr_torrent*
tr_torrentFindFromId( tr_session * session, int id )
{
    tr_torrent * tor = NULL;

    while( ( tor = tr_torrentNext( session, tor ) ) )
        if( tor->uniqueId == id )
            return tor;

    return NULL;
}

tr_torrent*
tr_torrentFindFromHashString( tr_session *  session, const char * str )
{
    tr_torrent * tor = NULL;

    while( ( tor = tr_torrentNext( session, tor ) ) )
        if( !strcmp( str, tor->info.hashString ) )
            return tor;

    return NULL;
}

tr_torrent*
tr_torrentFindFromHash( tr_session * session, const uint8_t * torrentHash )
{
    tr_torrent * tor = NULL;

    while( ( tor = tr_torrentNext( session, tor ) ) )
        if( *tor->info.hash == *torrentHash )
            if( !memcmp( tor->info.hash, torrentHash, SHA_DIGEST_LENGTH ) )
                return tor;

    return NULL;
}

tr_torrent*
tr_torrentFindFromObfuscatedHash( tr_session * session,
                                  const uint8_t * obfuscatedTorrentHash )
{
    tr_torrent * tor = NULL;

    while( ( tor = tr_torrentNext( session, tor ) ) )
        if( !memcmp( tor->obfuscatedHash, obfuscatedTorrentHash,
                     SHA_DIGEST_LENGTH ) )
            return tor;

    return NULL;
}

/***
****  PER-TORRENT UL / DL SPEEDS
***/

void
tr_torrentSetSpeedMode( tr_torrent *  tor,
                        tr_direction  dir,
                        tr_speedlimit mode )
{
    assert( tor != NULL );
    assert( tr_isDirection( dir ) );
    assert( mode==TR_SPEEDLIMIT_GLOBAL || mode==TR_SPEEDLIMIT_SINGLE || mode==TR_SPEEDLIMIT_UNLIMITED  );

    tor->speedLimitMode[dir] = mode;

    tr_bandwidthSetLimited( tor->bandwidth, dir, mode==TR_SPEEDLIMIT_SINGLE );
    tr_bandwidthHonorParentLimits( tor->bandwidth, dir, mode==TR_SPEEDLIMIT_GLOBAL );
}

tr_speedlimit
tr_torrentGetSpeedMode( const tr_torrent * tor,
                        tr_direction       dir )
{
    assert( tor != NULL );
    assert( tr_isDirection( dir ) );

    return tor->speedLimitMode[dir];
}

void
tr_torrentSetSpeedLimit( tr_torrent * tor,
                         tr_direction dir,
                         int          desiredSpeed )
{
    tr_bandwidthSetDesiredSpeed( tor->bandwidth, dir, desiredSpeed );
}

int
tr_torrentGetSpeedLimit( const tr_torrent * tor,
                         tr_direction       dir )
{
    return tr_bandwidthGetDesiredSpeed( tor->bandwidth, dir );
}

tr_bool
tr_torrentIsPieceTransferAllowed( const tr_torrent  * tor,
                                  tr_direction        direction )
{
    tr_bool isEnabled = FALSE;

    switch( tr_torrentGetSpeedMode( tor, direction ) )
    {
        case TR_SPEEDLIMIT_GLOBAL:
            isEnabled = !tr_sessionIsSpeedLimitEnabled( tor->session, direction )
                      || tr_sessionGetSpeedLimit( tor->session, direction ) > 0;
            break;

        case TR_SPEEDLIMIT_SINGLE:
            isEnabled = tr_torrentGetSpeedLimit( tor, direction ) > 0;
            break;

        case TR_SPEEDLIMIT_UNLIMITED:
            isEnabled = TRUE;
            break;

        default:
            assert( 0 && "unhandled speed mode" );
            break;
    }

    return isEnabled;
}

/***
****
***/

static void
onTrackerResponse( void * tracker UNUSED,
                   void *         vevent,
                   void *         user_data )
{
    tr_torrent *       tor = user_data;
    tr_tracker_event * event = vevent;

    switch( event->messageType )
    {
        case TR_TRACKER_PEERS:
        {
            size_t   i, n;
            tr_pex * pex = tr_peerMgrArrayToPex( event->compact,
                                                 event->compactLen, &n );
             if( event->allAreSeeds )
                tr_tordbg( tor, "Got %d seeds from tracker", (int)n );
            else
                tr_torinf( tor, _( "Got %d peers from tracker" ), (int)n );

            for( i = 0; i < n; ++i )
            {
                if( event->allAreSeeds )
                    pex[i].flags |= ADDED_F_SEED_FLAG;
                tr_peerMgrAddPex( tor->session->peerMgr, tor->info.hash,
                                  TR_PEER_FROM_TRACKER, pex + i );
            }

            tr_free( pex );
            break;
        }

        case TR_TRACKER_WARNING:
            tr_torerr( tor, _( "Tracker warning: \"%s\"" ), event->text );
            tor->error = -1;
            tr_strlcpy( tor->errorString, event->text,
                       sizeof( tor->errorString ) );
            break;

        case TR_TRACKER_ERROR:
            tr_torerr( tor, _( "Tracker error: \"%s\"" ), event->text );
            tor->error = -2;
            tr_strlcpy( tor->errorString, event->text,
                       sizeof( tor->errorString ) );
            break;

        case TR_TRACKER_ERROR_CLEAR:
            tor->error = 0;
            tor->errorString[0] = '\0';
            break;
    }
}

/***
****
****  TORRENT INSTANTIATION
****
***/

static int
getBytePiece( const tr_info * info,
              uint64_t        byteOffset )
{
    assert( info );
    assert( info->pieceSize != 0 );

    return byteOffset / info->pieceSize;
}

static void
initFilePieces( tr_info *       info,
                tr_file_index_t fileIndex )
{
    tr_file * file;
    uint64_t  firstByte, lastByte;

    assert( info );
    assert( fileIndex < info->fileCount );

    file = &info->files[fileIndex];
    firstByte = file->offset;
    lastByte = firstByte + ( file->length ? file->length - 1 : 0 );
    file->firstPiece = getBytePiece( info, firstByte );
    file->lastPiece = getBytePiece( info, lastByte );
}

static int
pieceHasFile( tr_piece_index_t piece,
              const tr_file *  file )
{
    return ( file->firstPiece <= piece ) && ( piece <= file->lastPiece );
}

static tr_priority_t
calculatePiecePriority( const tr_torrent * tor,
                        tr_piece_index_t   piece,
                        int                fileHint )
{
    tr_file_index_t i;
    int             priority = TR_PRI_LOW;

    /* find the first file that has data in this piece */
    if( fileHint >= 0 )
    {
        i = fileHint;
        while( i > 0 && pieceHasFile( piece, &tor->info.files[i - 1] ) )
            --i;
    }
    else
    {
        for( i = 0; i < tor->info.fileCount; ++i )
            if( pieceHasFile( piece, &tor->info.files[i] ) )
                break;
    }

    /* the piece's priority is the max of the priorities
     * of all the files in that piece */
    for( ; i < tor->info.fileCount; ++i )
    {
        const tr_file * file = &tor->info.files[i];

        if( !pieceHasFile( piece, file ) )
            break;

        priority = MAX( priority, file->priority );

        /* when dealing with multimedia files, getting the first and
           last pieces can sometimes allow you to preview it a bit
           before it's fully downloaded... */
        if( file->priority >= TR_PRI_NORMAL )
            if( file->firstPiece == piece || file->lastPiece == piece )
                priority = TR_PRI_HIGH;
    }

    return priority;
}

static void
tr_torrentInitFilePieces( tr_torrent * tor )
{
    tr_file_index_t  ff;
    tr_piece_index_t pp;
    uint64_t         offset = 0;
    tr_info *        inf = &tor->info;

    assert( inf );

    for( ff = 0; ff < inf->fileCount; ++ff )
    {
        inf->files[ff].offset = offset;
        offset += inf->files[ff].length;
        initFilePieces( inf, ff );
    }

    for( pp = 0; pp < inf->pieceCount; ++pp )
        inf->pieces[pp].priority = calculatePiecePriority( tor, pp, -1 );
}

int
tr_torrentPromoteTracker( tr_torrent * tor,
                          int          pos )
{
    int i;
    int tier;

    assert( tor );
    assert( ( 0 <= pos ) && ( pos < tor->info.trackerCount ) );

    /* the tier of the tracker we're promoting */
    tier = tor->info.trackers[pos].tier;

    /* find the index of that tier's first tracker */
    for( i = 0; i < tor->info.trackerCount; ++i )
        if( tor->info.trackers[i].tier == tier )
            break;

    assert( i < tor->info.trackerCount );

    /* promote the tracker at `pos' to the front of the tier */
    if( i != pos )
    {
        const tr_tracker_info tmp = tor->info.trackers[i];
        tor->info.trackers[i] = tor->info.trackers[pos];
        tor->info.trackers[pos] = tmp;
    }

    /* return the new position of the tracker that started out at [pos] */
    return i;
}

struct RandomTracker
{
    tr_tracker_info    tracker;
    int                random_value;
};

/* the tiers will be sorted from lowest to highest,
 * and trackers are randomized within the tiers */
static TR_INLINE int
compareRandomTracker( const void * va,
                      const void * vb )
{
    const struct RandomTracker * a = va;
    const struct RandomTracker * b = vb;

    if( a->tracker.tier != b->tracker.tier )
        return a->tracker.tier - b->tracker.tier;

    return a->random_value - b->random_value;
}

static void
randomizeTiers( tr_info * info )
{
    int                    i;
    const int              n = info->trackerCount;
    struct RandomTracker * r = tr_new0( struct RandomTracker, n );

    for( i = 0; i < n; ++i )
    {
        r[i].tracker = info->trackers[i];
        r[i].random_value = tr_cryptoRandInt( INT_MAX );
    }
    qsort( r, n, sizeof( struct RandomTracker ), compareRandomTracker );
    for( i = 0; i < n; ++i )
        info->trackers[i] = r[i].tracker;
    tr_free( r );
}

static void torrentStart( tr_torrent * tor,
                          int          reloadProgress );

/**
 * Decide on a block size.  constraints:
 * (1) most clients decline requests over 16 KiB
 * (2) pieceSize must be a multiple of block size
 */
static uint32_t
getBlockSize( uint32_t pieceSize )
{
    uint32_t b = pieceSize;

    while( b > MAX_BLOCK_SIZE )
        b /= 2u;

    if( !b || ( pieceSize % b ) ) /* not cleanly divisible */
        return 0;
    return b;
}

static void
torrentRealInit( tr_session      * session,
                 tr_torrent      * tor,
                 const tr_ctor   * ctor )
{
    int          doStart;
    uint64_t     loaded;
    uint64_t     t;
    const char * dir;
    static int   nextUniqueId = 1;
    tr_info    * info = &tor->info;

    tr_globalLock( session );

    tor->session   = session;
    tor->uniqueId = nextUniqueId++;

    randomizeTiers( info );

    tor->bandwidth = tr_bandwidthNew( session, session->bandwidth );

    tor->blockSize = getBlockSize( info->pieceSize );

    if( !tr_ctorGetDownloadDir( ctor, TR_FORCE, &dir ) ||
        !tr_ctorGetDownloadDir( ctor, TR_FALLBACK, &dir ) )
            tor->downloadDir = tr_strdup( dir );

    tor->lastPieceSize = info->totalSize % info->pieceSize;

    if( !tor->lastPieceSize )
        tor->lastPieceSize = info->pieceSize;

    tor->lastBlockSize = info->totalSize % tor->blockSize;

    if( !tor->lastBlockSize )
        tor->lastBlockSize = tor->blockSize;

    tor->blockCount =
        ( info->totalSize + tor->blockSize - 1 ) / tor->blockSize;

    tor->blockCountInPiece =
        info->pieceSize / tor->blockSize;

    tor->blockCountInLastPiece =
        ( tor->lastPieceSize + tor->blockSize - 1 ) / tor->blockSize;

    /* check our work */
    assert( ( info->pieceSize % tor->blockSize ) == 0 );
    t = info->pieceCount - 1;
    t *= info->pieceSize;
    t += tor->lastPieceSize;
    assert( t == info->totalSize );
    t = tor->blockCount - 1;
    t *= tor->blockSize;
    t += tor->lastBlockSize;
    assert( t == info->totalSize );
    t = info->pieceCount - 1;
    t *= tor->blockCountInPiece;
    t += tor->blockCountInLastPiece;
    assert( t == (uint64_t)tor->blockCount );

    tr_cpConstruct( &tor->completion, tor );

    tr_torrentInitFilePieces( tor );

    tr_rcConstruct( &tor->swarmSpeed );

    tr_sha1( tor->obfuscatedHash, "req2", 4,
             info->hash, SHA_DIGEST_LENGTH,
             NULL );

    tr_peerMgrAddTorrent( session->peerMgr, tor );

    assert( session->isPortSet );
    assert( !tor->downloadedCur );
    assert( !tor->uploadedCur );

    tor->error   = 0;

    tr_bitfieldConstruct( &tor->checkedPieces, tor->info.pieceCount );
    tr_torrentUncheck( tor );

    tor->addedDate = time( NULL ); /* this is a default value to be
                                      overwritten by the resume file */
    loaded = tr_torrentLoadResume( tor, ~0, ctor );

    doStart = tor->isRunning;
    tor->isRunning = 0;

    if( !( loaded & TR_FR_SPEEDLIMIT ) )
    {
        tr_torrentSetSpeedLimit( tor, TR_UP,
                                tr_sessionGetSpeedLimit( tor->session, TR_UP ) );
        tr_torrentSetSpeedLimit( tor, TR_DOWN,
                                tr_sessionGetSpeedLimit( tor->session,
                                                         TR_DOWN ) );
    }

    tor->completeness = tr_cpGetStatus( &tor->completion );

    tor->tracker = tr_trackerNew( tor );
    tor->trackerSubscription =
        tr_trackerSubscribe( tor->tracker, onTrackerResponse,
                             tor );

    {
        tr_torrent * it = NULL;
        tr_torrent * last = NULL;
        while( ( it = tr_torrentNext( session, it ) ) )
            last = it;

        if( !last )
            session->torrentList = tor;
        else
            last->next = tor;
        ++session->torrentCount;
    }

    tr_globalUnlock( session );

    /* maybe save our own copy of the metainfo */
    if( tr_ctorGetSave( ctor ) )
    {
        const tr_benc * val;
        if( !tr_ctorGetMetainfo( ctor, &val ) )
        {
            const char * filename = tor->info.torrent;
            tr_bencSaveFile( filename, val );
            tr_sessionSetTorrentFile( tor->session, tor->info.hashString,
                                      filename );
        }
    }

    tr_metainfoMigrate( session, &tor->info );

    if( doStart )
        torrentStart( tor, FALSE );
}

int
tr_torrentParse( const tr_session  * session,
                 const tr_ctor     * ctor,
                 tr_info           * setmeInfo )
{
    int             err = 0;
    int             doFree;
    tr_info         tmp;
    const tr_benc * metainfo;

    if( setmeInfo == NULL )
        setmeInfo = &tmp;
    memset( setmeInfo, 0, sizeof( tr_info ) );

    if( !err && tr_ctorGetMetainfo( ctor, &metainfo ) )
        return TR_EINVALID;

    err = tr_metainfoParse( session, setmeInfo, metainfo );
    doFree = !err && ( setmeInfo == &tmp );

    if( !err && !getBlockSize( setmeInfo->pieceSize ) )
        err = TR_EINVALID;

    if( !err && tr_torrentExists( session, setmeInfo->hash ) )
        err = TR_EDUPLICATE;

    if( doFree )
        tr_metainfoFree( setmeInfo );

    return err;
}

tr_torrent *
tr_torrentNew( tr_session     * session,
               const tr_ctor  * ctor,
               int            * setmeError )
{
    int          err;
    tr_info      tmpInfo;
    tr_torrent * tor = NULL;

    err = tr_torrentParse( session, ctor, &tmpInfo );
    if( !err )
    {
        tor = tr_new0( tr_torrent, 1 );
        tor->info = tmpInfo;
        torrentRealInit( session, tor, ctor );
    }
    else if( setmeError )
    {
        *setmeError = err;
    }

    return tor;
}

/**
***
**/

void
tr_torrentSetDownloadDir( tr_torrent * tor,
                          const char * path )
{
    if( !path || !tor->downloadDir || strcmp( path, tor->downloadDir ) )
    {
        tr_free( tor->downloadDir );
        tor->downloadDir = tr_strdup( path );
        tr_torrentSaveResume( tor );
    }
}

const char*
tr_torrentGetDownloadDir( const tr_torrent * tor )
{
    return tor->downloadDir;
}

void
tr_torrentChangeMyPort( tr_torrent * tor )
{
    if( tor->tracker )
        tr_trackerChangeMyPort( tor->tracker );
}

static TR_INLINE void
tr_torrentManualUpdateImpl( void * vtor )
{
    tr_torrent * tor = vtor;

    if( tor->isRunning )
        tr_trackerReannounce( tor->tracker );
}

void
tr_torrentManualUpdate( tr_torrent * tor )
{
    tr_runInEventThread( tor->session, tr_torrentManualUpdateImpl, tor );
}

int
tr_torrentCanManualUpdate( const tr_torrent * tor )
{
    return ( tor )
           && ( tor->isRunning )
           && ( tr_trackerCanManualAnnounce( tor->tracker ) );
}

const tr_info *
tr_torrentInfo( const tr_torrent * tor )
{
    return tor ? &tor->info : NULL;
}

const tr_stat *
tr_torrentStatCached( tr_torrent * tor )
{
    const time_t now = time( NULL );

    return tor && ( now == tor->lastStatTime ) ? &tor->stats
           : tr_torrentStat( tor );
}

tr_torrent_activity
tr_torrentGetActivity( tr_torrent * tor )
{
    tr_torrentRecheckCompleteness( tor );

    if( tor->verifyState == TR_VERIFY_NOW )
        return TR_STATUS_CHECK;
    if( tor->verifyState == TR_VERIFY_WAIT )
        return TR_STATUS_CHECK_WAIT;
    if( !tor->isRunning )
        return TR_STATUS_STOPPED;
    if( tor->completeness == TR_LEECH )
        return TR_STATUS_DOWNLOAD;

    return TR_STATUS_SEED;
}

const tr_stat *
tr_torrentStat( tr_torrent * tor )
{
    tr_stat *               s;
    struct tr_tracker *     tc;
    const tr_tracker_info * ti;
    int                     usableSeeds = 0;
    uint64_t                now;

    if( !tor )
        return NULL;

    tr_torrentLock( tor );

    tor->lastStatTime = time( NULL );

    s = &tor->stats;
    s->id = tor->uniqueId;
    s->activity = tr_torrentGetActivity( tor );
    s->error  = tor->error;
    memcpy( s->errorString, tor->errorString,
           sizeof( s->errorString ) );

    tc = tor->tracker;
    ti = tr_trackerGetAddress( tor->tracker, tor );
    s->announceURL = ti ? ti->announce : NULL;
    s->scrapeURL   = ti ? ti->scrape   : NULL;
    tr_trackerStat( tc, s );

    tr_trackerGetCounts( tc, &s->timesCompleted,
                             &s->leechers,
                             &s->seeders,
                             &s->downloaders );

    tr_peerMgrTorrentStats( tor->session->peerMgr,
                            tor->info.hash,
                            &s->peersKnown,
                            &s->peersConnected,
                            &usableSeeds,
                            &s->webseedsSendingToUs,
                            &s->peersSendingToUs,
                            &s->peersGettingFromUs,
                            s->peersFrom );

    now = tr_date( );
    s->swarmSpeed         = tr_rcRate( &tor->swarmSpeed, now );
    s->rawUploadSpeed     = tr_bandwidthGetRawSpeed  ( tor->bandwidth, now, TR_UP );
    s->rawDownloadSpeed   = tr_bandwidthGetRawSpeed  ( tor->bandwidth, now, TR_DOWN );
    s->pieceUploadSpeed   = tr_bandwidthGetPieceSpeed( tor->bandwidth, now, TR_UP );
    s->pieceDownloadSpeed = tr_bandwidthGetPieceSpeed( tor->bandwidth, now, TR_DOWN );

    usableSeeds += tor->info.webseedCount;

    s->percentComplete = tr_cpPercentComplete ( &tor->completion );

    s->percentDone   = tr_cpPercentDone  ( &tor->completion );
    s->leftUntilDone = tr_cpLeftUntilDone( &tor->completion );
    s->sizeWhenDone  = tr_cpSizeWhenDone ( &tor->completion );

    s->recheckProgress = s->activity == TR_STATUS_CHECK
                       ? 1.0 -
                         ( tr_torrentCountUncheckedPieces( tor ) /
                           (double) tor->info.pieceCount )
                       : 0.0;


    s->activityDate = tor->activityDate;
    s->addedDate    = tor->addedDate;
    s->doneDate     = tor->doneDate;
    s->startDate    = tor->startDate;

    s->corruptEver     = tor->corruptCur    + tor->corruptPrev;
    s->downloadedEver  = tor->downloadedCur + tor->downloadedPrev;
    s->uploadedEver    = tor->uploadedCur   + tor->uploadedPrev;
    s->haveValid       = tr_cpHaveValid( &tor->completion );
    s->haveUnchecked   = tr_cpHaveTotal( &tor->completion ) - s->haveValid;


    if( usableSeeds > 0 )
    {
        s->desiredAvailable = s->leftUntilDone;
    }
    else if( !s->leftUntilDone || !s->peersConnected )
    {
        s->desiredAvailable = 0;
    }
    else
    {
        tr_piece_index_t i;
        tr_bitfield *    peerPieces = tr_peerMgrGetAvailable(
            tor->session->peerMgr,
            tor->info.
            hash );
        s->desiredAvailable = 0;
        for( i = 0; i < tor->info.pieceCount; ++i )
            if( !tor->info.pieces[i].dnd && tr_bitfieldHas( peerPieces, i ) )
                s->desiredAvailable += tr_cpMissingBlocksInPiece( &tor->completion, i );
        s->desiredAvailable *= tor->blockSize;
        tr_bitfieldFree( peerPieces );
    }

    if( s->leftUntilDone > s->desiredAvailable )
        s->eta = TR_ETA_NOT_AVAIL;
    else if( s->pieceDownloadSpeed < 0.1 )
        s->eta = TR_ETA_UNKNOWN;
    else
        s->eta = s->leftUntilDone / s->pieceDownloadSpeed / 1024.0;

    s->ratio = tr_getRatio(
        s->uploadedEver,
        s->downloadedEver ? s->downloadedEver : s->
        haveValid );

    tr_torrentUnlock( tor );

    return s;
}

/***
****
***/

static uint64_t
fileBytesCompleted( const tr_torrent * tor,
                    tr_file_index_t    fileIndex )
{
    const tr_file *        file     =  &tor->info.files[fileIndex];
    const tr_block_index_t firstBlock       =  file->offset /
                                              tor->blockSize;
    const uint64_t         firstBlockOffset =  file->offset %
                                              tor->blockSize;
    const uint64_t         lastOffset       =
        file->length ? ( file->length - 1 ) : 0;
    const tr_block_index_t lastBlock        =
        ( file->offset + lastOffset ) / tor->blockSize;
    const uint64_t         lastBlockOffset  =
        ( file->offset + lastOffset ) % tor->blockSize;
    uint64_t               haveBytes = 0;

    assert( tor );
    assert( fileIndex < tor->info.fileCount );
    assert( file->offset + file->length <= tor->info.totalSize );
    assert( ( firstBlock < tor->blockCount )
          || ( !file->length && file->offset == tor->info.totalSize ) );
    assert( ( lastBlock < tor->blockCount )
          || ( !file->length && file->offset == tor->info.totalSize ) );
    assert( firstBlock <= lastBlock );
    assert( tr_torBlockPiece( tor, firstBlock ) == file->firstPiece );
    assert( tr_torBlockPiece( tor, lastBlock ) == file->lastPiece );

    if( firstBlock == lastBlock )
    {
        if( tr_cpBlockIsComplete( &tor->completion, firstBlock ) )
            haveBytes += lastBlockOffset + 1 - firstBlockOffset;
    }
    else
    {
        tr_block_index_t i;

        if( tr_cpBlockIsComplete( &tor->completion, firstBlock ) )
            haveBytes += tor->blockSize - firstBlockOffset;

        for( i = firstBlock + 1; i < lastBlock; ++i )
            if( tr_cpBlockIsComplete( &tor->completion, i ) )
                haveBytes += tor->blockSize;

        if( tr_cpBlockIsComplete( &tor->completion, lastBlock ) )
            haveBytes += lastBlockOffset + 1;
    }

    return haveBytes;
}

tr_file_stat *
tr_torrentFiles( const tr_torrent * tor,
                 tr_file_index_t *  fileCount )
{
    tr_file_index_t       i;
    const tr_file_index_t n = tor->info.fileCount;
    tr_file_stat *        files = tr_new0( tr_file_stat, n );
    tr_file_stat *        walk = files;

    for( i = 0; i < n; ++i, ++walk )
    {
        const uint64_t b = fileBytesCompleted( tor, i );
        walk->bytesCompleted = b;
        walk->progress = tr_getRatio( b, tor->info.files[i].length );
    }

    if( fileCount )
        *fileCount = n;

    return files;
}

void
tr_torrentFilesFree( tr_file_stat *            files,
                     tr_file_index_t fileCount UNUSED )
{
    tr_free( files );
}

/***
****
***/

float*
tr_torrentWebSpeeds( const tr_torrent * tor )
{
    return tor ? tr_peerMgrWebSpeeds( tor->session->peerMgr, tor->info.hash )
           : NULL;
}

tr_peer_stat *
tr_torrentPeers( const tr_torrent * tor,
                 int *              peerCount )
{
    tr_peer_stat * ret = NULL;

    if( tor )
        ret = tr_peerMgrPeerStats( tor->session->peerMgr,
                                   tor->info.hash, peerCount );

    return ret;
}

void
tr_torrentPeersFree( tr_peer_stat * peers,
                     int peerCount  UNUSED )
{
    tr_free( peers );
}

void
tr_torrentAvailability( const tr_torrent * tor,
                        int8_t *           tab,
                        int                size )
{
    tr_peerMgrTorrentAvailability( tor->session->peerMgr,
                                   tor->info.hash,
                                   tab, size );
}

void
tr_torrentAmountFinished( const tr_torrent * tor,
                          float *            tab,
                          int                size )
{
    tr_torrentLock( tor );
    tr_cpGetAmountDone( &tor->completion, tab, size );
    tr_torrentUnlock( tor );
}

void
tr_torrentResetTransferStats( tr_torrent * tor )
{
    tr_torrentLock( tor );

    tor->downloadedPrev += tor->downloadedCur;
    tor->downloadedCur   = 0;
    tor->uploadedPrev   += tor->uploadedCur;
    tor->uploadedCur     = 0;
    tor->corruptPrev    += tor->corruptCur;
    tor->corruptCur      = 0;

    tr_torrentUnlock( tor );
}

void
tr_torrentSetHasPiece( tr_torrent *     tor,
                       tr_piece_index_t pieceIndex,
                       tr_bool          has )
{
    tr_torrentLock( tor );

    assert( tor );
    assert( pieceIndex < tor->info.pieceCount );

    if( has )
        tr_cpPieceAdd( &tor->completion, pieceIndex );
    else
        tr_cpPieceRem( &tor->completion, pieceIndex );

    tr_torrentUnlock( tor );
}

/***
****
***/

static void
freeTorrent( tr_torrent * tor )
{
    tr_torrent * t;
    tr_session *  session = tor->session;
    tr_info *    inf = &tor->info;

    assert( tor );
    assert( !tor->isRunning );

    tr_globalLock( session );

    tr_peerMgrRemoveTorrent( session->peerMgr, tor->info.hash );

    tr_cpDestruct( &tor->completion );

    tr_rcDestruct( &tor->swarmSpeed );

    tr_trackerUnsubscribe( tor->tracker, tor->trackerSubscription );
    tr_trackerFree( tor->tracker );
    tor->tracker = NULL;

    tr_bitfieldDestruct( &tor->checkedPieces );

    tr_free( tor->downloadDir );
    tr_free( tor->peer_id );

    if( tor == session->torrentList )
        session->torrentList = tor->next;
    else for( t = session->torrentList; t != NULL; t = t->next ) {
        if( t->next == tor ) {
            t->next = tor->next;
            break;
        }
    }

    assert( session->torrentCount >= 1 );
    session->torrentCount--;

    tr_bandwidthFree( tor->bandwidth );

    tr_metainfoFree( inf );
    tr_free( tor );

    tr_globalUnlock( session );
}

/**
***  Start/Stop Callback
**/

static void
checkAndStartImpl( void * vtor )
{
    tr_torrent * tor = vtor;

    tr_globalLock( tor->session );

    tor->isRunning = 1;
    *tor->errorString = '\0';
    tr_torrentResetTransferStats( tor );
    tor->completeness = tr_cpGetStatus( &tor->completion );
    tr_torrentSaveResume( tor );
    tor->startDate = time( NULL );
    tr_trackerStart( tor->tracker );
    tr_peerMgrStartTorrent( tor->session->peerMgr, tor->info.hash );

    tr_globalUnlock( tor->session );
}

static void
checkAndStartCB( tr_torrent * tor )
{
    tr_runInEventThread( tor->session, checkAndStartImpl, tor );
}

static void
torrentStart( tr_torrent * tor,
              int          reloadProgress )
{
    tr_globalLock( tor->session );

    if( !tor->isRunning )
    {
        const int isVerifying = tr_verifyInProgress( tor );

        if( !isVerifying && reloadProgress )
            tr_torrentLoadResume( tor, TR_FR_PROGRESS, NULL );

        tor->isRunning = 1;

        if( !isVerifying )
            tr_verifyAdd( tor, checkAndStartCB );
    }

    tr_globalUnlock( tor->session );
}

void
tr_torrentStart( tr_torrent * tor )
{
    if( tor )
        torrentStart( tor, TRUE );
}

static void
torrentRecheckDoneImpl( void * vtor )
{
    tr_torrentRecheckCompleteness( vtor );
}

static void
torrentRecheckDoneCB( tr_torrent * tor )
{
    tr_runInEventThread( tor->session, torrentRecheckDoneImpl, tor );
}

void
tr_torrentVerify( tr_torrent * tor )
{
    tr_verifyRemove( tor );

    tr_globalLock( tor->session );

    tr_torrentUncheck( tor );
    tr_verifyAdd( tor, torrentRecheckDoneCB );

    tr_globalUnlock( tor->session );
}

static void
stopTorrent( void * vtor )
{
    tr_file_index_t i;

    tr_torrent *    tor = vtor;

    tr_verifyRemove( tor );
    tr_peerMgrStopTorrent( tor->session->peerMgr, tor->info.hash );
    tr_trackerStop( tor->tracker );

    for( i = 0; i < tor->info.fileCount; ++i )
    {
        const tr_file * file = &tor->info.files[i];
        char * path = tr_buildPath( tor->downloadDir, file->name, NULL );
        tr_fdFileClose( path );
        tr_free( path );
    }
}

void
tr_torrentStop( tr_torrent * tor )
{
    if( tor )
    {
        tr_globalLock( tor->session );

        tor->isRunning = 0;
        if( !tor->isDeleting )
            tr_torrentSaveResume( tor );
        tr_runInEventThread( tor->session, stopTorrent, tor );

        tr_globalUnlock( tor->session );
    }
}

static void
closeTorrent( void * vtor )
{
    tr_torrent * tor = vtor;

    tr_torrentSaveResume( tor );
    tor->isRunning = 0;
    stopTorrent( tor );
    if( tor->isDeleting )
    {
        tr_metainfoRemoveSaved( tor->session, &tor->info );
        tr_torrentRemoveResume( tor );
    }
    freeTorrent( tor );
}

void
tr_torrentFree( tr_torrent * tor )
{
    if( tor )
    {
        tr_session * session = tor->session;
        tr_globalLock( session );

        tr_torrentClearCompletenessCallback( tor );
        tr_runInEventThread( session, closeTorrent, tor );

        tr_globalUnlock( session );
    }
}

void
tr_torrentRemove( tr_torrent * tor )
{
    tor->isDeleting = 1;
    tr_torrentFree( tor );
}

/**
***  Completeness
**/

static const char *
getCompletionString( int type )
{
    switch( type )
    {
        /* Translators: this is a minor point that's safe to skip over, but FYI:
           "Complete" and "Done" are specific, different terms in Transmission:
           "Complete" means we've downloaded every file in the torrent.
           "Done" means we're done downloading the files we wanted, but NOT all
           that exist */
        case TR_PARTIAL_SEED:
            return _( "Done" );

        case TR_SEED:
            return _( "Complete" );

        default:
            return _( "Incomplete" );
    }
}

static void
fireCompletenessChange( tr_torrent       * tor,
                        tr_completeness    status )
{
    assert( tor );
    assert( ( status == TR_LEECH )
         || ( status == TR_SEED )
         || ( status == TR_PARTIAL_SEED ) );

    if( tor->completeness_func )
        tor->completeness_func( tor, status, tor->completeness_func_user_data );
}

void
tr_torrentSetCompletenessCallback( tr_torrent                    * tor,
                                   tr_torrent_completeness_func    func,
                                   void                          * user_data )
{
    assert( tor );
    tor->completeness_func = func;
    tor->completeness_func_user_data = user_data;
}

void
tr_torrentClearCompletenessCallback( tr_torrent * torrent )
{
    tr_torrentSetCompletenessCallback( torrent, NULL, NULL );
}

void
tr_torrentRecheckCompleteness( tr_torrent * tor )
{
    tr_completeness completeness;

    tr_torrentLock( tor );

    completeness = tr_cpGetStatus( &tor->completion );

    if( completeness != tor->completeness )
    {
        const int recentChange = tor->downloadedCur != 0;

        if( recentChange )
        {
            tr_torinf( tor, _( "State changed from \"%1$s\" to \"%2$s\"" ),
                      getCompletionString( tor->completeness ),
                      getCompletionString( completeness ) );
        }

        tor->completeness = completeness;
        fireCompletenessChange( tor, completeness );

        if( recentChange && ( completeness == TR_SEED ) )
        {
            tr_trackerCompleted( tor->tracker );

            tor->doneDate = time( NULL );
        }

        tr_torrentSaveResume( tor );
    }

    tr_torrentUnlock( tor );
}

/**
***  File priorities
**/

void
tr_torrentInitFilePriority( tr_torrent *    tor,
                            tr_file_index_t fileIndex,
                            tr_priority_t   priority )
{
    tr_piece_index_t i;
    tr_file *        file;

    assert( tor );
    assert( fileIndex < tor->info.fileCount );
    assert(
        priority == TR_PRI_LOW || priority == TR_PRI_NORMAL || priority ==
        TR_PRI_HIGH );

    file = &tor->info.files[fileIndex];
    file->priority = priority;
    for( i = file->firstPiece; i <= file->lastPiece; ++i )
        tor->info.pieces[i].priority = calculatePiecePriority( tor, i,
                                                               fileIndex );
}

void
tr_torrentSetFilePriorities( tr_torrent *      tor,
                             tr_file_index_t * files,
                             tr_file_index_t   fileCount,
                             tr_priority_t     priority )
{
    tr_file_index_t i;

    tr_torrentLock( tor );

    for( i = 0; i < fileCount; ++i )
        tr_torrentInitFilePriority( tor, files[i], priority );

    tr_torrentSaveResume( tor );
    tr_torrentUnlock( tor );
}

tr_priority_t
tr_torrentGetFilePriority( const tr_torrent * tor,
                           tr_file_index_t    file )
{
    tr_priority_t ret;

    tr_torrentLock( tor );
    assert( tor );
    assert( file < tor->info.fileCount );
    ret = tor->info.files[file].priority;
    tr_torrentUnlock( tor );

    return ret;
}

tr_priority_t*
tr_torrentGetFilePriorities( const tr_torrent * tor )
{
    tr_file_index_t i;
    tr_priority_t * p;

    tr_torrentLock( tor );
    p = tr_new0( tr_priority_t, tor->info.fileCount );
    for( i = 0; i < tor->info.fileCount; ++i )
        p[i] = tor->info.files[i].priority;
    tr_torrentUnlock( tor );

    return p;
}

/**
***  File DND
**/

int
tr_torrentGetFileDL( const tr_torrent * tor,
                     tr_file_index_t    file )
{
    int doDownload;

    tr_torrentLock( tor );

    assert( file < tor->info.fileCount );
    doDownload = !tor->info.files[file].dnd;

    tr_torrentUnlock( tor );
    return doDownload != 0;
}

static void
setFileDND( tr_torrent *    tor,
            tr_file_index_t fileIndex,
            int             doDownload )
{
    tr_file *        file;
    const int        dnd = !doDownload;
    tr_piece_index_t firstPiece, firstPieceDND;
    tr_piece_index_t lastPiece, lastPieceDND;
    tr_file_index_t  i;

    file = &tor->info.files[fileIndex];
    file->dnd = dnd;
    firstPiece = file->firstPiece;
    lastPiece = file->lastPiece;

    /* can't set the first piece to DND unless
       every file using that piece is DND */
    firstPieceDND = dnd;
    if( fileIndex > 0 )
    {
        for( i = fileIndex - 1; firstPieceDND; --i )
        {
            if( tor->info.files[i].lastPiece != firstPiece )
                break;
            firstPieceDND = tor->info.files[i].dnd;
            if( !i )
                break;
        }
    }

    /* can't set the last piece to DND unless
       every file using that piece is DND */
    lastPieceDND = dnd;
    for( i = fileIndex + 1; lastPieceDND && i < tor->info.fileCount; ++i )
    {
        if( tor->info.files[i].firstPiece != lastPiece )
            break;
        lastPieceDND = tor->info.files[i].dnd;
    }

    if( firstPiece == lastPiece )
    {
        tor->info.pieces[firstPiece].dnd = firstPieceDND && lastPieceDND;
    }
    else
    {
        tr_piece_index_t pp;
        tor->info.pieces[firstPiece].dnd = firstPieceDND;
        tor->info.pieces[lastPiece].dnd = lastPieceDND;
        for( pp = firstPiece + 1; pp < lastPiece; ++pp )
            tor->info.pieces[pp].dnd = dnd;
    }
}

void
tr_torrentInitFileDLs( tr_torrent *      tor,
                       tr_file_index_t * files,
                       tr_file_index_t   fileCount,
                       tr_bool           doDownload )
{
    tr_file_index_t i;

    tr_torrentLock( tor );

    for( i = 0; i < fileCount; ++i )
        setFileDND( tor, files[i], doDownload );
    tr_cpInvalidateDND ( &tor->completion );

    tr_torrentUnlock( tor );
}

void
tr_torrentSetFileDLs( tr_torrent *      tor,
                      tr_file_index_t * files,
                      tr_file_index_t   fileCount,
                      tr_bool           doDownload )
{
    tr_torrentLock( tor );
    tr_torrentInitFileDLs( tor, files, fileCount, doDownload );
    tr_torrentSaveResume( tor );
    tr_torrentUnlock( tor );
}

/***
****
***/

void
tr_torrentSetPeerLimit( tr_torrent * tor,
                        uint16_t     maxConnectedPeers )
{
    tor->maxConnectedPeers = maxConnectedPeers;
}

uint16_t
tr_torrentGetPeerLimit( const tr_torrent * tor )
{
    return tor->maxConnectedPeers;
}

/***
****
***/

tr_block_index_t
_tr_block( const tr_torrent * tor,
           tr_piece_index_t   index,
           uint32_t           offset )
{
    const tr_info *  inf = &tor->info;
    tr_block_index_t ret;

    ret = index;
    ret *= ( inf->pieceSize / tor->blockSize );
    ret += offset / tor->blockSize;
    return ret;
}

tr_bool
tr_torrentReqIsValid( const tr_torrent * tor,
                      tr_piece_index_t   index,
                      uint32_t           offset,
                      uint32_t           length )
{
    int err = 0;

    if( index >= tor->info.pieceCount )
        err = 1;
    else if( length < 1 )
        err = 2;
    else if( ( offset + length ) > tr_torPieceCountBytes( tor, index ) )
        err = 3;
    else if( length > MAX_BLOCK_SIZE )
        err = 4;
    else if( tr_pieceOffset( tor, index, offset, length ) > tor->info.totalSize )
        err = 5;

    if( err ) fprintf( stderr, "index %lu offset %lu length %lu err %d\n",
                       (unsigned long)index, (unsigned long)offset,
                       (unsigned long)length,
                       err );

    return !err;
}

uint64_t
tr_pieceOffset( const tr_torrent * tor,
                tr_piece_index_t   index,
                uint32_t           offset,
                uint32_t           length )
{
    uint64_t ret;

    ret = tor->info.pieceSize;
    ret *= index;
    ret += offset;
    ret += length;
    return ret;
}

/***
****
***/

void
tr_torrentSetPieceChecked( tr_torrent        * tor,
                           tr_piece_index_t    piece,
                           tr_bool             isChecked )
{
    if( isChecked )
        tr_bitfieldAdd( &tor->checkedPieces, piece );
    else
        tr_bitfieldRem( &tor->checkedPieces, piece );
}

void
tr_torrentSetFileChecked( tr_torrent *    tor,
                          tr_file_index_t fileIndex,
                          tr_bool         isChecked )
{
    const tr_file *        file = &tor->info.files[fileIndex];
    const tr_piece_index_t begin = file->firstPiece;
    const tr_piece_index_t end = file->lastPiece + 1;

    if( isChecked )
        tr_bitfieldAddRange ( &tor->checkedPieces, begin, end );
    else
        tr_bitfieldRemRange ( &tor->checkedPieces, begin, end );
}

tr_bool
tr_torrentIsFileChecked( const tr_torrent * tor,
                         tr_file_index_t    fileIndex )
{
    const tr_file *        file = &tor->info.files[fileIndex];
    const tr_piece_index_t begin = file->firstPiece;
    const tr_piece_index_t end = file->lastPiece + 1;
    tr_piece_index_t       i;
    tr_bool                isChecked = TRUE;

    for( i = begin; isChecked && i < end; ++i )
        if( !tr_torrentIsPieceChecked( tor, i ) )
            isChecked = FALSE;

    return isChecked;
}

void
tr_torrentUncheck( tr_torrent * tor )
{
    tr_bitfieldRemRange ( &tor->checkedPieces, 0, tor->info.pieceCount );
}

int
tr_torrentCountUncheckedPieces( const tr_torrent * tor )
{
    return tor->info.pieceCount - tr_bitfieldCountTrueBits( &tor->checkedPieces );
}

time_t*
tr_torrentGetMTimes( const tr_torrent * tor,
                     size_t *           setme_n )
{
    size_t       i;
    const size_t n = tor->info.fileCount;
    time_t *     m = tr_new0( time_t, n );

    for( i = 0; i < n; ++i )
    {
        struct stat sb;
        char * path = tr_buildPath( tor->downloadDir, tor->info.files[i].name, NULL );
        if( !stat( path, &sb ) )
        {
#ifdef SYS_DARWIN
            m[i] = sb.st_mtimespec.tv_sec;
#else
            m[i] = sb.st_mtime;
#endif
        }
        tr_free( path );
    }

    *setme_n = n;
    return m;
}

/***
****
***/

void
tr_torrentSetAnnounceList( tr_torrent *            tor,
                           const tr_tracker_info * trackers,
                           int                     trackerCount )
{
    tr_benc metainfo;

    /* save to the .torrent file */
    if( !tr_bencLoadFile( tor->info.torrent, &metainfo ) )
    {
        int       i;
        int       prevTier = -1;
        tr_benc * tier = NULL;
        tr_benc * announceList;
        tr_info   tmpInfo;

        /* remove the old fields */
        tr_bencDictRemove( &metainfo, "announce" );
        tr_bencDictRemove( &metainfo, "announce-list" );

        /* add the new fields */
        tr_bencDictAddStr( &metainfo, "announce", trackers[0].announce );
        announceList = tr_bencDictAddList( &metainfo, "announce-list", 0 );
        for( i = 0; i < trackerCount; ++i ) {
            if( prevTier != trackers[i].tier ) {
                prevTier = trackers[i].tier;
                tier = tr_bencListAddList( announceList, 0 );
            }
            tr_bencListAddStr( tier, trackers[i].announce );
        }

        /* try to parse it back again, to make sure it's good */
        memset( &tmpInfo, 0, sizeof( tr_info ) );
        if( !tr_metainfoParse( tor->session, &tmpInfo, &metainfo ) )
        {
            /* it's good, so keep these new trackers and free the old ones */

            tr_info swap;
            swap.trackers = tor->info.trackers;
            swap.trackerCount = tor->info.trackerCount;
            tor->info.trackers = tmpInfo.trackers;
            tor->info.trackerCount = tmpInfo.trackerCount;
            tmpInfo.trackers = swap.trackers;
            tmpInfo.trackerCount = swap.trackerCount;

            tr_metainfoFree( &tmpInfo );
            tr_bencSaveFile( tor->info.torrent, &metainfo );
        }

        /* cleanup */
        tr_bencFree( &metainfo );
    }
}

/**
***
**/

/** @deprecated this method will be removed in 1.40 */
void
tr_torrentSetAddedDate( tr_torrent * tor,
                        time_t       t )
{
    tor->addedDate = t;
}

/** @deprecated this method will be removed in 1.40 */
void
tr_torrentSetActivityDate( tr_torrent * tor,
                           time_t       t )
{
    tor->activityDate = t;
}

/** @deprecated this method will be removed in 1.40 */
void
tr_torrentSetDoneDate( tr_torrent * tor,
                       time_t       t )
{
    tor->doneDate = t;
}

/**
***
**/

uint64_t
tr_torrentGetBytesLeftToAllocate( const tr_torrent * tor )
{
    const tr_file * it;
    const tr_file * end;
    struct stat sb;
    uint64_t bytesLeft = 0;

    for( it=tor->info.files, end=it+tor->info.fileCount; it!=end; ++it )
    {
        if( !it->dnd )
        {
            char * path = tr_buildPath( tor->downloadDir, it->name, NULL );

            bytesLeft += it->length;

            if( !stat( path, &sb )
                    && S_ISREG( sb.st_mode )
                    && ( (uint64_t)sb.st_size <= it->length ) )
                bytesLeft -= sb.st_size;

            tr_free( path );
        }
    }

    return bytesLeft;
}

/****
*****  Removing the torrent's local data
****/

static int
vstrcmp( const void * a, const void * b )
{
    return strcmp( a, b );
}

static int
compareLongestFirst( const void * a, const void * b )
{
    const size_t alen = strlen( a );
    const size_t blen = strlen( b );

    if( alen != blen )
        return alen > blen ? -1 : 1;

    return vstrcmp( a, b );
}

static void
addDirtyFile( const char  * root,
              const char  * filename,
              tr_ptrArray * dirtyFolders )
{
    char * dir = tr_dirname( filename );

    /* add the parent folders to dirtyFolders until we reach the root or a known-dirty */
    while (     ( dir != NULL )
             && ( strlen( root ) <= strlen( dir ) )
             && ( tr_ptrArrayFindSorted( dirtyFolders, dir, vstrcmp ) == NULL ) )
    {
        char * tmp;
        tr_ptrArrayInsertSorted( dirtyFolders, tr_strdup( dir ), vstrcmp );

        tmp = tr_dirname( dir );
        tr_free( dir );
        dir = tmp;
    }

    tr_free( dir );
}

static void
walkLocalData( const tr_torrent * tor,
               const char       * root,
               const char       * dir,
               const char       * base,
               tr_ptrArray      * torrentFiles,
               tr_ptrArray      * folders,
               tr_ptrArray      * dirtyFolders )
{
    int i;
    struct stat sb;
    char * buf;

    buf = tr_buildPath( dir, base, NULL );
    i = stat( buf, &sb );
    if( !i )
    {
        DIR * odir = NULL;

        if( S_ISDIR( sb.st_mode ) && ( ( odir = opendir ( buf ) ) ) )
        {
            struct dirent *d;
            tr_ptrArrayInsertSorted( folders, tr_strdup( buf ), vstrcmp );
            for( d = readdir( odir ); d != NULL; d = readdir( odir ) )
                if( d->d_name && d->d_name[0] != '.' ) /* skip dotfiles */
                    walkLocalData( tor, root, buf, d->d_name, torrentFiles, folders, dirtyFolders );
            closedir( odir );
        }
        else if( S_ISREG( sb.st_mode ) && ( sb.st_size > 0 ) )
        {
            const char * sub = buf + strlen( tor->downloadDir ) + strlen( TR_PATH_DELIMITER_STR );
            const tr_bool isTorrentFile = tr_ptrArrayFindSorted( torrentFiles, sub, vstrcmp ) != NULL;
            if( !isTorrentFile )
                addDirtyFile( root, buf, dirtyFolders );
        }
    }

    tr_free( buf );
}

static void
deleteLocalData( tr_torrent * tor, tr_fileFunc fileFunc )
{
    int i, n;
    char ** s;
    tr_file_index_t f;
    tr_ptrArray torrentFiles = TR_PTR_ARRAY_INIT;
    tr_ptrArray folders      = TR_PTR_ARRAY_INIT;
    tr_ptrArray dirtyFolders = TR_PTR_ARRAY_INIT; /* dirty == contains non-torrent files */

    const char * firstFile = tor->info.files[0].name;
    const char * cpch = strchr( firstFile, TR_PATH_DELIMITER );
    char * tmp = cpch ? tr_strndup( firstFile, cpch - firstFile ) : NULL;
    char * root = tr_buildPath( tor->downloadDir, tmp, NULL );

    for( f=0; f<tor->info.fileCount; ++f )
        tr_ptrArrayInsertSorted( &torrentFiles, tor->info.files[f].name, vstrcmp );

    /* build the set of folders and dirtyFolders */
    walkLocalData( tor, root, root, NULL, &torrentFiles, &folders, &dirtyFolders );

    /* close all the files because we're about to delete them */
    for( f=0; f<tor->info.fileCount; ++f ) {
        char * path = tr_buildPath( tor->downloadDir, tor->info.files[f].name, NULL );
        tr_fdFileClose( path );
        tr_free( path );
    }

    /* try to remove entire folders first, so that the recycle bin will be tidy */
    s = (char**) tr_ptrArrayPeek( &folders, &n );
    for( i=0; i<n; ++i )
        if( tr_ptrArrayFindSorted( &dirtyFolders, s[i], vstrcmp ) == NULL )
            fileFunc( s[i] );

    /* now blow away any remaining torrent files, such as torrent files in dirty folders */
    for( f=0; f<tor->info.fileCount; ++f ) {
        char * path = tr_buildPath( tor->downloadDir, tor->info.files[f].name, NULL );
        fileFunc( path );
        tr_free( path );
    }

    /* Now clean out the directories left empty from the previous step.
     * Work from deepest to shallowest s.t. lower folders
     * won't prevent the upper folders from being deleted */
    {
        tr_ptrArray cleanFolders = TR_PTR_ARRAY_INIT;
        s = (char**) tr_ptrArrayPeek( &folders, &n );
        for( i=0; i<n; ++i )
            if( tr_ptrArrayFindSorted( &dirtyFolders, s[i], vstrcmp ) == NULL )
                tr_ptrArrayInsertSorted( &cleanFolders, s[i], compareLongestFirst );
        s = (char**) tr_ptrArrayPeek( &cleanFolders, &n );
        for( i=0; i<n; ++i )
            fileFunc( s[i] );
        tr_ptrArrayDestruct( &cleanFolders, NULL );
    }

    /* cleanup */
    tr_ptrArrayDestruct( &dirtyFolders, tr_free );
    tr_ptrArrayDestruct( &folders, tr_free );
    tr_ptrArrayDestruct( &torrentFiles, NULL );
    tr_free( root );
    tr_free( tmp );
}

void
tr_torrentDeleteLocalData( tr_torrent * tor, tr_fileFunc fileFunc )
{
    if( fileFunc == NULL )
        fileFunc = unlink;

    if( tor->info.fileCount > 1 )
        deleteLocalData( tor, fileFunc );
    else {
        /* torrent only has one file */
        char * path = tr_buildPath( tor->downloadDir, tor->info.files[0].name, NULL );
        tr_fdFileClose( path );
        fileFunc( path );
        tr_free( path );
    }
}
