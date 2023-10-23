// This file Copyright © 2009-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <cerrno> // EINVAL
#include <climits> /* INT_MAX */
#include <ctime>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <sys/wait.h> /* wait() */
#include <unistd.h> /* fork(), execvp(), _exit() */
#else
#include <windows.h> /* CreateProcess(), GetLastError() */
#endif

#include <fmt/chrono.h>
#include <fmt/core.h>

#include <small/map.hpp>

#include "libtransmission/transmission.h"

#include "libtransmission/announcer.h"
#include "libtransmission/bandwidth.h"
#include "libtransmission/completion.h"
#include "libtransmission/crypto-utils.h" // for tr_sha1()
#include "libtransmission/error.h"
#include "libtransmission/file.h"
#include "libtransmission/inout.h" // tr_ioTestPiece()
#include "libtransmission/log.h"
#include "libtransmission/magnet-metainfo.h"
#include "libtransmission/peer-common.h"
#include "libtransmission/peer-mgr.h"
#include "libtransmission/resume.h"
#include "libtransmission/session.h"
#include "libtransmission/subprocess.h"
#include "libtransmission/torrent-magnet.h"
#include "libtransmission/torrent-metainfo.h"
#include "libtransmission/torrent.h"
#include "libtransmission/tr-assert.h"
#include "libtransmission/utils.h"
#include "libtransmission/version.h"
#include "libtransmission/web-utils.h"

using namespace std::literals;

// ---

void tr_torrent::Error::set_tracker_warning(tr_interned_string announce_url, std::string_view errmsg)
{
    announce_url_ = announce_url;
    errmsg_.assign(errmsg);
    error_type_ = TR_STAT_TRACKER_WARNING;
}

void tr_torrent::Error::set_tracker_error(tr_interned_string announce_url, std::string_view errmsg)
{
    announce_url_ = announce_url;
    errmsg_.assign(errmsg);
    error_type_ = TR_STAT_TRACKER_ERROR;
}

void tr_torrent::Error::set_local_error(std::string_view errmsg)
{
    announce_url_.clear();
    errmsg_.assign(errmsg);
    error_type_ = TR_STAT_LOCAL_ERROR;
}

void tr_torrent::Error::clear() noexcept
{
    announce_url_.clear();
    errmsg_.clear();
    error_type_ = TR_STAT_OK;
}

void tr_torrent::Error::clear_if_tracker() noexcept
{
    if (error_type_ == TR_STAT_TRACKER_WARNING || error_type_ == TR_STAT_TRACKER_ERROR)
    {
        clear();
    }
}

// ---

char const* tr_torrentName(tr_torrent const* tor)
{
    return tor != nullptr ? tor->name().c_str() : "";
}

uint64_t tr_torrentTotalSize(tr_torrent const* tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    return tor->total_size();
}

tr_torrent_id_t tr_torrentId(tr_torrent const* tor)
{
    return tor != nullptr ? tor->id() : -1;
}

tr_torrent* tr_torrentFindFromId(tr_session* session, tr_torrent_id_t id)
{
    return session->torrents().get(id);
}

