/*
 * This file Copyright (C) 2008-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <algorithm>
#include <cctype> /* isdigit */
#include <cerrno>
#include <cstdlib> /* strtol */
#include <cstring> /* strcmp */
#include <iterator>
#include <string_view>
#include <vector>

#ifndef ZLIB_CONST
#define ZLIB_CONST
#endif
#include <zlib.h>

#include <event2/buffer.h>

#include "transmission.h"
#include "completion.h"
#include "crypto-utils.h"
#include "error.h"
#include "fdlimit.h"
#include "file.h"
#include "log.h"
#include "platform-quota.h" /* tr_device_info_get_disk_space() */
#include "rpcimpl.h"
#include "session.h"
#include "session-id.h"
#include "stats.h"
#include "torrent.h"
#include "tr-assert.h"
#include "tr-macros.h"
#include "utils.h"
#include "variant.h"
#include "version.h"
#include "web.h"

#define RPC_VERSION 17
#define RPC_VERSION_MIN 14
#define RPC_VERSION_SEMVER "5.3.0"

#define RECENTLY_ACTIVE_SECONDS 60

using namespace std::literals;

#if 0
#define dbgmsg(fmt, ...) fprintf(stderr, "%s:%d " fmt "\n", __FILE__, __LINE__, __VA_ARGS__)
#else
#define dbgmsg(...) tr_logAddDeepNamed("RPC", __VA_ARGS__)
#endif

enum tr_format
{
    TR_FORMAT_OBJECT = 0,
    TR_FORMAT_TABLE
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

static void tr_idle_function_done(struct tr_rpc_idle_data* data, char const* result)
{
    if (result == nullptr)
    {
        result = "success";
    }

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
    char const* str = nullptr;
    tr_variant* ids = nullptr;

    if (tr_variantDictFindList(args, TR_KEY_ids, &ids))
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
            else if (tr_variantGetStr(node, &str, nullptr))
            {
                tor = tr_torrentFindFromHashString(session, str);
            }

            if (tor != nullptr)
            {
                torrents.push_back(tor);
            }
        }
    }
    else if (tr_variantDictFindInt(args, TR_KEY_ids, &id) || tr_variantDictFindInt(args, TR_KEY_id, &id))
    {
        tr_torrent* const tor = tr_torrentFindFromId(session, id);

        if (tor != nullptr)
        {
            torrents.push_back(tor);
        }
    }
    else if (tr_variantDictFindStr(args, TR_KEY_ids, &str, nullptr))
    {
        if (strcmp(str, "recently-active") == 0)
        {
            time_t const cutoff = tr_time() - RECENTLY_ACTIVE_SECONDS;

            torrents.reserve(std::size(session->torrents));
            std::copy_if(
                std::begin(session->torrents),
                std::end(session->torrents),
                std::back_inserter(torrents),
                [&cutoff](tr_torrent* tor) { return tor->anyDate >= cutoff; });
        }
        else
        {
            tr_torrent* const tor = tr_torrentFindFromHashString(session, str);

            if (tor != nullptr)
            {
                torrents.push_back(tor);
            }
        }
    }
    else // all of them
    {
        torrents.reserve(std::size(session->torrents));
        std::copy(std::begin(session->torrents), std::end(session->torrents), std::back_inserter(torrents));
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
        if (tor->isRunning || tr_torrentIsQueued(tor))
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
    tr_variantDictFindBool(args_in, TR_KEY_delete_local_data, &delete_flag);

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
        tr_torrentVerify(tor, nullptr, nullptr);
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
        tr_variantListAddStr(list, label);
    }
}

static void addFileStats(tr_torrent const* tor, tr_variant* list)
{
    auto const* const info = tr_torrentInfo(tor);
    for (tr_file_index_t i = 0; i < info->fileCount; ++i)
    {
        auto const* const file = &info->files[i];
        tr_variant* const d = tr_variantListAddDict(list, 3);
        tr_variantDictAddInt(d, TR_KEY_bytesCompleted, tr_torrentFileProgress(tor, i).bytes_completed);
        tr_variantDictAddInt(d, TR_KEY_priority, file->priority);
        tr_variantDictAddBool(d, TR_KEY_wanted, !file->dnd);
    }
}

static void addFiles(tr_torrent const* tor, tr_variant* list)
{
    auto const* const info = tr_torrentInfo(tor);
    for (tr_file_index_t i = 0; i < info->fileCount; ++i)
    {
        tr_file const* file = &info->files[i];
        tr_variant* d = tr_variantListAddDict(list, 3);
        tr_variantDictAddInt(d, TR_KEY_bytesCompleted, tr_torrentFileProgress(tor, i).bytes_completed);
        tr_variantDictAddInt(d, TR_KEY_length, file->length);
        tr_variantDictAddStr(d, TR_KEY_name, file->name);
    }
}

static void addWebseeds(tr_info const* info, tr_variant* webseeds)
{
    for (unsigned int i = 0; i < info->webseedCount; ++i)
    {
        tr_variantListAddStr(webseeds, info->webseeds[i]);
    }
}

static void addTrackers(tr_info const* info, tr_variant* trackers)
{
    for (unsigned int i = 0; i < info->trackerCount; ++i)
    {
        tr_tracker_info const* t = &info->trackers[i];
        tr_variant* d = tr_variantListAddDict(trackers, 4);
        tr_variantDictAddStr(d, TR_KEY_announce, t->announce);
        tr_variantDictAddInt(d, TR_KEY_id, t->id);
        tr_variantDictAddStr(d, TR_KEY_scrape, t->scrape);
        tr_variantDictAddInt(d, TR_KEY_tier, t->tier);
    }
}

