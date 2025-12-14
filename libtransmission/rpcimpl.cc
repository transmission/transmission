// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <ctime>
#include <iterator>
#include <memory>
#include <numeric>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include <small/map.hpp>

#include <libdeflate.h>

#include "libtransmission/transmission.h"

#include "libtransmission/announcer.h"
#include "libtransmission/crypto-utils.h"
#include "libtransmission/error.h"
#include "libtransmission/file.h"
#include "libtransmission/log.h"
#include "libtransmission/net.h"
#include "libtransmission/peer-mgr.h"
#include "libtransmission/api-compat.h"
#include "libtransmission/quark.h"
#include "libtransmission/rpcimpl.h"
#include "libtransmission/session.h"
#include "libtransmission/torrent-ctor.h"
#include "libtransmission/torrent.h"
#include "libtransmission/tr-assert.h"
#include "libtransmission/tr-strbuf.h"
#include "libtransmission/utils.h"
#include "libtransmission/values.h"
#include "libtransmission/variant.h"
#include "libtransmission/version.h"
#include "libtransmission/web-utils.h"
#include "libtransmission/web.h"

using namespace std::literals;
using namespace libtransmission::Values;

namespace JsonRpc
{
// https://www.jsonrpc.org/specification#error_object
namespace Error
{
[[nodiscard]] std::string_view to_string(Code const code)
{
    switch (code)
    {
    case PARSE_ERROR:
        return "Parse error"sv;
    case INVALID_REQUEST:
        return "Invalid Request"sv;
    case METHOD_NOT_FOUND:
        return "Method not found"sv;
    case INVALID_PARAMS:
        return "Invalid params"sv;
    case INTERNAL_ERROR:
        return "Internal error"sv;
    case SUCCESS:
        return "success"sv;
    case SET_ANNOUNCE_LIST:
        return "error setting announce list"sv;
    case INVALID_TRACKER_LIST:
        return "Invalid tracker list"sv;
    case PATH_NOT_ABSOLUTE:
        return "path is not absolute"sv;
    case UNRECOGNIZED_INFO:
        return "unrecognized info"sv;
    case SYSTEM_ERROR:
        return "system error"sv;
    case FILE_IDX_OOR:
        return "file index out of range"sv;
    case PIECE_IDX_OOR:
        return "piece index out of range"sv;
    case HTTP_ERROR:
        return "HTTP error from backend service"sv;
    case CORRUPT_TORRENT:
        return "invalid or corrupt torrent file"sv;
    default:
        return {};
    }
}

namespace
{
[[nodiscard]] tr_variant::Map build_data(std::string_view error_string, tr_variant::Map&& result)
{
    auto ret = tr_variant::Map{ 2U };

    if (!std::empty(error_string))
    {
        ret.try_emplace(TR_KEY_error_string, error_string);
    }

    if (!std::empty(result))
    {
        ret.try_emplace(TR_KEY_result, std::move(result));
    }

    return ret;
}

[[nodiscard]] tr_variant::Map build(Code code, tr_variant::Map&& data)
{
    auto ret = tr_variant::Map{ 3U };
    ret.try_emplace(TR_KEY_code, code);
    ret.try_emplace(TR_KEY_message, tr_variant::unmanaged_string(to_string(code)));
    if (!std::empty(data))
    {
        ret.try_emplace(TR_KEY_data, std::move(data));
    }

    return ret;
}
} // namespace
} // namespace Error

namespace
{
[[nodiscard]] constexpr bool is_valid_id(tr_variant const& val)
{
    switch (val.index())
    {
    // https://www.jsonrpc.org/specification#request_object
    // An identifier established by the Client that MUST contain a String,
    // Number, or NULL value if included. .. The value SHOULD normally not
    // be Null and Numbers SHOULD NOT contain fractional parts
    case tr_variant::StringIndex:
    case tr_variant::IntIndex:
    case tr_variant::DoubleIndex:
    case tr_variant::NullIndex:
        return true;

    default:
        return false;
    }
}

// https://www.jsonrpc.org/specification#response_object
[[nodiscard]] tr_variant::Map build_response(Error::Code code, tr_variant id, tr_variant::Map&& body)
{
    if (!is_valid_id(id))
    {
        TR_ASSERT(false);
        id = nullptr;
    }

    auto ret = tr_variant::Map{ 3U };
    ret.try_emplace(TR_KEY_jsonrpc, Version);
    if (code == Error::SUCCESS)
    {
        ret.try_emplace(TR_KEY_result, std::move(body));
    }
    else
    {
        ret.try_emplace(TR_KEY_error, Error::build(code, std::move(body)));
    }
    ret.try_emplace(TR_KEY_id, std::move(id));

    return ret;
}
} // namespace
} // namespace JsonRpc