tr_torrent* tr_torrentFindFromMetainfo(tr_session* session, tr_torrent_metainfo const* metainfo)
{
    if (session == nullptr || metainfo == nullptr)
    {
        return nullptr;
    }

    return session->torrents().get(metainfo->info_hash());
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

bool tr_torrentSetMetainfoFromFile(tr_torrent* tor, tr_torrent_metainfo const* metainfo, char const* filename)
{
    if (tr_torrentHasMetadata(tor))
    {
        return false;
    }

    tr_error* error = nullptr;
    tr_torrentUseMetainfoFromFile(tor, metainfo, filename, &error);

    if (error != nullptr)
    {
        tor->error().set_local_error(fmt::format(
            _("Couldn't use metainfo from '{path}' for '{magnet}': {error} ({error_code})"),
            fmt::arg("path", filename),
            fmt::arg("magnet", tor->magnet()),
            fmt::arg("error", error->message),
            fmt::arg("error_code", error->code)));
        tr_error_clear(&error);
        return false;
    }

    return true;
}

// ---

namespace
{
constexpr void torrentSetQueued(tr_torrent* tor, bool queued)
{
    if (tor->is_queued_ != queued)
    {
        tor->is_queued_ = queued;
        tor->mark_changed();
        tor->set_dirty();
    }
}

bool setLocalErrorIfFilesDisappeared(tr_torrent* tor, std::optional<bool> has_local_data = {})
{
    auto const has = has_local_data ? *has_local_data : tor->has_any_local_data();
    bool const files_disappeared = tor->has_total() > 0 && !has;

    if (files_disappeared)
    {
        tr_logAddTraceTor(tor, "[LAZY] uh oh, the files disappeared");
        tor->error().set_local_error(_(
            "No data found! Ensure your drives are connected or use \"Set Location\". To re-download, remove the torrent and re-add it."));
    }

    return files_disappeared;
}

/* returns true if the seed ratio applies --
 * it applies if the torrent's a seed AND it has a seed ratio set */
bool tr_torrentGetSeedRatioBytes(tr_torrent const* tor, uint64_t* setme_left, uint64_t* setme_goal)
{
    bool seed_ratio_applies = false;

    TR_ASSERT(tr_isTorrent(tor));

    if (auto const seed_ratio = tor->effective_seed_ratio(); seed_ratio)
    {
        auto const uploaded = tor->uploadedCur + tor->uploadedPrev;
        auto const baseline = tor->size_when_done();
        auto const goal = baseline * *seed_ratio;

        if (setme_left != nullptr)
        {
            *setme_left = goal > uploaded ? goal - uploaded : 0;
        }

        if (setme_goal != nullptr)
        {
            *setme_goal = goal;
        }

        seed_ratio_applies = tor->is_done();
    }

    return seed_ratio_applies;
}

bool tr_torrentIsSeedRatioDone(tr_torrent const* tor)
{
    auto bytes_left = uint64_t{};
    return tr_torrentGetSeedRatioBytes(tor, &bytes_left, nullptr) && bytes_left == 0;
}
} // namespace

// --- PER-TORRENT UL / DL SPEEDS

void tr_torrentSetSpeedLimit_KBps(tr_torrent* tor, tr_direction dir, tr_kilobytes_per_second_t kilo_per_second)
{
    tor->set_speed_limit_bps(dir, tr_toSpeedBytes(kilo_per_second));
}

tr_kilobytes_per_second_t tr_torrentGetSpeedLimit_KBps(tr_torrent const* tor, tr_direction dir)
{
    TR_ASSERT(tr_isTorrent(tor));
    TR_ASSERT(tr_isDirection(dir));

    return tr_toSpeedKBps(tor->speed_limit_bps(dir));
}

void tr_torrentUseSpeedLimit(tr_torrent* tor, tr_direction dir, bool enabled)
{
    TR_ASSERT(tr_isTorrent(tor));
    TR_ASSERT(tr_isDirection(dir));

    tor->use_speed_limit(dir, enabled);
}

bool tr_torrentUsesSpeedLimit(tr_torrent const* tor, tr_direction dir)
{
    TR_ASSERT(tr_isTorrent(tor));

    return tor->uses_speed_limit(dir);
}

void tr_torrentUseSessionLimits(tr_torrent* tor, bool enabled)
{
    TR_ASSERT(tr_isTorrent(tor));

    if (tor->bandwidth_.honor_parent_limits(TR_UP, enabled) || tor->bandwidth_.honor_parent_limits(TR_DOWN, enabled))
    {
        tor->set_dirty();
    }
}

bool tr_torrentUsesSessionLimits(tr_torrent const* tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    return tor->uses_session_limits();
}

// --- Download Ratio

void tr_torrentSetRatioMode(tr_torrent* const tor, tr_ratiolimit mode)
{
    TR_ASSERT(tr_isTorrent(tor));

    tor->set_seed_ratio_mode(mode);
}

tr_ratiolimit tr_torrentGetRatioMode(tr_torrent const* const tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    return tor->seed_ratio_mode();
}

void tr_torrentSetRatioLimit(tr_torrent* const tor, double desired_ratio)
{
    TR_ASSERT(tr_isTorrent(tor));

    tor->set_seed_ratio(desired_ratio);
}

double tr_torrentGetRatioLimit(tr_torrent const* const tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    return tor->seed_ratio();
}

bool tr_torrentGetSeedRatio(tr_torrent const* const tor, double* ratio)
{
    TR_ASSERT(tr_isTorrent(tor));

    auto const val = tor->effective_seed_ratio();

    if (ratio != nullptr && val)
    {
        *ratio = *val;
    }

    return val.has_value();
}

// ---

void tr_torrentSetIdleMode(tr_torrent* const tor, tr_idlelimit mode)
{
    TR_ASSERT(tr_isTorrent(tor));

    tor->set_idle_limit_mode(mode);
}

tr_idlelimit tr_torrentGetIdleMode(tr_torrent const* const tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    return tor->idle_limit_mode();
}

void tr_torrentSetIdleLimit(tr_torrent* const tor, uint16_t idle_minutes)
{
    TR_ASSERT(tr_isTorrent(tor));

    tor->set_idle_limit_minutes(idle_minutes);
}

uint16_t tr_torrentGetIdleLimit(tr_torrent const* const tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    return tor->idle_limit_minutes();
}

namespace
{
namespace script_helpers
{
[[nodiscard]] std::string buildLabelsString(tr_torrent const* tor)
{
    auto buf = std::stringstream{};

    for (auto it = std::begin(tor->labels), end = std::end(tor->labels); it != end;)
    {
        buf << tr_quark_get_string_view(*it);

        if (++it != end)
        {
            buf << ',';
        }
    }

    return buf.str();
}

[[nodiscard]] std::string buildTrackersString(tr_torrent const* tor)
{
    auto buf = std::stringstream{};

    for (size_t i = 0, n = tr_torrentTrackerCount(tor); i < n; ++i)
    {
        buf << tr_torrentTracker(tor, i).host_and_port;

        if (++i < n)
        {
            buf << ',';
        }
    }

    return buf.str();
}

void torrentCallScript(tr_torrent const* tor, std::string const& script)
{
    if (std::empty(script))
    {
        return;
    }

    auto torrent_dir = tr_pathbuf{ tor->current_dir() };
    tr_sys_path_native_separators(std::data(torrent_dir));

    auto const cmd = std::array<char const*, 2>{ script.c_str(), nullptr };

    auto const id_str = std::to_string(tr_torrentId(tor));
    auto const labels_str = buildLabelsString(tor);
    auto const trackers_str = buildTrackersString(tor);
    auto const bytes_downloaded_str = std::to_string(tor->downloadedCur + tor->downloadedPrev);
    auto const localtime_str = fmt::format("{:%a %b %d %T %Y%n}", fmt::localtime(tr_time()));

    auto const env = std::map<std::string_view, std::string_view>{
        { "TR_APP_VERSION"sv, SHORT_VERSION_STRING },
        { "TR_TIME_LOCALTIME"sv, localtime_str },
        { "TR_TORRENT_BYTES_DOWNLOADED"sv, bytes_downloaded_str },
        { "TR_TORRENT_DIR"sv, torrent_dir.c_str() },
        { "TR_TORRENT_HASH"sv, tor->info_hash_string() },
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
} // namespace script_helpers

void callScriptIfEnabled(tr_torrent const* tor, TrScript type)
{
    using namespace script_helpers;

    auto const* session = tor->session;

    if (tr_sessionIsScriptEnabled(session, type))
    {
        torrentCallScript(tor, session->script(type));
    }
}

} // namespace

// ---

namespace
{
namespace seed_limit_helpers
{
bool torrent_is_seed_idle_limit_done(tr_torrent const& tor, time_t now)
{
    auto const secs_left = tor.idle_seconds_left(now);
    return secs_left && *secs_left == 0U;
}
} // namespace seed_limit_helpers
} // namespace

void tr_torrentCheckSeedLimit(tr_torrent* tor)
{
    using namespace seed_limit_helpers;

    TR_ASSERT(tr_isTorrent(tor));

    if (!tor->is_running() || tor->is_stopping_ || !tor->is_done())
    {
        return;
    }

    /* if we're seeding and reach our seed ratio limit, stop the torrent */
    if (tr_torrentIsSeedRatioDone(tor))
    {
        tr_logAddInfoTor(tor, _("Seed ratio reached; pausing torrent"));
        tor->is_stopping_ = true;
        tor->session->onRatioLimitHit(tor);
    }
    /* if we're seeding and reach our inactivity limit, stop the torrent */
    else if (torrent_is_seed_idle_limit_done(*tor, tr_time()))
    {
        tr_logAddInfoTor(tor, _("Seeding idle limit reached; pausing torrent"));

        tor->is_stopping_ = true;
        tor->finished_seeding_by_idle_ = true;
        tor->session->onIdleLimitHit(tor);
    }

    if (tor->is_stopping_)
    {
        callScriptIfEnabled(tor, TR_SCRIPT_ON_TORRENT_DONE_SEEDING);
    }
}

// --- Queue

namespace
{
namespace queue_helpers
{
constexpr struct
{
    constexpr bool operator()(tr_torrent const* a, tr_torrent const* b) const noexcept
    {
        return a->queuePosition < b->queuePosition;
    }
} CompareTorrentByQueuePosition{};

#ifdef TR_ENABLE_ASSERTS
bool queueIsSequenced(tr_session const* session)
{
    auto torrents = session->getAllTorrents();
    std::sort(
        std::begin(torrents),
        std::end(torrents),
        [](auto const* a, auto const* b) { return a->queuePosition < b->queuePosition; });

    /* test them */
    bool is_sequenced = true;

    for (size_t i = 0, n = std::size(torrents); is_sequenced && i < n; ++i)
    {
        is_sequenced = torrents[i]->queuePosition == i;
    }

    return is_sequenced;
}
#endif
} // namespace queue_helpers
} // namespace

size_t tr_torrentGetQueuePosition(tr_torrent const* tor)
{
    return tor->queuePosition;
}

void tr_torrentSetQueuePosition(tr_torrent* tor, size_t queue_position)
{
    using namespace queue_helpers;

    size_t current = 0;
    auto const old_pos = tor->queuePosition;

    tor->queuePosition = static_cast<size_t>(-1);

    for (auto* const walk : tor->session->torrents())
    {
        if ((old_pos < queue_position) && (old_pos <= walk->queuePosition) && (walk->queuePosition <= queue_position))
        {
            walk->queuePosition--;
            walk->mark_changed();
        }

        if ((old_pos > queue_position) && (queue_position <= walk->queuePosition) && (walk->queuePosition < old_pos))
        {
            walk->queuePosition++;
            walk->mark_changed();
        }

        if (current < walk->queuePosition + 1)
        {
            current = walk->queuePosition + 1;
        }
    }

    tor->queuePosition = std::min(queue_position, current);
    tor->mark_changed();

    TR_ASSERT(queueIsSequenced(tor->session));
}

void tr_torrentsQueueMoveTop(tr_torrent* const* torrents_in, size_t torrent_count)
{
    using namespace queue_helpers;

    auto torrents = std::vector<tr_torrent*>(torrents_in, torrents_in + torrent_count);
    std::sort(std::rbegin(torrents), std::rend(torrents), CompareTorrentByQueuePosition);
    for (auto* tor : torrents)
    {
        tr_torrentSetQueuePosition(tor, 0);
    }
}

void tr_torrentsQueueMoveUp(tr_torrent* const* torrents_in, size_t torrent_count)
{
    using namespace queue_helpers;

    auto torrents = std::vector<tr_torrent*>(torrents_in, torrents_in + torrent_count);
    std::sort(std::begin(torrents), std::end(torrents), CompareTorrentByQueuePosition);
    for (auto* tor : torrents)
    {
        if (tor->queuePosition > 0)
        {
            tr_torrentSetQueuePosition(tor, tor->queuePosition - 1);
        }
    }
}

void tr_torrentsQueueMoveDown(tr_torrent* const* torrents_in, size_t torrent_count)
{
    using namespace queue_helpers;

    auto torrents = std::vector<tr_torrent*>(torrents_in, torrents_in + torrent_count);
    std::sort(std::rbegin(torrents), std::rend(torrents), CompareTorrentByQueuePosition);
    for (auto* tor : torrents)
    {
        if (tor->queuePosition < UINT_MAX)
        {
            tr_torrentSetQueuePosition(tor, tor->queuePosition + 1);
        }
    }
}

void tr_torrentsQueueMoveBottom(tr_torrent* const* torrents_in, size_t torrent_count)
{
    using namespace queue_helpers;

    auto torrents = std::vector<tr_torrent*>(torrents_in, torrents_in + torrent_count);
    std::sort(std::begin(torrents), std::end(torrents), CompareTorrentByQueuePosition);
    for (auto* tor : torrents)
    {
        tr_torrentSetQueuePosition(tor, UINT_MAX);
    }
}

// --- Start, Stop

namespace
{
namespace start_stop_helpers
{
bool torrentShouldQueue(tr_torrent const* const tor)
{
    tr_direction const dir = tor->queue_direction();

    return tor->session->count_queue_free_slots(dir) == 0;
}

void torrentResetTransferStats(tr_torrent* tor)
{
    auto const lock = tor->unique_lock();

    tor->downloadedPrev += tor->downloadedCur;
    tor->downloadedCur = 0;
    tor->uploadedPrev += tor->uploadedCur;
    tor->uploadedCur = 0;
    tor->corruptPrev += tor->corruptCur;
    tor->corruptCur = 0;

    tor->set_dirty();
}

void torrentStartImpl(tr_torrent* const tor)
{
    auto const lock = tor->unique_lock();

    TR_ASSERT(tr_isTorrent(tor));

    tor->recheck_completeness();
    torrentSetQueued(tor, false);

    time_t const now = tr_time();

    tor->is_running_ = true;
    tor->completeness = tor->completion.status();
    tor->startDate = now;
    tor->mark_changed();
    tor->error().clear();
    tor->finished_seeding_by_idle_ = false;

    torrentResetTransferStats(tor);
    tor->session->announcer_->startTorrent(tor);
    tor->lpdAnnounceAt = now;
    tor->started_.emit(tor);
}

bool removeTorrentFile(char const* filename, void* /*user_data*/, tr_error** error)
{
    return tr_sys_path_remove(filename, error);
}

void removeTorrentInSessionThread(tr_torrent* tor, bool delete_flag, tr_fileFunc delete_func, void* user_data)
{
    auto const lock = tor->unique_lock();

    if (delete_flag && tor->has_metainfo())
    {
        // ensure the files are all closed and idle before moving
        tor->session->closeTorrentFiles(tor);
        tor->session->verify_remove(tor);

        if (delete_func == nullptr)
        {
            delete_func = removeTorrentFile;
        }

        auto const delete_func_wrapper = [&delete_func, user_data](char const* filename)
        {
            delete_func(filename, user_data, nullptr);
        };
        tor->metainfo_.files().remove(tor->current_dir(), tor->name(), delete_func_wrapper);
    }

    tr_torrentFreeInSessionThread(tor);
}

void freeTorrent(tr_torrent* tor)
{
    using namespace queue_helpers;

    auto const lock = tor->unique_lock();

    TR_ASSERT(!tor->is_running());

    tr_session* session = tor->session;

    tor->doomed_.emit(tor);

    session->announcer_->removeTorrent(tor);

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
                t->mark_changed();
            }
        }

        TR_ASSERT(queueIsSequenced(session));
    }

    delete tor;
}
} // namespace start_stop_helpers

struct torrent_start_opts
{
    bool bypass_queue = false;

    // true or false if we know whether or not local data exists,
    // or unset if we don't know and need to check for ourselves
    std::optional<bool> has_local_data;
};

void torrentStart(tr_torrent* tor, torrent_start_opts opts)
{
    using namespace start_stop_helpers;

    auto const lock = tor->unique_lock();

    switch (tor->activity())
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

    /* allow finished torrents to be resumed */
    if (tr_torrentIsSeedRatioDone(tor))
    {
        tr_logAddInfoTor(tor, _("Restarted manually -- disabling its seed ratio"));
        tor->set_seed_ratio_mode(TR_RATIOLIMIT_UNLIMITED);
    }

    tor->is_running_ = true;
    tor->set_dirty();
    tor->session->runInSessionThread(torrentStartImpl, tor);
}

void torrentStop(tr_torrent* const tor)
{
    TR_ASSERT(tr_isTorrent(tor));
    TR_ASSERT(tor->session->am_in_session_thread());
    auto const lock = tor->unique_lock();

    tor->is_running_ = false;
    tor->is_stopping_ = false;

    if (!tor->session->isClosing())
    {
        tr_logAddInfoTor(tor, _("Pausing torrent"));
    }

    tor->session->verify_remove(tor);

    tor->stopped_.emit(tor);
    tor->session->announcer_->stopTorrent(tor);

    tor->session->closeTorrentFiles(tor);

    if (!tor->is_deleting_)
    {
        tr_torrentSave(tor);
    }

    torrentSetQueued(tor, false);
}
} // namespace