static void addTrackerStats(tr_tracker_stat const* st, int n, tr_variant* list)
{
    for (int i = 0; i < n; ++i)
    {
        tr_tracker_stat const* s = &st[i];
        tr_variant* d = tr_variantListAddDict(list, 26);
        tr_variantDictAddStr(d, TR_KEY_announce, s->announce);
        tr_variantDictAddInt(d, TR_KEY_announceState, s->announceState);
        tr_variantDictAddInt(d, TR_KEY_downloadCount, s->downloadCount);
        tr_variantDictAddBool(d, TR_KEY_hasAnnounced, s->hasAnnounced);
        tr_variantDictAddBool(d, TR_KEY_hasScraped, s->hasScraped);
        tr_variantDictAddStr(d, TR_KEY_host, s->host);
        tr_variantDictAddInt(d, TR_KEY_id, s->id);
        tr_variantDictAddBool(d, TR_KEY_isBackup, s->isBackup);
        tr_variantDictAddInt(d, TR_KEY_lastAnnouncePeerCount, s->lastAnnouncePeerCount);
        tr_variantDictAddStr(d, TR_KEY_lastAnnounceResult, s->lastAnnounceResult);
        tr_variantDictAddInt(d, TR_KEY_lastAnnounceStartTime, s->lastAnnounceStartTime);
        tr_variantDictAddBool(d, TR_KEY_lastAnnounceSucceeded, s->lastAnnounceSucceeded);
        tr_variantDictAddInt(d, TR_KEY_lastAnnounceTime, s->lastAnnounceTime);
        tr_variantDictAddBool(d, TR_KEY_lastAnnounceTimedOut, s->lastAnnounceTimedOut);
        tr_variantDictAddStr(d, TR_KEY_lastScrapeResult, s->lastScrapeResult);
        tr_variantDictAddInt(d, TR_KEY_lastScrapeStartTime, s->lastScrapeStartTime);
        tr_variantDictAddBool(d, TR_KEY_lastScrapeSucceeded, s->lastScrapeSucceeded);
        tr_variantDictAddInt(d, TR_KEY_lastScrapeTime, s->lastScrapeTime);
        tr_variantDictAddBool(d, TR_KEY_lastScrapeTimedOut, s->lastScrapeTimedOut);
        tr_variantDictAddInt(d, TR_KEY_leecherCount, s->leecherCount);
        tr_variantDictAddInt(d, TR_KEY_nextAnnounceTime, s->nextAnnounceTime);
        tr_variantDictAddInt(d, TR_KEY_nextScrapeTime, s->nextScrapeTime);
        tr_variantDictAddStr(d, TR_KEY_scrape, s->scrape);
        tr_variantDictAddInt(d, TR_KEY_scrapeState, s->scrapeState);
        tr_variantDictAddInt(d, TR_KEY_seederCount, s->seederCount);
        tr_variantDictAddInt(d, TR_KEY_tier, s->tier);
    }
}

static void addPeers(tr_torrent* tor, tr_variant* list)
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
        tr_variantDictAddInt(d, TR_KEY_rateToClient, toSpeedBytes(peer->rateToClient_KBps));
        tr_variantDictAddInt(d, TR_KEY_rateToPeer, toSpeedBytes(peer->rateToPeer_KBps));
    }

    tr_torrentPeersFree(peers, peerCount);
}

static void initField(
    tr_torrent* const tor,
    tr_info const* const inf,
    tr_stat const* const st,
    tr_variant* const initme,
    tr_quark key)
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

    case TR_KEY_bandwidthPriority:
        tr_variantInitInt(initme, tr_torrentGetPriority(tor));
        break;

    case TR_KEY_comment:
        tr_variantInitStr(initme, std::string_view{ inf->comment != nullptr ? inf->comment : "" });
        break;

    case TR_KEY_corruptEver:
        tr_variantInitInt(initme, st->corruptEver);
        break;

    case TR_KEY_creator:
        tr_variantInitStr(initme, std::string_view{ inf->creator != nullptr ? inf->creator : "" });
        break;

    case TR_KEY_dateCreated:
        tr_variantInitInt(initme, inf->dateCreated);
        break;

    case TR_KEY_desiredAvailable:
        tr_variantInitInt(initme, st->desiredAvailable);
        break;

    case TR_KEY_doneDate:
        tr_variantInitInt(initme, st->doneDate);
        break;

    case TR_KEY_downloadDir:
        tr_variantInitStr(initme, tr_torrentGetDownloadDir(tor));
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
        tr_variantInitStr(initme, st->errorString);
        break;

    case TR_KEY_eta:
        tr_variantInitInt(initme, st->eta);
        break;

    case TR_KEY_file_count:
        tr_variantInitInt(initme, inf->fileCount);
        break;

    case TR_KEY_files:
        tr_variantInitList(initme, inf->fileCount);
        addFiles(tor, initme);
        break;

    case TR_KEY_fileStats:
        tr_variantInitList(initme, inf->fileCount);
        addFileStats(tor, initme);
        break;

    case TR_KEY_hashString:
        tr_variantInitStr(initme, tor->info.hashString);
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
        tr_variantInitBool(initme, tr_torrentIsPrivate(tor));
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
        tr_variantInitInt(initme, st->manualAnnounceTime);
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
        tr_variantInitStr(initme, tr_torrentName(tor));
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
            int const* f = st->peersFrom;
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
        if (tr_torrentHasMetadata(tor))
        {
            auto const bytes = tr_torrentCreatePieceBitfield(tor);
            auto* enc = static_cast<char*>(tr_base64_encode(bytes.data(), std::size(bytes), nullptr));
            tr_variantInitStr(initme, enc != nullptr ? std::string_view{ enc } : ""sv);
            tr_free(enc);
        }
        else
        {
            tr_variantInitStr(initme, ""sv);
        }

        break;

    case TR_KEY_pieceCount:
        tr_variantInitInt(initme, inf->pieceCount);
        break;

    case TR_KEY_pieceSize:
        tr_variantInitInt(initme, inf->pieceSize);
        break;

    case TR_KEY_primary_mime_type:
        tr_variantInitStr(initme, tr_torrentPrimaryMimeType(tor));
        break;

    case TR_KEY_priorities:
        tr_variantInitList(initme, inf->fileCount);
        for (tr_file_index_t i = 0; i < inf->fileCount; ++i)
        {
            tr_variantListAddInt(initme, inf->files[i].priority);
        }

        break;

    case TR_KEY_queuePosition:
        tr_variantInitInt(initme, st->queuePosition);
        break;

    case TR_KEY_etaIdle:
        tr_variantInitInt(initme, st->etaIdle);
        break;

    case TR_KEY_rateDownload:
        tr_variantInitInt(initme, toSpeedBytes(st->pieceDownloadSpeed_KBps));
        break;

    case TR_KEY_rateUpload:
        tr_variantInitInt(initme, toSpeedBytes(st->pieceUploadSpeed_KBps));
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
        tr_variantDictAddStr(initme, key, inf->source);
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
        tr_variantInitList(initme, inf->trackerCount);
        addTrackers(inf, initme);
        break;

    case TR_KEY_trackerStats:
        {
            auto n = int{};
            tr_tracker_stat* s = tr_torrentTrackers(tor, &n);
            tr_variantInitList(initme, n);
            addTrackerStats(s, n, initme);
            tr_torrentTrackersFree(s, n);
            break;
        }

    case TR_KEY_torrentFile:
        tr_variantInitStr(initme, inf->torrent);
        break;

    case TR_KEY_totalSize:
        tr_variantInitInt(initme, inf->totalSize);
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
        tr_variantInitList(initme, inf->fileCount);

        for (tr_file_index_t i = 0; i < inf->fileCount; ++i)
        {
            tr_variantListAddInt(initme, inf->files[i].dnd ? 0 : 1);
        }

        break;

    case TR_KEY_webseeds:
        tr_variantInitList(initme, inf->webseedCount);
        addWebseeds(inf, initme);
        break;

    case TR_KEY_webseedsSendingToUs:
        tr_variantInitInt(initme, st->webseedsSendingToUs);
        break;

    default:
        break;
    }
}

