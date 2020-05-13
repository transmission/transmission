/*
 * This file Copyright (C) 2009-2017 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <errno.h> /* EINVAL */
#include <signal.h> /* signal() */

#ifndef _WIN32
#include <sys/wait.h> /* wait() */
#include <unistd.h> /* fork(), execvp(), _exit() */
#else
#include <windows.h> /* CreateProcess(), GetLastError() */
#endif

#include <math.h>
#include <stdarg.h>
#include <string.h> /* memcmp */
#include <stdlib.h> /* qsort */
#include <limits.h> /* INT_MAX */

#include <event2/util.h> /* evutil_vsnprintf() */

#include "transmission.h"
#include "announcer.h"
#include "bandwidth.h"
#include "cache.h"
#include "completion.h"
#include "crypto-utils.h" /* for tr_sha1 */
#include "error.h"
#include "fdlimit.h" /* tr_fdTorrentClose */
#include "file.h"
#include "inout.h" /* tr_ioTestPiece() */
#include "log.h"
#include "magnet.h"
#include "metainfo.h"
#include "peer-common.h" /* MAX_BLOCK_SIZE */
#include "peer-mgr.h"
#include "platform.h" /* TR_PATH_DELIMITER_STR */
#include "ptrarray.h"
#include "resume.h"
#include "session.h"
#include "subprocess.h"
#include "torrent.h"
#include "torrent-magnet.h"
#include "tr-assert.h"
#include "trevent.h" /* tr_runInEventThread() */
#include "utils.h"
#include "variant.h"
#include "verify.h"
#include "version.h"

/***
****
***/

#define tr_deeplog_tor(tor, ...) tr_logAddDeepNamed(tr_torrentName(tor), __VA_ARGS__)

/***
****
***/

char const* tr_torrentName(tr_torrent const* tor)
{
    return tor != NULL ? tor->info.name : "";
}

int tr_torrentId(tr_torrent const* tor)
{
    return tor != NULL ? tor->uniqueId : -1;
}

tr_torrent* tr_torrentFindFromId(tr_session* session, int id)
{
    tr_torrent* tor = NULL;

    while ((tor = tr_torrentNext(session, tor)) != NULL)
    {
        if (tor->uniqueId == id)
        {
            return tor;
        }
    }

    return NULL;
}

tr_torrent* tr_torrentFindFromHashString(tr_session* session, char const* str)
{
    tr_torrent* tor = NULL;

    while ((tor = tr_torrentNext(session, tor)) != NULL)
    {
        if (evutil_ascii_strcasecmp(str, tor->info.hashString) == 0)
        {
            return tor;
        }
    }

    return NULL;
}

tr_torrent* tr_torrentFindFromHash(tr_session* session, uint8_t const* torrentHash)
{
    tr_torrent* tor = NULL;

    while ((tor = tr_torrentNext(session, tor)) != NULL)
    {
        if (*tor->info.hash == *torrentHash)
        {
            if (memcmp(tor->info.hash, torrentHash, SHA_DIGEST_LENGTH) == 0)
            {
                return tor;
            }
        }
    }

    return NULL;
}

tr_torrent* tr_torrentFindFromMagnetLink(tr_session* session, char const* magnet)
{
    tr_magnet_info* info;
    tr_torrent* tor = NULL;

    if ((info = tr_magnetParse(magnet)) != NULL)
    {
        tor = tr_torrentFindFromHash(session, info->hash);
        tr_magnetFree(info);
    }

    return tor;
}

tr_torrent* tr_torrentFindFromObfuscatedHash(tr_session* session, uint8_t const* obfuscatedTorrentHash)
{
    tr_torrent* tor = NULL;

    while ((tor = tr_torrentNext(session, tor)) != NULL)
    {
        if (memcmp(tor->obfuscatedHash, obfuscatedTorrentHash, SHA_DIGEST_LENGTH) == 0)
        {
            return tor;
        }
    }

    return NULL;
}

bool tr_torrentIsPieceTransferAllowed(tr_torrent const* tor, tr_direction direction)
{
    TR_ASSERT(tr_isTorrent(tor));
    TR_ASSERT(tr_isDirection(direction));

    bool allowed = true;

    if (tr_torrentUsesSpeedLimit(tor, direction))
    {
        if (tr_torrentGetSpeedLimit_Bps(tor, direction) <= 0)
        {
            allowed = false;
        }
    }

    if (tr_torrentUsesSessionLimits(tor))
    {
        unsigned int limit;

        if (tr_sessionGetActiveSpeedLimit_Bps(tor->session, direction, &limit))
        {
            if (limit <= 0)
            {
                allowed = false;
            }
        }
    }

    return allowed;
}

/***
****
***/

static void tr_torrentUnsetPeerId(tr_torrent* tor)
{
    /* triggers a rebuild next time tr_torrentGetPeerId() is called */
    *tor->peer_id = '\0';
}

static int peerIdTTL(tr_torrent const* tor)
{
    int ttl;

    if (tor->peer_id_creation_time == 0)
    {
        ttl = 0;
    }
    else
    {
        ttl = (int)difftime(tor->peer_id_creation_time + tor->session->peer_id_ttl_hours * 3600, tr_time());
    }

    return ttl;
}

unsigned char const* tr_torrentGetPeerId(tr_torrent* tor)
{
    bool needs_new_peer_id = false;

    if (*tor->peer_id == '\0')
    {
        needs_new_peer_id = true;
    }

    if (!needs_new_peer_id)
    {
        if (!tr_torrentIsPrivate(tor))
        {
            if (peerIdTTL(tor) <= 0)
            {
                needs_new_peer_id = true;
            }
        }
    }

    if (needs_new_peer_id)
    {
        tr_peerIdInit(tor->peer_id);
        tor->peer_id_creation_time = tr_time();
    }

    return tor->peer_id;
}

/***
****  PER-TORRENT UL / DL SPEEDS
***/

void tr_torrentSetSpeedLimit_Bps(tr_torrent* tor, tr_direction dir, unsigned int Bps)
{
    TR_ASSERT(tr_isTorrent(tor));
    TR_ASSERT(tr_isDirection(dir));

    if (tr_bandwidthSetDesiredSpeed_Bps(&tor->bandwidth, dir, Bps))
    {
        tr_torrentSetDirty(tor);
    }
}

void tr_torrentSetSpeedLimit_KBps(tr_torrent* tor, tr_direction dir, unsigned int KBps)
{
    tr_torrentSetSpeedLimit_Bps(tor, dir, toSpeedBytes(KBps));
}

unsigned int tr_torrentGetSpeedLimit_Bps(tr_torrent const* tor, tr_direction dir)
{
    TR_ASSERT(tr_isTorrent(tor));
    TR_ASSERT(tr_isDirection(dir));

    return tr_bandwidthGetDesiredSpeed_Bps(&tor->bandwidth, dir);
}

unsigned int tr_torrentGetSpeedLimit_KBps(tr_torrent const* tor, tr_direction dir)
{
    TR_ASSERT(tr_isTorrent(tor));
    TR_ASSERT(tr_isDirection(dir));

    return toSpeedKBps(tr_torrentGetSpeedLimit_Bps(tor, dir));
}

void tr_torrentUseSpeedLimit(tr_torrent* tor, tr_direction dir, bool do_use)
{
    TR_ASSERT(tr_isTorrent(tor));
    TR_ASSERT(tr_isDirection(dir));

    if (tr_bandwidthSetLimited(&tor->bandwidth, dir, do_use))
    {
        tr_torrentSetDirty(tor);
    }
}

bool tr_torrentUsesSpeedLimit(tr_torrent const* tor, tr_direction dir)
{
    TR_ASSERT(tr_isTorrent(tor));

    return tr_bandwidthIsLimited(&tor->bandwidth, dir);
}

void tr_torrentUseSessionLimits(tr_torrent* tor, bool doUse)
{
    TR_ASSERT(tr_isTorrent(tor));

    bool changed;

    changed = tr_bandwidthHonorParentLimits(&tor->bandwidth, TR_UP, doUse);
    changed |= tr_bandwidthHonorParentLimits(&tor->bandwidth, TR_DOWN, doUse);

    if (changed)
    {
        tr_torrentSetDirty(tor);
    }
}

bool tr_torrentUsesSessionLimits(tr_torrent const* tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    return tr_bandwidthAreParentLimitsHonored(&tor->bandwidth, TR_UP);
}

/***
****
***/

void tr_torrentSetRatioMode(tr_torrent* tor, tr_ratiolimit mode)
{
    TR_ASSERT(tr_isTorrent(tor));
    TR_ASSERT(mode == TR_RATIOLIMIT_GLOBAL || mode == TR_RATIOLIMIT_SINGLE || mode == TR_RATIOLIMIT_UNLIMITED);

    if (mode != tor->ratioLimitMode)
    {
        tor->ratioLimitMode = mode;

        tr_torrentSetDirty(tor);
    }
}

tr_ratiolimit tr_torrentGetRatioMode(tr_torrent const* tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    return tor->ratioLimitMode;
}

void tr_torrentSetRatioLimit(tr_torrent* tor, double desiredRatio)
{
    TR_ASSERT(tr_isTorrent(tor));

    if ((int)(desiredRatio * 100.0) != (int)(tor->desiredRatio * 100.0))
    {
        tor->desiredRatio = desiredRatio;

        tr_torrentSetDirty(tor);
    }
}

double tr_torrentGetRatioLimit(tr_torrent const* tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    return tor->desiredRatio;
}

bool tr_torrentGetSeedRatio(tr_torrent const* tor, double* ratio)
{
    TR_ASSERT(tr_isTorrent(tor));

    bool isLimited;

    switch (tr_torrentGetRatioMode(tor))
    {
    case TR_RATIOLIMIT_SINGLE:
        isLimited = true;

        if (ratio != NULL)
        {
            *ratio = tr_torrentGetRatioLimit(tor);
        }

        break;

    case TR_RATIOLIMIT_GLOBAL:
        isLimited = tr_sessionIsRatioLimited(tor->session);

        if (isLimited && ratio != NULL)
        {
            *ratio = tr_sessionGetRatioLimit(tor->session);
        }

        break;

    default: /* TR_RATIOLIMIT_UNLIMITED */
        isLimited = false;
        break;
    }

    return isLimited;
}

/* returns true if the seed ratio applies --
 * it applies if the torrent's a seed AND it has a seed ratio set */
static bool tr_torrentGetSeedRatioBytes(tr_torrent const* tor, uint64_t* setmeLeft, uint64_t* setmeGoal)
{
    TR_ASSERT(tr_isTorrent(tor));

    double seedRatio;
    bool seedRatioApplies = false;

    if (tr_torrentGetSeedRatio(tor, &seedRatio))
    {
        uint64_t const u = tor->uploadedCur + tor->uploadedPrev;
        uint64_t const d = tor->downloadedCur + tor->downloadedPrev;
        uint64_t const baseline = d != 0 ? d : tr_cpSizeWhenDone(&tor->completion);
        uint64_t const goal = baseline * seedRatio;

        if (setmeLeft != NULL)
        {
            *setmeLeft = goal > u ? goal - u : 0;
        }

        if (setmeGoal != NULL)
        {
            *setmeGoal = goal;
        }

        seedRatioApplies = tr_torrentIsSeed(tor);
    }

    return seedRatioApplies;
}

static bool tr_torrentIsSeedRatioDone(tr_torrent const* tor)
{
    uint64_t bytesLeft;
    return tr_torrentGetSeedRatioBytes(tor, &bytesLeft, NULL) && bytesLeft == 0;
}

/***
****
***/

void tr_torrentSetIdleMode(tr_torrent* tor, tr_idlelimit mode)
{
    TR_ASSERT(tr_isTorrent(tor));
    TR_ASSERT(mode == TR_IDLELIMIT_GLOBAL || mode == TR_IDLELIMIT_SINGLE || mode == TR_IDLELIMIT_UNLIMITED);

    if (mode != tor->idleLimitMode)
    {
        tor->idleLimitMode = mode;

        tr_torrentSetDirty(tor);
    }
}

tr_idlelimit tr_torrentGetIdleMode(tr_torrent const* tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    return tor->idleLimitMode;
}

void tr_torrentSetIdleLimit(tr_torrent* tor, uint16_t idleMinutes)
{
    TR_ASSERT(tr_isTorrent(tor));

    if (idleMinutes > 0)
    {
        tor->idleLimitMinutes = idleMinutes;

        tr_torrentSetDirty(tor);
    }
}

uint16_t tr_torrentGetIdleLimit(tr_torrent const* tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    return tor->idleLimitMinutes;
}

bool tr_torrentGetSeedIdle(tr_torrent const* tor, uint16_t* idleMinutes)
{
    bool isLimited;

    switch (tr_torrentGetIdleMode(tor))
    {
    case TR_IDLELIMIT_SINGLE:
        isLimited = true;

        if (idleMinutes != NULL)
        {
            *idleMinutes = tr_torrentGetIdleLimit(tor);
        }

        break;

    case TR_IDLELIMIT_GLOBAL:
        isLimited = tr_sessionIsIdleLimited(tor->session);

        if (isLimited && idleMinutes != NULL)
        {
            *idleMinutes = tr_sessionGetIdleLimit(tor->session);
        }

        break;

    default: /* TR_IDLELIMIT_UNLIMITED */
        isLimited = false;
        break;
    }

    return isLimited;
}

static bool tr_torrentIsSeedIdleLimitDone(tr_torrent* tor)
{
    uint16_t idleMinutes;
    return tr_torrentGetSeedIdle(tor, &idleMinutes) &&
        difftime(tr_time(), MAX(tor->startDate, tor->activityDate)) >= idleMinutes * 60U;
}

/***
****
***/

