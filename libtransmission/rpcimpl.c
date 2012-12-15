/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2. Works owned by the
 * Transmission project are granted a special exemption to clause 2 (b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#include <assert.h>
#include <ctype.h> /* isdigit */
#include <errno.h>
#include <stdlib.h> /* strtol */
#include <string.h> /* strcmp */
#include <unistd.h> /* unlink */

#ifdef HAVE_ZLIB
 #include <zlib.h>
#endif

#include <event2/buffer.h>

#include "transmission.h"
#include "completion.h"
#include "fdlimit.h"
#include "rpcimpl.h"
#include "session.h"
#include "torrent.h"
#include "utils.h"
#include "variant.h"
#include "version.h"
#include "web.h"

#define RPC_VERSION     14
#define RPC_VERSION_MIN 1

#define RECENTLY_ACTIVE_SECONDS 60

#define TR_N_ELEMENTS(ary)(sizeof (ary) / sizeof (*ary))

#if 0
#define dbgmsg(fmt, ...) \
    do { \
        fprintf (stderr, "%s:%d"#fmt, __FILE__, __LINE__, __VA_ARGS__); \
        fprintf (stderr, "\n"); \
    } while (0)
#else
#define dbgmsg(...) \
    do { \
        if (tr_deepLoggingIsActive ()) \
            tr_deepLog (__FILE__, __LINE__, "RPC", __VA_ARGS__); \
    } while (0)
#endif


/***
****
***/

static tr_rpc_callback_status
notify (tr_session * session,
        int          type,
        tr_torrent * tor)
{
    tr_rpc_callback_status status = 0;

    if (session->rpc_func)
        status = session->rpc_func (session, type, tor,
                                    session->rpc_func_user_data);

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
    tr_session            * session;
    tr_variant               * response;
    tr_variant               * args_out;
    tr_rpc_response_func    callback;
    void                  * callback_user_data;
};

static void
tr_idle_function_done (struct tr_rpc_idle_data * data, const char * result)
{
    struct evbuffer * buf;

    if (result == NULL)
        result = "success";
    tr_variantDictAddStr (data->response, "result", result);

    buf = tr_variantToBuf (data->response, TR_VARIANT_FMT_JSON_LEAN);
  (*data->callback)(data->session, buf, data->callback_user_data);
    evbuffer_free (buf);

    tr_variantFree (data->response);
    tr_free (data->response);
    tr_free (data);
}

/***
****
***/

static tr_torrent **
getTorrents (tr_session * session,
             tr_variant    * args,
             int        * setmeCount)
{
    int           torrentCount = 0;
    int64_t       id;
    tr_torrent ** torrents = NULL;
    tr_variant *     ids;
    const char * str;

    if (tr_variantDictFindList (args, "ids", &ids))
    {
        int       i;
        const int n = tr_variantListSize (ids);

        torrents = tr_new0 (tr_torrent *, n);

        for (i = 0; i < n; ++i)
        {
            tr_torrent * tor = NULL;
            tr_variant *    node = tr_variantListChild (ids, i);
            const char * str;
            if (tr_variantGetInt (node, &id))
                tor = tr_torrentFindFromId (session, id);
            else if (tr_variantGetStr (node, &str, NULL))
                tor = tr_torrentFindFromHashString (session, str);
            if (tor)
                torrents[torrentCount++] = tor;
        }
    }
    else if (tr_variantDictFindInt (args, "ids", &id)
           || tr_variantDictFindInt (args, "id", &id))
    {
        tr_torrent * tor;
        torrents = tr_new0 (tr_torrent *, 1);
        if ((tor = tr_torrentFindFromId (session, id)))
            torrents[torrentCount++] = tor;
    }
    else if (tr_variantDictFindStr (args, "ids", &str, NULL))
    {
        if (!strcmp (str, "recently-active"))
        {
            tr_torrent * tor = NULL;
            const time_t now = tr_time ();
            const time_t window = RECENTLY_ACTIVE_SECONDS;
            const int n = tr_sessionCountTorrents (session);
            torrents = tr_new0 (tr_torrent *, n);
            while ((tor = tr_torrentNext (session, tor)))
                if (tor->anyDate >= now - window)
                    torrents[torrentCount++] = tor;
        }
        else
        {
            tr_torrent * tor;
            torrents = tr_new0 (tr_torrent *, 1);
            if ((tor = tr_torrentFindFromHashString (session, str)))
                torrents[torrentCount++] = tor;
        }
    }
    else /* all of them */
    {
        tr_torrent * tor = NULL;
        const int n = tr_sessionCountTorrents (session);
        torrents = tr_new0 (tr_torrent *, n);
        while ((tor = tr_torrentNext (session, tor)))
            torrents[torrentCount++] = tor;
    }

    *setmeCount = torrentCount;
    return torrents;
}

static void
notifyBatchQueueChange (tr_session * session, tr_torrent ** torrents, int n)
{
    int i;
    for (i=0; i<n; ++i)
        notify (session, TR_RPC_TORRENT_CHANGED, torrents[i]);
    notify (session, TR_RPC_SESSION_QUEUE_POSITIONS_CHANGED, NULL);
}

static const char*
queueMoveTop (tr_session               * session,
              tr_variant                  * args_in,
              tr_variant                  * args_out UNUSED,
              struct tr_rpc_idle_data  * idle_data UNUSED)
{
    int n;
    tr_torrent ** torrents = getTorrents (session, args_in, &n);
    tr_torrentsQueueMoveTop (torrents, n);
    notifyBatchQueueChange (session, torrents, n);
    tr_free (torrents);
    return NULL;
}

static const char*
queueMoveUp (tr_session               * session,
             tr_variant                  * args_in,
             tr_variant                  * args_out UNUSED,
             struct tr_rpc_idle_data  * idle_data UNUSED)
{
    int n;
    tr_torrent ** torrents = getTorrents (session, args_in, &n);
    tr_torrentsQueueMoveUp (torrents, n);
    notifyBatchQueueChange (session, torrents, n);
    tr_free (torrents);
    return NULL;
}

static const char*
queueMoveDown (tr_session               * session,
               tr_variant                  * args_in,
               tr_variant                  * args_out UNUSED,
               struct tr_rpc_idle_data  * idle_data UNUSED)
{
    int n;
    tr_torrent ** torrents = getTorrents (session, args_in, &n);
    tr_torrentsQueueMoveDown (torrents, n);
    notifyBatchQueueChange (session, torrents, n);
    tr_free (torrents);
    return NULL;
}

static const char*
queueMoveBottom (tr_session               * session,
                 tr_variant                  * args_in,
                 tr_variant                  * args_out UNUSED,
                 struct tr_rpc_idle_data  * idle_data UNUSED)
{
    int n;
    tr_torrent ** torrents = getTorrents (session, args_in, &n);
    tr_torrentsQueueMoveBottom (torrents, n);
    notifyBatchQueueChange (session, torrents, n);
    tr_free (torrents);
    return NULL;
}

static const char*
torrentStart (tr_session               * session,
              tr_variant                  * args_in,
              tr_variant                  * args_out UNUSED,
              struct tr_rpc_idle_data  * idle_data UNUSED)
{
    int           i, torrentCount;
    tr_torrent ** torrents = getTorrents (session, args_in, &torrentCount);

    assert (idle_data == NULL);

    for (i = 0; i < torrentCount; ++i)
    {
        tr_torrent * tor = torrents[i];
        if (!tor->isRunning)
        {
            tr_torrentStart (tor);
            notify (session, TR_RPC_TORRENT_STARTED, tor);
        }
    }
    tr_free (torrents);
    return NULL;
}

static const char*
torrentStartNow (tr_session               * session,
                 tr_variant                  * args_in,
                 tr_variant                  * args_out UNUSED,
                 struct tr_rpc_idle_data  * idle_data UNUSED)
{
    int           i, torrentCount;
    tr_torrent ** torrents = getTorrents (session, args_in, &torrentCount);

    assert (idle_data == NULL);

    for (i = 0; i < torrentCount; ++i)
    {
        tr_torrent * tor = torrents[i];
        if (!tor->isRunning)
        {
            tr_torrentStartNow (tor);
            notify (session, TR_RPC_TORRENT_STARTED, tor);
        }
    }
    tr_free (torrents);
    return NULL;
}

static const char*
torrentStop (tr_session               * session,
             tr_variant                  * args_in,
             tr_variant                  * args_out UNUSED,
             struct tr_rpc_idle_data  * idle_data UNUSED)
{
    int           i, torrentCount;
    tr_torrent ** torrents = getTorrents (session, args_in, &torrentCount);

    assert (idle_data == NULL);

    for (i = 0; i < torrentCount; ++i)
    {
        tr_torrent * tor = torrents[i];

        if (tor->isRunning || tr_torrentIsQueued (tor))
        {
            tor->isStopping = true;
            notify (session, TR_RPC_TORRENT_STOPPED, tor);
        }
    }
    tr_free (torrents);
    return NULL;
}

static const char*
torrentRemove (tr_session               * session,
               tr_variant                  * args_in,
               tr_variant                  * args_out UNUSED,
               struct tr_rpc_idle_data  * idle_data UNUSED)
{
    int i;
    int torrentCount;
    tr_rpc_callback_type type;
    bool deleteFlag = false;
    tr_torrent ** torrents = getTorrents (session, args_in, &torrentCount);

    assert (idle_data == NULL);

    tr_variantDictFindBool (args_in, "delete-local-data", &deleteFlag);
    type = deleteFlag ? TR_RPC_TORRENT_TRASHING
                      : TR_RPC_TORRENT_REMOVING;

    for (i=0; i<torrentCount; ++i)
    {
        tr_torrent * tor = torrents[i];
        const tr_rpc_callback_status status = notify (session, type, tor);
        if (! (status & TR_RPC_NOREMOVE))
            tr_torrentRemove (tor, deleteFlag, NULL);
    }

    tr_free (torrents);
    return NULL;
}

static const char*
torrentReannounce (tr_session               * session,
                   tr_variant                  * args_in,
                   tr_variant                  * args_out UNUSED,
                   struct tr_rpc_idle_data  * idle_data UNUSED)
{
    int i, torrentCount;
    tr_torrent ** torrents = getTorrents (session, args_in, &torrentCount);

    assert (idle_data == NULL);

    for (i=0; i<torrentCount; ++i)
    {
        tr_torrent * tor = torrents[i];
        if (tr_torrentCanManualUpdate (tor))
        {
            tr_torrentManualUpdate (tor);
            notify (session, TR_RPC_TORRENT_CHANGED, tor);
        }
    }

    tr_free (torrents);
    return NULL;
}

static const char*
torrentVerify (tr_session               * session,
               tr_variant                  * args_in,
               tr_variant                  * args_out UNUSED,
               struct tr_rpc_idle_data  * idle_data UNUSED)
{
    int           i, torrentCount;
    tr_torrent ** torrents = getTorrents (session, args_in, &torrentCount);

    assert (idle_data == NULL);

    for (i = 0; i < torrentCount; ++i)
    {
        tr_torrent * tor = torrents[i];
        tr_torrentVerify (tor);
        notify (session, TR_RPC_TORRENT_CHANGED, tor);
    }

    tr_free (torrents);
    return NULL;
}

/***
****
***/

static void
addFileStats (const tr_torrent * tor, tr_variant * list)
{
    tr_file_index_t i;
    tr_file_index_t n;
    const tr_info * info = tr_torrentInfo (tor);
    tr_file_stat * files = tr_torrentFiles (tor, &n);

    for (i = 0; i < info->fileCount; ++i)
    {
        const tr_file * file = &info->files[i];
        tr_variant * d = tr_variantListAddDict (list, 3);
        tr_variantDictAddInt (d, "bytesCompleted", files[i].bytesCompleted);
        tr_variantDictAddInt (d, "priority", file->priority);
        tr_variantDictAddBool (d, "wanted", !file->dnd);
    }

    tr_torrentFilesFree (files, n);
}

static void
addFiles (const tr_torrent * tor,
          tr_variant *          list)
{
    tr_file_index_t i;
    tr_file_index_t n;
    const tr_info * info = tr_torrentInfo (tor);
    tr_file_stat *  files = tr_torrentFiles (tor, &n);

    for (i = 0; i < info->fileCount; ++i)
    {
        const tr_file * file = &info->files[i];
        tr_variant *       d = tr_variantListAddDict (list, 3);
        tr_variantDictAddInt (d, "bytesCompleted", files[i].bytesCompleted);
        tr_variantDictAddInt (d, "length", file->length);
        tr_variantDictAddStr (d, "name", file->name);
    }

    tr_torrentFilesFree (files, n);
}

static void
addWebseeds (const tr_info * info,
             tr_variant *       webseeds)
{
    int i;

    for (i = 0; i < info->webseedCount; ++i)
        tr_variantListAddStr (webseeds, info->webseeds[i]);
}

static void
addTrackers (const tr_info * info,
             tr_variant *       trackers)
{
    int i;

    for (i = 0; i < info->trackerCount; ++i)
    {
        const tr_tracker_info * t = &info->trackers[i];
        tr_variant *               d = tr_variantListAddDict (trackers, 4);
        tr_variantDictAddStr (d, "announce", t->announce);
        tr_variantDictAddInt (d, "id", t->id);
        tr_variantDictAddStr (d, "scrape", t->scrape);
        tr_variantDictAddInt (d, "tier", t->tier);
    }
}

static void
addTrackerStats (const tr_tracker_stat * st, int n, tr_variant * list)
{
    int i;

    for (i=0; i<n; ++i)
    {
        const tr_tracker_stat * s = &st[i];
        tr_variant * d = tr_variantListAddDict (list, 26);
        tr_variantDictAddStr (d, "announce", s->announce);
        tr_variantDictAddInt (d, "announceState", s->announceState);
        tr_variantDictAddInt (d, "downloadCount", s->downloadCount);
        tr_variantDictAddBool (d, "hasAnnounced", s->hasAnnounced);
        tr_variantDictAddBool (d, "hasScraped", s->hasScraped);
        tr_variantDictAddStr (d, "host", s->host);
        tr_variantDictAddInt (d, "id", s->id);
        tr_variantDictAddBool (d, "isBackup", s->isBackup);
        tr_variantDictAddInt (d, "lastAnnouncePeerCount", s->lastAnnouncePeerCount);
        tr_variantDictAddStr (d, "lastAnnounceResult", s->lastAnnounceResult);
        tr_variantDictAddInt (d, "lastAnnounceStartTime", s->lastAnnounceStartTime);
        tr_variantDictAddBool (d, "lastAnnounceSucceeded", s->lastAnnounceSucceeded);
        tr_variantDictAddInt (d, "lastAnnounceTime", s->lastAnnounceTime);
        tr_variantDictAddBool (d, "lastAnnounceTimedOut", s->lastAnnounceTimedOut);
        tr_variantDictAddStr (d, "lastScrapeResult", s->lastScrapeResult);
        tr_variantDictAddInt (d, "lastScrapeStartTime", s->lastScrapeStartTime);
        tr_variantDictAddBool (d, "lastScrapeSucceeded", s->lastScrapeSucceeded);
        tr_variantDictAddInt (d, "lastScrapeTime", s->lastScrapeTime);
        tr_variantDictAddInt (d, "lastScrapeTimedOut", s->lastScrapeTimedOut);
        tr_variantDictAddInt (d, "leecherCount", s->leecherCount);
        tr_variantDictAddInt (d, "nextAnnounceTime", s->nextAnnounceTime);
        tr_variantDictAddInt (d, "nextScrapeTime", s->nextScrapeTime);
        tr_variantDictAddStr (d, "scrape", s->scrape);
        tr_variantDictAddInt (d, "scrapeState", s->scrapeState);
        tr_variantDictAddInt (d, "seederCount", s->seederCount);
        tr_variantDictAddInt (d, "tier", s->tier);
    }
}

static void
addPeers (tr_torrent * tor, tr_variant * list)
{
  int i;
  int peerCount;
  tr_peer_stat * peers = tr_torrentPeers (tor, &peerCount);

  tr_variantInitList (list, peerCount);

  for (i=0; i<peerCount; ++i)
    {
        tr_variant *            d = tr_variantListAddDict (list, 16);
        const tr_peer_stat * peer = peers + i;
        tr_variantDictAddStr (d, "address", peer->addr);
        tr_variantDictAddStr (d, "clientName", peer->client);
        tr_variantDictAddBool (d, "clientIsChoked", peer->clientIsChoked);
        tr_variantDictAddBool (d, "clientIsInterested", peer->clientIsInterested);
        tr_variantDictAddStr (d, "flagStr", peer->flagStr);
        tr_variantDictAddBool (d, "isDownloadingFrom", peer->isDownloadingFrom);
        tr_variantDictAddBool (d, "isEncrypted", peer->isEncrypted);
        tr_variantDictAddBool (d, "isIncoming", peer->isIncoming);
        tr_variantDictAddBool (d, "isUploadingTo", peer->isUploadingTo);
        tr_variantDictAddBool (d, "isUTP", peer->isUTP);
        tr_variantDictAddBool (d, "peerIsChoked", peer->peerIsChoked);
        tr_variantDictAddBool (d, "peerIsInterested", peer->peerIsInterested);
        tr_variantDictAddInt (d, "port", peer->port);
        tr_variantDictAddReal (d, "progress", peer->progress);
        tr_variantDictAddInt (d, "rateToClient", toSpeedBytes (peer->rateToClient_KBps));
        tr_variantDictAddInt (d, "rateToPeer", toSpeedBytes (peer->rateToPeer_KBps));
    }

    tr_torrentPeersFree (peers, peerCount);
}

/* faster-than-strcmp () optimization. This is kind of clumsy,
   but addField () was in the profiler's top 10 list, and this
   makes it 4x faster... */
#define tr_streq(a,alen,b)((alen+1==sizeof (b)) && !memcmp (a,b,alen))

static void
addField (tr_torrent       * const tor,
          const tr_info    * const inf,
          const tr_stat    * const st,
          tr_variant          * const d,
          const char       * const key)
{
    const size_t keylen = strlen (key);

    if (tr_streq (key, keylen, "activityDate"))
        tr_variantDictAddInt (d, key, st->activityDate);
    else if (tr_streq (key, keylen, "addedDate"))
        tr_variantDictAddInt (d, key, st->addedDate);
    else if (tr_streq (key, keylen, "bandwidthPriority"))
        tr_variantDictAddInt (d, key, tr_torrentGetPriority (tor));
    else if (tr_streq (key, keylen, "comment"))
        tr_variantDictAddStr (d, key, inf->comment ? inf->comment : "");
    else if (tr_streq (key, keylen, "corruptEver"))
        tr_variantDictAddInt (d, key, st->corruptEver);
    else if (tr_streq (key, keylen, "creator"))
        tr_variantDictAddStr (d, key, inf->creator ? inf->creator : "");
    else if (tr_streq (key, keylen, "dateCreated"))
        tr_variantDictAddInt (d, key, inf->dateCreated);
    else if (tr_streq (key, keylen, "desiredAvailable"))
        tr_variantDictAddInt (d, key, st->desiredAvailable);
    else if (tr_streq (key, keylen, "doneDate"))
        tr_variantDictAddInt (d, key, st->doneDate);
    else if (tr_streq (key, keylen, "downloadDir"))
        tr_variantDictAddStr (d, key, tr_torrentGetDownloadDir (tor));
    else if (tr_streq (key, keylen, "downloadedEver"))
        tr_variantDictAddInt (d, key, st->downloadedEver);
    else if (tr_streq (key, keylen, "downloadLimit"))
        tr_variantDictAddInt (d, key, tr_torrentGetSpeedLimit_KBps (tor, TR_DOWN));
    else if (tr_streq (key, keylen, "downloadLimited"))
        tr_variantDictAddBool (d, key, tr_torrentUsesSpeedLimit (tor, TR_DOWN));
    else if (tr_streq (key, keylen, "error"))
        tr_variantDictAddInt (d, key, st->error);
    else if (tr_streq (key, keylen, "errorString"))
        tr_variantDictAddStr (d, key, st->errorString);
    else if (tr_streq (key, keylen, "eta"))
        tr_variantDictAddInt (d, key, st->eta);
    else if (tr_streq (key, keylen, "files"))
        addFiles (tor, tr_variantDictAddList (d, key, inf->fileCount));
    else if (tr_streq (key, keylen, "fileStats"))
        addFileStats (tor, tr_variantDictAddList (d, key, inf->fileCount));
    else if (tr_streq (key, keylen, "hashString"))
        tr_variantDictAddStr (d, key, tor->info.hashString);
    else if (tr_streq (key, keylen, "haveUnchecked"))
        tr_variantDictAddInt (d, key, st->haveUnchecked);
    else if (tr_streq (key, keylen, "haveValid"))
        tr_variantDictAddInt (d, key, st->haveValid);
    else if (tr_streq (key, keylen, "honorsSessionLimits"))
        tr_variantDictAddBool (d, key, tr_torrentUsesSessionLimits (tor));
    else if (tr_streq (key, keylen, "id"))
        tr_variantDictAddInt (d, key, st->id);
    else if (tr_streq (key, keylen, "isFinished"))
        tr_variantDictAddBool (d, key, st->finished);
    else if (tr_streq (key, keylen, "isPrivate"))
        tr_variantDictAddBool (d, key, tr_torrentIsPrivate (tor));
    else if (tr_streq (key, keylen, "isStalled"))
        tr_variantDictAddBool (d, key, st->isStalled);
    else if (tr_streq (key, keylen, "leftUntilDone"))
        tr_variantDictAddInt (d, key, st->leftUntilDone);
    else if (tr_streq (key, keylen, "manualAnnounceTime"))
        tr_variantDictAddInt (d, key, st->manualAnnounceTime);
    else if (tr_streq (key, keylen, "maxConnectedPeers"))
        tr_variantDictAddInt (d, key,  tr_torrentGetPeerLimit (tor));
    else if (tr_streq (key, keylen, "magnetLink")) {
        char * str = tr_torrentGetMagnetLink (tor);
        tr_variantDictAddStr (d, key, str);
        tr_free (str);
    }
    else if (tr_streq (key, keylen, "metadataPercentComplete"))
        tr_variantDictAddReal (d, key, st->metadataPercentComplete);
    else if (tr_streq (key, keylen, "name"))
        tr_variantDictAddStr (d, key, tr_torrentName (tor));
    else if (tr_streq (key, keylen, "percentDone"))
        tr_variantDictAddReal (d, key, st->percentDone);
    else if (tr_streq (key, keylen, "peer-limit"))
        tr_variantDictAddInt (d, key, tr_torrentGetPeerLimit (tor));
    else if (tr_streq (key, keylen, "peers"))
        addPeers (tor, tr_variantDictAdd (d, key, keylen));
    else if (tr_streq (key, keylen, "peersConnected"))
        tr_variantDictAddInt (d, key, st->peersConnected);
    else if (tr_streq (key, keylen, "peersFrom"))
    {
        tr_variant *   tmp = tr_variantDictAddDict (d, key, 7);
        const int * f = st->peersFrom;
        tr_variantDictAddInt (tmp, "fromCache",    f[TR_PEER_FROM_RESUME]);
        tr_variantDictAddInt (tmp, "fromDht",      f[TR_PEER_FROM_DHT]);
        tr_variantDictAddInt (tmp, "fromIncoming", f[TR_PEER_FROM_INCOMING]);
        tr_variantDictAddInt (tmp, "fromLpd",      f[TR_PEER_FROM_LPD]);
        tr_variantDictAddInt (tmp, "fromLtep",     f[TR_PEER_FROM_LTEP]);
        tr_variantDictAddInt (tmp, "fromPex",      f[TR_PEER_FROM_PEX]);
        tr_variantDictAddInt (tmp, "fromTracker",  f[TR_PEER_FROM_TRACKER]);
    }
    else if (tr_streq (key, keylen, "peersGettingFromUs"))
        tr_variantDictAddInt (d, key, st->peersGettingFromUs);
    else if (tr_streq (key, keylen, "peersSendingToUs"))
        tr_variantDictAddInt (d, key, st->peersSendingToUs);
    else if (tr_streq (key, keylen, "pieces")) {
        if (tr_torrentHasMetadata (tor)) {
            size_t byte_count = 0;
            void * bytes = tr_cpCreatePieceBitfield (&tor->completion, &byte_count);
            char * str = tr_base64_encode (bytes, byte_count, NULL);
            tr_variantDictAddStr (d, key, str!=NULL ? str : "");
            tr_free (str);
            tr_free (bytes);
        } else {
            tr_variantDictAddStr (d, key, "");
        }
    }
    else if (tr_streq (key, keylen, "pieceCount"))
        tr_variantDictAddInt (d, key, inf->pieceCount);
    else if (tr_streq (key, keylen, "pieceSize"))
        tr_variantDictAddInt (d, key, inf->pieceSize);
    else if (tr_streq (key, keylen, "priorities"))
    {
        tr_file_index_t i;
        tr_variant *       p = tr_variantDictAddList (d, key, inf->fileCount);
        for (i = 0; i < inf->fileCount; ++i)
            tr_variantListAddInt (p, inf->files[i].priority);
    }
    else if (tr_streq (key, keylen, "queuePosition"))
        tr_variantDictAddInt (d, key, st->queuePosition);
    else if (tr_streq (key, keylen, "rateDownload"))
        tr_variantDictAddInt (d, key, toSpeedBytes (st->pieceDownloadSpeed_KBps));
    else if (tr_streq (key, keylen, "rateUpload"))
        tr_variantDictAddInt (d, key, toSpeedBytes (st->pieceUploadSpeed_KBps));
    else if (tr_streq (key, keylen, "recheckProgress"))
        tr_variantDictAddReal (d, key, st->recheckProgress);
    else if (tr_streq (key, keylen, "seedIdleLimit"))
        tr_variantDictAddInt (d, key, tr_torrentGetIdleLimit (tor));
    else if (tr_streq (key, keylen, "seedIdleMode"))
        tr_variantDictAddInt (d, key, tr_torrentGetIdleMode (tor));
    else if (tr_streq (key, keylen, "seedRatioLimit"))
        tr_variantDictAddReal (d, key, tr_torrentGetRatioLimit (tor));
    else if (tr_streq (key, keylen, "seedRatioMode"))
        tr_variantDictAddInt (d, key, tr_torrentGetRatioMode (tor));
    else if (tr_streq (key, keylen, "sizeWhenDone"))
        tr_variantDictAddInt (d, key, st->sizeWhenDone);
    else if (tr_streq (key, keylen, "startDate"))
        tr_variantDictAddInt (d, key, st->startDate);
    else if (tr_streq (key, keylen, "status"))
        tr_variantDictAddInt (d, key, st->activity);
    else if (tr_streq (key, keylen, "secondsDownloading"))
        tr_variantDictAddInt (d, key, st->secondsDownloading);
    else if (tr_streq (key, keylen, "secondsSeeding"))
        tr_variantDictAddInt (d, key, st->secondsSeeding);
    else if (tr_streq (key, keylen, "trackers"))
        addTrackers (inf, tr_variantDictAddList (d, key, inf->trackerCount));
    else if (tr_streq (key, keylen, "trackerStats")) {
        int n;
        tr_tracker_stat * s = tr_torrentTrackers (tor, &n);
        addTrackerStats (s, n, tr_variantDictAddList (d, key, n));
        tr_torrentTrackersFree (s, n);
    }
    else if (tr_streq (key, keylen, "torrentFile"))
        tr_variantDictAddStr (d, key, inf->torrent);
    else if (tr_streq (key, keylen, "totalSize"))
        tr_variantDictAddInt (d, key, inf->totalSize);
    else if (tr_streq (key, keylen, "uploadedEver"))
        tr_variantDictAddInt (d, key, st->uploadedEver);
    else if (tr_streq (key, keylen, "uploadLimit"))
        tr_variantDictAddInt (d, key, tr_torrentGetSpeedLimit_KBps (tor, TR_UP));
    else if (tr_streq (key, keylen, "uploadLimited"))
        tr_variantDictAddBool (d, key, tr_torrentUsesSpeedLimit (tor, TR_UP));
    else if (tr_streq (key, keylen, "uploadRatio"))
        tr_variantDictAddReal (d, key, st->ratio);
    else if (tr_streq (key, keylen, "wanted"))
    {
        tr_file_index_t i;
        tr_variant *       w = tr_variantDictAddList (d, key, inf->fileCount);
        for (i = 0; i < inf->fileCount; ++i)
            tr_variantListAddInt (w, inf->files[i].dnd ? 0 : 1);
    }
    else if (tr_streq (key, keylen, "webseeds"))
        addWebseeds (inf, tr_variantDictAddList (d, key, inf->webseedCount));
    else if (tr_streq (key, keylen, "webseedsSendingToUs"))
        tr_variantDictAddInt (d, key, st->webseedsSendingToUs);
}

static void
addInfo (tr_torrent * tor, tr_variant * d, tr_variant * fields)
{
    const char * str;
    const int n = tr_variantListSize (fields);

    tr_variantInitDict (d, n);

    if (n > 0)
    {
        int i;
        const tr_info const * inf = tr_torrentInfo (tor);
        const tr_stat const * st = tr_torrentStat ((tr_torrent*)tor);

        for (i=0; i<n; ++i)
            if (tr_variantGetStr (tr_variantListChild (fields, i), &str, NULL))
                addField (tor, inf, st, d, str);
    }
}

static const char*
torrentGet (tr_session               * session,
            tr_variant                  * args_in,
            tr_variant                  * args_out,
            struct tr_rpc_idle_data  * idle_data UNUSED)
{
    int           i, torrentCount;
    tr_torrent ** torrents = getTorrents (session, args_in, &torrentCount);
    tr_variant *     list = tr_variantDictAddList (args_out, "torrents", torrentCount);
    tr_variant *     fields;
    const char *  msg = NULL;
    const char *  strVal;

    assert (idle_data == NULL);

    if (tr_variantDictFindStr (args_in, "ids", &strVal, NULL) && !strcmp (strVal, "recently-active")) {
        int n = 0;
        tr_variant * d;
        const time_t now = tr_time ();
        const int interval = RECENTLY_ACTIVE_SECONDS;
        tr_variant * removed_out = tr_variantDictAddList (args_out, "removed", 0);
        while ((d = tr_variantListChild (&session->removedTorrents, n++))) {
            int64_t intVal;
            if (tr_variantDictFindInt (d, "date", &intVal) && (intVal >= now - interval)) {
                tr_variantDictFindInt (d, "id", &intVal);
                tr_variantListAddInt (removed_out, intVal);
            }
        }
    }

    if (!tr_variantDictFindList (args_in, "fields", &fields))
        msg = "no fields specified";
    else for (i = 0; i < torrentCount; ++i)
        addInfo (torrents[i], tr_variantListAdd (list), fields);

    tr_free (torrents);
    return msg;
}

/***
****
***/

static const char*
setFilePriorities (tr_torrent * tor,
                   int          priority,
                   tr_variant *    list)
{
    int i;
    int64_t tmp;
    int fileCount = 0;
    const int n = tr_variantListSize (list);
    const char * errmsg = NULL;
    tr_file_index_t * files = tr_new0 (tr_file_index_t, tor->info.fileCount);

    if (n)
    {
        for (i = 0; i < n; ++i) {
            if (tr_variantGetInt (tr_variantListChild (list, i), &tmp)) {
                if (0 <= tmp && tmp < tor->info.fileCount) {
                    files[fileCount++] = tmp;
                } else {
                    errmsg = "file index out of range";
                }
            }
        }
    }
    else /* if empty set, apply to all */
    {
        tr_file_index_t t;
        for (t = 0; t < tor->info.fileCount; ++t)
            files[fileCount++] = t;
    }

    if (fileCount)
        tr_torrentSetFilePriorities (tor, files, fileCount, priority);

    tr_free (files);
    return errmsg;
}

static const char*
setFileDLs (tr_torrent * tor,
            int          do_download,
            tr_variant *    list)
{
    int i;
    int64_t tmp;
    int fileCount = 0;
    const int n = tr_variantListSize (list);
    const char * errmsg = NULL;
    tr_file_index_t * files = tr_new0 (tr_file_index_t, tor->info.fileCount);

    if (n) /* if argument list, process them */
    {
        for (i = 0; i < n; ++i) {
            if (tr_variantGetInt (tr_variantListChild (list, i), &tmp)) {
                if (0 <= tmp && tmp < tor->info.fileCount) {
                    files[fileCount++] = tmp;
                } else {
                    errmsg = "file index out of range";
                }
            }
        }
    }
    else /* if empty set, apply to all */
    {
        tr_file_index_t t;
        for (t = 0; t < tor->info.fileCount; ++t)
            files[fileCount++] = t;
    }

    if (fileCount)
        tr_torrentSetFileDLs (tor, files, fileCount, do_download);

    tr_free (files);
    return errmsg;
}

static bool
findAnnounceUrl (const tr_tracker_info * t, int n, const char * url, int * pos)
{
    int i;
    bool found = false;

    for (i=0; i<n; ++i)
    {
        if (!strcmp (t[i].announce, url))
        {
            found = true;
            if (pos) *pos = i;
            break;
        }
    }

    return found;
}

static int
copyTrackers (tr_tracker_info * tgt, const tr_tracker_info * src, int n)
{
    int i;
    int maxTier = -1;

    for (i=0; i<n; ++i)
    {
        tgt[i].tier = src[i].tier;
        tgt[i].announce = tr_strdup (src[i].announce);
        maxTier = MAX (maxTier, src[i].tier);
    }

    return maxTier;
}

static void
freeTrackers (tr_tracker_info * trackers, int n)
{
    int i;

    for (i=0; i<n; ++i)
        tr_free (trackers[i].announce);

    tr_free (trackers);
}

static const char*
addTrackerUrls (tr_torrent * tor, tr_variant * urls)
{
    int i;
    int n;
    int tier;
    tr_variant * val;
    tr_tracker_info * trackers;
    bool changed = false;
    const tr_info * inf = tr_torrentInfo (tor);
    const char * errmsg = NULL;

    /* make a working copy of the existing announce list */
    n = inf->trackerCount;
    trackers = tr_new0 (tr_tracker_info, n + tr_variantListSize (urls));
    tier = copyTrackers (trackers, inf->trackers, n);

    /* and add the new ones */
    i = 0;
    while ((val = tr_variantListChild (urls, i++)))
    {
        const char * announce = NULL;

        if ( tr_variantGetStr (val, &announce, NULL)
            && tr_urlIsValidTracker (announce)
            && !findAnnounceUrl (trackers, n, announce, NULL))
        {
            trackers[n].tier = ++tier; /* add a new tier */
            trackers[n].announce = tr_strdup (announce);
            ++n;
            changed = true;
        }
    }

    if (!changed)
        errmsg = "invalid argument";
    else if (!tr_torrentSetAnnounceList (tor, trackers, n))
        errmsg = "error setting announce list";

    freeTrackers (trackers, n);
    return errmsg;
}

static const char*
replaceTrackers (tr_torrent * tor, tr_variant * urls)
{
    int i;
    tr_variant * pair[2];
    tr_tracker_info * trackers;
    bool changed = false;
    const tr_info * inf = tr_torrentInfo (tor);
    const int n = inf->trackerCount;
    const char * errmsg = NULL;

    /* make a working copy of the existing announce list */
    trackers = tr_new0 (tr_tracker_info, n);
    copyTrackers (trackers, inf->trackers, n);

    /* make the substitutions... */
    i = 0;
    while (((pair[0] = tr_variantListChild (urls,i))) &&
        ((pair[1] = tr_variantListChild (urls,i+1))))
    {
        size_t len;
        int64_t pos;
        const char * newval;

        if ( tr_variantGetInt (pair[0], &pos)
            && tr_variantGetStr (pair[1], &newval, &len)
            && tr_urlIsValidTracker (newval)
            && pos < n
            && pos >= 0)
        {
            tr_free (trackers[pos].announce);
            trackers[pos].announce = tr_strndup (newval, len);
            changed = true;
        }

        i += 2;
    }

    if (!changed)
        errmsg = "invalid argument";
    else if (!tr_torrentSetAnnounceList (tor, trackers, n))
        errmsg = "error setting announce list";

    freeTrackers (trackers, n);
    return errmsg;
}

static const char*
removeTrackers (tr_torrent * tor, tr_variant * ids)
{
    int i;
    int n;
    int t = 0;
    int dup = -1;
    int * tids;
    tr_variant * val;
    tr_tracker_info * trackers;
    bool changed = false;
    const tr_info * inf = tr_torrentInfo (tor);
    const char * errmsg = NULL;

    /* make a working copy of the existing announce list */
    n = inf->trackerCount;
    tids = tr_new0 (int, n);
    trackers = tr_new0 (tr_tracker_info, n);
    copyTrackers (trackers, inf->trackers, n);

    /* remove the ones specified in the urls list */
    i = 0;
    while ((val = tr_variantListChild (ids, i++)))
    {
        int64_t pos;

        if ( tr_variantGetInt (val, &pos)
            && pos < n
            && pos >= 0)
            tids[t++] = pos;
    }

    /* sort trackerIds and remove from largest to smallest so there is no need to recacluate array indicies */
    qsort (tids, t, sizeof (int), compareInt);
    while (t--)
    {
        /* check for duplicates */
        if (tids[t] == dup)
            continue;
        tr_removeElementFromArray (trackers, tids[t], sizeof (tr_tracker_info), n--);
        dup = tids[t];
        changed = true;
    }

    if (!changed)
        errmsg = "invalid argument";
    else if (!tr_torrentSetAnnounceList (tor, trackers, n))
        errmsg = "error setting announce list";

    freeTrackers (trackers, n);
    tr_free (tids);
    return errmsg;
}

static const char*
torrentSet (tr_session               * session,
            tr_variant                  * args_in,
            tr_variant                  * args_out UNUSED,
            struct tr_rpc_idle_data  * idle_data UNUSED)
{
    const char * errmsg = NULL;
    int i, torrentCount;
    tr_torrent ** torrents = getTorrents (session, args_in, &torrentCount);

    assert (idle_data == NULL);

    for (i = 0; i < torrentCount; ++i)
    {
        int64_t      tmp;
        double       d;
        tr_variant *    files;
        tr_variant *    trackers;
        bool         boolVal;
        tr_torrent * tor = torrents[i];

        if (tr_variantDictFindInt (args_in, "bandwidthPriority", &tmp))
            if (tr_isPriority (tmp))
                tr_torrentSetPriority (tor, tmp);
        if (!errmsg && tr_variantDictFindList (args_in, "files-unwanted", &files))
            errmsg = setFileDLs (tor, false, files);
        if (!errmsg && tr_variantDictFindList (args_in, "files-wanted", &files))
            errmsg = setFileDLs (tor, true, files);
        if (tr_variantDictFindInt (args_in, "peer-limit", &tmp))
            tr_torrentSetPeerLimit (tor, tmp);
        if (!errmsg &&  tr_variantDictFindList (args_in, "priority-high", &files))
            errmsg = setFilePriorities (tor, TR_PRI_HIGH, files);
        if (!errmsg && tr_variantDictFindList (args_in, "priority-low", &files))
            errmsg = setFilePriorities (tor, TR_PRI_LOW, files);
        if (!errmsg && tr_variantDictFindList (args_in, "priority-normal", &files))
            errmsg = setFilePriorities (tor, TR_PRI_NORMAL, files);
        if (tr_variantDictFindInt (args_in, "downloadLimit", &tmp))
            tr_torrentSetSpeedLimit_KBps (tor, TR_DOWN, tmp);
        if (tr_variantDictFindBool (args_in, "downloadLimited", &boolVal))
            tr_torrentUseSpeedLimit (tor, TR_DOWN, boolVal);
        if (tr_variantDictFindBool (args_in, "honorsSessionLimits", &boolVal))
            tr_torrentUseSessionLimits (tor, boolVal);
        if (tr_variantDictFindInt (args_in, "uploadLimit", &tmp))
            tr_torrentSetSpeedLimit_KBps (tor, TR_UP, tmp);
        if (tr_variantDictFindBool (args_in, "uploadLimited", &boolVal))
            tr_torrentUseSpeedLimit (tor, TR_UP, boolVal);
        if (tr_variantDictFindInt (args_in, "seedIdleLimit", &tmp))
            tr_torrentSetIdleLimit (tor, tmp);
        if (tr_variantDictFindInt (args_in, "seedIdleMode", &tmp))
            tr_torrentSetIdleMode (tor, tmp);
        if (tr_variantDictFindReal (args_in, "seedRatioLimit", &d))
            tr_torrentSetRatioLimit (tor, d);
        if (tr_variantDictFindInt (args_in, "seedRatioMode", &tmp))
            tr_torrentSetRatioMode (tor, tmp);
        if (tr_variantDictFindInt (args_in, "queuePosition", &tmp))
            tr_torrentSetQueuePosition (tor, tmp);
        if (!errmsg && tr_variantDictFindList (args_in, "trackerAdd", &trackers))
            errmsg = addTrackerUrls (tor, trackers);
        if (!errmsg && tr_variantDictFindList (args_in, "trackerRemove", &trackers))
            errmsg = removeTrackers (tor, trackers);
        if (!errmsg && tr_variantDictFindList (args_in, "trackerReplace", &trackers))
            errmsg = replaceTrackers (tor, trackers);
        notify (session, TR_RPC_TORRENT_CHANGED, tor);
    }

    tr_free (torrents);
    return errmsg;
}

static const char*
torrentSetLocation (tr_session               * session,
                    tr_variant                  * args_in,
                    tr_variant                  * args_out UNUSED,
                    struct tr_rpc_idle_data  * idle_data UNUSED)
{
    const char * errmsg = NULL;
    const char * location = NULL;

    assert (idle_data == NULL);

    if (!tr_variantDictFindStr (args_in, "location", &location, NULL))
    {
        errmsg = "no location";
    }
    else
    {
        bool move = false;
        int i, torrentCount;
        tr_torrent ** torrents = getTorrents (session, args_in, &torrentCount);

        tr_variantDictFindBool (args_in, "move", &move);

        for (i=0; i<torrentCount; ++i)
        {
            tr_torrent * tor = torrents[i];
            tr_torrentSetLocation (tor, location, move, NULL, NULL);
            notify (session, TR_RPC_TORRENT_MOVED, tor);
        }

        tr_free (torrents);
    }

    return errmsg;
}

/***
****
***/

static void
portTested (tr_session       * session UNUSED,
            bool               did_connect UNUSED,
            bool               did_timeout UNUSED,
            long               response_code,
            const void       * response,
            size_t             response_byte_count,
            void             * user_data)
{
    char result[1024];
    struct tr_rpc_idle_data * data = user_data;

    if (response_code != 200)
    {
        tr_snprintf (result, sizeof (result), "portTested: http error %ld: %s",
                     response_code, tr_webGetResponseStr (response_code));
    }
    else /* success */
    {
        const bool isOpen = response_byte_count && * (char*)response == '1';
        tr_variantDictAddBool (data->args_out, "port-is-open", isOpen);
        tr_snprintf (result, sizeof (result), "success");
    }

    tr_idle_function_done (data, result);
}

static const char*
portTest (tr_session               * session,
          tr_variant                  * args_in UNUSED,
          tr_variant                  * args_out UNUSED,
          struct tr_rpc_idle_data  * idle_data)
{
    const int port = tr_sessionGetPeerPort (session);
    char * url = tr_strdup_printf ("http://portcheck.transmissionbt.com/%d", port);
    tr_webRun (session, url, NULL, NULL, portTested, idle_data);
    tr_free (url);
    return NULL;
}

/***
****
***/

static void
gotNewBlocklist (tr_session       * session,
                 bool               did_connect UNUSED,
                 bool               did_timeout UNUSED,
                 long               response_code,
                 const void       * response,
                 size_t             response_byte_count,
                 void             * user_data)
{
    char result[1024];
    struct tr_rpc_idle_data * data = user_data;

    *result = '\0';

    if (response_code != 200)
    {
        tr_snprintf (result, sizeof (result), "gotNewBlocklist: http error %ld: %s",
                     response_code, tr_webGetResponseStr (response_code));
    }
    else /* successfully fetched the blocklist... */
    {
        int fd;
        int err;
        char * filename;
        z_stream stream;
        const char * configDir = tr_sessionGetConfigDir (session);
        const size_t buflen = 1024 * 128; /* 128 KiB buffer */
        uint8_t * buf = tr_valloc (buflen);

        /* this is an odd Magic Number required by zlib to enable gz support.
           See zlib's inflateInit2 () documentation for a full description */
        const int windowBits = 15 + 32;

        stream.zalloc = (alloc_func) Z_NULL;
        stream.zfree = (free_func) Z_NULL;
        stream.opaque = (voidpf) Z_NULL;
        stream.next_in = (void*) response;
        stream.avail_in = response_byte_count;
        inflateInit2 (&stream, windowBits);

        filename = tr_buildPath (configDir, "blocklist.tmp", NULL);
        fd = tr_open_file_for_writing (filename);
        if (fd < 0)
            tr_snprintf (result, sizeof (result), _("Couldn't save file \"%1$s\": %2$s"), filename, tr_strerror (errno));

        for (;;)
        {
            stream.next_out = (void*) buf;
            stream.avail_out = buflen;
            err = inflate (&stream, Z_NO_FLUSH);

            if (stream.avail_out < buflen) {
                const int e = write (fd, buf, buflen - stream.avail_out);
                if (e < 0) {
                    tr_snprintf (result, sizeof (result), _("Couldn't save file \"%1$s\": %2$s"), filename, tr_strerror (errno));
                    break;
                }
            }

            if (err != Z_OK) {
                if ((err != Z_STREAM_END) && (err != Z_DATA_ERROR))
                    tr_snprintf (result, sizeof (result), _("Error uncompressing blocklist: %s (%d)"), zError (err), err);
                break;
            }
        }

        inflateEnd (&stream);

        if (err == Z_DATA_ERROR) /* couldn't inflate it... it's probably already uncompressed */
            if (write (fd, response, response_byte_count) < 0)
                tr_snprintf (result, sizeof (result), _("Couldn't save file \"%1$s\": %2$s"), filename, tr_strerror (errno));

        if (*result)
            tr_err ("%s", result);
        else {
            /* feed it to the session and give the client a response */
            const int rule_count = tr_blocklistSetContent (session, filename);
            tr_variantDictAddInt (data->args_out, "blocklist-size", rule_count);
            tr_snprintf (result, sizeof (result), "success");
        }

        unlink (filename);
        tr_free (filename);
        tr_free (buf);
    }

    tr_idle_function_done (data, result);
}

static const char*
blocklistUpdate (tr_session               * session,
                 tr_variant                  * args_in UNUSED,
                 tr_variant                  * args_out UNUSED,
                 struct tr_rpc_idle_data  * idle_data)
{
    tr_webRun (session, session->blocklist_url, NULL, NULL, gotNewBlocklist, idle_data);
    return NULL;
}

/***
****
***/

static void
addTorrentImpl (struct tr_rpc_idle_data * data, tr_ctor * ctor)
{
    int err = 0;
    const char * result = NULL;
    tr_torrent * tor = tr_torrentNew (ctor, &err);

    tr_ctorFree (ctor);

    if (tor)
    {
        tr_variant fields;
        tr_variantInitList (&fields, 3);
        tr_variantListAddStr (&fields, "id");
        tr_variantListAddStr (&fields, "name");
        tr_variantListAddStr (&fields, "hashString");
        addInfo (tor, tr_variantDictAdd (data->args_out, "torrent-added", 13), &fields);
        notify (data->session, TR_RPC_TORRENT_ADDED, tor);
        tr_variantFree (&fields);
    }
    else if (err == TR_PARSE_DUPLICATE)
    {
        result = "duplicate torrent";
    }
    else if (err == TR_PARSE_ERR)
    {
        result = "invalid or corrupt torrent file";
    }

    tr_idle_function_done (data, result);
}


struct add_torrent_idle_data
{
    struct tr_rpc_idle_data * data;
    tr_ctor * ctor;
};

static void
gotMetadataFromURL (tr_session       * session UNUSED,
                    bool               did_connect UNUSED,
                    bool               did_timeout UNUSED,
                    long               response_code,
                    const void       * response,
                    size_t             response_byte_count,
                    void             * user_data)
{
    struct add_torrent_idle_data * data = user_data;

    dbgmsg ("torrentAdd: HTTP response code was %ld (%s); response length was %zu bytes",
            response_code, tr_webGetResponseStr (response_code), response_byte_count);

    if (response_code==200 || response_code==221) /* http or ftp success.. */
    {
        tr_ctorSetMetainfo (data->ctor, response, response_byte_count);
        addTorrentImpl (data->data, data->ctor);
    }
    else
    {
        char result[1024];
        tr_snprintf (result, sizeof (result), "gotMetadataFromURL: http error %ld: %s",
                     response_code, tr_webGetResponseStr (response_code));
        tr_idle_function_done (data->data, result);
    }

    tr_free (data);
}

static bool
isCurlURL (const char * filename)
{
    if (filename == NULL)
        return false;

    return !strncmp (filename, "ftp://", 6) ||
           !strncmp (filename, "http://", 7) ||
           !strncmp (filename, "https://", 8);
}

static tr_file_index_t*
fileListFromList (tr_variant * list, tr_file_index_t * setmeCount)
{
    size_t i;
    const size_t childCount = tr_variantListSize (list);
    tr_file_index_t n = 0;
    tr_file_index_t * files = tr_new0 (tr_file_index_t, childCount);

    for (i=0; i<childCount; ++i) {
        int64_t intVal;
        if (tr_variantGetInt (tr_variantListChild (list, i), &intVal))
            files[n++] = (tr_file_index_t)intVal;
    }

    *setmeCount = n;
    return files;
}

static const char*
torrentAdd (tr_session               * session,
            tr_variant                  * args_in,
            tr_variant                  * args_out UNUSED,
            struct tr_rpc_idle_data  * idle_data)
{
    const char * filename = NULL;
    const char * metainfo_base64 = NULL;

    assert (idle_data != NULL);

    tr_variantDictFindStr (args_in, "filename", &filename, NULL);
    tr_variantDictFindStr (args_in, "metainfo", &metainfo_base64, NULL);
    if (!filename && !metainfo_base64)
        return "no filename or metainfo specified";
    else
    {
        int64_t      i;
        bool         boolVal;
        tr_variant    * l;
        const char * str;
        const char * cookies = NULL;
        tr_ctor    * ctor = tr_ctorNew (session);

        /* set the optional arguments */

        tr_variantDictFindStr (args_in, "cookies", &cookies, NULL);

        if (tr_variantDictFindStr (args_in, TR_PREFS_KEY_DOWNLOAD_DIR, &str, NULL))
            tr_ctorSetDownloadDir (ctor, TR_FORCE, str);

        if (tr_variantDictFindBool (args_in, "paused", &boolVal))
            tr_ctorSetPaused (ctor, TR_FORCE, boolVal);

        if (tr_variantDictFindInt (args_in, "peer-limit", &i))
            tr_ctorSetPeerLimit (ctor, TR_FORCE, i);

        if (tr_variantDictFindInt (args_in, "bandwidthPriority", &i))
            tr_ctorSetBandwidthPriority (ctor, i);

        if (tr_variantDictFindList (args_in, "files-unwanted", &l)) {
            tr_file_index_t fileCount;
            tr_file_index_t * files = fileListFromList (l, &fileCount);
            tr_ctorSetFilesWanted (ctor, files, fileCount, false);
            tr_free (files);
        }
        if (tr_variantDictFindList (args_in, "files-wanted", &l)) {
            tr_file_index_t fileCount;
            tr_file_index_t * files = fileListFromList (l, &fileCount);
            tr_ctorSetFilesWanted (ctor, files, fileCount, true);
            tr_free (files);
        }

        if (tr_variantDictFindList (args_in, "priority-low", &l)) {
            tr_file_index_t fileCount;
            tr_file_index_t * files = fileListFromList (l, &fileCount);
            tr_ctorSetFilePriorities (ctor, files, fileCount, TR_PRI_LOW);
            tr_free (files);
        }
        if (tr_variantDictFindList (args_in, "priority-normal", &l)) {
            tr_file_index_t fileCount;
            tr_file_index_t * files = fileListFromList (l, &fileCount);
            tr_ctorSetFilePriorities (ctor, files, fileCount, TR_PRI_NORMAL);
            tr_free (files);
        }
        if (tr_variantDictFindList (args_in, "priority-high", &l)) {
            tr_file_index_t fileCount;
            tr_file_index_t * files = fileListFromList (l, &fileCount);
            tr_ctorSetFilePriorities (ctor, files, fileCount, TR_PRI_HIGH);
            tr_free (files);
        }

        dbgmsg ("torrentAdd: filename is \"%s\"", filename ? filename : " (null)");

        if (isCurlURL (filename))
        {
            struct add_torrent_idle_data * d = tr_new0 (struct add_torrent_idle_data, 1);
            d->data = idle_data;
            d->ctor = ctor;
            tr_webRun (session, filename, NULL, cookies, gotMetadataFromURL, d);
        }
        else
        {
            char * fname = tr_strstrip (tr_strdup (filename));

            if (fname == NULL)
            {
                int len;
                char * metainfo = tr_base64_decode (metainfo_base64, -1, &len);
                tr_ctorSetMetainfo (ctor, (uint8_t*)metainfo, len);
                tr_free (metainfo);
            }
            else if (!strncmp (fname, "magnet:?", 8))
            {
                tr_ctorSetMetainfoFromMagnetLink (ctor, fname);
            }
            else
            {
                tr_ctorSetMetainfoFromFile (ctor, fname);
            }

            addTorrentImpl (idle_data, ctor);

            tr_free (fname);
        }

    }

    return NULL;
}

/***
****
***/

static const char*
sessionSet (tr_session               * session,
            tr_variant                  * args_in,
            tr_variant                  * args_out UNUSED,
            struct tr_rpc_idle_data  * idle_data UNUSED)
{
    int64_t      i;
    double       d;
    bool         boolVal;
    const char * str;

    assert (idle_data == NULL);

    if (tr_variantDictFindInt (args_in, TR_PREFS_KEY_MAX_CACHE_SIZE_MB, &i))
        tr_sessionSetCacheLimit_MB (session, i);
    if (tr_variantDictFindInt (args_in, TR_PREFS_KEY_ALT_SPEED_UP_KBps, &i))
        tr_sessionSetAltSpeed_KBps (session, TR_UP, i);
    if (tr_variantDictFindInt (args_in, TR_PREFS_KEY_ALT_SPEED_DOWN_KBps, &i))
        tr_sessionSetAltSpeed_KBps (session, TR_DOWN, i);
    if (tr_variantDictFindBool (args_in, TR_PREFS_KEY_ALT_SPEED_ENABLED, &boolVal))
        tr_sessionUseAltSpeed (session, boolVal);
    if (tr_variantDictFindInt (args_in, TR_PREFS_KEY_ALT_SPEED_TIME_BEGIN, &i))
        tr_sessionSetAltSpeedBegin (session, i);
    if (tr_variantDictFindInt (args_in, TR_PREFS_KEY_ALT_SPEED_TIME_END, &i))
        tr_sessionSetAltSpeedEnd (session, i);
    if (tr_variantDictFindInt (args_in, TR_PREFS_KEY_ALT_SPEED_TIME_DAY, &i))
        tr_sessionSetAltSpeedDay (session, i);
    if (tr_variantDictFindBool (args_in, TR_PREFS_KEY_ALT_SPEED_TIME_ENABLED, &boolVal))
        tr_sessionUseAltSpeedTime (session, boolVal);
    if (tr_variantDictFindBool (args_in, TR_PREFS_KEY_BLOCKLIST_ENABLED, &boolVal))
        tr_blocklistSetEnabled (session, boolVal);
    if (tr_variantDictFindStr (args_in, TR_PREFS_KEY_BLOCKLIST_URL, &str, NULL))
        tr_blocklistSetURL (session, str);
    if (tr_variantDictFindStr (args_in, TR_PREFS_KEY_DOWNLOAD_DIR, &str, NULL))
        tr_sessionSetDownloadDir (session, str);
    if (tr_variantDictFindInt (args_in, TR_PREFS_KEY_QUEUE_STALLED_MINUTES, &i))
        tr_sessionSetQueueStalledMinutes (session, i);
    if (tr_variantDictFindBool (args_in, TR_PREFS_KEY_QUEUE_STALLED_ENABLED, &boolVal))
        tr_sessionSetQueueStalledEnabled (session, boolVal);
    if (tr_variantDictFindInt (args_in, TR_PREFS_KEY_DOWNLOAD_QUEUE_SIZE, &i))
        tr_sessionSetQueueSize (session, TR_DOWN, i);
    if (tr_variantDictFindBool (args_in, TR_PREFS_KEY_DOWNLOAD_QUEUE_ENABLED, &boolVal))
        tr_sessionSetQueueEnabled (session, TR_DOWN, boolVal);
    if (tr_variantDictFindStr (args_in, TR_PREFS_KEY_INCOMPLETE_DIR, &str, NULL))
        tr_sessionSetIncompleteDir (session, str);
    if (tr_variantDictFindBool (args_in, TR_PREFS_KEY_INCOMPLETE_DIR_ENABLED, &boolVal))
        tr_sessionSetIncompleteDirEnabled (session, boolVal);
    if (tr_variantDictFindInt (args_in, TR_PREFS_KEY_PEER_LIMIT_GLOBAL, &i))
        tr_sessionSetPeerLimit (session, i);
    if (tr_variantDictFindInt (args_in, TR_PREFS_KEY_PEER_LIMIT_TORRENT, &i))
        tr_sessionSetPeerLimitPerTorrent (session, i);
    if (tr_variantDictFindBool (args_in, TR_PREFS_KEY_PEX_ENABLED, &boolVal))
        tr_sessionSetPexEnabled (session, boolVal);
    if (tr_variantDictFindBool (args_in, TR_PREFS_KEY_DHT_ENABLED, &boolVal))
        tr_sessionSetDHTEnabled (session, boolVal);
    if (tr_variantDictFindBool (args_in, TR_PREFS_KEY_UTP_ENABLED, &boolVal))
        tr_sessionSetUTPEnabled (session, boolVal);
    if (tr_variantDictFindBool (args_in, TR_PREFS_KEY_LPD_ENABLED, &boolVal))
        tr_sessionSetLPDEnabled (session, boolVal);
    if (tr_variantDictFindBool (args_in, TR_PREFS_KEY_PEER_PORT_RANDOM_ON_START, &boolVal))
        tr_sessionSetPeerPortRandomOnStart (session, boolVal);
    if (tr_variantDictFindInt (args_in, TR_PREFS_KEY_PEER_PORT, &i))
        tr_sessionSetPeerPort (session, i);
    if (tr_variantDictFindBool (args_in, TR_PREFS_KEY_PORT_FORWARDING, &boolVal))
        tr_sessionSetPortForwardingEnabled (session, boolVal);
    if (tr_variantDictFindBool (args_in, TR_PREFS_KEY_RENAME_PARTIAL_FILES, &boolVal))
        tr_sessionSetIncompleteFileNamingEnabled (session, boolVal);
    if (tr_variantDictFindReal (args_in, "seedRatioLimit", &d))
        tr_sessionSetRatioLimit (session, d);
    if (tr_variantDictFindBool (args_in, "seedRatioLimited", &boolVal))
        tr_sessionSetRatioLimited (session, boolVal);
    if (tr_variantDictFindInt (args_in, TR_PREFS_KEY_IDLE_LIMIT, &i))
        tr_sessionSetIdleLimit (session, i);
    if (tr_variantDictFindBool (args_in, TR_PREFS_KEY_IDLE_LIMIT_ENABLED, &boolVal))
        tr_sessionSetIdleLimited (session, boolVal);
    if (tr_variantDictFindBool (args_in, TR_PREFS_KEY_START, &boolVal))
        tr_sessionSetPaused (session, !boolVal);
    if (tr_variantDictFindBool (args_in, TR_PREFS_KEY_SEED_QUEUE_ENABLED, &boolVal))
        tr_sessionSetQueueEnabled (session, TR_UP, boolVal);
    if (tr_variantDictFindInt (args_in, TR_PREFS_KEY_SEED_QUEUE_SIZE, &i))
        tr_sessionSetQueueSize (session, TR_UP, i);
    if (tr_variantDictFindStr (args_in, TR_PREFS_KEY_SCRIPT_TORRENT_DONE_FILENAME, &str, NULL))
        tr_sessionSetTorrentDoneScript (session, str);
    if (tr_variantDictFindBool (args_in, TR_PREFS_KEY_SCRIPT_TORRENT_DONE_ENABLED, &boolVal))
        tr_sessionSetTorrentDoneScriptEnabled (session, boolVal);
    if (tr_variantDictFindBool (args_in, TR_PREFS_KEY_TRASH_ORIGINAL, &boolVal))
        tr_sessionSetDeleteSource (session, boolVal);
    if (tr_variantDictFindInt (args_in, TR_PREFS_KEY_DSPEED_KBps, &i))
        tr_sessionSetSpeedLimit_KBps (session, TR_DOWN, i);
    if (tr_variantDictFindBool (args_in, TR_PREFS_KEY_DSPEED_ENABLED, &boolVal))
        tr_sessionLimitSpeed (session, TR_DOWN, boolVal);
    if (tr_variantDictFindInt (args_in, TR_PREFS_KEY_USPEED_KBps, &i))
        tr_sessionSetSpeedLimit_KBps (session, TR_UP, i);
    if (tr_variantDictFindBool (args_in, TR_PREFS_KEY_USPEED_ENABLED, &boolVal))
        tr_sessionLimitSpeed (session, TR_UP, boolVal);
    if (tr_variantDictFindStr (args_in, TR_PREFS_KEY_ENCRYPTION, &str, NULL)) {
        if (!strcmp (str, "required"))
            tr_sessionSetEncryption (session, TR_ENCRYPTION_REQUIRED);
        else if (!strcmp (str, "tolerated"))
            tr_sessionSetEncryption (session, TR_CLEAR_PREFERRED);
        else
            tr_sessionSetEncryption (session, TR_ENCRYPTION_PREFERRED);
    }

    notify (session, TR_RPC_SESSION_CHANGED, NULL);

    return NULL;
}

static const char*
sessionStats (tr_session               * session,
              tr_variant                  * args_in UNUSED,
              tr_variant                  * args_out,
              struct tr_rpc_idle_data  * idle_data UNUSED)
{
    int running = 0;
    int total = 0;
    tr_variant * d;
    tr_session_stats currentStats = { 0.0f, 0, 0, 0, 0, 0 };
    tr_session_stats cumulativeStats = { 0.0f, 0, 0, 0, 0, 0 };
    tr_torrent * tor = NULL;

    assert (idle_data == NULL);

    while ((tor = tr_torrentNext (session, tor))) {
        ++total;
        if (tor->isRunning)
            ++running;
    }

    tr_sessionGetStats (session, &currentStats);
    tr_sessionGetCumulativeStats (session, &cumulativeStats);

    tr_variantDictAddInt (args_out, "activeTorrentCount", running);
    tr_variantDictAddReal (args_out, "downloadSpeed", tr_sessionGetPieceSpeed_Bps (session, TR_DOWN));
    tr_variantDictAddInt (args_out, "pausedTorrentCount", total - running);
    tr_variantDictAddInt (args_out, "torrentCount", total);
    tr_variantDictAddReal (args_out, "uploadSpeed", tr_sessionGetPieceSpeed_Bps (session, TR_UP));

    d = tr_variantDictAddDict (args_out, "cumulative-stats", 5);
    tr_variantDictAddInt (d, "downloadedBytes", cumulativeStats.downloadedBytes);
    tr_variantDictAddInt (d, "filesAdded", cumulativeStats.filesAdded);
    tr_variantDictAddInt (d, "secondsActive", cumulativeStats.secondsActive);
    tr_variantDictAddInt (d, "sessionCount", cumulativeStats.sessionCount);
    tr_variantDictAddInt (d, "uploadedBytes", cumulativeStats.uploadedBytes);

    d = tr_variantDictAddDict (args_out, "current-stats", 5);
    tr_variantDictAddInt (d, "downloadedBytes", currentStats.downloadedBytes);
    tr_variantDictAddInt (d, "filesAdded", currentStats.filesAdded);
    tr_variantDictAddInt (d, "secondsActive", currentStats.secondsActive);
    tr_variantDictAddInt (d, "sessionCount", currentStats.sessionCount);
    tr_variantDictAddInt (d, "uploadedBytes", currentStats.uploadedBytes);

    return NULL;
}

static const char*
sessionGet (tr_session               * s,
            tr_variant                  * args_in UNUSED,
            tr_variant                  * args_out,
            struct tr_rpc_idle_data  * idle_data UNUSED)
{
    const char * str;
    tr_variant * d = args_out;

    assert (idle_data == NULL);
    tr_variantDictAddInt (d, TR_PREFS_KEY_ALT_SPEED_UP_KBps, tr_sessionGetAltSpeed_KBps (s,TR_UP));
    tr_variantDictAddInt (d, TR_PREFS_KEY_ALT_SPEED_DOWN_KBps, tr_sessionGetAltSpeed_KBps (s,TR_DOWN));
    tr_variantDictAddBool (d, TR_PREFS_KEY_ALT_SPEED_ENABLED, tr_sessionUsesAltSpeed (s));
    tr_variantDictAddInt (d, TR_PREFS_KEY_ALT_SPEED_TIME_BEGIN, tr_sessionGetAltSpeedBegin (s));
    tr_variantDictAddInt (d, TR_PREFS_KEY_ALT_SPEED_TIME_END,tr_sessionGetAltSpeedEnd (s));
    tr_variantDictAddInt (d, TR_PREFS_KEY_ALT_SPEED_TIME_DAY,tr_sessionGetAltSpeedDay (s));
    tr_variantDictAddBool (d, TR_PREFS_KEY_ALT_SPEED_TIME_ENABLED, tr_sessionUsesAltSpeedTime (s));
    tr_variantDictAddBool (d, TR_PREFS_KEY_BLOCKLIST_ENABLED, tr_blocklistIsEnabled (s));
    tr_variantDictAddStr (d, TR_PREFS_KEY_BLOCKLIST_URL, tr_blocklistGetURL (s));
    tr_variantDictAddInt (d, TR_PREFS_KEY_MAX_CACHE_SIZE_MB, tr_sessionGetCacheLimit_MB (s));
    tr_variantDictAddInt (d, "blocklist-size", tr_blocklistGetRuleCount (s));
    tr_variantDictAddStr (d, "config-dir", tr_sessionGetConfigDir (s));
    tr_variantDictAddStr (d, TR_PREFS_KEY_DOWNLOAD_DIR, tr_sessionGetDownloadDir (s));
    tr_variantDictAddBool (d, TR_PREFS_KEY_DOWNLOAD_QUEUE_ENABLED, tr_sessionGetQueueEnabled (s, TR_DOWN));
    tr_variantDictAddInt (d, TR_PREFS_KEY_DOWNLOAD_QUEUE_SIZE, tr_sessionGetQueueSize (s, TR_DOWN));
    tr_variantDictAddInt (d, "download-dir-free-space",  tr_sessionGetDownloadDirFreeSpace (s));
    tr_variantDictAddInt (d, TR_PREFS_KEY_PEER_LIMIT_GLOBAL, tr_sessionGetPeerLimit (s));
    tr_variantDictAddInt (d, TR_PREFS_KEY_PEER_LIMIT_TORRENT, tr_sessionGetPeerLimitPerTorrent (s));
    tr_variantDictAddStr (d, TR_PREFS_KEY_INCOMPLETE_DIR, tr_sessionGetIncompleteDir (s));
    tr_variantDictAddBool (d, TR_PREFS_KEY_INCOMPLETE_DIR_ENABLED, tr_sessionIsIncompleteDirEnabled (s));
    tr_variantDictAddBool (d, TR_PREFS_KEY_PEX_ENABLED, tr_sessionIsPexEnabled (s));
    tr_variantDictAddBool (d, TR_PREFS_KEY_UTP_ENABLED, tr_sessionIsUTPEnabled (s));
    tr_variantDictAddBool (d, TR_PREFS_KEY_DHT_ENABLED, tr_sessionIsDHTEnabled (s));
    tr_variantDictAddBool (d, TR_PREFS_KEY_LPD_ENABLED, tr_sessionIsLPDEnabled (s));
    tr_variantDictAddInt (d, TR_PREFS_KEY_PEER_PORT, tr_sessionGetPeerPort (s));
    tr_variantDictAddBool (d, TR_PREFS_KEY_PEER_PORT_RANDOM_ON_START, tr_sessionGetPeerPortRandomOnStart (s));
    tr_variantDictAddBool (d, TR_PREFS_KEY_PORT_FORWARDING, tr_sessionIsPortForwardingEnabled (s));
    tr_variantDictAddBool (d, TR_PREFS_KEY_RENAME_PARTIAL_FILES, tr_sessionIsIncompleteFileNamingEnabled (s));
    tr_variantDictAddInt (d, "rpc-version", RPC_VERSION);
    tr_variantDictAddInt (d, "rpc-version-minimum", RPC_VERSION_MIN);
    tr_variantDictAddReal (d, "seedRatioLimit", tr_sessionGetRatioLimit (s));
    tr_variantDictAddBool (d, "seedRatioLimited", tr_sessionIsRatioLimited (s));
    tr_variantDictAddInt (d, TR_PREFS_KEY_IDLE_LIMIT, tr_sessionGetIdleLimit (s));
    tr_variantDictAddBool (d, TR_PREFS_KEY_IDLE_LIMIT_ENABLED, tr_sessionIsIdleLimited (s));
    tr_variantDictAddBool (d, TR_PREFS_KEY_SEED_QUEUE_ENABLED, tr_sessionGetQueueEnabled (s, TR_UP));
    tr_variantDictAddInt (d, TR_PREFS_KEY_SEED_QUEUE_SIZE, tr_sessionGetQueueSize (s, TR_UP));
    tr_variantDictAddBool (d, TR_PREFS_KEY_START, !tr_sessionGetPaused (s));
    tr_variantDictAddBool (d, TR_PREFS_KEY_TRASH_ORIGINAL, tr_sessionGetDeleteSource (s));
    tr_variantDictAddInt (d, TR_PREFS_KEY_USPEED_KBps, tr_sessionGetSpeedLimit_KBps (s, TR_UP));
    tr_variantDictAddBool (d, TR_PREFS_KEY_USPEED_ENABLED, tr_sessionIsSpeedLimited (s, TR_UP));
    tr_variantDictAddInt (d, TR_PREFS_KEY_DSPEED_KBps, tr_sessionGetSpeedLimit_KBps (s, TR_DOWN));
    tr_variantDictAddBool (d, TR_PREFS_KEY_DSPEED_ENABLED, tr_sessionIsSpeedLimited (s, TR_DOWN));
    tr_variantDictAddStr (d, TR_PREFS_KEY_SCRIPT_TORRENT_DONE_FILENAME, tr_sessionGetTorrentDoneScript (s));
    tr_variantDictAddBool (d, TR_PREFS_KEY_SCRIPT_TORRENT_DONE_ENABLED, tr_sessionIsTorrentDoneScriptEnabled (s));
    tr_variantDictAddInt (d, TR_PREFS_KEY_QUEUE_STALLED_MINUTES, tr_sessionGetQueueStalledMinutes (s));
    tr_variantDictAddBool (d, TR_PREFS_KEY_QUEUE_STALLED_ENABLED, tr_sessionGetQueueStalledEnabled (s));
    tr_formatter_get_units (tr_variantDictAddDict (d, "units", 0));
    tr_variantDictAddStr (d, "version", LONG_VERSION_STRING);
    switch (tr_sessionGetEncryption (s)) {
        case TR_CLEAR_PREFERRED: str = "tolerated"; break;
        case TR_ENCRYPTION_REQUIRED: str = "required"; break;
        default: str = "preferred"; break;
    }
    tr_variantDictAddStr (d, TR_PREFS_KEY_ENCRYPTION, str);

    return NULL;
}

/***
****
***/

static const char*
sessionClose (tr_session               * session,
              tr_variant                  * args_in UNUSED,
              tr_variant                  * args_out UNUSED,
              struct tr_rpc_idle_data  * idle_data UNUSED)
{
    notify (session, TR_RPC_SESSION_CLOSE, NULL);
    return NULL;
}

/***
****
***/

typedef const char* (*handler)(tr_session*, tr_variant*, tr_variant*, struct tr_rpc_idle_data *);

static struct method
{
    const char *  name;
    bool          immediate;
    handler       func;
}
methods[] =
{
    { "port-test",             false, portTest            },
    { "blocklist-update",      false, blocklistUpdate     },
    { "session-close",         true,  sessionClose        },
    { "session-get",           true,  sessionGet          },
    { "session-set",           true,  sessionSet          },
    { "session-stats",         true,  sessionStats        },
    { "torrent-add",           false, torrentAdd          },
    { "torrent-get",           true,  torrentGet          },
    { "torrent-remove",        true,  torrentRemove       },
    { "torrent-set",           true,  torrentSet          },
    { "torrent-set-location",  true,  torrentSetLocation  },
    { "torrent-start",         true,  torrentStart        },
    { "torrent-start-now",     true,  torrentStartNow     },
    { "torrent-stop",          true,  torrentStop         },
    { "torrent-verify",        true,  torrentVerify       },
    { "torrent-reannounce",    true,  torrentReannounce   },
    { "queue-move-top",        true,  queueMoveTop        },
    { "queue-move-up",         true,  queueMoveUp         },
    { "queue-move-down",       true,  queueMoveDown       },
    { "queue-move-bottom",     true,  queueMoveBottom     }
};

static void
noop_response_callback (tr_session       * session UNUSED,
                        struct evbuffer  * response UNUSED,
                        void             * user_data UNUSED)
{
}

static void
request_exec (tr_session             * session,
              tr_variant                * request,
              tr_rpc_response_func     callback,
              void                   * callback_user_data)
{
    int i;
    const char * str;
    tr_variant * args_in = tr_variantDictFind (request, "arguments");
    const char * result = NULL;

    if (callback == NULL)
        callback = noop_response_callback;

    /* parse the request */
    if (!tr_variantDictFindStr (request, "method", &str, NULL))
        result = "no method name";
    else {
        const int n = TR_N_ELEMENTS (methods);
        for (i = 0; i < n; ++i)
            if (!strcmp (str, methods[i].name))
                break;
        if (i ==n)
            result = "method name not recognized";
    }

    /* if we couldn't figure out which method to use, return an error */
    if (result != NULL)
    {
        int64_t tag;
        tr_variant response;
        struct evbuffer * buf;

        tr_variantInitDict (&response, 3);
        tr_variantDictAddDict (&response, "arguments", 0);
        tr_variantDictAddStr (&response, "result", result);
        if (tr_variantDictFindInt (request, "tag", &tag))
            tr_variantDictAddInt (&response, "tag", tag);

        buf = tr_variantToBuf (&response, TR_VARIANT_FMT_JSON_LEAN);
      (*callback)(session, buf, callback_user_data);
        evbuffer_free (buf);

        tr_variantFree (&response);
    }
    else if (methods[i].immediate)
    {
        int64_t tag;
        tr_variant response;
        tr_variant * args_out;
        struct evbuffer * buf;

        tr_variantInitDict (&response, 3);
        args_out = tr_variantDictAddDict (&response, "arguments", 0);
        result = (*methods[i].func)(session, args_in, args_out, NULL);
        if (result == NULL)
            result = "success";
        tr_variantDictAddStr (&response, "result", result);
        if (tr_variantDictFindInt (request, "tag", &tag))
            tr_variantDictAddInt (&response, "tag", tag);

        buf = tr_variantToBuf (&response, TR_VARIANT_FMT_JSON_LEAN);
      (*callback)(session, buf, callback_user_data);
        evbuffer_free (buf);

        tr_variantFree (&response);
    }
    else
    {
        int64_t tag;
        struct tr_rpc_idle_data * data = tr_new0 (struct tr_rpc_idle_data, 1);
        data->session = session;
        data->response = tr_new0 (tr_variant, 1);
        tr_variantInitDict (data->response, 3);
        if (tr_variantDictFindInt (request, "tag", &tag))
            tr_variantDictAddInt (data->response, "tag", tag);
        data->args_out = tr_variantDictAddDict (data->response, "arguments", 0);
        data->callback = callback;
        data->callback_user_data = callback_user_data;
      (*methods[i].func)(session, args_in, data->args_out, data);
    }
}

void
tr_rpc_request_exec_json (tr_session            * session,
                          const void            * request_json,
                          int                     request_len,
                          tr_rpc_response_func    callback,
                          void                  * callback_user_data)
{
    tr_variant top;
    int have_content;

    if (request_len < 0)
        request_len = strlen (request_json);

    have_content = !tr_variantFromJson (&top, request_json, request_len);
    request_exec (session, have_content ? &top : NULL, callback, callback_user_data);

    if (have_content)
        tr_variantFree (&top);
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
void
tr_rpc_parse_list_str (tr_variant  * setme,
                       const char  * str,
                       int           len)

{
    int valueCount;
    int * values = tr_parseNumberRange (str, len, &valueCount);

    if (valueCount == 0)
        tr_variantInitStr (setme, str, len);
    else if (valueCount == 1)
        tr_variantInitInt (setme, values[0]);
    else {
        int i;
        tr_variantInitList (setme, valueCount);
        for (i=0; i<valueCount; ++i)
            tr_variantListAddInt (setme, values[i]);
    }

    tr_free (values);
}

void
tr_rpc_request_exec_uri (tr_session           * session,
                         const void           * request_uri,
                         int                    request_len,
                         tr_rpc_response_func   callback,
                         void                 * callback_user_data)
{
    tr_variant      top, * args;
    char *       request = tr_strndup (request_uri, request_len);
    const char * pch;

    tr_variantInitDict (&top, 3);
    args = tr_variantDictAddDict (&top, "arguments", 0);

    pch = strchr (request, '?');
    if (!pch) pch = request;
    while (pch)
    {
        const char * delim = strchr (pch, '=');
        const char * next = strchr (pch, '&');
        if (delim)
        {
            char *    key = tr_strndup (pch, delim - pch);
            int       isArg = strcmp (key, "method") && strcmp (key, "tag");
            tr_variant * parent = isArg ? args : &top;
            tr_rpc_parse_list_str (tr_variantDictAdd (parent, key, delim-pch),
                                  delim + 1,
                                  next ? (size_t)(
                                       next -
                                    (delim + 1)) : strlen (delim + 1));
            tr_free (key);
        }
        pch = next ? next + 1 : NULL;
    }

    request_exec (session, &top, callback, callback_user_data);

    /* cleanup */
    tr_variantFree (&top);
    tr_free (request);
}