namespace
{
auto constexpr RecentlyActiveSeconds = time_t{ 60 };
auto constexpr RpcVersion = int64_t{ 18 };
auto constexpr RpcVersionMin = int64_t{ 14 };
auto constexpr RpcVersionSemver = "6.0.0"sv;

enum class TrFormat : uint8_t
{
    Object,
    Table
};

// ---

/* For functions that can't be immediately executed, like torrentAdd,
 * this is the callback data used to pass a response to the caller
 * when the task is complete */
struct tr_rpc_idle_data
{
    tr_variant id;
    tr_session* session = nullptr;
    tr_variant::Map args_out;
    tr_rpc_response_func callback;
};

void tr_rpc_idle_done_legacy(struct tr_rpc_idle_data* data, JsonRpc::Error::Code code, std::string_view result)
{
    // build the response
    auto response_map = tr_variant::Map{ 3U };
    response_map.try_emplace(TR_KEY_arguments, std::move(data->args_out));
    response_map.try_emplace(TR_KEY_result, std::empty(result) ? JsonRpc::Error::to_string(code) : result);
    if (auto& tag = data->id; tag.has_value())
    {
        response_map.try_emplace(TR_KEY_tag, std::move(tag));
    }

    // send the response back to the listener
    data->callback(data->session, tr_variant{ std::move(response_map) });
}

void tr_rpc_idle_done(struct tr_rpc_idle_data* data, JsonRpc::Error::Code code, std::string_view errmsg)
{
    using namespace JsonRpc;

    if (data->id.has_value())
    {
        auto const is_success = code == Error::SUCCESS;
        data->callback(
            data->session,
            build_response(
                code,
                std::move(data->id),
                is_success ? std::move(data->args_out) : Error::build_data(errmsg, std::move(data->args_out))));
    }
    else // notification
    {
        data->callback(data->session, {});
    }
}

using DoneCb = std::function<void(struct tr_rpc_idle_data* data, JsonRpc::Error::Code, std::string_view)>;

// ---

[[nodiscard]] auto getTorrents(tr_session* session, tr_variant::Map const& args)
{
    auto torrents_vec = std::vector<tr_torrent*>{};

    auto& torrents = session->torrents();
    torrents_vec.reserve(std::size(torrents));
    auto const add_torrent_from_var = [&torrents, &torrents_vec](tr_variant const& var)
    {
        tr_torrent* tor = nullptr;

        if (auto const val = var.value_if<int64_t>())
        {
            tor = torrents.get(*val);
        }

        if (auto const val = var.value_if<std::string_view>())
        {
            if (*val == tr_quark_get_string_view(TR_KEY_recently_active) ||
                *val == tr_quark_get_string_view(TR_KEY_recently_active_kebab))
            {
                auto const cutoff = tr_time() - RecentlyActiveSeconds;
                auto const recent = torrents.get_matching([cutoff](auto* walk) { return walk->has_changed_since(cutoff); });
                std::copy(std::begin(recent), std::end(recent), std::back_inserter(torrents_vec));
            }
            else
            {
                tor = torrents.get(*val);
            }
        }

        if (tor != nullptr)
        {
            torrents_vec.push_back(tor);
        }
    };

    if (auto const ids_iter = args.find(TR_KEY_ids); ids_iter != std::end(args))
    {
        auto const& ids_var = ids_iter->second;

        if (auto const* ids_vec = ids_var.get_if<tr_variant::Vector>(); ids_vec != nullptr)
        {
            std::for_each(std::begin(*ids_vec), std::end(*ids_vec), add_torrent_from_var);
        }
        else
        {
            add_torrent_from_var(ids_var);
        }
    }
    else // all of them
    {
        torrents_vec = torrents.get_all();
    }

    return torrents_vec;
}

void notifyBatchQueueChange(tr_session* session, std::vector<tr_torrent*> const& torrents)
{
    for (auto* tor : torrents)
    {
        session->rpcNotify(TR_RPC_TORRENT_CHANGED, tor);
    }

    session->rpcNotify(TR_RPC_SESSION_QUEUE_POSITIONS_CHANGED);
}

[[nodiscard]] std::pair<JsonRpc::Error::Code, std::string> queueMoveTop(
    tr_session* session,
    tr_variant::Map const& args_in,
    tr_variant::Map& /*args_out*/)
{
    auto const torrents = getTorrents(session, args_in);
    tr_torrentsQueueMoveTop(std::data(torrents), std::size(torrents));
    notifyBatchQueueChange(session, torrents);
    return { JsonRpc::Error::SUCCESS, {} };
}

[[nodiscard]] std::pair<JsonRpc::Error::Code, std::string> queueMoveUp(
    tr_session* session,
    tr_variant::Map const& args_in,
    tr_variant::Map& /*args_out*/)
{
    auto const torrents = getTorrents(session, args_in);
    tr_torrentsQueueMoveUp(std::data(torrents), std::size(torrents));
    notifyBatchQueueChange(session, torrents);
    return { JsonRpc::Error::SUCCESS, {} };
}

[[nodiscard]] std::pair<JsonRpc::Error::Code, std::string> queueMoveDown(
    tr_session* session,
    tr_variant::Map const& args_in,
    tr_variant::Map& /*args_out*/)
{
    auto const torrents = getTorrents(session, args_in);
    tr_torrentsQueueMoveDown(std::data(torrents), std::size(torrents));
    notifyBatchQueueChange(session, torrents);
    return { JsonRpc::Error::SUCCESS, {} };
}

[[nodiscard]] std::pair<JsonRpc::Error::Code, std::string> queueMoveBottom(
    tr_session* session,
    tr_variant::Map const& args_in,
    tr_variant::Map& /*args_out*/)
{
    auto const torrents = getTorrents(session, args_in);
    tr_torrentsQueueMoveBottom(std::data(torrents), std::size(torrents));
    notifyBatchQueueChange(session, torrents);
    return { JsonRpc::Error::SUCCESS, {} };
}

[[nodiscard]] std::pair<JsonRpc::Error::Code, std::string> torrentStart(
    tr_session* session,
    tr_variant::Map const& args_in,
    tr_variant::Map& /*args_out*/)
{
    auto torrents = getTorrents(session, args_in);
    std::sort(std::begin(torrents), std::end(torrents), tr_torrent::CompareQueuePosition);
    for (auto* tor : torrents)
    {
        if (!tor->is_running())
        {
            tr_torrentStart(tor);
            session->rpcNotify(TR_RPC_TORRENT_STARTED, tor);
        }
    }

    return { JsonRpc::Error::SUCCESS, {} };
}

[[nodiscard]] std::pair<JsonRpc::Error::Code, std::string> torrentStartNow(
    tr_session* session,
    tr_variant::Map const& args_in,
    tr_variant::Map& /*args_out*/)
{
    auto torrents = getTorrents(session, args_in);
    std::sort(std::begin(torrents), std::end(torrents), tr_torrent::CompareQueuePosition);
    for (auto* tor : torrents)
    {
        if (!tor->is_running())
        {
            tr_torrentStartNow(tor);
            session->rpcNotify(TR_RPC_TORRENT_STARTED, tor);
        }
    }

    return { JsonRpc::Error::SUCCESS, {} };
}

[[nodiscard]] std::pair<JsonRpc::Error::Code, std::string> torrentStop(
    tr_session* session,
    tr_variant::Map const& args_in,
    tr_variant::Map& /*args_out*/)
{
    for (auto* tor : getTorrents(session, args_in))
    {
        if (tor->activity() != TR_STATUS_STOPPED)
        {
            tor->stop_soon();
            session->rpcNotify(TR_RPC_TORRENT_STOPPED, tor);
        }
    }

    return { JsonRpc::Error::SUCCESS, {} };
}

[[nodiscard]] std::pair<JsonRpc::Error::Code, std::string> torrentRemove(
    tr_session* session,
    tr_variant::Map const& args_in,
    tr_variant::Map& /*args_out*/)
{
    auto const delete_flag = args_in.value_if<bool>({ TR_KEY_delete_local_data, TR_KEY_delete_local_data_kebab })
                                 .value_or(false);
    auto const type = delete_flag ? TR_RPC_TORRENT_TRASHING : TR_RPC_TORRENT_REMOVING;

    for (auto* tor : getTorrents(session, args_in))
    {
        if (auto const status = session->rpcNotify(type, tor); (status & TR_RPC_NOREMOVE) == 0)
        {
            tr_torrentRemove(tor, delete_flag, nullptr, nullptr, nullptr, nullptr);
        }
    }

    return { JsonRpc::Error::SUCCESS, {} };
}

[[nodiscard]] std::pair<JsonRpc::Error::Code, std::string> torrentReannounce(
    tr_session* session,
    tr_variant::Map const& args_in,
    tr_variant::Map& /*args_out*/)
{
    for (auto* tor : getTorrents(session, args_in))
    {
        if (tr_torrentCanManualUpdate(tor))
        {
            tr_torrentManualUpdate(tor);
            session->rpcNotify(TR_RPC_TORRENT_CHANGED, tor);
        }
    }

    return { JsonRpc::Error::SUCCESS, {} };
}

[[nodiscard]] std::pair<JsonRpc::Error::Code, std::string> torrentVerify(
    tr_session* session,
    tr_variant::Map const& args_in,
    tr_variant::Map& /*args_out*/)
{
    for (auto* tor : getTorrents(session, args_in))
    {
        tr_torrentVerify(tor);
        session->rpcNotify(TR_RPC_TORRENT_CHANGED, tor);
    }

    return { JsonRpc::Error::SUCCESS, {} };
}

// ---

namespace make_torrent_field_helpers
{
[[nodiscard]] auto make_file_wanted_vec(tr_torrent const& tor)
{
    auto const n_files = tor.file_count();
    auto vec = tr_variant::Vector{};
    vec.reserve(n_files);
    for (tr_file_index_t idx = 0U; idx != n_files; ++idx)
    {
        vec.emplace_back(tr_torrentFile(&tor, idx).wanted ? 1 : 0);
    }
    return tr_variant{ std::move(vec) };
}

[[nodiscard]] auto make_labels_vec(tr_torrent const& tor)
{
    auto const& labels = tor.labels();
    auto const n_labels = std::size(labels);
    auto vec = tr_variant::Vector{};
    vec.reserve(n_labels);
    for (auto const& label : labels)
    {
        vec.emplace_back(tr_variant::unmanaged_string(label.sv()));
    }
    return tr_variant{ std::move(vec) };
}

[[nodiscard]] auto make_file_priorities_vec(tr_torrent const& tor)
{
    auto const n_files = tor.file_count();
    auto vec = tr_variant::Vector{};
    vec.reserve(n_files);
    for (tr_file_index_t idx = 0U; idx != n_files; ++idx)
    {
        vec.emplace_back(tr_torrentFile(&tor, idx).priority);
    }
    return tr_variant{ std::move(vec) };
}

[[nodiscard]] auto make_file_stats_vec(tr_torrent const& tor)
{
    auto const n_files = tor.file_count();
    auto vec = tr_variant::Vector{};
    vec.reserve(n_files);
    for (tr_file_index_t idx = 0U; idx != n_files; ++idx)
    {
        auto const file = tr_torrentFile(&tor, idx);
        auto stats_map = tr_variant::Map{ 3U };
        stats_map.try_emplace(TR_KEY_bytes_completed, file.have);
        stats_map.try_emplace(TR_KEY_bytes_completed_camel, file.have);
        stats_map.try_emplace(TR_KEY_priority, file.priority);
        stats_map.try_emplace(TR_KEY_wanted, file.wanted);
        vec.emplace_back(std::move(stats_map));
    }
    return tr_variant{ std::move(vec) };
}

[[nodiscard]] auto make_bytes_completed_vec(tr_torrent const& tor)
{
    auto const n_files = tor.file_count();
    auto vec = tr_variant::Vector{};
    vec.reserve(n_files);
    tr_logAddDebug("Running bytes completed");
    for (tr_file_index_t idx = 0U; idx != n_files; ++idx)
    {
        vec.emplace_back(tr_torrentFile(&tor, idx).have);
    }
    return tr_variant{ std::move(vec) };
}

[[nodiscard]] auto make_file_vec(tr_torrent const& tor)
{
    auto const n_files = tor.file_count();
    auto vec = tr_variant::Vector{};
    vec.reserve(n_files);
    for (tr_file_index_t idx = 0U; idx != n_files; ++idx)
    {
        auto const file = tr_torrentFile(&tor, idx);
        auto file_map = tr_variant::Map{ 5U };
        file_map.try_emplace(TR_KEY_begin_piece, file.beginPiece);
        file_map.try_emplace(TR_KEY_bytes_completed, file.have);
        file_map.try_emplace(TR_KEY_bytes_completed_camel, file.have);
        file_map.try_emplace(TR_KEY_end_piece, file.endPiece);
        file_map.try_emplace(TR_KEY_length, file.length);
        file_map.try_emplace(TR_KEY_name, file.name);
        vec.emplace_back(std::move(file_map));
    }
    return tr_variant{ std::move(vec) };
}

[[nodiscard]] auto make_webseed_vec(tr_torrent const& tor)
{
    auto const n_webseeds = tor.webseed_count();
    auto vec = tr_variant::Vector{};
    for (size_t idx = 0U; idx != n_webseeds; ++idx)
    {
        vec.emplace_back(tor.webseed(idx));
    }
    return tr_variant{ std::move(vec) };
}

[[nodiscard]] auto make_tracker_vec(tr_torrent const& tor)
{
    auto const& trackers = tor.announce_list();
    auto const n_trackers = std::size(trackers);
    auto vec = tr_variant::Vector{};
    vec.reserve(n_trackers);
    for (auto const& tracker : trackers)
    {
        auto tracker_map = tr_variant::Map{ 5U };
        tracker_map.try_emplace(TR_KEY_announce, tr_variant::unmanaged_string(tracker.announce.sv()));
        tracker_map.try_emplace(TR_KEY_id, tracker.id);
        tracker_map.try_emplace(TR_KEY_scrape, tr_variant::unmanaged_string(tracker.scrape.sv()));
        tracker_map.try_emplace(TR_KEY_sitename, tr_variant::unmanaged_string(tracker.announce_parsed.sitename));
        tracker_map.try_emplace(TR_KEY_tier, tracker.tier);
        vec.emplace_back(std::move(tracker_map));
    }
    return tr_variant{ std::move(vec) };
}

[[nodiscard]] auto make_tracker_stats_vec(tr_torrent const& tor)
{
    auto const n_trackers = tr_torrentTrackerCount(&tor);
    auto vec = tr_variant::Vector{};
    vec.reserve(n_trackers);
    for (size_t idx = 0U; idx != n_trackers; ++idx)
    {
        auto const tracker = tr_torrentTracker(&tor, idx);
        auto stats_map = tr_variant::Map{ 28U };
        stats_map.try_emplace(TR_KEY_announce, tracker.announce);
        stats_map.try_emplace(TR_KEY_announce_state, tracker.announceState);
        stats_map.try_emplace(TR_KEY_announce_state_camel, tracker.announceState);
        stats_map.try_emplace(TR_KEY_download_count, tracker.downloadCount);
        stats_map.try_emplace(TR_KEY_download_count_camel, tracker.downloadCount);
        stats_map.try_emplace(TR_KEY_downloader_count, tracker.downloader_count);
        stats_map.try_emplace(TR_KEY_has_announced, tracker.hasAnnounced);
        stats_map.try_emplace(TR_KEY_has_announced_camel, tracker.hasAnnounced);
        stats_map.try_emplace(TR_KEY_has_scraped, tracker.hasScraped);
        stats_map.try_emplace(TR_KEY_has_scraped_camel, tracker.hasScraped);
        stats_map.try_emplace(TR_KEY_host, tracker.host_and_port);
        stats_map.try_emplace(TR_KEY_id, tracker.id);
        stats_map.try_emplace(TR_KEY_is_backup, tracker.isBackup);
        stats_map.try_emplace(TR_KEY_is_backup_camel, tracker.isBackup);
        stats_map.try_emplace(TR_KEY_last_announce_peer_count, tracker.lastAnnouncePeerCount);
        stats_map.try_emplace(TR_KEY_last_announce_peer_count_camel, tracker.lastAnnouncePeerCount);
        stats_map.try_emplace(TR_KEY_last_announce_result, tracker.lastAnnounceResult);
        stats_map.try_emplace(TR_KEY_last_announce_result_camel, tracker.lastAnnounceResult);
        stats_map.try_emplace(TR_KEY_last_announce_start_time, tracker.lastAnnounceStartTime);
        stats_map.try_emplace(TR_KEY_last_announce_start_time_camel, tracker.lastAnnounceStartTime);
        stats_map.try_emplace(TR_KEY_last_announce_succeeded, tracker.lastAnnounceSucceeded);
        stats_map.try_emplace(TR_KEY_last_announce_succeeded_camel, tracker.lastAnnounceSucceeded);
        stats_map.try_emplace(TR_KEY_last_announce_time, tracker.lastAnnounceTime);
        stats_map.try_emplace(TR_KEY_last_announce_time_camel, tracker.lastAnnounceTime);
        stats_map.try_emplace(TR_KEY_last_announce_timed_out, tracker.lastAnnounceTimedOut);
        stats_map.try_emplace(TR_KEY_last_announce_timed_out_camel, tracker.lastAnnounceTimedOut);
        stats_map.try_emplace(TR_KEY_last_scrape_result, tracker.lastScrapeResult);
        stats_map.try_emplace(TR_KEY_last_scrape_result_camel, tracker.lastScrapeResult);
        stats_map.try_emplace(TR_KEY_last_scrape_start_time, tracker.lastScrapeStartTime);
        stats_map.try_emplace(TR_KEY_last_scrape_start_time_camel, tracker.lastScrapeStartTime);
        stats_map.try_emplace(TR_KEY_last_scrape_succeeded, tracker.lastScrapeSucceeded);
        stats_map.try_emplace(TR_KEY_last_scrape_succeeded_camel, tracker.lastScrapeSucceeded);
        stats_map.try_emplace(TR_KEY_last_scrape_time, tracker.lastScrapeTime);
        stats_map.try_emplace(TR_KEY_last_scrape_time_camel, tracker.lastScrapeTime);
        stats_map.try_emplace(TR_KEY_last_scrape_timed_out, tracker.lastScrapeTimedOut);
        stats_map.try_emplace(TR_KEY_last_scrape_timed_out_camel, tracker.lastScrapeTimedOut);
        stats_map.try_emplace(TR_KEY_leecher_count, tracker.leecherCount);
        stats_map.try_emplace(TR_KEY_leecher_count_camel, tracker.leecherCount);
        stats_map.try_emplace(TR_KEY_next_announce_time, tracker.nextAnnounceTime);
        stats_map.try_emplace(TR_KEY_next_announce_time_camel, tracker.nextAnnounceTime);
        stats_map.try_emplace(TR_KEY_next_scrape_time, tracker.nextScrapeTime);
        stats_map.try_emplace(TR_KEY_next_scrape_time_camel, tracker.nextScrapeTime);
        stats_map.try_emplace(TR_KEY_scrape, tracker.scrape);
        stats_map.try_emplace(TR_KEY_scrape_state, tracker.scrapeState);
        stats_map.try_emplace(TR_KEY_scrape_state_camel, tracker.scrapeState);
        stats_map.try_emplace(TR_KEY_seeder_count, tracker.seederCount);
        stats_map.try_emplace(TR_KEY_seeder_count_camel, tracker.seederCount);
        stats_map.try_emplace(TR_KEY_sitename, tracker.sitename);
        stats_map.try_emplace(TR_KEY_tier, tracker.tier);
        vec.emplace_back(std::move(stats_map));
    }
    return tr_variant{ std::move(vec) };
}

[[nodiscard]] auto make_peer_vec(tr_torrent const& tor)
{
    auto n_peers = size_t{};
    auto* const peers = tr_torrentPeers(&tor, &n_peers);
    auto peers_vec = tr_variant::Vector{};
    peers_vec.reserve(n_peers);
    for (size_t idx = 0U; idx != n_peers; ++idx)
    {
        auto const& peer = peers[idx];
        auto peer_map = tr_variant::Map{ 16U };
        peer_map.try_emplace(TR_KEY_address, peer.addr);
        peer_map.try_emplace(TR_KEY_client_is_choked, peer.clientIsChoked);
        peer_map.try_emplace(TR_KEY_client_is_choked_camel, peer.clientIsChoked);
        peer_map.try_emplace(TR_KEY_client_is_interested, peer.clientIsInterested);
        peer_map.try_emplace(TR_KEY_client_is_interested_camel, peer.clientIsInterested);
        peer_map.try_emplace(TR_KEY_client_name, peer.client);
        peer_map.try_emplace(TR_KEY_client_name_camel, peer.client);
        peer_map.try_emplace(TR_KEY_peer_id, tr_base64_encode(std::string_view{ peer.peer_id.data(), peer.peer_id.size() }));
        peer_map.try_emplace(TR_KEY_flag_str, peer.flagStr);
        peer_map.try_emplace(TR_KEY_flag_str_camel, peer.flagStr);
        peer_map.try_emplace(TR_KEY_is_downloading_from, peer.isDownloadingFrom);
        peer_map.try_emplace(TR_KEY_is_downloading_from_camel, peer.isDownloadingFrom);
        peer_map.try_emplace(TR_KEY_is_encrypted, peer.isEncrypted);
        peer_map.try_emplace(TR_KEY_is_encrypted_camel, peer.isEncrypted);
        peer_map.try_emplace(TR_KEY_is_incoming, peer.isIncoming);
        peer_map.try_emplace(TR_KEY_is_incoming_camel, peer.isIncoming);
        peer_map.try_emplace(TR_KEY_is_utp, peer.isUTP);
        peer_map.try_emplace(TR_KEY_is_utp_camel, peer.isUTP);
        peer_map.try_emplace(TR_KEY_is_uploading_to, peer.isUploadingTo);
        peer_map.try_emplace(TR_KEY_is_uploading_to_camel, peer.isUploadingTo);
        peer_map.try_emplace(TR_KEY_peer_is_choked, peer.peerIsChoked);
        peer_map.try_emplace(TR_KEY_peer_is_choked_camel, peer.peerIsChoked);
        peer_map.try_emplace(TR_KEY_peer_is_interested, peer.peerIsInterested);
        peer_map.try_emplace(TR_KEY_peer_is_interested_camel, peer.peerIsInterested);
        peer_map.try_emplace(TR_KEY_port, peer.port);
        peer_map.try_emplace(TR_KEY_progress, peer.progress);
        peer_map.try_emplace(TR_KEY_rate_to_client, Speed{ peer.rateToClient_KBps, Speed::Units::KByps }.base_quantity());
        peer_map.try_emplace(TR_KEY_rate_to_client_camel, Speed{ peer.rateToClient_KBps, Speed::Units::KByps }.base_quantity());
        peer_map.try_emplace(TR_KEY_rate_to_peer, Speed{ peer.rateToPeer_KBps, Speed::Units::KByps }.base_quantity());
        peer_map.try_emplace(TR_KEY_rate_to_peer_camel, Speed{ peer.rateToPeer_KBps, Speed::Units::KByps }.base_quantity());
        peer_map.try_emplace(TR_KEY_bytes_to_peer, peer.bytes_to_peer);
        peer_map.try_emplace(TR_KEY_bytes_to_client, peer.bytes_to_client);
        peers_vec.emplace_back(std::move(peer_map));
    }
    tr_torrentPeersFree(peers, n_peers);
    return tr_variant{ std::move(peers_vec) };
}

[[nodiscard]] auto make_peer_counts_map(tr_stat const& st)
{
    auto const& from = st.peersFrom;
    auto peer_counts_map = tr_variant::Map{ 7U };
    peer_counts_map.try_emplace(TR_KEY_from_cache, from[TR_PEER_FROM_RESUME]);
    peer_counts_map.try_emplace(TR_KEY_from_cache_camel, from[TR_PEER_FROM_RESUME]);
    peer_counts_map.try_emplace(TR_KEY_from_dht, from[TR_PEER_FROM_DHT]);
    peer_counts_map.try_emplace(TR_KEY_from_dht_camel, from[TR_PEER_FROM_DHT]);
    peer_counts_map.try_emplace(TR_KEY_from_incoming, from[TR_PEER_FROM_INCOMING]);
    peer_counts_map.try_emplace(TR_KEY_from_incoming_camel, from[TR_PEER_FROM_INCOMING]);
    peer_counts_map.try_emplace(TR_KEY_from_lpd, from[TR_PEER_FROM_LPD]);
    peer_counts_map.try_emplace(TR_KEY_from_lpd_camel, from[TR_PEER_FROM_LPD]);
    peer_counts_map.try_emplace(TR_KEY_from_ltep, from[TR_PEER_FROM_LTEP]);
    peer_counts_map.try_emplace(TR_KEY_from_ltep_camel, from[TR_PEER_FROM_LTEP]);
    peer_counts_map.try_emplace(TR_KEY_from_pex, from[TR_PEER_FROM_PEX]);
    peer_counts_map.try_emplace(TR_KEY_from_pex_camel, from[TR_PEER_FROM_PEX]);
    peer_counts_map.try_emplace(TR_KEY_from_tracker, from[TR_PEER_FROM_TRACKER]);
    peer_counts_map.try_emplace(TR_KEY_from_tracker_camel, from[TR_PEER_FROM_TRACKER]);
    return tr_variant{ std::move(peer_counts_map) };
}

[[nodiscard]] auto make_piece_availability_vec(tr_torrent const& tor)
{
    auto const n_pieces = tor.piece_count();
    auto vec = tr_variant::Vector{};
    vec.reserve(n_pieces);
    for (tr_piece_index_t idx = 0U; idx != n_pieces; ++idx)
    {
        vec.emplace_back(tr_peerMgrPieceAvailability(&tor, idx));
    }
    return tr_variant{ std::move(vec) };
}

[[nodiscard]] auto make_piece_bitfield(tr_torrent const& tor)
{
    if (tor.has_metainfo())
    {
        auto const bytes = tor.create_piece_bitfield();
        return tr_variant{ tr_base64_encode({ reinterpret_cast<char const*>(std::data(bytes)), std::size(bytes) }) };
    }

    return tr_variant::unmanaged_string(""sv);
}
} // namespace make_torrent_field_helpers

[[nodiscard]] auto constexpr isSupportedTorrentGetField(tr_quark key)
{
    switch (key)
    {
    case TR_KEY_activity_date:
    case TR_KEY_activity_date_camel:
    case TR_KEY_added_date:
    case TR_KEY_added_date_camel:
    case TR_KEY_availability:
    case TR_KEY_bandwidth_priority:
    case TR_KEY_bandwidth_priority_camel:
    case TR_KEY_bytes_completed:
    case TR_KEY_bytes_completed_camel:
    case TR_KEY_comment:
    case TR_KEY_corrupt_ever:
    case TR_KEY_corrupt_ever_camel:
    case TR_KEY_creator:
    case TR_KEY_date_created:
    case TR_KEY_date_created_camel:
    case TR_KEY_desired_available:
    case TR_KEY_desired_available_camel:
    case TR_KEY_done_date:
    case TR_KEY_done_date_camel:
    case TR_KEY_download_dir:
    case TR_KEY_download_dir_camel:
    case TR_KEY_download_limit:
    case TR_KEY_download_limit_camel:
    case TR_KEY_download_limited:
    case TR_KEY_download_limited_camel:
    case TR_KEY_downloaded_ever:
    case TR_KEY_downloaded_ever_camel:
    case TR_KEY_edit_date:
    case TR_KEY_edit_date_camel:
    case TR_KEY_error:
    case TR_KEY_error_string:
    case TR_KEY_error_string_camel:
    case TR_KEY_eta:
    case TR_KEY_eta_idle:
    case TR_KEY_eta_idle_camel:
    case TR_KEY_file_stats:
    case TR_KEY_file_stats_camel:
    case TR_KEY_file_count:
    case TR_KEY_file_count_kebab:
    case TR_KEY_files:
    case TR_KEY_group:
    case TR_KEY_hash_string:
    case TR_KEY_hash_string_camel:
    case TR_KEY_have_unchecked:
    case TR_KEY_have_unchecked_camel:
    case TR_KEY_have_valid:
    case TR_KEY_have_valid_camel:
    case TR_KEY_honors_session_limits:
    case TR_KEY_honors_session_limits_camel:
    case TR_KEY_id:
    case TR_KEY_is_finished:
    case TR_KEY_is_finished_camel:
    case TR_KEY_is_private:
    case TR_KEY_is_private_camel:
    case TR_KEY_is_stalled:
    case TR_KEY_is_stalled_camel:
    case TR_KEY_labels:
    case TR_KEY_left_until_done:
    case TR_KEY_left_until_done_camel:
    case TR_KEY_magnet_link:
    case TR_KEY_magnet_link_camel:
    case TR_KEY_manual_announce_time:
    case TR_KEY_manual_announce_time_camel:
    case TR_KEY_max_connected_peers:
    case TR_KEY_max_connected_peers_camel:
    case TR_KEY_metadata_percent_complete:
    case TR_KEY_metadata_percent_complete_camel:
    case TR_KEY_name:
    case TR_KEY_peer_limit:
    case TR_KEY_peer_limit_kebab:
    case TR_KEY_peers:
    case TR_KEY_peers_connected:
    case TR_KEY_peers_connected_camel:
    case TR_KEY_peers_from:
    case TR_KEY_peers_from_camel:
    case TR_KEY_peers_getting_from_us:
    case TR_KEY_peers_getting_from_us_camel:
    case TR_KEY_peers_sending_to_us:
    case TR_KEY_peers_sending_to_us_camel:
    case TR_KEY_percent_complete:
    case TR_KEY_percent_complete_camel:
    case TR_KEY_percent_done:
    case TR_KEY_percent_done_camel:
    case TR_KEY_piece_count:
    case TR_KEY_piece_count_camel:
    case TR_KEY_piece_size:
    case TR_KEY_piece_size_camel:
    case TR_KEY_pieces:
    case TR_KEY_primary_mime_type:
    case TR_KEY_primary_mime_type_kebab:
    case TR_KEY_priorities:
    case TR_KEY_queue_position:
    case TR_KEY_queue_position_camel:
    case TR_KEY_rate_download:
    case TR_KEY_rate_download_camel:
    case TR_KEY_rate_upload:
    case TR_KEY_rate_upload_camel:
    case TR_KEY_recheck_progress:
    case TR_KEY_recheck_progress_camel:
    case TR_KEY_seconds_downloading:
    case TR_KEY_seconds_downloading_camel:
    case TR_KEY_seconds_seeding:
    case TR_KEY_seconds_seeding_camel:
    case TR_KEY_seed_idle_limit:
    case TR_KEY_seed_idle_limit_camel:
    case TR_KEY_seed_idle_mode:
    case TR_KEY_seed_idle_mode_camel:
    case TR_KEY_seed_ratio_limit:
    case TR_KEY_seed_ratio_limit_camel:
    case TR_KEY_seed_ratio_mode:
    case TR_KEY_seed_ratio_mode_camel:
    case TR_KEY_sequential_download:
    case TR_KEY_sequential_download_from_piece:
    case TR_KEY_size_when_done:
    case TR_KEY_size_when_done_camel:
    case TR_KEY_source:
    case TR_KEY_start_date:
    case TR_KEY_start_date_camel:
    case TR_KEY_status:
    case TR_KEY_torrent_file:
    case TR_KEY_torrent_file_camel:
    case TR_KEY_total_size:
    case TR_KEY_total_size_camel:
    case TR_KEY_tracker_list:
    case TR_KEY_tracker_list_camel:
    case TR_KEY_tracker_stats:
    case TR_KEY_tracker_stats_camel:
    case TR_KEY_trackers:
    case TR_KEY_upload_limit:
    case TR_KEY_upload_limit_camel:
    case TR_KEY_upload_limited:
    case TR_KEY_upload_limited_camel:
    case TR_KEY_upload_ratio:
    case TR_KEY_upload_ratio_camel:
    case TR_KEY_uploaded_ever:
    case TR_KEY_uploaded_ever_camel:
    case TR_KEY_wanted:
    case TR_KEY_webseeds:
    case TR_KEY_webseeds_sending_to_us:
    case TR_KEY_webseeds_sending_to_us_camel:
        return true;

    default:
        return false;
    }
}

[[nodiscard]] tr_variant make_torrent_field(tr_torrent const& tor, tr_stat const& st, tr_quark key)
{
    using namespace make_torrent_field_helpers;

    TR_ASSERT(isSupportedTorrentGetField(key));

    // clang-format off
    switch (key)
    {
    case TR_KEY_activity_date:
    case TR_KEY_activity_date_camel:
        return st.activityDate;
    case TR_KEY_added_date:
    case TR_KEY_added_date_camel:
        return st.addedDate;
    case TR_KEY_availability: return make_piece_availability_vec(tor);
    case TR_KEY_bandwidth_priority:
    case TR_KEY_bandwidth_priority_camel:
        return tor.get_priority();
    case TR_KEY_bytes_completed: return make_bytes_completed_vec(tor);
    case TR_KEY_bytes_completed_camel: return make_bytes_completed_vec(tor);
    case TR_KEY_comment: return tor.comment();
    case TR_KEY_corrupt_ever:
    case TR_KEY_corrupt_ever_camel:
        return st.corruptEver;
    case TR_KEY_creator: return tor.creator();
    case TR_KEY_date_created:
    case TR_KEY_date_created_camel:
        return tor.date_created();
    case TR_KEY_desired_available:
    case TR_KEY_desired_available_camel:
        return st.desiredAvailable;
    case TR_KEY_done_date:
    case TR_KEY_done_date_camel:
        return st.doneDate;
    case TR_KEY_download_dir:
    case TR_KEY_download_dir_camel:
        return tr_variant::unmanaged_string(tor.download_dir().sv());
    case TR_KEY_download_limit:
    case TR_KEY_download_limit_camel:
        return tr_torrentGetSpeedLimit_KBps(&tor, TR_DOWN);
    case TR_KEY_download_limited:
    case TR_KEY_download_limited_camel:
        return tor.uses_speed_limit(TR_DOWN);
    case TR_KEY_downloaded_ever:
    case TR_KEY_downloaded_ever_camel:
        return st.downloadedEver;
    case TR_KEY_edit_date:
    case TR_KEY_edit_date_camel:
        return st.editDate;
    case TR_KEY_error: return st.error;
    case TR_KEY_error_string:
    case TR_KEY_error_string_camel:
        return st.errorString;
    case TR_KEY_eta: return st.eta;
    case TR_KEY_eta_idle:
    case TR_KEY_eta_idle_camel:
        return st.etaIdle;
    case TR_KEY_file_stats:
    case TR_KEY_file_stats_camel:
        return make_file_stats_vec(tor);
    case TR_KEY_file_count:
    case TR_KEY_file_count_kebab:
        return tor.file_count();
    case TR_KEY_files: return make_file_vec(tor);
    case TR_KEY_group: return tr_variant::unmanaged_string(tor.bandwidth_group().sv());
    case TR_KEY_hash_string:
    case TR_KEY_hash_string_camel:
        return tr_variant::unmanaged_string(tor.info_hash_string().sv());
    case TR_KEY_have_unchecked:
    case TR_KEY_have_unchecked_camel:
        return st.haveUnchecked;
    case TR_KEY_have_valid:
    case TR_KEY_have_valid_camel:
        return st.haveValid;
    case TR_KEY_honors_session_limits:
    case TR_KEY_honors_session_limits_camel:
        return tor.uses_session_limits();
    case TR_KEY_id: return st.id;
    case TR_KEY_is_finished:
    case TR_KEY_is_finished_camel:
        return st.finished;
    case TR_KEY_is_private:
    case TR_KEY_is_private_camel:
        return tor.is_private();
    case TR_KEY_is_stalled:
    case TR_KEY_is_stalled_camel:
        return st.isStalled;
    case TR_KEY_labels: return make_labels_vec(tor);
    case TR_KEY_left_until_done:
    case TR_KEY_left_until_done_camel:
        return st.leftUntilDone;
    case TR_KEY_magnet_link:
    case TR_KEY_magnet_link_camel:
        return tor.magnet();
    case TR_KEY_manual_announce_time:
    case TR_KEY_manual_announce_time_camel:
        return tr_announcerNextManualAnnounce(&tor);
    case TR_KEY_max_connected_peers:
    case TR_KEY_max_connected_peers_camel:
        return tor.peer_limit();
    case TR_KEY_metadata_percent_complete:
    case TR_KEY_metadata_percent_complete_camel:
        return st.metadataPercentComplete;
    case TR_KEY_name: return tor.name();
    case TR_KEY_peer_limit:
    case TR_KEY_peer_limit_kebab:
        return tor.peer_limit();
    case TR_KEY_peers: return make_peer_vec(tor);
    case TR_KEY_peers_connected:
    case TR_KEY_peers_connected_camel:
        return st.peersConnected;
    case TR_KEY_peers_from:
    case TR_KEY_peers_from_camel:
        return make_peer_counts_map(st);
    case TR_KEY_peers_getting_from_us:
    case TR_KEY_peers_getting_from_us_camel:
        return st.peersGettingFromUs;
    case TR_KEY_peers_sending_to_us:
    case TR_KEY_peers_sending_to_us_camel:
        return st.peersSendingToUs;
    case TR_KEY_percent_complete:
    case TR_KEY_percent_complete_camel:
        return st.percentComplete;
    case TR_KEY_percent_done:
    case TR_KEY_percent_done_camel:
        return st.percentDone;
    case TR_KEY_piece_count:
    case TR_KEY_piece_count_camel:
        return tor.piece_count();
    case TR_KEY_piece_size:
    case TR_KEY_piece_size_camel:
        return tor.piece_size();
    case TR_KEY_pieces: return make_piece_bitfield(tor);
    case TR_KEY_primary_mime_type:
    case TR_KEY_primary_mime_type_kebab:
        return tr_variant::unmanaged_string(tor.primary_mime_type());
    case TR_KEY_priorities: return make_file_priorities_vec(tor);
    case TR_KEY_queue_position:
    case TR_KEY_queue_position_camel:
        return st.queuePosition;
    case TR_KEY_rate_download:
    case TR_KEY_rate_download_camel:
        return Speed{ st.pieceDownloadSpeed_KBps, Speed::Units::KByps }.base_quantity();
    case TR_KEY_rate_upload:
    case TR_KEY_rate_upload_camel:
        return Speed{ st.pieceUploadSpeed_KBps, Speed::Units::KByps }.base_quantity();
    case TR_KEY_recheck_progress:
    case TR_KEY_recheck_progress_camel:
        return st.recheckProgress;
    case TR_KEY_seconds_downloading:
    case TR_KEY_seconds_downloading_camel:
        return st.secondsDownloading;
    case TR_KEY_seconds_seeding:
    case TR_KEY_seconds_seeding_camel:
        return st.secondsSeeding;
    case TR_KEY_seed_idle_limit:
    case TR_KEY_seed_idle_limit_camel:
        return tor.idle_limit_minutes();
    case TR_KEY_seed_idle_mode:
    case TR_KEY_seed_idle_mode_camel:
        return tor.idle_limit_mode();
    case TR_KEY_seed_ratio_limit:
    case TR_KEY_seed_ratio_limit_camel:
        return tor.seed_ratio();
    case TR_KEY_seed_ratio_mode:
    case TR_KEY_seed_ratio_mode_camel:
        return tor.seed_ratio_mode();
    case TR_KEY_sequential_download: return tor.is_sequential_download();
    case TR_KEY_sequential_download_from_piece: return tor.sequential_download_from_piece();
    case TR_KEY_size_when_done:
    case TR_KEY_size_when_done_camel:
        return st.sizeWhenDone;
    case TR_KEY_source: return tor.source();
    case TR_KEY_start_date:
    case TR_KEY_start_date_camel:
        return st.startDate;
    case TR_KEY_status: return st.activity;
    case TR_KEY_torrent_file:
    case TR_KEY_torrent_file_camel:
        return tor.torrent_file();
    case TR_KEY_total_size:
    case TR_KEY_total_size_camel:
        return tor.total_size();
    case TR_KEY_tracker_list:
    case TR_KEY_tracker_list_camel:
        return tor.announce_list().to_string();
    case TR_KEY_tracker_stats:
    case TR_KEY_tracker_stats_camel:
        return make_tracker_stats_vec(tor);
    case TR_KEY_trackers: return make_tracker_vec(tor);
    case TR_KEY_upload_limit:
    case TR_KEY_upload_limit_camel:
        return tr_torrentGetSpeedLimit_KBps(&tor, TR_UP);
    case TR_KEY_upload_limited:
    case TR_KEY_upload_limited_camel:
        return tor.uses_speed_limit(TR_UP);
    case TR_KEY_upload_ratio:
    case TR_KEY_upload_ratio_camel:
        return st.ratio;
    case TR_KEY_uploaded_ever:
    case TR_KEY_uploaded_ever_camel:
        return st.uploadedEver;
    case TR_KEY_wanted: return make_file_wanted_vec(tor);
    case TR_KEY_webseeds: return make_webseed_vec(tor);
    case TR_KEY_webseeds_sending_to_us:
    case TR_KEY_webseeds_sending_to_us_camel:
        return st.webseedsSendingToUs;
    default: return tr_variant{};
    }
    // clang-format on
}

[[nodiscard]] auto make_torrent_info_map(tr_torrent* const tor, tr_quark const* const fields, size_t const field_count)
{
    auto const* const st = tr_torrentStat(tor);
    auto info_map = tr_variant::Map{ field_count };
    for (size_t i = 0; i < field_count; ++i)
    {
        info_map.try_emplace(fields[i], make_torrent_field(*tor, *st, fields[i]));
    }
    return tr_variant{ std::move(info_map) };
}

[[nodiscard]] auto make_torrent_info_vec(tr_torrent* const tor, tr_quark const* const fields, size_t const field_count)
{
    auto const* const st = tr_torrentStat(tor);
    auto info_vec = tr_variant::Vector{};
    info_vec.reserve(field_count);
    for (size_t i = 0; i < field_count; ++i)
    {
        info_vec.emplace_back(make_torrent_field(*tor, *st, fields[i]));
    }
    return tr_variant{ std::move(info_vec) };
}

[[nodiscard]] auto make_torrent_info(
    tr_torrent* const tor,
    TrFormat const format,
    tr_quark const* const fields,
    size_t const field_count)
{
    return format == TrFormat::Table ? make_torrent_info_vec(tor, fields, field_count) :
                                       make_torrent_info_map(tor, fields, field_count);
}

[[nodiscard]] std::pair<JsonRpc::Error::Code, std::string> torrentGet(
    tr_session* session,
    tr_variant::Map const& args_in,
    tr_variant::Map& args_out)
{
    using namespace JsonRpc;

    auto const torrents = getTorrents(session, args_in);
    auto torrents_vec = tr_variant::Vector{};

    auto const format = args_in.value_if<std::string_view>(TR_KEY_format).value_or("object"sv) == "table"sv ? TrFormat::Table :
                                                                                                              TrFormat::Object;

    if (auto val = args_in.value_if<std::string_view>(TR_KEY_ids).value_or(""sv);
        val == tr_quark_get_string_view(TR_KEY_recently_active) ||
        val == tr_quark_get_string_view(TR_KEY_recently_active_kebab))
    {
        auto const cutoff = tr_time() - RecentlyActiveSeconds;
        auto const ids = session->torrents().removedSince(cutoff);

        auto removed_vec = tr_variant::Vector{};
        removed_vec.reserve(std::size(ids));
        for (auto const& id : ids)
        {
            removed_vec.emplace_back(id);
        }
        args_out.try_emplace(TR_KEY_removed, std::move(removed_vec));
    }

    auto keys = std::vector<tr_quark>{};
    if (auto const* const fields_vec = args_in.find_if<tr_variant::Vector>(TR_KEY_fields); fields_vec != nullptr)
    {
        auto const n_fields = std::size(*fields_vec);
        keys.reserve(n_fields);
        for (auto const& field : *fields_vec)
        {
            if (auto const field_sv = field.value_if<std::string_view>())
            {
                if (auto const key = tr_quark_lookup(*field_sv); key && isSupportedTorrentGetField(*key))
                {
                    keys.emplace_back(*key);
                }
            }
        }
    }

    if (std::empty(keys))
    {
        return { Error::INVALID_PARAMS, "no fields specified"s };
    }

    if (format == TrFormat::Table)
    {
        /* first entry is an array of property names */
        auto names = tr_variant::Vector{};
        names.reserve(std::size(keys));
        std::transform(
            std::begin(keys),
            std::end(keys),
            std::back_inserter(names),
            [](tr_quark key) { return tr_quark_get_string_view(key); });
        torrents_vec.emplace_back(std::move(names));
    }

    for (auto* const tor : torrents)
    {
        torrents_vec.emplace_back(make_torrent_info(tor, format, std::data(keys), std::size(keys)));
    }

    args_out.try_emplace(TR_KEY_torrents, std::move(torrents_vec));
    return { Error::SUCCESS, {} }; // no error message
}

// ---

[[nodiscard]] std::tuple<tr_torrent::labels_t, JsonRpc::Error::Code, std::string> make_labels(
    tr_variant::Vector const& labels_vec)
{
    using namespace JsonRpc;

    auto const n_labels = std::size(labels_vec);

    auto labels = tr_torrent::labels_t{};
    labels.reserve(n_labels);
    for (auto const& label_var : labels_vec)
    {
        if (auto const value = label_var.value_if<std::string_view>())
        {
            auto const label = tr_strv_strip(*value);

            if (std::empty(label))
            {
                return { {}, Error::INVALID_PARAMS, "labels cannot be empty"s };
            }

            if (tr_strv_contains(label, ','))
            {
                return { {}, Error::INVALID_PARAMS, "labels cannot contain comma (,) character"s };
            }

            labels.emplace_back(tr_quark_new(label));
        }
    }

    return { std::move(labels), Error::SUCCESS, {} };
}

[[nodiscard]] std::pair<JsonRpc::Error::Code, std::string> set_labels(tr_torrent* tor, tr_variant::Vector const& list)
{
    auto [labels, err, errmsg] = make_labels(list);
    if (err == JsonRpc::Error::SUCCESS)
    {
        tor->set_labels(labels);
    }
    return { err, std::move(errmsg) };
}

[[nodiscard]] std::tuple<std::vector<tr_file_index_t>, JsonRpc::Error::Code, std::string> get_file_indices(
    tr_torrent const* tor,
    tr_variant::Vector const& files_vec)
{
    using namespace JsonRpc;

    auto const n_files = tor->file_count();

    auto files = std::vector<tr_file_index_t>{};
    files.reserve(n_files);

    if (std::empty(files_vec)) // if empty set, apply to all
    {
        files.resize(n_files);
        std::iota(std::begin(files), std::end(files), 0);
    }
    else
    {
        for (auto const& file_var : files_vec)
        {
            if (auto const val = file_var.value_if<int64_t>())
            {
                if (auto const idx = static_cast<tr_file_index_t>(*val); idx < n_files)
                {
                    files.push_back(idx);
                }
                else
                {
                    return { {}, Error::FILE_IDX_OOR, std::string{} };
                }
            }
        }
    }

    return { std::move(files), Error::SUCCESS, {} };
}

[[nodiscard]] std::pair<JsonRpc::Error::Code, std::string> set_file_priorities(
    tr_torrent* tor,
    tr_priority_t priority,
    tr_variant::Vector const& files_vec)
{
    auto const [indices, err, errmsg] = get_file_indices(tor, files_vec);
    if (err == JsonRpc::Error::SUCCESS)
    {
        tor->set_file_priorities(std::data(indices), std::size(indices), priority);
    }
    return { err, std::move(errmsg) };
}

[[nodiscard]] std::pair<JsonRpc::Error::Code, std::string> set_sequential_download_from_piece(
    tr_torrent& tor,
    tr_piece_index_t piece)
{
    using namespace JsonRpc;

    if (piece >= tor.piece_count())
    {
        return { Error::PIECE_IDX_OOR, "piece to sequentially download from is outside pieces range"s };
    }

    tor.set_sequential_download_from_piece(piece);
    return { Error::SUCCESS, {} }; // no error
}

[[nodiscard]] std::pair<JsonRpc::Error::Code, std::string> set_file_dls(
    tr_torrent* tor,
    bool wanted,
    tr_variant::Vector const& files_vec)
{
    auto const [indices, err, errmsg] = get_file_indices(tor, files_vec);
    if (err == JsonRpc::Error::SUCCESS)
    {
        tor->set_files_wanted(std::data(indices), std::size(indices), wanted);
    }
    return { err, std::move(errmsg) };
}

[[nodiscard]] std::pair<JsonRpc::Error::Code, std::string> add_tracker_urls(tr_torrent* tor, tr_variant::Vector const& urls_vec)
{
    using namespace JsonRpc;

    auto ann = tor->announce_list();
    auto const baseline = ann;

    for (auto const& url_var : urls_vec)
    {
        if (auto const val = url_var.value_if<std::string_view>())
        {
            ann.add(*val);
        }
    }

    if (ann == baseline) // unchanged
    {
        return { Error::SET_ANNOUNCE_LIST, {} };
    }

    tor->set_announce_list(std::move(ann));
    return { Error::SUCCESS, {} };
}

[[nodiscard]] std::pair<JsonRpc::Error::Code, std::string> replace_trackers(tr_torrent* tor, tr_variant::Vector const& urls_vec)
{
    using namespace JsonRpc;

    auto ann = tor->announce_list();
    auto const baseline = ann;

    for (size_t i = 0, vec_size = std::size(urls_vec); i + 1 < vec_size; i += 2U)
    {
        auto const id = urls_vec[i].value_if<int64_t>();
        auto const url = urls_vec[i + 1U].value_if<std::string_view>();

        if (id && url)
        {
            ann.replace(static_cast<tr_tracker_id_t>(*id), *url);
        }
    }

    if (ann == baseline) // unchanged
    {
        return { Error::SET_ANNOUNCE_LIST, {} };
    }

    tor->set_announce_list(std::move(ann));
    return { Error::SUCCESS, {} };
}

[[nodiscard]] std::pair<JsonRpc::Error::Code, std::string> remove_trackers(tr_torrent* tor, tr_variant::Vector const& ids_vec)
{
    using namespace JsonRpc;

    auto ann = tor->announce_list();
    auto const baseline = ann;

    for (auto const& id_var : ids_vec)
    {
        if (auto const val = id_var.value_if<int64_t>())
        {
            ann.remove(static_cast<tr_tracker_id_t>(*val));
        }
    }

    if (ann == baseline) // unchanged
    {
        return { Error::SET_ANNOUNCE_LIST, {} };
    }

    tor->set_announce_list(std::move(ann));
    return { Error::SUCCESS, {} };
}

[[nodiscard]] std::pair<JsonRpc::Error::Code, std::string> torrentSet(
    tr_session* session,
    tr_variant::Map const& args_in,
    tr_variant::Map& /*args_out*/)
{
    using namespace JsonRpc;

    auto err = Error::SUCCESS;
    auto errmsg = std::string{};

    for (auto* tor : getTorrents(session, args_in))
    {
        if (auto const val = args_in.value_if<int64_t>({ TR_KEY_bandwidth_priority, TR_KEY_bandwidth_priority_camel }); val)
        {
            if (auto const priority = static_cast<tr_priority_t>(*val); tr_isPriority(priority))
            {
                tr_torrentSetPriority(tor, priority);
            }
        }

        if (auto const val = args_in.value_if<std::string_view>(TR_KEY_group))
        {
            tor->set_bandwidth_group(*val);
        }

        if (auto const* val = args_in.find_if<tr_variant::Vector>(TR_KEY_labels); val != nullptr && err == Error::SUCCESS)
        {
            std::tie(err, errmsg) = set_labels(tor, *val);
        }

        if (auto const* val = args_in.find_if<tr_variant::Vector>({ TR_KEY_files_unwanted, TR_KEY_files_unwanted_kebab });
            val != nullptr && err == Error::SUCCESS)
        {
            std::tie(err, errmsg) = set_file_dls(tor, false, *val);
        }

        if (auto const* val = args_in.find_if<tr_variant::Vector>({ TR_KEY_files_wanted, TR_KEY_files_wanted_kebab });
            val != nullptr && err == Error::SUCCESS)
        {
            std::tie(err, errmsg) = set_file_dls(tor, true, *val);
        }

        if (auto const val = args_in.value_if<int64_t>({ TR_KEY_peer_limit, TR_KEY_peer_limit_kebab }); val)
        {
            tr_torrentSetPeerLimit(tor, *val);
        }

        if (auto const* val = args_in.find_if<tr_variant::Vector>({ TR_KEY_priority_high, TR_KEY_priority_high_kebab });
            val != nullptr && err == Error::SUCCESS)
        {
            std::tie(err, errmsg) = set_file_priorities(tor, TR_PRI_HIGH, *val);
        }

        if (auto const* val = args_in.find_if<tr_variant::Vector>({ TR_KEY_priority_low, TR_KEY_priority_low_kebab });
            val != nullptr && err == Error::SUCCESS)
        {
            std::tie(err, errmsg) = set_file_priorities(tor, TR_PRI_LOW, *val);
        }

        if (auto const* val = args_in.find_if<tr_variant::Vector>({ TR_KEY_priority_normal, TR_KEY_priority_normal_kebab });
            val != nullptr && err == Error::SUCCESS)
        {
            std::tie(err, errmsg) = set_file_priorities(tor, TR_PRI_NORMAL, *val);
        }

        if (auto const val = args_in.value_if<int64_t>({ TR_KEY_download_limit, TR_KEY_download_limit_camel }); val)
        {
            tr_torrentSetSpeedLimit_KBps(tor, TR_DOWN, *val);
        }

        if (auto const val = args_in.value_if<bool>(TR_KEY_sequential_download); val)
        {
            tor->set_sequential_download(*val);
        }

        if (auto const val = args_in.value_if<int64_t>(TR_KEY_sequential_download_from_piece); val && err == Error::SUCCESS)
        {
            std::tie(err, errmsg) = set_sequential_download_from_piece(*tor, *val);
        }

        if (auto const val = args_in.value_if<bool>({ TR_KEY_download_limited, TR_KEY_download_limited_camel }); val)
        {
            tor->use_speed_limit(TR_DOWN, *val);
        }

        if (auto const val = args_in.value_if<bool>({ TR_KEY_honors_session_limits, TR_KEY_honors_session_limits_camel }); val)
        {
            tr_torrentUseSessionLimits(tor, *val);
        }

        if (auto const val = args_in.value_if<int64_t>({ TR_KEY_upload_limit, TR_KEY_upload_limit_camel }); val)
        {
            tr_torrentSetSpeedLimit_KBps(tor, TR_UP, *val);
        }

        if (auto const val = args_in.value_if<bool>({ TR_KEY_upload_limited, TR_KEY_upload_limited_camel }); val)
        {
            tor->use_speed_limit(TR_UP, *val);
        }

        if (auto const val = args_in.value_if<int64_t>({ TR_KEY_seed_idle_limit, TR_KEY_seed_idle_limit_camel }); val)
        {
            tor->set_idle_limit_minutes(static_cast<uint16_t>(*val));
        }

        if (auto const val = args_in.value_if<int64_t>({ TR_KEY_seed_idle_mode, TR_KEY_seed_idle_mode_camel }); val)
        {
            tor->set_idle_limit_mode(static_cast<tr_idlelimit>(*val));
        }

        if (auto const val = args_in.value_if<double>({ TR_KEY_seed_ratio_limit, TR_KEY_seed_ratio_limit_camel }); val)
        {
            tor->set_seed_ratio(*val);
        }

        if (auto const val = args_in.value_if<int64_t>({ TR_KEY_seed_ratio_mode, TR_KEY_seed_ratio_mode_camel }); val)
        {
            tor->set_seed_ratio_mode(static_cast<tr_ratiolimit>(*val));
        }

        if (auto const val = args_in.value_if<int64_t>({ TR_KEY_queue_position, TR_KEY_queue_position_camel }); val)
        {
            tr_torrentSetQueuePosition(tor, static_cast<size_t>(*val));
        }

        if (auto const* val = args_in.find_if<tr_variant::Vector>({ TR_KEY_tracker_add, TR_KEY_tracker_add_camel });
            val != nullptr)
        {
            std::tie(err, errmsg) = add_tracker_urls(tor, *val);
        }

        if (auto const* val = args_in.find_if<tr_variant::Vector>({ TR_KEY_tracker_remove, TR_KEY_tracker_remove_camel });
            val != nullptr)
        {
            std::tie(err, errmsg) = remove_trackers(tor, *val);
        }

        if (auto const* val = args_in.find_if<tr_variant::Vector>({ TR_KEY_tracker_replace, TR_KEY_tracker_replace_camel });
            val != nullptr)
        {
            std::tie(err, errmsg) = replace_trackers(tor, *val);
        }

        if (auto const val = args_in.value_if<std::string_view>({ TR_KEY_tracker_list, TR_KEY_tracker_list_camel }); val)
        {
            if (!tor->set_announce_list(*val))
            {
                err = Error::INVALID_TRACKER_LIST;
                errmsg = {};
            }
        }

        session->rpcNotify(TR_RPC_TORRENT_CHANGED, tor);
    }

    return { err, std::move(errmsg) };
}

[[nodiscard]] std::pair<JsonRpc::Error::Code, std::string> torrentSetLocation(
    tr_session* session,
    tr_variant::Map const& args_in,
    tr_variant::Map& /*args_out*/)
{
    using namespace JsonRpc;

    auto const location = args_in.value_if<std::string_view>(TR_KEY_location);
    if (!location)
    {
        return { Error::INVALID_PARAMS, "no location"s };
    }

    if (tr_sys_path_is_relative(*location))
    {
        return { Error::PATH_NOT_ABSOLUTE, "new location path is not absolute"s };
    }

    auto const move_flag = args_in.value_if<bool>(TR_KEY_move).value_or(false);
    for (auto* tor : getTorrents(session, args_in))
    {
        tor->set_location(*location, move_flag, nullptr);
        session->rpcNotify(TR_RPC_TORRENT_MOVED, tor);
    }

    return { Error::SUCCESS, {} };
}

// ---

void torrentRenamePathDone(
    tr_torrent* tor,
    char const* oldpath,
    char const* newname,
    int error,
    DoneCb const& done_cb,
    void* user_data)
{
    using namespace JsonRpc;

    auto* const data = static_cast<struct tr_rpc_idle_data*>(user_data);

    data->args_out.try_emplace(TR_KEY_id, tor->id());
    data->args_out.try_emplace(TR_KEY_path, oldpath);
    data->args_out.try_emplace(TR_KEY_name, newname);

    auto const is_success = error == 0;
    done_cb(data, is_success ? Error::SUCCESS : Error::SYSTEM_ERROR, is_success ? ""sv : tr_strerror(error));
}

void torrentRenamePath(
    tr_session* session,
    tr_variant::Map const& args_in,
    DoneCb&& done_cb,
    struct tr_rpc_idle_data* idle_data)
{
    using namespace JsonRpc;