void tr_torrentCheckSeedLimit(tr_torrent* tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    if (!tor->isRunning || tor->isStopping || !tr_torrentIsSeed(tor))
    {
        return;
    }

    /* if we're seeding and reach our seed ratio limit, stop the torrent */
    if (tr_torrentIsSeedRatioDone(tor))
    {
        tr_logAddTorInfo(tor, "%s", "Seed ratio reached; pausing torrent");

        tor->isStopping = true;

        /* maybe notify the client */
        if (tor->ratio_limit_hit_func != NULL)
        {
            (*tor->ratio_limit_hit_func)(tor, tor->ratio_limit_hit_func_user_data);
        }
    }
    /* if we're seeding and reach our inactiviy limit, stop the torrent */
    else if (tr_torrentIsSeedIdleLimitDone(tor))
    {
        tr_logAddTorInfo(tor, "%s", "Seeding idle limit reached; pausing torrent");

        tor->isStopping = true;
        tor->finishedSeedingByIdle = true;

        /* maybe notify the client */
        if (tor->idle_limit_hit_func != NULL)
        {
            (*tor->idle_limit_hit_func)(tor, tor->idle_limit_hit_func_user_data);
        }
    }
}

/***
****
***/

void tr_torrentSetLocalError(tr_torrent* tor, char const* fmt, ...)
{
    TR_ASSERT(tr_isTorrent(tor));

    va_list ap;

    va_start(ap, fmt);
    tor->error = TR_STAT_LOCAL_ERROR;
    tor->errorTracker[0] = '\0';
    evutil_vsnprintf(tor->errorString, sizeof(tor->errorString), fmt, ap);
    va_end(ap);

    tr_logAddTorErr(tor, "%s", tor->errorString);

    if (tor->isRunning)
    {
        tor->isStopping = true;
    }
}

static void tr_torrentClearError(tr_torrent* tor)
{
    tor->error = TR_STAT_OK;
    tor->errorString[0] = '\0';
    tor->errorTracker[0] = '\0';
}

static void onTrackerResponse(tr_torrent* tor, tr_tracker_event const* event, void* unused UNUSED)
{
    switch (event->messageType)
    {
    case TR_TRACKER_PEERS:
        {
            int8_t const seedProbability = event->seedProbability;
            bool const allAreSeeds = seedProbability == 100;

            if (allAreSeeds)
            {
                tr_logAddTorDbg(tor, "Got %zu seeds from tracker", event->pexCount);
            }
            else
            {
                tr_logAddTorDbg(tor, "Got %zu peers from tracker", event->pexCount);
            }

            for (size_t i = 0; i < event->pexCount; ++i)
            {
                tr_peerMgrAddPex(tor, TR_PEER_FROM_TRACKER, &event->pex[i], seedProbability);
            }

            break;
        }

    case TR_TRACKER_WARNING:
        tr_logAddTorErr(tor, _("Tracker warning: \"%s\""), event->text);
        tor->error = TR_STAT_TRACKER_WARNING;
        tr_strlcpy(tor->errorTracker, event->tracker, sizeof(tor->errorTracker));
        tr_strlcpy(tor->errorString, event->text, sizeof(tor->errorString));
        break;

    case TR_TRACKER_ERROR:
        tr_logAddTorErr(tor, _("Tracker error: \"%s\""), event->text);
        tor->error = TR_STAT_TRACKER_ERROR;
        tr_strlcpy(tor->errorTracker, event->tracker, sizeof(tor->errorTracker));
        tr_strlcpy(tor->errorString, event->text, sizeof(tor->errorString));
        break;

    case TR_TRACKER_ERROR_CLEAR:
        if (tor->error != TR_STAT_LOCAL_ERROR)
        {
            tr_torrentClearError(tor);
        }

        break;
    }
}

/***
****
****  TORRENT INSTANTIATION
****
***/

static tr_piece_index_t getBytePiece(tr_info const* info, uint64_t byteOffset)
{
    TR_ASSERT(info != NULL);
    TR_ASSERT(info->pieceSize != 0);

    tr_piece_index_t piece = byteOffset / info->pieceSize;

    /* handle 0-byte files at the end of a torrent */
    if (byteOffset == info->totalSize)
    {
        piece = info->pieceCount - 1;
    }

    return piece;
}

static void initFilePieces(tr_info* info, tr_file_index_t fileIndex)
{
    TR_ASSERT(info != NULL);
    TR_ASSERT(fileIndex < info->fileCount);

    tr_file* file = &info->files[fileIndex];
    uint64_t firstByte = file->offset;
    uint64_t lastByte = firstByte + (file->length != 0 ? file->length - 1 : 0);

    file->firstPiece = getBytePiece(info, firstByte);
    file->lastPiece = getBytePiece(info, lastByte);
}

static bool pieceHasFile(tr_piece_index_t piece, tr_file const* file)
{
    return file->firstPiece <= piece && piece <= file->lastPiece;
}

static tr_priority_t calculatePiecePriority(tr_torrent const* tor, tr_piece_index_t piece, int fileHint)
{
    tr_file_index_t firstFileIndex;
    tr_priority_t priority = TR_PRI_LOW;

    /* find the first file that has data in this piece */
    if (fileHint >= 0)
    {
        firstFileIndex = fileHint;

        while (firstFileIndex > 0 && pieceHasFile(piece, &tor->info.files[firstFileIndex - 1]))
        {
            --firstFileIndex;
        }
    }
    else
    {
        firstFileIndex = 0;

        for (tr_file_index_t i = 0; i < tor->info.fileCount; ++i, ++firstFileIndex)
        {
            if (pieceHasFile(piece, &tor->info.files[i]))
            {
                break;
            }
        }
    }

    /* the piece's priority is the max of the priorities
     * of all the files in that piece */
    for (tr_file_index_t i = firstFileIndex; i < tor->info.fileCount; ++i)
    {
        tr_file const* file = &tor->info.files[i];

        if (!pieceHasFile(piece, file))
        {
            break;
        }

        priority = MAX(priority, file->priority);

        /* when dealing with multimedia files, getting the first and
           last pieces can sometimes allow you to preview it a bit
           before it's fully downloaded... */
        if (file->priority >= TR_PRI_NORMAL)
        {
            if (file->firstPiece == piece || file->lastPiece == piece)
            {
                priority = TR_PRI_HIGH;
            }
        }
    }

    return priority;
}

static void tr_torrentInitFilePieces(tr_torrent* tor)
{
    uint64_t offset = 0;
    tr_info* inf = &tor->info;

    /* assign the file offsets */
    for (tr_file_index_t f = 0; f < inf->fileCount; ++f)
    {
        inf->files[f].offset = offset;
        offset += inf->files[f].length;
        initFilePieces(inf, f);
    }

    /* build the array of first-file hints to give calculatePiecePriority */
    int* firstFiles = tr_new(int, inf->pieceCount);
    tr_file_index_t f = 0;

    for (tr_piece_index_t p = 0; p < inf->pieceCount; ++p)
    {
        while (inf->files[f].lastPiece < p)
        {
            ++f;
        }

        firstFiles[p] = f;
    }

#if 0

    /* test to confirm the first-file hints are correct */
    for (tr_piece_index_t p = 0; p < inf->pieceCount; ++p)
    {
        tr_file_index_t f = firstFiles[p];

        TR_ASSERT(inf->files[f].firstPiece <= p);
        TR_ASSERT(inf->files[f].lastPiece >= p);

        if (f > 0)
        {
            TR_ASSERT(inf->files[f - 1].lastPiece < p);
        }

        f = 0;

        for (tr_file_index_t i = 0; i < inf->fileCount; ++i, ++f)
        {
            if (pieceHasFile(p, &inf->files[i]))
            {
                break;
            }
        }

        TR_ASSERT((int)f == firstFiles[p]);
    }

#endif

    for (tr_piece_index_t p = 0; p < inf->pieceCount; ++p)
    {
        inf->pieces[p].priority = calculatePiecePriority(tor, p, firstFiles[p]);
    }

    tr_free(firstFiles);
}

static void torrentStart(tr_torrent* tor, bool bypass_queue);

/**
 * Decide on a block size. Constraints:
 * (1) most clients decline requests over 16 KiB
 * (2) pieceSize must be a multiple of block size
 */
uint32_t tr_getBlockSize(uint32_t pieceSize)
{
    uint32_t b = pieceSize;

    while (b > MAX_BLOCK_SIZE)
    {
        b /= 2U;
    }

    if (b == 0 || pieceSize % b != 0) /* not cleanly divisible */
    {
        return 0;
    }

    return b;
}

static void refreshCurrentDir(tr_torrent* tor);

static void torrentInitFromInfo(tr_torrent* tor)
{
    uint64_t t;
    tr_info* info = &tor->info;

    tor->blockSize = tr_getBlockSize(info->pieceSize);

    if (info->pieceSize != 0)
    {
        tor->lastPieceSize = (uint32_t)(info->totalSize % info->pieceSize);
    }

    if (tor->lastPieceSize == 0)
    {
        tor->lastPieceSize = info->pieceSize;
    }

    if (tor->blockSize != 0)
    {
        tor->lastBlockSize = info->totalSize % tor->blockSize;
    }

    if (tor->lastBlockSize == 0)
    {
        tor->lastBlockSize = tor->blockSize;
    }

    tor->blockCount = tor->blockSize != 0 ? (info->totalSize + tor->blockSize - 1) / tor->blockSize : 0;
    tor->blockCountInPiece = tor->blockSize != 0 ? info->pieceSize / tor->blockSize : 0;
    tor->blockCountInLastPiece = tor->blockSize != 0 ? (tor->lastPieceSize + tor->blockSize - 1) / tor->blockSize : 0;

    /* check our work */
    if (tor->blockSize != 0)
    {
        TR_ASSERT(info->pieceSize % tor->blockSize == 0);
    }

    t = info->pieceCount - 1;
    t *= info->pieceSize;
    t += tor->lastPieceSize;
    TR_ASSERT(t == info->totalSize);

    t = tor->blockCount - 1;
    t *= tor->blockSize;
    t += tor->lastBlockSize;
    TR_ASSERT(t == info->totalSize);

    t = info->pieceCount - 1;
    t *= tor->blockCountInPiece;
    t += tor->blockCountInLastPiece;
    TR_ASSERT(t == (uint64_t)tor->blockCount);

    tr_cpConstruct(&tor->completion, tor);

    tr_torrentInitFilePieces(tor);

    tor->completeness = tr_cpGetStatus(&tor->completion);
}

static void tr_torrentFireMetadataCompleted(tr_torrent* tor);

void tr_torrentGotNewInfoDict(tr_torrent* tor)
{
    torrentInitFromInfo(tor);

    tr_peerMgrOnTorrentGotMetainfo(tor);

    tr_torrentFireMetadataCompleted(tor);
}

static bool hasAnyLocalData(tr_torrent const* tor)
{
    for (tr_file_index_t i = 0; i < tor->info.fileCount; ++i)
    {
        if (tr_torrentFindFile2(tor, i, NULL, NULL, NULL))
        {
            return true;
        }
    }

    return false;
}

static bool setLocalErrorIfFilesDisappeared(tr_torrent* tor)
{
    bool const disappeared = tr_torrentHaveTotal(tor) > 0 && !hasAnyLocalData(tor);

    if (disappeared)
    {
        tr_deeplog_tor(tor, "%s", "[LAZY] uh oh, the files disappeared");
        tr_torrentSetLocalError(tor, "%s", _("No data found! Ensure your drives are connected or use \"Set Location\". "
            "To re-download, remove the torrent and re-add it."));
    }

    return disappeared;
}

static void torrentInit(tr_torrent* tor, tr_ctor const* ctor)
{
    tr_session* session = tr_ctorGetSession(ctor);

    TR_ASSERT(session != NULL);

    tr_sessionLock(session);

    bool doStart;
    uint64_t loaded;
    char const* dir;
    bool isNewTorrent;
    static int nextUniqueId = 1;

    tor->session = session;
    tor->uniqueId = nextUniqueId++;
    tor->magicNumber = TORRENT_MAGIC_NUMBER;
    tor->queuePosition = session->torrentCount;
    tor->labels = TR_PTR_ARRAY_INIT;

    tr_sha1(tor->obfuscatedHash, "req2", 4, tor->info.hash, SHA_DIGEST_LENGTH, NULL);

    if (tr_ctorGetDownloadDir(ctor, TR_FORCE, &dir) || tr_ctorGetDownloadDir(ctor, TR_FALLBACK, &dir))
    {
        tor->downloadDir = tr_strdup(dir);
    }

    if (!tr_ctorGetIncompleteDir(ctor, &dir))
    {
        dir = tr_sessionGetIncompleteDir(session);
    }

    if (tr_sessionIsIncompleteDirEnabled(session))
    {
        tor->incompleteDir = tr_strdup(dir);
    }

    tr_bandwidthConstruct(&tor->bandwidth, session, &session->bandwidth);

    tor->bandwidth.priority = tr_ctorGetBandwidthPriority(ctor);
    tor->error = TR_STAT_OK;
    tor->finishedSeedingByIdle = false;

    tr_peerMgrAddTorrent(session->peerMgr, tor);

    TR_ASSERT(tor->downloadedCur == 0);
    TR_ASSERT(tor->uploadedCur == 0);

    tr_torrentSetDateAdded(tor, tr_time()); /* this is a default value to be overwritten by the resume file */

    torrentInitFromInfo(tor);

    bool didRenameResumeFileToHashOnlyName = false;
    loaded = tr_torrentLoadResume(tor, ~0, ctor, &didRenameResumeFileToHashOnlyName);

    if (didRenameResumeFileToHashOnlyName)
    {
        /* Rename torrent file as well */
        tr_metainfoMigrateFile(session, &tor->info, TR_METAINFO_BASENAME_NAME_AND_PARTIAL_HASH, TR_METAINFO_BASENAME_HASH);
    }

    tor->completeness = tr_cpGetStatus(&tor->completion);
    setLocalErrorIfFilesDisappeared(tor);

    tr_ctorInitTorrentPriorities(ctor, tor);
    tr_ctorInitTorrentWanted(ctor, tor);

    refreshCurrentDir(tor);

    doStart = tor->isRunning;
    tor->isRunning = false;

    if ((loaded & TR_FR_SPEEDLIMIT) == 0)
    {
        tr_torrentUseSpeedLimit(tor, TR_UP, false);
        tr_torrentSetSpeedLimit_Bps(tor, TR_UP, tr_sessionGetSpeedLimit_Bps(tor->session, TR_UP));
        tr_torrentUseSpeedLimit(tor, TR_DOWN, false);
        tr_torrentSetSpeedLimit_Bps(tor, TR_DOWN, tr_sessionGetSpeedLimit_Bps(tor->session, TR_DOWN));
        tr_torrentUseSessionLimits(tor, true);
    }

    if ((loaded & TR_FR_RATIOLIMIT) == 0)
    {
        tr_torrentSetRatioMode(tor, TR_RATIOLIMIT_GLOBAL);
        tr_torrentSetRatioLimit(tor, tr_sessionGetRatioLimit(tor->session));
    }

    if ((loaded & TR_FR_IDLELIMIT) == 0)
    {
        tr_torrentSetIdleMode(tor, TR_IDLELIMIT_GLOBAL);
        tr_torrentSetIdleLimit(tor, tr_sessionGetIdleLimit(tor->session));
    }

    /* add the torrent to tr_session.torrentList */
    session->torrentCount++;

    if (session->torrentList == NULL)
    {
        session->torrentList = tor;
    }
    else
    {
        tr_torrent* it = session->torrentList;

        while (it->next != NULL)
        {
            it = it->next;
        }

        it->next = tor;
    }

    /* if we don't have a local .torrent file already, assume the torrent is new */
    isNewTorrent = !tr_sys_path_exists(tor->info.torrent, NULL);

    /* maybe save our own copy of the metainfo */
    if (tr_ctorGetSave(ctor))
    {
        tr_variant const* val;

        if (tr_ctorGetMetainfo(ctor, &val))
        {
            char const* path = tor->info.torrent;
            int const err = tr_variantToFile(val, TR_VARIANT_FMT_BENC, path);

            if (err != 0)
            {
                tr_torrentSetLocalError(tor, "Unable to save torrent file: %s", tr_strerror(err));
            }

            tr_sessionSetTorrentFile(tor->session, tor->info.hashString, path);
        }
    }

    tor->tiers = tr_announcerAddTorrent(tor, onTrackerResponse, NULL);

    if (isNewTorrent)
    {
        tor->startAfterVerify = doStart;
        tr_torrentVerify(tor, NULL, NULL);
    }
    else if (doStart)
    {
        tr_torrentStart(tor);
    }

    tr_sessionUnlock(session);
}