void tr_torrentStop(tr_torrent* tor)
{
    if (!tr_isTorrent(tor))
    {
        return;
    }

    auto const lock = tor->unique_lock();

    tor->start_when_stable = false;
    tor->set_dirty();
    tor->session->runInSessionThread(torrentStop, tor);
}

void tr_torrentRemove(tr_torrent* tor, bool delete_flag, tr_fileFunc delete_func, void* user_data)
{
    using namespace start_stop_helpers;

    TR_ASSERT(tr_isTorrent(tor));

    tor->is_deleting_ = true;

    tor->session->runInSessionThread(removeTorrentInSessionThread, tor, delete_flag, delete_func, user_data);
}

void tr_torrentFreeInSessionThread(tr_torrent* tor)
{
    using namespace start_stop_helpers;

    TR_ASSERT(tr_isTorrent(tor));
    TR_ASSERT(tor->session != nullptr);
    TR_ASSERT(tor->session->am_in_session_thread());

    if (!tor->session->isClosing())
    {
        tr_logAddInfoTor(tor, _("Removing torrent"));
    }

    torrentStop(tor);

    if (tor->is_deleting_)
    {
        tr_torrent_metainfo::remove_file(tor->session->torrentDir(), tor->name(), tor->info_hash_string(), ".torrent"sv);
        tr_torrent_metainfo::remove_file(tor->session->torrentDir(), tor->name(), tor->info_hash_string(), ".magnet"sv);
        tr_torrent_metainfo::remove_file(tor->session->resumeDir(), tor->name(), tor->info_hash_string(), ".resume"sv);
    }

    freeTorrent(tor);
}

// ---