static void addTorrentInfo(tr_torrent* tor, tr_format format, tr_variant* entry, tr_quark const* fields, size_t fieldCount)
{
    if (format == TR_FORMAT_TABLE)
    {
        tr_variantInitList(entry, fieldCount);
    }
    else
    {
        tr_variantInitDict(entry, fieldCount);
    }

    if (fieldCount > 0)
    {
        tr_info const* const inf = tr_torrentInfo(tor);
        tr_stat const* const st = tr_torrentStat(tor);

        for (size_t i = 0; i < fieldCount; ++i)
        {
            tr_variant* child = format == TR_FORMAT_TABLE ? tr_variantListAdd(entry) : tr_variantDictAdd(entry, fields[i]);

            initField(tor, inf, st, child, fields[i]);
        }
    }
}

static char const* torrentGet(tr_session* session, tr_variant* args_in, tr_variant* args_out, tr_rpc_idle_data* /*idle_data*/)
{
    auto const torrents = getTorrents(session, args_in);
    tr_variant* const list = tr_variantDictAddList(args_out, TR_KEY_torrents, std::size(torrents) + 1);

    char const* strVal = nullptr;
    tr_format const format = tr_variantDictFindStr(args_in, TR_KEY_format, &strVal, nullptr) && strcmp(strVal, "table") == 0 ?
        TR_FORMAT_TABLE :
        TR_FORMAT_OBJECT;

    if (tr_variantDictFindStr(args_in, TR_KEY_ids, &strVal, nullptr) && strcmp(strVal, "recently-active") == 0)
    {
        int n = 0;
        time_t const now = tr_time();
        int const interval = RECENTLY_ACTIVE_SECONDS;
        tr_variant* removed_out = tr_variantDictAddList(args_out, TR_KEY_removed, 0);

        tr_variant* d = nullptr;
        while ((d = tr_variantListChild(&session->removedTorrents, n)) != nullptr)
        {
            auto date = int64_t{};
            auto id = int64_t{};

            if (tr_variantDictFindInt(d, TR_KEY_date, &date) && date >= now - interval &&
                tr_variantDictFindInt(d, TR_KEY_id, &id))
            {
                tr_variantListAddInt(removed_out, id);
            }

            ++n;
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
        /* make an array of property name quarks */
        size_t keyCount = 0;
        size_t const n = tr_variantListSize(fields);
        tr_quark* keys = tr_new(tr_quark, n);
        for (size_t i = 0; i < n; ++i)
        {
            auto len = size_t{};
            if (tr_variantGetStr(tr_variantListChild(fields, i), &strVal, &len))
            {
                keys[keyCount++] = tr_quark_new(std::string_view{ strVal, len });
            }
        }

        if (format == TR_FORMAT_TABLE)
        {
            /* first entry is an array of property names */
            tr_variant* names = tr_variantListAddList(list, keyCount);
            for (size_t i = 0; i < keyCount; ++i)
            {
                tr_variantListAddQuark(names, keys[i]);
            }
        }

        for (auto* tor : torrents)
        {
            addTorrentInfo(tor, format, tr_variantListAdd(list), keys, keyCount);
        }

        tr_free(keys);
    }

    return errmsg;
}

/***
****
***/

static char const* setLabels(tr_torrent* tor, tr_variant* list)
{
    char const* errmsg = nullptr;
    auto labels = tr_labels_t{};

    size_t const n = tr_variantListSize(list);
    for (size_t i = 0; i < n; ++i)
    {
        char const* str = nullptr;
        auto str_len = size_t{};
        if (tr_variantGetStr(tr_variantListChild(list, i), &str, &str_len) && str != nullptr)
        {
            char* label = tr_strndup(str, str_len);
            tr_strstrip(label);
            if (tr_str_is_empty(label))
            {
                errmsg = "labels cannot be empty";
            }

            if (errmsg == nullptr && strchr(str, ',') != nullptr)
            {
                errmsg = "labels cannot contain comma (,) character";
            }

            if (errmsg == nullptr && labels.count(label) != 0)
            {
                errmsg = "labels cannot contain duplicates";
            }

            labels.emplace(label);
            tr_free(label);

            if (errmsg != nullptr)
            {
                break;
            }
        }
    }

    if (errmsg == nullptr)
    {
        tr_torrentSetLabels(tor, std::move(labels));
    }

    return errmsg;
}

static char const* setFilePriorities(tr_torrent* tor, int priority, tr_variant* list)
{
    int fileCount = 0;
    size_t const n = tr_variantListSize(list);
    char const* errmsg = nullptr;
    tr_file_index_t* files = tr_new0(tr_file_index_t, tor->info.fileCount);

    if (n != 0)
    {
        for (size_t i = 0; i < n; ++i)
        {
            auto tmp = int64_t{};
            if (tr_variantGetInt(tr_variantListChild(list, i), &tmp))
            {
                if (0 <= tmp && tmp < tor->info.fileCount)
                {
                    files[fileCount++] = tmp;
                }
                else
                {
                    errmsg = "file index out of range";
                }
            }
        }
    }
    else /* if empty set, apply to all */
    {
        for (tr_file_index_t t = 0; t < tor->info.fileCount; ++t)
        {
            files[fileCount++] = t;
        }
    }

    if (fileCount != 0)
    {
        tr_torrentSetFilePriorities(tor, files, fileCount, priority);
    }

    tr_free(files);
    return errmsg;
}

static char const* setFileDLs(tr_torrent* tor, bool do_download, tr_variant* list)
{
    char const* errmsg = nullptr;

    int fileCount = 0;
    size_t const n = tr_variantListSize(list);
    tr_file_index_t* files = tr_new0(tr_file_index_t, tor->info.fileCount);

    if (n != 0) /* if argument list, process them */
    {
        for (size_t i = 0; i < n; ++i)
        {
            auto tmp = int64_t{};
            if (tr_variantGetInt(tr_variantListChild(list, i), &tmp))
            {
                if (0 <= tmp && tmp < tor->info.fileCount)
                {
                    files[fileCount++] = tmp;
                }
                else
                {
                    errmsg = "file index out of range";
                }
            }
        }
    }
    else /* if empty set, apply to all */
    {
        for (tr_file_index_t t = 0; t < tor->info.fileCount; ++t)
        {
            files[fileCount++] = t;
        }
    }

    if (fileCount != 0)
    {
        tr_torrentSetFileDLs(tor, files, fileCount, do_download);
    }

    tr_free(files);
    return errmsg;
}

static bool findAnnounceUrl(tr_tracker_info const* t, int n, char const* url, int* pos)
{
    bool found = false;

    for (int i = 0; i < n; ++i)
    {
        if (strcmp(t[i].announce, url) == 0)
        {
            found = true;

            if (pos != nullptr)
            {
                *pos = i;
            }

            break;
        }
    }

    return found;
}

static int copyTrackers(tr_tracker_info* tgt, tr_tracker_info const* src, int n)
{
    int maxTier = -1;

    for (int i = 0; i < n; ++i)
    {
        tgt[i].tier = src[i].tier;
        tgt[i].announce = tr_strdup(src[i].announce);
        maxTier = std::max(maxTier, src[i].tier);
    }

    return maxTier;
}

static void freeTrackers(tr_tracker_info* trackers, int n)
{
    for (int i = 0; i < n; ++i)
    {
        tr_free(trackers[i].announce);
    }

    tr_free(trackers);
}

static char const* addTrackerUrls(tr_torrent* tor, tr_variant* urls)
{
    char const* errmsg = nullptr;

    /* make a working copy of the existing announce list */
    auto const* const inf = tr_torrentInfo(tor);
    int n = inf->trackerCount;
    auto* const trackers = tr_new0(tr_tracker_info, n + tr_variantListSize(urls));
    int tier = copyTrackers(trackers, inf->trackers, n);

    /* and add the new ones */
    auto i = int{ 0 };
    auto changed = bool{ false };
    tr_variant const* val = nullptr;
    while ((val = tr_variantListChild(urls, i)) != nullptr)
    {
        char const* announce = nullptr;

        if (tr_variantGetStr(val, &announce, nullptr) && tr_urlIsValidTracker(announce) &&
            !findAnnounceUrl(trackers, n, announce, nullptr))
        {
            trackers[n].tier = ++tier; /* add a new tier */
            trackers[n].announce = tr_strdup(announce);
            ++n;
            changed = true;
        }

        ++i;
    }

    if (!changed)
    {
        errmsg = "invalid argument";
    }
    else if (!tr_torrentSetAnnounceList(tor, trackers, n))
    {
        errmsg = "error setting announce list";
    }

    freeTrackers(trackers, n);
    return errmsg;
}

static char const* replaceTrackers(tr_torrent* tor, tr_variant* urls)
{
    char const* errmsg = nullptr;

    /* make a working copy of the existing announce list */
    auto const* const inf = tr_torrentInfo(tor);
    int const n = inf->trackerCount;
    auto* const trackers = tr_new0(tr_tracker_info, n);
    copyTrackers(trackers, inf->trackers, n);

    /* make the substitutions... */
    bool changed = false;
    for (size_t i = 0, url_count = tr_variantListSize(urls); i + 1 < url_count; i += 2)
    {
        auto len = size_t{};
        auto pos = int64_t{};
        char const* newval = nullptr;

        if (tr_variantGetInt(tr_variantListChild(urls, i), &pos) &&
            tr_variantGetStr(tr_variantListChild(urls, i + 1), &newval, &len) && tr_urlIsValidTracker(newval) && pos < n &&
            pos >= 0)
        {
            tr_free(trackers[pos].announce);
            trackers[pos].announce = tr_strndup(newval, len);
            changed = true;
        }
    }

    if (!changed)
    {
        errmsg = "invalid argument";
    }
    else if (!tr_torrentSetAnnounceList(tor, trackers, n))
    {
        errmsg = "error setting announce list";
    }

    freeTrackers(trackers, n);
    return errmsg;
}

static char const* removeTrackers(tr_torrent* tor, tr_variant* ids)
{
    /* make a working copy of the existing announce list */
    tr_info const* inf = tr_torrentInfo(tor);
    int n = inf->trackerCount;
    int* tids = tr_new0(int, n);
    tr_tracker_info* trackers = tr_new0(tr_tracker_info, n);
    copyTrackers(trackers, inf->trackers, n);

    /* remove the ones specified in the urls list */
    int i = 0;
    int t = 0;
    tr_variant const* val = nullptr;
    while ((val = tr_variantListChild(ids, i)) != nullptr)
    {
        auto pos = int64_t{};

        if (tr_variantGetInt(val, &pos) && 0 <= pos && pos < n)
        {
            tids[t++] = (int)pos;
        }

        ++i;
    }

    /* sort trackerIds and remove from largest to smallest so there is no need to recalculate array indicies */
    std::sort(tids, tids + t);

    bool changed = false;
    int dup = -1;
    while (t-- != 0)
    {
        /* check for duplicates */
        if (tids[t] == dup)
        {
            continue;
        }

        tr_removeElementFromArray(trackers, tids[t], sizeof(tr_tracker_info), n);
        --n;

        dup = tids[t];
        changed = true;
    }

    char const* errmsg = nullptr;
    if (!changed)
    {
        errmsg = "invalid argument";
    }
    else if (!tr_torrentSetAnnounceList(tor, trackers, n))
    {
        errmsg = "error setting announce list";
    }

    freeTrackers(trackers, n);
    tr_free(tids);
    return errmsg;
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
            tr_priority_t const priority = (tr_priority_t)tmp;

            if (tr_isPriority(priority))
            {
                tr_torrentSetPriority(tor, priority);
            }
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
    char const* location = nullptr;

    if (!tr_variantDictFindStr(args_in, TR_KEY_location, &location, nullptr))
    {
        return "no location";
    }

    if (tr_sys_path_is_relative(location))
    {
        return "new location path is not absolute";
    }

    auto move = bool{};
    tr_variantDictFindBool(args_in, TR_KEY_move, &move);

    for (auto* tor : getTorrents(session, args_in))
    {
        tr_torrentSetLocation(tor, location, move, nullptr, nullptr);
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

    char const* const result = error == 0 ? nullptr : tr_strerror(error);
    tr_idle_function_done(data, result);
}

static char const* torrentRenamePath(
    tr_session* session,
    tr_variant* args_in,
    tr_variant* /*args_out*/,
    struct tr_rpc_idle_data* idle_data)
{
    char const* errmsg = nullptr;

    char const* oldpath = nullptr;
    (void)tr_variantDictFindStr(args_in, TR_KEY_path, &oldpath, nullptr);
    char const* newname = nullptr;
    (void)tr_variantDictFindStr(args_in, TR_KEY_name, &newname, nullptr);

    auto const torrents = getTorrents(session, args_in);
    if (std::size(torrents) == 1)
    {
        tr_torrentRenamePath(torrents[0], oldpath, newname, torrentRenamePathDone, idle_data);
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

static void portTested(
    tr_session* /*session*/,
    bool /*did_connect*/,
    bool /*did_timeout*/,
    long response_code,
    void const* response,
    size_t response_byte_count,
    void* user_data)
{
    char result[1024];
    auto* data = static_cast<struct tr_rpc_idle_data*>(user_data);

    if (response_code != 200)
    {
        tr_snprintf(
            result,
            sizeof(result),
            "portTested: http error %ld: %s",
            response_code,
            tr_webGetResponseStr(response_code));
    }
    else /* success */
    {
        bool const isOpen = response_byte_count != 0 && *(char const*)response == '1';
        tr_variantDictAddBool(data->args_out, TR_KEY_port_is_open, isOpen);
        tr_snprintf(result, sizeof(result), "success");
    }

    tr_idle_function_done(data, result);
}

static char const* portTest(
    tr_session* session,
    tr_variant* /*args_in*/,
    tr_variant* /*args_out*/,
    struct tr_rpc_idle_data* idle_data)
{
    int const port = tr_sessionGetPeerPort(session);
    char* url = tr_strdup_printf("https://portcheck.transmissionbt.com/%d", port);
    tr_webRun(session, url, portTested, idle_data);
    tr_free(url);
    return nullptr;
}

/***
****
***/

static void gotNewBlocklist(
    tr_session* session,
    bool /*did_connect*/,
    bool /*did_timeout*/,
    long response_code,
    void const* response,
    size_t response_byte_count,
    void* user_data)
{
    char result[1024];
    auto* data = static_cast<struct tr_rpc_idle_data*>(user_data);

    *result = '\0';

    if (response_code != 200)
    {
        tr_snprintf(
            result,
            sizeof(result),
            "gotNewBlocklist: http error %ld: %s",
            response_code,
            tr_webGetResponseStr(response_code));
    }
    else /* successfully fetched the blocklist... */
    {
        z_stream stream;
        char const* configDir = tr_sessionGetConfigDir(session);
        size_t const buflen = 1024 * 128; /* 128 KiB buffer */
        auto* const buf = static_cast<uint8_t*>(tr_malloc(buflen));
        tr_error* error = nullptr;

        /* this is an odd Magic Number required by zlib to enable gz support.
           See zlib's inflateInit2() documentation for a full description */
        int const windowBits = 15 + 32;

        stream.zalloc = (alloc_func)Z_NULL;
        stream.zfree = (free_func)Z_NULL;
        stream.opaque = (voidpf)Z_NULL;
        stream.next_in = static_cast<Bytef const*>(response);
        stream.avail_in = response_byte_count;
        inflateInit2(&stream, windowBits);

        char* const filename = tr_buildPath(configDir, "blocklist.tmp.XXXXXX", nullptr);
        tr_sys_file_t const fd = tr_sys_file_open_temp(filename, &error);

        if (fd == TR_BAD_SYS_FILE)
        {
            tr_snprintf(result, sizeof(result), _("Couldn't save file \"%1$s\": %2$s"), filename, error->message);
            tr_error_clear(&error);
        }

        auto err = int{};
        for (;;)
        {
            stream.next_out = static_cast<Bytef*>(buf);
            stream.avail_out = buflen;
            err = inflate(&stream, Z_NO_FLUSH);

            if ((stream.avail_out < buflen) && (!tr_sys_file_write(fd, buf, buflen - stream.avail_out, nullptr, &error)))
            {
                tr_snprintf(result, sizeof(result), _("Couldn't save file \"%1$s\": %2$s"), filename, error->message);
                tr_error_clear(&error);
                break;
            }

            if (err != Z_OK)
            {
                if (err != Z_STREAM_END && err != Z_DATA_ERROR)
                {
                    tr_snprintf(result, sizeof(result), _("Error uncompressing blocklist: %s (%d)"), zError(err), err);
                }

                break;
            }
        }

        inflateEnd(&stream);

        if ((err == Z_DATA_ERROR) && // couldn't inflate it... it's probably already uncompressed
            !tr_sys_file_write(fd, response, response_byte_count, nullptr, &error))
        {
            tr_snprintf(result, sizeof(result), _("Couldn't save file \"%1$s\": %2$s"), filename, error->message);
            tr_error_clear(&error);
        }

        tr_sys_file_close(fd, nullptr);

        if (!tr_str_is_empty(result))
        {
            tr_logAddError("%s", result);
        }
        else
        {
            /* feed it to the session and give the client a response */
            int const rule_count = tr_blocklistSetContent(session, filename);
            tr_variantDictAddInt(data->args_out, TR_KEY_blocklist_size, rule_count);
            tr_snprintf(result, sizeof(result), "success");
        }

        tr_sys_path_remove(filename, nullptr);
        tr_free(filename);
        tr_free(buf);
    }

    tr_idle_function_done(data, result);
}

static char const* blocklistUpdate(
    tr_session* session,
    tr_variant* /*args_in*/,
    tr_variant* /*args_out*/,
    struct tr_rpc_idle_data* idle_data)
{
    tr_webRun(session, session->blocklist_url, gotNewBlocklist, idle_data);
    return nullptr;
}

/***
****
***/

static void addTorrentImpl(struct tr_rpc_idle_data* data, tr_ctor* ctor)
{
    auto err = int{};
    auto duplicate_id = int{};
    tr_torrent* tor = tr_torrentNew(ctor, &err, &duplicate_id);
    tr_ctorFree(ctor);

    auto key = tr_quark{};
    char const* result = "invalid or corrupt torrent file";
    if (err == 0)
    {
        key = TR_KEY_torrent_added;
        result = nullptr;
    }
    else if (err == TR_PARSE_DUPLICATE)
    {
        tor = tr_torrentFindFromId(data->session, duplicate_id);
        key = TR_KEY_torrent_duplicate;
        result = "duplicate torrent";
    }

    if (tor != nullptr && key != 0)
    {
        tr_quark const fields[] = {
            TR_KEY_id,
            TR_KEY_name,
            TR_KEY_hashString,
        };

        addTorrentInfo(tor, TR_FORMAT_OBJECT, tr_variantDictAdd(data->args_out, key), fields, TR_N_ELEMENTS(fields));

        if (result == nullptr)
        {
            notify(data->session, TR_RPC_TORRENT_ADDED, tor);
        }

        result = nullptr;
    }

    tr_idle_function_done(data, result);
}

struct add_torrent_idle_data
{
    struct tr_rpc_idle_data* data;
    tr_ctor* ctor;
};

static void gotMetadataFromURL(
    tr_session* /*session*/,
    bool /*did_connect*/,
    bool /*did_timeout*/,
    long response_code,
    void const* response,
    size_t response_byte_count,
    void* user_data)
{
    auto* data = static_cast<struct add_torrent_idle_data*>(user_data);

    dbgmsg(
        "torrentAdd: HTTP response code was %ld (%s); response length was %zu bytes",
        response_code,
        tr_webGetResponseStr(response_code),
        response_byte_count);

    if (response_code == 200 || response_code == 221) /* http or ftp success.. */
    {
        tr_ctorSetMetainfo(data->ctor, response, response_byte_count);
        addTorrentImpl(data->data, data->ctor);
    }
    else
    {
        char result[1024];
        tr_snprintf(
            result,
            sizeof(result),
            "gotMetadataFromURL: http error %ld: %s",
            response_code,
            tr_webGetResponseStr(response_code));
        tr_idle_function_done(data->data, result);
    }

    tr_free(data);
}

static bool isCurlURL(char const* filename)
{
    if (filename == nullptr)
    {
        return false;
    }

    return strncmp(filename, "ftp://", 6) == 0 || strncmp(filename, "http://", 7) == 0 || strncmp(filename, "https://", 8) == 0;
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

    char const* filename = nullptr;
    (void)tr_variantDictFindStr(args_in, TR_KEY_filename, &filename, nullptr);

    char const* metainfo_base64 = nullptr;
    (void)tr_variantDictFindStr(args_in, TR_KEY_metainfo, &metainfo_base64, nullptr);

    if (filename == nullptr && metainfo_base64 == nullptr)
    {
        return "no filename or metainfo specified";
    }

    char const* download_dir = nullptr;

    if (tr_variantDictFindStr(args_in, TR_KEY_download_dir, &download_dir, nullptr) && tr_sys_path_is_relative(download_dir))
    {
        return "download directory path is not absolute";
    }

    auto i = int64_t{};
    auto boolVal = bool{};
    tr_variant* l = nullptr;
    tr_ctor* ctor = tr_ctorNew(session);

    /* set the optional arguments */

    char const* cookies = nullptr;
    (void)tr_variantDictFindStr(args_in, TR_KEY_cookies, &cookies, nullptr);

    if (download_dir != nullptr)
    {
        tr_ctorSetDownloadDir(ctor, TR_FORCE, download_dir);
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

    dbgmsg("torrentAdd: filename is \"%s\"", filename ? filename : " (null)");

    if (isCurlURL(filename))
    {
        struct add_torrent_idle_data* d = tr_new0(struct add_torrent_idle_data, 1);
        d->data = idle_data;
        d->ctor = ctor;
        tr_webRunWithCookies(session, filename, cookies, gotMetadataFromURL, d);
    }
    else
    {
        char* fname = tr_strstrip(tr_strdup(filename));

        if (fname == nullptr)
        {
            auto len = size_t{};
            auto* const metainfo = static_cast<char*>(tr_base64_decode_str(metainfo_base64, &len));
            tr_ctorSetMetainfo(ctor, (uint8_t*)metainfo, len);
            tr_free(metainfo);
        }
        else if (strncmp(fname, "magnet:?", 8) == 0)
        {
            tr_ctorSetMetainfoFromMagnetLink(ctor, fname);
        }
        else
        {
            tr_ctorSetMetainfoFromFile(ctor, fname);
        }

        addTorrentImpl(idle_data, ctor);

        tr_free(fname);
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
    char const* download_dir = nullptr;
    char const* incomplete_dir = nullptr;

    if (tr_variantDictFindStr(args_in, TR_KEY_download_dir, &download_dir, nullptr) && tr_sys_path_is_relative(download_dir))
    {
        return "download directory path is not absolute";
    }

    if (tr_variantDictFindStr(args_in, TR_KEY_incomplete_dir, &incomplete_dir, nullptr) &&
        tr_sys_path_is_relative(incomplete_dir))
    {
        return "incomplete torrents directory path is not absolute";
    }

    auto boolVal = bool{};
    auto d = double{};
    auto i = int64_t{};
    char const* str = nullptr;

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
        tr_blocklistSetEnabled(session, boolVal);
    }

    if (tr_variantDictFindStr(args_in, TR_KEY_blocklist_url, &str, nullptr))
    {
        tr_blocklistSetURL(session, str);
    }

    if (download_dir != nullptr)
    {
        tr_sessionSetDownloadDir(session, download_dir);
    }

    if (tr_variantDictFindInt(args_in, TR_KEY_queue_stalled_minutes, &i))
    {
        tr_sessionSetQueueStalledMinutes(session, i);
    }

    if (tr_variantDictFindBool(args_in, TR_KEY_queue_stalled_enabled, &boolVal))
    {
        tr_sessionSetQueueStalledEnabled(session, boolVal);
    }

    if (tr_variantDictFindInt(args_in, TR_KEY_download_queue_size, &i))
    {
        tr_sessionSetQueueSize(session, TR_DOWN, (int)i);
    }

    if (tr_variantDictFindBool(args_in, TR_KEY_download_queue_enabled, &boolVal))
    {
        tr_sessionSetQueueEnabled(session, TR_DOWN, boolVal);
    }

    if (incomplete_dir != nullptr)
    {
        tr_sessionSetIncompleteDir(session, incomplete_dir);
    }

    if (tr_variantDictFindBool(args_in, TR_KEY_incomplete_dir_enabled, &boolVal))
    {
        tr_sessionSetIncompleteDirEnabled(session, boolVal);
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
        tr_sessionSetPeerPort(session, i);
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

    if (tr_variantDictFindStr(args_in, TR_KEY_script_torrent_added_filename, &str, nullptr))
    {
        tr_sessionSetScript(session, TR_SCRIPT_ON_TORRENT_ADDED, str);
    }

    if (tr_variantDictFindBool(args_in, TR_KEY_script_torrent_added_enabled, &boolVal))
    {
        tr_sessionSetScriptEnabled(session, TR_SCRIPT_ON_TORRENT_ADDED, boolVal);
    }

    if (tr_variantDictFindStr(args_in, TR_KEY_script_torrent_done_filename, &str, nullptr))
    {
        tr_sessionSetScript(session, TR_SCRIPT_ON_TORRENT_DONE, str);
    }

    if (tr_variantDictFindBool(args_in, TR_KEY_script_torrent_done_enabled, &boolVal))
    {
        tr_sessionSetScriptEnabled(session, TR_SCRIPT_ON_TORRENT_DONE, boolVal);
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

    if (tr_variantDictFindStr(args_in, TR_KEY_encryption, &str, nullptr))
    {
        if (tr_strcmp0(str, "required") == 0)
        {
            tr_sessionSetEncryption(session, TR_ENCRYPTION_REQUIRED);
        }
        else if (tr_strcmp0(str, "tolerated") == 0)
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

    int const total = std::size(session->torrents);
    int const running = std::count_if(
        std::begin(session->torrents),
        std::end(session->torrents),
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

static void addSessionField(tr_session* s, tr_variant* d, tr_quark key)
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
        tr_variantDictAddBool(d, key, tr_blocklistIsEnabled(s));
        break;

    case TR_KEY_blocklist_url:
        tr_variantDictAddStr(d, key, tr_blocklistGetURL(s));
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

    case TR_KEY_download_dir:
        tr_variantDictAddStr(d, key, tr_sessionGetDownloadDir(s));
        break;

    case TR_KEY_download_dir_free_space:
        tr_variantDictAddInt(d, key, tr_device_info_get_disk_space(s->downloadDir).free);
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
        tr_variantDictAddStr(d, key, tr_sessionGetIncompleteDir(s));
        break;

    case TR_KEY_incomplete_dir_enabled:
        tr_variantDictAddBool(d, key, tr_sessionIsIncompleteDirEnabled(s));
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
        tr_variantDictAddInt(d, key, tr_sessionGetPeerPort(s));
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
        tr_variantDictAddInt(d, key, RPC_VERSION);
        break;

    case TR_KEY_rpc_version_semver:
        tr_variantDictAddStr(d, key, RPC_VERSION_SEMVER);
        break;

    case TR_KEY_rpc_version_minimum:
        tr_variantDictAddInt(d, key, RPC_VERSION_MIN);
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
        tr_variantDictAddStr(d, key, tr_sessionGetScript(s, TR_SCRIPT_ON_TORRENT_ADDED));
        break;

    case TR_KEY_script_torrent_added_enabled:
        tr_variantDictAddBool(d, key, tr_sessionIsScriptEnabled(s, TR_SCRIPT_ON_TORRENT_ADDED));
        break;

    case TR_KEY_script_torrent_done_filename:
        tr_variantDictAddStr(d, key, tr_sessionGetScript(s, TR_SCRIPT_ON_TORRENT_DONE));
        break;

    case TR_KEY_script_torrent_done_enabled:
        tr_variantDictAddBool(d, key, tr_sessionIsScriptEnabled(s, TR_SCRIPT_ON_TORRENT_DONE));
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
        tr_variantDictAddStr(d, key, LONG_VERSION_STRING);
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
    tr_variant* fields = nullptr;
    if (tr_variantDictFindList(args_in, TR_KEY_fields, &fields))
    {
        size_t const field_count = tr_variantListSize(fields);

        for (size_t i = 0; i < field_count; ++i)
        {
            char const* field_name = nullptr;
            auto field_name_len = size_t{};
            if (!tr_variantGetStr(tr_variantListChild(fields, i), &field_name, &field_name_len))
            {
                continue;
            }

            auto field_id = tr_quark{};
            if (!tr_quark_lookup(field_name, field_name_len, &field_id))
            {
                continue;
            }

            addSessionField(s, args_out, field_id);
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
    char const* path = nullptr;
    if (!tr_variantDictFindStr(args_in, TR_KEY_path, &path, nullptr))
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
    auto const dir_space = tr_getDirSpace(path);
    char const* const err = dir_space.free < 0 || dir_space.total < 0 ? tr_strerror(errno) : nullptr;
    errno = old_errno;

    /* response */
    if (path != nullptr)
    {
        tr_variantDictAddStr(args_out, TR_KEY_path, path);
    }

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

static struct method
{
    char const* name;
    bool immediate;
    handler func;
} methods[] = {
    { "port-test", false, portTest },
    { "blocklist-update", false, blocklistUpdate },
    { "free-space", true, freeSpace },
    { "session-close", true, sessionClose },
    { "session-get", true, sessionGet },
    { "session-set", true, sessionSet },
    { "session-stats", true, sessionStats },
    { "torrent-add", false, torrentAdd },
    { "torrent-get", true, torrentGet },
    { "torrent-remove", true, torrentRemove },
    { "torrent-rename-path", false, torrentRenamePath },
    { "torrent-set", true, torrentSet },
    { "torrent-set-location", true, torrentSetLocation },
    { "torrent-start", true, torrentStart },
    { "torrent-start-now", true, torrentStartNow },
    { "torrent-stop", true, torrentStop },
    { "torrent-verify", true, torrentVerify },
    { "torrent-reannounce", true, torrentReannounce },
    { "queue-move-top", true, queueMoveTop },
    { "queue-move-up", true, queueMoveUp },
    { "queue-move-down", true, queueMoveDown },
    { "queue-move-bottom", true, queueMoveBottom },
};

static void noop_response_callback(tr_session* /*session*/, tr_variant* /*response*/, void* /*user_data*/)
{
}

void tr_rpc_request_exec_json(
    tr_session* session,
    tr_variant const* request,
    tr_rpc_response_func callback,
    void* callback_user_data)
{
    tr_variant* const mutable_request = (tr_variant*)request;
    tr_variant* args_in = tr_variantDictFind(mutable_request, TR_KEY_arguments);
    char const* result = nullptr;
    struct method const* method = nullptr;

    if (callback == nullptr)
    {
        callback = noop_response_callback;
    }

    /* parse the request */
    char const* str = nullptr;
    if (!tr_variantDictFindStr(mutable_request, TR_KEY_method, &str, nullptr))
    {
        result = "no method name";
    }
    else
    {
        for (size_t i = 0; method == nullptr && i < TR_N_ELEMENTS(methods); ++i)
        {
            if (strcmp(str, methods[i].name) == 0)
            {
                method = &methods[i];
            }
        }

        if (method == nullptr)
        {
            result = "method name not recognized";
        }
    }

    /* if we couldn't figure out which method to use, return an error */
    if (result != nullptr)
    {
        auto response = tr_variant{};
        tr_variantInitDict(&response, 3);
        tr_variantDictAddDict(&response, TR_KEY_arguments, 0);
        tr_variantDictAddStr(&response, TR_KEY_result, result);

        auto tag = int64_t{};
        if (tr_variantDictFindInt(mutable_request, TR_KEY_tag, &tag))
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

        auto tag = int64_t{};
        if (tr_variantDictFindInt(mutable_request, TR_KEY_tag, &tag))
        {
            tr_variantDictAddInt(&response, TR_KEY_tag, tag);
        }

        (*callback)(session, &response, callback_user_data);

        tr_variantFree(&response);
    }
    else
    {
        struct tr_rpc_idle_data* data = tr_new0(struct tr_rpc_idle_data, 1);
        data->session = session;
        data->response = tr_new0(tr_variant, 1);
        tr_variantInitDict(data->response, 3);

        auto tag = int64_t{};
        if (tr_variantDictFindInt(mutable_request, TR_KEY_tag, &tag))
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
    void const* request_uri,
    size_t request_uri_len,
    tr_rpc_response_func callback,
    void* callback_user_data)
{
    char* const request = tr_strndup(request_uri, request_uri_len);

    auto top = tr_variant{};
    tr_variantInitDict(&top, 3);
    tr_variant* const args = tr_variantDictAddDict(&top, TR_KEY_arguments, 0);

    char const* pch = strchr(request, '?');
    if (pch == nullptr)
    {
        pch = request;
    }

    while (pch != nullptr)
    {
        char const* delim = strchr(pch, '=');
        char const* next = strchr(pch, '&');

        if (delim != nullptr)
        {
            auto const key = std::string_view{ pch, size_t(delim - pch) };
            bool isArg = key != "method" && key != "tag";
            tr_variant* parent = isArg ? args : &top;

            auto const val = std::string_view{ delim + 1, next != nullptr ? (size_t)(next - (delim + 1)) : strlen(delim + 1) };
            tr_rpc_parse_list_str(tr_variantDictAdd(parent, tr_quark_new(key)), val);
        }

        pch = next != nullptr ? next + 1 : nullptr;
    }

    tr_rpc_request_exec_json(session, &top, callback, callback_user_data);

    /* cleanup */
    tr_variantFree(&top);
    tr_free(request);
}