static tr_parse_result torrentParseImpl(tr_ctor const* ctor, tr_info* setmeInfo, bool* setmeHasInfo, size_t* dictLength,
    int* setme_duplicate_id)
{
    bool doFree;
    bool didParse;
    bool hasInfo = false;
    tr_info tmp;
    tr_variant const* metainfo;
    tr_session* session = tr_ctorGetSession(ctor);
    tr_parse_result result = TR_PARSE_OK;

    if (setmeInfo == NULL)
    {
        setmeInfo = &tmp;
    }

    memset(setmeInfo, 0, sizeof(tr_info));

    if (!tr_ctorGetMetainfo(ctor, &metainfo))
    {
        return TR_PARSE_ERR;
    }

    didParse = tr_metainfoParse(session, metainfo, setmeInfo, &hasInfo, dictLength);
    doFree = didParse && (setmeInfo == &tmp);

    if (!didParse)
    {
        result = TR_PARSE_ERR;
    }

    if (didParse && hasInfo && tr_getBlockSize(setmeInfo->pieceSize) == 0)
    {
        result = TR_PARSE_ERR;
    }

    if (didParse && session != NULL && result == TR_PARSE_OK)
    {
        tr_torrent const* const tor = tr_torrentFindFromHash(session, setmeInfo->hash);

        if (tor != NULL)
        {
            result = TR_PARSE_DUPLICATE;

            if (setme_duplicate_id != NULL)
            {
                *setme_duplicate_id = tr_torrentId(tor);
            }
        }
    }

    if (doFree)
    {
        tr_metainfoFree(setmeInfo);
    }

    if (setmeHasInfo != NULL)
    {
        *setmeHasInfo = hasInfo;
    }

    return result;
}

tr_parse_result tr_torrentParse(tr_ctor const* ctor, tr_info* setmeInfo)
{
    return torrentParseImpl(ctor, setmeInfo, NULL, NULL, NULL);
}

tr_torrent* tr_torrentNew(tr_ctor const* ctor, int* setme_error, int* setme_duplicate_id)
{
    TR_ASSERT(ctor != NULL);
    TR_ASSERT(tr_isSession(tr_ctorGetSession(ctor)));

    size_t len;
    bool hasInfo;
    tr_info tmpInfo;
    tr_parse_result r;
    tr_torrent* tor = NULL;

    r = torrentParseImpl(ctor, &tmpInfo, &hasInfo, &len, setme_duplicate_id);

    if (r == TR_PARSE_OK)
    {
        tor = tr_new0(tr_torrent, 1);
        tor->info = tmpInfo;

        if (hasInfo)
        {
            tor->infoDictLength = len;
        }

        torrentInit(tor, ctor);
    }
    else
    {
        if (r == TR_PARSE_DUPLICATE)
        {
            tr_metainfoFree(&tmpInfo);
        }

        if (setme_error != NULL)
        {
            *setme_error = r;
        }
    }

    return tor;
}

/**
***
**/

void tr_torrentSetDownloadDir(tr_torrent* tor, char const* path)
{
    TR_ASSERT(tr_isTorrent(tor));

    if (path == NULL || tor->downloadDir == NULL || strcmp(path, tor->downloadDir) != 0)
    {
        tr_free(tor->downloadDir);
        tor->downloadDir = tr_strdup(path);

        tr_torrentMarkEdited(tor);
        tr_torrentSetDirty(tor);
    }

    refreshCurrentDir(tor);
}

char const* tr_torrentGetDownloadDir(tr_torrent const* tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    return tor->downloadDir;
}

char const* tr_torrentGetCurrentDir(tr_torrent const* tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    return tor->currentDir;
}

void tr_torrentChangeMyPort(tr_torrent* tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    if (tor->isRunning)
    {
        tr_announcerChangeMyPort(tor);
    }
}

static inline void tr_torrentManualUpdateImpl(void* vtor)
{
    tr_torrent* tor = vtor;

    TR_ASSERT(tr_isTorrent(tor));

    if (tor->isRunning)
    {
        tr_announcerManualAnnounce(tor);
    }
}

void tr_torrentManualUpdate(tr_torrent* tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    tr_runInEventThread(tor->session, tr_torrentManualUpdateImpl, tor);
}

bool tr_torrentCanManualUpdate(tr_torrent const* tor)
{
    return tr_isTorrent(tor) && tor->isRunning && tr_announcerCanManualAnnounce(tor);
}

tr_info const* tr_torrentInfo(tr_torrent const* tor)
{
    return tr_isTorrent(tor) ? &tor->info : NULL;
}

tr_stat const* tr_torrentStatCached(tr_torrent* tor)
{
    time_t const now = tr_time();

    return (tr_isTorrent(tor) && now == tor->lastStatTime) ? &tor->stats : tr_torrentStat(tor);
}

void tr_torrentSetVerifyState(tr_torrent* tor, tr_verify_state state)
{
    TR_ASSERT(tr_isTorrent(tor));
    TR_ASSERT(state == TR_VERIFY_NONE || state == TR_VERIFY_WAIT || state == TR_VERIFY_NOW);

    tor->verifyState = state;
    tor->anyDate = tr_time();
}

tr_torrent_activity tr_torrentGetActivity(tr_torrent const* tor)
{
    tr_torrent_activity ret = TR_STATUS_STOPPED;

    bool const is_seed = tr_torrentIsSeed(tor);

    if (tor->verifyState == TR_VERIFY_NOW)
    {
        ret = TR_STATUS_CHECK;
    }
    else if (tor->verifyState == TR_VERIFY_WAIT)
    {
        ret = TR_STATUS_CHECK_WAIT;
    }
    else if (tor->isRunning)
    {
        ret = is_seed ? TR_STATUS_SEED : TR_STATUS_DOWNLOAD;
    }
    else if (tr_torrentIsQueued(tor))
    {
        if (is_seed && tr_sessionGetQueueEnabled(tor->session, TR_UP))
        {
            ret = TR_STATUS_SEED_WAIT;
        }
        else if (!is_seed && tr_sessionGetQueueEnabled(tor->session, TR_DOWN))
        {
            ret = TR_STATUS_DOWNLOAD_WAIT;
        }
    }

    return ret;
}

static time_t torrentGetIdleSecs(tr_torrent const* tor)
{
    int idle_secs;
    tr_torrent_activity const activity = tr_torrentGetActivity(tor);

    if ((activity == TR_STATUS_DOWNLOAD || activity == TR_STATUS_SEED) && tor->startDate != 0)
    {
        idle_secs = difftime(tr_time(), MAX(tor->startDate, tor->activityDate));
    }
    else
    {
        idle_secs = -1;
    }

    return idle_secs;
}

bool tr_torrentIsStalled(tr_torrent const* tor)
{
    return tr_sessionGetQueueStalledEnabled(tor->session) &&
        torrentGetIdleSecs(tor) > tr_sessionGetQueueStalledMinutes(tor->session) * 60;
}

static double getVerifyProgress(tr_torrent const* tor)
{
    double d = 0;

    if (tr_torrentHasMetadata(tor))
    {
        tr_piece_index_t checked = 0;

        for (tr_piece_index_t i = 0; i < tor->info.pieceCount; ++i)
        {
            if (tor->info.pieces[i].timeChecked != 0)
            {
                ++checked;
            }
        }

        d = checked / (double)tor->info.pieceCount;
    }

    return d;
}

tr_stat const* tr_torrentStat(tr_torrent* tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    uint64_t const now = tr_time_msec();

    tr_stat* s;
    uint64_t seedRatioBytesLeft;
    uint64_t seedRatioBytesGoal;
    bool seedRatioApplies;
    uint16_t seedIdleMinutes;
    unsigned int pieceUploadSpeed_Bps;
    unsigned int pieceDownloadSpeed_Bps;
    struct tr_swarm_stats swarm_stats;

    tor->lastStatTime = tr_time();

    if (tor->swarm != NULL)
    {
        tr_swarmGetStats(tor->swarm, &swarm_stats);
    }
    else
    {
        swarm_stats = TR_SWARM_STATS_INIT;
    }

    s = &tor->stats;
    s->id = tor->uniqueId;
    s->activity = tr_torrentGetActivity(tor);
    s->error = tor->error;
    s->queuePosition = tor->queuePosition;
    s->isStalled = tr_torrentIsStalled(tor);
    tr_strlcpy(s->errorString, tor->errorString, sizeof(s->errorString));

    s->manualAnnounceTime = tr_announcerNextManualAnnounce(tor);
    s->peersConnected = swarm_stats.peerCount;
    s->peersSendingToUs = swarm_stats.activePeerCount[TR_DOWN];
    s->peersGettingFromUs = swarm_stats.activePeerCount[TR_UP];
    s->webseedsSendingToUs = swarm_stats.activeWebseedCount;

    for (int i = 0; i < TR_PEER_FROM__MAX; i++)
    {
        s->peersFrom[i] = swarm_stats.peerFromCount[i];
    }

    s->rawUploadSpeed_KBps = toSpeedKBps(tr_bandwidthGetRawSpeed_Bps(&tor->bandwidth, now, TR_UP));
    s->rawDownloadSpeed_KBps = toSpeedKBps(tr_bandwidthGetRawSpeed_Bps(&tor->bandwidth, now, TR_DOWN));
    pieceUploadSpeed_Bps = tr_bandwidthGetPieceSpeed_Bps(&tor->bandwidth, now, TR_UP);
    pieceDownloadSpeed_Bps = tr_bandwidthGetPieceSpeed_Bps(&tor->bandwidth, now, TR_DOWN);
    s->pieceUploadSpeed_KBps = toSpeedKBps(pieceUploadSpeed_Bps);
    s->pieceDownloadSpeed_KBps = toSpeedKBps(pieceDownloadSpeed_Bps);

    s->percentComplete = tr_cpPercentComplete(&tor->completion);
    s->metadataPercentComplete = tr_torrentGetMetadataPercent(tor);

    s->percentDone = tr_cpPercentDone(&tor->completion);
    s->leftUntilDone = tr_torrentGetLeftUntilDone(tor);
    s->sizeWhenDone = tr_cpSizeWhenDone(&tor->completion);
    s->recheckProgress = s->activity == TR_STATUS_CHECK ? getVerifyProgress(tor) : 0;
    s->activityDate = tor->activityDate;
    s->addedDate = tor->addedDate;
    s->doneDate = tor->doneDate;
    s->editDate = tor->editDate;
    s->startDate = tor->startDate;
    s->secondsSeeding = tor->secondsSeeding;
    s->secondsDownloading = tor->secondsDownloading;
    s->idleSecs = torrentGetIdleSecs(tor);

    s->corruptEver = tor->corruptCur + tor->corruptPrev;
    s->downloadedEver = tor->downloadedCur + tor->downloadedPrev;
    s->uploadedEver = tor->uploadedCur + tor->uploadedPrev;
    s->haveValid = tr_cpHaveValid(&tor->completion);
    s->haveUnchecked = tr_torrentHaveTotal(tor) - s->haveValid;
    s->desiredAvailable = tr_peerMgrGetDesiredAvailable(tor);

    s->ratio = tr_getRatio(s->uploadedEver, s->downloadedEver != 0 ? s->downloadedEver : s->haveValid);

    seedRatioApplies = tr_torrentGetSeedRatioBytes(tor, &seedRatioBytesLeft, &seedRatioBytesGoal);

    switch (s->activity)
    {
    /* etaXLSpeed exists because if we use the piece speed directly,
     * brief fluctuations cause the ETA to jump all over the place.
     * so, etaXLSpeed is a smoothed-out version of the piece speed
     * to dampen the effect of fluctuations */
    case TR_STATUS_DOWNLOAD:
        if (tor->etaDLSpeedCalculatedAt + 800 < now)
        {
            tor->etaDLSpeed_Bps = tor->etaDLSpeedCalculatedAt + 4000 < now ?
                pieceDownloadSpeed_Bps : /* if no recent previous speed, no need to smooth */
                (tor->etaDLSpeed_Bps * 4.0 + pieceDownloadSpeed_Bps) / 5.0; /* smooth across 5 readings */
            tor->etaDLSpeedCalculatedAt = now;
        }

        if (s->leftUntilDone > s->desiredAvailable && tor->info.webseedCount < 1)
        {
            s->eta = TR_ETA_NOT_AVAIL;
        }
        else if (tor->etaDLSpeed_Bps == 0)
        {
            s->eta = TR_ETA_UNKNOWN;
        }
        else
        {
            s->eta = s->leftUntilDone / tor->etaDLSpeed_Bps;
        }

        s->etaIdle = TR_ETA_NOT_AVAIL;
        break;

    case TR_STATUS_SEED:
        if (!seedRatioApplies)
        {
            s->eta = TR_ETA_NOT_AVAIL;
        }
        else
        {
            if (tor->etaULSpeedCalculatedAt + 800 < now)
            {
                tor->etaULSpeed_Bps = tor->etaULSpeedCalculatedAt + 4000 < now ?
                    pieceUploadSpeed_Bps : /* if no recent previous speed, no need to smooth */
                    (tor->etaULSpeed_Bps * 4.0 + pieceUploadSpeed_Bps) / 5.0; /* smooth across 5 readings */
                tor->etaULSpeedCalculatedAt = now;
            }

            if (tor->etaULSpeed_Bps == 0)
            {
                s->eta = TR_ETA_UNKNOWN;
            }
            else
            {
                s->eta = seedRatioBytesLeft / tor->etaULSpeed_Bps;
            }
        }

        if (tor->etaULSpeed_Bps < 1 && tr_torrentGetSeedIdle(tor, &seedIdleMinutes))
        {
            s->etaIdle = seedIdleMinutes * 60 - s->idleSecs;
        }
        else
        {
            s->etaIdle = TR_ETA_NOT_AVAIL;
        }

        break;

    default:
        s->eta = TR_ETA_NOT_AVAIL;
        s->etaIdle = TR_ETA_NOT_AVAIL;
        break;
    }

    /* s->haveValid is here to make sure a torrent isn't marked 'finished'
     * when the user hits "uncheck all" prior to starting the torrent... */
    s->finished = tor->finishedSeedingByIdle || (seedRatioApplies && seedRatioBytesLeft == 0 && s->haveValid != 0);

    if (!seedRatioApplies || s->finished)
    {
        s->seedRatioPercentDone = 1;
    }
    else if (seedRatioBytesGoal == 0) /* impossible? safeguard for div by zero */
    {
        s->seedRatioPercentDone = 0;
    }
    else
    {
        s->seedRatioPercentDone = (double)(seedRatioBytesGoal - seedRatioBytesLeft) / seedRatioBytesGoal;
    }

    /* test some of the constraints */
    TR_ASSERT(s->sizeWhenDone <= tor->info.totalSize);
    TR_ASSERT(s->leftUntilDone <= s->sizeWhenDone);
    TR_ASSERT(s->desiredAvailable <= s->leftUntilDone);

    return s;
}

