// This file Copyright © 2008-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <cerrno>
#include <ctime>
#include <iterator>
#include <numeric>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/core.h>

#include <libdeflate.h>

#include "transmission.h"

#include "announcer.h"
#include "completion.h"
#include "crypto-utils.h"
#include "error.h"
#include "file.h"
#include "log.h"
#include "peer-mgr.h"
#include "quark.h"
#include "rpcimpl.h"
#include "session-id.h"
#include "session.h"
#include "stats.h"
#include "torrent.h"
#include "tr-assert.h"
#include "tr-macros.h"
#include "tr-strbuf.h"
#include "utils.h"
#include "variant.h"
#include "version.h"
#include "web-utils.h"
#include "web.h"

using namespace std::literals;

static auto constexpr RecentlyActiveSeconds = time_t{ 60 };
static auto constexpr RpcVersion = int64_t{ 17 };
static auto constexpr RpcVersionMin = int64_t{ 14 };
static auto constexpr RpcVersionSemver = "5.3.0"sv;

enum class TrFormat
{
    Object,
    Table
};

/***
****
***/

static tr_rpc_callback_status notify(tr_session* session, tr_rpc_callback_type type, tr_torrent* tor)
{
    tr_rpc_callback_status status = TR_RPC_OK;

    if (session->rpc_func != nullptr)
    {
        status = (*session->rpc_func)(session, type, tor, session->rpc_func_user_data);
    }

    return status;
}

/***
****
***/

/* For functions that can't be immediately executed, like torrentAdd,
 * this is the callback data used to pass a response to the caller
 * when the task is complete */
struct tr_rpc_idle_data
{
    tr_session* session;
    tr_variant* response;
    tr_variant* args_out;
    tr_rpc_response_func callback;
    void* callback_user_data;
};

static auto constexpr SuccessResult = "success"sv;

static void tr_idle_function_done(struct tr_rpc_idle_data* data, std::string_view result)
{
    tr_variantDictAddStr(data->response, TR_KEY_result, result);

    (*data->callback)(data->session, data->response, data->callback_user_data);

    tr_variantFree(data->response);
    tr_free(data->response);
    tr_free(data);
}

/***
****
***/

static auto getTorrents(tr_session* session, tr_variant* args)
{
    auto torrents = std::vector<tr_torrent*>{};

    auto id = int64_t{};
    auto sv = std::string_view{};

    if (tr_variant* ids = nullptr; tr_variantDictFindList(args, TR_KEY_ids, &ids))
    {
        size_t const n = tr_variantListSize(ids);
        torrents.reserve(n);

        for (size_t i = 0; i < n; ++i)
        {
            tr_variant const* const node = tr_variantListChild(ids, i);
            tr_torrent* tor = nullptr;

            if (tr_variantGetInt(node, &id))
            {
                tor = tr_torrentFindFromId(session, id);
            }
            else if (tr_variantGetStrView(node, &sv))
            {
                tor = session->torrents().get(sv);
            }

            if (tor != nullptr)
            {
                torrents.push_back(tor);
            }
        }
    }
    else if (tr_variantDictFindInt(args, TR_KEY_ids, &id) || tr_variantDictFindInt(args, TR_KEY_id, &id))
    {
        if (auto* const tor = tr_torrentFindFromId(session, id); tor != nullptr)
        {
            torrents.push_back(tor);
        }
    }
    else if (tr_variantDictFindStrView(args, TR_KEY_ids, &sv))
    {
        if (sv == "recently-active"sv)
        {
            time_t const cutoff = tr_time() - RecentlyActiveSeconds;

            torrents.reserve(std::size(session->torrents()));
            std::copy_if(
                std::begin(session->torrents()),
                std::end(session->torrents()),
                std::back_inserter(torrents),
                [&cutoff](auto const* tor) { return tor->anyDate >= cutoff; });
        }
        else
        {
            auto* const tor = session->torrents().get(sv);
            if (tor != nullptr)
            {
                torrents.push_back(tor);
            }
        }
    }
    else // all of them
    {
        torrents.reserve(std::size(session->torrents()));
        std::copy(std::begin(session->torrents()), std::end(session->torrents()), std::back_inserter(torrents));
    }

    return torrents;
}

static void notifyBatchQueueChange(tr_session* session, std::vector<tr_torrent*> const& torrents)
{
    for (auto* tor : torrents)
    {
        notify(session, TR_RPC_TORRENT_CHANGED, tor);
    }

    notify(session, TR_RPC_SESSION_QUEUE_POSITIONS_CHANGED, nullptr);
}

static char const* queueMoveTop(
    tr_session* session,
    tr_variant* args_in,
    tr_variant* /*args_out*/,
    tr_rpc_idle_data* /*idle_data*/)
{
    auto const torrents = getTorrents(session, args_in);
    tr_torrentsQueueMoveTop(std::data(torrents), std::size(torrents));
    notifyBatchQueueChange(session, torrents);
    return nullptr;
}

static char const* queueMoveUp(
    tr_session* session,
    tr_variant* args_in,
    tr_variant* /*args_out*/,
    tr_rpc_idle_data* /*idle_data*/)
{
    auto const torrents = getTorrents(session, args_in);
    tr_torrentsQueueMoveUp(std::data(torrents), std::size(torrents));
    notifyBatchQueueChange(session, torrents);
    return nullptr;
}

static char const* queueMoveDown(
    tr_session* session,
    tr_variant* args_in,
    tr_variant* /*args_out*/,
    tr_rpc_idle_data* /*idle_data*/)
{
    auto const torrents = getTorrents(session, args_in);
    tr_torrentsQueueMoveDown(std::data(torrents), std::size(torrents));
    notifyBatchQueueChange(session, torrents);
    return nullptr;
}

static char const* queueMoveBottom(
    tr_session* session,
    tr_variant* args_in,
    tr_variant* /*args_out*/,
    tr_rpc_idle_data* /*idle_data*/)
{
    auto const torrents = getTorrents(session, args_in);
    tr_torrentsQueueMoveBottom(std::data(torrents), std::size(torrents));
    notifyBatchQueueChange(session, torrents);
    return nullptr;
}

struct CompareTorrentByQueuePosition
{
    bool operator()(tr_torrent const* a, tr_torrent const* b) const
    {
        return a->queuePosition < b->queuePosition;
    }
};

static char const* torrentStart(
    tr_session* session,
    tr_variant* args_in,
    tr_variant* /*args_out*/,
    tr_rpc_idle_data* /*idle_data*/)
{
    auto torrents = getTorrents(session, args_in);
    std::sort(std::begin(torrents), std::end(torrents), CompareTorrentByQueuePosition{});
    for (auto* tor : torrents)
    {
        if (!tor->isRunning)
        {
            tr_torrentStart(tor);
            notify(session, TR_RPC_TORRENT_STARTED, tor);
        }
    }

    return nullptr;
}

static char const* torrentStartNow(
    tr_session* session,
    tr_variant* args_in,
    tr_variant* /*args_out*/,
    tr_rpc_idle_data* /*idle_data*/)
{
    auto torrents = getTorrents(session, args_in);
    std::sort(std::begin(torrents), std::end(torrents), CompareTorrentByQueuePosition{});
    for (auto* tor : torrents)
    {
        if (!tor->isRunning)
        {
            tr_torrentStartNow(tor);
            notify(session, TR_RPC_TORRENT_STARTED, tor);
        }
    }

    return nullptr;
}

static char const* torrentStop(
    tr_session* session,
    tr_variant* args_in,
    tr_variant* /*args_out*/,
    tr_rpc_idle_data* /*idle_data*/)
{
    for (auto* tor : getTorrents(session, args_in))
    {
        if (tor->isRunning || tor->isQueued() || tor->verifyState() != TR_VERIFY_NONE)
        {
            tor->isStopping = true;
            notify(session, TR_RPC_TORRENT_STOPPED, tor);
        }
    }

    return nullptr;
}

static char const* torrentRemove(
    tr_session* session,
    tr_variant* args_in,
    tr_variant* /*args_out*/,
    tr_rpc_idle_data* /*idle_data*/)
{
    auto delete_flag = bool{ false };
    (void)tr_variantDictFindBool(args_in, TR_KEY_delete_local_data, &delete_flag);

    tr_rpc_callback_type type = delete_flag ? TR_RPC_TORRENT_TRASHING : TR_RPC_TORRENT_REMOVING;

    for (auto* tor : getTorrents(session, args_in))
    {
        tr_rpc_callback_status const status = notify(session, type, tor);

        if ((status & TR_RPC_NOREMOVE) == 0)
        {
            tr_torrentRemove(tor, delete_flag, nullptr);
        }
    }

    return nullptr;
}

static char const* torrentReannounce(
    tr_session* session,
    tr_variant* args_in,
    tr_variant* /*args_out*/,
    tr_rpc_idle_data* /*idle_data*/)
{
    for (auto* tor : getTorrents(session, args_in))
    {
        if (tr_torrentCanManualUpdate(tor))
        {
            tr_torrentManualUpdate(tor);
            notify(session, TR_RPC_TORRENT_CHANGED, tor);
        }
    }

    return nullptr;
}

static char const* torrentVerify(
    tr_session* session,
    tr_variant* args_in,
    tr_variant* /*args_out*/,
    tr_rpc_idle_data* /*idle_data*/)
{
    for (auto* tor : getTorrents(session, args_in))
    {
        tr_torrentVerify(tor);
        notify(session, TR_RPC_TORRENT_CHANGED, tor);
    }

    return nullptr;
}

/***
****
***/

static void addLabels(tr_torrent const* tor, tr_variant* list)
{
    tr_variantInitList(list, std::size(tor->labels));
    for (auto const& label : tor->labels)
    {
        tr_variantListAddQuark(list, label);
    }
}

static void addFileStats(tr_torrent const* tor, tr_variant* list)
{
    for (tr_file_index_t i = 0, n = tor->fileCount(); i < n; ++i)
    {
        auto const file = tr_torrentFile(tor, i);
        tr_variant* d = tr_variantListAddDict(list, 3);
        tr_variantDictAddInt(d, TR_KEY_bytesCompleted, file.have);
        tr_variantDictAddInt(d, TR_KEY_priority, file.priority);
        tr_variantDictAddBool(d, TR_KEY_wanted, file.wanted);
    }
}

static void addFiles(tr_torrent const* tor, tr_variant* list)
{
    for (tr_file_index_t i = 0, n = tor->fileCount(); i < n; ++i)
    {
        auto const file = tr_torrentFile(tor, i);
        tr_variant* d = tr_variantListAddDict(list, 3);
        tr_variantDictAddInt(d, TR_KEY_bytesCompleted, file.have);
        tr_variantDictAddInt(d, TR_KEY_length, file.length);
        tr_variantDictAddStr(d, TR_KEY_name, file.name);
    }
}

