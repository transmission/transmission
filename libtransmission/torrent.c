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
#include <math.h> /* fabs */
#include <string.h> /* memcmp */
#include <stdlib.h> /* qsort */

#include <event.h> /* evbuffer */

#include "transmission.h"
#include "session.h"
#include "bandwidth.h"
#include "bencode.h"
#include "completion.h"
#include "crypto.h" /* for tr_sha1 */
#include "resume.h"
#include "fdlimit.h" /* tr_fdTorrentClose */
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

    while(( tor = tr_torrentNext( session, tor )))
        if( tor->uniqueId == id )
            return tor;

    return NULL;
}

tr_torrent*
tr_torrentFindFromHashString( tr_session *  session, const char * str )
{
    tr_torrent * tor = NULL;

    while(( tor = tr_torrentNext( session, tor )))
        if( !strcmp( str, tor->info.hashString ) )
            return tor;

    return NULL;
}

tr_torrent*
tr_torrentFindFromHash( tr_session * session, const uint8_t * torrentHash )
{
    tr_torrent * tor = NULL;

    while(( tor = tr_torrentNext( session, tor )))
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

    while(( tor = tr_torrentNext( session, tor )))
        if( !memcmp( tor->obfuscatedHash, obfuscatedTorrentHash,
                     SHA_DIGEST_LENGTH ) )
            return tor;

    return NULL;
}

/***
****  PER-TORRENT UL / DL SPEEDS
***/

void
tr_torrentSetSpeedLimit( tr_torrent * tor, tr_direction dir, int KiB_sec )
{
    assert( tr_isTorrent( tor ) );
    assert( tr_isDirection( dir ) );

    tr_bandwidthSetDesiredSpeed( tor->bandwidth, dir, KiB_sec );

    tr_torrentSetDirty( tor );
}

int
tr_torrentGetSpeedLimit( const tr_torrent * tor, tr_direction dir )
{
    assert( tr_isTorrent( tor ) );
    assert( tr_isDirection( dir ) );

    return tr_bandwidthGetDesiredSpeed( tor->bandwidth, dir );
}

void
tr_torrentUseSpeedLimit( tr_torrent * tor, tr_direction dir, tr_bool do_use )
{
    assert( tr_isTorrent( tor ) );
    assert( tr_isDirection( dir ) );

    tr_bandwidthSetLimited( tor->bandwidth, dir, do_use );

    tr_torrentSetDirty( tor );
}

tr_bool
tr_torrentUsesSpeedLimit( const tr_torrent * tor, tr_direction dir )
{
    assert( tr_isTorrent( tor ) );
    assert( tr_isDirection( dir ) );

    return tr_bandwidthIsLimited( tor->bandwidth, dir );
}

void
tr_torrentUseSessionLimits( tr_torrent * tor, tr_bool doUse )
{
    assert( tr_isTorrent( tor ) );

    tr_bandwidthHonorParentLimits( tor->bandwidth, TR_UP, doUse );
    tr_bandwidthHonorParentLimits( tor->bandwidth, TR_DOWN, doUse );

    tr_torrentSetDirty( tor );
}

tr_bool
tr_torrentUsesSessionLimits( const tr_torrent * tor )
{
    assert( tr_isTorrent( tor ) );

    return tr_bandwidthAreParentLimitsHonored( tor->bandwidth, TR_UP );
}

/***
****
***/

void
tr_torrentSetRatioMode( tr_torrent *  tor, tr_ratiolimit mode )
{
    assert( tr_isTorrent( tor ) );
    assert( mode==TR_RATIOLIMIT_GLOBAL || mode==TR_RATIOLIMIT_SINGLE || mode==TR_RATIOLIMIT_UNLIMITED  );

    tor->ratioLimitMode = mode;
    tor->needsSeedRatioCheck = TRUE;

    tr_torrentSetDirty( tor );
}

tr_ratiolimit
tr_torrentGetRatioMode( const tr_torrent * tor )
{
    assert( tr_isTorrent( tor ) );

    return tor->ratioLimitMode;
}

void
tr_torrentSetRatioLimit( tr_torrent * tor, double desiredRatio )
{
    assert( tr_isTorrent( tor ) );

    tor->desiredRatio = desiredRatio;

    tor->needsSeedRatioCheck = TRUE;

    tr_torrentSetDirty( tor );
}

double
tr_torrentGetRatioLimit( const tr_torrent * tor )
{
    assert( tr_isTorrent( tor ) );

    return tor->desiredRatio;
}

tr_bool
tr_torrentIsPieceTransferAllowed( const tr_torrent  * tor,
                                  tr_direction        direction )
{
    int limit;
    tr_bool allowed = TRUE;

    if( tr_torrentUsesSpeedLimit( tor, direction ) )
        if( tr_torrentGetSpeedLimit( tor, direction ) <= 0 )
            allowed = FALSE;

    if( tr_torrentUsesSessionLimits( tor ) )
        if( tr_sessionGetActiveSpeedLimit( tor->session, direction, &limit ) )
            if( limit <= 0 )
                allowed = FALSE;

    return allowed;
}