/***
****
***/

static uint64_t countFileBytesCompleted(tr_torrent const* tor, tr_file_index_t index)
{
    uint64_t total = 0;
    tr_file const* f = &tor->info.files[index];

    if (f->length != 0)
    {
        tr_block_index_t first;
        tr_block_index_t last;
        tr_torGetFileBlockRange(tor, index, &first, &last);

        if (first == last)
        {
            if (tr_torrentBlockIsComplete(tor, first))
            {
                total = f->length;
            }
        }
        else
        {
            /* the first block */
            if (tr_torrentBlockIsComplete(tor, first))
            {
                total += tor->blockSize - f->offset % tor->blockSize;
            }

            /* the middle blocks */
            if (first + 1 < last)
            {
                uint64_t u = tr_bitfieldCountRange(&tor->completion.blockBitfield, first + 1, last);
                u *= tor->blockSize;
                total += u;
            }

            /* the last block */
            if (tr_torrentBlockIsComplete(tor, last))
            {
                total += f->offset + f->length - (uint64_t)tor->blockSize * last;
            }
        }
    }

    return total;
}

tr_file_stat* tr_torrentFiles(tr_torrent const* tor, tr_file_index_t* fileCount)
{
    TR_ASSERT(tr_isTorrent(tor));

    tr_file_index_t const n = tor->info.fileCount;
    tr_file_stat* files = tr_new0(tr_file_stat, n);
    tr_file_stat* walk = files;
    bool const isSeed = tor->completeness == TR_SEED;

    for (tr_file_index_t i = 0; i < n; ++i, ++walk)
    {
        uint64_t const b = isSeed ? tor->info.files[i].length : countFileBytesCompleted(tor, i);
        walk->bytesCompleted = b;
        walk->progress = tor->info.files[i].length > 0 ? (float)b / tor->info.files[i].length : 1.0F;
    }

    if (fileCount != NULL)
    {
        *fileCount = n;
    }

    return files;
}

void tr_torrentFilesFree(tr_file_stat* files, tr_file_index_t fileCount UNUSED)
{
    tr_free(files);
}

/***
****
***/

double* tr_torrentWebSpeeds_KBps(tr_torrent const* tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    return tr_peerMgrWebSpeeds_KBps(tor);
}

tr_peer_stat* tr_torrentPeers(tr_torrent const* tor, int* peerCount)
{
    TR_ASSERT(tr_isTorrent(tor));

    return tr_peerMgrPeerStats(tor, peerCount);
}

void tr_torrentPeersFree(tr_peer_stat* peers, int peerCount UNUSED)
{
    tr_free(peers);
}

tr_tracker_stat* tr_torrentTrackers(tr_torrent const* tor, int* setmeTrackerCount)
{
    TR_ASSERT(tr_isTorrent(tor));

    return tr_announcerStats(tor, setmeTrackerCount);
}

void tr_torrentTrackersFree(tr_tracker_stat* trackers, int trackerCount)
{
    tr_announcerStatsFree(trackers, trackerCount);
}

void tr_torrentAvailability(tr_torrent const* tor, int8_t* tab, int size)
{
    TR_ASSERT(tr_isTorrent(tor));

    if (tab != NULL && size > 0)
    {
        tr_peerMgrTorrentAvailability(tor, tab, size);
    }
}

void tr_torrentAmountFinished(tr_torrent const* tor, float* tab, int size)
{
    tr_cpGetAmountDone(&tor->completion, tab, size);
}

static void tr_torrentResetTransferStats(tr_torrent* tor)
{
    tr_torrentLock(tor);

    tor->downloadedPrev += tor->downloadedCur;
    tor->downloadedCur = 0;
    tor->uploadedPrev += tor->uploadedCur;
    tor->uploadedCur = 0;
    tor->corruptPrev += tor->corruptCur;
    tor->corruptCur = 0;

    tr_torrentSetDirty(tor);

    tr_torrentUnlock(tor);
}

void tr_torrentSetHasPiece(tr_torrent* tor, tr_piece_index_t pieceIndex, bool has)
{
    TR_ASSERT(tr_isTorrent(tor));
    TR_ASSERT(pieceIndex < tor->info.pieceCount);

    if (has)
    {
        tr_cpPieceAdd(&tor->completion, pieceIndex);
    }
    else
    {
        tr_cpPieceRem(&tor->completion, pieceIndex);
    }
}

/***
****
***/

#ifdef TR_ENABLE_ASSERTS
static bool queueIsSequenced(tr_session*);
#endif

static void freeTorrent(tr_torrent* tor)
{
    TR_ASSERT(!tor->isRunning);

    tr_session* session = tor->session;
    tr_info* inf = &tor->info;
    time_t const now = tr_time();

    tr_sessionLock(session);

    tr_peerMgrRemoveTorrent(tor);

    tr_announcerRemoveTorrent(session->announcer, tor);

    tr_cpDestruct(&tor->completion);

    tr_free(tor->downloadDir);
    tr_free(tor->incompleteDir);

    if (tor == session->torrentList)
    {
        session->torrentList = tor->next;
    }
    else
    {
        for (tr_torrent* t = session->torrentList; t != NULL; t = t->next)
        {
            if (t->next == tor)
            {
                t->next = tor->next;
                break;
            }
        }
    }

    /* decrement the torrent count */
    TR_ASSERT(session->torrentCount >= 1);
    session->torrentCount--;

    /* resequence the queue positions */
    tr_torrent* t = NULL;

    while ((t = tr_torrentNext(session, t)) != NULL)
    {
        if (t->queuePosition > tor->queuePosition)
        {
            t->queuePosition--;
            t->anyDate = now;
        }
    }

    TR_ASSERT(queueIsSequenced(session));

    tr_bandwidthDestruct(&tor->bandwidth);
    tr_ptrArrayDestruct(&tor->labels, tr_free);

    tr_metainfoFree(inf);
    memset(tor, ~0, sizeof(tr_torrent));
    tr_free(tor);

    tr_sessionUnlock(session);
}

/**
***  Start/Stop Callback
**/

static void torrentSetQueued(tr_torrent* tor, bool queued);

static void torrentStartImpl(void* vtor)
{
    tr_torrent* tor = vtor;

    TR_ASSERT(tr_isTorrent(tor));

    tr_sessionLock(tor->session);

    tr_torrentRecheckCompleteness(tor);
    torrentSetQueued(tor, false);

    time_t const now = tr_time();

    tor->isRunning = true;
    tor->completeness = tr_cpGetStatus(&tor->completion);
    tor->startDate = now;
    tor->anyDate = now;
    tr_torrentClearError(tor);
    tor->finishedSeedingByIdle = false;

    tr_torrentResetTransferStats(tor);
    tr_announcerTorrentStarted(tor);
    tor->dhtAnnounceAt = now + tr_rand_int_weak(20);
    tor->dhtAnnounce6At = now + tr_rand_int_weak(20);
    tor->lpdAnnounceAt = now;
    tr_peerMgrStartTorrent(tor);

    tr_sessionUnlock(tor->session);
}

uint64_t tr_torrentGetCurrentSizeOnDisk(tr_torrent const* tor)
{
    uint64_t byte_count = 0;
    tr_file_index_t const n = tor->info.fileCount;

    for (tr_file_index_t i = 0; i < n; ++i)
    {
        tr_sys_path_info info;
        char* filename = tr_torrentFindFile(tor, i);

        if (filename != NULL && tr_sys_path_get_info(filename, 0, &info, NULL))
        {
            byte_count += info.size;
        }

        tr_free(filename);
    }

    return byte_count;
}

static bool torrentShouldQueue(tr_torrent const* tor)
{
    tr_direction const dir = tr_torrentGetQueueDirection(tor);

    return tr_sessionCountQueueFreeSlots(tor->session, dir) == 0;
}

static void torrentStart(tr_torrent* tor, bool bypass_queue)
{
    switch (tr_torrentGetActivity(tor))
    {
    case TR_STATUS_SEED:
    case TR_STATUS_DOWNLOAD:
        return; /* already started */
        break;

    case TR_STATUS_SEED_WAIT:
    case TR_STATUS_DOWNLOAD_WAIT:
        if (!bypass_queue)
        {
            return; /* already queued */
        }

        break;

    case TR_STATUS_CHECK:
    case TR_STATUS_CHECK_WAIT:
        /* verifying right now... wait until that's done so
         * we'll know what completeness to use/announce */
        tor->startAfterVerify = true;
        return;
        break;

    case TR_STATUS_STOPPED:
        if (!bypass_queue && torrentShouldQueue(tor))
        {
            torrentSetQueued(tor, true);
            return;
        }

        break;
    }

    /* don't allow the torrent to be started if the files disappeared */
    if (setLocalErrorIfFilesDisappeared(tor))
    {
        return;
    }

    /* otherwise, start it now... */
    tr_sessionLock(tor->session);

    /* allow finished torrents to be resumed */
    if (tr_torrentIsSeedRatioDone(tor))
    {
        tr_logAddTorInfo(tor, "%s", _("Restarted manually -- disabling its seed ratio"));
        tr_torrentSetRatioMode(tor, TR_RATIOLIMIT_UNLIMITED);
    }

    /* corresponds to the peer_id sent as a tracker request parameter.
     * one tracker admin says: "When the same torrent is opened and
     * closed and opened again without quitting Transmission ...
     * change the peerid. It would help sometimes if a stopped event
     * was missed to ensure that we didn't think someone was cheating. */
    tr_torrentUnsetPeerId(tor);
    tor->isRunning = true;
    tr_torrentSetDirty(tor);
    tr_runInEventThread(tor->session, torrentStartImpl, tor);

    tr_sessionUnlock(tor->session);
}

void tr_torrentStart(tr_torrent* tor)
{
    if (tr_isTorrent(tor))
    {
        torrentStart(tor, false);
    }
}

void tr_torrentStartNow(tr_torrent* tor)
{
    if (tr_isTorrent(tor))
    {
        torrentStart(tor, true);
    }
}

struct verify_data
{
    bool aborted;
    tr_torrent* tor;
    tr_verify_done_func callback_func;
    void* callback_data;
};

static void onVerifyDoneThreadFunc(void* vdata)
{
    struct verify_data* data = vdata;
    tr_torrent* tor = data->tor;

    if (tor->isDeleting)
    {
        goto cleanup;
    }

    if (!data->aborted)
    {
        tr_torrentRecheckCompleteness(tor);
    }

    if (data->callback_func != NULL)
    {
        (*data->callback_func)(tor, data->aborted, data->callback_data);
    }

    if (!data->aborted && tor->startAfterVerify)
    {
        tor->startAfterVerify = false;
        torrentStart(tor, false);
    }

cleanup:
    tr_free(data);
}

static void onVerifyDone(tr_torrent* tor, bool aborted, void* vdata)
{
    struct verify_data* data = vdata;

    TR_ASSERT(data->tor == tor);

    if (tor->isDeleting)
    {
        tr_free(data);
        return;
    }

    data->aborted = aborted;
    tr_runInEventThread(tor->session, onVerifyDoneThreadFunc, data);
}

static void verifyTorrent(void* vdata)
{
    bool startAfter;
    struct verify_data* data = vdata;
    tr_torrent* tor = data->tor;
    tr_sessionLock(tor->session);

    if (tor->isDeleting)
    {
        tr_free(data);
        goto unlock;
    }

    /* if the torrent's already being verified, stop it */
    tr_verifyRemove(tor);

    startAfter = (tor->isRunning || tor->startAfterVerify) && !tor->isStopping;

    if (tor->isRunning)
    {
        tr_torrentStop(tor);
    }

    tor->startAfterVerify = startAfter;

    if (setLocalErrorIfFilesDisappeared(tor))
    {
        tor->startAfterVerify = false;
    }
    else
    {
        tr_verifyAdd(tor, onVerifyDone, data);
    }

unlock:
    tr_sessionUnlock(tor->session);
}