static void addWebseeds(tr_torrent const* tor, tr_variant* webseeds)
{
    for (size_t i = 0, n = tor->webseedCount(); i < n; ++i)
    {
        tr_variantListAddStr(webseeds, tor->webseed(i));
    }
}

static void addTrackers(tr_torrent const* tor, tr_variant* trackers)
{
    for (auto const& tracker : tor->announceList())
    {
        auto* const d = tr_variantListAddDict(trackers, 5);
        tr_variantDictAddQuark(d, TR_KEY_announce, tracker.announce.quark());
        tr_variantDictAddInt(d, TR_KEY_id, tracker.id);
        tr_variantDictAddQuark(d, TR_KEY_scrape, tracker.scrape.quark());
        tr_variantDictAddStrView(d, TR_KEY_sitename, tracker.sitename);
        tr_variantDictAddInt(d, TR_KEY_tier, tracker.tier);
    }
}

static void addTrackerStats(tr_tracker_view const& tracker, tr_variant* list)
{
    auto* const d = tr_variantListAddDict(list, 27);
    tr_variantDictAddStr(d, TR_KEY_announce, tracker.announce);
    tr_variantDictAddInt(d, TR_KEY_announceState, tracker.announceState);
    tr_variantDictAddInt(d, TR_KEY_downloadCount, tracker.downloadCount);
    tr_variantDictAddBool(d, TR_KEY_hasAnnounced, tracker.hasAnnounced);
    tr_variantDictAddBool(d, TR_KEY_hasScraped, tracker.hasScraped);
    tr_variantDictAddStr(d, TR_KEY_host, tracker.host);
    tr_variantDictAddStr(d, TR_KEY_sitename, tracker.sitename);
    tr_variantDictAddInt(d, TR_KEY_id, tracker.id);
    tr_variantDictAddBool(d, TR_KEY_isBackup, tracker.isBackup);
    tr_variantDictAddInt(d, TR_KEY_lastAnnouncePeerCount, tracker.lastAnnouncePeerCount);
    tr_variantDictAddStr(d, TR_KEY_lastAnnounceResult, tracker.lastAnnounceResult);
    tr_variantDictAddInt(d, TR_KEY_lastAnnounceStartTime, tracker.lastAnnounceStartTime);
    tr_variantDictAddBool(d, TR_KEY_lastAnnounceSucceeded, tracker.lastAnnounceSucceeded);
    tr_variantDictAddInt(d, TR_KEY_lastAnnounceTime, tracker.lastAnnounceTime);
    tr_variantDictAddBool(d, TR_KEY_lastAnnounceTimedOut, tracker.lastAnnounceTimedOut);
    tr_variantDictAddStr(d, TR_KEY_lastScrapeResult, tracker.lastScrapeResult);
    tr_variantDictAddInt(d, TR_KEY_lastScrapeStartTime, tracker.lastScrapeStartTime);
    tr_variantDictAddBool(d, TR_KEY_lastScrapeSucceeded, tracker.lastScrapeSucceeded);
    tr_variantDictAddInt(d, TR_KEY_lastScrapeTime, tracker.lastScrapeTime);
    tr_variantDictAddBool(d, TR_KEY_lastScrapeTimedOut, tracker.lastScrapeTimedOut);
    tr_variantDictAddInt(d, TR_KEY_leecherCount, tracker.leecherCount);
    tr_variantDictAddInt(d, TR_KEY_nextAnnounceTime, tracker.nextAnnounceTime);
    tr_variantDictAddInt(d, TR_KEY_nextScrapeTime, tracker.nextScrapeTime);
    tr_variantDictAddStr(d, TR_KEY_scrape, tracker.scrape);
    tr_variantDictAddInt(d, TR_KEY_scrapeState, tracker.scrapeState);
    tr_variantDictAddInt(d, TR_KEY_seederCount, tracker.seederCount);
    tr_variantDictAddInt(d, TR_KEY_tier, tracker.tier);
}

static void addPeers(tr_torrent const* tor, tr_variant* list)
{
    auto peerCount = int{};
    tr_peer_stat* peers = tr_torrentPeers(tor, &peerCount);

    tr_variantInitList(list, peerCount);

    for (int i = 0; i < peerCount; ++i)
    {
        tr_variant* d = tr_variantListAddDict(list, 16);
        tr_peer_stat const* peer = peers + i;
        tr_variantDictAddStr(d, TR_KEY_address, peer->addr);
        tr_variantDictAddStr(d, TR_KEY_clientName, peer->client);
        tr_variantDictAddBool(d, TR_KEY_clientIsChoked, peer->clientIsChoked);
        tr_variantDictAddBool(d, TR_KEY_clientIsInterested, peer->clientIsInterested);
        tr_variantDictAddStr(d, TR_KEY_flagStr, peer->flagStr);
        tr_variantDictAddBool(d, TR_KEY_isDownloadingFrom, peer->isDownloadingFrom);
        tr_variantDictAddBool(d, TR_KEY_isEncrypted, peer->isEncrypted);
        tr_variantDictAddBool(d, TR_KEY_isIncoming, peer->isIncoming);
        tr_variantDictAddBool(d, TR_KEY_isUploadingTo, peer->isUploadingTo);
        tr_variantDictAddBool(d, TR_KEY_isUTP, peer->isUTP);
        tr_variantDictAddBool(d, TR_KEY_peerIsChoked, peer->peerIsChoked);
        tr_variantDictAddBool(d, TR_KEY_peerIsInterested, peer->peerIsInterested);
        tr_variantDictAddInt(d, TR_KEY_port, peer->port);
        tr_variantDictAddReal(d, TR_KEY_progress, peer->progress);
        tr_variantDictAddInt(d, TR_KEY_rateToClient, tr_toSpeedBytes(peer->rateToClient_KBps));
        tr_variantDictAddInt(d, TR_KEY_rateToPeer, tr_toSpeedBytes(peer->rateToPeer_KBps));
    }

    tr_torrentPeersFree(peers, peerCount);
}