tr_bool
tr_torrentGetSeedRatio( const tr_torrent * tor, double * ratio )
{
    tr_bool isLimited;

    switch( tr_torrentGetRatioMode( tor ) )
    {
        case TR_RATIOLIMIT_SINGLE:
            isLimited = TRUE;
            if( ratio )
                *ratio = tr_torrentGetRatioLimit( tor );
            break;

        case TR_RATIOLIMIT_GLOBAL:
            isLimited = tr_sessionIsRatioLimited( tor->session );
            if( isLimited && ratio )
                *ratio = tr_sessionGetRatioLimit( tor->session );
            break;

        default: /* TR_RATIOLIMIT_UNLIMITED */
            isLimited = FALSE;
            break;
    }

    return isLimited;
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
                tr_peerMgrAddPex( tor, TR_PEER_FROM_TRACKER, pex + i );
            }

            tr_free( pex );
            break;
        }

        case TR_TRACKER_WARNING:
            tr_torerr( tor, _( "Tracker warning: \"%s\"" ), event->text );
            tor->error = TR_STAT_TRACKER_WARNING;
            tr_strlcpy( tor->errorString, event->text, sizeof( tor->errorString ) );
            break;

        case TR_TRACKER_ERROR:
            tr_torerr( tor, _( "Tracker error: \"%s\"" ), event->text );
            tor->error = TR_STAT_TRACKER_ERROR;
            tr_strlcpy( tor->errorString, event->text, sizeof( tor->errorString ) );
            break;

        case TR_TRACKER_ERROR_CLEAR:
            tor->error = TR_STAT_OK;
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
    if( fileHint >= 0 ) {
        i = fileHint;
        while( i > 0 && pieceHasFile( piece, &tor->info.files[i - 1] ) )
            --i;
    } else {
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
    tr_file_index_t  f;
    tr_piece_index_t p;
    uint64_t offset = 0;
    tr_info * inf = &tor->info;
    int * firstFiles;

    /* assign the file offsets */
    for( f=0; f<inf->fileCount; ++f ) {
        inf->files[f].offset = offset;
        offset += inf->files[f].length;
        initFilePieces( inf, f );
    }

    /* build the array of first-file hints to give calculatePiecePriority */
    firstFiles = tr_new( int, inf->pieceCount );
    for( p=f=0; p<inf->pieceCount; ++p ) {
        while( inf->files[f].lastPiece < p )
            ++f;
        firstFiles[p] = f;
    }

#if 0
    /* test to confirm the first-file hints are correct */
    for( p=0; p<inf->pieceCount; ++p ) {
        f = firstFiles[p];
        assert( inf->files[f].firstPiece <= p );
        assert( inf->files[f].lastPiece >= p );
        if( f > 0 )
            assert( inf->files[f-1].lastPiece < p );
        for( f=0; f<inf->fileCount; ++f )
            if( pieceHasFile( p, &inf->files[f] ) )
                break;
        assert( (int)f == firstFiles[p] );
    }
#endif

    for( p=0; p<inf->pieceCount; ++p )
        inf->pieces[p].priority = calculatePiecePriority( tor, p, firstFiles[p] );

    tr_free( firstFiles );
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
torrentRealInit( tr_torrent * tor, const tr_ctor * ctor )
{
    int          doStart;
    uint64_t     loaded;
    uint64_t     t;
    const char * dir;
    static int   nextUniqueId = 1;
    tr_info    * info = &tor->info;
    tr_session * session = tr_ctorGetSession( ctor );

    assert( session != NULL );

    tr_globalLock( session );

    tor->session   = session;
    tor->uniqueId = nextUniqueId++;
    tor->magicNumber = TORRENT_MAGIC_NUMBER;

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

    assert( !tor->downloadedCur );
    assert( !tor->uploadedCur );

    tr_ctorInitTorrentPriorities( ctor, tor );

    tr_ctorInitTorrentWanted( ctor, tor );

    tor->error = TR_STAT_OK;

    tr_bitfieldConstruct( &tor->checkedPieces, tor->info.pieceCount );
    tr_torrentUncheck( tor );

    tr_torrentSetAddedDate( tor, time( NULL ) ); /* this is a default value to be
                                                    overwritten by the resume file */

    loaded = tr_torrentLoadResume( tor, ~0, ctor );

    doStart = tor->isRunning;
    tor->isRunning = 0;

    if( !( loaded & TR_FR_SPEEDLIMIT ) )
    {
        tr_torrentUseSpeedLimit( tor, TR_UP, FALSE );
        tr_torrentSetSpeedLimit( tor, TR_UP, tr_sessionGetSpeedLimit( tor->session, TR_UP ) );
        tr_torrentUseSpeedLimit( tor, TR_DOWN, FALSE );
        tr_torrentSetSpeedLimit( tor, TR_DOWN, tr_sessionGetSpeedLimit( tor->session, TR_DOWN ) );
        tr_torrentUseSessionLimits( tor, TRUE );
    }

    if( !( loaded & TR_FR_RATIOLIMIT ) )
    {
        tr_torrentSetRatioMode( tor, TR_RATIOLIMIT_GLOBAL );
        tr_torrentSetRatioLimit( tor, tr_sessionGetRatioLimit( tor->session ) );
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
            tr_bencToFile( val, TR_FMT_BENC, filename );
            tr_sessionSetTorrentFile( tor->session, tor->info.hashString, filename );
        }
    }

    tr_metainfoMigrate( session, &tor->info );

    if( doStart )
        torrentStart( tor, FALSE );
}