void tr_torrentVerify(tr_torrent* tor, tr_verify_done_func callback_func, void* callback_data)
{
    struct verify_data* data;

    data = tr_new(struct verify_data, 1);
    data->tor = tor;
    data->aborted = false;
    data->callback_func = callback_func;
    data->callback_data = callback_data;
    tr_runInEventThread(tor->session, verifyTorrent, data);
}

void tr_torrentSave(tr_torrent* tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    if (tor->isDirty)
    {
        tor->isDirty = false;
        tr_torrentSaveResume(tor);
    }
}

static void stopTorrent(void* vtor)
{
    tr_torrent* tor = vtor;

    TR_ASSERT(tr_isTorrent(tor));

    tr_logAddTorInfo(tor, "%s", "Pausing");

    tr_torrentLock(tor);

    tr_verifyRemove(tor);
    tr_peerMgrStopTorrent(tor);
    tr_announcerTorrentStopped(tor);
    tr_cacheFlushTorrent(tor->session->cache, tor);

    tr_fdTorrentClose(tor->session, tor->uniqueId);

    if (!tor->isDeleting)
    {
        tr_torrentSave(tor);
    }

    torrentSetQueued(tor, false);

    tr_torrentUnlock(tor);

    if (tor->magnetVerify)
    {
        tor->magnetVerify = false;
        tr_logAddTorInfo(tor, "%s", "Magnet Verify");
        refreshCurrentDir(tor);
        tr_torrentVerify(tor, NULL, NULL);
    }
}

void tr_torrentStop(tr_torrent* tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    if (tr_isTorrent(tor))
    {
        tr_sessionLock(tor->session);

        tor->isRunning = false;
        tor->isStopping = false;
        tr_torrentSetDirty(tor);
        tr_runInEventThread(tor->session, stopTorrent, tor);

        tr_sessionUnlock(tor->session);
    }
}

static void closeTorrent(void* vtor)
{
    tr_torrent* tor = vtor;

    TR_ASSERT(tr_isTorrent(tor));

    tr_variant* d = tr_variantListAddDict(&tor->session->removedTorrents, 2);
    tr_variantDictAddInt(d, TR_KEY_id, tor->uniqueId);
    tr_variantDictAddInt(d, TR_KEY_date, tr_time());

    tr_logAddTorInfo(tor, "%s", _("Removing torrent"));

    tor->magnetVerify = false;
    stopTorrent(tor);

    if (tor->isDeleting)
    {
        tr_metainfoRemoveSaved(tor->session, &tor->info);
        tr_torrentRemoveResume(tor);
    }

    tor->isRunning = false;
    freeTorrent(tor);
}

void tr_torrentFree(tr_torrent* tor)
{
    if (tr_isTorrent(tor))
    {
        tr_session* session = tor->session;

        TR_ASSERT(tr_isSession(session));

        tr_sessionLock(session);

        tr_torrentClearCompletenessCallback(tor);
        tr_runInEventThread(session, closeTorrent, tor);

        tr_sessionUnlock(session);
    }
}

struct remove_data
{
    tr_torrent* tor;
    bool deleteFlag;
    tr_fileFunc deleteFunc;
};

static void tr_torrentDeleteLocalData(tr_torrent*, tr_fileFunc);

static void removeTorrent(void* vdata)
{
    struct remove_data* data = vdata;
    tr_session* session = data->tor->session;
    tr_sessionLock(session);

    if (data->deleteFlag)
    {
        tr_torrentDeleteLocalData(data->tor, data->deleteFunc);
    }

    tr_torrentClearCompletenessCallback(data->tor);
    closeTorrent(data->tor);
    tr_free(data);

    tr_sessionUnlock(session);
}

void tr_torrentRemove(tr_torrent* tor, bool deleteFlag, tr_fileFunc deleteFunc)
{
    TR_ASSERT(tr_isTorrent(tor));

    tor->isDeleting = true;

    struct remove_data* data = tr_new0(struct remove_data, 1);
    data->tor = tor;
    data->deleteFlag = deleteFlag;
    data->deleteFunc = deleteFunc;
    tr_runInEventThread(tor->session, removeTorrent, data);
}

/**
***  Completeness
**/

static char const* getCompletionString(int type)
{
    switch (type)
    {
    case TR_PARTIAL_SEED:
        /* Translators: this is a minor point that's safe to skip over, but FYI:
           "Complete" and "Done" are specific, different terms in Transmission:
           "Complete" means we've downloaded every file in the torrent.
           "Done" means we're done downloading the files we wanted, but NOT all
           that exist */
        return _("Done");

    case TR_SEED:
        return _("Complete");

    default:
        return _("Incomplete");
    }
}

static void fireCompletenessChange(tr_torrent* tor, tr_completeness status, bool wasRunning)
{
    TR_ASSERT(status == TR_LEECH || status == TR_SEED || status == TR_PARTIAL_SEED);

    if (tor->completeness_func != NULL)
    {
        (*tor->completeness_func)(tor, status, wasRunning, tor->completeness_func_user_data);
    }
}

void tr_torrentSetCompletenessCallback(tr_torrent* tor, tr_torrent_completeness_func func, void* user_data)
{
    TR_ASSERT(tr_isTorrent(tor));

    tor->completeness_func = func;
    tor->completeness_func_user_data = user_data;
}

void tr_torrentClearCompletenessCallback(tr_torrent* torrent)
{
    tr_torrentSetCompletenessCallback(torrent, NULL, NULL);
}

void tr_torrentSetRatioLimitHitCallback(tr_torrent* tor, tr_torrent_ratio_limit_hit_func func, void* user_data)
{
    TR_ASSERT(tr_isTorrent(tor));

    tor->ratio_limit_hit_func = func;
    tor->ratio_limit_hit_func_user_data = user_data;
}

void tr_torrentClearRatioLimitHitCallback(tr_torrent* torrent)
{
    tr_torrentSetRatioLimitHitCallback(torrent, NULL, NULL);
}

void tr_torrentSetIdleLimitHitCallback(tr_torrent* tor, tr_torrent_idle_limit_hit_func func, void* user_data)
{
    TR_ASSERT(tr_isTorrent(tor));

    tor->idle_limit_hit_func = func;
    tor->idle_limit_hit_func_user_data = user_data;
}

void tr_torrentClearIdleLimitHitCallback(tr_torrent* torrent)
{
    tr_torrentSetIdleLimitHitCallback(torrent, NULL, NULL);
}

static void get_local_time_str(char* const buffer, size_t const buffer_len)
{
    time_t const now = tr_time();

    tr_strlcpy(buffer, ctime(&now), buffer_len);

    char* newline_pos = strchr(buffer, '\n');

    /* ctime() includes '\n', but it's better to be safe */
    if (newline_pos != NULL)
    {
        *newline_pos = '\0';
    }
}

static void torrentCallScript(tr_torrent const* tor, char const* script)
{
    if (tr_str_is_empty(script))
    {
        return;
    }

    char time_str[32];
    get_local_time_str(time_str, TR_N_ELEMENTS(time_str));

    char* const torrent_dir = tr_sys_path_native_separators(tr_strdup(tor->currentDir));

    char* const cmd[] =
    {
        tr_strdup(script),
        NULL
    };

    char* labels = tr_strjoin((char const* const*)tr_ptrArrayBase(&tor->labels), tr_ptrArraySize(&tor->labels), ",");

    char* const env[] =
    {
        tr_strdup_printf("TR_APP_VERSION=%s", SHORT_VERSION_STRING),
        tr_strdup_printf("TR_TIME_LOCALTIME=%s", time_str),
        tr_strdup_printf("TR_TORRENT_DIR=%s", torrent_dir),
        tr_strdup_printf("TR_TORRENT_HASH=%s", tor->info.hashString),
        tr_strdup_printf("TR_TORRENT_ID=%d", tr_torrentId(tor)),
        tr_strdup_printf("TR_TORRENT_NAME=%s", tr_torrentName(tor)),
        tr_strdup_printf("TR_TORRENT_LABELS=%s", labels),
        NULL
    };

    tr_logAddTorInfo(tor, "Calling script \"%s\"", script);

    tr_error* error = NULL;

    if (!tr_spawn_async(cmd, env, TR_IF_WIN32("\\", "/"), &error))
    {
        tr_logAddTorErr(tor, "Error executing script \"%s\" (%d): %s", script, error->code, error->message);
        tr_error_free(error);
    }

    tr_free_ptrv((void* const*)env);
    tr_free_ptrv((void* const*)cmd);
    tr_free(labels);
    tr_free(torrent_dir);
}

void tr_torrentRecheckCompleteness(tr_torrent* tor)
{
    tr_completeness completeness;

    tr_torrentLock(tor);

    completeness = tr_cpGetStatus(&tor->completion);

    if (completeness != tor->completeness)
    {
        bool const recentChange = tor->downloadedCur != 0;
        bool const wasLeeching = !tr_torrentIsSeed(tor);
        bool const wasRunning = tor->isRunning;

        if (recentChange)
        {
            tr_logAddTorInfo(tor, _("State changed from \"%1$s\" to \"%2$s\""), getCompletionString(tor->completeness),
                getCompletionString(completeness));
        }

        tor->completeness = completeness;
        tr_fdTorrentClose(tor->session, tor->uniqueId);

        if (tr_torrentIsSeed(tor))
        {
            if (recentChange)
            {
                tr_announcerTorrentCompleted(tor);
                tor->doneDate = tor->anyDate = tr_time();
            }

            if (wasLeeching && wasRunning)
            {
                /* clear interested flag on all peers */
                tr_peerMgrClearInterest(tor);
            }

            if (tor->currentDir == tor->incompleteDir)
            {
                tr_torrentSetLocation(tor, tor->downloadDir, true, NULL, NULL);
            }
        }

        fireCompletenessChange(tor, completeness, wasRunning);

        if (tr_torrentIsSeed(tor) && wasLeeching && wasRunning)
        {
            /* if completeness was TR_LEECH, the seed limit check
               will have been skipped in bandwidthPulse */
            tr_torrentCheckSeedLimit(tor);
        }

        tr_torrentSetDirty(tor);

        if (tr_torrentIsSeed(tor) && tr_sessionIsTorrentDoneScriptEnabled(tor->session))
        {
            tr_torrentSave(tor);

            torrentCallScript(tor, tr_sessionGetTorrentDoneScript(tor->session));
        }
    }

    tr_torrentUnlock(tor);
}

/***
****
***/

static void tr_torrentFireMetadataCompleted(tr_torrent* tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    if (tor->metadata_func != NULL)
    {
        (*tor->metadata_func)(tor, tor->metadata_func_user_data);
    }
}

void tr_torrentSetMetadataCallback(tr_torrent* tor, tr_torrent_metadata_func func, void* user_data)
{
    TR_ASSERT(tr_isTorrent(tor));

    tor->metadata_func = func;
    tor->metadata_func_user_data = user_data;
}

/**
***  File priorities
**/

void tr_torrentInitFilePriority(tr_torrent* tor, tr_file_index_t fileIndex, tr_priority_t priority)
{
    TR_ASSERT(tr_isTorrent(tor));
    TR_ASSERT(fileIndex < tor->info.fileCount);
    TR_ASSERT(tr_isPriority(priority));

    tr_file* file = &tor->info.files[fileIndex];

    file->priority = priority;

    for (tr_piece_index_t i = file->firstPiece; i <= file->lastPiece; ++i)
    {
        tor->info.pieces[i].priority = calculatePiecePriority(tor, i, fileIndex);
    }
}

void tr_torrentSetFilePriorities(tr_torrent* tor, tr_file_index_t const* files, tr_file_index_t fileCount,
    tr_priority_t priority)
{
    TR_ASSERT(tr_isTorrent(tor));

    tr_torrentLock(tor);

    for (tr_file_index_t i = 0; i < fileCount; ++i)
    {
        if (files[i] < tor->info.fileCount)
        {
            tr_torrentInitFilePriority(tor, files[i], priority);
        }
    }

    tr_torrentSetDirty(tor);
    tr_peerMgrRebuildRequests(tor);

    tr_torrentUnlock(tor);
}

tr_priority_t* tr_torrentGetFilePriorities(tr_torrent const* tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    tr_priority_t* p = tr_new0(tr_priority_t, tor->info.fileCount);

    for (tr_file_index_t i = 0; i < tor->info.fileCount; ++i)
    {
        p[i] = tor->info.files[i].priority;
    }

    return p;
}

/**
***  File DND
**/

static void setFileDND(tr_torrent* tor, tr_file_index_t fileIndex, bool doDownload)
{
    bool const dnd = !doDownload;
    tr_piece_index_t firstPiece;
    bool firstPieceDND;
    tr_piece_index_t lastPiece;
    bool lastPieceDND;
    tr_file* file = &tor->info.files[fileIndex];

    file->dnd = dnd;
    firstPiece = file->firstPiece;
    lastPiece = file->lastPiece;

    /* can't set the first piece to DND unless
       every file using that piece is DND */
    firstPieceDND = dnd;

    if (fileIndex > 0)
    {
        for (tr_file_index_t i = fileIndex - 1; firstPieceDND; --i)
        {
            if (tor->info.files[i].lastPiece != firstPiece)
            {
                break;
            }

            firstPieceDND = tor->info.files[i].dnd;

            if (i == 0)
            {
                break;
            }
        }
    }

    /* can't set the last piece to DND unless
       every file using that piece is DND */
    lastPieceDND = dnd;

    for (tr_file_index_t i = fileIndex + 1; lastPieceDND && i < tor->info.fileCount; ++i)
    {
        if (tor->info.files[i].firstPiece != lastPiece)
        {
            break;
        }

        lastPieceDND = tor->info.files[i].dnd;
    }

    if (firstPiece == lastPiece)
    {
        tor->info.pieces[firstPiece].dnd = firstPieceDND && lastPieceDND;
    }
    else
    {
        tor->info.pieces[firstPiece].dnd = firstPieceDND;
        tor->info.pieces[lastPiece].dnd = lastPieceDND;

        for (tr_piece_index_t pp = firstPiece + 1; pp < lastPiece; ++pp)
        {
            tor->info.pieces[pp].dnd = dnd;
        }
    }
}

