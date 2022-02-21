// This file Copyright © 2009-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm> /* EINVAL */
#include <array>
#include <cerrno> /* EINVAL */
#include <climits> /* INT_MAX */
#include <cmath>
#include <csignal> /* signal() */
#include <cstring> /* memcmp */
#include <ctime>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#ifndef _WIN32
#include <sys/wait.h> /* wait() */
#include <unistd.h> /* fork(), execvp(), _exit() */
#else
#include <windows.h> /* CreateProcess(), GetLastError() */
#endif

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
#include "magnet-metainfo.h"
#include "peer-common.h" /* MAX_BLOCK_SIZE */
#include "peer-mgr.h"
#include "resume.h"
#include "session.h"
#include "subprocess.h"
#include "torrent-magnet.h"
#include "torrent-metainfo.h"
#include "torrent.h"
#include "tr-assert.h"
#include "trevent.h" /* tr_runInEventThread() */
#include "utils.h"
#include "variant.h"
#include "verify.h"
#include "version.h"
#include "web-utils.h"

/***
****
***/

#define tr_deeplog_tor(tor, ...) tr_logAddDeepNamed(tr_torrentName(tor), __VA_ARGS__)

using namespace std::literals;

/***
****
***/

char const* tr_torrentName(tr_torrent const* tor)
{
    return tor != nullptr ? tor->name().c_str() : "";
}

uint64_t tr_torrentTotalSize(tr_torrent const* tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    return tor->totalSize();
}

int tr_torrentId(tr_torrent const* tor)
{
    return tor != nullptr ? tor->uniqueId : -1;
}

tr_torrent* tr_torrentFindFromId(tr_session* session, int id)
{
    auto& src = session->torrentsById;
    auto it = src.find(id);
    return it == std::end(src) ? nullptr : it->second;
}

tr_torrent* tr_torrentFindFromHash(tr_session* session, tr_sha1_digest_t const* hash)
{
    return hash == nullptr ? nullptr : session->getTorrent(*hash);
}

tr_torrent* tr_torrentFindFromMetainfo(tr_session* session, tr_torrent_metainfo const* metainfo)
{
    if (session == nullptr || metainfo == nullptr)
    {
        return nullptr;
    }

    return tr_torrentFindFromHash(session, &metainfo->infoHash());
}

tr_torrent* tr_torrentFindFromMagnetLink(tr_session* session, char const* magnet_link)
{
    auto mm = tr_magnet_metainfo{};
    return mm.parseMagnet(magnet_link != nullptr ? magnet_link : "") ? session->getTorrent(mm.infoHash()) : nullptr;
}

tr_torrent* tr_torrentFindFromObfuscatedHash(tr_session* session, tr_sha1_digest_t const& obfuscated_hash)
{
    for (auto* tor : session->torrents)
    {
        if (tor->obfuscated_hash == obfuscated_hash)
        {
            return tor;
        }
    }

    return nullptr;
}

bool tr_torrent::isPieceTransferAllowed(tr_direction direction) const
{
    TR_ASSERT(tr_isDirection(direction));

    bool allowed = true;

    if (tr_torrentUsesSpeedLimit(this, direction) && this->speedLimitBps(direction) <= 0)
    {
        allowed = false;
    }

    if (tr_torrentUsesSessionLimits(this))
    {
        unsigned int limit = 0;
        if (tr_sessionGetActiveSpeedLimit_Bps(this->session, direction, &limit) && (limit <= 0))
        {
            allowed = false;
        }
    }

    return allowed;
}

/***
****
***/

static void tr_torrentUnsetPeerId(tr_torrent* tor)
{
    // triggers a rebuild next time tr_torrentGetPeerId() is called
    tor->peer_id.reset();
}

static int peerIdTTL(tr_torrent const* tor)
{
    auto const ctime = tor->peer_id_creation_time;
    return ctime == 0 ? 0 : (int)difftime(ctime + tor->session->peer_id_ttl_hours * 3600, tr_time());
}

tr_peer_id_t const& tr_torrentGetPeerId(tr_torrent* tor)
{
    bool const needs_new_peer_id = !tor->peer_id || // doesn't have one
        (tor->isPublic() && (peerIdTTL(tor) <= 0)); // has one but it's expired

    if (needs_new_peer_id)
    {
        tor->peer_id = tr_peerIdInit();
        tor->peer_id_creation_time = tr_time();
    }

    return *tor->peer_id;
}

/***
****  PER-TORRENT UL / DL SPEEDS
***/

void tr_torrent::setSpeedLimitBps(tr_direction dir, unsigned int Bps)
{
    TR_ASSERT(tr_isDirection(dir));

    if (this->bandwidth->setDesiredSpeedBytesPerSecond(dir, Bps))
    {
        this->setDirty();
    }
}

void tr_torrentSetSpeedLimit_KBps(tr_torrent* tor, tr_direction dir, unsigned int KBps)
{
    tor->setSpeedLimitBps(dir, tr_toSpeedBytes(KBps));
}

unsigned int tr_torrent::speedLimitBps(tr_direction dir) const
{
    TR_ASSERT(tr_isDirection(dir));

    return this->bandwidth->getDesiredSpeedBytesPerSecond(dir);
}

unsigned int tr_torrentGetSpeedLimit_KBps(tr_torrent const* tor, tr_direction dir)
{
    TR_ASSERT(tr_isTorrent(tor));
    TR_ASSERT(tr_isDirection(dir));

    return tr_toSpeedKBps(tor->speedLimitBps(dir));
}

void tr_torrentUseSpeedLimit(tr_torrent* tor, tr_direction dir, bool do_use)
{
    TR_ASSERT(tr_isTorrent(tor));
    TR_ASSERT(tr_isDirection(dir));

    if (tor->bandwidth->setLimited(dir, do_use))
    {
        tor->setDirty();
    }
}

bool tr_torrentUsesSpeedLimit(tr_torrent const* tor, tr_direction dir)
{
    TR_ASSERT(tr_isTorrent(tor));

    return tor->bandwidth->isLimited(dir);
}

void tr_torrentUseSessionLimits(tr_torrent* tor, bool doUse)
{
    TR_ASSERT(tr_isTorrent(tor));

    if (tor->bandwidth->honorParentLimits(TR_UP, doUse) || tor->bandwidth->honorParentLimits(TR_DOWN, doUse))
    {
        tor->setDirty();
    }
}

bool tr_torrentUsesSessionLimits(tr_torrent const* tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    return tor->bandwidth->areParentLimitsHonored(TR_UP);
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

        tor->setDirty();
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

        tor->setDirty();
    }
}

double tr_torrentGetRatioLimit(tr_torrent const* tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    return tor->desiredRatio;
}

