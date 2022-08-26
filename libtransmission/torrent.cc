// This file Copyright © 2009-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <cerrno> // EINVAL
#include <climits> /* INT_MAX */
#include <csignal> /* signal() */
#include <ctime>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <sys/wait.h> /* wait() */
#include <unistd.h> /* fork(), execvp(), _exit() */
#else
#include <windows.h> /* CreateProcess(), GetLastError() */
#endif

#include <event2/util.h> /* evutil_vsnprintf() */

#include <fmt/chrono.h>
#include <fmt/core.h>

#include "transmission.h"

#include "announcer.h"
#include "bandwidth.h"
#include "completion.h"
#include "crypto-utils.h" /* for tr_sha1 */
#include "error.h"
#include "file.h"
#include "inout.h" /* tr_ioTestPiece() */
#include "log.h"
#include "magnet-metainfo.h"
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
#include "verify.h"
#include "version.h"
#include "web-utils.h"

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

tr_torrent_id_t tr_torrentId(tr_torrent const* tor)
{
    return tor != nullptr ? tor->id() : -1;
}

tr_torrent* tr_torrentFindFromId(tr_session* session, int id)
{
    return session->torrents().get(id);
}

tr_torrent* tr_torrentFindFromMetainfo(tr_session* session, tr_torrent_metainfo const* metainfo)
{
    if (session == nullptr || metainfo == nullptr)
    {
        return nullptr;
    }

    return session->torrents().get(metainfo->infoHash());
}

tr_torrent* tr_torrentFindFromMagnetLink(tr_session* session, char const* magnet_link)
{
    return magnet_link == nullptr ? nullptr : session->torrents().get(magnet_link);
}

tr_torrent* tr_torrentFindFromObfuscatedHash(tr_session* session, tr_sha1_digest_t const& obfuscated_hash)
{
    for (auto* const tor : session->torrents())
    {
        if (tor->obfuscated_hash == obfuscated_hash)
        {
            return tor;
        }
    }

    return nullptr;
}

bool tr_torrentSetMetainfoFromFile(tr_torrent* tor, tr_torrent_metainfo* metainfo, char const* filename)
{
    if (tr_torrentHasMetadata(tor))
    {
        return false;
    }

    tr_error* error = nullptr;
    tr_torrentUseMetainfoFromFile(tor, metainfo, filename, &error);

    if (error != nullptr)
    {
        tor->setLocalError(fmt::format(
            _("Couldn't use metaInfo from '{path}' for '{magnet}': {error} ({error_code})"),
            fmt::arg("path", filename),
            fmt::arg("magnet", tor->magnet()),
            fmt::arg("error", error->message),
            fmt::arg("error_code", error->code)));
        tr_error_clear(&error);
        return false;
    }

    return true;
}

bool tr_torrent::isPieceTransferAllowed(tr_direction direction) const
{
    TR_ASSERT(tr_isDirection(direction));

    if (tr_torrentUsesSpeedLimit(this, direction) && this->speedLimitBps(direction) <= 0)
    {
        return false;
    }

    if (tr_torrentUsesSessionLimits(this))
    {
        if (auto const limit = session->activeSpeedLimitBps(direction); limit && *limit == 0U)
        {
            return false;
        }
    }

    return true;
}

/***
****
***/

static void tr_torrentUnsetPeerId(tr_torrent* tor)
{
    // triggers a rebuild next time tr_torrentGetPeerId() is called
    tor->peer_id_ = {};
}

static int peerIdTTL(tr_torrent const* tor)
{
    auto const ctime = tor->peer_id_creation_time_;
    return ctime == 0 ? 0 : (int)difftime(ctime + tor->session->peerIdTTLHours() * 3600, tr_time());
}

tr_peer_id_t const& tr_torrentGetPeerId(tr_torrent* tor)
{
    bool const needs_new_peer_id = tor->peer_id_[0] == '\0' || // doesn't have one
        (tor->isPublic() && (peerIdTTL(tor) <= 0)); // has one but it's expired

    if (needs_new_peer_id)
    {
        tor->peer_id_ = tr_peerIdInit();
        tor->peer_id_creation_time_ = tr_time();
    }

    return tor->peer_id_;
}

/***
****  PER-TORRENT UL / DL SPEEDS
***/

void tr_torrent::setSpeedLimitBps(tr_direction dir, unsigned int Bps)
{
    TR_ASSERT(tr_isDirection(dir));

    if (this->bandwidth_.setDesiredSpeedBytesPerSecond(dir, Bps))
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

    return this->bandwidth_.getDesiredSpeedBytesPerSecond(dir);
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

    if (tor->bandwidth_.setLimited(dir, do_use))
    {
        tor->setDirty();
    }
}

bool tr_torrentUsesSpeedLimit(tr_torrent const* tor, tr_direction dir)
{
    TR_ASSERT(tr_isTorrent(tor));

    return tor->bandwidth_.isLimited(dir);
}

void tr_torrentUseSessionLimits(tr_torrent* tor, bool doUse)
{
    TR_ASSERT(tr_isTorrent(tor));

    if (tor->bandwidth_.honorParentLimits(TR_UP, doUse) || tor->bandwidth_.honorParentLimits(TR_DOWN, doUse))
    {
        tor->setDirty();
    }
}