void tr_torrentInitFileDLs(tr_torrent* tor, tr_file_index_t const* files, tr_file_index_t fileCount, bool doDownload)
{
    TR_ASSERT(tr_isTorrent(tor));

    tr_torrentLock(tor);

    for (tr_file_index_t i = 0; i < fileCount; ++i)
    {
        if (files[i] < tor->info.fileCount)
        {
            setFileDND(tor, files[i], doDownload);
        }
    }

    tr_cpInvalidateDND(&tor->completion);

    tr_torrentUnlock(tor);
}

void tr_torrentSetFileDLs(tr_torrent* tor, tr_file_index_t const* files, tr_file_index_t fileCount, bool doDownload)
{
    TR_ASSERT(tr_isTorrent(tor));

    tr_torrentLock(tor);

    tr_torrentInitFileDLs(tor, files, fileCount, doDownload);
    tr_torrentSetDirty(tor);
    tr_torrentRecheckCompleteness(tor);
    tr_peerMgrRebuildRequests(tor);

    tr_torrentUnlock(tor);
}

/***
****
***/

void tr_torrentSetLabels(tr_torrent* tor, tr_ptrArray* labels)
{
    TR_ASSERT(tr_isTorrent(tor));

    tr_torrentLock(tor);

    tr_ptrArrayDestruct(&tor->labels, tr_free);
    tor->labels = TR_PTR_ARRAY_INIT;
    char** l = (char**)tr_ptrArrayBase(labels);
    int const n = tr_ptrArraySize(labels);
    for (int i = 0; i < n; i++)
    {
        tr_ptrArrayAppend(&tor->labels, tr_strdup(l[i]));
    }

    tr_torrentSetDirty(tor);

    tr_torrentUnlock(tor);
}

/***
****
***/

tr_priority_t tr_torrentGetPriority(tr_torrent const* tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    return tor->bandwidth.priority;
}

void tr_torrentSetPriority(tr_torrent* tor, tr_priority_t priority)
{
    TR_ASSERT(tr_isTorrent(tor));
    TR_ASSERT(tr_isPriority(priority));

    if (tor->bandwidth.priority != priority)
    {
        tor->bandwidth.priority = priority;

        tr_torrentSetDirty(tor);
    }
}

/***
****
***/

void tr_torrentSetPeerLimit(tr_torrent* tor, uint16_t maxConnectedPeers)
{
    TR_ASSERT(tr_isTorrent(tor));

    if (tor->maxConnectedPeers != maxConnectedPeers)
    {
        tor->maxConnectedPeers = maxConnectedPeers;

        tr_torrentSetDirty(tor);
    }
}

uint16_t tr_torrentGetPeerLimit(tr_torrent const* tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    return tor->maxConnectedPeers;
}

/***
****
***/

void tr_torrentGetBlockLocation(tr_torrent const* tor, tr_block_index_t block, tr_piece_index_t* piece, uint32_t* offset,
    uint32_t* length)
{
    uint64_t pos = block;
    pos *= tor->blockSize;
    *piece = pos / tor->info.pieceSize;
    *offset = pos - *piece * tor->info.pieceSize;
    *length = tr_torBlockCountBytes(tor, block);
}

tr_block_index_t _tr_block(tr_torrent const* tor, tr_piece_index_t index, uint32_t offset)
{
    TR_ASSERT(tr_isTorrent(tor));

    tr_block_index_t ret;

    ret = index;
    ret *= tor->info.pieceSize / tor->blockSize;
    ret += offset / tor->blockSize;
    return ret;
}

bool tr_torrentReqIsValid(tr_torrent const* tor, tr_piece_index_t index, uint32_t offset, uint32_t length)
{
    TR_ASSERT(tr_isTorrent(tor));

    int err = 0;

    if (index >= tor->info.pieceCount)
    {
        err = 1;
    }
    else if (length < 1)
    {
        err = 2;
    }
    else if (offset + length > tr_torPieceCountBytes(tor, index))
    {
        err = 3;
    }
    else if (length > MAX_BLOCK_SIZE)
    {
        err = 4;
    }
    else if (tr_pieceOffset(tor, index, offset, length) > tor->info.totalSize)
    {
        err = 5;
    }

    if (err != 0)
    {
        tr_logAddTorDbg(tor, "index %lu offset %lu length %lu err %d\n", (unsigned long)index, (unsigned long)offset,
            (unsigned long)length, err);
    }

    return err == 0;
}

uint64_t tr_pieceOffset(tr_torrent const* tor, tr_piece_index_t index, uint32_t offset, uint32_t length)
{
    TR_ASSERT(tr_isTorrent(tor));

    uint64_t ret;

    ret = tor->info.pieceSize;
    ret *= index;
    ret += offset;
    ret += length;
    return ret;
}

void tr_torGetFileBlockRange(tr_torrent const* tor, tr_file_index_t const file, tr_block_index_t* first, tr_block_index_t* last)
{
    tr_file const* f = &tor->info.files[file];
    uint64_t offset = f->offset;

    *first = offset / tor->blockSize;

    if (f->length == 0)
    {
        *last = *first;
    }
    else
    {
        offset += f->length - 1;
        *last = offset / tor->blockSize;
    }
}

void tr_torGetPieceBlockRange(tr_torrent const* tor, tr_piece_index_t const piece, tr_block_index_t* first,
    tr_block_index_t* last)
{
    uint64_t offset = tor->info.pieceSize;
    offset *= piece;
    *first = offset / tor->blockSize;
    offset += tr_torPieceCountBytes(tor, piece) - 1;
    *last = offset / tor->blockSize;
}

/***
****
***/

void tr_torrentSetPieceChecked(tr_torrent* tor, tr_piece_index_t pieceIndex)
{
    TR_ASSERT(tr_isTorrent(tor));
    TR_ASSERT(pieceIndex < tor->info.pieceCount);

    tor->info.pieces[pieceIndex].timeChecked = tr_time();
}

void tr_torrentSetChecked(tr_torrent* tor, time_t when)
{
    TR_ASSERT(tr_isTorrent(tor));

    for (tr_piece_index_t i = 0; i < tor->info.pieceCount; ++i)
    {
        tor->info.pieces[i].timeChecked = when;
    }
}

bool tr_torrentCheckPiece(tr_torrent* tor, tr_piece_index_t pieceIndex)
{
    bool const pass = tr_ioTestPiece(tor, pieceIndex);

    tr_deeplog_tor(tor, "[LAZY] tr_torrentCheckPiece tested piece %zu, pass==%d", (size_t)pieceIndex, (int)pass);
    tr_torrentSetHasPiece(tor, pieceIndex, pass);
    tr_torrentSetPieceChecked(tor, pieceIndex);
    tor->anyDate = tr_time();
    tr_torrentSetDirty(tor);

    return pass;
}

time_t tr_torrentGetFileMTime(tr_torrent const* tor, tr_file_index_t i)
{
    time_t mtime = 0;

    if (!tr_fdFileGetCachedMTime(tor->session, tor->uniqueId, i, &mtime))
    {
        tr_torrentFindFile2(tor, i, NULL, NULL, &mtime);
    }

    return mtime;
}

bool tr_torrentPieceNeedsCheck(tr_torrent const* tor, tr_piece_index_t p)
{
    uint64_t unused;
    tr_file_index_t f;
    tr_info const* inf = tr_torrentInfo(tor);

    /* if we've never checked this piece, then it needs to be checked */
    if (inf->pieces[p].timeChecked == 0)
    {
        return true;
    }

    /* If we think we've completed one of the files in this piece,
     * but it's been modified since we last checked it,
     * then it needs to be rechecked */
    tr_ioFindFileLocation(tor, p, 0, &f, &unused);

    for (tr_file_index_t i = f; i < inf->fileCount && pieceHasFile(p, &inf->files[i]); ++i)
    {
        if (tr_cpFileIsComplete(&tor->completion, i))
        {
            if (tr_torrentGetFileMTime(tor, i) > inf->pieces[p].timeChecked)
            {
                return true;
            }
        }
    }

    return false;
}

/***
****
***/

static int compareTrackerByTier(void const* va, void const* vb)
{
    tr_tracker_info const* a = va;
    tr_tracker_info const* b = vb;

    /* sort by tier */
    if (a->tier != b->tier)
    {
        return a->tier - b->tier;
    }

    /* get the effects of a stable sort by comparing the two elements' addresses */
    return a - b;
}

bool tr_torrentSetAnnounceList(tr_torrent* tor, tr_tracker_info const* trackers_in, int trackerCount)
{
    TR_ASSERT(tr_isTorrent(tor));

    tr_torrentLock(tor);

    tr_variant metainfo;
    bool ok = true;
    tr_tracker_info* trackers;

    /* ensure the trackers' tiers are in ascending order */
    trackers = tr_memdup(trackers_in, sizeof(tr_tracker_info) * trackerCount);
    qsort(trackers, trackerCount, sizeof(tr_tracker_info), compareTrackerByTier);

    /* look for bad URLs */
    for (int i = 0; ok && i < trackerCount; ++i)
    {
        if (!tr_urlIsValidTracker(trackers[i].announce))
        {
            ok = false;
        }
    }

    /* save to the .torrent file */
    if (ok && tr_variantFromFile(&metainfo, TR_VARIANT_FMT_BENC, tor->info.torrent, NULL))
    {
        bool hasInfo;
        tr_info tmpInfo;

        /* remove the old fields */
        tr_variantDictRemove(&metainfo, TR_KEY_announce);
        tr_variantDictRemove(&metainfo, TR_KEY_announce_list);

        /* add the new fields */
        if (trackerCount > 0)
        {
            tr_variantDictAddStr(&metainfo, TR_KEY_announce, trackers[0].announce);
        }

        if (trackerCount > 1)
        {
            int prevTier = -1;
            tr_variant* tier = NULL;
            tr_variant* announceList = tr_variantDictAddList(&metainfo, TR_KEY_announce_list, 0);

            for (int i = 0; i < trackerCount; ++i)
            {
                if (prevTier != trackers[i].tier)
                {
                    prevTier = trackers[i].tier;
                    tier = tr_variantListAddList(announceList, 0);
                }

                tr_variantListAddStr(tier, trackers[i].announce);
            }
        }

        /* try to parse it back again, to make sure it's good */
        memset(&tmpInfo, 0, sizeof(tr_info));

        if (tr_metainfoParse(tor->session, &metainfo, &tmpInfo, &hasInfo, &tor->infoDictLength))
        {
            /* it's good, so keep these new trackers and free the old ones */

            tr_info swap;
            swap.trackers = tor->info.trackers;
            swap.trackerCount = tor->info.trackerCount;
            tor->info.trackers = tmpInfo.trackers;
            tor->info.trackerCount = tmpInfo.trackerCount;
            tmpInfo.trackers = swap.trackers;
            tmpInfo.trackerCount = swap.trackerCount;
            tr_torrentMarkEdited(tor);

            tr_metainfoFree(&tmpInfo);
            tr_variantToFile(&metainfo, TR_VARIANT_FMT_BENC, tor->info.torrent);
        }

        /* cleanup */
        tr_variantFree(&metainfo);

        /* if we had a tracker-related error on this torrent,
         * and that tracker's been removed,
         * then clear the error */
        if (tor->error == TR_STAT_TRACKER_WARNING || tor->error == TR_STAT_TRACKER_ERROR)
        {
            bool clear = true;

            for (int i = 0; clear && i < trackerCount; ++i)
            {
                if (strcmp(trackers[i].announce, tor->errorTracker) == 0)
                {
                    clear = false;
                }
            }

            if (clear)
            {
                tr_torrentClearError(tor);
            }
        }

        /* tell the announcer to reload this torrent's tracker list */
        tr_announcerResetTorrent(tor->session->announcer, tor);
    }

    tr_torrentUnlock(tor);

    tr_free(trackers);
    return ok;
}

/**
***
**/

#define BACK_COMPAT_FUNC(oldname, newname) \
    void oldname(tr_torrent * tor, time_t t) { newname(tor, t); }
BACK_COMPAT_FUNC(tr_torrentSetAddedDate, tr_torrentSetDateAdded)
BACK_COMPAT_FUNC(tr_torrentSetActivityDate, tr_torrentSetDateActive)
BACK_COMPAT_FUNC(tr_torrentSetDoneDate, tr_torrentSetDateDone)
#undef BACK_COMPAT_FUNC

void tr_torrentSetDateAdded(tr_torrent* tor, time_t t)
{
    TR_ASSERT(tr_isTorrent(tor));

    tor->addedDate = t;
    tor->anyDate = MAX(tor->anyDate, tor->addedDate);
}

void tr_torrentSetDateActive(tr_torrent* tor, time_t t)
{
    TR_ASSERT(tr_isTorrent(tor));

    tor->activityDate = t;
    tor->anyDate = MAX(tor->anyDate, tor->activityDate);
}

void tr_torrentSetDateDone(tr_torrent* tor, time_t t)
{
    TR_ASSERT(tr_isTorrent(tor));

    tor->doneDate = t;
    tor->anyDate = MAX(tor->anyDate, tor->doneDate);
}

/**
***
**/

uint64_t tr_torrentGetBytesLeftToAllocate(tr_torrent const* tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    uint64_t bytesLeft = 0;

    for (tr_file_index_t i = 0; i < tor->info.fileCount; ++i)
    {
        if (!tor->info.files[i].dnd)
        {
            tr_sys_path_info info;
            uint64_t const length = tor->info.files[i].length;
            char* path = tr_torrentFindFile(tor, i);

            bytesLeft += length;

            if (path != NULL && tr_sys_path_get_info(path, 0, &info, NULL) && info.type == TR_SYS_PATH_IS_FILE &&
                info.size <= length)
            {
                bytesLeft -= info.size;
            }

            tr_free(path);
        }
    }

    return bytesLeft;
}

/****
*****  Removing the torrent's local data
****/