static void initField(tr_torrent const* const tor, tr_stat const* const st, tr_variant* const initme, tr_quark key)
{
    char* str = nullptr;

    switch (key)
    {
    case TR_KEY_activityDate:
        tr_variantInitInt(initme, st->activityDate);
        break;

    case TR_KEY_addedDate:
        tr_variantInitInt(initme, st->addedDate);
        break;

    case TR_KEY_availability:
        tr_variantInitList(initme, tor->pieceCount());
        for (tr_piece_index_t piece = 0, n = tor->pieceCount(); piece < n; ++piece)
        {
            tr_variantListAddInt(initme, tr_peerMgrPieceAvailability(tor, piece));
        }
        break;

    case TR_KEY_bandwidthPriority:
        tr_variantInitInt(initme, tr_torrentGetPriority(tor));
        break;

    case TR_KEY_comment:
        tr_variantInitStr(initme, tor->comment());
        break;

    case TR_KEY_corruptEver:
        tr_variantInitInt(initme, st->corruptEver);
        break;

    case TR_KEY_creator:
        tr_variantInitStrView(initme, tor->creator());
        break;

    case TR_KEY_dateCreated:
        tr_variantInitInt(initme, tor->dateCreated());
        break;

    case TR_KEY_desiredAvailable:
        tr_variantInitInt(initme, st->desiredAvailable);
        break;

    case TR_KEY_doneDate:
        tr_variantInitInt(initme, st->doneDate);
        break;

    case TR_KEY_downloadDir:
        tr_variantInitStrView(initme, tr_torrentGetDownloadDir(tor));
        break;

    case TR_KEY_downloadedEver:
        tr_variantInitInt(initme, st->downloadedEver);
        break;

    case TR_KEY_downloadLimit:
        tr_variantInitInt(initme, tr_torrentGetSpeedLimit_KBps(tor, TR_DOWN));
        break;

    case TR_KEY_downloadLimited:
        tr_variantInitBool(initme, tr_torrentUsesSpeedLimit(tor, TR_DOWN));
        break;

    case TR_KEY_error:
        tr_variantInitInt(initme, st->error);
        break;

    case TR_KEY_errorString:
        tr_variantInitStrView(initme, st->errorString);
        break;

    case TR_KEY_eta:
        tr_variantInitInt(initme, st->eta);
        break;

    case TR_KEY_file_count:
        tr_variantInitInt(initme, tor->fileCount());
        break;

    case TR_KEY_files:
        tr_variantInitList(initme, tor->fileCount());
        addFiles(tor, initme);
        break;

    case TR_KEY_fileStats:
        tr_variantInitList(initme, tor->fileCount());
        addFileStats(tor, initme);
        break;

    case TR_KEY_group:
        tr_variantInitStrView(initme, tor->bandwidthGroup().sv());
        break;

    case TR_KEY_hashString:
        tr_variantInitStrView(initme, tor->infoHashString());
        break;

    case TR_KEY_haveUnchecked:
        tr_variantInitInt(initme, st->haveUnchecked);
        break;

    case TR_KEY_haveValid:
        tr_variantInitInt(initme, st->haveValid);
        break;

    case TR_KEY_honorsSessionLimits:
        tr_variantInitBool(initme, tr_torrentUsesSessionLimits(tor));
        break;

    case TR_KEY_id:
        tr_variantInitInt(initme, st->id);
        break;

    case TR_KEY_editDate:
        tr_variantInitInt(initme, st->editDate);
        break;

    case TR_KEY_isFinished:
        tr_variantInitBool(initme, st->finished);
        break;

    case TR_KEY_isPrivate:
        tr_variantInitBool(initme, tor->isPrivate());
        break;

    case TR_KEY_isStalled:
        tr_variantInitBool(initme, st->isStalled);
        break;

    case TR_KEY_labels:
        addLabels(tor, initme);
        break;

    case TR_KEY_leftUntilDone:
        tr_variantInitInt(initme, st->leftUntilDone);
        break;

    case TR_KEY_manualAnnounceTime:
        tr_variantInitInt(initme, tr_announcerNextManualAnnounce(tor));
        break;

    case TR_KEY_maxConnectedPeers:
        tr_variantInitInt(initme, tr_torrentGetPeerLimit(tor));
        break;

    case TR_KEY_magnetLink:
        str = tr_torrentGetMagnetLink(tor);
        tr_variantInitStr(initme, str);
        tr_free(str);
        break;

    case TR_KEY_metadataPercentComplete:
        tr_variantInitReal(initme, st->metadataPercentComplete);
        break;

    case TR_KEY_name:
        tr_variantInitStrView(initme, tr_torrentName(tor));
        break;

    case TR_KEY_percentComplete:
        tr_variantInitReal(initme, st->percentComplete);
        break;

    case TR_KEY_percentDone:
        tr_variantInitReal(initme, st->percentDone);
        break;

    case TR_KEY_peer_limit:
        tr_variantInitInt(initme, tr_torrentGetPeerLimit(tor));
        break;

    case TR_KEY_peers:
        addPeers(tor, initme);
        break;

    case TR_KEY_peersConnected:
        tr_variantInitInt(initme, st->peersConnected);
        break;

    case TR_KEY_peersFrom:
        {
            tr_variantInitDict(initme, 7);
            auto const* f = st->peersFrom;
            tr_variantDictAddInt(initme, TR_KEY_fromCache, f[TR_PEER_FROM_RESUME]);
            tr_variantDictAddInt(initme, TR_KEY_fromDht, f[TR_PEER_FROM_DHT]);
            tr_variantDictAddInt(initme, TR_KEY_fromIncoming, f[TR_PEER_FROM_INCOMING]);
            tr_variantDictAddInt(initme, TR_KEY_fromLpd, f[TR_PEER_FROM_LPD]);
            tr_variantDictAddInt(initme, TR_KEY_fromLtep, f[TR_PEER_FROM_LTEP]);
            tr_variantDictAddInt(initme, TR_KEY_fromPex, f[TR_PEER_FROM_PEX]);
            tr_variantDictAddInt(initme, TR_KEY_fromTracker, f[TR_PEER_FROM_TRACKER]);
            break;
        }

    case TR_KEY_peersGettingFromUs:
        tr_variantInitInt(initme, st->peersGettingFromUs);
        break;

    case TR_KEY_peersSendingToUs:
        tr_variantInitInt(initme, st->peersSendingToUs);
        break;

    case TR_KEY_pieces:
        if (tor->hasMetainfo())
        {
            auto const bytes = tor->createPieceBitfield();
            auto const enc = tr_base64_encode({ reinterpret_cast<char const*>(std::data(bytes)), std::size(bytes) });
            tr_variantInitStr(initme, enc);
        }
        else
        {
            tr_variantInitStrView(initme, ""sv);
        }

        break;

    case TR_KEY_pieceCount:
        tr_variantInitInt(initme, tor->pieceCount());
        break;

    case TR_KEY_pieceSize:
        tr_variantInitInt(initme, tor->pieceSize());
        break;

    case TR_KEY_primary_mime_type:
        tr_variantInitStrView(initme, tor->primaryMimeType());
        break;

    case TR_KEY_priorities:
        {
            auto const n = tor->fileCount();
            tr_variantInitList(initme, n);
            for (tr_file_index_t i = 0; i < n; ++i)
            {
                tr_variantListAddInt(initme, tr_torrentFile(tor, i).priority);
            }
        }
        break;

    case TR_KEY_queuePosition:
        tr_variantInitInt(initme, st->queuePosition);
        break;

    case TR_KEY_etaIdle:
        tr_variantInitInt(initme, st->etaIdle);
        break;

    case TR_KEY_rateDownload:
        tr_variantInitInt(initme, tr_toSpeedBytes(st->pieceDownloadSpeed_KBps));
        break;

    case TR_KEY_rateUpload:
        tr_variantInitInt(initme, tr_toSpeedBytes(st->pieceUploadSpeed_KBps));
        break;

    case TR_KEY_recheckProgress:
        tr_variantInitReal(initme, st->recheckProgress);
        break;

    case TR_KEY_seedIdleLimit:
        tr_variantInitInt(initme, tr_torrentGetIdleLimit(tor));
        break;

    case TR_KEY_seedIdleMode:
        tr_variantInitInt(initme, tr_torrentGetIdleMode(tor));
        break;

    case TR_KEY_seedRatioLimit:
        tr_variantInitReal(initme, tr_torrentGetRatioLimit(tor));
        break;

    case TR_KEY_seedRatioMode:
        tr_variantInitInt(initme, tr_torrentGetRatioMode(tor));
        break;

    case TR_KEY_sizeWhenDone:
        tr_variantInitInt(initme, st->sizeWhenDone);
        break;

    case TR_KEY_source:
        tr_variantInitStrView(initme, tor->source());
        break;

    case TR_KEY_startDate:
        tr_variantInitInt(initme, st->startDate);
        break;

    case TR_KEY_status:
        tr_variantInitInt(initme, st->activity);
        break;

    case TR_KEY_secondsDownloading:
        tr_variantInitInt(initme, st->secondsDownloading);
        break;

    case TR_KEY_secondsSeeding:
        tr_variantInitInt(initme, st->secondsSeeding);
        break;

    case TR_KEY_trackers:
        tr_variantInitList(initme, tor->trackerCount());
        addTrackers(tor, initme);
        break;

    case TR_KEY_trackerList:
        tr_variantInitStr(initme, tor->trackerList());
        break;

    case TR_KEY_trackerStats:
        {
            auto const n = tr_torrentTrackerCount(tor);
            tr_variantInitList(initme, n);
            for (size_t i = 0; i < n; ++i)
            {
                auto const& tracker = tr_torrentTracker(tor, i);
                addTrackerStats(tracker, initme);
            }
            break;
        }

    case TR_KEY_torrentFile:
        tr_variantInitStr(initme, tor->torrentFile());
        break;

    case TR_KEY_totalSize:
        tr_variantInitInt(initme, tor->totalSize());
        break;

    case TR_KEY_uploadedEver:
        tr_variantInitInt(initme, st->uploadedEver);
        break;

    case TR_KEY_uploadLimit:
        tr_variantInitInt(initme, tr_torrentGetSpeedLimit_KBps(tor, TR_UP));
        break;

    case TR_KEY_uploadLimited:
        tr_variantInitBool(initme, tr_torrentUsesSpeedLimit(tor, TR_UP));
        break;

    case TR_KEY_uploadRatio:
        tr_variantInitReal(initme, st->ratio);
        break;

    case TR_KEY_wanted:
        {
            auto const n = tor->fileCount();
            tr_variantInitList(initme, n);
            for (tr_file_index_t i = 0; i < n; ++i)
            {
                tr_variantListAddBool(initme, tr_torrentFile(tor, i).wanted);
            }
        }
        break;

    case TR_KEY_webseeds:
        tr_variantInitList(initme, tor->webseedCount());
        addWebseeds(tor, initme);
        break;

    case TR_KEY_webseedsSendingToUs:
        tr_variantInitInt(initme, st->webseedsSendingToUs);
        break;

    default:
        break;
    }
}

static void addTorrentInfo(tr_torrent* tor, TrFormat format, tr_variant* entry, tr_quark const* fields, size_t fieldCount)
{
    if (format == TrFormat::Table)
    {
        tr_variantInitList(entry, fieldCount);
    }
    else
    {
        tr_variantInitDict(entry, fieldCount);
    }

    if (fieldCount > 0)
    {
        tr_stat const* const st = tr_torrentStat(tor);

        for (size_t i = 0; i < fieldCount; ++i)
        {
            tr_variant* child = format == TrFormat::Table ? tr_variantListAdd(entry) : tr_variantDictAdd(entry, fields[i]);

            initField(tor, st, child, fields[i]);
        }
    }
}

static char const* torrentGet(tr_session* session, tr_variant* args_in, tr_variant* args_out, tr_rpc_idle_data* /*idle_data*/)
{
    auto const torrents = getTorrents(session, args_in);
    tr_variant* const list = tr_variantDictAddList(args_out, TR_KEY_torrents, std::size(torrents) + 1);

    auto sv = std::string_view{};
    auto const format = tr_variantDictFindStrView(args_in, TR_KEY_format, &sv) && sv == "table"sv ? TrFormat::Table :
                                                                                                    TrFormat::Object;

    if (tr_variantDictFindStrView(args_in, TR_KEY_ids, &sv) && sv == "recently-active"sv)
    {
        auto const cutoff = tr_time() - RecentlyActiveSeconds;
        auto const ids = session->torrents().removedSince(cutoff);
        auto* const out = tr_variantDictAddList(args_out, TR_KEY_removed, std::size(ids));
        for (auto const& id : ids)
        {
            tr_variantListAddInt(out, id);
        }
    }

    tr_variant* fields = nullptr;
    char const* errmsg = nullptr;
    if (!tr_variantDictFindList(args_in, TR_KEY_fields, &fields))
    {
        errmsg = "no fields specified";
    }
    else
    {
        auto const n = tr_variantListSize(fields);
        auto keys = std::vector<tr_quark>{};
        keys.reserve(n);

        for (size_t i = 0; i < n; ++i)
        {
            if (!tr_variantGetStrView(tr_variantListChild(fields, i), &sv))
            {
                continue;
            }

            if (auto const key = tr_quark_lookup(sv); key)
            {
                keys.emplace_back(*key);
            }
        }

        if (format == TrFormat::Table)
        {
            /* first entry is an array of property names */
            tr_variant* names = tr_variantListAddList(list, std::size(keys));
            for (auto const& key : keys)
            {
                tr_variantListAddQuark(names, key);
            }
        }

        for (auto* tor : torrents)
        {
            addTorrentInfo(tor, format, tr_variantListAdd(list), std::data(keys), std::size(keys));
        }
    }

    return errmsg;
}

/***
****
***/

static std::pair<std::vector<tr_quark>, char const* /*errmsg*/> makeLabels(tr_variant* list)
{
    auto labels = std::vector<tr_quark>{};
    size_t const n = tr_variantListSize(list);
    labels.reserve(n);

    for (size_t i = 0; i < n; ++i)
    {
        auto label = std::string_view{};
        if (!tr_variantGetStrView(tr_variantListChild(list, i), &label))
        {
            continue;
        }

        label = tr_strvStrip(label);
        if (std::empty(label))
        {
            return { {}, "labels cannot be empty" };
        }

        if (tr_strvContains(label, ','))
        {
            return { {}, "labels cannot contain comma (,) character" };
        }

        labels.emplace_back(tr_quark_new(label));
    }

    return { labels, nullptr };
}

static char const* setLabels(tr_torrent* tor, tr_variant* list)
{
    auto [labels, errmsg] = makeLabels(list);

    if (errmsg != nullptr)
    {
        return errmsg;
    }

    tor->setLabels(labels);
    return nullptr;
}

static char const* setFilePriorities(tr_torrent* tor, tr_priority_t priority, tr_variant* list)
{
    char const* errmsg = nullptr;
    auto const n_files = tor->fileCount();

    auto files = std::vector<tr_file_index_t>{};
    files.reserve(n_files);

    if (size_t const n = tr_variantListSize(list); n != 0)
    {
        for (size_t i = 0; i < n; ++i)
        {
            auto tmp = int64_t{};
            if (tr_variantGetInt(tr_variantListChild(list, i), &tmp))
            {
                if (0 <= tmp && tmp < n_files)
                {
                    files.push_back(tr_file_index_t(tmp));
                }
                else
                {
                    errmsg = "file index out of range";
                }
            }
        }
    }
    else // if empty set, apply to all
    {
        files.resize(n_files);
        std::iota(std::begin(files), std::end(files), 0);
    }

    tor->setFilePriorities(std::data(files), std::size(files), priority);

    return errmsg;
}

