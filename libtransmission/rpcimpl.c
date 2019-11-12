/*
 * This file Copyright (C) 2008-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <ctype.h> /* isdigit */
#include <errno.h>
#include <stdlib.h> /* strtol */
#include <string.h> /* strcmp */

#include <zlib.h>

#include <event2/buffer.h>

#include "transmission.h"
#include "completion.h"
#include "crypto-utils.h"
#include "error.h"
#include "fdlimit.h"
#include "file.h"
#include "log.h"
#include "platform-quota.h" /* tr_device_info_get_free_space() */
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

#define RPC_VERSION 16
#define RPC_VERSION_MIN 1

#define RECENTLY_ACTIVE_SECONDS 60

#if 0
#define dbgmsg(fmt, ...) fprintf(stderr, "%s:%d " fmt "\n", __FILE__, __LINE__, __VA_ARGS__)
#else
#define dbgmsg(...) tr_logAddDeepNamed("RPC", __VA_ARGS__)
#endif

typedef enum
{
    TR_FORMAT_OBJECT = 0,
    TR_FORMAT_TABLE
}
tr_format;

/***
****
***/

static tr_rpc_callback_status notify(tr_session* session, int type, tr_torrent* tor)
{
    tr_rpc_callback_status status = 0;

    if (session->rpc_func != NULL)
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
    if (result == NULL)
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

static tr_torrent** getTorrents(tr_session* session, tr_variant* args, int* setmeCount)
{
    int torrentCount = 0;
    int64_t id;
    tr_torrent** torrents = NULL;
    tr_variant* ids;
    char const* str;

    if (tr_variantDictFindList(args, TR_KEY_ids, &ids))
    {
        int const n = tr_variantListSize(ids);

        torrents = tr_new0(tr_torrent*, n);

        for (int i = 0; i < n; ++i)
        {
            char const* str;
            tr_torrent* tor;
            tr_variant* node = tr_variantListChild(ids, i);

            if (tr_variantGetInt(node, &id))
            {
                tor = tr_torrentFindFromId(session, id);
            }
            else if (tr_variantGetStr(node, &str, NULL))
            {
                tor = tr_torrentFindFromHashString(session, str);
            }
            else
            {
                tor = NULL;
            }

            if (tor != NULL)
            {
                torrents[torrentCount++] = tor;
            }
        }
    }
    else if (tr_variantDictFindInt(args, TR_KEY_ids, &id) || tr_variantDictFindInt(args, TR_KEY_id, &id))
    {
        tr_torrent* tor;
        torrents = tr_new0(tr_torrent*, 1);

        if ((tor = tr_torrentFindFromId(session, id)) != NULL)
        {
            torrents[torrentCount++] = tor;
        }
    }
    else if (tr_variantDictFindStr(args, TR_KEY_ids, &str, NULL))
    {
        if (strcmp(str, "recently-active") == 0)
        {
            tr_torrent* tor = NULL;
            time_t const now = tr_time();
            time_t const window = RECENTLY_ACTIVE_SECONDS;
            int const n = tr_sessionCountTorrents(session);
            torrents = tr_new0(tr_torrent*, n);

            while ((tor = tr_torrentNext(session, tor)) != NULL)
            {
                if (tor->anyDate >= now - window)
                {
                    torrents[torrentCount++] = tor;
                }
            }
        }
        else
        {
            tr_torrent* tor;
            torrents = tr_new0(tr_torrent*, 1);

            if ((tor = tr_torrentFindFromHashString(session, str)) != NULL)
            {
                torrents[torrentCount++] = tor;
            }
        }
    }
    else /* all of them */
    {
        torrents = tr_sessionGetTorrents(session, &torrentCount);
    }

    *setmeCount = torrentCount;
    return torrents;
}

static void notifyBatchQueueChange(tr_session* session, tr_torrent** torrents, int n)
{
    for (int i = 0; i < n; ++i)
    {
        notify(session, TR_RPC_TORRENT_CHANGED, torrents[i]);
    }

    notify(session, TR_RPC_SESSION_QUEUE_POSITIONS_CHANGED, NULL);
}

static char const* queueMoveTop(tr_session* session, tr_variant* args_in, tr_variant* args_out UNUSED,
    struct tr_rpc_idle_data* idle_data UNUSED)
{
    int n;
    tr_torrent** torrents = getTorrents(session, args_in, &n);
    tr_torrentsQueueMoveTop(torrents, n);
    notifyBatchQueueChange(session, torrents, n);
    tr_free(torrents);
    return NULL;
}

static char const* queueMoveUp(tr_session* session, tr_variant* args_in, tr_variant* args_out UNUSED,
    struct tr_rpc_idle_data* idle_data UNUSED)
{
    int n;
    tr_torrent** torrents = getTorrents(session, args_in, &n);
    tr_torrentsQueueMoveUp(torrents, n);
    notifyBatchQueueChange(session, torrents, n);
    tr_free(torrents);
    return NULL;
}

static char const* queueMoveDown(tr_session* session, tr_variant* args_in, tr_variant* args_out UNUSED,
    struct tr_rpc_idle_data* idle_data UNUSED)
{
    int n;
    tr_torrent** torrents = getTorrents(session, args_in, &n);
    tr_torrentsQueueMoveDown(torrents, n);
    notifyBatchQueueChange(session, torrents, n);
    tr_free(torrents);
    return NULL;
}

static char const* queueMoveBottom(tr_session* session, tr_variant* args_in, tr_variant* args_out UNUSED,
    struct tr_rpc_idle_data* idle_data UNUSED)
{
    int n;
    tr_torrent** torrents = getTorrents(session, args_in, &n);
    tr_torrentsQueueMoveBottom(torrents, n);
    notifyBatchQueueChange(session, torrents, n);
    tr_free(torrents);
    return NULL;
}

static int compareTorrentByQueuePosition(void const* va, void const* vb)
{
    tr_torrent const* a = *(tr_torrent const**)va;
    tr_torrent const* b = *(tr_torrent const**)vb;

    return a->queuePosition - b->queuePosition;
}

static char const* torrentStart(tr_session* session, tr_variant* args_in, tr_variant* args_out UNUSED,
    struct tr_rpc_idle_data* idle_data UNUSED)
{
    TR_ASSERT(idle_data == NULL);

    int torrentCount;
    tr_torrent** torrents = getTorrents(session, args_in, &torrentCount);

    qsort(torrents, torrentCount, sizeof(tr_torrent*), compareTorrentByQueuePosition);

    for (int i = 0; i < torrentCount; ++i)
    {
        tr_torrent* tor = torrents[i];

        if (!tor->isRunning)
        {
            tr_torrentStart(tor);
            notify(session, TR_RPC_TORRENT_STARTED, tor);
        }
    }

    tr_free(torrents);
    return NULL;
}

static char const* torrentStartNow(tr_session* session, tr_variant* args_in, tr_variant* args_out UNUSED,
    struct tr_rpc_idle_data* idle_data UNUSED)
{
    TR_ASSERT(idle_data == NULL);

    int torrentCount;
    tr_torrent** torrents = getTorrents(session, args_in, &torrentCount);

    qsort(torrents, torrentCount, sizeof(tr_torrent*), compareTorrentByQueuePosition);

    for (int i = 0; i < torrentCount; ++i)
    {
        tr_torrent* tor = torrents[i];

        if (!tor->isRunning)
        {
            tr_torrentStartNow(tor);
            notify(session, TR_RPC_TORRENT_STARTED, tor);
        }
    }

    tr_free(torrents);
    return NULL;
}

static char const* torrentStop(tr_session* session, tr_variant* args_in, tr_variant* args_out UNUSED,
    struct tr_rpc_idle_data* idle_data UNUSED)
{
    TR_ASSERT(idle_data == NULL);

    int torrentCount;
    tr_torrent** torrents = getTorrents(session, args_in, &torrentCount);

    for (int i = 0; i < torrentCount; ++i)
    {
        tr_torrent* tor = torrents[i];

        if (tor->isRunning || tr_torrentIsQueued(tor))
        {
            tor->isStopping = true;
            notify(session, TR_RPC_TORRENT_STOPPED, tor);
        }
    }

    tr_free(torrents);
    return NULL;
}

static char const* torrentRemove(tr_session* session, tr_variant* args_in, tr_variant* args_out UNUSED,
    struct tr_rpc_idle_data* idle_data UNUSED)
{
    TR_ASSERT(idle_data == NULL);

    bool deleteFlag;

    if (!tr_variantDictFindBool(args_in, TR_KEY_delete_local_data, &deleteFlag))
    {
        deleteFlag = false;
    }

    tr_rpc_callback_type type = deleteFlag ? TR_RPC_TORRENT_TRASHING : TR_RPC_TORRENT_REMOVING;

    int torrentCount;
    tr_torrent** torrents = getTorrents(session, args_in, &torrentCount);

    for (int i = 0; i < torrentCount; ++i)
    {
        tr_torrent* tor = torrents[i];
        tr_rpc_callback_status const status = notify(session, type, tor);

        if ((status & TR_RPC_NOREMOVE) == 0)
        {
            tr_torrentRemove(tor, deleteFlag, NULL);
        }
    }

    tr_free(torrents);
    return NULL;
}

static char const* torrentReannounce(tr_session* session, tr_variant* args_in, tr_variant* args_out UNUSED,
    struct tr_rpc_idle_data* idle_data UNUSED)
{
    TR_ASSERT(idle_data == NULL);

    int torrentCount;
    tr_torrent** torrents = getTorrents(session, args_in, &torrentCount);

    for (int i = 0; i < torrentCount; ++i)
    {
        tr_torrent* tor = torrents[i];

        if (tr_torrentCanManualUpdate(tor))
        {
            tr_torrentManualUpdate(tor);
            notify(session, TR_RPC_TORRENT_CHANGED, tor);
        }
    }

    tr_free(torrents);
    return NULL;
}

static char const* torrentVerify(tr_session* session, tr_variant* args_in, tr_variant* args_out UNUSED,
    struct tr_rpc_idle_data* idle_data UNUSED)
{
    TR_ASSERT(idle_data == NULL);

    int torrentCount;
    tr_torrent** torrents = getTorrents(session, args_in, &torrentCount);

    for (int i = 0; i < torrentCount; ++i)
    {
        tr_torrent* tor = torrents[i];
        tr_torrentVerify(tor, NULL, NULL);
        notify(session, TR_RPC_TORRENT_CHANGED, tor);
    }

    tr_free(torrents);
    return NULL;
}

/***
****
***/

static void addLabels(tr_torrent const* tor, tr_variant* list)
{
    int const labelsCount = tr_ptrArraySize(&tor->labels);
    tr_variantInitList(list, labelsCount);
    char const* const* labels = (char const* const*)tr_ptrArrayBase(&tor->labels);
    for (int i = 0; i < labelsCount; ++i)
    {
        tr_variantListAddStr(list, labels[i]);
    }
}

static void addFileStats(tr_torrent const* tor, tr_variant* list)
{
    tr_file_index_t n;
    tr_info const* info = tr_torrentInfo(tor);
    tr_file_stat* files = tr_torrentFiles(tor, &n);

    for (tr_file_index_t i = 0; i < info->fileCount; ++i)
    {
        tr_file const* file = &info->files[i];
        tr_variant* d = tr_variantListAddDict(list, 3);
        tr_variantDictAddInt(d, TR_KEY_bytesCompleted, files[i].bytesCompleted);
        tr_variantDictAddInt(d, TR_KEY_priority, file->priority);
        tr_variantDictAddBool(d, TR_KEY_wanted, !file->dnd);
    }

    tr_torrentFilesFree(files, n);
}

static void addFiles(tr_torrent const* tor, tr_variant* list)
{
    tr_file_index_t n;
    tr_info const* info = tr_torrentInfo(tor);
    tr_file_stat* files = tr_torrentFiles(tor, &n);

    for (tr_file_index_t i = 0; i < info->fileCount; ++i)
    {
        tr_file const* file = &info->files[i];
        tr_variant* d = tr_variantListAddDict(list, 3);
        tr_variantDictAddInt(d, TR_KEY_bytesCompleted, files[i].bytesCompleted);
        tr_variantDictAddInt(d, TR_KEY_length, file->length);
        tr_variantDictAddStr(d, TR_KEY_name, file->name);
    }

    tr_torrentFilesFree(files, n);
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
    int peerCount;
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

static void initField(tr_torrent* const tor, tr_info const* const inf, tr_stat const* const st, tr_variant* const initme,
    tr_quark key)
{
    char* str;

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
        tr_variantInitStr(initme, inf->comment != NULL ? inf->comment : "", TR_BAD_SIZE);
        break;

    case TR_KEY_corruptEver:
        tr_variantInitInt(initme, st->corruptEver);
        break;

    case TR_KEY_creator:
        tr_variantInitStr(initme, inf->creator != NULL ? inf->creator : "", TR_BAD_SIZE);
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
        tr_variantInitStr(initme, tr_torrentGetDownloadDir(tor), TR_BAD_SIZE);
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
        tr_variantInitStr(initme, st->errorString, TR_BAD_SIZE);
        break;

    case TR_KEY_eta:
        tr_variantInitInt(initme, st->eta);
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
        tr_variantInitStr(initme, tor->info.hashString, TR_BAD_SIZE);
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
        tr_variantInitStr(initme, str, TR_BAD_SIZE);
        tr_free(str);
        break;

    case TR_KEY_metadataPercentComplete:
        tr_variantInitReal(initme, st->metadataPercentComplete);
        break;

    case TR_KEY_name:
        tr_variantInitStr(initme, tr_torrentName(tor), TR_BAD_SIZE);
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
            size_t byte_count = 0;
            void* bytes = tr_torrentCreatePieceBitfield(tor, &byte_count);
            char* str = tr_base64_encode(bytes, byte_count, NULL);
            tr_variantInitStr(initme, str != NULL ? str : "", TR_BAD_SIZE);
            tr_free(str);
            tr_free(bytes);
        }
        else
        {
            tr_variantInitStr(initme, "", 0);
        }

        break;

    case TR_KEY_pieceCount:
        tr_variantInitInt(initme, inf->pieceCount);
        break;

    case TR_KEY_pieceSize:
        tr_variantInitInt(initme, inf->pieceSize);
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
            int n;
            tr_tracker_stat* s = tr_torrentTrackers(tor, &n);
            tr_variantInitList(initme, n);
            addTrackerStats(s, n, initme);
            tr_torrentTrackersFree(s, n);
            break;
        }

    case TR_KEY_torrentFile:
        tr_variantInitStr(initme, inf->torrent, TR_BAD_SIZE);
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
        tr_stat const* const st = tr_torrentStat((tr_torrent*)tor);

        for (size_t i = 0; i < fieldCount; ++i)
        {
            tr_variant* child = format == TR_FORMAT_TABLE ?
                tr_variantListAdd(entry) :
                tr_variantDictAdd(entry, fields[i]);

            initField(tor, inf, st, child, fields[i]);
        }
    }
}