static bool isJunkFile(char const* base)
{
    static char const* files[] =
    {
        ".DS_Store",
        "desktop.ini",
        "Thumbs.db"
    };

    for (size_t i = 0; i < TR_N_ELEMENTS(files); ++i)
    {
        if (strcmp(base, files[i]) == 0)
        {
            return true;
        }
    }

#ifdef __APPLE__

    /* check for resource forks. <http://support.apple.com/kb/TA20578> */
    if (memcmp(base, "._", 2) == 0)
    {
        return true;
    }

#endif

    return false;
}

static void removeEmptyFoldersAndJunkFiles(char const* folder)
{
    tr_sys_dir_t odir;

    if ((odir = tr_sys_dir_open(folder, NULL)) != TR_BAD_SYS_DIR)
    {
        char const* name;

        while ((name = tr_sys_dir_read_name(odir, NULL)) != NULL)
        {
            if (strcmp(name, ".") != 0 && strcmp(name, "..") != 0)
            {
                tr_sys_path_info info;
                char* filename = tr_buildPath(folder, name, NULL);

                if (tr_sys_path_get_info(filename, 0, &info, NULL) && info.type == TR_SYS_PATH_IS_DIRECTORY)
                {
                    removeEmptyFoldersAndJunkFiles(filename);
                }
                else if (isJunkFile(name))
                {
                    tr_sys_path_remove(filename, NULL);
                }

                tr_free(filename);
            }
        }

        tr_sys_path_remove(folder, NULL);
        tr_sys_dir_close(odir, NULL);
    }
}

/**
 * This convoluted code does something (seemingly) simple:
 * remove the torrent's local files.
 *
 * Fun complications:
 * 1. Try to preserve the directory hierarchy in the recycle bin.
 * 2. If there are nontorrent files, don't delete them...
 * 3. ...unless the other files are "junk", such as .DS_Store
 */
static void deleteLocalData(tr_torrent* tor, tr_fileFunc func)
{
    char* base;
    tr_sys_dir_t odir;
    char* tmpdir = NULL;
    tr_ptrArray files = TR_PTR_ARRAY_INIT;
    tr_ptrArray folders = TR_PTR_ARRAY_INIT;
    PtrArrayCompareFunc vstrcmp = (PtrArrayCompareFunc)strcmp;
    char const* const top = tor->currentDir;

    /* don't try to delete local data if the directory's gone missing */
    if (!tr_sys_path_exists(top, NULL))
    {
        return;
    }

    /* if it's a magnet link, there's nothing to move... */
    if (!tr_torrentHasMetadata(tor))
    {
        return;
    }

    /***
    ****  Move the local data to a new tmpdir
    ***/

    base = tr_strdup_printf("%s__XXXXXX", tr_torrentName(tor));
    tmpdir = tr_buildPath(top, base, NULL);
    tr_sys_dir_create_temp(tmpdir, NULL);
    tr_free(base);

    for (tr_file_index_t f = 0; f < tor->info.fileCount; ++f)
    {
        char* filename;

        /* try to find the file, looking in the partial and download dirs */
        filename = tr_buildPath(top, tor->info.files[f].name, NULL);

        if (!tr_sys_path_exists(filename, NULL))
        {
            char* partial = tr_torrentBuildPartial(tor, f);
            tr_free(filename);
            filename = tr_buildPath(top, partial, NULL);
            tr_free(partial);

            if (!tr_sys_path_exists(filename, NULL))
            {
                tr_free(filename);
                filename = NULL;
            }
        }

        /* if we found the file, move it */
        if (filename != NULL)
        {
            char* target = tr_buildPath(tmpdir, tor->info.files[f].name, NULL);
            tr_moveFile(filename, target, NULL);
            tr_ptrArrayAppend(&files, target);
            tr_free(filename);
        }
    }

    /***
    ****  Remove tmpdir.
    ****
    ****  Try deleting the top-level files & folders to preserve
    ****  the directory hierarchy in the recycle bin.
    ****  If case that fails -- for example, rmdir () doesn't
    ****  delete nonempty folders -- go from the bottom up too.
    ***/

    /* try deleting the local data's top-level files & folders */
    if ((odir = tr_sys_dir_open(tmpdir, NULL)) != TR_BAD_SYS_DIR)
    {
        char const* name;

        while ((name = tr_sys_dir_read_name(odir, NULL)) != NULL)
        {
            if (strcmp(name, ".") != 0 && strcmp(name, "..") != 0)
            {
                char* file = tr_buildPath(tmpdir, name, NULL);
                (*func)(file, NULL);
                tr_free(file);
            }
        }

        tr_sys_dir_close(odir, NULL);
    }

    /* go from the bottom up */
    for (int i = 0, n = tr_ptrArraySize(&files); i < n; ++i)
    {
        char* walk = tr_strdup(tr_ptrArrayNth(&files, i));

        while (tr_sys_path_exists(walk, NULL) && !tr_sys_path_is_same(tmpdir, walk, NULL))
        {
            char* tmp = tr_sys_path_dirname(walk, NULL);
            (*func)(walk, NULL);
            tr_free(walk);
            walk = tmp;
        }

        tr_free(walk);
    }

    /***
    ****  The local data has been removed.
    ****  What's left in top are empty folders, junk, and user-generated files.
    ****  Remove the first two categories and leave the third.
    ***/

    /* build a list of 'top's child directories that belong to this torrent */
    for (tr_file_index_t f = 0; f < tor->info.fileCount; ++f)
    {
        char* dir;
        char* filename;

        /* get the directory that this file goes in... */
        filename = tr_buildPath(top, tor->info.files[f].name, NULL);
        dir = tr_sys_path_dirname(filename, NULL);
        tr_free(filename);

        if (dir == NULL)
        {
            continue;
        }

        /* walk up the directory tree until we reach 'top' */
        if (!tr_sys_path_is_same(top, dir, NULL) && strcmp(top, dir) != 0)
        {
            for (;;)
            {
                char* parent = tr_sys_path_dirname(dir, NULL);

                if (tr_sys_path_is_same(top, parent, NULL) || strcmp(top, parent) == 0)
                {
                    if (tr_ptrArrayFindSorted(&folders, dir, vstrcmp) == NULL)
                    {
                        tr_ptrArrayInsertSorted(&folders, tr_strdup(dir), vstrcmp);
                    }

                    tr_free(parent);
                    break;
                }

                /* walk upwards to parent */
                tr_free(dir);
                dir = parent;
            }
        }

        tr_free(dir);
    }

    for (int i = 0, n = tr_ptrArraySize(&folders); i < n; ++i)
    {
        removeEmptyFoldersAndJunkFiles(tr_ptrArrayNth(&folders, i));
    }

    /* cleanup */
    tr_sys_path_remove(tmpdir, NULL);
    tr_free(tmpdir);
    tr_ptrArrayDestruct(&folders, tr_free);
    tr_ptrArrayDestruct(&files, tr_free);
}

static void tr_torrentDeleteLocalData(tr_torrent* tor, tr_fileFunc func)
{
    TR_ASSERT(tr_isTorrent(tor));

    if (func == NULL)
    {
        func = tr_sys_path_remove;
    }

    /* close all the files because we're about to delete them */
    tr_cacheFlushTorrent(tor->session->cache, tor);
    tr_fdTorrentClose(tor->session, tor->uniqueId);

    deleteLocalData(tor, func);
}

/***
****
***/

struct LocationData
{
    bool move_from_old_location;
    int volatile* setme_state;
    double volatile* setme_progress;
    char* location;
    tr_torrent* tor;
};

static void setLocation(void* vdata)
{
    struct LocationData* data = vdata;
    tr_torrent* tor = data->tor;

    TR_ASSERT(tr_isTorrent(tor));

    tr_torrentLock(tor);

    bool err = false;
    bool const do_move = data->move_from_old_location;
    char const* location = data->location;
    double bytesHandled = 0;

    tr_logAddDebug("Moving \"%s\" location from currentDir \"%s\" to \"%s\"", tr_torrentName(tor), tor->currentDir, location);

    tr_sys_dir_create(location, TR_SYS_DIR_CREATE_PARENTS, 0777, NULL);

    if (!tr_sys_path_is_same(location, tor->currentDir, NULL))
    {
        /* bad idea to move files while they're being verified... */
        tr_verifyRemove(tor);

        /* try to move the files.
         * FIXME: there are still all kinds of nasty cases, like what
         * if the target directory runs out of space halfway through... */
        for (tr_file_index_t i = 0; !err && i < tor->info.fileCount; ++i)
        {
            char* sub;
            char const* oldbase;
            tr_file const* f = &tor->info.files[i];

            if (tr_torrentFindFile2(tor, i, &oldbase, &sub, NULL))
            {
                char* oldpath = tr_buildPath(oldbase, sub, NULL);
                char* newpath = tr_buildPath(location, sub, NULL);

                tr_logAddDebug("Found file #%d: %s", (int)i, oldpath);

                if (do_move && !tr_sys_path_is_same(oldpath, newpath, NULL))
                {
                    tr_error* error = NULL;

                    tr_logAddTorInfo(tor, "moving \"%s\" to \"%s\"", oldpath, newpath);

                    if (!tr_moveFile(oldpath, newpath, &error))
                    {
                        err = true;
                        tr_logAddTorErr(tor, "error moving \"%s\" to \"%s\": %s", oldpath, newpath, error->message);
                        tr_error_free(error);
                    }
                }

                tr_free(newpath);
                tr_free(oldpath);
                tr_free(sub);
            }

            if (data->setme_progress != NULL)
            {
                bytesHandled += f->length;
                *data->setme_progress = bytesHandled / tor->info.totalSize;
            }
        }

        if (!err && do_move)
        {
            /* blow away the leftover subdirectories in the old location */
            tr_torrentDeleteLocalData(tor, tr_sys_path_remove);
        }
    }

    if (!err)
    {
        /* set the new location and reverify */
        tr_torrentSetDownloadDir(tor, location);

        if (do_move)
        {
            tr_free(tor->incompleteDir);
            tor->incompleteDir = NULL;
            tor->currentDir = tor->downloadDir;
        }
    }

    if (data->setme_state != NULL)
    {
        *data->setme_state = err ? TR_LOC_ERROR : TR_LOC_DONE;
    }

    /* cleanup */
    tr_torrentUnlock(tor);
    tr_free(data->location);
    tr_free(data);
}

void tr_torrentSetLocation(tr_torrent* tor, char const* location, bool move_from_old_location, double volatile* setme_progress,
    int volatile* setme_state)
{
    TR_ASSERT(tr_isTorrent(tor));

    if (setme_state != NULL)
    {
        *setme_state = TR_LOC_MOVING;
    }

    if (setme_progress != NULL)
    {
        *setme_progress = 0;
    }

    /* run this in the libtransmission thread */
    struct LocationData* data = tr_new(struct LocationData, 1);
    data->tor = tor;
    data->location = tr_strdup(location);
    data->move_from_old_location = move_from_old_location;
    data->setme_state = setme_state;
    data->setme_progress = setme_progress;
    tr_runInEventThread(tor->session, setLocation, data);
}

/***
****
***/

static void tr_torrentFileCompleted(tr_torrent* tor, tr_file_index_t fileIndex)
{
    char* sub;
    char const* base;
    tr_info const* inf = &tor->info;
    tr_file const* f = &inf->files[fileIndex];
    time_t const now = tr_time();

    /* close the file so that we can reopen in read-only mode as needed */
    tr_cacheFlushFile(tor->session->cache, tor, fileIndex);
    tr_fdFileClose(tor->session, tor, fileIndex);

    /* now that the file is complete and closed, we can start watching its
     * mtime timestamp for changes to know if we need to reverify pieces */
    for (tr_piece_index_t i = f->firstPiece; i <= f->lastPiece; ++i)
    {
        inf->pieces[i].timeChecked = now;
    }

    /* if the torrent's current filename isn't the same as the one in the
     * metadata -- for example, if it had the ".part" suffix appended to
     * it until now -- then rename it to match the one in the metadata */
    if (tr_torrentFindFile2(tor, fileIndex, &base, &sub, NULL))
    {
        if (strcmp(sub, f->name) != 0)
        {
            char* oldpath = tr_buildPath(base, sub, NULL);
            char* newpath = tr_buildPath(base, f->name, NULL);
            tr_error* error = NULL;

            if (!tr_sys_path_rename(oldpath, newpath, &error))
            {
                tr_logAddTorErr(tor, "Error moving \"%s\" to \"%s\": %s", oldpath, newpath, error->message);
                tr_error_free(error);
            }

            tr_free(newpath);
            tr_free(oldpath);
        }

        tr_free(sub);
    }
}

static void tr_torrentPieceCompleted(tr_torrent* tor, tr_piece_index_t pieceIndex)
{
    tr_peerMgrPieceCompleted(tor, pieceIndex);

    /* if this piece completes any file, invoke the fileCompleted func for it */
    for (tr_file_index_t i = 0; i < tor->info.fileCount; ++i)
    {
        tr_file const* file = &tor->info.files[i];

        if (file->firstPiece <= pieceIndex && pieceIndex <= file->lastPiece)
        {
            if (tr_cpFileIsComplete(&tor->completion, i))
            {
                tr_torrentFileCompleted(tor, i);
            }
        }
    }
}

void tr_torrentGotBlock(tr_torrent* tor, tr_block_index_t block)
{
    TR_ASSERT(tr_isTorrent(tor));
    TR_ASSERT(tr_amInEventThread(tor->session));

    bool const block_is_new = !tr_torrentBlockIsComplete(tor, block);

    if (block_is_new)
    {
        tr_piece_index_t p;

        tr_cpBlockAdd(&tor->completion, block);
        tr_torrentSetDirty(tor);

        p = tr_torBlockPiece(tor, block);

        if (tr_torrentPieceIsComplete(tor, p))
        {
            tr_logAddTorDbg(tor, "[LAZY] checking just-completed piece %zu", (size_t)p);

            if (tr_torrentCheckPiece(tor, p))
            {
                tr_torrentPieceCompleted(tor, p);
            }
            else
            {
                uint32_t const n = tr_torPieceCountBytes(tor, p);
                tr_logAddTorErr(tor, _("Piece %" PRIu32 ", which was just downloaded, failed its checksum test"), p);
                tor->corruptCur += n;
                tor->downloadedCur -= MIN(tor->downloadedCur, n);
                tr_peerMgrGotBadPiece(tor, p);
            }
        }
    }
    else
    {
        uint32_t const n = tr_torBlockCountBytes(tor, block);
        tor->downloadedCur -= MIN(tor->downloadedCur, n);
        tr_logAddTorDbg(tor, "we have this block already...");
    }
}