bool tr_torrentUsesSessionLimits(tr_torrent const* tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    return tor->bandwidth_.areParentLimitsHonored(TR_UP);
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
    auto is_limited = bool{};

    TR_ASSERT(tr_isTorrent(tor));

    switch (tr_torrentGetRatioMode(tor))
    {
    case TR_RATIOLIMIT_SINGLE:
        is_limited = true;

        if (ratio != nullptr)
        {
            *ratio = tr_torrentGetRatioLimit(tor);
        }

        break;

    case TR_RATIOLIMIT_GLOBAL:
        is_limited = tor->session->isRatioLimited();

        if (is_limited && ratio != nullptr)
        {
            *ratio = tor->session->desiredRatio();
        }

        break;

    default: /* TR_RATIOLIMIT_UNLIMITED */
        is_limited = false;
        break;
    }

    return is_limited;
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
        auto const baseline = tor->sizeWhenDone();
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
        isLimited = tor->session->isIdleLimited();

        if (isLimited && idleMinutes != nullptr)
        {
            *idleMinutes = tor->session->idleLimitMinutes();
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

static void torrentCallScript(tr_torrent const* tor, std::string const& script);

static void callScriptIfEnabled(tr_torrent const* tor, TrScript type)
{
    auto const* session = tor->session;

    if (tr_sessionIsScriptEnabled(session, type))
    {
        torrentCallScript(tor, session->script(type));
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
        tr_logAddInfoTor(tor, _("Seed ratio reached; pausing torrent"));
        tor->isStopping = true;
        tor->session->onRatioLimitHit(tor);
    }
    /* if we're seeding and reach our inactivity limit, stop the torrent */
    else if (tr_torrentIsSeedIdleLimitDone(tor))
    {
        tr_logAddInfoTor(tor, _("Seeding idle limit reached; pausing torrent"));

        tor->isStopping = true;
        tor->finishedSeedingByIdle = true;
        tor->session->onIdleLimitHit(tor);
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
        tr_logAddTraceTor(tor, fmt::format("Got {} peers from tracker", std::size(event->pex)));
        tr_peerMgrAddPex(tor, TR_PEER_FROM_TRACKER, std::data(event->pex), std::size(event->pex));
        break;

    case TR_TRACKER_COUNTS:
        if (tor->isPrivate() && (event->leechers == 0))
        {
            tr_peerMgrSetSwarmIsAllSeeds(tor);
        }

        break;

    case TR_TRACKER_WARNING:
        tr_logAddWarnTor(tor, fmt::format(_("Tracker warning: '{warning}'"), fmt::arg("warning", event->text)));
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

struct torrent_start_opts
{
    bool bypass_queue = false;

    // true or false if we know whether or not local data exists,
    // or unset if we don't know and need to check for ourselves
    std::optional<bool> has_local_data;
};

static void torrentStart(tr_torrent* tor, torrent_start_opts opts);

static void torrentInitFromInfoDict(tr_torrent* tor)
{
    tor->completion = tr_completion{ tor, &tor->blockInfo() };
    tor->obfuscated_hash = tr_sha1::digest("req2"sv, tor->infoHash());
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
    session->onMetadataCompleted(this);
    this->setDirty();
}

static size_t buildSearchPathArray(tr_torrent const* tor, std::string_view* paths)
{
    auto* walk = paths;

    if (auto const& path = tor->downloadDir(); !std::empty(path))
    {
        *walk++ = path.sv();
    }

    if (auto const& path = tor->incompleteDir(); !std::empty(path))
    {
        *walk++ = path.sv();
    }

    return walk - paths;
}

std::optional<tr_torrent_files::FoundFile> tr_torrent::findFile(tr_file_index_t file_index) const
{
    auto paths = std::array<std::string_view, 4>{};
    auto const n_paths = buildSearchPathArray(this, std::data(paths));
    return metainfo_.files().find(file_index, std::data(paths), n_paths);
}

bool tr_torrent::hasAnyLocalData() const
{
    auto paths = std::array<std::string_view, 4>{};
    auto const n_paths = buildSearchPathArray(this, std::data(paths));
    return metainfo_.files().hasAnyLocalData(std::data(paths), n_paths);
}

static bool setLocalErrorIfFilesDisappeared(tr_torrent* tor, std::optional<bool> has_local_data = {})
{
    auto const has = has_local_data ? *has_local_data : tor->hasAnyLocalData();
    bool const files_disappeared = tor->hasTotal() > 0 && !has;

    if (files_disappeared)
    {
        tr_logAddTraceTor(tor, "[LAZY] uh oh, the files disappeared");
        tor->setLocalError(_(
            "No data found! Ensure your drives are connected or use \"Set Location\". To re-download, remove the torrent and re-add it."));
    }

    return files_disappeared;
}

/**
 * Sniff out newly-added seeds so that they can skip the verify step
 */
static bool isNewTorrentASeed(tr_torrent* tor)
{
    if (!tor->hasMetainfo())
    {
        return false;
    }

    for (tr_file_index_t i = 0, n = tor->fileCount(); i < n; ++i)
    {
        // it's not a new seed if a file is missing
        auto const found = tor->findFile(i);
        if (!found)
        {
            return false;
        }

        // it's not a new seed if a file is partial
        if (tr_strvEndsWith(found->filename(), tr_torrent_files::PartialFileSuffix))
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

static void torrentInit(tr_torrent* tor, tr_ctor const* ctor)
{
    tr_session* session = tr_ctorGetSession(ctor);
    TR_ASSERT(session != nullptr);
    tor->session = session;

    auto const lock = tor->unique_lock();

    tor->queuePosition = std::size(session->torrents());

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
    tor->bandwidth_.setParent(&session->top_bandwidth_);
    tor->bandwidth_.setPriority(tr_ctorGetBandwidthPriority(ctor));
    tor->error = TR_STAT_OK;
    tor->finishedSeedingByIdle = false;

    auto const& labels = tr_ctorGetLabels(ctor);
    tor->setLabels(labels);

    tor->unique_id_ = session->torrents().add(tor);

    tr_peerMgrAddTorrent(session->peerMgr, tor);

    TR_ASSERT(tor->downloadedCur == 0);
    TR_ASSERT(tor->uploadedCur == 0);

    auto const now = tr_time();
    tor->addedDate = now; // this is a default that will be overwritten by the resume file
    tor->anyDate = now;

    tr_resume::fields_t loaded = {};
    if (tor->hasMetainfo())
    {
        // tr_resume::load() calls a lot of tr_torrentSetFoo() methods
        // that set things as dirty, but... these settings being loaded are
        // the same ones that would be saved back again, so don't let them
        // affect the 'is dirty' flag.
        auto const was_dirty = tor->isDirty;

        bool resume_file_was_migrated = false;
        loaded = tr_resume::load(tor, tr_resume::All, ctor, &resume_file_was_migrated);
        tor->isDirty = was_dirty;

        if (resume_file_was_migrated)
        {
            tr_torrent_metainfo::migrateFile(session->torrentDir(), tor->name(), tor->infoHashString(), ".torrent"sv);
        }
    }

    tor->completeness = tor->completion.status();

    tr_ctorInitTorrentPriorities(ctor, tor);
    tr_ctorInitTorrentWanted(ctor, tor);

    tor->refreshCurrentDir();

    bool const doStart = tor->isRunning;
    tor->isRunning = false;

    if ((loaded & tr_resume::Speedlimit) == 0)
    {
        tr_torrentUseSpeedLimit(tor, TR_UP, false);
        tor->setSpeedLimitBps(TR_UP, tor->session->speedLimitBps(TR_UP));
        tr_torrentUseSpeedLimit(tor, TR_DOWN, false);
        tor->setSpeedLimitBps(TR_DOWN, tor->session->speedLimitBps(TR_DOWN));
        tr_torrentUseSessionLimits(tor, true);
    }

    if ((loaded & tr_resume::Ratiolimit) == 0)
    {
        tr_torrentSetRatioMode(tor, TR_RATIOLIMIT_GLOBAL);
        tr_torrentSetRatioLimit(tor, tor->session->desiredRatio());
    }

    if ((loaded & tr_resume::Idlelimit) == 0)
    {
        tr_torrentSetIdleMode(tor, TR_IDLELIMIT_GLOBAL);
        tr_torrentSetIdleLimit(tor, tor->session->idleLimitMinutes());
    }

    auto has_local_data = std::optional<bool>{};
    if ((loaded & tr_resume::Progress) != 0)
    {
        // if tr_resume::load() loaded progress info, then initCheckedPieces()
        // has already looked for local data on the filesystem
        has_local_data = std::any_of(
            std::begin(tor->file_mtimes_),
            std::end(tor->file_mtimes_),
            [](auto mtime) { return mtime > 0; });
    }

    auto const filename = tor->hasMetainfo() ? tor->torrentFile() : tor->magnetFile();

    // if we don't have a local .torrent or .magnet file already,
    // assume the torrent is new
    bool const is_new_torrent = !tr_sys_path_exists(filename);

    if (is_new_torrent)
    {
        tr_error* error = nullptr;

        if (tor->hasMetainfo()) // torrent file
        {
            tr_ctorSaveContents(ctor, filename, &error);
        }
        else // magnet link
        {
            auto const magnet_link = tor->magnet();
            tr_saveFile(filename, magnet_link, &error);
        }

        if (error != nullptr)
        {
            tor->setLocalError(fmt::format(
                _("Couldn't save '{path}': {error} ({error_code})"),
                fmt::arg("path", filename),
                fmt::arg("error", error->message),
                fmt::arg("error_code", error->code)));
            tr_error_clear(&error);
        }
    }

    tor->torrent_announcer = tr_announcerAddTorrent(tor, onTrackerResponse, nullptr);

    if (is_new_torrent)
    {
        if (tor->hasMetainfo())
        {
            callScriptIfEnabled(tor, TR_SCRIPT_ON_TORRENT_ADDED);
        }

        if (!tor->hasMetainfo() && !doStart)
        {
            auto opts = torrent_start_opts{};
            opts.bypass_queue = true;
            opts.has_local_data = has_local_data;
            torrentStart(tor, opts);
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
        // if checked_pieces_ got populated from the loading the resume
        // file above, then torrentStart doesn't need to check again
        auto opts = torrent_start_opts{};
        opts.has_local_data = has_local_data;
        torrentStart(tor, opts);
    }
    else
    {
        setLocalErrorIfFilesDisappeared(tor, has_local_data);
    }
}

tr_torrent* tr_torrentNew(tr_ctor* ctor, tr_torrent** setme_duplicate_of)
{
    TR_ASSERT(ctor != nullptr);
    auto* const session = tr_ctorGetSession(ctor);
    TR_ASSERT(session != nullptr);

    // is the metainfo valid?
    auto metainfo = tr_ctorStealMetainfo(ctor);
    if (std::empty(metainfo.infoHashString()))
    {
        return nullptr;
    }

    // is it a duplicate?
    if (auto* const duplicate_of = session->torrents().get(metainfo.infoHash()); duplicate_of != nullptr)
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
        tor->setDownloadDir(path);
    }
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

static inline void tr_torrentManualUpdateImpl(tr_torrent* const tor)
{
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

    this->verify_state_ = state;
    this->verify_progress_ = {};
    this->markChanged();
}

tr_torrent_activity tr_torrentGetActivity(tr_torrent const* tor)
{
    tr_torrent_activity ret = TR_STATUS_STOPPED;

    bool const is_seed = tor->isDone();

    if (tor->verifyState() == TR_VERIFY_NOW)
    {
        ret = TR_STATUS_CHECK;
    }
    else if (tor->verifyState() == TR_VERIFY_WAIT)
    {
        ret = TR_STATUS_CHECK_WAIT;
    }
    else if (tor->isRunning)
    {
        ret = is_seed ? TR_STATUS_SEED : TR_STATUS_DOWNLOAD;
    }
    else if (tor->isQueued())
    {
        if (is_seed && tor->session->queueEnabled(TR_UP))
        {
            ret = TR_STATUS_SEED_WAIT;
        }
        else if (!is_seed && tor->session->queueEnabled(TR_DOWN))
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
    return tor->session->queueStalledEnabled() && idle_secs > tor->session->queueStalledMinutes() * 60;
}

tr_stat const* tr_torrentStat(tr_torrent* tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    uint64_t const now = tr_time_msec();

    auto swarm_stats = tr_swarm_stats{};

    tor->lastStatTime = tr_time();

    if (tor->swarm != nullptr)
    {
        swarm_stats = tr_swarmGetStats(tor->swarm);
    }

    tr_stat* const s = &tor->stats;
    s->id = tor->id();
    s->activity = tr_torrentGetActivity(tor);
    s->error = tor->error;
    s->queuePosition = tor->queuePosition;
    s->idleSecs = torrentGetIdleSecs(tor, s->activity);
    s->isStalled = tr_torrentIsStalled(tor, s->idleSecs);
    s->errorString = tor->error_string.c_str();

    s->peersConnected = swarm_stats.peer_count;
    s->peersSendingToUs = swarm_stats.active_peer_count[TR_DOWN];
    s->peersGettingFromUs = swarm_stats.active_peer_count[TR_UP];
    s->webseedsSendingToUs = swarm_stats.active_webseed_count;

    for (int i = 0; i < TR_PEER_FROM__MAX; i++)
    {
        s->peersFrom[i] = swarm_stats.peer_from_count[i];
    }

    auto const pieceUploadSpeed_Bps = tor->bandwidth_.getPieceSpeedBytesPerSecond(now, TR_UP);
    s->pieceUploadSpeed_KBps = tr_toSpeedKBps(pieceUploadSpeed_Bps);
    auto const pieceDownloadSpeed_Bps = tor->bandwidth_.getPieceSpeedBytesPerSecond(now, TR_DOWN);
    s->pieceDownloadSpeed_KBps = tr_toSpeedKBps(pieceDownloadSpeed_Bps);

    s->percentComplete = tor->completion.percentComplete();
    s->metadataPercentComplete = tr_torrentGetMetadataPercent(tor);

    s->percentDone = tor->completion.percentDone();
    s->leftUntilDone = tor->completion.leftUntilDone();
    s->sizeWhenDone = tor->completion.sizeWhenDone();

    auto const verify_progress = tor->verifyProgress();
    s->recheckProgress = verify_progress ? *verify_progress : 0.0F;
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

    s->ratio = tr_getRatio(s->uploadedEver, tor->sizeWhenDone());

    auto seedRatioBytesLeft = uint64_t{};
    auto seedRatioBytesGoal = uint64_t{};
    bool const seedRatioApplies = tr_torrentGetSeedRatioBytes(tor, &seedRatioBytesLeft, &seedRatioBytesGoal);

    switch (s->activity)
    {
    /* etaSpeed exists because if we use the piece speed directly,
     * brief fluctuations cause the ETA to jump all over the place.
     * so, etaXLSpeed is a smoothed-out version of the piece speed
     * to dampen the effect of fluctuations */
    case TR_STATUS_DOWNLOAD:
        if (tor->etaSpeedCalculatedAt + 800 < now)
        {
            tor->etaSpeed_Bps = tor->etaSpeedCalculatedAt + 4000 < now ?
                pieceDownloadSpeed_Bps : /* if no recent previous speed, no need to smooth */
                (tor->etaSpeed_Bps * 4.0 + pieceDownloadSpeed_Bps) / 5.0; /* smooth across 5 readings */
            tor->etaSpeedCalculatedAt = now;
        }

        if (s->leftUntilDone > s->desiredAvailable && tor->webseedCount() < 1)
        {
            s->eta = TR_ETA_NOT_AVAIL;
        }
        else if (tor->etaSpeed_Bps == 0)
        {
            s->eta = TR_ETA_UNKNOWN;
        }
        else
        {
            s->eta = s->leftUntilDone / tor->etaSpeed_Bps;
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
            if (tor->etaSpeedCalculatedAt + 800 < now)
            {
                tor->etaSpeed_Bps = tor->etaSpeedCalculatedAt + 4000 < now ?
                    pieceUploadSpeed_Bps : /* if no recent previous speed, no need to smooth */
                    (tor->etaSpeed_Bps * 4.0 + pieceUploadSpeed_Bps) / 5.0; /* smooth across 5 readings */
                tor->etaSpeedCalculatedAt = now;
            }

            if (tor->etaSpeed_Bps == 0)
            {
                s->eta = TR_ETA_UNKNOWN;
            }
            else
            {
                s->eta = seedRatioBytesLeft / tor->etaSpeed_Bps;
            }
        }

        {
            auto seedIdleMinutes = uint16_t{};
            s->etaIdle = tor->etaSpeed_Bps < 1 && tr_torrentGetSeedIdle(tor, &seedIdleMinutes) ?
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

tr_file_view tr_torrentFile(tr_torrent const* tor, tr_file_index_t file)
{
    TR_ASSERT(tr_isTorrent(tor));

    auto const& subpath = tor->fileSubpath(file);
    auto const priority = tor->file_priorities_.filePriority(file);
    auto const wanted = tor->files_wanted_.fileWanted(file);
    auto const length = tor->fileSize(file);

    if (tor->completeness == TR_SEED || length == 0)
    {
        return { subpath.c_str(), length, length, 1.0, priority, wanted };
    }

    auto const have = tor->completion.countHasBytesInSpan(tor->fpm_.byteSpan(file));
    return { subpath.c_str(), have, length, have >= length ? 1.0 : have / double(length), priority, wanted };
}

size_t tr_torrentFileCount(tr_torrent const* torrent)
{
    TR_ASSERT(tr_isTorrent(torrent));

    return torrent->fileCount();
}

tr_webseed_view tr_torrentWebseed(tr_torrent const* tor, size_t nth)
{
    return tr_peerMgrWebseed(tor, nth);
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

std::string tr_torrentFilename(tr_torrent const* tor)
{
    return std::string{ tor->torrentFile() };
}

size_t tr_torrentFilenameToBuf(tr_torrent const* tor, char* buf, size_t buflen)
{
    return tr_strvToBuf(tr_torrentFilename(tor), buf, buflen);
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
    delete[] peers;
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

    session->torrents().remove(tor, tr_time());

    if (!session->isClosing())
    {
        // "so you die, captain, and we all move up in rank."
        // resequence the queue positions
        for (auto* t : session->torrents())
        {
            if (t->queuePosition > tor->queuePosition)
            {
                t->queuePosition--;
                t->markChanged();
            }
        }

        TR_ASSERT(queueIsSequenced(session));
    }

    delete tor;
}

/**
***  Start/Stop Callback
**/

static void torrentSetQueued(tr_torrent* tor, bool queued);

static void torrentStartImpl(tr_torrent* const tor)
{
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

static bool torrentShouldQueue(tr_torrent const* const tor)
{
    tr_direction const dir = tor->queueDirection();

    return tor->session->countQueueFreeSlots(dir) == 0;
}

static void torrentStart(tr_torrent* tor, torrent_start_opts opts)
{
    switch (tr_torrentGetActivity(tor))
    {
    case TR_STATUS_SEED:
    case TR_STATUS_DOWNLOAD:
        return; /* already started */

    case TR_STATUS_SEED_WAIT:
    case TR_STATUS_DOWNLOAD_WAIT:
        if (!opts.bypass_queue)
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
        if (!opts.bypass_queue && torrentShouldQueue(tor))
        {
            torrentSetQueued(tor, true);
            return;
        }

        break;
    }

    /* don't allow the torrent to be started if the files disappeared */
    if (setLocalErrorIfFilesDisappeared(tor, opts.has_local_data))
    {
        return;
    }

    /* otherwise, start it now... */
    auto const lock = tor->unique_lock();

    /* allow finished torrents to be resumed */
    if (tr_torrentIsSeedRatioDone(tor))
    {
        tr_logAddInfoTor(tor, _("Restarted manually -- disabling its seed ratio"));
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
        tor->startAfterVerify = true;
        torrentStart(tor, {});
    }
}

void tr_torrentStartNow(tr_torrent* tor)
{
    if (tr_isTorrent(tor))
    {
        auto opts = torrent_start_opts{};
        opts.bypass_queue = true;
        torrentStart(tor, opts);
    }
}

static void onVerifyDoneThreadFunc(tr_torrent* const tor)
{
    TR_ASSERT(tr_amInEventThread(tor->session));

    if (tor->isDeleting)
    {
        return;
    }

    tor->recheckCompleteness();

    if (tor->startAfterVerify)
    {
        tor->startAfterVerify = false;

        auto opts = torrent_start_opts{};
        opts.has_local_data = !tor->checked_pieces_.hasNone();
        torrentStart(tor, opts);
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

static void verifyTorrent(tr_torrent* const tor)
{
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

static void stopTorrent(tr_torrent* const tor)
{
    TR_ASSERT(tr_isTorrent(tor));
    TR_ASSERT(tr_amInEventThread(tor->session));
    auto const lock = tor->unique_lock();

    if (!tor->session->isClosing())
    {
        tr_logAddInfoTor(tor, _("Pausing torrent"));
    }

    tr_verifyRemove(tor);
    tr_peerMgrStopTorrent(tor);
    tr_announcerTorrentStopped(tor);

    tor->session->closeTorrentFiles(tor);

    if (!tor->isDeleting)
    {
        tr_torrentSave(tor);
    }

    torrentSetQueued(tor, false);

    if (tor->magnetVerify)
    {
        tor->magnetVerify = false;
        tr_logAddTraceTor(tor, "Magnet Verify");
        tor->refreshCurrentDir();
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
    tor->setDirty();
    tr_runInEventThread(tor->session, stopTorrent, tor);
}

static void closeTorrent(tr_torrent* const tor)
{
    TR_ASSERT(tr_isTorrent(tor));
    TR_ASSERT(tr_amInEventThread(tor->session));

    if (!tor->session->isClosing())
    {
        tr_logAddInfoTor(tor, _("Removing torrent"));
    }

    tor->magnetVerify = false;
    stopTorrent(tor);

    if (tor->isDeleting)
    {
        tr_torrent_metainfo::removeFile(tor->session->torrentDir(), tor->name(), tor->infoHashString(), ".torrent"sv);
        tr_torrent_metainfo::removeFile(tor->session->torrentDir(), tor->name(), tor->infoHashString(), ".magnet"sv);
        tr_torrent_metainfo::removeFile(tor->session->resumeDir(), tor->name(), tor->infoHashString(), ".resume"sv);
    }

    tor->isRunning = false;
    freeTorrent(tor);
}

void tr_torrentFree(tr_torrent* tor)
{
    if (tr_isTorrent(tor))
    {
        tr_session* session = tor->session;

        TR_ASSERT(session != nullptr);

        auto const lock = tor->unique_lock();

        tr_runInEventThread(session, closeTorrent, tor);
    }
}

static void removeTorrentInEventThread(tr_torrent* tor, bool delete_flag, tr_fileFunc delete_func)
{
    auto const lock = tor->unique_lock();

    if (delete_flag && tor->hasMetainfo())
    {
        // ensure the files are all closed and idle before moving
        tor->session->closeTorrentFiles(tor);
        tr_verifyRemove(tor);

        if (delete_func == nullptr)
        {
            delete_func = tr_sys_path_remove;
        }

        auto const delete_func_wrapper = [&delete_func](char const* filename)
        {
            delete_func(filename, nullptr);
        };
        tor->metainfo_.files().remove(tor->currentDir(), tor->name(), delete_func_wrapper);
    }

    closeTorrent(tor);
}

void tr_torrentRemove(tr_torrent* tor, bool delete_flag, tr_fileFunc delete_func)
{
    TR_ASSERT(tr_isTorrent(tor));

    tor->isDeleting = true;

    tr_runInEventThread(tor->session, removeTorrentInEventThread, tor, delete_flag, delete_func);
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
        return "Done";

    case TR_SEED:
        return "Complete";

    default:
        return "Incomplete";
    }
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

static void torrentCallScript(tr_torrent const* tor, std::string const& script)
{
    if (std::empty(script))
    {
        return;
    }

    auto torrent_dir = tr_pathbuf{ tor->currentDir() };
    tr_sys_path_native_separators(std::data(torrent_dir));

    auto const cmd = std::array<char const*, 2>{ script.c_str(), nullptr };

    auto const id_str = std::to_string(tr_torrentId(tor));
    auto const labels_str = buildLabelsString(tor);
    auto const trackers_str = buildTrackersString(tor);
    auto const bytes_downloaded_str = std::to_string(tor->downloadedCur + tor->downloadedPrev);

    auto const env = std::map<std::string_view, std::string_view>{
        { "TR_APP_VERSION"sv, SHORT_VERSION_STRING },
        { "TR_TIME_LOCALTIME"sv, fmt::format("{:%a %b %d %T %Y%n}", fmt::localtime(tr_time())) },
        { "TR_TORRENT_BYTES_DOWNLOADED"sv, bytes_downloaded_str },
        { "TR_TORRENT_DIR"sv, torrent_dir.c_str() },
        { "TR_TORRENT_HASH"sv, tor->infoHashString() },
        { "TR_TORRENT_ID"sv, id_str },
        { "TR_TORRENT_LABELS"sv, labels_str },
        { "TR_TORRENT_NAME"sv, tr_torrentName(tor) },
        { "TR_TORRENT_TRACKERS"sv, trackers_str },
    };

    tr_logAddInfoTor(tor, fmt::format(_("Calling script '{path}'"), fmt::arg("path", script)));

    tr_error* error = nullptr;

    if (!tr_spawn_async(std::data(cmd), env, TR_IF_WIN32("\\", "/"), &error))
    {
        tr_logAddWarnTor(
            tor,
            fmt::format(
                _("Couldn't call script '{path}': {error} ({error_code})"),
                fmt::arg("path", script),
                fmt::arg("error", error->message),
                fmt::arg("error_code", error->code)));
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
            tr_logAddTraceTor(
                this,
                fmt::format(
                    "State changed from {} to {}",
                    getCompletionString(this->completeness),
                    getCompletionString(completeness)));
        }

        this->completeness = new_completeness;
        this->session->closeTorrentFiles(this);

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
                this->setLocation(this->downloadDir(), true, nullptr, nullptr);
            }
        }

        this->session->onTorrentCompletenessChanged(this, completeness, wasRunning);

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

void tr_torrent::setLabels(std::vector<tr_quark> const& new_labels)
{
    auto const lock = unique_lock();
    this->labels.clear();

    for (auto label : new_labels)
    {
        if (std::find(std::begin(this->labels), std::end(this->labels), label) == std::end(this->labels))
        {
            this->labels.push_back(label);
        }
    }
    this->labels.shrink_to_fit();
    this->setDirty();
}

/***
****
***/

void tr_torrent::setBandwidthGroup(std::string_view group_name) noexcept
{
    group_name = tr_strvStrip(group_name);

    auto const lock = this->unique_lock();

    if (std::empty(group_name))
    {
        this->bandwidth_group_ = tr_interned_string{};
        this->bandwidth_.setParent(&this->session->top_bandwidth_);
    }
    else
    {
        this->bandwidth_group_ = group_name;
        this->bandwidth_.setParent(&this->session->getBandwidthGroup(group_name));
    }

    this->setDirty();
}

/***
****
***/

tr_priority_t tr_torrentGetPriority(tr_torrent const* tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    return tor->bandwidth_.getPriority();
}

void tr_torrentSetPriority(tr_torrent* tor, tr_priority_t priority)
{
    TR_ASSERT(tr_isTorrent(tor));
    TR_ASSERT(tr_isPriority(priority));

    if (tor->bandwidth_.getPriority() != priority)
    {
        tor->bandwidth_.setPriority(priority);

        tor->setDirty();
    }
}

/***
****
***/

void tr_torrentSetPeerLimit(tr_torrent* tor, uint16_t max_connected_peers)
{
    TR_ASSERT(tr_isTorrent(tor));

    if (tor->max_connected_peers != max_connected_peers)
    {
        tor->max_connected_peers = max_connected_peers;

        tor->setDirty();
    }
}

uint16_t tr_torrentGetPeerLimit(tr_torrent const* tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    return tor->max_connected_peers;
}

/***
****
***/

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
    else if (length > tr_block_info::BlockSize)
    {
        err = 4;
    }
    else if (tor->pieceLoc(index, offset, length).byte > tor->totalSize())
    {
        err = 5;
    }

    if (err != 0)
    {
        tr_logAddTraceTor(tor, fmt::format("index {} offset {} length {} err {}", index, offset, length, err));
    }

    return err == 0;
}

// TODO(ckerr) migrate to fpm?
tr_block_span_t tr_torGetFileBlockSpan(tr_torrent const* tor, tr_file_index_t file)
{
    auto const [begin_byte, end_byte] = tor->fpm_.byteSpan(file);

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
    tr_logAddTraceTor(this, fmt::format("[LAZY] tr_torrent.checkPiece tested piece {}, pass=={}", piece, pass));
    return pass;
}

/***
****
***/

bool tr_torrent::setTrackerList(std::string_view text)
{
    auto const lock = this->unique_lock();

    auto announce_list = tr_announce_list();
    if (!announce_list.parse(text))
    {
        return false;
    }

    auto const has_metadata = this->hasMetainfo();
    if (has_metadata && !announce_list.save(torrentFile()))
    {
        return false;
    }

    this->metainfo_.announceList() = announce_list;
    this->markEdited();

    // magnet links
    if (!has_metadata)
    {
        auto const magnet_file = magnetFile();
        auto const magnet_link = this->magnet();
        tr_error* save_error = nullptr;
        if (!tr_saveFile(magnet_file, magnet_link, &save_error))
        {
            this->setLocalError(fmt::format(
                _("Couldn't save '{path}': {error} ({error_code})"),
                fmt::arg("path", magnet_file),
                fmt::arg("error", save_error->message),
                fmt::arg("error_code", save_error->code)));
            tr_error_clear(&save_error);
        }
    }

    /* if we had a tracker-related error on this torrent,
     * and that tracker's been removed,
     * then clear the error */
    if (this->error == TR_STAT_TRACKER_WARNING || this->error == TR_STAT_TRACKER_ERROR)
    {
        auto const error_url = this->error_announce_url;

        if (std::any_of(
                std::begin(this->announceList()),
                std::end(this->announceList()),
                [error_url](auto const& tracker) { return tracker.announce == error_url; }))
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

std::string tr_torrentGetTrackerList(tr_torrent const* tor)
{
    return tor->trackerList();
}

size_t tr_torrentGetTrackerListToBuf(tr_torrent const* tor, char* buf, size_t buflen)
{
    return tr_strvToBuf(tr_torrentGetTrackerList(tor), buf, buflen);
}

/**
***
**/

uint64_t tr_torrentGetBytesLeftToAllocate(tr_torrent const* tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    uint64_t bytes_left = 0;

    for (tr_file_index_t i = 0, n = tor->fileCount(); i < n; ++i)
    {
        if (auto const wanted = tor->files_wanted_.fileWanted(i); !wanted)
        {
            continue;
        }

        auto const length = tor->fileSize(i);
        bytes_left += length;

        auto const found = tor->findFile(i);
        if (found)
        {
            bytes_left -= found->size;
        }
    }

    return bytes_left;
}

///

static void setLocationInEventThread(
    tr_torrent* tor,
    std::string const& path,
    bool move_from_old_path,
    double volatile* setme_progress,
    int volatile* setme_state)
{
    TR_ASSERT(tr_isTorrent(tor));
    TR_ASSERT(tr_amInEventThread(tor->session));

    auto ok = bool{ true };
    if (move_from_old_path)
    {
        if (setme_state != nullptr)
        {
            *setme_state = TR_LOC_MOVING;
        }

        // ensure the files are all closed and idle before moving
        tor->session->closeTorrentFiles(tor);
        tr_verifyRemove(tor);

        tr_error* error = nullptr;
        ok = tor->metainfo_.files().move(tor->currentDir(), path, setme_progress, tor->name(), &error);
        if (error != nullptr)
        {
            tor->setLocalError(fmt::format(
                _("Couldn't move '{old_path}' to '{path}': {error} ({error_code})"),
                fmt::arg("old_path", tor->currentDir()),
                fmt::arg("path", path),
                fmt::arg("error", error->message),
                fmt::arg("error_code", error->code)));
            tr_torrentStop(tor);
            tr_error_clear(&error);
        }
    }

    // tell the torrent where the files are
    if (ok)
    {
        tor->setDownloadDir(path);

        if (move_from_old_path)
        {
            tor->incomplete_dir.clear();
            tor->current_dir = tor->downloadDir();
        }
    }

    if (setme_state != nullptr)
    {
        *setme_state = ok ? TR_LOC_DONE : TR_LOC_ERROR;
    }
}

void tr_torrent::setLocation(
    std::string_view location,
    bool move_from_old_path,
    double volatile* setme_progress,
    int volatile* setme_state)
{
    if (setme_state != nullptr)
    {
        *setme_state = TR_LOC_MOVING;
    }

    tr_runInEventThread(
        this->session,
        setLocationInEventThread,
        this,
        std::string{ location },
        move_from_old_path,
        setme_progress,
        setme_state);
}

void tr_torrentSetLocation(
    tr_torrent* tor,
    char const* location,
    bool move_from_old_path,
    double volatile* setme_progress,
    int volatile* setme_state)
{
    TR_ASSERT(tr_isTorrent(tor));
    TR_ASSERT(!tr_str_is_empty(location));

    tor->setLocation(location, move_from_old_path, setme_progress, setme_state);
}

///

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
    tor->session->closeTorrentFile(tor, i);

    /* now that the file is complete and closed, we can start watching its
     * mtime timestamp for changes to know if we need to reverify pieces */
    tor->file_mtimes_[i] = tr_time();

    /* if the torrent's current filename isn't the same as the one in the
     * metadata -- for example, if it had the ".part" suffix appended to
     * it until now -- then rename it to match the one in the metadata */
    if (auto found = tor->findFile(i); found)
    {
        if (auto const& file_subpath = tor->fileSubpath(i); file_subpath != found->subpath())
        {
            auto const& oldpath = found->filename();
            auto const newpath = tr_pathbuf{ found->base(), '/', file_subpath };
            tr_error* error = nullptr;

            if (!tr_sys_path_rename(oldpath, newpath, &error))
            {
                tr_logAddErrorTor(
                    tor,
                    fmt::format(
                        _("Couldn't move '{old_path}' to '{path}': {error} ({error_code})"),
                        fmt::arg("old_path", oldpath),
                        fmt::arg("path", newpath),
                        fmt::arg("error", error->message),
                        fmt::arg("error_code", error->code)));
                tr_error_free(error);
            }
        }
    }
}

static void tr_torrentPieceCompleted(tr_torrent* tor, tr_piece_index_t piece_index)
{
    tr_peerMgrPieceCompleted(tor, piece_index);

    // if this piece completes any file, invoke the fileCompleted func for it
    auto const span = tor->fpm_.fileSpan(piece_index);
    for (auto file = span.begin; file < span.end; ++file)
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
                tr_logAddDebugTor(tor, fmt::format("Piece {}, which was just downloaded, failed its checksum test", piece));
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
        tr_logAddDebugTor(tor, "we have this block already...");
    }
}

/***
****
***/

std::string tr_torrentFindFile(tr_torrent const* tor, tr_file_index_t file_num)
{
    auto const found = tor->findFile(file_num);
    return std::string{ found ? found->filename().sv() : ""sv };
}

size_t tr_torrentFindFileToBuf(tr_torrent const* tor, tr_file_index_t file_num, char* buf, size_t buflen)
{
    return tr_strvToBuf(tr_torrentFindFile(tor, file_num), buf, buflen);
}

// decide whether we should be looking for files in downloadDir or incompleteDir
void tr_torrent::refreshCurrentDir()
{
    auto dir = tr_interned_string{};

    if (std::empty(incompleteDir()))
    {
        dir = downloadDir();
    }
    else if (!hasMetainfo()) // no files to find
    {
        dir = incompleteDir();
    }
    else
    {
        auto const found = findFile(0);
        dir = found ? tr_interned_string{ found->base() } : incompleteDir();
    }

    TR_ASSERT(!std::empty(dir));
    TR_ASSERT(dir == downloadDir() || dir == incompleteDir());

    current_dir = dir;
}

/***
****
***/

#ifdef TR_ENABLE_ASSERTS

static bool queueIsSequenced(tr_session* session)
{
    auto torrents = session->getAllTorrents();
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

void tr_torrentSetQueuePosition(tr_torrent* tor, int queue_position)
{
    int back = -1;
    int const old_pos = tor->queuePosition;

    if (queue_position < 0)
    {
        queue_position = 0;
    }

    tor->queuePosition = -1;

    for (auto* const walk : tor->session->torrents())
    {
        if ((old_pos < queue_position) && (old_pos <= walk->queuePosition) && (walk->queuePosition <= queue_position))
        {
            walk->queuePosition--;
            walk->markChanged();
        }

        if ((old_pos > queue_position) && (queue_position <= walk->queuePosition) && (walk->queuePosition < old_pos))
        {
            walk->queuePosition++;
            walk->markChanged();
        }

        if (back < walk->queuePosition)
        {
            back = walk->queuePosition;
        }
    }

    tor->queuePosition = std::min(queue_position, back + 1);
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

void tr_torrentsQueueMoveTop(tr_torrent* const* torrents_in, size_t torrent_count)
{
    auto torrents = std::vector<tr_torrent*>(torrents_in, torrents_in + torrent_count);
    std::sort(std::rbegin(torrents), std::rend(torrents), CompareTorrentByQueuePosition{});
    for (auto* tor : torrents)
    {
        tr_torrentSetQueuePosition(tor, 0);
    }
}

void tr_torrentsQueueMoveUp(tr_torrent* const* torrents_in, size_t torrent_count)
{
    auto torrents = std::vector<tr_torrent*>(torrents_in, torrents_in + torrent_count);
    std::sort(std::begin(torrents), std::end(torrents), CompareTorrentByQueuePosition{});
    for (auto* tor : torrents)
    {
        tr_torrentSetQueuePosition(tor, tor->queuePosition - 1);
    }
}

void tr_torrentsQueueMoveDown(tr_torrent* const* torrents_in, size_t torrent_count)
{
    auto torrents = std::vector<tr_torrent*>(torrents_in, torrents_in + torrent_count);
    std::sort(std::rbegin(torrents), std::rend(torrents), CompareTorrentByQueuePosition{});
    for (auto* tor : torrents)
    {
        tr_torrentSetQueuePosition(tor, tor->queuePosition + 1);
    }
}

void tr_torrentsQueueMoveBottom(tr_torrent* const* torrents_in, size_t torrent_count)
{
    auto torrents = std::vector<tr_torrent*>(torrents_in, torrents_in + torrent_count);
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

/***
****
****  RENAME
****
***/

static bool renameArgsAreValid(std::string_view oldpath, std::string_view newname)
{
    return !std::empty(oldpath) && !std::empty(newname) && newname != "."sv && newname != ".."sv &&
        !tr_strvContains(newname, TR_PATH_DELIMITER);
}

static auto renameFindAffectedFiles(tr_torrent const* tor, std::string_view oldpath)
{
    auto indices = std::vector<tr_file_index_t>{};
    auto const oldpath_as_dir = tr_pathbuf{ oldpath, '/' };
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

static int renamePath(tr_torrent* tor, std::string_view oldpath, std::string_view newname)
{
    int err = 0;

    auto const base = tor->isDone() || std::empty(tor->incompleteDir()) ? tor->downloadDir() : tor->incompleteDir();

    auto src = tr_pathbuf{ base, '/', oldpath };

    if (!tr_sys_path_exists(src)) /* check for it as a partial */
    {
        src += tr_torrent_files::PartialFileSuffix;
    }

    if (tr_sys_path_exists(src))
    {
        auto const parent = tr_sys_path_dirname(src);
        auto const tgt = tr_strvEndsWith(src, tr_torrent_files::PartialFileSuffix) ?
            tr_pathbuf{ parent, '/', newname, tr_torrent_files::PartialFileSuffix } :
            tr_pathbuf{ parent, '/', newname };

        auto tmp = errno;
        bool const tgt_exists = tr_sys_path_exists(tgt);
        errno = tmp;

        if (!tgt_exists)
        {
            tr_error* error = nullptr;

            tmp = errno;

            if (!tr_sys_path_rename(src, tgt, &error))
            {
                err = error->code;
                tr_error_free(error);
            }

            errno = tmp;
        }
    }

    return err;
}

static void renameTorrentFileString(
    tr_torrent* tor,
    std::string_view oldpath,
    std::string_view newname,
    tr_file_index_t file_index)
{
    auto name = std::string{};
    auto const subpath = std::string_view{ tor->fileSubpath(file_index) };
    auto const oldpath_len = std::size(oldpath);

    if (!tr_strvContains(oldpath, TR_PATH_DELIMITER))
    {
        if (oldpath_len >= std::size(subpath))
        {
            name = newname;
        }
        else
        {
            name = fmt::format(FMT_STRING("{:s}/{:s}"sv), newname, subpath.substr(oldpath_len + 1));
        }
    }
    else
    {
        auto const tmp = tr_sys_path_dirname(oldpath);

        if (std::empty(tmp))
        {
            return;
        }

        if (oldpath_len >= std::size(subpath))
        {
            name = fmt::format(FMT_STRING("{:s}/{:s}"sv), tmp, newname);
        }
        else
        {
            name = fmt::format(FMT_STRING("{:s}/{:s}/{:s}"sv), tmp, newname, subpath.substr(oldpath_len + 1));
        }
    }

    if (subpath != name)
    {
        tor->setFileSubpath(file_index, name);
    }
}

static void torrentRenamePath(
    tr_torrent* const tor,
    std::string oldpath, // NOLINT performance-unnecessary-value-param
    std::string newname, // NOLINT performance-unnecessary-value-param
    tr_torrent_rename_done_func callback,
    void* const callback_user_data)
{
    TR_ASSERT(tr_isTorrent(tor));

    /***
    ****
    ***/

    int error = 0;

    if (!renameArgsAreValid(oldpath, newname))
    {
        error = EINVAL;
    }
    else if (auto const file_indices = renameFindAffectedFiles(tor, oldpath); std::empty(file_indices))
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
            if (std::size(file_indices) == tor->fileCount() && !tr_strvContains(oldpath, '/'))
            {
                tor->setName(newname);
            }

            tor->markEdited();
            tor->setDirty();
        }
    }

    /***
    ****
    ***/

    tor->markChanged();

    /* callback */
    if (callback != nullptr)
    {
        (*callback)(tor, oldpath.c_str(), newname.c_str(), error, callback_user_data);
    }
}

void tr_torrent::renamePath(
    std::string_view oldpath,
    std::string_view newname,
    tr_torrent_rename_done_func callback,
    void* callback_user_data)
{
    tr_runInEventThread(
        this->session,
        torrentRenamePath,
        this,
        std::string{ oldpath },
        std::string{ newname },
        callback,
        callback_user_data);
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
    return tor->hasMetainfo();
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

    if (isPieceChecked(piece))
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

    for (size_t i = 0; i < n; ++i)
    {
        auto const found = this->findFile(i);
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