tr_parse_result
tr_torrentParse( const tr_ctor * ctor, tr_info * setmeInfo )
{
    int             doFree;
    tr_bool         didParse;
    tr_info         tmp;
    const tr_benc * metainfo;
    tr_session    * session = tr_ctorGetSession( ctor );
    tr_parse_result result = TR_PARSE_OK;

    if( setmeInfo == NULL )
        setmeInfo = &tmp;
    memset( setmeInfo, 0, sizeof( tr_info ) );

    if( tr_ctorGetMetainfo( ctor, &metainfo ) )
        return TR_PARSE_ERR;

    didParse = tr_metainfoParse( session, setmeInfo, metainfo );
    doFree = didParse && ( setmeInfo == &tmp );

    if( didParse && !getBlockSize( setmeInfo->pieceSize ) )
        result = TR_PARSE_ERR;

    if( didParse && session && tr_torrentExists( session, setmeInfo->hash ) )
        result = TR_PARSE_DUPLICATE;

    if( doFree )
        tr_metainfoFree( setmeInfo );

    return result;
}

tr_torrent *
tr_torrentNew( const tr_ctor  * ctor,
               int            * setmeError )
{
    int          err;
    tr_info      tmpInfo;
    tr_torrent * tor = NULL;

    assert( ctor != NULL );
    assert( tr_isSession( tr_ctorGetSession( ctor ) ) );

    err = tr_torrentParse( ctor, &tmpInfo );
    if( !err )
    {
        tor = tr_new0( tr_torrent, 1 );
        tor->info = tmpInfo;
        torrentRealInit( tor, ctor );
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
tr_torrentSetDownloadDir( tr_torrent * tor, const char * path )
{
    assert( tr_isTorrent( tor  ) );

    if( !path || !tor->downloadDir || strcmp( path, tor->downloadDir ) )
    {
        tr_free( tor->downloadDir );
        tor->downloadDir = tr_strdup( path );
        tr_torrentSetDirty( tor );
    }
}

const char*
tr_torrentGetDownloadDir( const tr_torrent * tor )
{
    assert( tr_isTorrent( tor  ) );

    return tor->downloadDir;
}

void
tr_torrentChangeMyPort( tr_torrent * tor )
{
    assert( tr_isTorrent( tor  ) );

    if( tor->tracker )
        tr_trackerChangeMyPort( tor->tracker );
}

static TR_INLINE void
tr_torrentManualUpdateImpl( void * vtor )
{
    tr_torrent * tor = vtor;

    assert( tr_isTorrent( tor  ) );

    if( tor->isRunning )
        tr_trackerReannounce( tor->tracker );
}

void
tr_torrentManualUpdate( tr_torrent * tor )
{
    assert( tr_isTorrent( tor  ) );

    tr_runInEventThread( tor->session, tr_torrentManualUpdateImpl, tor );
}

tr_bool
tr_torrentCanManualUpdate( const tr_torrent * tor )
{
    return ( tr_isTorrent( tor  ) )
        && ( tor->isRunning )
        && ( tr_trackerCanManualAnnounce( tor->tracker ) );
}

const tr_info *
tr_torrentInfo( const tr_torrent * tor )
{
    return tr_isTorrent( tor ) ? &tor->info : NULL;
}

const tr_stat *
tr_torrentStatCached( tr_torrent * tor )
{
    const time_t now = time( NULL );

    return tr_isTorrent( tor ) && ( now == tor->lastStatTime )
         ? &tor->stats
         : tr_torrentStat( tor );
}

void
tr_torrentSetVerifyState( tr_torrent * tor, tr_verify_state state )
{
    assert( tr_isTorrent( tor ) );
    assert( state==TR_VERIFY_NONE || state==TR_VERIFY_WAIT || state==TR_VERIFY_NOW );

    tor->verifyState = state;
    tor->anyDate = time( NULL );
}

tr_torrent_activity
tr_torrentGetActivity( tr_torrent * tor )
{
    assert( tr_isTorrent( tor ) );

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
    double                  downloadedForRatio, seedRatio=0;
    double                  d;
    tr_bool                 checkSeedRatio;

    if( !tor )
        return NULL;

    assert( tr_isTorrent( tor ) );
    tr_torrentLock( tor );

    tor->lastStatTime = time( NULL );

    s = &tor->stats;
    s->id = tor->uniqueId;
    s->activity = tr_torrentGetActivity( tor );
    s->error = tor->error;
    memcpy( s->errorString, tor->errorString, sizeof( s->errorString ) );

    tc = tor->tracker;
    ti = tr_trackerGetAddress( tor->tracker, tor );
    s->announceURL = ti ? ti->announce : NULL;
    s->scrapeURL   = ti ? ti->scrape   : NULL;
    tr_trackerStat( tc, s );

    tr_trackerGetCounts( tc, &s->timesCompleted,
                             &s->leechers,
                             &s->seeders,
                             &s->downloaders );

    tr_peerMgrTorrentStats( tor,
                            &s->peersKnown,
                            &s->peersConnected,
                            &usableSeeds,
                            &s->webseedsSendingToUs,
                            &s->peersSendingToUs,
                            &s->peersGettingFromUs,
                            s->peersFrom );

    now = tr_date( );
    d = tr_peerMgrGetWebseedSpeed( tor, now );
    s->swarmSpeed         = tr_rcRate( &tor->swarmSpeed, now );
    s->rawUploadSpeed     = tr_bandwidthGetRawSpeed  ( tor->bandwidth, now, TR_UP );
    s->pieceUploadSpeed   = tr_bandwidthGetPieceSpeed( tor->bandwidth, now, TR_UP );
    s->rawDownloadSpeed   = d + tr_bandwidthGetRawSpeed  ( tor->bandwidth, now, TR_DOWN );
    s->pieceDownloadSpeed = d + tr_bandwidthGetPieceSpeed( tor->bandwidth, now, TR_DOWN );

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
        tr_bitfield *    peerPieces = tr_peerMgrGetAvailable( tor );
        s->desiredAvailable = 0;
        for( i = 0; i < tor->info.pieceCount; ++i )
            if( !tor->info.pieces[i].dnd && tr_bitfieldHasFast( peerPieces, i ) )
                s->desiredAvailable += tr_cpMissingBlocksInPiece( &tor->completion, i );
        s->desiredAvailable *= tor->blockSize;
        tr_bitfieldFree( peerPieces );
    }

    downloadedForRatio = s->downloadedEver ? s->downloadedEver : s->haveValid;
    s->ratio = tr_getRatio( s->uploadedEver, downloadedForRatio );

    checkSeedRatio = tr_torrentGetSeedRatio( tor, &seedRatio );

    switch( s->activity )
    {
        case TR_STATUS_DOWNLOAD:
            if( s->leftUntilDone > s->desiredAvailable )
                s->eta = TR_ETA_NOT_AVAIL;
            else if( s->pieceDownloadSpeed < 0.1 )
                s->eta = TR_ETA_UNKNOWN;
            else {
                /* etaSpeed exists because if we pieceDownloadSpeed directly,
                 * brief fluctuations cause the ETA to jump all over the place.
                 * so, etaSpeed is a smoothed-out version of pieceDownloadSpeed
                 * to dampen the effect of fluctuations */
                if( ( tor->etaSpeedCalculatedAt + 800 ) < now ) {
                    tor->etaSpeedCalculatedAt = now;
                    tor->etaSpeed = (fabs(tor->etaSpeed)<0.0001)
                        ? s->pieceDownloadSpeed /* if no previous speed, no need to smooth */
                        : 0.8*tor->etaSpeed + 0.2*s->pieceDownloadSpeed; /* smooth across 5 readings */
                }
                s->eta = s->leftUntilDone / tor->etaSpeed / 1024.0;
            }
            break;

        case TR_STATUS_SEED:
            if( checkSeedRatio )
            {
                if( s->pieceUploadSpeed < 0.1 )
                    s->eta = TR_ETA_UNKNOWN;
                else
                    s->eta = (downloadedForRatio * (seedRatio - s->ratio)) / s->pieceUploadSpeed / 1024.0;
            }
            else
                s->eta = TR_ETA_NOT_AVAIL;
            break;

        default:
            s->eta = TR_ETA_NOT_AVAIL;
            break;
    }

    if( !checkSeedRatio || s->ratio >= seedRatio || s->ratio == TR_RATIO_INF )
        s->percentRatio = 1.0;
    else if( s->ratio == TR_RATIO_NA )
        s->percentRatio = 0.0;
    else 
        s->percentRatio = s->ratio / seedRatio;

    tr_torrentUnlock( tor );

    return s;
}

/***
****
***/

static uint64_t
fileBytesCompleted( const tr_torrent * tor, tr_file_index_t index )
{
    uint64_t total = 0;
    const tr_file * f = &tor->info.files[index];

    if( f->length )
    {
        const tr_block_index_t firstBlock = f->offset / tor->blockSize;
        const uint64_t lastByte = f->offset + f->length - 1;
        const tr_block_index_t lastBlock = lastByte / tor->blockSize;

        if( firstBlock == lastBlock )
        {
            if( tr_cpBlockIsCompleteFast( &tor->completion, firstBlock ) )
                total = f->length;
        }
        else
        {
            uint32_t i;

            /* the first block */
            if( tr_cpBlockIsCompleteFast( &tor->completion, firstBlock ) )
                total += tor->blockSize - ( f->offset % tor->blockSize );

            /* the middle blocks */
            if( f->firstPiece == f->lastPiece )
            {
                for( i=firstBlock+1; i<lastBlock; ++i )
                    if( tr_cpBlockIsCompleteFast( &tor->completion, i ) )
                        total += tor->blockSize;
            }
            else
            {
                int64_t b = 0;
                const tr_block_index_t firstBlockOfLastPiece
                           = tr_torPieceFirstBlock( tor, f->lastPiece );
                const tr_block_index_t lastBlockOfFirstPiece
                           = tr_torPieceFirstBlock( tor, f->firstPiece )
                             + tr_torPieceCountBlocks( tor, f->firstPiece ) - 1;

                /* the rest of the first piece */
                for( i=firstBlock+1; i<lastBlock && i<=lastBlockOfFirstPiece; ++i )
                    if( tr_cpBlockIsCompleteFast( &tor->completion, i ) )
                        ++b;

                /* the middle pieces */
                if( f->firstPiece + 1 < f->lastPiece )
                    for( i=f->firstPiece+1; i<f->lastPiece; ++i )
                        b += tor->blockCountInPiece - tr_cpMissingBlocksInPiece( &tor->completion, i );

                /* the rest of the last piece */
                for( i=firstBlockOfLastPiece; i<lastBlock; ++i )
                    if( tr_cpBlockIsCompleteFast( &tor->completion, i ) )
                        ++b;

                b *= tor->blockSize;
                total += b;
            }

            /* the last block */
            if( tr_cpBlockIsCompleteFast( &tor->completion, lastBlock ) )
                total += ( f->offset + f->length ) - ( tor->blockSize * lastBlock );
        }
    }

    return total;
}

tr_file_stat *
tr_torrentFiles( const tr_torrent * tor,
                 tr_file_index_t *  fileCount )
{
    tr_file_index_t       i;
    const tr_file_index_t n = tor->info.fileCount;
    tr_file_stat *        files = tr_new0( tr_file_stat, n );
    tr_file_stat *        walk = files;
    const tr_bool         isSeed = tor->completeness == TR_SEED;

    assert( tr_isTorrent( tor ) );

    for( i = 0; i < n; ++i, ++walk )
    {
        const uint64_t b = isSeed ? tor->info.files[i].length : fileBytesCompleted( tor, i );
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
    return tr_isTorrent( tor )
         ? tr_peerMgrWebSpeeds( tor )
         : NULL;
}

tr_peer_stat *
tr_torrentPeers( const tr_torrent * tor,
                 int *              peerCount )
{
    tr_peer_stat * ret = NULL;

    if( tr_isTorrent( tor ) )
        ret = tr_peerMgrPeerStats( tor, peerCount );

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
    tr_peerMgrTorrentAvailability( tor, tab, size );
}

void
tr_torrentAmountFinished( const tr_torrent * tor,
                          float *            tab,
                          int                size )
{
    assert( tr_isTorrent( tor ) );

    tr_torrentLock( tor );
    tr_cpGetAmountDone( &tor->completion, tab, size );
    tr_torrentUnlock( tor );
}

void
tr_torrentResetTransferStats( tr_torrent * tor )
{
    assert( tr_isTorrent( tor ) );

    tr_torrentLock( tor );

    tor->downloadedPrev += tor->downloadedCur;
    tor->downloadedCur   = 0;
    tor->uploadedPrev   += tor->uploadedCur;
    tor->uploadedCur     = 0;
    tor->corruptPrev    += tor->corruptCur;
    tor->corruptCur      = 0;

    tr_torrentSetDirty( tor );

    tr_torrentUnlock( tor );
}

void
tr_torrentSetHasPiece( tr_torrent *     tor,
                       tr_piece_index_t pieceIndex,
                       tr_bool          has )
{
    assert( tr_isTorrent( tor ) );
    assert( pieceIndex < tor->info.pieceCount );

    if( has )
        tr_cpPieceAdd( &tor->completion, pieceIndex );
    else
        tr_cpPieceRem( &tor->completion, pieceIndex );
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

    assert( tr_isTorrent( tor ) );
    assert( !tor->isRunning );

    tr_globalLock( session );

    tr_peerMgrRemoveTorrent( tor );

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
    time_t now;
    tr_torrent * tor = vtor;

    assert( tr_isTorrent( tor ) );

    tr_globalLock( tor->session );

    now = time( NULL );
    tor->isRunning = TRUE;
    tor->needsSeedRatioCheck = TRUE;
    tor->error = TR_STAT_OK;
    tor->errorString[0] = '\0';
    tor->completeness = tr_cpGetStatus( &tor->completion );
    tor->startDate = tor->anyDate = now;
    tr_torrentResetTransferStats( tor );
    tr_trackerStart( tor->tracker );
    tor->dhtAnnounceAt = now + tr_cryptoWeakRandInt( 20 );
    tr_peerMgrStartTorrent( tor );

    tr_globalUnlock( tor->session );
}

static void
checkAndStartCB( tr_torrent * tor )
{
    assert( tr_isTorrent( tor ) );
    assert( tr_isSession( tor->session ) );

    tr_runInEventThread( tor->session, checkAndStartImpl, tor );
}

static void
torrentStart( tr_torrent * tor, int reloadProgress )
{
    assert( tr_isTorrent( tor ) );

    tr_globalLock( tor->session );

    if( !tor->isRunning )
    {
        tr_verifyRemove( tor );

        if( reloadProgress )
            tr_torrentLoadResume( tor, TR_FR_PROGRESS, NULL );

        tor->isRunning = 1;
        tr_verifyAdd( tor, checkAndStartCB );
    }

    tr_globalUnlock( tor->session );
}

void
tr_torrentStart( tr_torrent * tor )
{
    if( tr_isTorrent( tor ) )
        torrentStart( tor, TRUE );
}

static void
torrentRecheckDoneImpl( void * vtor )
{
    tr_torrent * tor = vtor;

    assert( tr_isTorrent( tor ) );
    tr_torrentRecheckCompleteness( tor );

    if( tor->startAfterVerify )
    {
        tor->startAfterVerify = FALSE;
        tr_torrentStart( tor );
    }
}

static void
torrentRecheckDoneCB( tr_torrent * tor )
{
    assert( tr_isTorrent( tor ) );

    tr_runInEventThread( tor->session, torrentRecheckDoneImpl, tor );
}

void
tr_torrentVerify( tr_torrent * tor )
{
    assert( tr_isTorrent( tor ) );
    tr_globalLock( tor->session );

    /* if the torrent's already being verified, stop it */
    tr_verifyRemove( tor );

    /* if the torrent's running, stop it & set the restart-after-verify flag */
    if( tor->isRunning ) {
        tr_torrentStop( tor );
        tor->startAfterVerify = TRUE;
    }

    /* add the torrent to the recheck queue */
    tr_torrentUncheck( tor );
    tr_verifyAdd( tor, torrentRecheckDoneCB );

    tr_globalUnlock( tor->session );
}

static void
stopTorrent( void * vtor )
{
    tr_torrent * tor = vtor;

    assert( tr_isTorrent( tor ) );

    tr_verifyRemove( tor );
    tr_peerMgrStopTorrent( tor );
    tr_trackerStop( tor->tracker );

    tr_fdTorrentClose( tor->uniqueId );

    if( tor->isDirty ) {
        tor->isDirty = 0;
        if( !tor->isDeleting )
            tr_torrentSaveResume( tor );
    }
}

void
tr_torrentStop( tr_torrent * tor )
{
    assert( tr_isTorrent( tor ) );

    if( tr_isTorrent( tor ) )
    {
        tr_globalLock( tor->session );

        tor->isRunning = 0;
        tr_runInEventThread( tor->session, stopTorrent, tor );

        tr_globalUnlock( tor->session );
    }
}

static void
closeTorrent( void * vtor )
{
    tr_benc * d;
    tr_torrent * tor = vtor;

    assert( tr_isTorrent( tor ) );

    d = tr_bencListAddDict( &tor->session->removedTorrents, 2 );
    tr_bencDictAddInt( d, "id", tor->uniqueId );
    tr_bencDictAddInt( d, "date", time( NULL ) );

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
    if( tr_isTorrent( tor ) )
    {
        tr_session * session = tor->session;
        assert( tr_isSession( session ) );
        tr_globalLock( session );

        tr_torrentClearCompletenessCallback( tor );
        tr_runInEventThread( session, closeTorrent, tor );

        tr_globalUnlock( session );
    }
}

void
tr_torrentRemove( tr_torrent * tor )
{
    assert( tr_isTorrent( tor ) );

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
    assert( tr_isTorrent( tor ) );
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
    assert( tr_isTorrent( tor ) );

    tor->completeness_func = func;
    tor->completeness_func_user_data = user_data;
}

void
tr_torrentSetRatioLimitHitCallback( tr_torrent                     * tor,
                                    tr_torrent_ratio_limit_hit_func  func,
                                    void                           * user_data )
{
    assert( tr_isTorrent( tor ) );

    tor->ratio_limit_hit_func = func;
    tor->ratio_limit_hit_func_user_data = user_data;
}

void
tr_torrentClearCompletenessCallback( tr_torrent * torrent )
{
    tr_torrentSetCompletenessCallback( torrent, NULL, NULL );
}

void
tr_torrentClearRatioLimitHitCallback( tr_torrent * torrent )
{
    tr_torrentSetRatioLimitHitCallback( torrent, NULL, NULL );
}

void
tr_torrentRecheckCompleteness( tr_torrent * tor )
{
    tr_completeness completeness;

    assert( tr_isTorrent( tor ) );

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
        tor->needsSeedRatioCheck = TRUE;
        tr_fdTorrentClose( tor->uniqueId );
        fireCompletenessChange( tor, completeness );

        if( recentChange && ( completeness == TR_SEED ) )
        {
            tr_trackerCompleted( tor->tracker );

            tor->doneDate = tor->anyDate = time( NULL );
        }

        tr_torrentSetDirty( tor );
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

    assert( tr_isTorrent( tor ) );
    assert( fileIndex < tor->info.fileCount );
    assert( tr_isPriority( priority ) );

    file = &tor->info.files[fileIndex];
    file->priority = priority;
    for( i = file->firstPiece; i <= file->lastPiece; ++i )
        tor->info.pieces[i].priority = calculatePiecePriority( tor, i, fileIndex );
}

void
tr_torrentSetFilePriorities( tr_torrent *      tor,
                             tr_file_index_t * files,
                             tr_file_index_t   fileCount,
                             tr_priority_t     priority )
{
    tr_file_index_t i;

    assert( tr_isTorrent( tor ) );

    tr_torrentLock( tor );

    for( i = 0; i < fileCount; ++i )
        tr_torrentInitFilePriority( tor, files[i], priority );

    tr_torrentSetDirty( tor );
    tr_torrentUnlock( tor );
}

tr_priority_t
tr_torrentGetFilePriority( const tr_torrent * tor,
                           tr_file_index_t    file )
{
    tr_priority_t ret;

    assert( tr_isTorrent( tor ) );

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

    assert( tr_isTorrent( tor ) );

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

    assert( tr_isTorrent( tor ) );

    tr_torrentLock( tor );

    assert( file < tor->info.fileCount );
    doDownload = !tor->info.files[file].dnd;

    tr_torrentUnlock( tor );
    return doDownload != 0;
}

static void
setFileDND( tr_torrent * tor, tr_file_index_t fileIndex, int doDownload )
{
    tr_file *        file;
    const int        dnd = !doDownload;
    tr_piece_index_t firstPiece, firstPieceDND;
    tr_piece_index_t lastPiece, lastPieceDND;
    tr_file_index_t  i;

    assert( tr_isTorrent( tor ) );

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
tr_torrentInitFileDLs( tr_torrent      * tor,
                       tr_file_index_t * files,
                       tr_file_index_t   fileCount,
                       tr_bool           doDownload )
{
    tr_file_index_t i;

    assert( tr_isTorrent( tor ) );

    tr_torrentLock( tor );

    for( i=0; i<fileCount; ++i )
        setFileDND( tor, files[i], doDownload );
    tr_cpInvalidateDND( &tor->completion );
    tor->needsSeedRatioCheck = TRUE;

    tr_torrentUnlock( tor );
}

void
tr_torrentSetFileDLs( tr_torrent *      tor,
                      tr_file_index_t * files,
                      tr_file_index_t   fileCount,
                      tr_bool           doDownload )
{
    assert( tr_isTorrent( tor ) );

    tr_torrentLock( tor );
    tr_torrentInitFileDLs( tor, files, fileCount, doDownload );
    tr_torrentSetDirty( tor );
    tr_torrentUnlock( tor );
}

/***
****
***/

tr_priority_t
tr_torrentGetPriority( const tr_torrent * tor )
{
    assert( tr_isTorrent( tor ) );

    return tor->bandwidth->priority;
}

void
tr_torrentSetPriority( tr_torrent * tor, tr_priority_t priority )
{
    assert( tr_isTorrent( tor ) );
    assert( tr_isPriority( priority ) );

    tor->bandwidth->priority = priority;

    tr_torrentSetDirty( tor );
}

/***
****
***/

void
tr_torrentSetPeerLimit( tr_torrent * tor,
                        uint16_t     maxConnectedPeers )
{
    assert( tr_isTorrent( tor ) );

    tor->maxConnectedPeers = maxConnectedPeers;
}

uint16_t
tr_torrentGetPeerLimit( const tr_torrent * tor )
{
    assert( tr_isTorrent( tor ) );

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
    tr_block_index_t ret;

    assert( tr_isTorrent( tor ) );

    ret = index;
    ret *= ( tor->info.pieceSize / tor->blockSize );
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

    assert( tr_isTorrent( tor ) );

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

    assert( tr_isTorrent( tor ) );

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
    assert( tr_isTorrent( tor ) );

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

    assert( tr_isTorrent( tor ) );

    if( isChecked )
        tr_bitfieldAddRange( &tor->checkedPieces, begin, end );
    else
        tr_bitfieldRemRange( &tor->checkedPieces, begin, end );
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

    assert( tr_isTorrent( tor ) );

    for( i = begin; isChecked && i < end; ++i )
        if( !tr_torrentIsPieceChecked( tor, i ) )
            isChecked = FALSE;

    return isChecked;
}

void
tr_torrentUncheck( tr_torrent * tor )
{
    assert( tr_isTorrent( tor ) );

    tr_bitfieldRemRange( &tor->checkedPieces, 0, tor->info.pieceCount );
}

int
tr_torrentCountUncheckedPieces( const tr_torrent * tor )
{
    assert( tr_isTorrent( tor ) );

    return tor->info.pieceCount - tr_bitfieldCountTrueBits( &tor->checkedPieces );
}

time_t*
tr_torrentGetMTimes( const tr_torrent * tor,
                     size_t *           setme_n )
{
    size_t       i;
    const size_t n = tor->info.fileCount;
    time_t *     m = tr_new0( time_t, n );

    assert( tr_isTorrent( tor ) );

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

    assert( tr_isTorrent( tor ) );

    /* save to the .torrent file */
    if( !tr_bencLoadFile( &metainfo, TR_FMT_BENC, tor->info.torrent ) )
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
        if( tr_metainfoParse( tor->session, &tmpInfo, &metainfo ) )
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
            tr_bencToFile( &metainfo, TR_FMT_BENC, tor->info.torrent );
        }

        /* cleanup */
        tr_bencFree( &metainfo );
    }
}

/**
***
**/

void
tr_torrentSetAddedDate( tr_torrent * tor,
                        time_t       t )
{
    assert( tr_isTorrent( tor ) );

    tor->addedDate = t;
    tor->anyDate = MAX( tor->anyDate, tor->addedDate );
}

void
tr_torrentSetActivityDate( tr_torrent * tor,
                           time_t       t )
{
    assert( tr_isTorrent( tor ) );

    tor->activityDate = t;
    tor->anyDate = MAX( tor->anyDate, tor->activityDate );
    tr_torrentSetDirty( tor );
}

void
tr_torrentSetDoneDate( tr_torrent * tor,
                       time_t       t )
{
    assert( tr_isTorrent( tor ) );

    tor->doneDate = t;
    tor->anyDate = MAX( tor->anyDate, tor->doneDate );
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

    assert( tr_isTorrent( tor ) );

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

    assert( tr_isTorrent( tor ) );

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

    assert( tr_isTorrent( tor ) );

    for( f=0; f<tor->info.fileCount; ++f )
        tr_ptrArrayInsertSorted( &torrentFiles, tor->info.files[f].name, vstrcmp );

    /* build the set of folders and dirtyFolders */
    walkLocalData( tor, root, root, NULL, &torrentFiles, &folders, &dirtyFolders );

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
    assert( tr_isTorrent( tor ) );

    if( fileFunc == NULL )
        fileFunc = remove;

    /* close all the files because we're about to delete them */
    tr_fdTorrentClose( tor->uniqueId );

    if( tor->info.fileCount > 1 )
        deleteLocalData( tor, fileFunc );
    else {
        /* torrent only has one file */
        char * path = tr_buildPath( tor->downloadDir, tor->info.files[0].name, NULL );
        fileFunc( path );
        tr_free( path );
    }
}

/***
****
***/

struct LocationData
{
    tr_bool move_from_old_location;
    int * setme_state;
    double * setme_progress;
    char * location;
    tr_torrent * tor;
};

static void
setLocation( void * vdata )
{
    int err = 0;
    struct LocationData * data = vdata;
    tr_torrent * tor = data->tor;
    const tr_bool do_move = data->move_from_old_location;
    const char * location = data->location;
    double bytesHandled = 0;

    assert( tr_isTorrent( tor ) );

    if( strcmp( location, tor->downloadDir ) )
    {
        tr_file_index_t i;

        /* bad idea to move files while they're being verified... */
        tr_verifyRemove( tor );

        /* if the torrent is running, stop it and set a flag to
         * restart after we're done */
        if( tor->isRunning )
        {
            tr_torrentStop( tor );
            tor->startAfterVerify = TRUE;
        }

        /* try to move the files.
         * FIXME: there are still all kinds of nasty cases, like what
         * if the target directory runs out of space halfway through... */
        for( i=0; !err && i<tor->info.fileCount; ++i )
        {
            struct stat sb;
            const tr_file * f = &tor->info.files[i];
            char * oldpath = tr_buildPath( tor->downloadDir, f->name, NULL );
            char * newpath = tr_buildPath( location, f->name, NULL );
            
            if( do_move )
            {
                errno = 0;
                tr_torinf( tor, "moving \"%s\" to \"%s\"", oldpath, newpath );
                if( tr_moveFile( oldpath, newpath ) ) {
                    err = 1;
                    tr_torerr( tor, "error moving \"%s\" to \"%s\": %s",
                                    oldpath, newpath, tr_strerror( errno ) );
                }
            }
            else if( !stat( newpath, &sb ) )
            {
                tr_torinf( tor, "found \"%s\"", newpath );
            }

            if( data->setme_progress )
            {
                bytesHandled += f->length;
                *data->setme_progress = bytesHandled / tor->info.totalSize;
            }

            tr_free( newpath );
            tr_free( oldpath );
        }

        if( !err )
        {
            /* blow away the leftover subdirectories in the old location */
            tr_torrentDeleteLocalData( tor, remove );

            /* set the new location and reverify */
            tr_torrentSetDownloadDir( tor, location );
            tr_torrentVerify( tor );
        }
    }

    if( data->setme_state )
        *data->setme_state = err ? TR_LOC_ERROR : TR_LOC_DONE;

    /* cleanup */
    tr_free( data->location );
    tr_free( data );
}

void
tr_torrentSetLocation( tr_torrent  * tor,
                       const char  * location,
                       tr_bool       move_from_old_location,
                       double      * setme_progress,
                       int         * setme_state )
{
    struct LocationData * data;

    assert( tr_isTorrent( tor ) );

    if( setme_state )
        *setme_state = TR_LOC_MOVING;
    if( setme_progress )
        *setme_progress = 0;

    /* run this in the libtransmission thread */
    data = tr_new( struct LocationData, 1 );
    data->tor = tor;
    data->location = tr_strdup( location );
    data->move_from_old_location = move_from_old_location;
    data->setme_state = setme_state;
    data->setme_progress = setme_progress;
    tr_runInEventThread( tor->session, setLocation, data );
}

/***
****
***/

void
tr_torrentCheckSeedRatio( tr_torrent * tor )
{
    double seedRatio;

    assert( tr_isTorrent( tor ) );

    /* if we're seeding and we've reached our seed ratio limit, stop the torrent */
    if( tor->isRunning && tr_torrentIsSeed( tor ) && tr_torrentGetSeedRatio( tor, &seedRatio ) )
    {
        const uint64_t up = tor->uploadedCur + tor->uploadedPrev;
        uint64_t down = tor->downloadedCur + tor->downloadedPrev;
        double ratio;

        /* maybe we're the initial seeder and never downloaded anything... */
        if( down == 0 )
            down = tr_cpHaveValid( &tor->completion );

        ratio = tr_getRatio( up, down );

        if( ratio >= seedRatio || ratio == TR_RATIO_INF )
        {
            tr_torrentStop( tor );

            /* set to no ratio limit to allow easy restarting */
            tr_torrentSetRatioMode( tor, TR_RATIOLIMIT_UNLIMITED );

            /* maybe notify the client */
            if( tor->ratio_limit_hit_func != NULL )
                tor->ratio_limit_hit_func( tor, tor->ratio_limit_hit_func_user_data );
        }
    }
}