    auto const torrents = getTorrents(session, args_in);
    if (std::size(torrents) != 1U)
    {
        done_cb(idle_data, Error::INVALID_PARAMS, "torrent_rename_path requires 1 torrent"sv);
        return;
    }

    auto const oldpath = args_in.value_if<std::string_view>(TR_KEY_path).value_or(""sv);
    auto const newname = args_in.value_if<std::string_view>(TR_KEY_name).value_or(""sv);
    torrents[0]->rename_path(
        oldpath,
        newname,
        [cb = std::move(done_cb)](tr_torrent* tor, char const* old_path, char const* new_name, int error, void* user_data)
        { torrentRenamePathDone(tor, old_path, new_name, error, cb, user_data); },
        idle_data);
}

// ---

void onPortTested(tr_web::FetchResponse const& web_response, DoneCb const& done_cb)
{
    using namespace JsonRpc;

    auto const& [status, body, primary_ip, did_connect, did_timeout, user_data] = web_response;
    auto* data = static_cast<tr_rpc_idle_data*>(user_data);

    if (auto const addr = tr_address::from_string(primary_ip);
        data->args_out.find_if<std::string_view>(TR_KEY_ip_protocol) == nullptr && addr && addr->is_valid())
    {
        data->args_out.try_emplace(TR_KEY_ip_protocol, addr->is_ipv4() ? "ipv4"sv : "ipv6"sv);
    }

    if (status != 200)
    {
        done_cb(
            data,
            Error::HTTP_ERROR,
            fmt::format(
                fmt::runtime(_("Couldn't test port: {error} ({error_code})")),
                fmt::arg("error", tr_webGetResponseStr(status)),
                fmt::arg("error_code", status)));
        return;
    }

    auto const port_is_open = tr_strv_starts_with(body, '1');
    data->args_out.try_emplace(TR_KEY_port_is_open, port_is_open);
    data->args_out.try_emplace(TR_KEY_port_is_open_kebab, port_is_open);
    done_cb(data, Error::SUCCESS, {});
}

void portTest(tr_session* session, tr_variant::Map const& args_in, DoneCb&& done_cb, struct tr_rpc_idle_data* idle_data)
{
    using namespace JsonRpc;

    static auto constexpr TimeoutSecs = 20s;

    auto const port = session->advertisedPeerPort();
    auto const url = fmt::format("https://portcheck.transmissionbt.com/{:d}", port.host());
    auto ip_proto = std::optional<tr_web::FetchOptions::IPProtocol>{};

    if (auto const val = args_in.value_if<std::string_view>(TR_KEY_ip_protocol); val)
    {
        if (*val == "ipv4"sv)
        {
            ip_proto = tr_web::FetchOptions::IPProtocol::V4;
            idle_data->args_out.try_emplace(TR_KEY_ip_protocol, "ipv4"sv);
        }
        else if (*val == "ipv6"sv)
        {
            ip_proto = tr_web::FetchOptions::IPProtocol::V6;
            idle_data->args_out.try_emplace(TR_KEY_ip_protocol, "ipv6"sv);
        }
        else
        {
            done_cb(idle_data, Error::INVALID_PARAMS, "invalid ip protocol string"sv);
            return;
        }
    }

    auto options = tr_web::FetchOptions{
        url,
        [cb = std::move(done_cb)](tr_web::FetchResponse const& r) { onPortTested(r, cb); },
        idle_data,
    };
    options.timeout_secs = TimeoutSecs;
    if (ip_proto)
    {
        options.ip_proto = *ip_proto;
    }
    session->fetch(std::move(options));
}

// ---

void onBlocklistFetched(tr_web::FetchResponse const& web_response, DoneCb const& done_cb)
{
    using namespace JsonRpc;

    auto const& [status, body, primary_ip, did_connect, did_timeout, user_data] = web_response;
    auto* data = static_cast<struct tr_rpc_idle_data*>(user_data);
    auto* const session = data->session;

    if (status != 200)
    {
        // we failed to download the blocklist...
        done_cb(
            data,
            Error::HTTP_ERROR,
            fmt::format(
                fmt::runtime(_("Couldn't fetch blocklist: {error} ({error_code})")),
                fmt::arg("error", tr_webGetResponseStr(status)),
                fmt::arg("error_code", status)));
        return;
    }

    // see if we need to decompress the content
    auto content = std::vector<char>{};
    content.resize(1024 * 128);
    for (;;)
    {
        auto decompressor = std::unique_ptr<libdeflate_decompressor, void (*)(libdeflate_decompressor*)>{
            libdeflate_alloc_decompressor(),
            libdeflate_free_decompressor
        };
        auto actual_size = size_t{};
        auto const decompress_result = libdeflate_gzip_decompress(
            decompressor.get(),
            std::data(body),
            std::size(body),
            std::data(content),
            std::size(content),
            &actual_size);
        if (decompress_result == LIBDEFLATE_INSUFFICIENT_SPACE)
        {
            // need a bigger buffer
            content.resize(content.size() * 2);
            continue;
        }
        if (decompress_result == LIBDEFLATE_BAD_DATA)
        {
            // couldn't decompress it; maybe we downloaded an uncompressed file
            content.assign(std::begin(body), std::end(body));
        }
        break;
    }

    // tr_blocklistSetContent needs a source file,
    // so save content into a tmpfile
    auto const filename = tr_pathbuf{ session->configDir(), "/blocklist.tmp"sv };
    if (auto error = tr_error{}; !tr_file_save(filename, content, &error))
    {
        done_cb(
            data,
            Error::SYSTEM_ERROR,
            fmt::format(
                fmt::runtime(_("Couldn't save '{path}': {error} ({error_code})")),
                fmt::arg("path", filename),
                fmt::arg("error", error.message()),
                fmt::arg("error_code", error.code())));
        return;
    }

    // feed it to the session and give the client a response
    auto const blocklist_size = tr_blocklistSetContent(session, filename);
    data->args_out.try_emplace(TR_KEY_blocklist_size, blocklist_size);
    data->args_out.try_emplace(TR_KEY_blocklist_size_kebab, blocklist_size);
    tr_sys_path_remove(filename);
    done_cb(data, Error::SUCCESS, {});
}

void blocklistUpdate(
    tr_session* session,
    tr_variant::Map const& /*args_in*/,
    DoneCb&& done_cb,
    struct tr_rpc_idle_data* idle_data)
{
    session->fetch(
        {
            session->blocklistUrl(),
            [cb = std::move(done_cb)](tr_web::FetchResponse const& r) { onBlocklistFetched(r, cb); },
            idle_data,
        });
}

// ---

void add_torrent_impl(struct tr_rpc_idle_data* data, DoneCb const& done_cb, tr_ctor& ctor)
{
    using namespace JsonRpc;

    tr_torrent* duplicate_of = nullptr;
    tr_torrent* tor = tr_torrentNew(&ctor, &duplicate_of);

    if (tor == nullptr && duplicate_of == nullptr)
    {
        done_cb(data, Error::CORRUPT_TORRENT, {});
        return;
    }

    static auto constexpr Fields = std::array<tr_quark, 4>{
        TR_KEY_id,
        TR_KEY_name,
        TR_KEY_hash_string,
        TR_KEY_hash_string_camel,
    };
    if (duplicate_of != nullptr)
    {
        data->args_out.try_emplace(
            TR_KEY_torrent_duplicate,
            make_torrent_info(duplicate_of, TrFormat::Object, std::data(Fields), std::size(Fields)));
        data->args_out.try_emplace(
            TR_KEY_torrent_duplicate_kebab,
            make_torrent_info(duplicate_of, TrFormat::Object, std::data(Fields), std::size(Fields)));
        done_cb(data, Error::SUCCESS, {});
        return;
    }

    data->session->rpcNotify(TR_RPC_TORRENT_ADDED, tor);
    data->args_out.try_emplace(
        TR_KEY_torrent_added,
        make_torrent_info(tor, TrFormat::Object, std::data(Fields), std::size(Fields)));
    data->args_out.try_emplace(
        TR_KEY_torrent_added_kebab,
        make_torrent_info(tor, TrFormat::Object, std::data(Fields), std::size(Fields)));
    done_cb(data, Error::SUCCESS, {});
}

struct add_torrent_idle_data
{
    add_torrent_idle_data(tr_rpc_idle_data* data_in, tr_ctor&& ctor_in)
        : data{ data_in }
        , ctor{ std::move(ctor_in) }
    {
    }