static char const* torrentGet(tr_session* session, tr_variant* args_in, tr_variant* args_out,
    struct tr_rpc_idle_data* idle_data UNUSED)
{
    TR_ASSERT(idle_data == NULL);

    int torrentCount;
    tr_torrent** torrents = getTorrents(session, args_in, &torrentCount);
    tr_variant* list = tr_variantDictAddList(args_out, TR_KEY_torrents, torrentCount + 1);
    tr_variant* fields;
    char const* strVal;
    char const* errmsg = NULL;
    tr_format format;

    if (tr_variantDictFindStr(args_in, TR_KEY_format, &strVal, NULL) && strcmp(strVal, "table") == 0)
    {
        format = TR_FORMAT_TABLE;
    }
    else /* default value */
    {
        format = TR_FORMAT_OBJECT;
    }

    if (tr_variantDictFindStr(args_in, TR_KEY_ids, &strVal, NULL) && strcmp(strVal, "recently-active") == 0)
    {
        int n = 0;
        tr_variant* d;
        time_t const now = tr_time();
        int const interval = RECENTLY_ACTIVE_SECONDS;
        tr_variant* removed_out = tr_variantDictAddList(args_out, TR_KEY_removed, 0);

        while ((d = tr_variantListChild(&session->removedTorrents, n)) != NULL)
        {
            int64_t date;
            int64_t id;

            if (tr_variantDictFindInt(d, TR_KEY_date, &date) &&
                date >= now - interval &&
                tr_variantDictFindInt(d, TR_KEY_id, &id))
            {
                tr_variantListAddInt(removed_out, id);
            }

            ++n;
        }
    }

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
            size_t len;
            if (tr_variantGetStr(tr_variantListChild(fields, i), &strVal, &len))
            {
                keys[keyCount++] = tr_quark_new(strVal, len);
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

        for (int i = 0; i < torrentCount; ++i)
        {
            addTorrentInfo(torrents[i], format, tr_variantListAdd(list), keys, keyCount);
        }

        tr_free(keys);
    }