namespace
{
namespace torrent_init_helpers
{
// Sniff out newly-added seeds so that they can skip the verify step
bool isNewTorrentASeed(tr_torrent* tor)
{
    if (!tor->has_metainfo())
    {
        return false;
    }

    for (tr_file_index_t i = 0, n = tor->file_count(); i < n; ++i)
    {
        // it's not a new seed if a file is missing
        auto const found = tor->find_file(i);
        if (!found)
        {
            return false;
        }

        // it's not a new seed if a file is partial
        if (tr_strv_ends_with(found->filename(), tr_torrent_files::PartialFileSuffix))
        {
            return false;
        }

        // it's not a new seed if a file size is wrong
        if (found->size != tor->file_size(i))
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
    return tor->ensure_piece_is_checked(0);
}

void on_metainfo_completed(tr_torrent* tor)
{
    // we can look for files now that we know what files are in the torrent
    tor->refresh_current_dir();

    callScriptIfEnabled(tor, TR_SCRIPT_ON_TORRENT_ADDED);

    if (tor->session->shouldFullyVerifyAddedTorrents() || !isNewTorrentASeed(tor))
    {
        tr_torrentVerify(tor);
    }
    else
    {
        tor->completion.set_has_all();
        tor->doneDate = tor->addedDate;
        tor->recheck_completeness();

        if (tor->start_when_stable)
        {
            torrentStart(tor, {});
        }
        else if (tor->is_running())
        {
            tr_torrentStop(tor);
        }
    }
}
} // namespace torrent_init_helpers
} // namespace

void tr_torrent::on_metainfo_updated()
{
    completion = tr_completion{ this, &block_info() };
    obfuscated_hash = tr_sha1::digest("req2"sv, info_hash());
    fpm_.reset(metainfo_);
    file_mtimes_.resize(file_count());
    file_priorities_.reset(&fpm_);
    files_wanted_.reset(&fpm_);
    checked_pieces_ = tr_bitfield{ size_t(piece_count()) };
}

void tr_torrent::init(tr_ctor const* const ctor)
{
    using namespace torrent_init_helpers;

    session = tr_ctorGetSession(ctor);
    TR_ASSERT(session != nullptr);
    auto const lock = unique_lock();

    queuePosition = std::size(session->torrents());

    on_metainfo_updated();

    char const* dir = nullptr;
    if (tr_ctorGetDownloadDir(ctor, TR_FORCE, &dir) || tr_ctorGetDownloadDir(ctor, TR_FALLBACK, &dir))
    {
        download_dir_ = dir;
    }

    if (!tr_ctorGetIncompleteDir(ctor, &dir))
    {
        dir = tr_sessionGetIncompleteDir(session);
    }

    if (tr_sessionIsIncompleteDirEnabled(session))
    {
        incomplete_dir_ = dir;
    }
    bandwidth_.set_parent(&session->top_bandwidth_);
    bandwidth_.set_priority(tr_ctorGetBandwidthPriority(ctor));
    error().clear();
    finished_seeding_by_idle_ = false;

    setLabels(tr_ctorGetLabels(ctor));

    session->addTorrent(this);

    TR_ASSERT(downloadedCur == 0);
    TR_ASSERT(uploadedCur == 0);

    mark_changed();

    addedDate = tr_time(); // this is a default that will be overwritten by the resume file

    tr_resume::fields_t loaded = {};

    {
        // tr_resume::load() calls a lot of tr_torrentSetFoo() methods
        // that set things as dirty, but... these settings being loaded are
        // the same ones that would be saved back again, so don't let them
        // affect the 'is dirty' flag.
        auto const was_dirty = is_dirty();
        loaded = tr_resume::load(this, tr_resume::All, ctor);
        set_dirty(was_dirty);
        tr_torrent_metainfo::migrate_file(session->torrentDir(), name(), info_hash_string(), ".torrent"sv);
    }

    completeness = completion.status();

    tr_ctorInitTorrentPriorities(ctor, this);
    tr_ctorInitTorrentWanted(ctor, this);

    refresh_current_dir();

    if ((loaded & tr_resume::Speedlimit) == 0)
    {
        use_speed_limit(TR_UP, false);
        set_speed_limit_bps(TR_UP, tr_toSpeedBytes(session->speedLimitKBps(TR_UP)));
        use_speed_limit(TR_DOWN, false);
        set_speed_limit_bps(TR_DOWN, tr_toSpeedBytes(session->speedLimitKBps(TR_DOWN)));
        tr_torrentUseSessionLimits(this, true);
    }

    if ((loaded & tr_resume::Ratiolimit) == 0)
    {
        set_seed_ratio_mode(TR_RATIOLIMIT_GLOBAL);
        set_seed_ratio(session->desiredRatio());
    }

    if ((loaded & tr_resume::Idlelimit) == 0)
    {
        set_idle_limit_mode(TR_IDLELIMIT_GLOBAL);
        set_idle_limit_minutes(session->idleLimitMinutes());
    }

    auto has_local_data = std::optional<bool>{};
    if ((loaded & tr_resume::Progress) != 0)
    {
        // if tr_resume::load() loaded progress info, then initCheckedPieces()
        // has already looked for local data on the filesystem
        has_local_data = std::any_of(std::begin(file_mtimes_), std::end(file_mtimes_), [](auto mtime) { return mtime > 0; });
    }

    auto const filename = has_metainfo() ? torrent_file() : magnet_file();

    // if we don't have a local .torrent or .magnet file already,
    // assume the torrent is new
    bool const is_new_torrent = !tr_sys_path_exists(filename);

    if (is_new_torrent)
    {
        tr_error* error = nullptr;

        if (has_metainfo()) // torrent file
        {
            tr_ctorSaveContents(ctor, filename, &error);
        }
        else // magnet link
        {
            auto const magnet_link = magnet();
            tr_file_save(filename, magnet_link, &error);
        }

        if (error != nullptr)
        {
            this->error().set_local_error(fmt::format(
                _("Couldn't save '{path}': {error} ({error_code})"),
                fmt::arg("path", filename),
                fmt::arg("error", error->message),
                fmt::arg("error_code", error->code)));
            tr_error_clear(&error);
        }
    }

    torrent_announcer = session->announcer_->addTorrent(this, &tr_torrent::on_tracker_response);

    if (auto const has_metainfo = this->has_metainfo(); is_new_torrent && has_metainfo)
    {
        on_metainfo_completed(this);
    }
    else if (start_when_stable)
    {
        auto opts = torrent_start_opts{};
        opts.bypass_queue = !has_metainfo; // to fetch metainfo from peers
        opts.has_local_data = has_local_data;
        torrentStart(this, opts);
    }
    else
    {
        setLocalErrorIfFilesDisappeared(this, has_local_data);
    }
}

void tr_torrent::set_metainfo(tr_torrent_metainfo tm)
{
    using namespace torrent_init_helpers;

    TR_ASSERT(!has_metainfo());
    metainfo_ = std::move(tm);
    on_metainfo_updated();

    got_metainfo_.emit(this);
    session->onMetadataCompleted(this);
    this->set_dirty();
    this->mark_edited();

    on_metainfo_completed(this);
    this->on_announce_list_changed();
}

tr_torrent* tr_torrentNew(tr_ctor* ctor, tr_torrent** setme_duplicate_of)
{
    using namespace torrent_init_helpers;

    TR_ASSERT(ctor != nullptr);
    auto* const session = tr_ctorGetSession(ctor);
    TR_ASSERT(session != nullptr);

    // is the metainfo valid?
    auto metainfo = tr_ctorStealMetainfo(ctor);
    if (std::empty(metainfo.info_hash_string()))
    {
        return nullptr;
    }

    // is it a duplicate?
    if (auto* const duplicate_of = session->torrents().get(metainfo.info_hash()); duplicate_of != nullptr)
    {
        if (setme_duplicate_of != nullptr)
        {
            *setme_duplicate_of = duplicate_of;
        }

        return nullptr;
    }

    auto* const tor = new tr_torrent{ std::move(metainfo) };
    tor->verify_done_callback_ = tr_ctorStealVerifyDoneCallback(ctor);
    tor->init(ctor);
    return tor;
}

// --- Location

namespace
{
namespace location_helpers
{
void setLocationInSessionThread(tr_torrent* tor, std::string const& path, bool move_from_old_path, int volatile* setme_state)
{
    TR_ASSERT(tr_isTorrent(tor));
    TR_ASSERT(tor->session->am_in_session_thread());

    auto ok = bool{ true };
    if (move_from_old_path)
    {
        if (setme_state != nullptr)
        {
            *setme_state = TR_LOC_MOVING;
        }

        // ensure the files are all closed and idle before moving
        tor->session->closeTorrentFiles(tor);
        tor->session->verify_remove(tor);

        tr_error* error = nullptr;
        ok = tor->metainfo_.files().move(tor->current_dir(), path, tor->name(), &error);
        if (error != nullptr)
        {
            tor->error().set_local_error(fmt::format(
                _("Couldn't move '{old_path}' to '{path}': {error} ({error_code})"),
                fmt::arg("old_path", tor->current_dir()),
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
        tor->set_download_dir(path);

        if (move_from_old_path)
        {
            tor->incomplete_dir_.clear();
            tor->current_dir_ = tor->download_dir();
        }
    }

    if (setme_state != nullptr)
    {
        *setme_state = ok ? TR_LOC_DONE : TR_LOC_ERROR;
    }
}
size_t buildSearchPathArray(tr_torrent const* tor, std::string_view* paths)
{
    auto* walk = paths;

    if (auto const& path = tor->download_dir(); !std::empty(path))
    {
        *walk++ = path.sv();
    }

    if (auto const& path = tor->incomplete_dir(); !std::empty(path))
    {
        *walk++ = path.sv();
    }

    return walk - paths;
}
} // namespace location_helpers
} // namespace

void tr_torrent::set_location(std::string_view location, bool move_from_old_path, int volatile* setme_state)
{
    using namespace location_helpers;

    if (setme_state != nullptr)
    {
        *setme_state = TR_LOC_MOVING;
    }

    this->session
        ->runInSessionThread(setLocationInSessionThread, this, std::string{ location }, move_from_old_path, setme_state);
}

void tr_torrentSetLocation(tr_torrent* tor, char const* location, bool move_from_old_path, int volatile* setme_state)
{
    TR_ASSERT(tr_isTorrent(tor));
    TR_ASSERT(!tr_str_is_empty(location));

    tor->set_location(location, move_from_old_path, setme_state);
}

std::optional<tr_torrent_files::FoundFile> tr_torrent::find_file(tr_file_index_t file_index) const
{
    using namespace location_helpers;

    auto paths = std::array<std::string_view, 4>{};
    auto const n_paths = buildSearchPathArray(this, std::data(paths));
    return metainfo_.files().find(file_index, std::data(paths), n_paths);
}

bool tr_torrent::has_any_local_data() const
{
    using namespace location_helpers;

    auto paths = std::array<std::string_view, 4>{};
    auto const n_paths = buildSearchPathArray(this, std::data(paths));
    return metainfo_.files().hasAnyLocalData(std::data(paths), n_paths);
}

void tr_torrentSetDownloadDir(tr_torrent* tor, char const* path)
{
    TR_ASSERT(tr_isTorrent(tor));

    if (tor->download_dir_ != path)
    {
        tor->set_download_dir(path, true);
    }
}

char const* tr_torrentGetDownloadDir(tr_torrent const* tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    return tor->download_dir().c_str();
}

char const* tr_torrentGetCurrentDir(tr_torrent const* tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    return tor->current_dir().c_str();
}

void tr_torrentChangeMyPort(tr_torrent* tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    if (tor->is_running())
    {
        tr_announcerChangeMyPort(tor);
    }
}

// ---

namespace
{
namespace manual_update_helpers
{
void torrentManualUpdateImpl(tr_torrent* const tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    if (tor->is_running())
    {
        tr_announcerManualAnnounce(tor);
    }
}
} // namespace manual_update_helpers
} // namespace

void tr_torrentManualUpdate(tr_torrent* tor)
{
    using namespace manual_update_helpers;

    TR_ASSERT(tr_isTorrent(tor));

    tor->session->runInSessionThread(torrentManualUpdateImpl, tor);
}

bool tr_torrentCanManualUpdate(tr_torrent const* tor)
{
    return tr_isTorrent(tor) && tor->is_running() && tr_announcerCanManualAnnounce(tor);
}

// ---

tr_stat tr_torrent::stats() const
{
    static auto constexpr IsStalled = [](tr_torrent const* const tor, std::optional<size_t> idle_secs)
    {
        return tor->session->queueStalledEnabled() && idle_secs > tor->session->queueStalledMinutes() * 60U;
    };

    auto const now_msec = tr_time_msec();
    auto const now_sec = tr_time();

    auto const swarm_stats = this->swarm != nullptr ? tr_swarmGetStats(this->swarm) : tr_swarm_stats{};
    auto const activity = this->activity();
    auto const idle_seconds = this->idle_seconds(now_sec);

    auto stats = tr_stat{};

    stats.id = this->id();
    stats.activity = activity;
    stats.error = this->error().error_type();
    stats.queuePosition = this->queuePosition;
    stats.idleSecs = idle_seconds ? static_cast<time_t>(*idle_seconds) : -1;
    stats.isStalled = IsStalled(this, idle_seconds);
    stats.errorString = this->error().errmsg().c_str();

    stats.peersConnected = swarm_stats.peer_count;
    stats.peersSendingToUs = swarm_stats.active_peer_count[TR_DOWN];
    stats.peersGettingFromUs = swarm_stats.active_peer_count[TR_UP];
    stats.webseedsSendingToUs = swarm_stats.active_webseed_count;

    for (int i = 0; i < TR_PEER_FROM__MAX; i++)
    {
        stats.peersFrom[i] = swarm_stats.peer_from_count[i];
    }

    auto const piece_upload_speed_byps = this->bandwidth_.get_piece_speed_bytes_per_second(now_msec, TR_UP);
    stats.pieceUploadSpeed_KBps = tr_toSpeedKBps(piece_upload_speed_byps);
    auto const piece_download_speed_byps = this->bandwidth_.get_piece_speed_bytes_per_second(now_msec, TR_DOWN);
    stats.pieceDownloadSpeed_KBps = tr_toSpeedKBps(piece_download_speed_byps);

    stats.percentComplete = this->completion.percent_complete();
    stats.metadataPercentComplete = tr_torrentGetMetadataPercent(this);

    stats.percentDone = this->completion.percent_done();
    stats.leftUntilDone = this->completion.left_until_done();
    stats.sizeWhenDone = this->completion.size_when_done();

    auto const verify_progress = this->verify_progress();
    stats.recheckProgress = verify_progress.value_or(0.0);
    stats.activityDate = this->activityDate;
    stats.addedDate = this->addedDate;
    stats.doneDate = this->doneDate;
    stats.editDate = this->editDate;
    stats.startDate = this->startDate;
    stats.secondsSeeding = this->seconds_seeding(now_sec);
    stats.secondsDownloading = this->seconds_downloading(now_sec);

    stats.corruptEver = this->corruptCur + this->corruptPrev;
    stats.downloadedEver = this->downloadedCur + this->downloadedPrev;
    stats.uploadedEver = this->uploadedCur + this->uploadedPrev;
    stats.haveValid = this->completion.has_valid();
    stats.haveUnchecked = this->has_total() - stats.haveValid;
    stats.desiredAvailable = tr_peerMgrGetDesiredAvailable(this);

    stats.ratio = tr_getRatio(stats.uploadedEver, this->size_when_done());

    auto seed_ratio_bytes_left = uint64_t{};
    auto seed_ratio_bytes_goal = uint64_t{};
    bool const seed_ratio_applies = tr_torrentGetSeedRatioBytes(this, &seed_ratio_bytes_left, &seed_ratio_bytes_goal);

    // eta, etaIdle
    stats.eta = TR_ETA_NOT_AVAIL;
    stats.etaIdle = TR_ETA_NOT_AVAIL;
    if (activity == TR_STATUS_DOWNLOAD)
    {
        if (auto const eta_speed_byps = eta_speed_.update(now_msec, piece_download_speed_byps); eta_speed_byps == 0U)
        {
            stats.eta = TR_ETA_UNKNOWN;
        }
        else if (stats.leftUntilDone <= stats.desiredAvailable || webseed_count() >= 1U)
        {
            stats.eta = stats.leftUntilDone / eta_speed_byps;
        }
    }
    else if (activity == TR_STATUS_SEED)
    {
        auto const eta_speed_byps = eta_speed_.update(now_msec, piece_upload_speed_byps);

        if (seed_ratio_applies)
        {
            stats.eta = eta_speed_byps == 0U ? TR_ETA_UNKNOWN : seed_ratio_bytes_left / eta_speed_byps;
        }

        if (eta_speed_byps < 1U)
        {
            if (auto const secs_left = idle_seconds_left(now_sec); secs_left)
            {
                stats.etaIdle = *secs_left;
            }
        }
    }

    /* stats.haveValid is here to make sure a torrent isn't marked 'finished'
     * when the user hits "uncheck all" prior to starting the torrent... */
    stats.finished = this->finished_seeding_by_idle_ ||
        (seed_ratio_applies && seed_ratio_bytes_left == 0 && stats.haveValid != 0);

    if (!seed_ratio_applies || stats.finished)
    {
        stats.seedRatioPercentDone = 1.0F;
    }
    else if (seed_ratio_bytes_goal == 0) /* impossible? safeguard for div by zero */
    {
        stats.seedRatioPercentDone = 0.0F;
    }
    else
    {
        stats.seedRatioPercentDone = float(seed_ratio_bytes_goal - seed_ratio_bytes_left) / seed_ratio_bytes_goal;
    }

    /* test some of the constraints */
    TR_ASSERT(stats.sizeWhenDone <= this->total_size());
    TR_ASSERT(stats.leftUntilDone <= stats.sizeWhenDone);
    TR_ASSERT(stats.desiredAvailable <= stats.leftUntilDone);
    return stats;
}

tr_stat const* tr_torrentStat(tr_torrent* const tor)
{
    tor->stats_ = tor->stats();
    return &tor->stats_;
}

// ---

tr_file_view tr_torrentFile(tr_torrent const* tor, tr_file_index_t file)
{
    TR_ASSERT(tr_isTorrent(tor));

    auto const& subpath = tor->file_subpath(file);
    auto const priority = tor->file_priorities_.file_priority(file);
    auto const wanted = tor->files_wanted_.file_wanted(file);
    auto const length = tor->file_size(file);
    auto const [begin, end] = tor->pieces_in_file(file);

    if (tor->completeness == TR_SEED || length == 0)
    {
        return { subpath.c_str(), length, length, 1.0, begin, end, priority, wanted };
    }

    auto const have = tor->completion.count_has_bytes_in_span(tor->fpm_.byte_span(file));
    return { subpath.c_str(), have, length, have >= length ? 1.0 : have / double(length), begin, end, priority, wanted };
}

size_t tr_torrentFileCount(tr_torrent const* torrent)
{
    TR_ASSERT(tr_isTorrent(torrent));

    return torrent->file_count();
}

tr_webseed_view tr_torrentWebseed(tr_torrent const* tor, size_t nth)
{
    return tr_peerMgrWebseed(tor, nth);
}

size_t tr_torrentWebseedCount(tr_torrent const* tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    return tor->webseed_count();
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
    ret.hash_string = tor->info_hash_string().c_str();
    ret.comment = tor->comment().c_str();
    ret.creator = tor->creator().c_str();
    ret.source = tor->source().c_str();
    ret.total_size = tor->total_size();
    ret.date_created = tor->date_created();
    ret.piece_size = tor->piece_size();
    ret.n_pieces = tor->piece_count();
    ret.is_private = tor->is_private();
    ret.is_folder = tor->file_count() > 1 || (tor->file_count() == 1 && tr_strv_contains(tor->file_subpath(0), '/'));

    return ret;
}

std::string tr_torrentFilename(tr_torrent const* tor)
{
    return std::string{ tor->torrent_file() };
}

size_t tr_torrentFilenameToBuf(tr_torrent const* tor, char* buf, size_t buflen)
{
    return tr_strv_to_buf(tr_torrentFilename(tor), buf, buflen);
}

// ---

tr_peer_stat* tr_torrentPeers(tr_torrent const* tor, size_t* peer_count)
{
    TR_ASSERT(tr_isTorrent(tor));

    return tr_peerMgrPeerStats(tor, peer_count);
}

void tr_torrentPeersFree(tr_peer_stat* peer_stats, size_t /*peer_count*/)
{
    delete[] peer_stats;
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
    return tor->amount_done_bins(tabs, n_tabs);
}

// --- Start/Stop Callback

namespace
{
void tr_torrentStartImpl(tr_torrent* tor, bool bypass_queue)
{
    if (!tr_isTorrent(tor))
    {
        return;
    }

    tor->start_when_stable = true;
    auto opts = torrent_start_opts{};
    opts.bypass_queue = bypass_queue;
    torrentStart(tor, opts);
}
} // namespace

void tr_torrentStart(tr_torrent* tor)
{
    tr_torrentStartImpl(tor, false);
}

void tr_torrentStartNow(tr_torrent* tor)
{
    tr_torrentStartImpl(tor, true);
}

void tr_torrentStartMagnet(tr_torrent* tor)
{
    tr_torrentStart(tor);
}

// ---

namespace
{
namespace verify_helpers
{
void onVerifyDoneThreadFunc(tr_torrent* const tor)
{
    TR_ASSERT(tor->session->am_in_session_thread());

    if (tor->is_deleting_)
    {
        return;
    }

    tor->recheck_completeness();

    if (tor->start_when_stable)
    {
        auto opts = torrent_start_opts{};
        opts.has_local_data = !tor->checked_pieces_.has_none();
        torrentStart(tor, opts);
    }
}

void verifyTorrent(tr_torrent* const tor, bool force)
{
    TR_ASSERT(tor->session->am_in_session_thread());
    auto const lock = tor->unique_lock();

    if (tor->is_deleting_)
    {
        return;
    }

    /* if the torrent's already being verified, stop it */
    tor->session->verify_remove(tor);

    if (!tor->has_metainfo())
    {
        return;
    }

    if (tor->is_running())
    {
        torrentStop(tor);
    }

    if (force || !setLocalErrorIfFilesDisappeared(tor))
    {
        tor->session->verify_add(tor);
    }
}
} // namespace verify_helpers
} // namespace

void tr_torrentVerify(tr_torrent* tor, bool force)
{
    using namespace verify_helpers;

    tor->session->runInSessionThread(verifyTorrent, tor, force);
}

void tr_torrent::set_verify_state(VerifyState const state)
{
    TR_ASSERT(state == VerifyState::None || state == VerifyState::Queued || state == VerifyState::Active);

    verify_state_ = state;
    verify_progress_ = {};
    mark_changed();
}

tr_torrent_metainfo const& tr_torrent::VerifyMediator::metainfo() const
{
    return tor_->metainfo_;
}

std::optional<std::string> tr_torrent::VerifyMediator::find_file(tr_file_index_t const file_index) const
{
    if (auto const found = tor_->find_file(file_index); found)
    {
        return std::string{ found->filename().sv() };
    }

    return {};
}

void tr_torrent::VerifyMediator::on_verify_queued()
{
    tr_logAddTraceTor(tor_, "Queued for verification");
    tor_->set_verify_state(VerifyState::Queued);
}

void tr_torrent::VerifyMediator::on_verify_started()
{
    tr_logAddDebugTor(tor_, "Verifying torrent");
    time_started_ = tr_time();
    tor_->set_verify_state(VerifyState::Active);
}

void tr_torrent::VerifyMediator::on_piece_checked(tr_piece_index_t const piece, bool const has_piece)
{
    auto const had_piece = tor_->has_piece(piece);

    if (has_piece || had_piece)
    {
        tor_->set_has_piece(piece, has_piece);
        tor_->set_dirty();
    }

    tor_->checked_pieces_.set(piece, true);
    tor_->mark_changed();
    tor_->verify_progress_ = std::clamp(static_cast<float>(piece + 1U) / tor_->metainfo_.piece_count(), 0.0F, 1.0F);
}

void tr_torrent::VerifyMediator::on_verify_done(bool const aborted)
{
    using namespace verify_helpers;

    if (time_started_.has_value())
    {
        auto const total_size = tor_->total_size();
        auto const duration_secs = tr_time() - *time_started_;
        tr_logAddDebugTor(
            tor_,
            fmt::format(
                "Verification is done. It took {} seconds to verify {} bytes ({} bytes per second)",
                duration_secs,
                total_size,
                total_size / (1 + duration_secs)));
    }

    tor_->set_verify_state(VerifyState::None);

    if (!aborted && !tor_->is_deleting_)
    {
        tor_->session->runInSessionThread(onVerifyDoneThreadFunc, tor_);
    }

    if (tor_->verify_done_callback_)
    {
        tor_->verify_done_callback_(tor_);
    }
}

// ---

void tr_torrentSave(tr_torrent* tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    if (tor->is_dirty_)
    {
        tor->is_dirty_ = false;
        tr_resume::save(tor);
    }
}

// --- Completeness

namespace
{
namespace completeness_helpers
{
[[nodiscard]] constexpr char const* get_completion_string(int type)
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
} // namespace completeness_helpers
} // namespace

void tr_torrent::recheck_completeness()
{
    using namespace completeness_helpers;

    auto const lock = unique_lock();

    needs_completeness_check_ = false;

    auto const new_completeness = completion.status();

    if (new_completeness != completeness)
    {
        bool const recent_change = downloadedCur != 0;
        bool const was_running = is_running();

        if (recent_change)
        {
            tr_logAddTraceTor(
                this,
                fmt::format(
                    "State changed from {} to {}",
                    get_completion_string(completeness),
                    get_completion_string(new_completeness)));
        }

        this->completeness = new_completeness;
        this->session->closeTorrentFiles(this);

        if (this->is_done())
        {
            if (recent_change)
            {
                tr_announcerTorrentCompleted(this);
                this->mark_changed();
                this->doneDate = tr_time();
            }

            if (this->current_dir() == this->incomplete_dir())
            {
                this->set_location(this->download_dir(), true, nullptr);
            }

            done_.emit(this, recent_change);
        }

        this->session->onTorrentCompletenessChanged(this, completeness, was_running);

        this->set_dirty();

        if (this->is_done())
        {
            tr_torrentSave(this);
            callScriptIfEnabled(this, TR_SCRIPT_ON_TORRENT_DONE);
        }
    }
}

// --- File DND

void tr_torrentSetFileDLs(tr_torrent* tor, tr_file_index_t const* files, tr_file_index_t n_files, bool wanted)
{
    TR_ASSERT(tr_isTorrent(tor));

    tor->set_files_wanted(files, n_files, wanted);
}

// ---

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
    this->set_dirty();
}

// ---

void tr_torrent::set_bandwidth_group(std::string_view group_name) noexcept
{
    group_name = tr_strv_strip(group_name);

    auto const lock = this->unique_lock();

    if (std::empty(group_name))
    {
        this->bandwidth_group_ = tr_interned_string{};
        this->bandwidth_.set_parent(&this->session->top_bandwidth_);
    }
    else
    {
        this->bandwidth_group_ = group_name;
        this->bandwidth_.set_parent(&this->session->getBandwidthGroup(group_name));
    }

    this->set_dirty();
}

// ---

tr_priority_t tr_torrentGetPriority(tr_torrent const* tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    return tor->get_priority();
}

void tr_torrentSetPriority(tr_torrent* tor, tr_priority_t priority)
{
    TR_ASSERT(tr_isTorrent(tor));
    TR_ASSERT(tr_isPriority(priority));

    if (tor->bandwidth_.get_priority() != priority)
    {
        tor->bandwidth_.set_priority(priority);

        tor->set_dirty();
    }
}

// ---

void tr_torrentSetPeerLimit(tr_torrent* tor, uint16_t max_connected_peers)
{
    TR_ASSERT(tr_isTorrent(tor));

    if (tor->max_connected_peers_ != max_connected_peers)
    {
        tor->max_connected_peers_ = max_connected_peers;

        tor->set_dirty();
    }
}

uint16_t tr_torrentGetPeerLimit(tr_torrent const* tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    return tor->peer_limit();
}

// ---

bool tr_torrentReqIsValid(tr_torrent const* tor, tr_piece_index_t index, uint32_t offset, uint32_t length)
{
    TR_ASSERT(tr_isTorrent(tor));

    int err = 0;

    if (index >= tor->piece_count())
    {
        err = 1;
    }
    else if (length < 1)
    {
        err = 2;
    }
    else if (offset + length > tor->piece_size(index))
    {
        err = 3;
    }
    else if (length > tr_block_info::BlockSize)
    {
        err = 4;
    }
    else if (tor->piece_loc(index, offset, length).byte > tor->total_size())
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
    auto const [begin_byte, end_byte] = tor->fpm_.byte_span(file);

    auto const begin_block = tor->byte_loc(begin_byte).block;
    if (begin_byte >= end_byte) // 0-byte file
    {
        return { begin_block, begin_block + 1 };
    }

    auto const final_block = tor->byte_loc(end_byte - 1).block;
    auto const end_block = final_block + 1;
    return { begin_block, end_block };
}

// ---

// TODO: should be const after tr_ioTestPiece() is const
bool tr_torrent::check_piece(tr_piece_index_t piece)
{
    bool const pass = tr_ioTestPiece(this, piece);
    tr_logAddTraceTor(this, fmt::format("[LAZY] tr_torrent.checkPiece tested piece {}, pass=={}", piece, pass));
    return pass;
}

// ---

bool tr_torrent::set_tracker_list(std::string_view text)
{
    auto const lock = this->unique_lock();

    auto announce_list = tr_announce_list();
    if (!announce_list.parse(text))
    {
        return false;
    }

    auto const has_metadata = this->has_metainfo();
    if (has_metadata && !announce_list.save(torrent_file()))
    {
        return false;
    }

    this->metainfo_.announce_list() = announce_list;
    this->mark_edited();

    // magnet links
    if (!has_metadata)
    {
        auto const magnet_file = this->magnet_file();
        auto const magnet_link = this->magnet();
        tr_error* save_error = nullptr;
        if (!tr_file_save(magnet_file, magnet_link, &save_error))
        {
            this->error().set_local_error(fmt::format(
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
    if (auto const& error_url = error_.announce_url(); !std::empty(error_url))
    {
        if (std::any_of(
                std::begin(this->announce_list()),
                std::end(this->announce_list()),
                [error_url](auto const& tracker) { return tracker.announce == error_url; }))
        {
            error_.clear();
        }
    }

    on_announce_list_changed();

    return true;
}

void tr_torrent::on_tracker_response(tr_tracker_event const* event)
{
    switch (event->type)
    {
    case tr_tracker_event::Type::Peers:
        tr_logAddTraceTor(this, fmt::format("Got {} peers from tracker", std::size(event->pex)));
        tr_peerMgrAddPex(this, TR_PEER_FROM_TRACKER, std::data(event->pex), std::size(event->pex));
        break;

    case tr_tracker_event::Type::Counts:
        if (is_private() && (event->leechers == 0))
        {
            swarm_is_all_seeds_.emit(this);
        }

        break;

    case tr_tracker_event::Type::Warning:
        tr_logAddWarnTor(
            this,
            fmt::format(
                _("Tracker warning: '{warning}' ({url})"),
                fmt::arg("warning", event->text),
                fmt::arg("url", tr_urlTrackerLogName(event->announce_url))));
        error_.set_tracker_warning(event->announce_url, event->text);
        break;

    case tr_tracker_event::Type::Error:
        error_.set_tracker_error(event->announce_url, event->text);
        break;

    case tr_tracker_event::Type::ErrorClear:
        error_.clear_if_tracker();
        break;
    }
}

bool tr_torrentSetTrackerList(tr_torrent* tor, char const* text)
{
    return text != nullptr && tor->set_tracker_list(text);
}

std::string tr_torrentGetTrackerList(tr_torrent const* tor)
{
    return tor->tracker_list();
}

size_t tr_torrentGetTrackerListToBuf(tr_torrent const* tor, char* buf, size_t buflen)
{
    return tr_strv_to_buf(tr_torrentGetTrackerList(tor), buf, buflen);
}

// ---

uint64_t tr_torrentGetBytesLeftToAllocate(tr_torrent const* tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    uint64_t bytes_left = 0;

    for (tr_file_index_t i = 0, n = tor->file_count(); i < n; ++i)
    {
        if (auto const wanted = tor->files_wanted_.file_wanted(i); !wanted)
        {
            continue;
        }

        auto const length = tor->file_size(i);
        bytes_left += length;

        auto const found = tor->find_file(i);
        if (found)
        {
            bytes_left -= found->size;
        }
    }

    return bytes_left;
}

// ---

std::string_view tr_torrent::primary_mime_type() const
{
    // count up how many bytes there are for each mime-type in the torrent
    // NB: get_mime_type_for_filename() always returns the same ptr for a
    // mime_type, so its raw pointer can be used as a key.
    auto size_per_mime_type = small::unordered_map<std::string_view, size_t, 256U>{};
    for (tr_file_index_t i = 0, n = this->file_count(); i < n; ++i)
    {
        auto const mime_type = tr_get_mime_type_for_filename(this->file_subpath(i));
        size_per_mime_type[mime_type] += this->file_size(i);
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

// ---

namespace
{
namespace got_block_helpers
{
void onFileCompleted(tr_torrent* tor, tr_file_index_t i)
{
    /* close the file so that we can reopen in read-only mode as needed */
    tor->session->closeTorrentFile(tor, i);

    /* now that the file is complete and closed, we can start watching its
     * mtime timestamp for changes to know if we need to reverify pieces */
    tor->file_mtimes_[i] = tr_time();

    /* if the torrent's current filename isn't the same as the one in the
     * metadata -- for example, if it had the ".part" suffix appended to
     * it until now -- then rename it to match the one in the metadata */
    if (auto found = tor->find_file(i); found)
    {
        if (auto const& file_subpath = tor->file_subpath(i); file_subpath != found->subpath())
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

void onPieceCompleted(tr_torrent* tor, tr_piece_index_t piece)
{
    tor->piece_completed_.emit(tor, piece);

    // bookkeeping
    tor->set_needs_completeness_check();

    // if this piece completes any file, invoke the fileCompleted func for it
    auto const span = tor->fpm_.file_span(piece);
    for (auto file = span.begin; file < span.end; ++file)
    {
        if (tor->completion.has_blocks(tr_torGetFileBlockSpan(tor, file)))
        {
            onFileCompleted(tor, file);
        }
    }
}

void onPieceFailed(tr_torrent* tor, tr_piece_index_t piece)
{
    tr_logAddDebugTor(tor, fmt::format("Piece {}, which was just downloaded, failed its checksum test", piece));

    auto const n = tor->piece_size(piece);
    tor->corruptCur += n;
    tor->downloadedCur -= std::min(tor->downloadedCur, uint64_t{ n });
    tor->got_bad_piece_.emit(tor, piece);
    tor->set_has_piece(piece, false);
}
} // namespace got_block_helpers
} // namespace

void tr_torrentGotBlock(tr_torrent* tor, tr_block_index_t block)
{
    using namespace got_block_helpers;

    TR_ASSERT(tr_isTorrent(tor));
    TR_ASSERT(tor->session->am_in_session_thread());

    if (tor->has_block(block))
    {
        tr_logAddDebugTor(tor, "we have this block already...");
        auto const n = tor->block_size(block);
        tor->downloadedCur -= std::min(tor->downloadedCur, uint64_t{ n });
        return;
    }

    tor->set_dirty();

    tor->completion.add_block(block);

    auto const block_loc = tor->block_loc(block);
    auto const first_piece = block_loc.piece;
    auto const last_piece = tor->byte_loc(block_loc.byte + tor->block_size(block) - 1).piece;
    for (auto piece = first_piece; piece <= last_piece; ++piece)
    {
        if (!tor->has_piece(piece))
        {
            continue;
        }

        if (tor->check_piece(piece))
        {
            onPieceCompleted(tor, piece);
        }
        else
        {
            onPieceFailed(tor, piece);
        }
    }
}

// ---

std::string tr_torrentFindFile(tr_torrent const* tor, tr_file_index_t file_num)
{
    auto const found = tor->find_file(file_num);
    return std::string{ found ? found->filename().sv() : ""sv };
}

size_t tr_torrentFindFileToBuf(tr_torrent const* tor, tr_file_index_t file_num, char* buf, size_t buflen)
{
    return tr_strv_to_buf(tr_torrentFindFile(tor, file_num), buf, buflen);
}

void tr_torrent::set_download_dir(std::string_view path, bool is_new_torrent)
{
    download_dir_ = path;
    mark_edited();
    set_dirty();
    refresh_current_dir();

    if (is_new_torrent)
    {
        if (session->shouldFullyVerifyAddedTorrents() || !torrent_init_helpers::isNewTorrentASeed(this))
        {
            tr_torrentVerify(this);
        }
        else
        {
            completion.set_has_all();
            doneDate = addedDate;
            recheck_completeness();
        }
    }
    else if (error_.error_type() == TR_STAT_LOCAL_ERROR && !setLocalErrorIfFilesDisappeared(this))
    {
        error_.clear();
    }
}

// decide whether we should be looking for files in downloadDir or incompleteDir
void tr_torrent::refresh_current_dir()
{
    auto dir = tr_interned_string{};

    if (std::empty(incomplete_dir()))
    {
        dir = download_dir();
    }
    else if (!has_metainfo()) // no files to find
    {
        dir = incomplete_dir();
    }
    else
    {
        auto const found = find_file(0);
        dir = found ? tr_interned_string{ found->base() } : incomplete_dir();
    }

    TR_ASSERT(!std::empty(dir));
    TR_ASSERT(dir == download_dir() || dir == incomplete_dir());

    current_dir_ = dir;
}

// --- RENAME

namespace
{
namespace rename_helpers
{
bool renameArgsAreValid(tr_torrent const* tor, std::string_view oldpath, std::string_view newname)
{
    if (std::empty(oldpath) || std::empty(newname) || newname == "."sv || newname == ".."sv ||
        tr_strv_contains(newname, TR_PATH_DELIMITER))
    {
        return false;
    }

    auto const newpath = tr_strv_contains(oldpath, TR_PATH_DELIMITER) ?
        tr_pathbuf{ tr_sys_path_dirname(oldpath), '/', newname } :
        tr_pathbuf{ newname };

    if (newpath == oldpath)
    {
        return true;
    }

    auto const newpath_as_dir = tr_pathbuf{ newpath, '/' };
    auto const n_files = tor->file_count();

    for (tr_file_index_t i = 0; i < n_files; ++i)
    {
        auto const& name = tor->file_subpath(i);
        if (newpath == name || tr_strv_starts_with(name, newpath_as_dir))
        {
            return false;
        }
    }

    return true;
}

auto renameFindAffectedFiles(tr_torrent const* tor, std::string_view oldpath)
{
    auto indices = std::vector<tr_file_index_t>{};
    auto const oldpath_as_dir = tr_pathbuf{ oldpath, '/' };
    auto const n_files = tor->file_count();

    for (tr_file_index_t i = 0; i < n_files; ++i)
    {
        auto const& name = tor->file_subpath(i);
        if (name == oldpath || tr_strv_starts_with(name, oldpath_as_dir))
        {
            indices.push_back(i);
        }
    }

    return indices;
}

int renamePath(tr_torrent const* tor, std::string_view oldpath, std::string_view newname)
{
    int err = 0;

    auto const base = tor->is_done() || std::empty(tor->incomplete_dir()) ? tor->download_dir() : tor->incomplete_dir();

    auto src = tr_pathbuf{ base, '/', oldpath };

    if (!tr_sys_path_exists(src)) /* check for it as a partial */
    {
        src += tr_torrent_files::PartialFileSuffix;
    }

    if (tr_sys_path_exists(src))
    {
        auto const parent = tr_sys_path_dirname(src);
        auto const tgt = tr_strv_ends_with(src, tr_torrent_files::PartialFileSuffix) ?
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

void renameTorrentFileString(tr_torrent* tor, std::string_view oldpath, std::string_view newname, tr_file_index_t file_index)
{
    auto name = std::string{};
    auto const subpath = std::string_view{ tor->file_subpath(file_index) };
    auto const oldpath_len = std::size(oldpath);

    if (!tr_strv_contains(oldpath, TR_PATH_DELIMITER))
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
        tor->set_file_subpath(file_index, name);
    }
}

void torrentRenamePath(
    tr_torrent* const tor,
    std::string const& oldpath, // NOLINT performance-unnecessary-value-param
    std::string const& newname, // NOLINT performance-unnecessary-value-param
    tr_torrent_rename_done_func callback,
    void* const callback_user_data)
{
    TR_ASSERT(tr_isTorrent(tor));

    int error = 0;

    if (!renameArgsAreValid(tor, oldpath, newname))
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
            if (std::size(file_indices) == tor->file_count() && !tr_strv_contains(oldpath, '/'))
            {
                tor->set_name(newname);
            }

            tor->mark_edited();
            tor->set_dirty();
        }
    }

    ///

    tor->mark_changed();

    /* callback */
    if (callback != nullptr)
    {
        (*callback)(tor, oldpath.c_str(), newname.c_str(), error, callback_user_data);
    }
}

} // namespace rename_helpers
} // namespace

void tr_torrent::rename_path(
    std::string_view oldpath,
    std::string_view newname,
    tr_torrent_rename_done_func callback,
    void* callback_user_data)
{
    using namespace rename_helpers;

    this->session->runInSessionThread(
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

    tor->rename_path(oldpath, newname, callback, callback_user_data);
}

// ---

void tr_torrentSetFilePriorities(
    tr_torrent* tor,
    tr_file_index_t const* files,
    tr_file_index_t file_count,
    tr_priority_t priority)
{
    tor->set_file_priorities(files, file_count, priority);
}

bool tr_torrentHasMetadata(tr_torrent const* tor)
{
    return tor->has_metainfo();
}

void tr_torrent::mark_edited()
{
    this->editDate = tr_time();
}

void tr_torrent::mark_changed()
{
    this->bump_date_changed(tr_time());
}

void tr_torrent::set_blocks(tr_bitfield blocks)
{
    this->completion.set_blocks(std::move(blocks));
}

[[nodiscard]] bool tr_torrent::ensure_piece_is_checked(tr_piece_index_t piece)
{
    TR_ASSERT(piece < this->piece_count());

    if (is_piece_checked(piece))
    {
        return true;
    }

    bool const checked = check_piece(piece);
    this->mark_changed();
    this->set_dirty();

    checked_pieces_.set(piece, checked);
    return checked;
}

void tr_torrent::init_checked_pieces(tr_bitfield const& checked, time_t const* mtimes /*fileCount()*/)
{
    TR_ASSERT(std::size(checked) == this->piece_count());
    checked_pieces_ = checked;

    auto const n = this->file_count();
    this->file_mtimes_.resize(n);

    for (size_t i = 0; i < n; ++i)
    {
        auto const found = this->find_file(i);
        auto const mtime = found ? found->last_modified_at : 0;

        this->file_mtimes_[i] = mtime;

        // if a file has changed, mark its pieces as unchecked
        if (mtime == 0 || mtime != mtimes[i])
        {
            auto const [begin, end] = pieces_in_file(i);
            checked_pieces_.unset_span(begin, end);
        }
    }
}