bool tr_torrentGetSeedRatio(tr_torrent const* tor, double* ratio)
{
    auto isLimited = bool{};

    TR_ASSERT(tr_isTorrent(tor));

    switch (tr_torrentGetRatioMode(tor))
    {
    case TR_RATIOLIMIT_SINGLE:
        isLimited = true;

        if (ratio != nullptr)
        {
            *ratio = tr_torrentGetRatioLimit(tor);
        }

        break;

    case TR_RATIOLIMIT_GLOBAL:
        isLimited = tr_sessionIsRatioLimited(tor->session);

        if (isLimited && ratio != nullptr)
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
static bool tr_torrentGetSeedRatioBytes(tr_torrent const* tor, uint64_t* setme_left, uint64_t* setme_goal)
{
    bool seed_ratio_applies = false;

    TR_ASSERT(tr_isTorrent(tor));

    if (auto seed_ratio = double{}; tr_torrentGetSeedRatio(tor, &seed_ratio))
    {
        auto const uploaded = tor->uploadedCur + tor->uploadedPrev;
        auto const baseline = tor->totalSize();
        auto const goal = baseline * seed_ratio;

        if (setme_left != nullptr)
        {
            *setme_left = goal > uploaded ? goal - uploaded : 0;
        }

        if (setme_goal != nullptr)
        {
            *setme_goal = goal;
        }

        seed_ratio_applies = tor->isDone();
    }

    return seed_ratio_applies;
}

static bool tr_torrentIsSeedRatioDone(tr_torrent const* tor)
{
    auto bytesLeft = uint64_t{};
    return tr_torrentGetSeedRatioBytes(tor, &bytesLeft, nullptr) && bytesLeft == 0;
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

        tor->setDirty();
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

        tor->setDirty();
    }
}

uint16_t tr_torrentGetIdleLimit(tr_torrent const* tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    return tor->idleLimitMinutes;
}

bool tr_torrentGetSeedIdle(tr_torrent const* tor, uint16_t* idleMinutes)
{
    auto isLimited = bool{};

    switch (tr_torrentGetIdleMode(tor))
    {
    case TR_IDLELIMIT_SINGLE:
        isLimited = true;

        if (idleMinutes != nullptr)
        {
            *idleMinutes = tr_torrentGetIdleLimit(tor);
        }

        break;

    case TR_IDLELIMIT_GLOBAL:
        isLimited = tr_sessionIsIdleLimited(tor->session);

        if (isLimited && idleMinutes != nullptr)
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

static bool tr_torrentIsSeedIdleLimitDone(tr_torrent const* tor)
{
    auto idleMinutes = uint16_t{};
    return tr_torrentGetSeedIdle(tor, &idleMinutes) &&
        difftime(tr_time(), std::max(tor->startDate, tor->activityDate)) >= idleMinutes * 60U;
}

static void torrentCallScript(tr_torrent const* tor, char const* script);

static void callScriptIfEnabled(tr_torrent const* tor, TrScript type)
{
    auto const* session = tor->session;

    if (tr_sessionIsScriptEnabled(session, type))
    {
        torrentCallScript(tor, tr_sessionGetScript(session, type));
    }
}

/***
****
***/

void tr_torrentCheckSeedLimit(tr_torrent* tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    if (!tor->isRunning || tor->isStopping || !tor->isDone())
    {
        return;
    }

    /* if we're seeding and reach our seed ratio limit, stop the torrent */
    if (tr_torrentIsSeedRatioDone(tor))
    {
        tr_logAddTorInfo(tor, "%s", "Seed ratio reached; pausing torrent");

        tor->isStopping = true;

        /* maybe notify the client */
        if (tor->ratio_limit_hit_func != nullptr)
        {
            (*tor->ratio_limit_hit_func)(tor, tor->ratio_limit_hit_func_user_data);
        }
    }
    /* if we're seeding and reach our inactivity limit, stop the torrent */
    else if (tr_torrentIsSeedIdleLimitDone(tor))
    {
        tr_logAddTorInfo(tor, "%s", "Seeding idle limit reached; pausing torrent");

        tor->isStopping = true;
        tor->finishedSeedingByIdle = true;

        /* maybe notify the client */
        if (tor->idle_limit_hit_func != nullptr)
        {
            (*tor->idle_limit_hit_func)(tor, tor->idle_limit_hit_func_user_data);
        }
    }

    if (tor->isStopping)
    {
        callScriptIfEnabled(tor, TR_SCRIPT_ON_TORRENT_DONE_SEEDING);
    }
}

/***
****
***/

static void tr_torrentClearError(tr_torrent* tor)
{
    tor->error = TR_STAT_OK;
    tor->error_announce_url.clear();
    tor->error_string.clear();
}

static void onTrackerResponse(tr_torrent* tor, tr_tracker_event const* event, void* /*user_data*/)
{
    switch (event->messageType)
    {
    case TR_TRACKER_PEERS:
        tr_logAddTorDbg(tor, "Got %zu peers from tracker", size_t(std::size(event->pex)));
        tr_peerMgrAddPex(tor, TR_PEER_FROM_TRACKER, std::data(event->pex), std::size(event->pex));
        break;

    case TR_TRACKER_COUNTS:
        if (tor->isPrivate() && (event->leechers == 0))
        {
            tr_peerMgrSetSwarmIsAllSeeds(tor);
        }

        break;

    case TR_TRACKER_WARNING:
        tr_logAddTorErr(tor, _("Tracker warning: \"%" TR_PRIsv "\""), TR_PRIsv_ARG(event->text));
        tor->error = TR_STAT_TRACKER_WARNING;
        tor->error_announce_url = event->announce_url;
        tor->error_string = event->text;
        break;

    case TR_TRACKER_ERROR:
        tor->error = TR_STAT_TRACKER_ERROR;
        tor->error_announce_url = event->announce_url;
        tor->error_string = event->text;
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

static void torrentStart(tr_torrent* tor, bool bypass_queue);

static void tr_torrentFireMetadataCompleted(tr_torrent* tor);

static void torrentInitFromInfoDict(tr_torrent* tor)
{
    tor->completion = tr_completion{ tor, &tor->blockInfo() };
    if (auto const obfuscated = tr_sha1("req2"sv, tor->infoHash()); obfuscated)
    {
        tor->obfuscated_hash = *obfuscated;
    }
    else
    {
        // lookups by obfuscated hash will fail for this torrent
        tr_logAddTorErr(tor, "error computing obfuscated info hash");
        tor->obfuscated_hash = tr_sha1_digest_t{};
    }

    tor->fpm_.reset(tor->metainfo_);
    tor->file_mtimes_.resize(tor->fileCount());
    tor->file_priorities_.reset(&tor->fpm_);
    tor->files_wanted_.reset(&tor->fpm_);
    tor->checked_pieces_ = tr_bitfield{ size_t(tor->pieceCount()) };
}

void tr_torrent::setMetainfo(tr_torrent_metainfo const& tm)
{
    metainfo_ = tm;

    torrentInitFromInfoDict(this);
    tr_peerMgrOnTorrentGotMetainfo(this);
    tr_torrentFireMetadataCompleted(this);
    this->setDirty();
}

static bool hasAnyLocalData(tr_torrent const* tor)
{
    auto filename = std::string{};

    for (tr_file_index_t i = 0, n = tor->fileCount(); i < n; ++i)
    {
        if (tor->findFile(filename, i))
        {
            return true;
        }
    }

    return false;
}

static bool setLocalErrorIfFilesDisappeared(tr_torrent* tor)
{
    bool const disappeared = tor->hasTotal() > 0 && !hasAnyLocalData(tor);

    if (disappeared)
    {
        tr_deeplog_tor(tor, "%s", "[LAZY] uh oh, the files disappeared");
        tor->setLocalError(_(
            "No data found! Ensure your drives are connected or use \"Set Location\". To re-download, remove the torrent and re-add it."));
    }

    return disappeared;
}

/**
 * Sniff out newly-added seeds so that they can skip the verify step
 */
static bool isNewTorrentASeed(tr_torrent* tor)
{
    if (!tor->hasMetadata())
    {
        return false;
    }

    auto filename_buf = std::string{};
    for (tr_file_index_t i = 0, n = tor->fileCount(); i < n; ++i)
    {
        // it's not a new seed if a file is missing
        auto const found = tor->findFile(filename_buf, i);
        if (!found)
        {
            return false;
        }

        // it's not a new seed if a file is partial
        if (tr_strvEndsWith(found->filename, ".part"sv))
        {
            return false;
        }

        // it's not a new seed if a file size is wrong
        if (found->size != tor->fileSize(i))
        {
            return false;
        }

        // it's not a new seed if it was modified after it was added
        if (found->last_modified_at >= tor->addedDate)
        {
            return false;
        }
    }

    // check the first piece
    return tor->ensurePieceIsChecked(0);
}

static void refreshCurrentDir(tr_torrent* tor);

static void torrentInit(tr_torrent* tor, tr_ctor const* ctor)
{
    static auto next_unique_id = int{ 1 };
    auto const lock = tor->unique_lock();

    tr_session* session = tr_ctorGetSession(ctor);
    TR_ASSERT(session != nullptr);

    tor->session = session;
    tor->uniqueId = next_unique_id++;
    tor->queuePosition = tr_sessionCountTorrents(session);

    torrentInitFromInfoDict(tor);

    char const* dir = nullptr;
    if (tr_ctorGetDownloadDir(ctor, TR_FORCE, &dir) || tr_ctorGetDownloadDir(ctor, TR_FALLBACK, &dir))
    {
        tor->download_dir = dir;
    }

    if (!tr_ctorGetIncompleteDir(ctor, &dir))
    {
        dir = tr_sessionGetIncompleteDir(session);
    }

    if (tr_sessionIsIncompleteDirEnabled(session))
    {
        tor->incomplete_dir = dir;
    }

    tor->bandwidth = new Bandwidth(session->bandwidth);

    tor->bandwidth->setPriority(tr_ctorGetBandwidthPriority(ctor));
    tor->error = TR_STAT_OK;
    tor->finishedSeedingByIdle = false;

    tor->labels = tr_ctorGetLabels(ctor);

    tr_peerMgrAddTorrent(session->peerMgr, tor);

    TR_ASSERT(tor->downloadedCur == 0);
    TR_ASSERT(tor->uploadedCur == 0);

    auto const now = tr_time();
    tor->addedDate = now; // this is a default that will be overwritten by the resume file
    tor->anyDate = now;

    // tr_resume::load() calls a lot of tr_torrentSetFoo() methods
    // that set things as dirty, but... these settings being loaded are
    // the same ones that would be saved back again, so don't let them
    // affect the 'is dirty' flag.
    auto const was_dirty = tor->isDirty;
    bool resume_file_was_migrated = false;
    auto const loaded = tr_resume::load(tor, tr_resume::All, ctor, &resume_file_was_migrated);
    tor->isDirty = was_dirty;

    if (resume_file_was_migrated)
    {
        tr_torrent_metainfo::migrateFile(session->torrent_dir, tor->name(), tor->infoHashString(), ".torrent"sv);
    }

    tor->completeness = tor->completion.status();

    tr_ctorInitTorrentPriorities(ctor, tor);
    tr_ctorInitTorrentWanted(ctor, tor);

    refreshCurrentDir(tor);

    bool const doStart = tor->isRunning;
    tor->isRunning = false;

    if ((loaded & tr_resume::Speedlimit) == 0)
    {
        tr_torrentUseSpeedLimit(tor, TR_UP, false);
        tor->setSpeedLimitBps(TR_UP, tr_sessionGetSpeedLimit_Bps(tor->session, TR_UP));
        tr_torrentUseSpeedLimit(tor, TR_DOWN, false);
        tor->setSpeedLimitBps(TR_DOWN, tr_sessionGetSpeedLimit_Bps(tor->session, TR_DOWN));
        tr_torrentUseSessionLimits(tor, true);
    }

    if ((loaded & tr_resume::Ratiolimit) == 0)
    {
        tr_torrentSetRatioMode(tor, TR_RATIOLIMIT_GLOBAL);
        tr_torrentSetRatioLimit(tor, tr_sessionGetRatioLimit(tor->session));
    }

    if ((loaded & tr_resume::Idlelimit) == 0)
    {
        tr_torrentSetIdleMode(tor, TR_IDLELIMIT_GLOBAL);
        tr_torrentSetIdleLimit(tor, tr_sessionGetIdleLimit(tor->session));
    }

    tr_sessionAddTorrent(session, tor);

    // if we don't have a local .torrent file already, assume the torrent is new
    auto const filename = tor->torrentFile();
    bool const is_new_torrent = !tr_sys_path_exists(filename.c_str(), nullptr);
    if (is_new_torrent)
    {
        tr_error* error = nullptr;
        if (!tr_ctorSaveContents(ctor, filename, &error))
        {
            tor->setLocalError(
                tr_strvJoin("Unable to save torrent file: ", error->message, " ("sv, std::to_string(error->code), ")"sv));
        }
        tr_error_clear(&error);
    }

    tor->torrent_announcer = tr_announcerAddTorrent(tor, onTrackerResponse, nullptr);

    if (is_new_torrent)
    {
        if (tor->hasMetadata())
        {
            callScriptIfEnabled(tor, TR_SCRIPT_ON_TORRENT_ADDED);
        }

        if (!tor->hasMetadata() && !doStart)
        {
            tor->prefetchMagnetMetadata = true;
            tr_torrentStartNow(tor);
        }
        else if (isNewTorrentASeed(tor))
        {
            tor->completion.setHasAll();
            tor->doneDate = tor->addedDate;
            tor->recheckCompleteness();
        }
        else
        {
            tor->startAfterVerify = doStart;
            tr_torrentVerify(tor);
        }
    }
    else if (doStart)
    {
        tr_torrentStart(tor);
    }
    else
    {
        setLocalErrorIfFilesDisappeared(tor);
    }
}

tr_torrent* tr_torrentNew(tr_ctor* ctor, tr_torrent** setme_duplicate_of)
{
    TR_ASSERT(ctor != nullptr);
    auto* const session = tr_ctorGetSession(ctor);
    TR_ASSERT(tr_isSession(session));

    // is the metainfo valid?
    auto metainfo = tr_ctorStealMetainfo(ctor);
    if (std::empty(metainfo.infoHashString()))
    {
        return nullptr;
    }

    // is it a duplicate?
    if (auto* const duplicate_of = session->getTorrent(metainfo.infoHash()); duplicate_of != nullptr)
    {
        if (setme_duplicate_of != nullptr)
        {
            *setme_duplicate_of = duplicate_of;
        }

        return nullptr;
    }

    auto* const tor = new tr_torrent{ std::move(metainfo) };
    torrentInit(tor, ctor);
    return tor;
}

/**
***
**/

void tr_torrentSetDownloadDir(tr_torrent* tor, char const* path)
{
    TR_ASSERT(tr_isTorrent(tor));

    if (tor->download_dir != path)
    {
        tor->download_dir = path;
        tor->markEdited();
        tor->setDirty();
    }

    refreshCurrentDir(tor);
}

char const* tr_torrentGetDownloadDir(tr_torrent const* tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    return tor->downloadDir().c_str();
}

char const* tr_torrentGetCurrentDir(tr_torrent const* tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    return tor->currentDir().c_str();
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
    auto* tor = static_cast<tr_torrent*>(vtor);

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

tr_stat const* tr_torrentStatCached(tr_torrent* tor)
{
    time_t const now = tr_time();

    return (tr_isTorrent(tor) && now == tor->lastStatTime) ? &tor->stats : tr_torrentStat(tor);
}

void tr_torrent::setVerifyState(tr_verify_state state)
{
    TR_ASSERT(state == TR_VERIFY_NONE || state == TR_VERIFY_WAIT || state == TR_VERIFY_NOW);

    this->verifyState = state;
    this->markChanged();
}

tr_torrent_activity tr_torrentGetActivity(tr_torrent const* tor)
{
    tr_torrent_activity ret = TR_STATUS_STOPPED;

    bool const is_seed = tor->isDone();

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
    else if (tor->isQueued())
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

static int torrentGetIdleSecs(tr_torrent const* tor, tr_torrent_activity activity)
{
    return ((activity == TR_STATUS_DOWNLOAD || activity == TR_STATUS_SEED) && tor->startDate != 0) ?
        (int)difftime(tr_time(), std::max(tor->startDate, tor->activityDate)) :
        -1;
}

static inline bool tr_torrentIsStalled(tr_torrent const* tor, int idle_secs)
{
    return tr_sessionGetQueueStalledEnabled(tor->session) && idle_secs > tr_sessionGetQueueStalledMinutes(tor->session) * 60;
}

static double getVerifyProgress(tr_torrent const* tor)
{
    return tor->verify_progress ? *tor->verify_progress : 0.0;
}

tr_stat const* tr_torrentStat(tr_torrent* tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    uint64_t const now = tr_time_msec();

    auto swarm_stats = tr_swarm_stats{};

    tor->lastStatTime = tr_time();

    if (tor->swarm != nullptr)
    {
        tr_swarmGetStats(tor->swarm, &swarm_stats);
    }

    tr_stat* const s = &tor->stats;
    s->id = tor->uniqueId;
    s->activity = tr_torrentGetActivity(tor);
    s->error = tor->error;
    s->queuePosition = tor->queuePosition;
    s->idleSecs = torrentGetIdleSecs(tor, s->activity);
    s->isStalled = tr_torrentIsStalled(tor, s->idleSecs);
    s->errorString = tor->error_string.c_str();

    s->manualAnnounceTime = tr_announcerNextManualAnnounce(tor);
    s->peersConnected = swarm_stats.peerCount;
    s->peersSendingToUs = swarm_stats.activePeerCount[TR_DOWN];
    s->peersGettingFromUs = swarm_stats.activePeerCount[TR_UP];
    s->webseedsSendingToUs = swarm_stats.activeWebseedCount;

    for (int i = 0; i < TR_PEER_FROM__MAX; i++)
    {
        s->peersFrom[i] = swarm_stats.peerFromCount[i];
    }

    s->rawUploadSpeed_KBps = tr_toSpeedKBps(tor->bandwidth->getRawSpeedBytesPerSecond(now, TR_UP));
    s->rawDownloadSpeed_KBps = tr_toSpeedKBps(tor->bandwidth->getRawSpeedBytesPerSecond(now, TR_DOWN));
    auto const pieceUploadSpeed_Bps = tor->bandwidth->getPieceSpeedBytesPerSecond(now, TR_UP);
    s->pieceUploadSpeed_KBps = tr_toSpeedKBps(pieceUploadSpeed_Bps);
    auto const pieceDownloadSpeed_Bps = tor->bandwidth->getPieceSpeedBytesPerSecond(now, TR_DOWN);
    s->pieceDownloadSpeed_KBps = tr_toSpeedKBps(pieceDownloadSpeed_Bps);

    s->percentComplete = tor->completion.percentComplete();
    s->metadataPercentComplete = tr_torrentGetMetadataPercent(tor);

    s->percentDone = tor->completion.percentDone();
    s->leftUntilDone = tor->completion.leftUntilDone();
    s->sizeWhenDone = tor->completion.sizeWhenDone();
    s->recheckProgress = s->activity == TR_STATUS_CHECK ? getVerifyProgress(tor) : 0;
    s->activityDate = tor->activityDate;
    s->addedDate = tor->addedDate;
    s->doneDate = tor->doneDate;
    s->editDate = tor->editDate;
    s->startDate = tor->startDate;
    s->secondsSeeding = tor->secondsSeeding;
    s->secondsDownloading = tor->secondsDownloading;

    s->corruptEver = tor->corruptCur + tor->corruptPrev;
    s->downloadedEver = tor->downloadedCur + tor->downloadedPrev;
    s->uploadedEver = tor->uploadedCur + tor->uploadedPrev;
    s->haveValid = tor->completion.hasValid();
    s->haveUnchecked = tor->hasTotal() - s->haveValid;
    s->desiredAvailable = tr_peerMgrGetDesiredAvailable(tor);

    s->ratio = tr_getRatio(s->uploadedEver, tor->totalSize());

    auto seedRatioBytesLeft = uint64_t{};
    auto seedRatioBytesGoal = uint64_t{};
    bool const seedRatioApplies = tr_torrentGetSeedRatioBytes(tor, &seedRatioBytesLeft, &seedRatioBytesGoal);

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

        if (s->leftUntilDone > s->desiredAvailable && tor->webseedCount() < 1)
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

        {
            auto seedIdleMinutes = uint16_t{};
            s->etaIdle = tor->etaULSpeed_Bps < 1 && tr_torrentGetSeedIdle(tor, &seedIdleMinutes) ?
                seedIdleMinutes * 60 - s->idleSecs :
                TR_ETA_NOT_AVAIL;
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
        s->seedRatioPercentDone = 1.0F;
    }
    else if (seedRatioBytesGoal == 0) /* impossible? safeguard for div by zero */
    {
        s->seedRatioPercentDone = 0.0F;
    }
    else
    {
        s->seedRatioPercentDone = float(seedRatioBytesGoal - seedRatioBytesLeft) / seedRatioBytesGoal;
    }

    /* test some of the constraints */
    TR_ASSERT(s->sizeWhenDone <= tor->totalSize());
    TR_ASSERT(s->leftUntilDone <= s->sizeWhenDone);
    TR_ASSERT(s->desiredAvailable <= s->leftUntilDone);

    return s;
}

/***
****
***/

tr_file_view tr_torrentFile(tr_torrent const* tor, tr_file_index_t i)
{
    TR_ASSERT(tr_isTorrent(tor));

    auto const& subpath = tor->fileSubpath(i);
    auto const priority = tor->file_priorities_.filePriority(i);
    auto const wanted = tor->files_wanted_.fileWanted(i);
    auto const length = tor->fileSize(i);

    if (tor->completeness == TR_SEED || length == 0)
    {
        return { subpath.c_str(), length, length, 1.0, priority, wanted };
    }

    auto const have = tor->completion.countHasBytesInSpan(tor->fpm_.byteSpan(i));
    return { subpath.c_str(), have, length, have >= length ? 1.0 : have / double(length), priority, wanted };
}

size_t tr_torrentFileCount(tr_torrent const* torrent)
{
    TR_ASSERT(tr_isTorrent(torrent));

    return torrent->fileCount();
}

tr_webseed_view tr_torrentWebseed(tr_torrent const* tor, size_t i)
{
    return tr_peerMgrWebseed(tor, i);
}

size_t tr_torrentWebseedCount(tr_torrent const* tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    return tor->webseedCount();
}

tr_tracker_view tr_torrentTracker(tr_torrent const* tor, size_t i)
{
    return tr_announcerTracker(tor, i);
}

size_t tr_torrentTrackerCount(tr_torrent const* tor)
{
    return tr_announcerTrackerCount(tor);
}

tr_torrent_view tr_torrentView(tr_torrent const* tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    auto ret = tr_torrent_view{};
    ret.name = tr_torrentName(tor);
    ret.hash_string = tor->infoHashString().c_str();
    ret.comment = tor->comment().c_str();
    ret.creator = tor->creator().c_str();
    ret.source = tor->source().c_str();
    ret.total_size = tor->totalSize();
    ret.date_created = tor->dateCreated();
    ret.piece_size = tor->pieceSize();
    ret.n_pieces = tor->pieceCount();
    ret.is_private = tor->isPrivate();
    ret.is_folder = tor->fileCount() > 1;

    return ret;
}

char* tr_torrentFilename(tr_torrent const* tor)
{
    return tr_strvDup(tor->torrentFile());
}

/***
****
***/

tr_peer_stat* tr_torrentPeers(tr_torrent const* tor, int* peerCount)
{
    TR_ASSERT(tr_isTorrent(tor));

    return tr_peerMgrPeerStats(tor, peerCount);
}

void tr_torrentPeersFree(tr_peer_stat* peers, int /*peerCount*/)
{
    tr_free(peers);
}

void tr_torrentAvailability(tr_torrent const* tor, int8_t* tab, int size)
{
    TR_ASSERT(tr_isTorrent(tor));

    if (tab != nullptr && size > 0)
    {
        tr_peerMgrTorrentAvailability(tor, tab, size);
    }
}

void tr_torrentAmountFinished(tr_torrent const* tor, float* tabs, int n_tabs)
{
    return tor->amountDoneBins(tabs, n_tabs);
}

static void tr_torrentResetTransferStats(tr_torrent* tor)
{
    auto const lock = tor->unique_lock();

    tor->downloadedPrev += tor->downloadedCur;
    tor->downloadedCur = 0;
    tor->uploadedPrev += tor->uploadedCur;
    tor->uploadedCur = 0;
    tor->corruptPrev += tor->corruptCur;
    tor->corruptCur = 0;

    tor->setDirty();
}

/***
****
***/

#ifdef TR_ENABLE_ASSERTS
static bool queueIsSequenced(tr_session* /*session*/);
#endif

static void freeTorrent(tr_torrent* tor)
{
    auto const lock = tor->unique_lock();

    TR_ASSERT(!tor->isRunning);

    tr_session* session = tor->session;

    tr_peerMgrRemoveTorrent(tor);

    tr_announcerRemoveTorrent(session->announcer, tor);

    tr_sessionRemoveTorrent(session, tor);

    if (!session->isClosing())
    {
        // "so you die, captain, and we all move up in rank."
        // resequence the queue positions
        for (auto* t : session->torrents)
        {
            if (t->queuePosition > tor->queuePosition)
            {
                t->queuePosition--;
                t->markChanged();
            }
        }

        TR_ASSERT(queueIsSequenced(session));
    }

    delete tor->bandwidth;
    delete tor;
}

/**
***  Start/Stop Callback
**/

static void torrentSetQueued(tr_torrent* tor, bool queued);

static void torrentStartImpl(void* vtor)
{
    auto* tor = static_cast<tr_torrent*>(vtor);
    auto const lock = tor->unique_lock();

    TR_ASSERT(tr_isTorrent(tor));

    tor->recheckCompleteness();
    torrentSetQueued(tor, false);

    time_t const now = tr_time();

    tor->isRunning = true;
    tor->completeness = tor->completion.status();
    tor->startDate = now;
    tor->markChanged();
    tr_torrentClearError(tor);
    tor->finishedSeedingByIdle = false;

    tr_torrentResetTransferStats(tor);
    tr_announcerTorrentStarted(tor);
    tor->dhtAnnounceAt = now + tr_rand_int_weak(20);
    tor->dhtAnnounce6At = now + tr_rand_int_weak(20);
    tor->lpdAnnounceAt = now;
    tr_peerMgrStartTorrent(tor);
}

static bool torrentShouldQueue(tr_torrent const* tor)
{
    tr_direction const dir = tor->queueDirection();

    return tr_sessionCountQueueFreeSlots(tor->session, dir) == 0;
}

static void torrentStart(tr_torrent* tor, bool bypass_queue)
{
    switch (tr_torrentGetActivity(tor))
    {
    case TR_STATUS_SEED:
    case TR_STATUS_DOWNLOAD:
        return; /* already started */

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
    auto const lock = tor->unique_lock();

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
    tor->setDirty();
    tr_runInEventThread(tor->session, torrentStartImpl, tor);
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

static void onVerifyDoneThreadFunc(void* vtor)
{
    auto* const tor = static_cast<tr_torrent*>(vtor);
    TR_ASSERT(tr_amInEventThread(tor->session));

    if (tor->isDeleting)
    {
        return;
    }

    tor->recheckCompleteness();

    if (tor->startAfterVerify)
    {
        tor->startAfterVerify = false;
        torrentStart(tor, false);
    }
}

static void onVerifyDone(tr_torrent* tor, bool aborted, void* /*unused*/)
{
    if (aborted || tor->isDeleting)
    {
        return;
    }

    tr_runInEventThread(tor->session, onVerifyDoneThreadFunc, tor);
}

static void verifyTorrent(void* vtor)
{
    auto* tor = static_cast<tr_torrent*>(vtor);
    TR_ASSERT(tr_amInEventThread(tor->session));
    auto const lock = tor->unique_lock();

    if (tor->isDeleting)
    {
        return;
    }

    /* if the torrent's already being verified, stop it */
    tr_verifyRemove(tor);

    bool const startAfter = (tor->isRunning || tor->startAfterVerify) && !tor->isStopping;

    if (tor->isRunning)
    {
        tr_torrentStop(tor);
    }

    if (setLocalErrorIfFilesDisappeared(tor))
    {
        tor->startAfterVerify = false;
    }
    else
    {
        tor->startAfterVerify = startAfter;
        tr_verifyAdd(tor, onVerifyDone, nullptr);
    }
}

void tr_torrentVerify(tr_torrent* tor)
{
    tr_runInEventThread(tor->session, verifyTorrent, tor);
}

void tr_torrentSave(tr_torrent* tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    if (tor->isDirty)
    {
        tor->isDirty = false;
        tr_resume::save(tor);
    }
}

static void stopTorrent(void* vtor)
{
    auto* tor = static_cast<tr_torrent*>(vtor);
    TR_ASSERT(tr_isTorrent(tor));
    TR_ASSERT(tr_amInEventThread(tor->session));
    auto const lock = tor->unique_lock();

    tr_logAddTorInfo(tor, "%s", "Pausing");

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

    if (tor->magnetVerify)
    {
        tor->magnetVerify = false;
        tr_logAddTorInfo(tor, "%s", "Magnet Verify");
        refreshCurrentDir(tor);
        tr_torrentVerify(tor);

        callScriptIfEnabled(tor, TR_SCRIPT_ON_TORRENT_ADDED);
    }
}

void tr_torrentStop(tr_torrent* tor)
{
    if (!tr_isTorrent(tor))
    {
        return;
    }

    auto const lock = tor->unique_lock();

    tor->isRunning = false;
    tor->isStopping = false;
    tor->prefetchMagnetMetadata = false;
    tor->setDirty();
    tr_runInEventThread(tor->session, stopTorrent, tor);
}

static void closeTorrent(void* vtor)
{
    auto* const tor = static_cast<tr_torrent*>(vtor);
    TR_ASSERT(tr_isTorrent(tor));
    TR_ASSERT(tr_amInEventThread(tor->session));

    tor->session->removed_torrents.emplace_back(tor->uniqueId, tr_time());

    tr_logAddTorInfo(tor, "%s", _("Removing torrent"));

    tor->magnetVerify = false;
    stopTorrent(tor);

    if (tor->isDeleting)
    {
        tr_torrent_metainfo::removeFile(tor->session->torrent_dir, tor->name(), tor->infoHashString(), ".torrent"sv);
        tr_torrent_metainfo::removeFile(tor->session->resume_dir, tor->name(), tor->infoHashString(), ".resume"sv);
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

        auto const lock = tor->unique_lock();

        tr_torrentClearCompletenessCallback(tor);
        tr_runInEventThread(session, closeTorrent, tor);
    }
}

struct remove_data
{
    tr_torrent* tor;
    bool deleteFlag;
    tr_fileFunc deleteFunc;
};

static void tr_torrentDeleteLocalData(tr_torrent* /*tor*/, tr_fileFunc /*func*/);

static void removeTorrent(void* vdata)
{
    auto* const data = static_cast<struct remove_data*>(vdata);
    auto const lock = data->tor->unique_lock();

    if (data->deleteFlag)
    {
        tr_torrentDeleteLocalData(data->tor, data->deleteFunc);
    }

    tr_torrentClearCompletenessCallback(data->tor);
    closeTorrent(data->tor);
    tr_free(data);
}

void tr_torrentRemove(tr_torrent* tor, bool deleteFlag, tr_fileFunc deleteFunc)
{
    TR_ASSERT(tr_isTorrent(tor));

    tor->isDeleting = true;

    auto* const data = tr_new0(struct remove_data, 1);
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

    if (tor->completeness_func != nullptr)
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
    tr_torrentSetCompletenessCallback(torrent, nullptr, nullptr);
}

void tr_torrentSetRatioLimitHitCallback(tr_torrent* tor, tr_torrent_ratio_limit_hit_func func, void* user_data)
{
    TR_ASSERT(tr_isTorrent(tor));

    tor->ratio_limit_hit_func = func;
    tor->ratio_limit_hit_func_user_data = user_data;
}

void tr_torrentClearRatioLimitHitCallback(tr_torrent* torrent)
{
    tr_torrentSetRatioLimitHitCallback(torrent, nullptr, nullptr);
}

void tr_torrentSetIdleLimitHitCallback(tr_torrent* tor, tr_torrent_idle_limit_hit_func func, void* user_data)
{
    TR_ASSERT(tr_isTorrent(tor));

    tor->idle_limit_hit_func = func;
    tor->idle_limit_hit_func_user_data = user_data;
}

void tr_torrentClearIdleLimitHitCallback(tr_torrent* torrent)
{
    tr_torrentSetIdleLimitHitCallback(torrent, nullptr, nullptr);
}

static std::string buildLabelsString(tr_torrent const* tor)
{
    auto buf = std::stringstream{};

    for (auto it = std::begin(tor->labels), end = std::end(tor->labels); it != end;)
    {
        buf << *it;

        if (++it != end)
        {
            buf << ',';
        }
    }

    return buf.str();
}

static std::string buildTrackersString(tr_torrent const* tor)
{
    auto buf = std::stringstream{};

    for (size_t i = 0, n = tr_torrentTrackerCount(tor); i < n; ++i)
    {
        buf << tr_torrentTracker(tor, i).host;

        if (++i < n)
        {
            buf << ',';
        }
    }

    return buf.str();
}

static void torrentCallScript(tr_torrent const* tor, char const* script)
{
    if (tr_str_is_empty(script))
    {
        return;
    }

    time_t const now = tr_time();
    struct tm tm;
    char ctime_str[32];
    tr_localtime_r(&now, &tm);
    strftime(ctime_str, sizeof(ctime_str), "%a %b %d %T %Y%n", &tm); /* ctime equiv */

    auto torrent_dir = std::string{ tor->currentDir().sv() };
    tr_sys_path_native_separators(std::data(torrent_dir));

    auto const cmd = std::array<char const*, 2>{ script, nullptr };

    auto const id_str = std::to_string(tr_torrentId(tor));
    auto const labels_str = buildLabelsString(tor);
    auto const trackers_str = buildTrackersString(tor);
    auto const bytes_downloaded_str = std::to_string(tor->downloadedCur + tor->downloadedPrev);

    auto const env = std::map<std::string_view, std::string_view>{
        { "TR_APP_VERSION"sv, SHORT_VERSION_STRING },
        { "TR_TIME_LOCALTIME"sv, ctime_str },
        { "TR_TORRENT_BYTES_DOWNLOADED"sv, bytes_downloaded_str },
        { "TR_TORRENT_DIR"sv, torrent_dir.c_str() },
        { "TR_TORRENT_HASH"sv, tor->infoHashString() },
        { "TR_TORRENT_ID"sv, id_str },
        { "TR_TORRENT_LABELS"sv, labels_str },
        { "TR_TORRENT_NAME"sv, tr_torrentName(tor) },
        { "TR_TORRENT_TRACKERS"sv, trackers_str },
    };

    tr_logAddTorInfo(tor, "Calling script \"%s\"", script);

    tr_error* error = nullptr;

    if (!tr_spawn_async(std::data(cmd), env, TR_IF_WIN32("\\", "/"), &error))
    {
        tr_logAddTorErr(tor, "Error executing script \"%s\" (%d): %s", script, error->code, error->message);
        tr_error_free(error);
    }
}

void tr_torrent::recheckCompleteness()
{
    auto const lock = unique_lock();

    auto const new_completeness = completion.status();

    if (new_completeness != completeness)
    {
        bool const recentChange = downloadedCur != 0;
        bool const wasLeeching = !this->isDone();
        bool const wasRunning = isRunning;

        if (recentChange)
        {
            tr_logAddTorInfo(
                this,
                _("State changed from \"%1$s\" to \"%2$s\""),
                getCompletionString(this->completeness),
                getCompletionString(completeness));
        }

        this->completeness = new_completeness;
        tr_fdTorrentClose(this->session, this->uniqueId);

        if (this->isDone())
        {
            if (recentChange)
            {
                tr_announcerTorrentCompleted(this);
                this->markChanged();
                this->doneDate = tr_time();
            }

            if (wasLeeching && wasRunning)
            {
                /* clear interested flag on all peers */
                tr_peerMgrClearInterest(this);
            }

            if (this->currentDir() == this->incompleteDir())
            {
                this->setLocation(this->downloadDir().sv(), true, nullptr, nullptr);
            }
        }

        fireCompletenessChange(this, completeness, wasRunning);

        if (this->isDone() && wasLeeching && wasRunning)
        {
            /* if completeness was TR_LEECH, the seed limit check
               will have been skipped in bandwidthPulse */
            tr_torrentCheckSeedLimit(this);
        }

        this->setDirty();

        if (this->isDone())
        {
            tr_torrentSave(this);
            callScriptIfEnabled(this, TR_SCRIPT_ON_TORRENT_DONE);
        }
    }
}

/***
****
***/

static void tr_torrentFireMetadataCompleted(tr_torrent* tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    if (tor->metadata_func != nullptr)
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
***  File DND
**/

void tr_torrentSetFileDLs(tr_torrent* tor, tr_file_index_t const* files, tr_file_index_t n_files, bool wanted)
{
    TR_ASSERT(tr_isTorrent(tor));

    tor->setFilesWanted(files, n_files, wanted);
}

/***
****
***/

void tr_torrentSetLabels(tr_torrent* tor, tr_labels_t&& labels)
{
    TR_ASSERT(tr_isTorrent(tor));
    auto const lock = tor->unique_lock();

    tor->labels = std::move(labels);
    tor->setDirty();
}

/***
****
***/

tr_priority_t tr_torrentGetPriority(tr_torrent const* tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    return tor->bandwidth->getPriority();
}

void tr_torrentSetPriority(tr_torrent* tor, tr_priority_t priority)
{
    TR_ASSERT(tr_isTorrent(tor));
    TR_ASSERT(tr_isPriority(priority));

    if (tor->bandwidth->getPriority() != priority)
    {
        tor->bandwidth->setPriority(priority);

        tor->setDirty();
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

        tor->setDirty();
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

void tr_torrentGetBlockLocation(
    tr_torrent const* tor,
    tr_block_index_t block,
    tr_piece_index_t* piece,
    uint32_t* offset,
    uint32_t* length)
{
    uint64_t pos = block;
    pos *= tor->blockSize();
    *piece = pos / tor->pieceSize();
    uint64_t piece_begin = tor->pieceSize();
    piece_begin *= *piece;
    *offset = pos - piece_begin;
    *length = tor->blockSize(block);
}

bool tr_torrentReqIsValid(tr_torrent const* tor, tr_piece_index_t index, uint32_t offset, uint32_t length)
{
    TR_ASSERT(tr_isTorrent(tor));

    int err = 0;

    if (index >= tor->pieceCount())
    {
        err = 1;
    }
    else if (length < 1)
    {
        err = 2;
    }
    else if (offset + length > tor->pieceSize(index))
    {
        err = 3;
    }
    else if (length > MAX_BLOCK_SIZE)
    {
        err = 4;
    }
    else if (tor->pieceLoc(index, offset, length).byte > tor->totalSize())
    {
        err = 5;
    }

    if (err != 0)
    {
        tr_logAddTorDbg(
            tor,
            "index %lu offset %lu length %lu err %d\n",
            (unsigned long)index,
            (unsigned long)offset,
            (unsigned long)length,
            err);
    }

    return err == 0;
}

// TODO(ckerr) migrate to fpm?
tr_block_span_t tr_torGetFileBlockSpan(tr_torrent const* tor, tr_file_index_t i)
{
    auto const [begin_byte, end_byte] = tor->fpm_.byteSpan(i);

    auto const begin_block = tor->byteLoc(begin_byte).block;
    if (begin_byte >= end_byte) // 0-byte file
    {
        return { begin_block, begin_block + 1 };
    }

    auto const final_block = tor->byteLoc(end_byte - 1).block;
    auto const end_block = final_block + 1;
    return { begin_block, end_block };
}

/***
****
***/

// TODO: should be const after tr_ioTestPiece() is const
bool tr_torrent::checkPiece(tr_piece_index_t piece)
{
    bool const pass = tr_ioTestPiece(this, piece);
    tr_logAddTorDbg(this, "[LAZY] tr_torrent.checkPiece tested piece %zu, pass==%d", size_t(piece), int(pass));
    return pass;
}

/***
****
***/

bool tr_torrent::setTrackerList(std::string_view text)
{
    auto const lock = this->unique_lock();

    auto announce_list = tr_announce_list();
    if (!announce_list.parse(text) || !announce_list.save(this->torrentFile()))
    {
        return false;
    }

    this->metainfo_.announceList() = announce_list;
    this->markEdited();

    /* if we had a tracker-related error on this torrent,
     * and that tracker's been removed,
     * then clear the error */
    if (this->error == TR_STAT_TRACKER_WARNING || this->error == TR_STAT_TRACKER_ERROR)
    {
        auto const error_url = this->error_announce_url;

        if (std::any_of(
                std::begin(this->announceList()),
                std::end(this->announceList()),
                [error_url](auto const& tracker) { return tracker.announce_str == error_url; }))
        {
            tr_torrentClearError(this);
        }
    }

    /* tell the announcer to reload this torrent's tracker list */
    tr_announcerResetTorrent(this->session->announcer, this);

    return true;
}

bool tr_torrentSetTrackerList(tr_torrent* tor, char const* text)
{
    return text != nullptr && tor->setTrackerList(text);
}

char* tr_torrentGetTrackerList(tr_torrent const* tor)
{
    return tr_strvDup(tor->trackerList());
}

/**
***
**/

uint64_t tr_torrentGetBytesLeftToAllocate(tr_torrent const* tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    uint64_t bytesLeft = 0;

    for (tr_file_index_t i = 0, n = tor->fileCount(); i < n; ++i)
    {
        auto const file = tr_torrentFile(tor, i);

        if (file.wanted)
        {
            uint64_t const length = file.length;
            char* path = tr_torrentFindFile(tor, i);

            bytesLeft += length;

            tr_sys_path_info info;
            if (path != nullptr && tr_sys_path_get_info(path, 0, &info, nullptr) && info.type == TR_SYS_PATH_IS_FILE &&
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

static bool isJunkFile(std::string_view base)
{
#ifdef __APPLE__
    // check for resource forks. <http://support.apple.com/kb/TA20578>
    if (tr_strvStartsWith(base, "._"sv))
    {
        return true;
    }
#endif

    auto constexpr Files = std::array<std::string_view, 3>{
        ".DS_Store"sv,
        "Thumbs.db"sv,
        "desktop.ini"sv,
    };

    return std::find(std::begin(Files), std::end(Files), base) != std::end(Files);
}

static void removeEmptyFoldersAndJunkFiles(char const* folder)
{
    auto const odir = tr_sys_dir_open(folder, nullptr);
    if (odir == TR_BAD_SYS_DIR)
    {
        return;
    }

    char const* name = nullptr;
    while ((name = tr_sys_dir_read_name(odir, nullptr)) != nullptr)
    {
        if (strcmp(name, ".") != 0 && strcmp(name, "..") != 0)
        {
            auto const filename = tr_strvPath(folder, name);

            auto info = tr_sys_path_info{};
            if (tr_sys_path_get_info(filename.c_str(), TR_SYS_PATH_NO_FOLLOW, &info, nullptr) &&
                info.type == TR_SYS_PATH_IS_DIRECTORY)
            {
                removeEmptyFoldersAndJunkFiles(filename.c_str());
            }
            else if (isJunkFile(name))
            {
                tr_sys_path_remove(filename.c_str(), nullptr);
            }
        }
    }

    tr_sys_path_remove(folder, nullptr);
    tr_sys_dir_close(odir, nullptr);
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
static void deleteLocalData(tr_torrent const* tor, tr_fileFunc func)
{
    auto files = std::vector<std::string>{};
    auto folders = std::set<std::string>{};
    char const* const top = tor->currentDir().c_str();

    /* don't try to delete local data if the directory's gone missing */
    if (!tr_sys_path_exists(top, nullptr))
    {
        return;
    }

    /* if it's a magnet link, there's nothing to move... */
    if (!tor->hasMetadata())
    {
        return;
    }

    /***
    ****  Move the local data to a new tmpdir
    ***/

    auto tmpdir = tr_strvPath(top, tr_torrentName(tor) + "__XXXXXX"s);
    tr_sys_dir_create_temp(std::data(tmpdir), nullptr);

    for (tr_file_index_t f = 0, n = tor->fileCount(); f < n; ++f)
    {
        /* try to find the file, looking in the partial and download dirs */
        auto filename = tr_strvPath(top, tor->fileSubpath(f));

        if (!tr_sys_path_exists(filename.c_str(), nullptr))
        {
            filename += ".part"sv;

            if (!tr_sys_path_exists(filename.c_str(), nullptr))
            {
                filename.clear();
            }
        }

        /* if we found the file, move it */
        if (!std::empty(filename))
        {
            auto target = tr_strvPath(tmpdir, tor->fileSubpath(f));
            tr_moveFile(filename.c_str(), target.c_str(), nullptr);
            files.emplace_back(target);
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
    if (auto const odir = tr_sys_dir_open(tmpdir.c_str(), nullptr); odir != TR_BAD_SYS_DIR)
    {
        char const* name = nullptr;
        while ((name = tr_sys_dir_read_name(odir, nullptr)) != nullptr)
        {
            if (strcmp(name, ".") != 0 && strcmp(name, "..") != 0)
            {
                auto const file = tr_strvPath(tmpdir, name);
                (*func)(file.c_str(), nullptr);
            }
        }

        tr_sys_dir_close(odir, nullptr);
    }

    /* go from the bottom up */
    for (auto const& file : files)
    {
        char* walk = tr_strvDup(file);

        while (tr_sys_path_exists(walk, nullptr) && !tr_sys_path_is_same(tmpdir.c_str(), walk, nullptr))
        {
            char* tmp = tr_sys_path_dirname(walk, nullptr);
            (*func)(walk, nullptr);
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
    for (tr_file_index_t f = 0, n = tor->fileCount(); f < n; ++f)
    {
        /* get the directory that this file goes in... */
        auto const filename = tr_strvPath(top, tor->fileSubpath(f));
        char* dir = tr_sys_path_dirname(filename.c_str(), nullptr);
        if (dir == nullptr)
        {
            continue;
        }

        /* walk up the directory tree until we reach 'top' */
        if (!tr_sys_path_is_same(top, dir, nullptr) && strcmp(top, dir) != 0)
        {
            for (;;)
            {
                char* parent = tr_sys_path_dirname(dir, nullptr);

                if (tr_sys_path_is_same(top, parent, nullptr) || strcmp(top, parent) == 0)
                {
                    folders.emplace(dir);
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

    for (auto const& folder : folders)
    {
        removeEmptyFoldersAndJunkFiles(folder.c_str());
    }

    /* cleanup */
    tr_sys_path_remove(tmpdir.c_str(), nullptr);
}

static void tr_torrentDeleteLocalData(tr_torrent* tor, tr_fileFunc func)
{
    TR_ASSERT(tr_isTorrent(tor));

    if (func == nullptr)
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
    std::string location;

    tr_torrent* tor = nullptr;
    double volatile* setme_progress = nullptr;
    int volatile* setme_state = nullptr;

    bool move_from_old_location = false;
};

static void setLocationImpl(void* vdata)
{
    auto* data = static_cast<struct LocationData*>(vdata);
    tr_torrent* tor = data->tor;
    TR_ASSERT(tr_isTorrent(tor));
    auto const lock = tor->unique_lock();

    bool err = false;
    bool const do_move = data->move_from_old_location;
    auto const& location = data->location;
    double bytesHandled = 0;

    tr_logAddDebug(
        "Moving \"%s\" location from currentDir \"%s\" to \"%s\"",
        tr_torrentName(tor),
        tor->currentDir().c_str(),
        location.c_str());

    tr_sys_dir_create(location.c_str(), TR_SYS_DIR_CREATE_PARENTS, 0777, nullptr);

    if (!tr_sys_path_is_same(location.c_str(), tor->currentDir().c_str(), nullptr))
    {
        /* bad idea to move files while they're being verified... */
        tr_verifyRemove(tor);

        /* try to move the files.
         * FIXME: there are still all kinds of nasty cases, like what
         * if the target directory runs out of space halfway through... */
        for (tr_file_index_t i = 0, n = tor->fileCount(); !err && i < n; ++i)
        {
            auto const file_size = tor->fileSize(i);

            char const* oldbase = nullptr;

            if (char* sub = nullptr; tr_torrentFindFile2(tor, i, &oldbase, &sub, nullptr))
            {
                auto const oldpath = tr_strvPath(oldbase, sub);
                auto const newpath = tr_strvPath(location, sub);

                tr_logAddDebug("Found file #%d: %s", (int)i, oldpath.c_str());

                if (do_move && !tr_sys_path_is_same(oldpath.c_str(), newpath.c_str(), nullptr))
                {
                    tr_error* error = nullptr;

                    tr_logAddTorInfo(tor, "moving \"%s\" to \"%s\"", oldpath.c_str(), newpath.c_str());

                    if (!tr_moveFile(oldpath.c_str(), newpath.c_str(), &error))
                    {
                        err = true;
                        tr_logAddTorErr(
                            tor,
                            "error moving \"%s\" to \"%s\": %s",
                            oldpath.c_str(),
                            newpath.c_str(),
                            error->message);
                        tr_error_free(error);
                    }
                }

                tr_free(sub);
            }

            if (data->setme_progress != nullptr)
            {
                bytesHandled += file_size;
                *data->setme_progress = bytesHandled / tor->totalSize();
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
        tr_torrentSetDownloadDir(tor, location.c_str());

        if (do_move)
        {
            tor->incomplete_dir.clear();
            tor->current_dir = tor->downloadDir();
        }
    }

    if (data->setme_state != nullptr)
    {
        *data->setme_state = err ? TR_LOC_ERROR : TR_LOC_DONE;
    }

    /* cleanup */
    delete data;
}

void tr_torrent::setLocation(
    std::string_view location,
    bool move_from_old_location,
    double volatile* setme_progress,
    int volatile* setme_state)
{
    if (setme_state != nullptr)
    {
        *setme_state = TR_LOC_MOVING;
    }

    if (setme_progress != nullptr)
    {
        *setme_progress = 0;
    }

    /* run this in the libtransmission thread */
    auto* const data = new LocationData{};
    data->tor = this;
    data->location = location;
    data->move_from_old_location = move_from_old_location;
    data->setme_state = setme_state;
    data->setme_progress = setme_progress;
    tr_runInEventThread(this->session, setLocationImpl, data);
}

void tr_torrentSetLocation(
    tr_torrent* tor,
    char const* location,
    bool move_from_old_location,
    double volatile* setme_progress,
    int volatile* setme_state)
{
    TR_ASSERT(tr_isTorrent(tor));
    TR_ASSERT(!tr_str_is_empty(location));

    return tor->setLocation(location != nullptr ? location : "", move_from_old_location, setme_progress, setme_state);
}

std::string_view tr_torrent::primaryMimeType() const
{
    // count up how many bytes there are for each mime-type in the torrent
    // NB: get_mime_type_for_filename() always returns the same ptr for a
    // mime_type, so its raw pointer can be used as a key.
    auto size_per_mime_type = std::unordered_map<std::string_view, size_t>{};
    for (tr_file_index_t i = 0, n = this->fileCount(); i < n; ++i)
    {
        auto const mime_type = tr_get_mime_type_for_filename(this->fileSubpath(i));
        size_per_mime_type[mime_type] += this->fileSize(i);
    }

    if (std::empty(size_per_mime_type))
    {
        // https://developer.mozilla.org/en-US/docs/Web/HTTP/Basics_of_HTTP/MIME_types/Common_types
        // application/octet-stream is the default value for all other cases.
        // An unknown file type should use this type.
        auto constexpr Fallback = "application/octet-stream"sv;
        return Fallback;
    }

    auto const it = std::max_element(
        std::begin(size_per_mime_type),
        std::end(size_per_mime_type),
        [](auto const& a, auto const& b) { return a.second < b.second; });
    return it->first;
}

/***
****
***/

static void tr_torrentFileCompleted(tr_torrent* tor, tr_file_index_t i)
{
    /* close the file so that we can reopen in read-only mode as needed */
    tr_cacheFlushFile(tor->session->cache, tor, i);
    tr_fdFileClose(tor->session, tor, i);

    /* now that the file is complete and closed, we can start watching its
     * mtime timestamp for changes to know if we need to reverify pieces */
    tor->file_mtimes_[i] = tr_time();

    /* if the torrent's current filename isn't the same as the one in the
     * metadata -- for example, if it had the ".part" suffix appended to
     * it until now -- then rename it to match the one in the metadata */
    char const* base = nullptr;
    char* sub = nullptr;
    if (tr_torrentFindFile2(tor, i, &base, &sub, nullptr))
    {
        if (auto const& file_subpath = tor->fileSubpath(i); file_subpath != sub)
        {
            auto const oldpath = tr_strvPath(base, sub);
            auto const newpath = tr_strvPath(base, file_subpath);
            tr_error* error = nullptr;

            if (!tr_sys_path_rename(oldpath.c_str(), newpath.c_str(), &error))
            {
                tr_logAddTorErr(tor, "Error moving \"%s\" to \"%s\": %s", oldpath.c_str(), newpath.c_str(), error->message);
                tr_error_free(error);
            }
        }

        tr_free(sub);
    }
}

static void tr_torrentPieceCompleted(tr_torrent* tor, tr_piece_index_t pieceIndex)
{
    tr_peerMgrPieceCompleted(tor, pieceIndex);

    // if this piece completes any file, invoke the fileCompleted func for it
    auto const [begin, end] = tor->fpm_.fileSpan(pieceIndex);
    for (tr_file_index_t file = begin; file < end; ++file)
    {
        if (tor->completion.hasBlocks(tr_torGetFileBlockSpan(tor, file)))
        {
            tr_torrentFileCompleted(tor, file);
        }
    }
}

void tr_torrentGotBlock(tr_torrent* tor, tr_block_index_t block)
{
    TR_ASSERT(tr_isTorrent(tor));
    TR_ASSERT(tr_amInEventThread(tor->session));

    bool const block_is_new = !tor->hasBlock(block);

    if (block_is_new)
    {
        tor->completion.addBlock(block);
        tor->setDirty();

        auto const piece = tor->blockLoc(block).piece;

        if (tor->hasPiece(piece))
        {
            if (tor->checkPiece(piece))
            {
                tr_torrentPieceCompleted(tor, piece);
            }
            else
            {
                uint32_t const n = tor->pieceSize(piece);
                tr_logAddTorErr(tor, _("Piece %" PRIu32 ", which was just downloaded, failed its checksum test"), piece);
                tor->corruptCur += n;
                tor->downloadedCur -= std::min(tor->downloadedCur, uint64_t{ n });
                tr_peerMgrGotBadPiece(tor, piece);
            }
        }
    }
    else
    {
        uint32_t const n = tor->blockSize(block);
        tor->downloadedCur -= std::min(tor->downloadedCur, uint64_t{ n });
        tr_logAddTorDbg(tor, "we have this block already...");
    }
}

/***
****
***/

std::optional<tr_torrent::tr_found_file_t> tr_torrent::findFile(std::string& filename, tr_file_index_t i) const
{
    auto const subpath = std::string_view{ this->fileSubpath(i) };
    auto file_info = tr_sys_path_info{};

    if (!std::empty(this->downloadDir()))
    {
        auto const base = this->downloadDir().sv();

        tr_buildBuf(filename, base, "/"sv, subpath);
        if (tr_sys_path_get_info(filename.c_str(), 0, &file_info, nullptr))
        {
            return tr_found_file_t{ file_info, filename, base };
        }

        tr_buildBuf(filename, base, "/"sv, subpath, ".part"sv);
        if (tr_sys_path_get_info(filename.c_str(), 0, &file_info, nullptr))
        {
            return tr_found_file_t{ file_info, filename, base };
        }
    }

    if (!std::empty(this->incompleteDir()))
    {
        auto const base = this->incompleteDir().sv();

        tr_buildBuf(filename, base, "/"sv, subpath);
        if (tr_sys_path_get_info(filename.c_str(), 0, &file_info, nullptr))
        {
            return tr_found_file_t{ file_info, filename, base };
        }

        tr_buildBuf(filename, base, "/"sv, subpath, ".part"sv);
        if (tr_sys_path_get_info(filename.c_str(), 0, &file_info, nullptr))
        {
            return tr_found_file_t{ file_info, filename, base };
        }
    }

    return {};
}

// TODO: clients that call this should call tr_torrent::findFile() instead
bool tr_torrentFindFile2(tr_torrent const* tor, tr_file_index_t fileNum, char const** base, char** subpath, time_t* mtime)
{
    auto filename = std::string{};
    auto const found = tor->findFile(filename, fileNum);

    if (!found)
    {
        return false;
    }

    if (base != nullptr)
    {
        *base = std::data(found->base);
    }

    if (subpath != nullptr)
    {
        *subpath = tr_strvDup(found->subpath);
    }

    if (mtime != nullptr)
    {
        *mtime = found->last_modified_at;
    }

    return true;
}

// TODO: clients that call this should call tr_torrent::findFile() instead
char* tr_torrentFindFile(tr_torrent const* tor, tr_file_index_t fileNum)
{
    auto filename = std::string{};
    auto const found = tor->findFile(filename, fileNum);
    return found ? tr_strdup(filename.c_str()) : nullptr;
}

/* Decide whether we should be looking for files in downloadDir or incompleteDir. */
static void refreshCurrentDir(tr_torrent* tor)
{
    tr_interned_string dir;

    if (std::empty(tor->incompleteDir()))
    {
        dir = tor->downloadDir();
    }
    else if (!tor->hasMetadata()) /* no files to find */
    {
        dir = tor->incompleteDir();
    }
    else
    {
        auto filename = std::string{};
        auto const found = tor->findFile(filename, 0);
        dir = found ? tr_interned_string{ found->base } : tor->incompleteDir();
    }

    TR_ASSERT(!std::empty(dir));
    TR_ASSERT(dir == tor->downloadDir() || dir == tor->incompleteDir());

    tor->current_dir = dir;
}

char* tr_torrentBuildPartial(tr_torrent const* tor, tr_file_index_t i)
{
    return tr_strvDup(tr_strvJoin(tor->fileSubpath(i), ".part"sv));
}

/***
****
***/

#ifdef TR_ENABLE_ASSERTS

static bool queueIsSequenced(tr_session* session)
{
    auto torrents = tr_sessionGetTorrents(session);
    std::sort(
        std::begin(torrents),
        std::end(torrents),
        [](auto const* a, auto const* b) { return a->queuePosition < b->queuePosition; });

#if 0

    fprintf(stderr, "%s", "queue: ");

    for (int i = 0; i < n; ++i)
    {
        fprintf(stderr, "%d ", tmp[i]->queuePosition);
    }

    fputc('\n', stderr);

#endif

    /* test them */
    bool is_sequenced = true;

    for (int i = 0, n = std::size(torrents); is_sequenced && i < n; ++i)
    {
        is_sequenced = torrents[i]->queuePosition == i;
    }

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
    int const old_pos = tor->queuePosition;

    if (pos < 0)
    {
        pos = 0;
    }

    tor->queuePosition = -1;

    for (auto* walk : tor->session->torrents)
    {
        if ((old_pos < pos) && (old_pos <= walk->queuePosition) && (walk->queuePosition <= pos))
        {
            walk->queuePosition--;
            walk->markChanged();
        }

        if ((old_pos > pos) && (pos <= walk->queuePosition) && (walk->queuePosition < old_pos))
        {
            walk->queuePosition++;
            walk->markChanged();
        }

        if (back < walk->queuePosition)
        {
            back = walk->queuePosition;
        }
    }

    tor->queuePosition = std::min(pos, back + 1);
    tor->markChanged();

    TR_ASSERT(queueIsSequenced(tor->session));
}

struct CompareTorrentByQueuePosition
{
    bool operator()(tr_torrent const* a, tr_torrent const* b) const
    {
        return a->queuePosition < b->queuePosition;
    }
};

void tr_torrentsQueueMoveTop(tr_torrent* const* torrents_in, size_t n)
{
    auto torrents = std::vector<tr_torrent*>(torrents_in, torrents_in + n);
    std::sort(std::rbegin(torrents), std::rend(torrents), CompareTorrentByQueuePosition{});
    for (auto* tor : torrents)
    {
        tr_torrentSetQueuePosition(tor, 0);
    }
}

void tr_torrentsQueueMoveUp(tr_torrent* const* torrents_in, size_t n)
{
    auto torrents = std::vector<tr_torrent*>(torrents_in, torrents_in + n);
    std::sort(std::begin(torrents), std::end(torrents), CompareTorrentByQueuePosition{});
    for (auto* tor : torrents)
    {
        tr_torrentSetQueuePosition(tor, tor->queuePosition - 1);
    }
}

void tr_torrentsQueueMoveDown(tr_torrent* const* torrents_in, size_t n)
{
    auto torrents = std::vector<tr_torrent*>(torrents_in, torrents_in + n);
    std::sort(std::rbegin(torrents), std::rend(torrents), CompareTorrentByQueuePosition{});
    for (auto* tor : torrents)
    {
        tr_torrentSetQueuePosition(tor, tor->queuePosition + 1);
    }
}

void tr_torrentsQueueMoveBottom(tr_torrent* const* torrents_in, size_t n)
{
    auto torrents = std::vector<tr_torrent*>(torrents_in, torrents_in + n);
    std::sort(std::begin(torrents), std::end(torrents), CompareTorrentByQueuePosition{});
    for (auto* tor : torrents)
    {
        tr_torrentSetQueuePosition(tor, INT_MAX);
    }
}

static void torrentSetQueued(tr_torrent* tor, bool queued)
{
    TR_ASSERT(tr_isTorrent(tor));

    if (tor->isQueued() != queued)
    {
        tor->is_queued = queued;
        tor->markChanged();
        tor->setDirty();
    }
}

void tr_torrentSetQueueStartCallback(tr_torrent* torrent, void (*callback)(tr_torrent*, void*), void* user_data)
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
        strchr(newname, TR_PATH_DELIMITER) == nullptr;
}

static auto renameFindAffectedFiles(tr_torrent const* tor, std::string_view oldpath)
{
    auto indices = std::vector<tr_file_index_t>{};
    auto oldpath_as_dir = tr_strvJoin(oldpath, "/"sv);
    auto const n_files = tor->fileCount();

    for (tr_file_index_t i = 0; i < n_files; ++i)
    {
        auto const& name = tor->fileSubpath(i);
        if (name == oldpath || tr_strvStartsWith(name, oldpath_as_dir))
        {
            indices.push_back(i);
        }
    }

    return indices;
}

static int renamePath(tr_torrent* tor, char const* oldpath, char const* newname)
{
    int err = 0;

    auto const base = tor->isDone() || std::empty(tor->incompleteDir()) ? tor->downloadDir().sv() : tor->incompleteDir().sv();

    auto src = tr_strvPath(base, oldpath);

    if (!tr_sys_path_exists(src.c_str(), nullptr)) /* check for it as a partial */
    {
        src += ".part"sv;
    }

    if (tr_sys_path_exists(src.c_str(), nullptr))
    {
        char* const parent = tr_sys_path_dirname(src.c_str(), nullptr);
        auto const tgt = tr_strvEndsWith(src, ".part"sv) ? tr_strvJoin(parent, TR_PATH_DELIMITER_STR, newname, ".part"sv) :
                                                           tr_strvPath(parent, newname);

        auto tmp = errno;
        bool const tgt_exists = tr_sys_path_exists(tgt.c_str(), nullptr);
        errno = tmp;

        if (!tgt_exists)
        {
            tr_error* error = nullptr;

            tmp = errno;

            if (!tr_sys_path_rename(src.c_str(), tgt.c_str(), &error))
            {
                err = error->code;
                tr_error_free(error);
            }

            errno = tmp;
        }

        tr_free(parent);
    }

    return err;
}

static void renameTorrentFileString(tr_torrent* tor, char const* oldpath, char const* newname, tr_file_index_t file_index)
{
    auto name = std::string{};
    auto const subpath = std::string_view{ tor->fileSubpath(file_index) };
    auto const oldpath_len = strlen(oldpath);

    if (strchr(oldpath, TR_PATH_DELIMITER) == nullptr)
    {
        if (oldpath_len >= std::size(subpath))
        {
            name = tr_strvPath(newname);
        }
        else
        {
            name = tr_strvPath(newname, subpath.substr(oldpath_len + 1));
        }
    }
    else
    {
        char* tmp = tr_sys_path_dirname(oldpath, nullptr);

        if (tmp == nullptr)
        {
            return;
        }

        if (oldpath_len >= std::size(subpath))
        {
            name = tr_strvPath(tmp, newname);
        }
        else
        {
            name = tr_strvPath(tmp, newname, subpath.substr(oldpath_len + 1));
        }

        tr_free(tmp);
    }

    if (subpath != name)
    {
        tor->setFileSubpath(file_index, name);
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
    auto* data = static_cast<struct rename_data*>(vdata);
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
        auto const file_indices = renameFindAffectedFiles(tor, oldpath);
        if (std::empty(file_indices))
        {
            error = EINVAL;
        }
        else
        {
            error = renamePath(tor, oldpath, newname);

            if (error == 0)
            {
                /* update tr_info.files */
                for (auto const& file_index : file_indices)
                {
                    renameTorrentFileString(tor, oldpath, newname, file_index);
                }

                /* update tr_info.name if user changed the toplevel */
                if (std::size(file_indices) == tor->fileCount() && strchr(oldpath, '/') == nullptr)
                {
                    tor->setName(newname);
                }

                tor->markEdited();
                tor->setDirty();
            }
        }
    }

    /***
    ****
    ***/

    tor->markChanged();

    /* callback */
    if (data->callback != nullptr)
    {
        (*data->callback)(tor, data->oldpath, data->newname, error, data->callback_user_data);
    }

    /* cleanup */
    tr_free(data->oldpath);
    tr_free(data->newname);
    tr_free(data);
}

void tr_torrent::renamePath(
    std::string_view oldpath,
    std::string_view newname,
    tr_torrent_rename_done_func callback,
    void* callback_user_data)
{
    auto* const data = tr_new0(struct rename_data, 1);
    data->tor = this;
    data->oldpath = tr_strvDup(oldpath);
    data->newname = tr_strvDup(newname);
    data->callback = callback;
    data->callback_user_data = callback_user_data;

    tr_runInEventThread(this->session, torrentRenamePath, data);
}

void tr_torrentRenamePath(
    tr_torrent* tor,
    char const* oldpath,
    char const* newname,
    tr_torrent_rename_done_func callback,
    void* callback_user_data)
{
    oldpath = oldpath != nullptr ? oldpath : "";
    newname = newname != nullptr ? newname : "";

    tor->renamePath(oldpath, newname, callback, callback_user_data);
}

void tr_torrentSetFilePriorities(
    tr_torrent* tor,
    tr_file_index_t const* files,
    tr_file_index_t fileCount,
    tr_priority_t priority)
{
    tor->setFilePriorities(files, fileCount, priority);
}

bool tr_torrentHasMetadata(tr_torrent const* tor)
{
    return tor->hasMetadata();
}

void tr_torrent::markEdited()
{
    this->editDate = tr_time();
}

void tr_torrent::markChanged()
{
    this->anyDate = tr_time();
}

void tr_torrent::setDateActive(time_t t)
{
    this->activityDate = t;
    this->anyDate = std::max(this->anyDate, this->activityDate);
}

void tr_torrent::setBlocks(tr_bitfield blocks)
{
    this->completion.setBlocks(std::move(blocks));
}

[[nodiscard]] bool tr_torrent::ensurePieceIsChecked(tr_piece_index_t piece)
{
    TR_ASSERT(piece < this->pieceCount());

    if (checked_pieces_.test(piece))
    {
        return true;
    }

    bool const checked = checkPiece(piece);
    this->markChanged();
    this->setDirty();

    checked_pieces_.set(piece, checked);
    return checked;
}

void tr_torrent::initCheckedPieces(tr_bitfield const& checked, time_t const* mtimes /*fileCount()*/)
{
    TR_ASSERT(std::size(checked) == this->pieceCount());
    checked_pieces_ = checked;

    auto const n = this->fileCount();
    this->file_mtimes_.resize(n);

    auto filename = std::string{};
    for (size_t i = 0; i < n; ++i)
    {
        auto const found = this->findFile(filename, i);
        auto const mtime = found ? found->last_modified_at : 0;

        this->file_mtimes_[i] = mtime;

        // if a file has changed, mark its pieces as unchecked
        if (mtime == 0 || mtime != mtimes[i])
        {
            auto const [begin, end] = piecesInFile(i);
            checked_pieces_.unsetSpan(begin, end);
        }
    }
}