/***
****
***/

static void find_file_in_dir(char const* name, char const* search_dir, char const** base, char const** subpath,
    tr_sys_path_info* file_info)
{
    char* filename = tr_buildPath(search_dir, name, NULL);

    if (tr_sys_path_get_info(filename, 0, file_info, NULL))
    {
        *base = search_dir;
        *subpath = name;
    }

    tr_free(filename);
}

bool tr_torrentFindFile2(tr_torrent const* tor, tr_file_index_t fileNum, char const** base, char** subpath, time_t* mtime)
{
    TR_ASSERT(tr_isTorrent(tor));
    TR_ASSERT(fileNum < tor->info.fileCount);

    char* part = NULL;
    tr_file const* file;
    char const* b = NULL;
    char const* s = NULL;
    tr_sys_path_info file_info;

    file = &tor->info.files[fileNum];

    /* look in the download dir... */
    if (b == NULL)
    {
        find_file_in_dir(file->name, tor->downloadDir, &b, &s, &file_info);
    }

    /* look in the incomplete dir... */
    if (b == NULL && tor->incompleteDir != NULL)
    {
        find_file_in_dir(file->name, tor->incompleteDir, &b, &s, &file_info);
    }

    if (b == NULL)
    {
        part = tr_torrentBuildPartial(tor, fileNum);
    }

    /* look for a .part file in the incomplete dir... */
    if (b == NULL && tor->incompleteDir != NULL)
    {
        find_file_in_dir(part, tor->incompleteDir, &b, &s, &file_info);
    }

    /* look for a .part file in the download dir... */
    if (b == NULL)
    {
        find_file_in_dir(part, tor->downloadDir, &b, &s, &file_info);
    }

    /* return the results */
    if (base != NULL)
    {
        *base = b;
    }

    if (subpath != NULL)
    {
        *subpath = tr_strdup(s);
    }

    if (mtime != NULL)
    {
        *mtime = file_info.last_modified_at;
    }

    /* cleanup */
    tr_free(part);
    return b != NULL;
}

char* tr_torrentFindFile(tr_torrent const* tor, tr_file_index_t fileNum)
{
    char* subpath;
    char* ret = NULL;
    char const* base;

    if (tr_torrentFindFile2(tor, fileNum, &base, &subpath, NULL))
    {
        ret = tr_buildPath(base, subpath, NULL);
        tr_free(subpath);
    }

    return ret;
}

/* Decide whether we should be looking for files in downloadDir or incompleteDir. */
static void refreshCurrentDir(tr_torrent* tor)
{
    char const* dir = NULL;

    if (tor->incompleteDir == NULL)
    {
        dir = tor->downloadDir;
    }
    else if (!tr_torrentHasMetadata(tor)) /* no files to find */
    {
        dir = tor->incompleteDir;
    }
    else if (!tr_torrentFindFile2(tor, 0, &dir, NULL, NULL))
    {
        dir = tor->incompleteDir;
    }

    TR_ASSERT(dir != NULL);
    TR_ASSERT(dir == tor->downloadDir || dir == tor->incompleteDir);

    tor->currentDir = dir;
}

char* tr_torrentBuildPartial(tr_torrent const* tor, tr_file_index_t fileNum)
{
    return tr_strdup_printf("%s.part", tor->info.files[fileNum].name);
}

/***
****
***/

static int compareTorrentByQueuePosition(void const* va, void const* vb)
{
    tr_torrent const* a = *(tr_torrent const* const*)va;
    tr_torrent const* b = *(tr_torrent const* const*)vb;

    return a->queuePosition - b->queuePosition;
}

#ifdef TR_ENABLE_ASSERTS

static bool queueIsSequenced(tr_session* session)
{
    int n;
    bool is_sequenced;
    tr_torrent** torrents;

    n = 0;
    torrents = tr_sessionGetTorrents(session, &n);
    qsort(torrents, n, sizeof(tr_torrent*), compareTorrentByQueuePosition);

#if 0

    fprintf(stderr, "%s", "queue: ");

    for (int i = 0; i < n; ++i)
    {
        fprintf(stderr, "%d ", tmp[i]->queuePosition);
    }

    fputc('\n', stderr);

#endif

    /* test them */
    is_sequenced = true;

    for (int i = 0; is_sequenced && i < n; ++i)
    {
        is_sequenced = torrents[i]->queuePosition == i;
    }

    tr_free(torrents);
    return is_sequenced;
}

#endif

int tr_torrentGetQueuePosition(tr_torrent const* tor)
{
    return tor->queuePosition;
}

void tr_torrentSetQueuePosition(tr_torrent* tor, int pos)
{
    int back = -1;
    tr_torrent* walk;
    int const old_pos = tor->queuePosition;
    time_t const now = tr_time();

    if (pos < 0)
    {
        pos = 0;
    }

    tor->queuePosition = -1;

    walk = NULL;

    while ((walk = tr_torrentNext(tor->session, walk)) != NULL)
    {
        if (old_pos < pos)
        {
            if (old_pos <= walk->queuePosition && walk->queuePosition <= pos)
            {
                walk->queuePosition--;
                walk->anyDate = now;
            }
        }

        if (old_pos > pos)
        {
            if (pos <= walk->queuePosition && walk->queuePosition < old_pos)
            {
                walk->queuePosition++;
                walk->anyDate = now;
            }
        }

        if (back < walk->queuePosition)
        {
            back = walk->queuePosition;
        }
    }

    tor->queuePosition = MIN(pos, back + 1);
    tor->anyDate = now;

    TR_ASSERT(queueIsSequenced(tor->session));
}

void tr_torrentsQueueMoveTop(tr_torrent** torrents_in, int n)
{
    tr_torrent** torrents = tr_memdup(torrents_in, sizeof(tr_torrent*) * n);
    qsort(torrents, n, sizeof(tr_torrent*), compareTorrentByQueuePosition);

    for (int i = n - 1; i >= 0; --i)
    {
        tr_torrentSetQueuePosition(torrents[i], 0);
    }

    tr_free(torrents);
}

void tr_torrentsQueueMoveUp(tr_torrent** torrents_in, int n)
{
    tr_torrent** torrents;

    torrents = tr_memdup(torrents_in, sizeof(tr_torrent*) * n);
    qsort(torrents, n, sizeof(tr_torrent*), compareTorrentByQueuePosition);

    for (int i = 0; i < n; ++i)
    {
        tr_torrentSetQueuePosition(torrents[i], torrents[i]->queuePosition - 1);
    }

    tr_free(torrents);
}

void tr_torrentsQueueMoveDown(tr_torrent** torrents_in, int n)
{
    tr_torrent** torrents;

    torrents = tr_memdup(torrents_in, sizeof(tr_torrent*) * n);
    qsort(torrents, n, sizeof(tr_torrent*), compareTorrentByQueuePosition);

    for (int i = n - 1; i >= 0; --i)
    {
        tr_torrentSetQueuePosition(torrents[i], torrents[i]->queuePosition + 1);
    }

    tr_free(torrents);
}

void tr_torrentsQueueMoveBottom(tr_torrent** torrents_in, int n)
{
    tr_torrent** torrents;

    torrents = tr_memdup(torrents_in, sizeof(tr_torrent*) * n);
    qsort(torrents, n, sizeof(tr_torrent*), compareTorrentByQueuePosition);

    for (int i = 0; i < n; ++i)
    {
        tr_torrentSetQueuePosition(torrents[i], INT_MAX);
    }

    tr_free(torrents);
}

static void torrentSetQueued(tr_torrent* tor, bool queued)
{
    TR_ASSERT(tr_isTorrent(tor));

    if (tr_torrentIsQueued(tor) != queued)
    {
        tor->isQueued = queued;
        tor->anyDate = tr_time();
        tr_torrentSetDirty(tor);
    }
}

void tr_torrentSetQueueStartCallback(tr_torrent* torrent, void (* callback)(tr_torrent*, void*), void* user_data)
{
    torrent->queue_started_callback = callback;
    torrent->queue_started_user_data = user_data;
}

/***
****
****  RENAME
****
***/

static bool renameArgsAreValid(char const* oldpath, char const* newname)
{
    return !tr_str_is_empty(oldpath) && !tr_str_is_empty(newname) && strcmp(newname, ".") != 0 && strcmp(newname, "..") != 0 &&
        strchr(newname, TR_PATH_DELIMITER) == NULL;
}

static tr_file_index_t* renameFindAffectedFiles(tr_torrent* tor, char const* oldpath, size_t* setme_n)
{
    size_t n;
    size_t oldpath_len;
    tr_file_index_t* indices = tr_new0(tr_file_index_t, tor->info.fileCount);

    n = 0;
    oldpath_len = strlen(oldpath);

    for (tr_file_index_t i = 0; i < tor->info.fileCount; ++i)
    {
        char const* name = tor->info.files[i].name;
        size_t const len = strlen(name);

        if ((len == oldpath_len || (len > oldpath_len && name[oldpath_len] == '/')) && memcmp(oldpath, name, oldpath_len) == 0)
        {
            indices[n++] = i;
        }
    }

    *setme_n = n;
    return indices;
}

static int renamePath(tr_torrent* tor, char const* oldpath, char const* newname)
{
    char* src;
    char const* base;
    int err = 0;

    if (!tr_torrentIsSeed(tor) && tor->incompleteDir != NULL)
    {
        base = tor->incompleteDir;
    }
    else
    {
        base = tor->downloadDir;
    }

    src = tr_buildPath(base, oldpath, NULL);

    if (!tr_sys_path_exists(src, NULL)) /* check for it as a partial */
    {
        char* tmp = tr_strdup_printf("%s.part", src);
        tr_free(src);
        src = tmp;
    }

    if (tr_sys_path_exists(src, NULL))
    {
        int tmp;
        bool tgt_exists;
        char* parent = tr_sys_path_dirname(src, NULL);
        char* tgt;

        if (tr_str_has_suffix(src, ".part"))
        {
            tgt = tr_strdup_printf("%s" TR_PATH_DELIMITER_STR "%s.part", parent, newname);
        }
        else
        {
            tgt = tr_buildPath(parent, newname, NULL);
        }

        tmp = errno;
        tgt_exists = tr_sys_path_exists(tgt, NULL);
        errno = tmp;

        if (!tgt_exists)
        {
            tr_error* error = NULL;

            tmp = errno;

            if (!tr_sys_path_rename(src, tgt, &error))
            {
                err = error->code;
                tr_error_free(error);
            }

            errno = tmp;
        }

        tr_free(tgt);
        tr_free(parent);
    }

    tr_free(src);

    return err;
}

static void renameTorrentFileString(tr_torrent* tor, char const* oldpath, char const* newname, tr_file_index_t fileIndex)
{
    char* name;
    tr_file* file = &tor->info.files[fileIndex];
    size_t const oldpath_len = strlen(oldpath);

    if (strchr(oldpath, TR_PATH_DELIMITER) == NULL)
    {
        if (oldpath_len >= strlen(file->name))
        {
            name = tr_buildPath(newname, NULL);
        }
        else
        {
            name = tr_buildPath(newname, file->name + oldpath_len + 1, NULL);
        }
    }
    else
    {
        char* tmp = tr_sys_path_dirname(oldpath, NULL);

        if (tmp == NULL)
        {
            return;
        }

        if (oldpath_len >= strlen(file->name))
        {
            name = tr_buildPath(tmp, newname, NULL);
        }
        else
        {
            name = tr_buildPath(tmp, newname, file->name + oldpath_len + 1, NULL);
        }

        tr_free(tmp);
    }

    if (strcmp(file->name, name) == 0)
    {
        tr_free(name);
    }
    else
    {
        tr_free(file->name);
        file->name = name;
        file->is_renamed = true;
    }
}

struct rename_data
{
    tr_torrent* tor;
    char* oldpath;
    char* newname;
    tr_torrent_rename_done_func callback;
    void* callback_user_data;
};

static void torrentRenamePath(void* vdata)
{
    struct rename_data* data = vdata;
    tr_torrent* const tor = data->tor;

    TR_ASSERT(tr_isTorrent(tor));

    /***
    ****
    ***/

    int error = 0;
    char const* const oldpath = data->oldpath;
    char const* const newname = data->newname;

    if (!renameArgsAreValid(oldpath, newname))
    {
        error = EINVAL;
    }
    else
    {
        size_t n;
        tr_file_index_t* file_indices;

        file_indices = renameFindAffectedFiles(tor, oldpath, &n);

        if (n == 0)
        {
            error = EINVAL;
        }
        else
        {
            error = renamePath(tor, oldpath, newname);

            if (error == 0)
            {
                /* update tr_info.files */
                for (size_t i = 0; i < n; ++i)
                {
                    renameTorrentFileString(tor, oldpath, newname, file_indices[i]);
                }

                /* update tr_info.name if user changed the toplevel */
                if (n == tor->info.fileCount && strchr(oldpath, '/') == NULL)
                {
                    tr_free(tor->info.name);
                    tor->info.name = tr_strdup(newname);
                }

                tr_torrentMarkEdited(tor);
                tr_torrentSetDirty(tor);
            }
        }

        tr_free(file_indices);
    }

    /***
    ****
    ***/

    tor->anyDate = tr_time();

    /* callback */
    if (data->callback != NULL)
    {
        (*data->callback)(tor, data->oldpath, data->newname, error, data->callback_user_data);
    }

    /* cleanup */
    tr_free(data->oldpath);
    tr_free(data->newname);
    tr_free(data);
}

void tr_torrentRenamePath(tr_torrent* tor, char const* oldpath, char const* newname, tr_torrent_rename_done_func callback,
    void* callback_user_data)
{
    struct rename_data* data;

    data = tr_new0(struct rename_data, 1);
    data->tor = tor;
    data->oldpath = tr_strdup(oldpath);
    data->newname = tr_strdup(newname);
    data->callback = callback;
    data->callback_user_data = callback_user_data;

    tr_runInEventThread(tor->session, torrentRenamePath, data);
}