    tr_rpc_idle_data* data;
    tr_ctor ctor;
};

void onMetadataFetched(tr_web::FetchResponse const& web_response, DoneCb const& done_cb)
{
    auto const& [status, body, primary_ip, did_connect, did_timeout, user_data] = web_response;
    auto* data = static_cast<struct add_torrent_idle_data*>(user_data);

    tr_logAddTrace(
        fmt::format(
            "torrentAdd: HTTP response code was {} ({}); response length was {} bytes",
            status,
            tr_webGetResponseStr(status),
            std::size(body)));

    if (status == 200 || status == 221) /* http or ftp success.. */
    {
        data->ctor.set_metainfo(body);
        add_torrent_impl(data->data, done_cb, data->ctor);
    }
    else
    {
        done_cb(
            data->data,
            JsonRpc::Error::HTTP_ERROR,
            fmt::format(
                fmt::runtime(_("Couldn't fetch torrent: {error} ({error_code})")),
                fmt::arg("error", tr_webGetResponseStr(status)),
                fmt::arg("error_code", status)));
    }

    delete data;
}

bool isCurlURL(std::string_view url)
{
    auto constexpr Schemes = std::array<std::string_view, 4>{ "http"sv, "https"sv, "ftp"sv, "sftp"sv };
    auto const parsed = tr_urlParse(url);
    return parsed && std::find(std::begin(Schemes), std::end(Schemes), parsed->scheme) != std::end(Schemes);
}

[[nodiscard]] auto file_list_from_list(tr_variant::Vector const& idx_vec)
{
    auto const n_files = std::size(idx_vec);
    auto files = std::vector<tr_file_index_t>{};
    files.reserve(n_files);
    for (auto const& idx_var : idx_vec)
    {
        if (auto const val = idx_var.value_if<int64_t>())
        {
            files.emplace_back(static_cast<tr_file_index_t>(*val));
        }
    }
    return files;
}

void torrentAdd(tr_session* session, tr_variant::Map const& args_in, DoneCb&& done_cb, tr_rpc_idle_data* idle_data)
{
    using namespace JsonRpc;

    TR_ASSERT(idle_data != nullptr);

    auto const filename = args_in.value_if<std::string_view>(TR_KEY_filename).value_or(""sv);
    auto const metainfo_base64 = args_in.value_if<std::string_view>(TR_KEY_metainfo).value_or(""sv);
    if (std::empty(filename) && std::empty(metainfo_base64))
    {
        done_cb(idle_data, Error::INVALID_PARAMS, "no filename or metainfo specified"sv);
        return;
    }

    auto const download_dir = args_in.value_if<std::string_view>({ TR_KEY_download_dir, TR_KEY_download_dir_kebab });
    if (download_dir && tr_sys_path_is_relative(*download_dir))
    {
        done_cb(idle_data, Error::PATH_NOT_ABSOLUTE, "download directory path is not absolute"sv);
        return;
    }

    auto ctor = tr_ctor{ session };

    // set the optional parameters

    auto const cookies = args_in.value_if<std::string_view>(TR_KEY_cookies).value_or(""sv);

    if (download_dir && !std::empty(*download_dir))
    {
        ctor.set_download_dir(TR_FORCE, *download_dir);
    }

    if (auto const val = args_in.value_if<bool>(TR_KEY_paused))
    {
        ctor.set_paused(TR_FORCE, *val);
    }

    if (auto const val = args_in.value_if<int64_t>({ TR_KEY_peer_limit, TR_KEY_peer_limit_kebab }); val)
    {
        ctor.set_peer_limit(TR_FORCE, static_cast<uint16_t>(*val));
    }

    if (auto const val = args_in.value_if<int64_t>({ TR_KEY_bandwidth_priority, TR_KEY_bandwidth_priority_camel }); val)
    {
        ctor.set_bandwidth_priority(static_cast<tr_priority_t>(*val));
    }

    if (auto const val = args_in.find_if<tr_variant::Vector>({ TR_KEY_files_unwanted, TR_KEY_files_unwanted_kebab });
        val != nullptr)
    {
        auto const files = file_list_from_list(*val);
        ctor.set_files_wanted(std::data(files), std::size(files), false);
    }

    if (auto const val = args_in.find_if<tr_variant::Vector>({ TR_KEY_files_wanted, TR_KEY_files_wanted_kebab });
        val != nullptr)
    {
        auto const files = file_list_from_list(*val);
        ctor.set_files_wanted(std::data(files), std::size(files), true);
    }

    if (auto const val = args_in.find_if<tr_variant::Vector>({ TR_KEY_priority_low, TR_KEY_priority_low_kebab });
        val != nullptr)
    {
        auto const files = file_list_from_list(*val);
        ctor.set_file_priorities(std::data(files), std::size(files), TR_PRI_LOW);
    }

    if (auto const* val = args_in.find_if<tr_variant::Vector>({ TR_KEY_priority_normal, TR_KEY_priority_normal_kebab });
        val != nullptr)
    {
        auto const files = file_list_from_list(*val);
        ctor.set_file_priorities(std::data(files), std::size(files), TR_PRI_NORMAL);
    }

    if (auto const* val = args_in.find_if<tr_variant::Vector>({ TR_KEY_priority_high, TR_KEY_priority_high_kebab });
        val != nullptr)
    {
        auto const files = file_list_from_list(*val);
        ctor.set_file_priorities(std::data(files), std::size(files), TR_PRI_HIGH);
    }

    if (auto const* val = args_in.find_if<tr_variant::Vector>(TR_KEY_labels); val != nullptr)
    {
        auto [labels, err, errmsg] = make_labels(*val);

        if (err != Error::SUCCESS)
        {
            done_cb(idle_data, err, errmsg);
            return;
        }

        ctor.set_labels(std::move(labels));
    }

    if (auto const val = args_in.value_if<bool>(TR_KEY_sequential_download); val)
    {
        ctor.set_sequential_download(TR_FORCE, *val);
    }

    if (auto const val = args_in.value_if<int64_t>(TR_KEY_sequential_download_from_piece); val)
    {
        ctor.set_sequential_download_from_piece(TR_FORCE, *val);
    }

    tr_logAddTrace(fmt::format("torrentAdd: filename is '{}'", filename));

    if (isCurlURL(filename))
    {
        auto* const d = new add_torrent_idle_data{ idle_data, std::move(ctor) };
        auto options = tr_web::FetchOptions{
            filename,
            [cb = std::move(done_cb)](tr_web::FetchResponse const& r) { onMetadataFetched(r, cb); },
            d,
        };
        options.cookies = cookies;
        session->fetch(std::move(options));
        return;
    }

    auto ok = false;

    if (std::empty(filename))
    {
        ok = ctor.set_metainfo(tr_base64_decode(metainfo_base64));
    }
    else if (tr_sys_path_exists(tr_pathbuf{ filename }))
    {
        ok = ctor.set_metainfo_from_file(filename);
    }
    else
    {
        ok = ctor.set_metainfo_from_magnet_link(filename);
    }

    if (!ok)
    {
        done_cb(idle_data, Error::UNRECOGNIZED_INFO, {});
        return;
    }

    add_torrent_impl(idle_data, done_cb, ctor);
}

// ---

void add_strings_from_var(std::set<std::string_view>& strings, tr_variant const& var)
{
    if (auto const val = var.value_if<std::string_view>())
    {
        strings.insert(*val);
        return;
    }

    if (auto const* val = var.get_if<tr_variant::Vector>(); val != nullptr)
    {
        for (auto const& vecvar : *val)
        {
            add_strings_from_var(strings, vecvar);
        }
    }
}

[[nodiscard]] std::pair<JsonRpc::Error::Code, std::string> groupGet(
    tr_session* session,
    tr_variant::Map const& args_in,
    tr_variant::Map& args_out)
{
    auto names = std::set<std::string_view>{};
    if (auto const iter = args_in.find(TR_KEY_name); iter != std::end(args_in))
    {
        add_strings_from_var(names, iter->second);
    }

    auto groups_vec = tr_variant::Vector{};
    for (auto const& [name, group] : session->bandwidthGroups())
    {
        if (names.empty() || names.count(name.sv()) > 0U)
        {
            auto const limits = group->get_limits();
            auto group_map = tr_variant::Map{ 11U };
            group_map.try_emplace(TR_KEY_honors_session_limits, group->are_parent_limits_honored(TR_UP));
            group_map.try_emplace(TR_KEY_honors_session_limits_camel, group->are_parent_limits_honored(TR_UP));
            group_map.try_emplace(TR_KEY_name, name.sv());
            group_map.try_emplace(TR_KEY_speed_limit_down, limits.down_limit.count(Speed::Units::KByps));
            group_map.try_emplace(TR_KEY_speed_limit_down_kebab, limits.down_limit.count(Speed::Units::KByps));
            group_map.try_emplace(TR_KEY_speed_limit_down_enabled, limits.down_limited);
            group_map.try_emplace(TR_KEY_speed_limit_down_enabled_kebab, limits.down_limited);
            group_map.try_emplace(TR_KEY_speed_limit_up, limits.up_limit.count(Speed::Units::KByps));
            group_map.try_emplace(TR_KEY_speed_limit_up_kebab, limits.up_limit.count(Speed::Units::KByps));
            group_map.try_emplace(TR_KEY_speed_limit_up_enabled, limits.up_limited);
            group_map.try_emplace(TR_KEY_speed_limit_up_enabled_kebab, limits.up_limited);
            groups_vec.emplace_back(std::move(group_map));
        }
    }
    args_out.try_emplace(TR_KEY_group, std::move(groups_vec));

    return { JsonRpc::Error::SUCCESS, {} };
}

[[nodiscard]] std::pair<JsonRpc::Error::Code, std::string> groupSet(
    tr_session* session,
    tr_variant::Map const& args_in,
    tr_variant::Map& /*args_out*/)
{
    using namespace JsonRpc;

    auto const name = tr_strv_strip(args_in.value_if<std::string_view>(TR_KEY_name).value_or(""sv));
    if (std::empty(name))
    {
        return { Error::INVALID_PARAMS, "No group name given"s };
    }

    auto& group = session->getBandwidthGroup(name);
    auto limits = group.get_limits();

    if (auto const val = args_in.value_if<bool>({ TR_KEY_speed_limit_down_enabled, TR_KEY_speed_limit_down_enabled_kebab });
        val)
    {
        limits.down_limited = *val;
    }

    if (auto const val = args_in.value_if<bool>({ TR_KEY_speed_limit_up_enabled, TR_KEY_speed_limit_up_enabled_kebab }); val)
    {
        limits.up_limited = *val;
    }

    if (auto const val = args_in.value_if<int64_t>({ TR_KEY_speed_limit_down, TR_KEY_speed_limit_down_kebab }); val)
    {
        limits.down_limit = Speed{ *val, Speed::Units::KByps };
    }

    if (auto const val = args_in.value_if<int64_t>({ TR_KEY_speed_limit_up, TR_KEY_speed_limit_up_kebab }); val)
    {
        limits.up_limit = Speed{ *val, Speed::Units::KByps };
    }

    group.set_limits(limits);

    if (auto const val = args_in.value_if<bool>({ TR_KEY_honors_session_limits, TR_KEY_honors_session_limits_camel }); val)
    {
        group.honor_parent_limits(TR_UP, *val);
        group.honor_parent_limits(TR_DOWN, *val);
    }

    return { Error::SUCCESS, {} };
}

// ---

[[nodiscard]] std::pair<JsonRpc::Error::Code, std::string> sessionSet(
    tr_session* session,
    tr_variant::Map const& args_in,
    tr_variant::Map& /*args_out*/)
{
    using namespace JsonRpc;

    auto const download_dir = args_in.value_if<std::string_view>({ TR_KEY_download_dir, TR_KEY_download_dir_kebab });
    if (download_dir && tr_sys_path_is_relative(*download_dir))
    {
        return { Error::PATH_NOT_ABSOLUTE, "download directory path is not absolute"s };
    }

    auto const incomplete_dir = args_in.value_if<std::string_view>({ TR_KEY_incomplete_dir, TR_KEY_incomplete_dir_kebab });
    if (incomplete_dir && tr_sys_path_is_relative(*incomplete_dir))
    {
        return { Error::PATH_NOT_ABSOLUTE, "incomplete torrents directory path is not absolute"s };
    }

    if (auto const iter = args_in.find(TR_KEY_preferred_transports); iter != std::end(args_in))
    {
        if (!session->load_preferred_transports(iter->second))
        {
            return { Error::INVALID_PARAMS, R"(the list must be unique with the values "utp" or "tcp")" };
        }
    }

    if (auto const val = args_in.value_if<int64_t>({ TR_KEY_cache_size_mb, TR_KEY_cache_size_mb_kebab }); val)
    {
        tr_sessionSetCacheLimit_MB(session, *val);
    }

    if (auto const val = args_in.value_if<int64_t>({ TR_KEY_alt_speed_up, TR_KEY_alt_speed_up_kebab }); val)
    {
        tr_sessionSetAltSpeed_KBps(session, TR_UP, *val);
    }

    if (auto const val = args_in.value_if<int64_t>({ TR_KEY_alt_speed_down, TR_KEY_alt_speed_down_kebab }); val)
    {
        tr_sessionSetAltSpeed_KBps(session, TR_DOWN, *val);
    }

    if (auto const val = args_in.value_if<bool>({ TR_KEY_alt_speed_enabled, TR_KEY_alt_speed_enabled_kebab }); val)
    {
        tr_sessionUseAltSpeed(session, *val);
    }

    if (auto const val = args_in.value_if<int64_t>({ TR_KEY_alt_speed_time_begin, TR_KEY_alt_speed_time_begin_kebab }); val)
    {
        tr_sessionSetAltSpeedBegin(session, static_cast<size_t>(*val));
    }

    if (auto const val = args_in.value_if<int64_t>({ TR_KEY_alt_speed_time_end, TR_KEY_alt_speed_time_end_kebab }); val)
    {
        tr_sessionSetAltSpeedEnd(session, static_cast<size_t>(*val));
    }

    if (auto const val = args_in.value_if<int64_t>({ TR_KEY_alt_speed_time_day, TR_KEY_alt_speed_time_day_kebab }); val)
    {
        tr_sessionSetAltSpeedDay(session, static_cast<tr_sched_day>(*val));
    }

    if (auto const val = args_in.value_if<bool>({ TR_KEY_alt_speed_time_enabled, TR_KEY_alt_speed_time_enabled_kebab }); val)
    {
        tr_sessionUseAltSpeedTime(session, *val);
    }

    if (auto const val = args_in.value_if<bool>({ TR_KEY_blocklist_enabled, TR_KEY_blocklist_enabled_kebab }); val)
    {
        session->set_blocklist_enabled(*val);
    }

    if (auto const val = args_in.value_if<std::string_view>({ TR_KEY_blocklist_url, TR_KEY_blocklist_url_kebab }); val)
    {
        session->setBlocklistUrl(*val);
    }

    if (download_dir && !std::empty(*download_dir))
    {
        session->setDownloadDir(*download_dir);
    }

    if (auto const val = args_in.value_if<int64_t>({ TR_KEY_queue_stalled_minutes, TR_KEY_queue_stalled_minutes_kebab }); val)
    {
        tr_sessionSetQueueStalledMinutes(session, static_cast<int>(*val));
    }

    if (auto const val = args_in.value_if<bool>({ TR_KEY_queue_stalled_enabled, TR_KEY_queue_stalled_enabled_kebab }); val)
    {
        tr_sessionSetQueueStalledEnabled(session, *val);
    }

    if (auto const val = args_in.value_if<std::string_view>({ TR_KEY_default_trackers, TR_KEY_default_trackers_kebab }); val)
    {
        session->setDefaultTrackers(*val);
    }

    if (auto const val = args_in.value_if<int64_t>({ TR_KEY_download_queue_size, TR_KEY_download_queue_size_kebab }); val)
    {
        tr_sessionSetQueueSize(session, TR_DOWN, *val);
    }

    if (auto const val = args_in.value_if<bool>({ TR_KEY_download_queue_enabled, TR_KEY_download_queue_enabled_kebab }); val)
    {
        tr_sessionSetQueueEnabled(session, TR_DOWN, *val);
    }

    if (incomplete_dir && !std::empty(*incomplete_dir))
    {
        session->setIncompleteDir(*incomplete_dir);
    }

    if (auto const val = args_in.value_if<bool>({ TR_KEY_incomplete_dir_enabled, TR_KEY_incomplete_dir_enabled_kebab }); val)
    {
        session->useIncompleteDir(*val);
    }

    if (auto const val = args_in.value_if<int64_t>({ TR_KEY_peer_limit_global, TR_KEY_peer_limit_global_kebab }); val)
    {
        tr_sessionSetPeerLimit(session, *val);
    }

    if (auto const val = args_in.value_if<int64_t>({ TR_KEY_peer_limit_per_torrent, TR_KEY_peer_limit_per_torrent_kebab }); val)
    {
        tr_sessionSetPeerLimitPerTorrent(session, *val);
    }

    if (auto const val = args_in.value_if<int64_t>(TR_KEY_reqq); val && val > 0)
    {
        session->set_reqq(*val);
    }

    if (auto const val = args_in.value_if<bool>({ TR_KEY_pex_enabled, TR_KEY_pex_enabled_kebab }); val)
    {
        tr_sessionSetPexEnabled(session, *val);
    }

    if (auto const val = args_in.value_if<bool>({ TR_KEY_dht_enabled, TR_KEY_dht_enabled_kebab }); val)
    {
        tr_sessionSetDHTEnabled(session, *val);
    }

    if (auto const val = args_in.value_if<bool>({ TR_KEY_utp_enabled, TR_KEY_utp_enabled_kebab }); val)
    {
        tr_sessionSetUTPEnabled(session, *val);
    }

    if (auto const val = args_in.value_if<bool>({ TR_KEY_lpd_enabled, TR_KEY_lpd_enabled_kebab }); val)
    {
        tr_sessionSetLPDEnabled(session, *val);
    }

    if (auto const val = args_in.value_if<bool>({ TR_KEY_peer_port_random_on_start, TR_KEY_peer_port_random_on_start_kebab });
        val)
    {
        tr_sessionSetPeerPortRandomOnStart(session, *val);
    }

    if (auto const val = args_in.value_if<int64_t>({ TR_KEY_peer_port, TR_KEY_peer_port_kebab }); val)
    {
        tr_sessionSetPeerPort(session, *val);
    }

    if (auto const val = args_in.value_if<bool>({ TR_KEY_port_forwarding_enabled, TR_KEY_port_forwarding_enabled_kebab }); val)
    {
        tr_sessionSetPortForwardingEnabled(session, *val);
    }

    if (auto const val = args_in.value_if<bool>({ TR_KEY_rename_partial_files, TR_KEY_rename_partial_files_kebab }); val)
    {
        tr_sessionSetIncompleteFileNamingEnabled(session, *val);
    }

    if (auto const val = args_in.value_if<double>({ TR_KEY_seed_ratio_limit, TR_KEY_seed_ratio_limit_camel }); val)
    {
        tr_sessionSetRatioLimit(session, *val);
    }

    if (auto const val = args_in.value_if<bool>({ TR_KEY_seed_ratio_limited, TR_KEY_seed_ratio_limited_camel }); val)
    {
        tr_sessionSetRatioLimited(session, *val);
    }

    if (auto const val = args_in.value_if<int64_t>({ TR_KEY_idle_seeding_limit, TR_KEY_idle_seeding_limit_kebab }); val)
    {
        tr_sessionSetIdleLimit(session, *val);
    }

    if (auto const val = args_in.value_if<bool>({ TR_KEY_idle_seeding_limit_enabled, TR_KEY_idle_seeding_limit_enabled_kebab });
        val)
    {
        tr_sessionSetIdleLimited(session, *val);
    }

    if (auto const val = args_in.value_if<bool>({ TR_KEY_start_added_torrents, TR_KEY_start_added_torrents_kebab }); val)
    {
        tr_sessionSetPaused(session, !*val);
    }

    if (auto const val = args_in.value_if<bool>({ TR_KEY_seed_queue_enabled, TR_KEY_seed_queue_enabled_kebab }); val)
    {
        tr_sessionSetQueueEnabled(session, TR_UP, *val);
    }

    if (auto const val = args_in.value_if<int64_t>({ TR_KEY_seed_queue_size, TR_KEY_seed_queue_size_kebab }); val)
    {
        tr_sessionSetQueueSize(session, TR_UP, *val);
    }

    for (auto const& [enabled_key, script_key, script] : tr_session::Scripts)
    {
        if (auto const val = args_in.value_if<bool>({ tr_quark_convert(enabled_key), enabled_key }); val)
        {
            session->useScript(script, *val);
        }

        if (auto const val = args_in.value_if<std::string_view>({ tr_quark_convert(script_key), script_key }); val)
        {
            session->setScript(script, *val);
        }
    }

    if (auto const val = args_in.value_if<bool>(
            { TR_KEY_trash_original_torrent_files, TR_KEY_trash_original_torrent_files_kebab });
        val)
    {
        tr_sessionSetDeleteSource(session, *val);
    }

    if (auto const val = args_in.value_if<int64_t>({ TR_KEY_speed_limit_down, TR_KEY_speed_limit_down_kebab }); val)
    {
        session->set_speed_limit(TR_DOWN, Speed{ *val, Speed::Units::KByps });
    }

    if (auto const val = args_in.value_if<bool>({ TR_KEY_speed_limit_down_enabled, TR_KEY_speed_limit_down_enabled_kebab });
        val)
    {
        tr_sessionLimitSpeed(session, TR_DOWN, *val);
    }

    if (auto const val = args_in.value_if<int64_t>({ TR_KEY_speed_limit_up, TR_KEY_speed_limit_up_kebab }); val)
    {
        session->set_speed_limit(TR_UP, Speed{ *val, Speed::Units::KByps });
    }

    if (auto const val = args_in.value_if<bool>({ TR_KEY_speed_limit_up_enabled, TR_KEY_speed_limit_up_enabled_kebab }); val)
    {
        tr_sessionLimitSpeed(session, TR_UP, *val);
    }

    if (auto const val = args_in.value_if<std::string_view>(TR_KEY_encryption))
    {
        if (*val == "required"sv)
        {
            tr_sessionSetEncryption(session, TR_ENCRYPTION_REQUIRED);
        }
        else if (*val == "tolerated"sv)
        {
            tr_sessionSetEncryption(session, TR_CLEAR_PREFERRED);
        }
        else
        {
            tr_sessionSetEncryption(session, TR_ENCRYPTION_PREFERRED);
        }
    }

    if (auto const val = args_in.value_if<int64_t>(
            { TR_KEY_anti_brute_force_threshold, TR_KEY_anti_brute_force_threshold_kebab });
        val)
    {
        tr_sessionSetAntiBruteForceThreshold(session, static_cast<int>(*val));
    }

    if (auto const val = args_in.value_if<bool>({ TR_KEY_anti_brute_force_enabled, TR_KEY_anti_brute_force_enabled_kebab });
        val)
    {
        tr_sessionSetAntiBruteForceEnabled(session, *val);
    }

    if (auto const val = args_in.value_if<bool>(TR_KEY_sequential_download); val)
    {
        session->set_sequential_download(*val);
    }

    session->rpcNotify(TR_RPC_SESSION_CHANGED, nullptr);

    return { Error::SUCCESS, {} };
}

[[nodiscard]] std::pair<JsonRpc::Error::Code, std::string> sessionStats(
    tr_session* session,
    tr_variant::Map const& /*args_in*/,
    tr_variant::Map& args_out)
{
    auto const make_stats_map = [](auto const& stats)
    {
        auto stats_map = tr_variant::Map{ 5U };
        stats_map.try_emplace(TR_KEY_downloaded_bytes, stats.downloadedBytes);
        stats_map.try_emplace(TR_KEY_downloaded_bytes_camel, stats.downloadedBytes);
        stats_map.try_emplace(TR_KEY_files_added, stats.filesAdded);
        stats_map.try_emplace(TR_KEY_files_added_camel, stats.filesAdded);
        stats_map.try_emplace(TR_KEY_seconds_active, stats.secondsActive);
        stats_map.try_emplace(TR_KEY_seconds_active_camel, stats.secondsActive);
        stats_map.try_emplace(TR_KEY_session_count, stats.sessionCount);
        stats_map.try_emplace(TR_KEY_session_count_camel, stats.sessionCount);
        stats_map.try_emplace(TR_KEY_uploaded_bytes, stats.uploadedBytes);
        stats_map.try_emplace(TR_KEY_uploaded_bytes_camel, stats.uploadedBytes);
        return stats_map;
    };

    auto const& torrents = session->torrents();
    auto const total = std::size(torrents);
    auto const n_running = std::count_if(
        std::begin(torrents),
        std::end(torrents),
        [](auto const* tor) { return tor->is_running(); });

    args_out.reserve(std::size(args_out) + 7U);
    args_out.try_emplace(TR_KEY_active_torrent_count, n_running);
    args_out.try_emplace(TR_KEY_active_torrent_count_camel, n_running);
    args_out.try_emplace(TR_KEY_cumulative_stats, make_stats_map(session->stats().cumulative()));
    args_out.try_emplace(TR_KEY_cumulative_stats_kebab, make_stats_map(session->stats().cumulative()));
    args_out.try_emplace(TR_KEY_current_stats, make_stats_map(session->stats().current()));
    args_out.try_emplace(TR_KEY_current_stats_kebab, make_stats_map(session->stats().current()));
    args_out.try_emplace(TR_KEY_download_speed, session->piece_speed(TR_DOWN).base_quantity());
    args_out.try_emplace(TR_KEY_download_speed_camel, session->piece_speed(TR_DOWN).base_quantity());
    args_out.try_emplace(TR_KEY_paused_torrent_count, total - n_running);
    args_out.try_emplace(TR_KEY_paused_torrent_count_camel, total - n_running);
    args_out.try_emplace(TR_KEY_torrent_count, total);
    args_out.try_emplace(TR_KEY_torrent_count_camel, total);
    args_out.try_emplace(TR_KEY_upload_speed, session->piece_speed(TR_UP).base_quantity());
    args_out.try_emplace(TR_KEY_upload_speed_camel, session->piece_speed(TR_UP).base_quantity());

    return { JsonRpc::Error::SUCCESS, {} };
}

[[nodiscard]] constexpr std::string_view getEncryptionModeString(tr_encryption_mode mode)
{
    switch (mode)
    {
    case TR_CLEAR_PREFERRED:
        return "tolerated"sv;

    case TR_ENCRYPTION_REQUIRED:
        return "required"sv;

    default:
        return "preferred"sv;
    }
}

[[nodiscard]] auto values_get_units()
{
    using namespace libtransmission::Values;

    auto const make_units_vec = [](auto const& units)
    {
        auto units_vec = tr_variant::Vector{};
        for (size_t i = 0;; ++i)
        {
            auto const display_name = units.display_name(i);
            if (std::empty(display_name))
            {
                break;
            }
            units_vec.emplace_back(display_name);
        }
        return units_vec;
    };

    auto units_map = tr_variant::Map{ 6U };
    units_map.try_emplace(TR_KEY_memory_bytes, Memory::units().base());
    units_map.try_emplace(TR_KEY_memory_bytes_kebab, Memory::units().base());
    units_map.try_emplace(TR_KEY_memory_units, make_units_vec(Memory::units()));
    units_map.try_emplace(TR_KEY_memory_units_kebab, make_units_vec(Memory::units()));
    units_map.try_emplace(TR_KEY_size_bytes, Storage::units().base());
    units_map.try_emplace(TR_KEY_size_bytes_kebab, Storage::units().base());
    units_map.try_emplace(TR_KEY_size_units, make_units_vec(Storage::units()));
    units_map.try_emplace(TR_KEY_size_units_kebab, make_units_vec(Storage::units()));
    units_map.try_emplace(TR_KEY_speed_bytes, Speed::units().base());
    units_map.try_emplace(TR_KEY_speed_bytes_kebab, Speed::units().base());
    units_map.try_emplace(TR_KEY_speed_units, make_units_vec(Speed::units()));
    units_map.try_emplace(TR_KEY_speed_units_kebab, make_units_vec(Speed::units()));
    return tr_variant{ std::move(units_map) };
}

[[nodiscard]] tr_variant make_session_field(tr_session const& session, tr_quark const key)
{
    // clang-format off
    switch (key)
    {
    case TR_KEY_alt_speed_down:
    case TR_KEY_alt_speed_down_kebab:
        return tr_sessionGetAltSpeed_KBps(&session, TR_DOWN);
    case TR_KEY_alt_speed_enabled:
    case TR_KEY_alt_speed_enabled_kebab:
        return tr_sessionUsesAltSpeed(&session);
    case TR_KEY_alt_speed_time_begin:
    case TR_KEY_alt_speed_time_begin_kebab:
        return tr_sessionGetAltSpeedBegin(&session);
    case TR_KEY_alt_speed_time_day:
    case TR_KEY_alt_speed_time_day_kebab:
        return tr_sessionGetAltSpeedDay(&session);
    case TR_KEY_alt_speed_time_enabled:
    case TR_KEY_alt_speed_time_enabled_kebab:
        return tr_sessionUsesAltSpeedTime(&session);
    case TR_KEY_alt_speed_time_end:
    case TR_KEY_alt_speed_time_end_kebab:
        return tr_sessionGetAltSpeedEnd(&session);
    case TR_KEY_alt_speed_up:
    case TR_KEY_alt_speed_up_kebab:
        return tr_sessionGetAltSpeed_KBps(&session, TR_UP);
    case TR_KEY_anti_brute_force_enabled:
    case TR_KEY_anti_brute_force_enabled_kebab:
        return tr_sessionGetAntiBruteForceEnabled(&session);
    case TR_KEY_anti_brute_force_threshold:
    case TR_KEY_anti_brute_force_threshold_kebab:
        return tr_sessionGetAntiBruteForceThreshold(&session);
    case TR_KEY_blocklist_enabled:
    case TR_KEY_blocklist_enabled_kebab:
        return session.blocklist_enabled();
    case TR_KEY_blocklist_size:
    case TR_KEY_blocklist_size_kebab:
        return tr_blocklistGetRuleCount(&session);
    case TR_KEY_blocklist_url:
    case TR_KEY_blocklist_url_kebab:
        return session.blocklistUrl();
    case TR_KEY_cache_size_mb:
    case TR_KEY_cache_size_mb_kebab:
        return tr_sessionGetCacheLimit_MB(&session);
    case TR_KEY_config_dir:
    case TR_KEY_config_dir_kebab:
        return session.configDir();
    case TR_KEY_default_trackers:
    case TR_KEY_default_trackers_kebab:
        return session.defaultTrackersStr();
    case TR_KEY_dht_enabled:
    case TR_KEY_dht_enabled_kebab:
        return session.allowsDHT();
    case TR_KEY_download_dir:
    case TR_KEY_download_dir_kebab:
        return session.downloadDir();
    case TR_KEY_download_dir_free_space:
    case TR_KEY_download_dir_free_space_kebab:
        return tr_sys_path_get_capacity(session.downloadDir()).value_or(tr_sys_path_capacity{}).free;
    case TR_KEY_download_queue_enabled:
    case TR_KEY_download_queue_enabled_kebab:
        return session.queueEnabled(TR_DOWN);
    case TR_KEY_download_queue_size:
    case TR_KEY_download_queue_size_kebab:
        return session.queueSize(TR_DOWN);
    case TR_KEY_encryption: return getEncryptionModeString(tr_sessionGetEncryption(&session));
    case TR_KEY_idle_seeding_limit:
    case TR_KEY_idle_seeding_limit_kebab:
        return session.idleLimitMinutes();
    case TR_KEY_idle_seeding_limit_enabled:
    case TR_KEY_idle_seeding_limit_enabled_kebab:
        return session.isIdleLimited();
    case TR_KEY_incomplete_dir:
    case TR_KEY_incomplete_dir_kebab:
        return session.incompleteDir();
    case TR_KEY_incomplete_dir_enabled:
    case TR_KEY_incomplete_dir_enabled_kebab:
        return session.useIncompleteDir();
    case TR_KEY_lpd_enabled:
    case TR_KEY_lpd_enabled_kebab:
        return session.allowsLPD();
    case TR_KEY_peer_limit_global:
    case TR_KEY_peer_limit_global_kebab:
        return session.peerLimit();
    case TR_KEY_peer_limit_per_torrent:
    case TR_KEY_peer_limit_per_torrent_kebab:
        return session.peerLimitPerTorrent();
    case TR_KEY_peer_port:
    case TR_KEY_peer_port_kebab:
        return session.advertisedPeerPort().host();
    case TR_KEY_peer_port_random_on_start:
    case TR_KEY_peer_port_random_on_start_kebab:
        return session.isPortRandom();
    case TR_KEY_pex_enabled:
    case TR_KEY_pex_enabled_kebab:
        return session.allows_pex();
    case TR_KEY_port_forwarding_enabled:
    case TR_KEY_port_forwarding_enabled_kebab:
        return tr_sessionIsPortForwardingEnabled(&session);
    case TR_KEY_preferred_transports: return session.save_preferred_transports();
    case TR_KEY_queue_stalled_enabled:
    case TR_KEY_queue_stalled_enabled_kebab:
        return session.queueStalledEnabled();
    case TR_KEY_queue_stalled_minutes:
    case TR_KEY_queue_stalled_minutes_kebab:
        return session.queueStalledMinutes();
    case TR_KEY_rename_partial_files:
    case TR_KEY_rename_partial_files_kebab:
        return session.isIncompleteFileNamingEnabled();
    case TR_KEY_reqq: return session.reqq();
    case TR_KEY_rpc_version:
    case TR_KEY_rpc_version_kebab:
        return RpcVersion;
    case TR_KEY_rpc_version_minimum:
    case TR_KEY_rpc_version_minimum_kebab:
        return RpcVersionMin;
    case TR_KEY_rpc_version_semver:
    case TR_KEY_rpc_version_semver_kebab:
        return RpcVersionSemver;
    case TR_KEY_script_torrent_added_enabled:
    case TR_KEY_script_torrent_added_enabled_kebab:
        return session.useScript(TR_SCRIPT_ON_TORRENT_ADDED);
    case TR_KEY_script_torrent_added_filename:
    case TR_KEY_script_torrent_added_filename_kebab:
        return session.script(TR_SCRIPT_ON_TORRENT_ADDED);
    case TR_KEY_script_torrent_done_enabled:
    case TR_KEY_script_torrent_done_enabled_kebab:
        return session.useScript(TR_SCRIPT_ON_TORRENT_DONE);
    case TR_KEY_script_torrent_done_filename:
    case TR_KEY_script_torrent_done_filename_kebab:
        return session.script(TR_SCRIPT_ON_TORRENT_DONE);
    case TR_KEY_script_torrent_done_seeding_enabled:
    case TR_KEY_script_torrent_done_seeding_enabled_kebab:
        return session.useScript(TR_SCRIPT_ON_TORRENT_DONE_SEEDING);
    case TR_KEY_script_torrent_done_seeding_filename:
    case TR_KEY_script_torrent_done_seeding_filename_kebab:
        return session.script(TR_SCRIPT_ON_TORRENT_DONE_SEEDING);
    case TR_KEY_seed_ratio_limit:
    case TR_KEY_seed_ratio_limit_camel:
        return session.desiredRatio();
    case TR_KEY_seed_ratio_limited:
    case TR_KEY_seed_ratio_limited_camel:
        return session.isRatioLimited();
    case TR_KEY_seed_queue_enabled:
    case TR_KEY_seed_queue_enabled_kebab:
        return session.queueEnabled(TR_UP);
    case TR_KEY_seed_queue_size:
    case TR_KEY_seed_queue_size_kebab:
        return session.queueSize(TR_UP);
    case TR_KEY_sequential_download: return session.sequential_download();
    case TR_KEY_session_id:
    case TR_KEY_session_id_kebab:
        return session.sessionId();
    case TR_KEY_speed_limit_down:
    case TR_KEY_speed_limit_down_kebab:
        return session.speed_limit(TR_DOWN).count(Speed::Units::KByps);
    case TR_KEY_speed_limit_down_enabled:
    case TR_KEY_speed_limit_down_enabled_kebab:
        return session.is_speed_limited(TR_DOWN);
    case TR_KEY_speed_limit_up:
    case TR_KEY_speed_limit_up_kebab:
        return session.speed_limit(TR_UP).count(Speed::Units::KByps);
    case TR_KEY_speed_limit_up_enabled:
    case TR_KEY_speed_limit_up_enabled_kebab:
        return session.is_speed_limited(TR_UP);
    case TR_KEY_start_added_torrents:
    case TR_KEY_start_added_torrents_kebab:
        return !session.shouldPauseAddedTorrents();
    case TR_KEY_tcp_enabled:
    case TR_KEY_tcp_enabled_kebab:
        return session.allowsTCP();
    case TR_KEY_trash_original_torrent_files:
    case TR_KEY_trash_original_torrent_files_kebab:
        return session.shouldDeleteSource();
    case TR_KEY_units: return values_get_units();
    case TR_KEY_utp_enabled:
    case TR_KEY_utp_enabled_kebab:
        return session.allowsUTP();
    case TR_KEY_version: return LONG_VERSION_STRING;
    default: return tr_variant{};
    }
    // clang-format on
}

namespace session_get_helpers
{
[[nodiscard]] auto get_session_fields(tr_variant::Vector const* fields_vec)
{
    auto fields = std::set<tr_quark>{};

    if (fields_vec != nullptr)
    {
        for (auto const& field_var : *fields_vec)
        {
            if (auto const field_name = field_var.value_if<std::string_view>())
            {
                if (auto const field_id = tr_quark_lookup(*field_name); field_id)
                {
                    fields.insert(*field_id);
                }
            }
        }
    }

    if (std::empty(fields)) // no fields specified; get them all
    {
        for (tr_quark field_id = TR_KEY_NONE + 1; field_id < TR_N_KEYS; ++field_id)
        {
            fields.insert(field_id);
        }
    }

    return fields;
}
} // namespace session_get_helpers

[[nodiscard]] std::pair<JsonRpc::Error::Code, std::string> sessionGet(
    tr_session* session,
    tr_variant::Map const& args_in,
    tr_variant::Map& args_out)
{
    using namespace session_get_helpers;

    for (auto const key : get_session_fields(args_in.find_if<tr_variant::Vector>(TR_KEY_fields)))
    {
        if (auto var = make_session_field(*session, key); var.has_value())
        {
            args_out.try_emplace(key, std::move(var));
        }
    }

    return { JsonRpc::Error::SUCCESS, {} };
}

[[nodiscard]] std::pair<JsonRpc::Error::Code, std::string> freeSpace(
    tr_session* /*session*/,
    tr_variant::Map const& args_in,
    tr_variant::Map& args_out)
{
    using namespace JsonRpc;

    auto const path = args_in.value_if<std::string_view>(TR_KEY_path);
    if (!path)
    {
        return { Error::INVALID_PARAMS, "directory path argument is missing"s };
    }

    if (tr_sys_path_is_relative(*path))
    {
        return { Error::PATH_NOT_ABSOLUTE, "directory path is not absolute"s };
    }

    // get the free space
    auto const old_errno = errno;
    auto error = tr_error{};
    auto const capacity = tr_sys_path_get_capacity(*path, &error);
    errno = old_errno;

    // response
    args_out.try_emplace(TR_KEY_path, *path);
    args_out.try_emplace(TR_KEY_size_bytes, capacity ? capacity->free : -1);
    args_out.try_emplace(TR_KEY_size_bytes_kebab, capacity ? capacity->free : -1);
    args_out.try_emplace(TR_KEY_total_size, capacity ? capacity->total : -1);

    if (error)
    {
        return { Error::SYSTEM_ERROR, tr_strerror(error.code()) };
    }
    return { Error::SUCCESS, {} };
}

// ---

[[nodiscard]] std::pair<JsonRpc::Error::Code, std::string> sessionClose(
    tr_session* session,
    tr_variant::Map const& /*args_in*/,
    tr_variant::Map& /*args_out*/)
{
    session->rpcNotify(TR_RPC_SESSION_CLOSE, nullptr);
    return { JsonRpc::Error::SUCCESS, {} };
}

// ---

using SyncHandler = std::pair<JsonRpc::Error::Code, std::string> (*)(tr_session*, tr_variant::Map const&, tr_variant::Map&);

auto const sync_handlers = small::max_size_map<tr_quark, std::pair<SyncHandler, bool /*has_side_effects*/>, 20U>{ {
    { TR_KEY_free_space, { freeSpace, false } },
    { TR_KEY_group_get, { groupGet, false } },
    { TR_KEY_group_set, { groupSet, true } },
    { TR_KEY_queue_move_bottom, { queueMoveBottom, true } },
    { TR_KEY_queue_move_down, { queueMoveDown, true } },
    { TR_KEY_queue_move_top, { queueMoveTop, true } },
    { TR_KEY_queue_move_up, { queueMoveUp, true } },
    { TR_KEY_session_close, { sessionClose, true } },
    { TR_KEY_session_get, { sessionGet, false } },
    { TR_KEY_session_set, { sessionSet, true } },
    { TR_KEY_session_stats, { sessionStats, false } },
    { TR_KEY_torrent_get, { torrentGet, false } },
    { TR_KEY_torrent_reannounce, { torrentReannounce, true } },
    { TR_KEY_torrent_remove, { torrentRemove, true } },
    { TR_KEY_torrent_set, { torrentSet, true } },
    { TR_KEY_torrent_set_location, { torrentSetLocation, true } },
    { TR_KEY_torrent_start, { torrentStart, true } },
    { TR_KEY_torrent_start_now, { torrentStartNow, true } },
    { TR_KEY_torrent_stop, { torrentStop, true } },
    { TR_KEY_torrent_verify, { torrentVerify, true } },
} };

using AsyncHandler = void (*)(tr_session*, tr_variant::Map const&, DoneCb&&, tr_rpc_idle_data*);

auto const async_handlers = small::max_size_map<tr_quark, std::pair<AsyncHandler, bool /*has_side_effects*/>, 4U>{ {
    { TR_KEY_blocklist_update, { blocklistUpdate, true } },
    { TR_KEY_port_test, { portTest, false } },
    { TR_KEY_torrent_add, { torrentAdd, true } },
    { TR_KEY_torrent_rename_path, { torrentRenamePath, true } },
} };

void noop_response_callback(tr_session* /*session*/, tr_variant&& /*response*/)
{
}

void tr_rpc_request_exec_impl(tr_session* session, tr_variant const& request, tr_rpc_response_func&& callback, bool is_batch)
{
    using namespace JsonRpc;

    if (!callback)
    {
        callback = noop_response_callback;
    }

    auto const* const map = request.get_if<tr_variant::Map>();
    if (map == nullptr)
    {
        callback(
            session,
            build_response(
                Error::INVALID_REQUEST,
                nullptr,
                Error::build_data(is_batch ? "request must be an Object"sv : "request must be an Array or Object"sv, {})));
        return;
    }

    auto is_jsonrpc = false;
    if (auto jsonrpc = map->value_if<std::string_view>(TR_KEY_jsonrpc); jsonrpc == Version)
    {
        is_jsonrpc = true;
    }
    else if (jsonrpc || is_batch)
    {
        callback(
            session,
            build_response(Error::INVALID_REQUEST, nullptr, Error::build_data("JSON-RPC version is not 2.0"sv, {})));
        return;
    }

    auto const empty_params = tr_variant::Map{};
    auto const* params = map->find_if<tr_variant::Map>(is_jsonrpc ? TR_KEY_params : TR_KEY_arguments);
    if (params == nullptr)
    {
        params = &empty_params;
    }

    auto const method_name = map->value_if<std::string_view>(TR_KEY_method).value_or(""sv);
    auto const method_key = tr_quark_convert(tr_quark_new(method_name));

    auto data = tr_rpc_idle_data{};
    data.session = session;

    if (auto iter = map->find(is_jsonrpc ? TR_KEY_id : TR_KEY_tag); iter != std::end(*map))
    {
        tr_variant const& id = iter->second;
        if (is_jsonrpc && !is_valid_id(id))
        {
            callback(
                session,
                build_response(
                    Error::INVALID_REQUEST,
                    nullptr,
                    Error::build_data("id type must be String, Number, or Null"sv, {})));
            return;
        }
        data.id.merge(id);
    }

    data.callback = std::move(callback);

    auto const is_notification = is_jsonrpc && !data.id.has_value();

    auto done_cb = is_jsonrpc ? tr_rpc_idle_done : tr_rpc_idle_done_legacy;

    if (auto const handler = async_handlers.find(method_key); handler != std::end(async_handlers))
    {
        auto const& [func, has_side_effects] = handler->second;
        if (is_notification && !has_side_effects)
        {
            done_cb(&data, Error::SUCCESS, {});
            return;
        }

        func(
            session,
            *params,
            [cb = std::move(done_cb)](tr_rpc_idle_data* d, Error::Code code, std::string_view errmsg)
            {
                cb(d, code, errmsg);
                delete d;
            },
            new tr_rpc_idle_data{ std::move(data) });
        return;
    }

    if (auto const handler = sync_handlers.find(method_key); handler != std::end(sync_handlers))
    {
        auto const& [func, has_side_effects] = handler->second;
        if (is_notification && !has_side_effects)
        {
            done_cb(&data, Error::SUCCESS, {});
            return;
        }

        auto const [err, errmsg] = func(session, *params, data.args_out);
        done_cb(&data, err, errmsg);
        return;
    }

    // couldn't find a handler
    done_cb(&data, Error::METHOD_NOT_FOUND, is_jsonrpc ? ""sv : "no method name"sv);
}

void tr_rpc_request_exec_batch(tr_session* session, tr_variant::Vector const& requests, tr_rpc_response_func&& callback)
{
    auto const n_requests = std::size(requests);
    auto responses = std::make_shared<tr_variant::Vector>(n_requests);
    auto n_responses = std::make_shared<size_t>();
    auto cb = std::make_shared<tr_rpc_response_func>(std::move(callback));

    for (size_t i = 0U; i < n_requests; ++i)
    {
        tr_rpc_request_exec_impl(
            session,
            requests[i],
            [responses, n_requests, n_responses, i, cb](tr_session* s, tr_variant&& response)
            {
                (*responses)[i] = std::move(response);

                if (++(*n_responses) >= n_requests)
                {
                    // Remove notifications from reply
                    auto const it_end = std::remove_if(
                        std::begin(*responses),
                        std::end(*responses),
                        [](auto const& r) { return !r.has_value(); });
                    responses->erase(it_end, std::end(*responses));

                    (*cb)(s, !std::empty(*responses) ? std::move(*responses) : tr_variant{});
                }
            },
            true);
    }
}

} // namespace

void tr_rpc_request_exec(tr_session* session, tr_variant const& request, tr_rpc_response_func&& callback)
{
    auto const lock = session->unique_lock();

    if (auto const* const vec = request.get_if<tr_variant::Vector>(); vec != nullptr)
    {
        tr_rpc_request_exec_batch(session, *vec, std::move(callback));
        return;
    }

    tr_rpc_request_exec_impl(session, request, std::move(callback), false);
}

void tr_rpc_request_exec(tr_session* session, std::string_view request, tr_rpc_response_func&& callback)
{
    using namespace JsonRpc;

    auto serde = tr_variant_serde::json().inplace();
    if (auto otop = serde.parse(request); otop)
    {
        tr_rpc_request_exec(session, *otop, std::move(callback));
        return;
    }

    callback(session, build_response(Error::PARSE_ERROR, nullptr, Error::build_data(serde.error_.message(), {})));
}