static char const* setFileDLs(tr_torrent* tor, bool wanted, tr_variant* list)
{
    char const* errmsg = nullptr;

    auto const n_files = tor->fileCount();
    size_t const n_items = tr_variantListSize(list);

    auto files = std::vector<tr_file_index_t>{};
    files.reserve(n_files);

    if (n_items != 0) // if argument list, process them
    {
        for (size_t i = 0; i < n_items; ++i)
        {
            auto tmp = int64_t{};
            if (tr_variantGetInt(tr_variantListChild(list, i), &tmp))
            {
                if (0 <= tmp && tmp < n_files)
                {
                    files.push_back(tmp);
                }
                else
                {
                    errmsg = "file index out of range";
                }
            }
        }
    }
    else // if empty set, apply to all
    {
        files.resize(n_files);
        std::iota(std::begin(files), std::end(files), 0);
    }

    tor->setFilesWanted(std::data(files), std::size(files), wanted);

    return errmsg;
}

static char const* addTrackerUrls(tr_torrent* tor, tr_variant* urls)
{
    auto const old_size = tor->trackerCount();

    for (size_t i = 0, n = tr_variantListSize(urls); i < n; ++i)
    {
        auto announce = std::string_view();
        auto const* const val = tr_variantListChild(urls, i);
        if (val == nullptr || !tr_variantGetStrView(val, &announce))
        {
            continue;
        }

        tor->announceList().add(announce);
    }

    if (tor->trackerCount() == old_size)
    {
        return "error setting announce list";
    }

    tor->announceList().save(tor->torrentFile());

    return nullptr;
}

static char const* replaceTrackers(tr_torrent* tor, tr_variant* urls)
{
    auto changed = bool{ false };

    for (size_t i = 0, url_count = tr_variantListSize(urls); i + 1 < url_count; i += 2)
    {
        auto id = int64_t{};
        auto newval = std::string_view{};

        if (tr_variantGetInt(tr_variantListChild(urls, i), &id) &&
            tr_variantGetStrView(tr_variantListChild(urls, i + 1), &newval))
        {
            changed |= tor->announceList().replace(id, newval);
        }
    }

    if (!changed)
    {
        return "error setting announce list";
    }

    tor->announceList().save(tor->torrentFile());

    return nullptr;
}

static char const* removeTrackers(tr_torrent* tor, tr_variant* ids)
{
    auto const old_size = tor->trackerCount();

    for (size_t i = 0, n = tr_variantListSize(ids); i < n; ++i)
    {
        auto id = int64_t{};
        auto const* const val = tr_variantListChild(ids, i);
        if (val == nullptr || !tr_variantGetInt(val, &id))
        {
            continue;
        }

        tor->announceList().remove(id);
    }

    if (tor->trackerCount() == old_size)
    {
        return "error setting announce list";
    }

    tor->announceList().save(tor->torrentFile());

    return nullptr;
}

static char const* torrentSet(
    tr_session* session,
    tr_variant* args_in,
    tr_variant* /*args_out*/,
    tr_rpc_idle_data* /*idle_data*/)
{
    char const* errmsg = nullptr;

    for (auto* tor : getTorrents(session, args_in))
    {
        auto tmp = int64_t{};
        auto d = double{};
        auto boolVal = bool{};
        tr_variant* tmp_variant = nullptr;

        if (tr_variantDictFindInt(args_in, TR_KEY_bandwidthPriority, &tmp))
        {
            auto const priority = tr_priority_t(tmp);

            if (tr_isPriority(priority))
            {
                tr_torrentSetPriority(tor, priority);
            }
        }

        if (std::string_view group; tr_variantDictFindStrView(args_in, TR_KEY_group, &group))
        {
            tor->setBandwidthGroup(group);
        }

        if (errmsg == nullptr && tr_variantDictFindList(args_in, TR_KEY_labels, &tmp_variant))
        {
            errmsg = setLabels(tor, tmp_variant);
        }

        if (errmsg == nullptr && tr_variantDictFindList(args_in, TR_KEY_files_unwanted, &tmp_variant))
        {
            errmsg = setFileDLs(tor, false, tmp_variant);
        }

        if (errmsg == nullptr && tr_variantDictFindList(args_in, TR_KEY_files_wanted, &tmp_variant))
        {
            errmsg = setFileDLs(tor, true, tmp_variant);
        }

        if (tr_variantDictFindInt(args_in, TR_KEY_peer_limit, &tmp))
        {
            tr_torrentSetPeerLimit(tor, tmp);
        }

        if (errmsg == nullptr && tr_variantDictFindList(args_in, TR_KEY_priority_high, &tmp_variant))
        {
            errmsg = setFilePriorities(tor, TR_PRI_HIGH, tmp_variant);
        }

        if (errmsg == nullptr && tr_variantDictFindList(args_in, TR_KEY_priority_low, &tmp_variant))
        {
            errmsg = setFilePriorities(tor, TR_PRI_LOW, tmp_variant);
        }

        if (errmsg == nullptr && tr_variantDictFindList(args_in, TR_KEY_priority_normal, &tmp_variant))
        {
            errmsg = setFilePriorities(tor, TR_PRI_NORMAL, tmp_variant);
        }

        if (tr_variantDictFindInt(args_in, TR_KEY_downloadLimit, &tmp))
        {
            tr_torrentSetSpeedLimit_KBps(tor, TR_DOWN, tmp);
        }

        if (tr_variantDictFindBool(args_in, TR_KEY_downloadLimited, &boolVal))
        {
            tr_torrentUseSpeedLimit(tor, TR_DOWN, boolVal);
        }

        if (tr_variantDictFindBool(args_in, TR_KEY_honorsSessionLimits, &boolVal))
        {
            tr_torrentUseSessionLimits(tor, boolVal);
        }

        if (tr_variantDictFindInt(args_in, TR_KEY_uploadLimit, &tmp))
        {
            tr_torrentSetSpeedLimit_KBps(tor, TR_UP, tmp);
        }

        if (tr_variantDictFindBool(args_in, TR_KEY_uploadLimited, &boolVal))
        {
            tr_torrentUseSpeedLimit(tor, TR_UP, boolVal);
        }

        if (tr_variantDictFindInt(args_in, TR_KEY_seedIdleLimit, &tmp))
        {
            tr_torrentSetIdleLimit(tor, (uint16_t)tmp);
        }

        if (tr_variantDictFindInt(args_in, TR_KEY_seedIdleMode, &tmp))
        {
            tr_torrentSetIdleMode(tor, (tr_idlelimit)tmp);
        }

        if (tr_variantDictFindReal(args_in, TR_KEY_seedRatioLimit, &d))
        {
            tr_torrentSetRatioLimit(tor, d);
        }

        if (tr_variantDictFindInt(args_in, TR_KEY_seedRatioMode, &tmp))
        {
            tr_torrentSetRatioMode(tor, (tr_ratiolimit)tmp);
        }

        if (tr_variantDictFindInt(args_in, TR_KEY_queuePosition, &tmp))
        {
            tr_torrentSetQueuePosition(tor, (int)tmp);
        }

        if (errmsg == nullptr && tr_variantDictFindList(args_in, TR_KEY_trackerAdd, &tmp_variant))
        {
            errmsg = addTrackerUrls(tor, tmp_variant);
        }

        if (errmsg == nullptr && tr_variantDictFindList(args_in, TR_KEY_trackerRemove, &tmp_variant))
        {
            errmsg = removeTrackers(tor, tmp_variant);
        }

        if (errmsg == nullptr && tr_variantDictFindList(args_in, TR_KEY_trackerReplace, &tmp_variant))
        {
            errmsg = replaceTrackers(tor, tmp_variant);
        }

        if (std::string_view txt; errmsg == nullptr && tr_variantDictFindStrView(args_in, TR_KEY_trackerList, &txt))
        {
            if (!tor->setTrackerList(txt))
            {
                errmsg = "Invalid tracker list";
            }
        }

        notify(session, TR_RPC_TORRENT_CHANGED, tor);
    }

    return errmsg;
}

static char const* torrentSetLocation(
    tr_session* session,
    tr_variant* args_in,
    tr_variant* /*args_out*/,
    tr_rpc_idle_data* /*idle_data*/)
{
    auto location = std::string_view{};

    if (!tr_variantDictFindStrView(args_in, TR_KEY_location, &location))
    {
        return "no location";
    }

    if (tr_sys_path_is_relative(location))
    {
        return "new location path is not absolute";
    }

    auto move = bool{};
    (void)tr_variantDictFindBool(args_in, TR_KEY_move, &move);

    for (auto* tor : getTorrents(session, args_in))
    {
        tor->setLocation(location, move, nullptr, nullptr);
        notify(session, TR_RPC_TORRENT_MOVED, tor);
    }

    return nullptr;
}

/***
****
***/

static void torrentRenamePathDone(tr_torrent* tor, char const* oldpath, char const* newname, int error, void* user_data)
{
    auto* data = static_cast<struct tr_rpc_idle_data*>(user_data);

    tr_variantDictAddInt(data->args_out, TR_KEY_id, tr_torrentId(tor));
    tr_variantDictAddStr(data->args_out, TR_KEY_path, oldpath);
    tr_variantDictAddStr(data->args_out, TR_KEY_name, newname);

    tr_idle_function_done(data, error != 0 ? tr_strerror(error) : SuccessResult);
}

static char const* torrentRenamePath(
    tr_session* session,
    tr_variant* args_in,
    tr_variant* /*args_out*/,
    struct tr_rpc_idle_data* idle_data)
{
    char const* errmsg = nullptr;

    auto oldpath = std::string_view{};
    (void)tr_variantDictFindStrView(args_in, TR_KEY_path, &oldpath);
    auto newname = std::string_view{};
    (void)tr_variantDictFindStrView(args_in, TR_KEY_name, &newname);

    if (auto const torrents = getTorrents(session, args_in); std::size(torrents) == 1)
    {
        torrents[0]->renamePath(oldpath, newname, torrentRenamePathDone, idle_data);
    }
    else
    {
        errmsg = "torrent-rename-path requires 1 torrent";
    }

    /* cleanup */
    return errmsg;
}

/***
****
***/