    tr_free(torrents);
    return errmsg;
}

/***
****
***/

static char const* setLabels(tr_torrent* tor, tr_variant* list)
{
    int const n = tr_variantListSize(list);
    char const* errmsg = NULL;
    tr_ptrArray labels = TR_PTR_ARRAY_INIT;
    int labelcount = 0;
    for (int i = 0; i < n; i++)
    {
        char const* str;
        size_t str_len;
        if (tr_variantGetStr(tr_variantListChild(list, i), &str, &str_len) && str != NULL)
        {
            char* label = tr_strndup(str, str_len);
            tr_strstrip(label);
            if (tr_str_is_empty(label))
            {
                errmsg = "labels cannot be empty";
            }

            if (errmsg == NULL && strchr(str, ',') != NULL)
            {
                errmsg = "labels cannot contain comma (,) character";
            }

            if (errmsg == NULL)
            {
                bool dup = false;
                for (int j = 0; j < labelcount; j++)
                {
                    if (tr_strcmp0(label, (char*)tr_ptrArrayNth(&labels, j)) == 0)
                    {
                        dup = true;
                        break;
                    }
                }

                if (dup)
                {
                    errmsg = "labels cannot contain duplicates";
                }
            }

            tr_ptrArrayAppend(&labels, label);
            labelcount++;

            if (errmsg != NULL)
            {
                break;
            }
        }
    }

    if (errmsg == NULL)
    {
        tr_torrentSetLabels(tor, &labels);
    }

    tr_ptrArrayDestruct(&labels, tr_free);
    return errmsg;
}