static void onPortTested(tr_web::FetchResponse const& web_response)
{
    auto const& [status, body, did_connect, did_timeout, user_data] = web_response;
    auto* data = static_cast<struct tr_rpc_idle_data*>(user_data);

    if (status != 200)
    {
        tr_idle_function_done(
            data,
            fmt::format(
                _("Couldn't test port: {error} ({error_code})"),
                fmt::arg("error", tr_webGetResponseStr(status)),
                fmt::arg("error_code", status)));
    }
    else /* success */
    {
        bool const isOpen = tr_strvStartsWith(body, '1');
        tr_variantDictAddBool(data->args_out, TR_KEY_port_is_open, isOpen);
        tr_idle_function_done(data, SuccessResult);
    }
}

static char const* portTest(
    tr_session* session,
    tr_variant* /*args_in*/,
    tr_variant* /*args_out*/,
    struct tr_rpc_idle_data* idle_data)
{
    auto const port = session->peerPort();
    auto const url = fmt::format(FMT_STRING("https://portcheck.transmissionbt.com/{:d}"), port.host());
    session->web->fetch({ url, onPortTested, idle_data });
    return nullptr;
}

/***
****
***/

static void onBlocklistFetched(tr_web::FetchResponse const& web_response)
{
    auto const& [status, body, did_connect, did_timeout, user_data] = web_response;
    auto* data = static_cast<struct tr_rpc_idle_data*>(user_data);
    auto* const session = data->session;

    if (status != 200)
    {
        // we failed to download the blocklist...
        tr_idle_function_done(
            data,
            fmt::format(
                _("Couldn't fetch blocklist: {error} ({error_code})"),
                fmt::arg("error", tr_webGetResponseStr(status)),
                fmt::arg("error_code", status)));
        return;
    }

    // see if we need to decompress the content
    auto content = std::vector<char>{};
    content.resize(1024 * 128);
    for (;;)
    {
        auto decompressor = std::shared_ptr<libdeflate_decompressor>{ libdeflate_alloc_decompressor(),
                                                                      libdeflate_free_decompressor };
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
    auto const filename = tr_pathbuf{ session->config_dir, "/blocklist.tmp"sv };
    if (tr_error* error = nullptr; !tr_saveFile(filename, content, &error))
    {
        tr_idle_function_done(
            data,
            fmt::format(
                _("Couldn't save '{path}': {error} ({error_code})"),
                fmt::arg("path", filename),
                fmt::arg("error", error->message),
                fmt::arg("error_code", error->code)));
        tr_error_clear(&error);
        return;
    }

    // feed it to the session and give the client a response
    size_t const rule_count = tr_blocklistSetContent(session, filename);
    tr_variantDictAddInt(data->args_out, TR_KEY_blocklist_size, static_cast<int64_t>(rule_count));
    tr_sys_path_remove(filename);
    tr_idle_function_done(data, SuccessResult);
}

static char const* blocklistUpdate(
    tr_session* session,
    tr_variant* /*args_in*/,
    tr_variant* /*args_out*/,
    struct tr_rpc_idle_data* idle_data)
{
    session->web->fetch({ session->blocklistUrl(), onBlocklistFetched, idle_data });
    return nullptr;
}

/***
****
***/

static void addTorrentImpl(struct tr_rpc_idle_data* data, tr_ctor* ctor)
{
    tr_torrent* duplicate_of = nullptr;
    tr_torrent* tor = tr_torrentNew(ctor, &duplicate_of);
    tr_ctorFree(ctor);

    if (tor == nullptr && duplicate_of == nullptr)
    {
        tr_idle_function_done(data, "invalid or corrupt torrent file"sv);
        return;
    }

    static auto constexpr Fields = std::array<tr_quark, 3>{ TR_KEY_id, TR_KEY_name, TR_KEY_hashString };
    if (duplicate_of != nullptr)
    {
        addTorrentInfo(
            duplicate_of,
            TrFormat::Object,
            tr_variantDictAdd(data->args_out, TR_KEY_torrent_duplicate),
            std::data(Fields),
            std::size(Fields));
        tr_idle_function_done(data, "duplicate torrent"sv);
        return;
    }

    notify(data->session, TR_RPC_TORRENT_ADDED, tor);
    addTorrentInfo(
        tor,
        TrFormat::Object,
        tr_variantDictAdd(data->args_out, TR_KEY_torrent_added),
        std::data(Fields),
        std::size(Fields));
    tr_idle_function_done(data, SuccessResult);
}

struct add_torrent_idle_data
{
    struct tr_rpc_idle_data* data;
    tr_ctor* ctor;
};

static void onMetadataFetched(tr_web::FetchResponse const& web_response)
{
    auto const& [status, body, did_connect, did_timeout, user_data] = web_response;
    auto* data = static_cast<struct add_torrent_idle_data*>(user_data);

    tr_logAddTrace(fmt::format(
        "torrentAdd: HTTP response code was {} ({}); response length was {} bytes",
        status,
        tr_webGetResponseStr(status),
        std::size(body)));

    if (status == 200 || status == 221) /* http or ftp success.. */
    {
        tr_ctorSetMetainfo(data->ctor, std::data(body), std::size(body), nullptr);
        addTorrentImpl(data->data, data->ctor);
    }
    else
    {
        tr_idle_function_done(
            data->data,
            fmt::format(
                _("Couldn't fetch torrent: {error} ({error_code})"),
                fmt::arg("error", tr_webGetResponseStr(status)),
                fmt::arg("error_code", status)));
    }

    tr_free(data);
}

static bool isCurlURL(std::string_view url)
{
    auto constexpr Schemes = std::array<std::string_view, 4>{ "http"sv, "https"sv, "ftp"sv, "sftp"sv };
    auto const parsed = tr_urlParse(url);
    return parsed && std::find(std::begin(Schemes), std::end(Schemes), parsed->scheme) != std::end(Schemes);
}

static auto fileListFromList(tr_variant* list)
{
    size_t const n = tr_variantListSize(list);

    auto files = std::vector<tr_file_index_t>{};
    files.reserve(n);

    auto file_index = int64_t{};
    for (size_t i = 0; i < n; ++i)
    {
        if (tr_variantGetInt(tr_variantListChild(list, i), &file_index))
        {
            files.push_back(file_index);
        }
    }

    return files;
}

static char const* torrentAdd(tr_session* session, tr_variant* args_in, tr_variant* /*args_out*/, tr_rpc_idle_data* idle_data)
{
    TR_ASSERT(idle_data != nullptr);

    auto filename = std::string_view{};
    (void)tr_variantDictFindStrView(args_in, TR_KEY_filename, &filename);

    auto metainfo_base64 = std::string_view{};
    (void)tr_variantDictFindStrView(args_in, TR_KEY_metainfo, &metainfo_base64);

    if (std::empty(filename) && std::empty(metainfo_base64))
    {
        return "no filename or metainfo specified";
    }

    auto download_dir = std::string_view{};
    if (tr_variantDictFindStrView(args_in, TR_KEY_download_dir, &download_dir) && tr_sys_path_is_relative(download_dir))
    {
        return "download directory path is not absolute";
    }

    auto i = int64_t{};
    auto boolVal = bool{};
    tr_variant* l = nullptr;
    tr_ctor* ctor = tr_ctorNew(session);

    /* set the optional arguments */

    auto cookies = std::string_view{};
    (void)tr_variantDictFindStrView(args_in, TR_KEY_cookies, &cookies);

    if (!std::empty(download_dir))
    {
        auto const sz_download_dir = std::string{ download_dir };
        tr_ctorSetDownloadDir(ctor, TR_FORCE, sz_download_dir.c_str());
    }

    if (tr_variantDictFindBool(args_in, TR_KEY_paused, &boolVal))
    {
        tr_ctorSetPaused(ctor, TR_FORCE, boolVal);
    }

    if (tr_variantDictFindInt(args_in, TR_KEY_peer_limit, &i))
    {
        tr_ctorSetPeerLimit(ctor, TR_FORCE, (uint16_t)i);
    }

    if (tr_variantDictFindInt(args_in, TR_KEY_bandwidthPriority, &i))
    {
        tr_ctorSetBandwidthPriority(ctor, (tr_priority_t)i);
    }

    if (tr_variantDictFindList(args_in, TR_KEY_files_unwanted, &l))
    {
        auto const files = fileListFromList(l);
        tr_ctorSetFilesWanted(ctor, std::data(files), std::size(files), false);
    }

    if (tr_variantDictFindList(args_in, TR_KEY_files_wanted, &l))
    {
        auto const files = fileListFromList(l);
        tr_ctorSetFilesWanted(ctor, std::data(files), std::size(files), true);
    }

    if (tr_variantDictFindList(args_in, TR_KEY_priority_low, &l))
    {
        auto const files = fileListFromList(l);
        tr_ctorSetFilePriorities(ctor, std::data(files), std::size(files), TR_PRI_LOW);
    }

    if (tr_variantDictFindList(args_in, TR_KEY_priority_normal, &l))
    {
        auto const files = fileListFromList(l);
        tr_ctorSetFilePriorities(ctor, std::data(files), std::size(files), TR_PRI_NORMAL);
    }

    if (tr_variantDictFindList(args_in, TR_KEY_priority_high, &l))
    {
        auto const files = fileListFromList(l);
        tr_ctorSetFilePriorities(ctor, std::data(files), std::size(files), TR_PRI_HIGH);
    }

    if (tr_variantDictFindList(args_in, TR_KEY_labels, &l))
    {
        auto [labels, errmsg] = makeLabels(l);

        if (errmsg != nullptr)
        {
            tr_ctorFree(ctor);
            return errmsg;
        }

        tr_ctorSetLabels(ctor, std::data(labels), std::size(labels));
    }

    tr_logAddTrace(fmt::format("torrentAdd: filename is '{}'", filename));

    if (isCurlURL(filename))
    {
        auto* const d = tr_new0(struct add_torrent_idle_data, 1);
        d->data = idle_data;
        d->ctor = ctor;

        auto options = tr_web::FetchOptions{ filename, onMetadataFetched, d };
        options.cookies = cookies;
        session->web->fetch(std::move(options));
    }
    else
    {
        if (std::empty(filename))
        {
            auto const metainfo = tr_base64_decode(metainfo_base64);
            tr_ctorSetMetainfo(ctor, std::data(metainfo), std::size(metainfo), nullptr);
        }
        else
        {
            if (tr_sys_path_exists(tr_pathbuf{ filename }))
            {
                tr_ctorSetMetainfoFromFile(ctor, filename);
            }
            else
            {
                tr_ctorSetMetainfoFromMagnetLink(ctor, filename);
            }
        }

        addTorrentImpl(idle_data, ctor);
    }

    return nullptr;
}

/***
****
***/

static char const* groupGet(tr_session* s, tr_variant* args_in, tr_variant* args_out, struct tr_rpc_idle_data* /*idle_data*/)
{
    std::set<std::string_view> names;

    if (std::string_view one_name; tr_variantDictFindStrView(args_in, TR_KEY_name, &one_name))
    {
        names.insert(one_name);
    }
    else if (tr_variant* namesList = nullptr; tr_variantDictFindList(args_in, TR_KEY_name, &namesList))
    {
        auto const names_count = tr_variantListSize(namesList);

        for (size_t i = 0; i < names_count; ++i)
        {
            auto const* const v = tr_variantListChild(namesList, i);
            if (std::string_view l; tr_variantIsString(v) && tr_variantGetStrView(v, &l))
            {
                names.insert(l);
            }
        }
    }

    auto* const list = tr_variantDictAddList(args_out, TR_KEY_group, 1);
    for (auto const& [name, group] : s->bandwidth_groups_)
    {
        if (names.empty() || names.count(name.sv()) > 0)
        {
            tr_variant* dict = tr_variantListAddDict(list, 5);
            auto limits = group->getLimits();
            tr_variantDictAddStr(dict, TR_KEY_name, name);
            tr_variantDictAddBool(dict, TR_KEY_uploadLimited, limits.up_limited);
            tr_variantDictAddInt(dict, TR_KEY_uploadLimit, limits.up_limit_KBps);
            tr_variantDictAddBool(dict, TR_KEY_downloadLimited, limits.down_limited);
            tr_variantDictAddInt(dict, TR_KEY_downloadLimit, limits.down_limit_KBps);
            tr_variantDictAddBool(dict, TR_KEY_honorsSessionLimits, group->areParentLimitsHonored(TR_UP));
        }
    }

    return nullptr;
}

static char const* groupSet(
    tr_session* session,
    tr_variant* args_in,
    tr_variant* /*args_out*/,
    struct tr_rpc_idle_data* /*idle_data*/)
{
    auto name = std::string_view{};
    (void)tr_variantDictFindStrView(args_in, TR_KEY_name, &name);
    name = tr_strvStrip(name);
    if (std::empty(name))
    {
        return "No group name given";
    }

    auto& group = session->getBandwidthGroup(name);
    auto limits = group.getLimits();

    (void)tr_variantDictFindBool(args_in, TR_KEY_speed_limit_down_enabled, &limits.down_limited);
    (void)tr_variantDictFindBool(args_in, TR_KEY_speed_limit_up_enabled, &limits.up_limited);

    if (auto limit = int64_t{}; tr_variantDictFindInt(args_in, TR_KEY_speed_limit_down, &limit))
    {
        limits.down_limit_KBps = limit;
    }

    if (auto limit = int64_t{}; tr_variantDictFindInt(args_in, TR_KEY_speed_limit_up, &limit))
    {
        limits.up_limit_KBps = limit;
    }

    group.setLimits(&limits);

    if (auto honors = bool{}; tr_variantDictFindBool(args_in, TR_KEY_honorsSessionLimits, &honors))
    {
        group.honorParentLimits(TR_UP, honors);
        group.honorParentLimits(TR_DOWN, honors);
    }

    return nullptr;
}

/***
****
***/

static char const* sessionSet(
    tr_session* session,
    tr_variant* args_in,
    tr_variant* /*args_out*/,
    tr_rpc_idle_data* /*idle_data*/)
{
    auto download_dir = std::string_view{};
    auto incomplete_dir = std::string_view{};

    if (tr_variantDictFindStrView(args_in, TR_KEY_download_dir, &download_dir) && tr_sys_path_is_relative(download_dir))
    {
        return "download directory path is not absolute";
    }

    if (tr_variantDictFindStrView(args_in, TR_KEY_incomplete_dir, &incomplete_dir) && tr_sys_path_is_relative(incomplete_dir))
    {
        return "incomplete torrents directory path is not absolute";
    }

    auto boolVal = bool{};
    auto d = double{};
    auto i = int64_t{};
    auto sv = std::string_view{};

    if (tr_variantDictFindInt(args_in, TR_KEY_cache_size_mb, &i))
    {
        tr_sessionSetCacheLimit_MB(session, i);
    }

    if (tr_variantDictFindInt(args_in, TR_KEY_alt_speed_up, &i))
    {
        tr_sessionSetAltSpeed_KBps(session, TR_UP, i);
    }

    if (tr_variantDictFindInt(args_in, TR_KEY_alt_speed_down, &i))
    {
        tr_sessionSetAltSpeed_KBps(session, TR_DOWN, i);
    }

    if (tr_variantDictFindBool(args_in, TR_KEY_alt_speed_enabled, &boolVal))
    {
        tr_sessionUseAltSpeed(session, boolVal);
    }

    if (tr_variantDictFindInt(args_in, TR_KEY_alt_speed_time_begin, &i))
    {
        tr_sessionSetAltSpeedBegin(session, i);
    }

    if (tr_variantDictFindInt(args_in, TR_KEY_alt_speed_time_end, &i))
    {
        tr_sessionSetAltSpeedEnd(session, i);
    }

    if (tr_variantDictFindInt(args_in, TR_KEY_alt_speed_time_day, &i))
    {
        tr_sessionSetAltSpeedDay(session, tr_sched_day(i));
    }

    if (tr_variantDictFindBool(args_in, TR_KEY_alt_speed_time_enabled, &boolVal))
    {
        tr_sessionUseAltSpeedTime(session, boolVal);
    }

    if (tr_variantDictFindBool(args_in, TR_KEY_blocklist_enabled, &boolVal))
    {
        session->useBlocklist(boolVal);
    }

    if (tr_variantDictFindStrView(args_in, TR_KEY_blocklist_url, &sv))
    {
        session->setBlocklistUrl(sv);
    }

    if (!std::empty(download_dir))
    {
        session->setDownloadDir(download_dir);
    }

    if (tr_variantDictFindInt(args_in, TR_KEY_queue_stalled_minutes, &i))
    {
        tr_sessionSetQueueStalledMinutes(session, i);
    }

    if (tr_variantDictFindBool(args_in, TR_KEY_queue_stalled_enabled, &boolVal))
    {
        tr_sessionSetQueueStalledEnabled(session, boolVal);
    }

    if (tr_variantDictFindStrView(args_in, TR_KEY_default_trackers, &sv))
    {
        session->setDefaultTrackers(sv);
    }

    if (tr_variantDictFindInt(args_in, TR_KEY_download_queue_size, &i))
    {
        tr_sessionSetQueueSize(session, TR_DOWN, (int)i);
    }

    if (tr_variantDictFindBool(args_in, TR_KEY_download_queue_enabled, &boolVal))
    {
        tr_sessionSetQueueEnabled(session, TR_DOWN, boolVal);
    }

    if (!std::empty(incomplete_dir))
    {
        session->setIncompleteDir(incomplete_dir);
    }

    if (tr_variantDictFindBool(args_in, TR_KEY_incomplete_dir_enabled, &boolVal))
    {
        session->useIncompleteDir(boolVal);
    }

    if (tr_variantDictFindInt(args_in, TR_KEY_peer_limit_global, &i))
    {
        tr_sessionSetPeerLimit(session, i);
    }

    if (tr_variantDictFindInt(args_in, TR_KEY_peer_limit_per_torrent, &i))
    {
        tr_sessionSetPeerLimitPerTorrent(session, i);
    }

    if (tr_variantDictFindBool(args_in, TR_KEY_pex_enabled, &boolVal))
    {
        tr_sessionSetPexEnabled(session, boolVal);
    }

    if (tr_variantDictFindBool(args_in, TR_KEY_dht_enabled, &boolVal))
    {
        tr_sessionSetDHTEnabled(session, boolVal);
    }

    if (tr_variantDictFindBool(args_in, TR_KEY_utp_enabled, &boolVal))
    {
        tr_sessionSetUTPEnabled(session, boolVal);
    }

    if (tr_variantDictFindBool(args_in, TR_KEY_lpd_enabled, &boolVal))
    {
        tr_sessionSetLPDEnabled(session, boolVal);
    }

    if (tr_variantDictFindBool(args_in, TR_KEY_peer_port_random_on_start, &boolVal))
    {
        tr_sessionSetPeerPortRandomOnStart(session, boolVal);
    }

    if (tr_variantDictFindInt(args_in, TR_KEY_peer_port, &i))
    {
        session->setPeerPort(tr_port::fromHost(i));
    }

    if (tr_variantDictFindBool(args_in, TR_KEY_port_forwarding_enabled, &boolVal))
    {
        tr_sessionSetPortForwardingEnabled(session, boolVal);
    }

    if (tr_variantDictFindBool(args_in, TR_KEY_rename_partial_files, &boolVal))
    {
        tr_sessionSetIncompleteFileNamingEnabled(session, boolVal);
    }

    if (tr_variantDictFindReal(args_in, TR_KEY_seedRatioLimit, &d))
    {
        tr_sessionSetRatioLimit(session, d);
    }

    if (tr_variantDictFindBool(args_in, TR_KEY_seedRatioLimited, &boolVal))
    {
        tr_sessionSetRatioLimited(session, boolVal);
    }

    if (tr_variantDictFindInt(args_in, TR_KEY_idle_seeding_limit, &i))
    {
        tr_sessionSetIdleLimit(session, i);
    }

    if (tr_variantDictFindBool(args_in, TR_KEY_idle_seeding_limit_enabled, &boolVal))
    {
        tr_sessionSetIdleLimited(session, boolVal);
    }

    if (tr_variantDictFindBool(args_in, TR_KEY_start_added_torrents, &boolVal))
    {
        tr_sessionSetPaused(session, !boolVal);
    }

    if (tr_variantDictFindBool(args_in, TR_KEY_seed_queue_enabled, &boolVal))
    {
        tr_sessionSetQueueEnabled(session, TR_UP, boolVal);
    }

    if (tr_variantDictFindInt(args_in, TR_KEY_seed_queue_size, &i))
    {
        tr_sessionSetQueueSize(session, TR_UP, (int)i);
    }

    for (auto const& [enabled_key, script_key, script] : tr_session::Scripts)
    {
        if (auto enabled = bool{}; tr_variantDictFindBool(args_in, enabled_key, &enabled))
        {
            session->useScript(script, enabled);
        }

        if (auto file = std::string_view{}; tr_variantDictFindStrView(args_in, script_key, &file))
        {
            session->setScript(script, file);
        }
    }

    if (tr_variantDictFindBool(args_in, TR_KEY_trash_original_torrent_files, &boolVal))
    {
        tr_sessionSetDeleteSource(session, boolVal);
    }

    if (tr_variantDictFindInt(args_in, TR_KEY_speed_limit_down, &i))
    {
        tr_sessionSetSpeedLimit_KBps(session, TR_DOWN, i);
    }

    if (tr_variantDictFindBool(args_in, TR_KEY_speed_limit_down_enabled, &boolVal))
    {
        tr_sessionLimitSpeed(session, TR_DOWN, boolVal);
    }

    if (tr_variantDictFindInt(args_in, TR_KEY_speed_limit_up, &i))
    {
        tr_sessionSetSpeedLimit_KBps(session, TR_UP, i);
    }

    if (tr_variantDictFindBool(args_in, TR_KEY_speed_limit_up_enabled, &boolVal))
    {
        tr_sessionLimitSpeed(session, TR_UP, boolVal);
    }

    if (tr_variantDictFindStrView(args_in, TR_KEY_encryption, &sv))
    {
        if (sv == "required"sv)
        {
            tr_sessionSetEncryption(session, TR_ENCRYPTION_REQUIRED);
        }
        else if (sv == "tolerated"sv)
        {
            tr_sessionSetEncryption(session, TR_CLEAR_PREFERRED);
        }
        else
        {
            tr_sessionSetEncryption(session, TR_ENCRYPTION_PREFERRED);
        }
    }

    if (tr_variantDictFindInt(args_in, TR_KEY_anti_brute_force_threshold, &i))
    {
        tr_sessionSetAntiBruteForceThreshold(session, i);
    }

    if (tr_variantDictFindBool(args_in, TR_KEY_anti_brute_force_enabled, &boolVal))
    {
        tr_sessionSetAntiBruteForceEnabled(session, boolVal);
    }

    notify(session, TR_RPC_SESSION_CHANGED, nullptr);

    return nullptr;
}

static char const* sessionStats(
    tr_session* session,
    tr_variant* /*args_in*/,
    tr_variant* args_out,
    tr_rpc_idle_data* /*idle_data*/)
{
    auto currentStats = tr_session_stats{};
    auto cumulativeStats = tr_session_stats{};

    auto const& torrents = session->torrents();
    auto const total = std::size(torrents);
    auto const running = std::count_if(
        std::begin(torrents),
        std::end(torrents),
        [](auto const* tor) { return tor->isRunning; });

    tr_sessionGetStats(session, &currentStats);
    tr_sessionGetCumulativeStats(session, &cumulativeStats);

    tr_variantDictAddInt(args_out, TR_KEY_activeTorrentCount, running);
    tr_variantDictAddReal(args_out, TR_KEY_downloadSpeed, tr_sessionGetPieceSpeed_Bps(session, TR_DOWN));
    tr_variantDictAddInt(args_out, TR_KEY_pausedTorrentCount, total - running);
    tr_variantDictAddInt(args_out, TR_KEY_torrentCount, total);
    tr_variantDictAddReal(args_out, TR_KEY_uploadSpeed, tr_sessionGetPieceSpeed_Bps(session, TR_UP));

    tr_variant* d = tr_variantDictAddDict(args_out, TR_KEY_cumulative_stats, 5);
    tr_variantDictAddInt(d, TR_KEY_downloadedBytes, cumulativeStats.downloadedBytes);
    tr_variantDictAddInt(d, TR_KEY_filesAdded, cumulativeStats.filesAdded);
    tr_variantDictAddInt(d, TR_KEY_secondsActive, cumulativeStats.secondsActive);
    tr_variantDictAddInt(d, TR_KEY_sessionCount, cumulativeStats.sessionCount);
    tr_variantDictAddInt(d, TR_KEY_uploadedBytes, cumulativeStats.uploadedBytes);

    d = tr_variantDictAddDict(args_out, TR_KEY_current_stats, 5);
    tr_variantDictAddInt(d, TR_KEY_downloadedBytes, currentStats.downloadedBytes);
    tr_variantDictAddInt(d, TR_KEY_filesAdded, currentStats.filesAdded);
    tr_variantDictAddInt(d, TR_KEY_secondsActive, currentStats.secondsActive);
    tr_variantDictAddInt(d, TR_KEY_sessionCount, currentStats.sessionCount);
    tr_variantDictAddInt(d, TR_KEY_uploadedBytes, currentStats.uploadedBytes);

    return nullptr;
}

static constexpr std::string_view getEncryptionModeString(tr_encryption_mode mode)
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

static void addSessionField(tr_session const* s, tr_variant* d, tr_quark key)
{
    switch (key)
    {
    case TR_KEY_alt_speed_up:
        tr_variantDictAddInt(d, key, tr_sessionGetAltSpeed_KBps(s, TR_UP));
        break;

    case TR_KEY_alt_speed_down:
        tr_variantDictAddInt(d, key, tr_sessionGetAltSpeed_KBps(s, TR_DOWN));
        break;

    case TR_KEY_alt_speed_enabled:
        tr_variantDictAddBool(d, key, tr_sessionUsesAltSpeed(s));
        break;

    case TR_KEY_alt_speed_time_begin:
        tr_variantDictAddInt(d, key, tr_sessionGetAltSpeedBegin(s));
        break;

    case TR_KEY_alt_speed_time_end:
        tr_variantDictAddInt(d, key, tr_sessionGetAltSpeedEnd(s));
        break;

    case TR_KEY_alt_speed_time_day:
        tr_variantDictAddInt(d, key, tr_sessionGetAltSpeedDay(s));
        break;

    case TR_KEY_alt_speed_time_enabled:
        tr_variantDictAddBool(d, key, tr_sessionUsesAltSpeedTime(s));
        break;

    case TR_KEY_blocklist_enabled:
        tr_variantDictAddBool(d, key, s->useBlocklist());
        break;

    case TR_KEY_blocklist_url:
        tr_variantDictAddStr(d, key, s->blocklistUrl());
        break;

    case TR_KEY_cache_size_mb:
        tr_variantDictAddInt(d, key, tr_sessionGetCacheLimit_MB(s));
        break;

    case TR_KEY_blocklist_size:
        tr_variantDictAddInt(d, key, tr_blocklistGetRuleCount(s));
        break;

    case TR_KEY_config_dir:
        tr_variantDictAddStr(d, key, tr_sessionGetConfigDir(s));
        break;

    case TR_KEY_default_trackers:
        tr_variantDictAddStr(d, key, s->defaultTrackersStr());
        break;

    case TR_KEY_download_dir:
        tr_variantDictAddStr(d, key, s->downloadDir());
        break;

    case TR_KEY_download_dir_free_space:
        tr_variantDictAddInt(d, key, tr_dirSpace(s->downloadDir()).free);
        break;

    case TR_KEY_download_queue_enabled:
        tr_variantDictAddBool(d, key, tr_sessionGetQueueEnabled(s, TR_DOWN));
        break;

    case TR_KEY_download_queue_size:
        tr_variantDictAddInt(d, key, tr_sessionGetQueueSize(s, TR_DOWN));
        break;

    case TR_KEY_peer_limit_global:
        tr_variantDictAddInt(d, key, tr_sessionGetPeerLimit(s));
        break;

    case TR_KEY_peer_limit_per_torrent:
        tr_variantDictAddInt(d, key, tr_sessionGetPeerLimitPerTorrent(s));
        break;

    case TR_KEY_incomplete_dir:
        tr_variantDictAddStr(d, key, s->incompleteDir());
        break;

    case TR_KEY_incomplete_dir_enabled:
        tr_variantDictAddBool(d, key, s->useIncompleteDir());
        break;

    case TR_KEY_pex_enabled:
        tr_variantDictAddBool(d, key, tr_sessionIsPexEnabled(s));
        break;

    case TR_KEY_utp_enabled:
        tr_variantDictAddBool(d, key, tr_sessionIsUTPEnabled(s));
        break;

    case TR_KEY_dht_enabled:
        tr_variantDictAddBool(d, key, tr_sessionIsDHTEnabled(s));
        break;

    case TR_KEY_lpd_enabled:
        tr_variantDictAddBool(d, key, tr_sessionIsLPDEnabled(s));
        break;

    case TR_KEY_peer_port:
        tr_variantDictAddInt(d, key, s->peerPort().host());
        break;

    case TR_KEY_peer_port_random_on_start:
        tr_variantDictAddBool(d, key, tr_sessionGetPeerPortRandomOnStart(s));
        break;

    case TR_KEY_port_forwarding_enabled:
        tr_variantDictAddBool(d, key, tr_sessionIsPortForwardingEnabled(s));
        break;

    case TR_KEY_rename_partial_files:
        tr_variantDictAddBool(d, key, tr_sessionIsIncompleteFileNamingEnabled(s));
        break;

    case TR_KEY_rpc_version:
        tr_variantDictAddInt(d, key, RpcVersion);
        break;

    case TR_KEY_rpc_version_semver:
        tr_variantDictAddStrView(d, key, RpcVersionSemver);
        break;

    case TR_KEY_rpc_version_minimum:
        tr_variantDictAddInt(d, key, RpcVersionMin);
        break;

    case TR_KEY_seedRatioLimit:
        tr_variantDictAddReal(d, key, tr_sessionGetRatioLimit(s));
        break;

    case TR_KEY_seedRatioLimited:
        tr_variantDictAddBool(d, key, tr_sessionIsRatioLimited(s));
        break;

    case TR_KEY_idle_seeding_limit:
        tr_variantDictAddInt(d, key, tr_sessionGetIdleLimit(s));
        break;

    case TR_KEY_idle_seeding_limit_enabled:
        tr_variantDictAddBool(d, key, tr_sessionIsIdleLimited(s));
        break;

    case TR_KEY_seed_queue_enabled:
        tr_variantDictAddBool(d, key, tr_sessionGetQueueEnabled(s, TR_UP));
        break;

    case TR_KEY_seed_queue_size:
        tr_variantDictAddInt(d, key, tr_sessionGetQueueSize(s, TR_UP));
        break;

    case TR_KEY_start_added_torrents:
        tr_variantDictAddBool(d, key, !tr_sessionGetPaused(s));
        break;

    case TR_KEY_trash_original_torrent_files:
        tr_variantDictAddBool(d, key, tr_sessionGetDeleteSource(s));
        break;

    case TR_KEY_speed_limit_up:
        tr_variantDictAddInt(d, key, tr_sessionGetSpeedLimit_KBps(s, TR_UP));
        break;

    case TR_KEY_speed_limit_up_enabled:
        tr_variantDictAddBool(d, key, tr_sessionIsSpeedLimited(s, TR_UP));
        break;

    case TR_KEY_speed_limit_down:
        tr_variantDictAddInt(d, key, tr_sessionGetSpeedLimit_KBps(s, TR_DOWN));
        break;

    case TR_KEY_speed_limit_down_enabled:
        tr_variantDictAddBool(d, key, tr_sessionIsSpeedLimited(s, TR_DOWN));
        break;

    case TR_KEY_script_torrent_added_filename:
        tr_variantDictAddStr(d, key, s->script(TR_SCRIPT_ON_TORRENT_ADDED));
        break;

    case TR_KEY_script_torrent_added_enabled:
        tr_variantDictAddBool(d, key, s->useScript(TR_SCRIPT_ON_TORRENT_ADDED));
        break;

    case TR_KEY_script_torrent_done_filename:
        tr_variantDictAddStr(d, key, s->script(TR_SCRIPT_ON_TORRENT_DONE));
        break;

    case TR_KEY_script_torrent_done_enabled:
        tr_variantDictAddBool(d, key, s->useScript(TR_SCRIPT_ON_TORRENT_DONE));
        break;

    case TR_KEY_script_torrent_done_seeding_filename:
        tr_variantDictAddStr(d, key, s->script(TR_SCRIPT_ON_TORRENT_DONE_SEEDING));
        break;

    case TR_KEY_script_torrent_done_seeding_enabled:
        tr_variantDictAddBool(d, key, s->useScript(TR_SCRIPT_ON_TORRENT_DONE_SEEDING));
        break;

    case TR_KEY_queue_stalled_enabled:
        tr_variantDictAddBool(d, key, tr_sessionGetQueueStalledEnabled(s));
        break;

    case TR_KEY_queue_stalled_minutes:
        tr_variantDictAddInt(d, key, tr_sessionGetQueueStalledMinutes(s));
        break;

    case TR_KEY_anti_brute_force_enabled:
        tr_variantDictAddBool(d, key, tr_sessionGetAntiBruteForceEnabled(s));
        break;

    case TR_KEY_anti_brute_force_threshold:
        tr_variantDictAddInt(d, key, tr_sessionGetAntiBruteForceThreshold(s));
        break;

    case TR_KEY_units:
        tr_formatter_get_units(tr_variantDictAddDict(d, key, 0));
        break;

    case TR_KEY_version:
        tr_variantDictAddStrView(d, key, LONG_VERSION_STRING);
        break;

    case TR_KEY_encryption:
        tr_variantDictAddStr(d, key, getEncryptionModeString(tr_sessionGetEncryption(s)));
        break;

    case TR_KEY_session_id:
        tr_variantDictAddStr(d, key, tr_session_id_get_current(s->session_id));
        break;
    }
}

static char const* sessionGet(tr_session* s, tr_variant* args_in, tr_variant* args_out, tr_rpc_idle_data* /*idle_data*/)
{
    if (tr_variant* fields = nullptr; tr_variantDictFindList(args_in, TR_KEY_fields, &fields))
    {
        size_t const field_count = tr_variantListSize(fields);

        for (size_t i = 0; i < field_count; ++i)
        {
            auto field_name = std::string_view{};
            if (!tr_variantGetStrView(tr_variantListChild(fields, i), &field_name))
            {
                continue;
            }

            auto const field_id = tr_quark_lookup(field_name);
            if (field_id)
            {
                addSessionField(s, args_out, *field_id);
            }
        }
    }
    else
    {
        for (tr_quark field_id = TR_KEY_NONE + 1; field_id < TR_N_KEYS; ++field_id)
        {
            addSessionField(s, args_out, field_id);
        }
    }

    return nullptr;
}

static char const* freeSpace(
    tr_session* /*session*/,
    tr_variant* args_in,
    tr_variant* args_out,
    tr_rpc_idle_data* /*idle_data*/)
{
    auto path = std::string_view{};

    if (!tr_variantDictFindStrView(args_in, TR_KEY_path, &path))
    {
        return "directory path argument is missing";
    }

    if (tr_sys_path_is_relative(path))
    {
        return "directory path is not absolute";
    }

    /* get the free space */
    auto const old_errno = errno;
    errno = 0;
    auto const dir_space = tr_dirSpace(path);
    char const* const err = dir_space.free < 0 || dir_space.total < 0 ? tr_strerror(errno) : nullptr;
    errno = old_errno;

    /* response */
    tr_variantDictAddStr(args_out, TR_KEY_path, path);
    tr_variantDictAddInt(args_out, TR_KEY_size_bytes, dir_space.free);
    tr_variantDictAddInt(args_out, TR_KEY_total_size, dir_space.total);
    return err;
}

/***
****
***/

static char const* sessionClose(
    tr_session* session,
    tr_variant* /*args_in*/,
    tr_variant* /*args_out*/,
    tr_rpc_idle_data* /*idle_data*/)
{
    notify(session, TR_RPC_SESSION_CLOSE, nullptr);
    return nullptr;
}

/***
****
***/

using handler = char const* (*)(tr_session*, tr_variant*, tr_variant*, struct tr_rpc_idle_data*);

struct rpc_method
{
    std::string_view name;
    bool immediate;
    handler func;
};

static auto constexpr Methods = std::array<rpc_method, 24>{ {
    { "blocklist-update"sv, false, blocklistUpdate },
    { "free-space"sv, true, freeSpace },
    { "group-get"sv, true, groupGet },
    { "group-set"sv, true, groupSet },
    { "port-test"sv, false, portTest },
    { "queue-move-bottom"sv, true, queueMoveBottom },
    { "queue-move-down"sv, true, queueMoveDown },
    { "queue-move-top"sv, true, queueMoveTop },
    { "queue-move-up"sv, true, queueMoveUp },
    { "session-close"sv, true, sessionClose },
    { "session-get"sv, true, sessionGet },
    { "session-set"sv, true, sessionSet },
    { "session-stats"sv, true, sessionStats },
    { "torrent-add"sv, false, torrentAdd },
    { "torrent-get"sv, true, torrentGet },
    { "torrent-reannounce"sv, true, torrentReannounce },
    { "torrent-remove"sv, true, torrentRemove },
    { "torrent-rename-path"sv, false, torrentRenamePath },
    { "torrent-set"sv, true, torrentSet },
    { "torrent-set-location"sv, true, torrentSetLocation },
    { "torrent-start"sv, true, torrentStart },
    { "torrent-start-now"sv, true, torrentStartNow },
    { "torrent-stop"sv, true, torrentStop },
    { "torrent-verify"sv, true, torrentVerify },
} };

static void noop_response_callback(tr_session* /*session*/, tr_variant* /*response*/, void* /*user_data*/)
{
}

void tr_rpc_request_exec_json(
    tr_session* session,
    tr_variant const* request,
    tr_rpc_response_func callback,
    void* callback_user_data)
{
    auto* const mutable_request = const_cast<tr_variant*>(request);
    tr_variant* args_in = tr_variantDictFind(mutable_request, TR_KEY_arguments);
    char const* result = nullptr;

    if (callback == nullptr)
    {
        callback = noop_response_callback;
    }

    // parse the request's method name
    auto sv = std::string_view{};
    rpc_method const* method = nullptr;
    if (!tr_variantDictFindStrView(mutable_request, TR_KEY_method, &sv))
    {
        result = "no method name";
    }
    else
    {
        auto const it = std::find_if(std::begin(Methods), std::end(Methods), [&sv](auto const& row) { return row.name == sv; });
        if (it == std::end(Methods))
        {
            result = "method name not recognized";
        }
        else
        {
            method = &*it;
        }
    }

    /* if we couldn't figure out which method to use, return an error */
    if (result != nullptr)
    {
        auto response = tr_variant{};
        tr_variantInitDict(&response, 3);
        tr_variantDictAddDict(&response, TR_KEY_arguments, 0);
        tr_variantDictAddStr(&response, TR_KEY_result, result);

        if (auto tag = int64_t{}; tr_variantDictFindInt(mutable_request, TR_KEY_tag, &tag))
        {
            tr_variantDictAddInt(&response, TR_KEY_tag, tag);
        }

        (*callback)(session, &response, callback_user_data);

        tr_variantFree(&response);
    }
    else if (method->immediate)
    {
        auto response = tr_variant{};
        tr_variantInitDict(&response, 3);
        tr_variant* const args_out = tr_variantDictAddDict(&response, TR_KEY_arguments, 0);
        result = (*method->func)(session, args_in, args_out, nullptr);

        if (result == nullptr)
        {
            result = "success";
        }

        tr_variantDictAddStr(&response, TR_KEY_result, result);

        if (auto tag = int64_t{}; tr_variantDictFindInt(mutable_request, TR_KEY_tag, &tag))
        {
            tr_variantDictAddInt(&response, TR_KEY_tag, tag);
        }

        (*callback)(session, &response, callback_user_data);

        tr_variantFree(&response);
    }
    else
    {
        auto* const data = tr_new0(struct tr_rpc_idle_data, 1);
        data->session = session;
        data->response = tr_new0(tr_variant, 1);
        tr_variantInitDict(data->response, 3);

        if (auto tag = int64_t{}; tr_variantDictFindInt(mutable_request, TR_KEY_tag, &tag))
        {
            tr_variantDictAddInt(data->response, TR_KEY_tag, tag);
        }

        data->args_out = tr_variantDictAddDict(data->response, TR_KEY_arguments, 0);
        data->callback = callback;
        data->callback_user_data = callback_user_data;
        result = (*method->func)(session, args_in, data->args_out, data);

        /* Async operation failed prematurely? Invoke callback or else client will not get a reply */
        if (result != nullptr)
        {
            tr_idle_function_done(data, result);
        }
    }
}

/**
 * Munge the URI into a usable form.
 *
 * We have very loose typing on this to make the URIs as simple as possible:
 * - anything not a 'tag' or 'method' is automatically in 'arguments'
 * - values that are all-digits are numbers
 * - values that are all-digits or commas are number lists
 * - all other values are strings
 */
void tr_rpc_parse_list_str(tr_variant* setme, std::string_view str)
{
    auto const values = tr_parseNumberRange(str);
    auto const valueCount = std::size(values);

    if (valueCount == 0)
    {
        tr_variantInitStr(setme, str);
    }
    else if (valueCount == 1)
    {
        tr_variantInitInt(setme, values[0]);
    }
    else
    {
        tr_variantInitList(setme, valueCount);

        for (auto const& value : values)
        {
            tr_variantListAddInt(setme, value);
        }
    }
}

void tr_rpc_request_exec_uri(
    tr_session* session,
    std::string_view request_uri,
    tr_rpc_response_func callback,
    void* callback_user_data)
{
    auto top = tr_variant{};
    tr_variantInitDict(&top, 3);
    tr_variant* const args = tr_variantDictAddDict(&top, TR_KEY_arguments, 0);

    if (auto const parsed = tr_urlParse(request_uri); parsed)
    {
        for (auto const& [key, val] : tr_url_query_view(parsed->query))
        {
            auto is_arg = key != "method"sv && key != "tag"sv;
            auto* const parent = is_arg ? args : &top;
            tr_rpc_parse_list_str(tr_variantDictAdd(parent, tr_quark_new(key)), val);
        }
    }

    tr_rpc_request_exec_json(session, &top, callback, callback_user_data);

    // cleanup
    tr_variantFree(&top);
}