static char const* setFilePriorities(tr_torrent* tor, int priority, tr_variant* list)
{
    int64_t tmp;
    int fileCount = 0;
    int const n = tr_variantListSize(list);
    char const* errmsg = NULL;
    tr_file_index_t* files = tr_new0(tr_file_index_t, tor->info.fileCount);

    if (n != 0)
    {
        for (int i = 0; i < n; ++i)
        {
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
    int64_t tmp;
    int fileCount = 0;
    int const n = tr_variantListSize(list);
    char const* errmsg = NULL;
    tr_file_index_t* files = tr_new0(tr_file_index_t, tor->info.fileCount);

    if (n != 0) /* if argument list, process them */
    {
        for (int i = 0; i < n; ++i)
        {
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

            if (pos != NULL)
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
        maxTier = MAX(maxTier, src[i].tier);
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
    int i;
    int n;
    int tier;
    tr_variant* val;
    tr_tracker_info* trackers;
    bool changed = false;
    tr_info const* inf = tr_torrentInfo(tor);
    char const* errmsg = NULL;

    /* make a working copy of the existing announce list */
    n = inf->trackerCount;
    trackers = tr_new0(tr_tracker_info, n + tr_variantListSize(urls));
    tier = copyTrackers(trackers, inf->trackers, n);

    /* and add the new ones */
    i = 0;

    while ((val = tr_variantListChild(urls, i)) != NULL)
    {
        char const* announce = NULL;

        if (tr_variantGetStr(val, &announce, NULL) && tr_urlIsValidTracker(announce) &&
            !findAnnounceUrl(trackers, n, announce, NULL))
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
    tr_tracker_info* trackers;
    bool changed = false;
    tr_info const* inf = tr_torrentInfo(tor);
    int const n = inf->trackerCount;
    char const* errmsg = NULL;

    /* make a working copy of the existing announce list */
    trackers = tr_new0(tr_tracker_info, n);
    copyTrackers(trackers, inf->trackers, n);

    /* make the substitutions... */
    for (size_t i = 0, url_count = tr_variantListSize(urls); i + 1 < url_count; i += 2)
    {
        size_t len;
        int64_t pos;
        char const* newval;

        if (tr_variantGetInt(tr_variantListChild(urls, i), &pos) &&
            tr_variantGetStr(tr_variantListChild(urls, i + 1), &newval, &len) &&
            tr_urlIsValidTracker(newval) && pos < n && pos >= 0)
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
    int i;
    int n;
    int t = 0;
    int dup = -1;
    int* tids;
    tr_variant* val;
    tr_tracker_info* trackers;
    bool changed = false;
    tr_info const* inf = tr_torrentInfo(tor);
    char const* errmsg = NULL;

    /* make a working copy of the existing announce list */
    n = inf->trackerCount;
    tids = tr_new0(int, n);
    trackers = tr_new0(tr_tracker_info, n);
    copyTrackers(trackers, inf->trackers, n);

    /* remove the ones specified in the urls list */
    i = 0;

    while ((val = tr_variantListChild(ids, i)) != NULL)
    {
        int64_t pos;

        if (tr_variantGetInt(val, &pos) && 0 <= pos && pos < n)
        {
            tids[t++] = pos;
        }

        ++i;
    }

    /* sort trackerIds and remove from largest to smallest so there is no need to recalculate array indicies */
    qsort(tids, t, sizeof(int), compareInt);

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

static char const* torrentSet(tr_session* session, tr_variant* args_in, tr_variant* args_out UNUSED,
    struct tr_rpc_idle_data* idle_data UNUSED)
{
    TR_ASSERT(idle_data == NULL);

    int torrentCount;
    tr_torrent** torrents = getTorrents(session, args_in, &torrentCount);

    char const* errmsg = NULL;

    for (int i = 0; i < torrentCount; ++i)
    {
        int64_t tmp;
        double d;
        tr_variant* tmp_variant;
        bool boolVal;
        tr_torrent* tor;

        tor = torrents[i];

        if (tr_variantDictFindInt(args_in, TR_KEY_bandwidthPriority, &tmp))
        {
            if (tr_isPriority(tmp))
            {
                tr_torrentSetPriority(tor, tmp);
            }
        }

        if (errmsg == NULL && tr_variantDictFindList(args_in, TR_KEY_labels, &tmp_variant))
        {
            errmsg = setLabels(tor, tmp_variant);
        }

        if (errmsg == NULL && tr_variantDictFindList(args_in, TR_KEY_files_unwanted, &tmp_variant))
        {
            errmsg = setFileDLs(tor, false, tmp_variant);
        }

        if (errmsg == NULL && tr_variantDictFindList(args_in, TR_KEY_files_wanted, &tmp_variant))
        {
            errmsg = setFileDLs(tor, true, tmp_variant);
        }

        if (tr_variantDictFindInt(args_in, TR_KEY_peer_limit, &tmp))
        {
            tr_torrentSetPeerLimit(tor, tmp);
        }

        if (errmsg == NULL && tr_variantDictFindList(args_in, TR_KEY_priority_high, &tmp_variant))
        {
            errmsg = setFilePriorities(tor, TR_PRI_HIGH, tmp_variant);
        }

        if (errmsg == NULL && tr_variantDictFindList(args_in, TR_KEY_priority_low, &tmp_variant))
        {
            errmsg = setFilePriorities(tor, TR_PRI_LOW, tmp_variant);
        }

        if (errmsg == NULL && tr_variantDictFindList(args_in, TR_KEY_priority_normal, &tmp_variant))
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
            tr_torrentSetIdleLimit(tor, tmp);
        }

        if (tr_variantDictFindInt(args_in, TR_KEY_seedIdleMode, &tmp))
        {
            tr_torrentSetIdleMode(tor, tmp);
        }

        if (tr_variantDictFindReal(args_in, TR_KEY_seedRatioLimit, &d))
        {
            tr_torrentSetRatioLimit(tor, d);
        }

        if (tr_variantDictFindInt(args_in, TR_KEY_seedRatioMode, &tmp))
        {
            tr_torrentSetRatioMode(tor, tmp);
        }

        if (tr_variantDictFindInt(args_in, TR_KEY_queuePosition, &tmp))
        {
            tr_torrentSetQueuePosition(tor, tmp);
        }

        if (errmsg == NULL && tr_variantDictFindList(args_in, TR_KEY_trackerAdd, &tmp_variant))
        {
            errmsg = addTrackerUrls(tor, tmp_variant);
        }

        if (errmsg == NULL && tr_variantDictFindList(args_in, TR_KEY_trackerRemove, &tmp_variant))
        {
            errmsg = removeTrackers(tor, tmp_variant);
        }

        if (errmsg == NULL && tr_variantDictFindList(args_in, TR_KEY_trackerReplace, &tmp_variant))
        {
            errmsg = replaceTrackers(tor, tmp_variant);
        }

        notify(session, TR_RPC_TORRENT_CHANGED, tor);
    }

    tr_free(torrents);
    return errmsg;
}

static char const* torrentSetLocation(tr_session* session, tr_variant* args_in, tr_variant* args_out UNUSED,
    struct tr_rpc_idle_data* idle_data UNUSED)
{
    TR_ASSERT(idle_data == NULL);

    char const* location = NULL;

    if (!tr_variantDictFindStr(args_in, TR_KEY_location, &location, NULL))
    {
        return "no location";
    }

    if (tr_sys_path_is_relative(location))
    {
        return "new location path is not absolute";
    }

    bool move;
    int torrentCount;
    tr_torrent** torrents = getTorrents(session, args_in, &torrentCount);

    if (!tr_variantDictFindBool(args_in, TR_KEY_move, &move))
    {
        move = false;
    }

    for (int i = 0; i < torrentCount; ++i)
    {
        tr_torrent* tor = torrents[i];
        tr_torrentSetLocation(tor, location, move, NULL, NULL);
        notify(session, TR_RPC_TORRENT_MOVED, tor);
    }

    tr_free(torrents);

    return NULL;
}

/***
****
***/

static void torrentRenamePathDone(tr_torrent* tor, char const* oldpath, char const* newname, int error, void* user_data)
{
    char const* result;
    struct tr_rpc_idle_data* data = user_data;

    tr_variantDictAddInt(data->args_out, TR_KEY_id, tr_torrentId(tor));
    tr_variantDictAddStr(data->args_out, TR_KEY_path, oldpath);
    tr_variantDictAddStr(data->args_out, TR_KEY_name, newname);

    if (error == 0)
    {
        result = NULL;
    }
    else
    {
        result = tr_strerror(error);
    }

    tr_idle_function_done(data, result);
}

static char const* torrentRenamePath(tr_session* session, tr_variant* args_in, tr_variant* args_out UNUSED,
    struct tr_rpc_idle_data* idle_data)
{
    int torrentCount;
    tr_torrent** torrents;
    char const* oldpath = NULL;
    char const* newname = NULL;
    char const* errmsg = NULL;

    tr_variantDictFindStr(args_in, TR_KEY_path, &oldpath, NULL);
    tr_variantDictFindStr(args_in, TR_KEY_name, &newname, NULL);
    torrents = getTorrents(session, args_in, &torrentCount);

    if (torrentCount == 1)
    {
        tr_torrentRenamePath(torrents[0], oldpath, newname, torrentRenamePathDone, idle_data);
    }
    else
    {
        errmsg = "torrent-rename-path requires 1 torrent";
    }

    /* cleanup */
    tr_free(torrents);
    return errmsg;
}

/***
****
***/

static void portTested(tr_session* session UNUSED, bool did_connect UNUSED, bool did_timeout UNUSED, long response_code,
    void const* response, size_t response_byte_count, void* user_data)
{
    char result[1024];
    struct tr_rpc_idle_data* data = user_data;

    if (response_code != 200)
    {
        tr_snprintf(result, sizeof(result), "portTested: http error %ld: %s", response_code,
            tr_webGetResponseStr(response_code));
    }
    else /* success */
    {
        bool const isOpen = response_byte_count != 0 && *(char*)response == '1';
        tr_variantDictAddBool(data->args_out, TR_KEY_port_is_open, isOpen);
        tr_snprintf(result, sizeof(result), "success");
    }

    tr_idle_function_done(data, result);
}

static char const* portTest(tr_session* session, tr_variant* args_in UNUSED, tr_variant* args_out UNUSED,
    struct tr_rpc_idle_data* idle_data)
{
    int const port = tr_sessionGetPeerPort(session);
    char* url = tr_strdup_printf("https://portcheck.transmissionbt.com/%d", port);
    tr_webRun(session, url, portTested, idle_data);
    tr_free(url);
    return NULL;
}

/***
****
***/

static void gotNewBlocklist(tr_session* session, bool did_connect UNUSED, bool did_timeout UNUSED, long response_code,
    void const* response, size_t response_byte_count, void* user_data)
{
    char result[1024];
    struct tr_rpc_idle_data* data = user_data;

    *result = '\0';

    if (response_code != 200)
    {
        tr_snprintf(result, sizeof(result), "gotNewBlocklist: http error %ld: %s", response_code,
            tr_webGetResponseStr(response_code));
    }
    else /* successfully fetched the blocklist... */
    {
        tr_sys_file_t fd;
        int err;
        char* filename;
        z_stream stream;
        char const* configDir = tr_sessionGetConfigDir(session);
        size_t const buflen = 1024 * 128; /* 128 KiB buffer */
        uint8_t* buf = tr_valloc(buflen);
        tr_error* error = NULL;

        /* this is an odd Magic Number required by zlib to enable gz support.
           See zlib's inflateInit2() documentation for a full description */
        int const windowBits = 15 + 32;

        stream.zalloc = (alloc_func)Z_NULL;
        stream.zfree = (free_func)Z_NULL;
        stream.opaque = (voidpf)Z_NULL;
        stream.next_in = (void*)response;
        stream.avail_in = response_byte_count;
        inflateInit2(&stream, windowBits);

        filename = tr_buildPath(configDir, "blocklist.tmp.XXXXXX", NULL);
        fd = tr_sys_file_open_temp(filename, &error);

        if (fd == TR_BAD_SYS_FILE)
        {
            tr_snprintf(result, sizeof(result), _("Couldn't save file \"%1$s\": %2$s"), filename, error->message);
            tr_error_clear(&error);
        }

        for (;;)
        {
            stream.next_out = (void*)buf;
            stream.avail_out = buflen;
            err = inflate(&stream, Z_NO_FLUSH);

            if (stream.avail_out < buflen)
            {
                if (!tr_sys_file_write(fd, buf, buflen - stream.avail_out, NULL, &error))
                {
                    tr_snprintf(result, sizeof(result), _("Couldn't save file \"%1$s\": %2$s"), filename, error->message);
                    tr_error_clear(&error);
                    break;
                }
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

        if (err == Z_DATA_ERROR) /* couldn't inflate it... it's probably already uncompressed */
        {
            if (!tr_sys_file_write(fd, response, response_byte_count, NULL, &error))
            {
                tr_snprintf(result, sizeof(result), _("Couldn't save file \"%1$s\": %2$s"), filename, error->message);
                tr_error_clear(&error);
            }
        }

        tr_sys_file_close(fd, NULL);

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

        tr_sys_path_remove(filename, NULL);
        tr_free(filename);
        tr_free(buf);
    }

    tr_idle_function_done(data, result);
}

static char const* blocklistUpdate(tr_session* session, tr_variant* args_in UNUSED, tr_variant* args_out UNUSED,
    struct tr_rpc_idle_data* idle_data)
{
    tr_webRun(session, session->blocklist_url, gotNewBlocklist, idle_data);
    return NULL;
}

/***
****
***/

static void addTorrentImpl(struct tr_rpc_idle_data* data, tr_ctor* ctor)
{
    int err;
    int duplicate_id;
    char const* result;
    tr_torrent* tor;
    tr_quark key;

    err = 0;
    duplicate_id = 0;
    tor = tr_torrentNew(ctor, &err, &duplicate_id);
    tr_ctorFree(ctor);

    if (err == 0)
    {
        key = TR_KEY_torrent_added;
        result = NULL;
    }
    else if (err == TR_PARSE_DUPLICATE)
    {
        tor = tr_torrentFindFromId(data->session, duplicate_id);
        key = TR_KEY_torrent_duplicate;
        result = "duplicate torrent";
    }
    else /* err == TR_PARSE_ERR */
    {
        key = 0;
        result = "invalid or corrupt torrent file";
    }

    if (tor != NULL && key != 0)
    {
        tr_quark const fields[] =
        {
            TR_KEY_id,
            TR_KEY_name,
            TR_KEY_hashString
        };

        addTorrentInfo(tor, TR_FORMAT_OBJECT, tr_variantDictAdd(data->args_out, key), fields, TR_N_ELEMENTS(fields));

        if (result == NULL)
        {
            notify(data->session, TR_RPC_TORRENT_ADDED, tor);
        }

        result = NULL;
    }

    tr_idle_function_done(data, result);
}

struct add_torrent_idle_data
{
    struct tr_rpc_idle_data* data;
    tr_ctor* ctor;
};

static void gotMetadataFromURL(tr_session* session UNUSED, bool did_connect UNUSED, bool did_timeout UNUSED, long response_code,
    void const* response, size_t response_byte_count, void* user_data)
{
    struct add_torrent_idle_data* data = user_data;

    dbgmsg("torrentAdd: HTTP response code was %ld (%s); response length was %zu bytes", response_code,
        tr_webGetResponseStr(response_code), response_byte_count);

    if (response_code == 200 || response_code == 221) /* http or ftp success.. */
    {
        tr_ctorSetMetainfo(data->ctor, response, response_byte_count);
        addTorrentImpl(data->data, data->ctor);
    }
    else
    {
        char result[1024];
        tr_snprintf(result, sizeof(result), "gotMetadataFromURL: http error %ld: %s", response_code,
            tr_webGetResponseStr(response_code));
        tr_idle_function_done(data->data, result);
    }

    tr_free(data);
}

static bool isCurlURL(char const* filename)
{
    if (filename == NULL)
    {
        return false;
    }

    return strncmp(filename, "ftp://", 6) == 0 || strncmp(filename, "http://", 7) == 0 || strncmp(filename, "https://", 8) == 0;
}

static tr_file_index_t* fileListFromList(tr_variant* list, tr_file_index_t* setmeCount)
{
    size_t const childCount = tr_variantListSize(list);
    tr_file_index_t n = 0;
    tr_file_index_t* files = tr_new0(tr_file_index_t, childCount);

    for (size_t i = 0; i < childCount; ++i)
    {
        int64_t intVal;

        if (tr_variantGetInt(tr_variantListChild(list, i), &intVal))
        {
            files[n++] = (tr_file_index_t)intVal;
        }
    }

    *setmeCount = n;
    return files;
}

static char const* torrentAdd(tr_session* session, tr_variant* args_in, tr_variant* args_out UNUSED,
    struct tr_rpc_idle_data* idle_data)
{
    TR_ASSERT(idle_data != NULL);

    char const* filename = NULL;
    char const* metainfo_base64 = NULL;

    tr_variantDictFindStr(args_in, TR_KEY_filename, &filename, NULL);
    tr_variantDictFindStr(args_in, TR_KEY_metainfo, &metainfo_base64, NULL);

    if (filename == NULL && metainfo_base64 == NULL)
    {
        return "no filename or metainfo specified";
    }

    char const* download_dir = NULL;

    if (tr_variantDictFindStr(args_in, TR_KEY_download_dir, &download_dir, NULL))
    {
        if (tr_sys_path_is_relative(download_dir))
        {
            return "download directory path is not absolute";
        }
    }

    int64_t i;
    bool boolVal;
    tr_variant* l;
    char const* cookies = NULL;
    tr_ctor* ctor = tr_ctorNew(session);

    /* set the optional arguments */

    tr_variantDictFindStr(args_in, TR_KEY_cookies, &cookies, NULL);

    if (download_dir != NULL)
    {
        tr_ctorSetDownloadDir(ctor, TR_FORCE, download_dir);
    }

    if (tr_variantDictFindBool(args_in, TR_KEY_paused, &boolVal))
    {
        tr_ctorSetPaused(ctor, TR_FORCE, boolVal);
    }

    if (tr_variantDictFindInt(args_in, TR_KEY_peer_limit, &i))
    {
        tr_ctorSetPeerLimit(ctor, TR_FORCE, i);
    }

    if (tr_variantDictFindInt(args_in, TR_KEY_bandwidthPriority, &i))
    {
        tr_ctorSetBandwidthPriority(ctor, i);
    }

    if (tr_variantDictFindList(args_in, TR_KEY_files_unwanted, &l))
    {
        tr_file_index_t fileCount;
        tr_file_index_t* files = fileListFromList(l, &fileCount);
        tr_ctorSetFilesWanted(ctor, files, fileCount, false);
        tr_free(files);
    }

    if (tr_variantDictFindList(args_in, TR_KEY_files_wanted, &l))
    {
        tr_file_index_t fileCount;
        tr_file_index_t* files = fileListFromList(l, &fileCount);
        tr_ctorSetFilesWanted(ctor, files, fileCount, true);
        tr_free(files);
    }

    if (tr_variantDictFindList(args_in, TR_KEY_priority_low, &l))
    {
        tr_file_index_t fileCount;
        tr_file_index_t* files = fileListFromList(l, &fileCount);
        tr_ctorSetFilePriorities(ctor, files, fileCount, TR_PRI_LOW);
        tr_free(files);
    }

    if (tr_variantDictFindList(args_in, TR_KEY_priority_normal, &l))
    {
        tr_file_index_t fileCount;
        tr_file_index_t* files = fileListFromList(l, &fileCount);
        tr_ctorSetFilePriorities(ctor, files, fileCount, TR_PRI_NORMAL);
        tr_free(files);
    }

    if (tr_variantDictFindList(args_in, TR_KEY_priority_high, &l))
    {
        tr_file_index_t fileCount;
        tr_file_index_t* files = fileListFromList(l, &fileCount);
        tr_ctorSetFilePriorities(ctor, files, fileCount, TR_PRI_HIGH);
        tr_free(files);
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

        if (fname == NULL)
        {
            size_t len;
            char* metainfo = tr_base64_decode_str(metainfo_base64, &len);
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

    return NULL;
}

/***
****
***/

static char const* sessionSet(tr_session* session, tr_variant* args_in, tr_variant* args_out UNUSED,
    struct tr_rpc_idle_data* idle_data UNUSED)
{
    TR_ASSERT(idle_data == NULL);

    char const* download_dir = NULL;
    char const* incomplete_dir = NULL;

    if (tr_variantDictFindStr(args_in, TR_KEY_download_dir, &download_dir, NULL))
    {
        if (tr_sys_path_is_relative(download_dir))
        {
            return "download directory path is not absolute";
        }
    }

    if (tr_variantDictFindStr(args_in, TR_KEY_incomplete_dir, &incomplete_dir, NULL))
    {
        if (tr_sys_path_is_relative(incomplete_dir))
        {
            return "incomplete torrents directory path is not absolute";
        }
    }

    int64_t i;
    double d;
    bool boolVal;
    char const* str;

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
        tr_sessionSetAltSpeedDay(session, i);
    }

    if (tr_variantDictFindBool(args_in, TR_KEY_alt_speed_time_enabled, &boolVal))
    {
        tr_sessionUseAltSpeedTime(session, boolVal);
    }

    if (tr_variantDictFindBool(args_in, TR_KEY_blocklist_enabled, &boolVal))
    {
        tr_blocklistSetEnabled(session, boolVal);
    }

    if (tr_variantDictFindStr(args_in, TR_KEY_blocklist_url, &str, NULL))
    {
        tr_blocklistSetURL(session, str);
    }

    if (download_dir != NULL)
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
        tr_sessionSetQueueSize(session, TR_DOWN, i);
    }

    if (tr_variantDictFindBool(args_in, TR_KEY_download_queue_enabled, &boolVal))
    {
        tr_sessionSetQueueEnabled(session, TR_DOWN, boolVal);
    }

    if (incomplete_dir != NULL)
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
        tr_sessionSetQueueSize(session, TR_UP, i);
    }

    if (tr_variantDictFindStr(args_in, TR_KEY_script_torrent_done_filename, &str, NULL))
    {
        tr_sessionSetTorrentDoneScript(session, str);
    }

    if (tr_variantDictFindBool(args_in, TR_KEY_script_torrent_done_enabled, &boolVal))
    {
        tr_sessionSetTorrentDoneScriptEnabled(session, boolVal);
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

    if (tr_variantDictFindStr(args_in, TR_KEY_encryption, &str, NULL))
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

    notify(session, TR_RPC_SESSION_CHANGED, NULL);

    return NULL;
}

static char const* sessionStats(tr_session* session, tr_variant* args_in UNUSED, tr_variant* args_out,
    struct tr_rpc_idle_data* idle_data UNUSED)
{
    TR_ASSERT(idle_data == NULL);

    int running = 0;
    int total = 0;
    tr_variant* d;
    tr_session_stats currentStats = TR_SESSION_STATS_INIT;
    tr_session_stats cumulativeStats = TR_SESSION_STATS_INIT;
    tr_torrent* tor = NULL;

    while ((tor = tr_torrentNext(session, tor)) != NULL)
    {
        ++total;

        if (tor->isRunning)
        {
            ++running;
        }
    }

    tr_sessionGetStats(session, &currentStats);
    tr_sessionGetCumulativeStats(session, &cumulativeStats);

    tr_variantDictAddInt(args_out, TR_KEY_activeTorrentCount, running);
    tr_variantDictAddReal(args_out, TR_KEY_downloadSpeed, tr_sessionGetPieceSpeed_Bps(session, TR_DOWN));
    tr_variantDictAddInt(args_out, TR_KEY_pausedTorrentCount, total - running);
    tr_variantDictAddInt(args_out, TR_KEY_torrentCount, total);
    tr_variantDictAddReal(args_out, TR_KEY_uploadSpeed, tr_sessionGetPieceSpeed_Bps(session, TR_UP));

    d = tr_variantDictAddDict(args_out, TR_KEY_cumulative_stats, 5);
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

    return NULL;
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
        tr_variantDictAddInt(d, key, tr_device_info_get_free_space(s->downloadDir));
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

    case TR_KEY_script_torrent_done_filename:
        tr_variantDictAddStr(d, key, tr_sessionGetTorrentDoneScript(s));
        break;

    case TR_KEY_script_torrent_done_enabled:
        tr_variantDictAddBool(d, key, tr_sessionIsTorrentDoneScriptEnabled(s));
        break;

    case TR_KEY_queue_stalled_enabled:
        tr_variantDictAddBool(d, key, tr_sessionGetQueueStalledEnabled(s));
        break;

    case TR_KEY_queue_stalled_minutes:
        tr_variantDictAddInt(d, key, tr_sessionGetQueueStalledMinutes(s));
        break;

    case TR_KEY_units:
        tr_formatter_get_units(tr_variantDictAddDict(d, key, 0));
        break;

    case TR_KEY_version:
        tr_variantDictAddStr(d, key, LONG_VERSION_STRING);
        break;

    case TR_KEY_encryption:
        {
            char const* str;

            switch (tr_sessionGetEncryption(s))
            {
            case TR_CLEAR_PREFERRED:
                str = "tolerated";
                break;

            case TR_ENCRYPTION_REQUIRED:
                str = "required";
                break;

            default:
                str = "preferred";
                break;
            }

            tr_variantDictAddStr(d, key, str);
            break;
        }

    case TR_KEY_session_id:
        tr_variantDictAddStr(d, key, tr_session_id_get_current(s->session_id));
        break;
    }
}

static char const* sessionGet(tr_session* s, tr_variant* args_in, tr_variant* args_out,
    struct tr_rpc_idle_data* idle_data UNUSED)
{
    TR_ASSERT(idle_data == NULL);

    tr_variant* fields;

    if (tr_variantDictFindList(args_in, TR_KEY_fields, &fields))
    {
        size_t const field_count = tr_variantListSize(fields);

        for (size_t i = 0; i < field_count; ++i)
        {
            char const* field_name;
            size_t field_name_len;
            tr_quark field_id;

            if (!tr_variantGetStr(tr_variantListChild(fields, i), &field_name, &field_name_len))
            {
                continue;
            }

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

    return NULL;
}

static char const* freeSpace(tr_session* session, tr_variant* args_in, tr_variant* args_out,
    struct tr_rpc_idle_data* idle_data UNUSED)
{
    int tmperr;
    char const* path = NULL;
    char const* err = NULL;
    int64_t free_space = -1;

    if (!tr_variantDictFindStr(args_in, TR_KEY_path, &path, NULL))
    {
        return "directory path argument is missing";
    }

    if (tr_sys_path_is_relative(path))
    {
        return "directory path is not absolute";
    }

    /* get the free space */
    tmperr = errno;
    errno = 0;
    free_space = tr_sessionGetDirFreeSpace(session, path);

    if (free_space < 0)
    {
        err = tr_strerror(errno);
    }

    errno = tmperr;

    /* response */
    if (path != NULL)
    {
        tr_variantDictAddStr(args_out, TR_KEY_path, path);
    }

    tr_variantDictAddInt(args_out, TR_KEY_size_bytes, free_space);
    return err;
}

/***
****
***/

static char const* sessionClose(tr_session* session, tr_variant* args_in UNUSED, tr_variant* args_out UNUSED,
    struct tr_rpc_idle_data* idle_data UNUSED)
{
    notify(session, TR_RPC_SESSION_CLOSE, NULL);
    return NULL;
}

/***
****
***/

typedef char const* (* handler)(tr_session*, tr_variant*, tr_variant*, struct tr_rpc_idle_data*);

static struct method
{
    char const* name;
    bool immediate;
    handler func;
}
methods[] =
{
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
    { "queue-move-bottom", true, queueMoveBottom }
};

static void noop_response_callback(tr_session* session UNUSED, tr_variant* response UNUSED, void* user_data UNUSED)
{
}

void tr_rpc_request_exec_json(tr_session* session, tr_variant const* request, tr_rpc_response_func callback,
    void* callback_user_data)
{
    char const* str;
    tr_variant* const mutable_request = (tr_variant*)request;
    tr_variant* args_in = tr_variantDictFind(mutable_request, TR_KEY_arguments);
    char const* result = NULL;
    struct method* method = NULL;

    if (callback == NULL)
    {
        callback = noop_response_callback;
    }

    /* parse the request */
    if (!tr_variantDictFindStr(mutable_request, TR_KEY_method, &str, NULL))
    {
        result = "no method name";
    }
    else
    {
        for (size_t i = 0; method == NULL && i < TR_N_ELEMENTS(methods); ++i)
        {
            if (strcmp(str, methods[i].name) == 0)
            {
                method = &methods[i];
            }
        }

        if (method == NULL)
        {
            result = "method name not recognized";
        }
    }

    /* if we couldn't figure out which method to use, return an error */
    if (result != NULL)
    {
        int64_t tag;
        tr_variant response;

        tr_variantInitDict(&response, 3);
        tr_variantDictAddDict(&response, TR_KEY_arguments, 0);
        tr_variantDictAddStr(&response, TR_KEY_result, result);

        if (tr_variantDictFindInt(mutable_request, TR_KEY_tag, &tag))
        {
            tr_variantDictAddInt(&response, TR_KEY_tag, tag);
        }

        (*callback)(session, &response, callback_user_data);

        tr_variantFree(&response);
    }
    else if (method->immediate)
    {
        int64_t tag;
        tr_variant response;
        tr_variant* args_out;

        tr_variantInitDict(&response, 3);
        args_out = tr_variantDictAddDict(&response, TR_KEY_arguments, 0);
        result = (*method->func)(session, args_in, args_out, NULL);

        if (result == NULL)
        {
            result = "success";
        }

        tr_variantDictAddStr(&response, TR_KEY_result, result);

        if (tr_variantDictFindInt(mutable_request, TR_KEY_tag, &tag))
        {
            tr_variantDictAddInt(&response, TR_KEY_tag, tag);
        }

        (*callback)(session, &response, callback_user_data);

        tr_variantFree(&response);
    }
    else
    {
        int64_t tag;
        struct tr_rpc_idle_data* data = tr_new0(struct tr_rpc_idle_data, 1);
        data->session = session;
        data->response = tr_new0(tr_variant, 1);
        tr_variantInitDict(data->response, 3);

        if (tr_variantDictFindInt(mutable_request, TR_KEY_tag, &tag))
        {
            tr_variantDictAddInt(data->response, TR_KEY_tag, tag);
        }

        data->args_out = tr_variantDictAddDict(data->response, TR_KEY_arguments, 0);
        data->callback = callback;
        data->callback_user_data = callback_user_data;
        result = (*method->func)(session, args_in, data->args_out, data);

        /* Async operation failed prematurely? Invoke callback or else client will not get a reply */
        if (result != NULL)
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
void tr_rpc_parse_list_str(tr_variant* setme, char const* str, size_t len)
{
    int valueCount;
    int* values = tr_parseNumberRange(str, len, &valueCount);

    if (valueCount == 0)
    {
        tr_variantInitStr(setme, str, len);
    }
    else if (valueCount == 1)
    {
        tr_variantInitInt(setme, values[0]);
    }
    else
    {
        tr_variantInitList(setme, valueCount);

        for (int i = 0; i < valueCount; ++i)
        {
            tr_variantListAddInt(setme, values[i]);
        }
    }

    tr_free(values);
}

void tr_rpc_request_exec_uri(tr_session* session, void const* request_uri, size_t request_uri_len,
    tr_rpc_response_func callback, void* callback_user_data)
{
    char const* pch;
    tr_variant top;
    tr_variant* args;
    char* request = tr_strndup(request_uri, request_uri_len);

    tr_variantInitDict(&top, 3);
    args = tr_variantDictAddDict(&top, TR_KEY_arguments, 0);

    pch = strchr(request, '?');

    if (pch == NULL)
    {
        pch = request;
    }

    while (pch != NULL)
    {
        char const* delim = strchr(pch, '=');
        char const* next = strchr(pch, '&');

        if (delim != NULL)
        {
            char* key = tr_strndup(pch, (size_t)(delim - pch));
            bool isArg = strcmp(key, "method") != 0 && strcmp(key, "tag") != 0;
            tr_variant* parent = isArg ? args : &top;

            tr_rpc_parse_list_str(tr_variantDictAdd(parent, tr_quark_new(key, (size_t)(delim - pch))), delim + 1,
                next != NULL ? (size_t)(next - (delim + 1)) : strlen(delim + 1));
            tr_free(key);
        }

        pch = next != NULL ? next + 1 : NULL;
    }

    tr_rpc_request_exec_json(session, &top, callback, callback_user_data);

    /* cleanup */
    tr_variantFree(&top);
    tr_free(request);
}
