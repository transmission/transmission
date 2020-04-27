/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <stdlib.h> /* bsearch() */
#include <string.h> /* memcmp() */

#include "transmission.h"
#include "ptrarray.h"
#include "quark.h"
#include "tr-assert.h"
#include "utils.h" /* tr_memdup(), tr_strndup() */

struct tr_key_struct
{
    char const* str;
    size_t len;
};

#define Q(name) { "" name "", sizeof("" name "") - 1 }

static struct tr_key_struct const my_static[] =
{
    Q(""),
    Q("activeTorrentCount"),
    Q("activity-date"),
    Q("activityDate"),
    Q("added"),
    Q("added-date"),
    Q("added.f"),
    Q("added6"),
    Q("added6.f"),
    Q("addedDate"),
    Q("address"),
    Q("alt-speed-down"),
    Q("alt-speed-enabled"),
    Q("alt-speed-time-begin"),
    Q("alt-speed-time-day"),
    Q("alt-speed-time-enabled"),
    Q("alt-speed-time-end"),
    Q("alt-speed-up"),
    Q("announce"),
    Q("announce-list"),
    Q("announceState"),
    Q("arguments"),
    Q("bandwidth-priority"),
    Q("bandwidthPriority"),
    Q("bind-address-ipv4"),
    Q("bind-address-ipv6"),
    Q("bitfield"),
    Q("blocklist-date"),
    Q("blocklist-enabled"),
    Q("blocklist-size"),
    Q("blocklist-updates-enabled"),
    Q("blocklist-url"),
    Q("blocks"),
    Q("bytesCompleted"),
    Q("cache-size-mb"),
    Q("clientIsChoked"),
    Q("clientIsInterested"),
    Q("clientName"),
    Q("comment"),
    Q("comment_utf_8"),
    Q("compact-view"),
    Q("complete"),
    Q("config-dir"),
    Q("cookies"),
    Q("corrupt"),
    Q("corruptEver"),
    Q("created by"),
    Q("created by.utf-8"),
    Q("creation date"),
    Q("creator"),
    Q("cumulative-stats"),
    Q("current-stats"),
    Q("date"),
    Q("dateCreated"),
    Q("delete-local-data"),
    Q("desiredAvailable"),
    Q("destination"),
    Q("details-window-height"),
    Q("details-window-width"),
    Q("dht-enabled"),
    Q("display-name"),
    Q("dnd"),
    Q("done-date"),
    Q("doneDate"),
    Q("download-dir"),
    Q("download-dir-free-space"),
    Q("download-queue-enabled"),
    Q("download-queue-size"),
    Q("downloadCount"),
    Q("downloadDir"),
    Q("downloadLimit"),
    Q("downloadLimited"),
    Q("downloadSpeed"),
    Q("downloaded"),
    Q("downloaded-bytes"),
    Q("downloadedBytes"),
    Q("downloadedEver"),
    Q("downloaders"),
    Q("downloading-time-seconds"),
    Q("dropped"),
    Q("dropped6"),
    Q("e"),
    Q("editDate"),
    Q("encoding"),
    Q("encryption"),
    Q("error"),
    Q("errorString"),
    Q("eta"),
    Q("etaIdle"),
    Q("failure reason"),
    Q("fields"),
    Q("fileStats"),
    Q("filename"),
    Q("files"),
    Q("files-added"),
    Q("files-unwanted"),
    Q("files-wanted"),
    Q("filesAdded"),
    Q("filter-mode"),
    Q("filter-text"),
    Q("filter-trackers"),
    Q("flagStr"),
    Q("flags"),
    Q("format"),
    Q("fromCache"),
    Q("fromDht"),
    Q("fromIncoming"),
    Q("fromLpd"),
    Q("fromLtep"),
    Q("fromPex"),
    Q("fromTracker"),
    Q("hasAnnounced"),
    Q("hasScraped"),
    Q("hashString"),
    Q("have"),
    Q("haveUnchecked"),
    Q("haveValid"),
    Q("honorsSessionLimits"),
    Q("host"),
    Q("id"),
    Q("idle-limit"),
    Q("idle-mode"),
    Q("idle-seeding-limit"),
    Q("idle-seeding-limit-enabled"),
    Q("ids"),
    Q("incomplete"),
    Q("incomplete-dir"),
    Q("incomplete-dir-enabled"),
    Q("info"),
    Q("info_hash"),
    Q("inhibit-desktop-hibernation"),
    Q("interval"),
    Q("ip"),
    Q("ipv4"),
    Q("ipv6"),
    Q("isBackup"),
    Q("isDownloadingFrom"),
    Q("isEncrypted"),
    Q("isFinished"),
    Q("isIncoming"),
    Q("isPrivate"),
    Q("isStalled"),
    Q("isUTP"),
    Q("isUploadingTo"),
    Q("labels"),
    Q("lastAnnouncePeerCount"),
    Q("lastAnnounceResult"),
    Q("lastAnnounceStartTime"),
    Q("lastAnnounceSucceeded"),
    Q("lastAnnounceTime"),
    Q("lastAnnounceTimedOut"),
    Q("lastScrapeResult"),
    Q("lastScrapeStartTime"),
    Q("lastScrapeSucceeded"),
    Q("lastScrapeTime"),
    Q("lastScrapeTimedOut"),
    Q("leecherCount"),
    Q("leftUntilDone"),
    Q("length"),
    Q("location"),
    Q("lpd-enabled"),
    Q("m"),
    Q("magnet-info"),
    Q("magnetLink"),
    Q("main-window-height"),
    Q("main-window-is-maximized"),
    Q("main-window-layout-order"),
    Q("main-window-width"),
    Q("main-window-x"),
    Q("main-window-y"),
    Q("manualAnnounceTime"),
    Q("max-peers"),
    Q("maxConnectedPeers"),
    Q("memory-bytes"),
    Q("memory-units"),
    Q("message-level"),
    Q("metadataPercentComplete"),
    Q("metadata_size"),
    Q("metainfo"),
    Q("method"),
    Q("min interval"),
    Q("min_request_interval"),
    Q("move"),
    Q("msg_type"),
    Q("mtimes"),
    Q("name"),
    Q("name.utf-8"),
    Q("nextAnnounceTime"),
    Q("nextScrapeTime"),
    Q("nodes"),
    Q("nodes6"),
    Q("open-dialog-dir"),
    Q("p"),
    Q("path"),
    Q("path.utf-8"),
    Q("paused"),
    Q("pausedTorrentCount"),
    Q("peer-congestion-algorithm"),
    Q("peer-id-ttl-hours"),
    Q("peer-limit"),
    Q("peer-limit-global"),
    Q("peer-limit-per-torrent"),
    Q("peer-port"),
    Q("peer-port-random-high"),
    Q("peer-port-random-low"),
    Q("peer-port-random-on-start"),
    Q("peer-socket-tos"),
    Q("peerIsChoked"),
    Q("peerIsInterested"),
    Q("peers"),
    Q("peers2"),
    Q("peers2-6"),
    Q("peers6"),
    Q("peersConnected"),
    Q("peersFrom"),
    Q("peersGettingFromUs"),
    Q("peersSendingToUs"),
    Q("percentDone"),
    Q("pex-enabled"),
    Q("piece"),
    Q("piece length"),
    Q("pieceCount"),
    Q("pieceSize"),
    Q("pieces"),
    Q("play-download-complete-sound"),
    Q("port"),
    Q("port-forwarding-enabled"),
    Q("port-is-open"),
    Q("preallocation"),
    Q("prefetch-enabled"),
    Q("priorities"),
    Q("priority"),
    Q("priority-high"),
    Q("priority-low"),
    Q("priority-normal"),
    Q("private"),
    Q("progress"),
    Q("prompt-before-exit"),
    Q("queue-move-bottom"),
    Q("queue-move-down"),
    Q("queue-move-top"),
    Q("queue-move-up"),
    Q("queue-stalled-enabled"),
    Q("queue-stalled-minutes"),
    Q("queuePosition"),
    Q("rateDownload"),
    Q("rateToClient"),
    Q("rateToPeer"),
    Q("rateUpload"),
    Q("ratio-limit"),
    Q("ratio-limit-enabled"),
    Q("ratio-mode"),
    Q("recent-download-dir-1"),
    Q("recent-download-dir-2"),
    Q("recent-download-dir-3"),
    Q("recent-download-dir-4"),
    Q("recheckProgress"),
    Q("remote-session-enabled"),
    Q("remote-session-host"),
    Q("remote-session-password"),
    Q("remote-session-port"),
    Q("remote-session-requres-authentication"),
    Q("remote-session-username"),
    Q("removed"),
    Q("rename-partial-files"),
    Q("reqq"),
    Q("result"),
    Q("rpc-authentication-required"),
    Q("rpc-bind-address"),
    Q("rpc-enabled"),
    Q("rpc-host-whitelist"),
    Q("rpc-host-whitelist-enabled"),
    Q("rpc-password"),
    Q("rpc-port"),
    Q("rpc-url"),
    Q("rpc-username"),
    Q("rpc-version"),
    Q("rpc-version-minimum"),
    Q("rpc-whitelist"),
    Q("rpc-whitelist-enabled"),
    Q("scrape"),
    Q("scrape-paused-torrents-enabled"),
    Q("scrapeState"),
    Q("script-torrent-done-enabled"),
    Q("script-torrent-done-filename"),
    Q("seconds-active"),
    Q("secondsActive"),
    Q("secondsDownloading"),
    Q("secondsSeeding"),
    Q("seed-queue-enabled"),
    Q("seed-queue-size"),
    Q("seedIdleLimit"),
    Q("seedIdleMode"),
    Q("seedRatioLimit"),
    Q("seedRatioLimited"),
    Q("seedRatioMode"),
    Q("seederCount"),
    Q("seeding-time-seconds"),
    Q("session-count"),
    Q("session-id"),
    Q("sessionCount"),
    Q("show-backup-trackers"),
    Q("show-extra-peer-details"),
    Q("show-filterbar"),
    Q("show-notification-area-icon"),
    Q("show-options-window"),
    Q("show-statusbar"),
    Q("show-toolbar"),
    Q("show-tracker-scrapes"),
    Q("size-bytes"),
    Q("size-units"),
    Q("sizeWhenDone"),
    Q("sort-mode"),
    Q("sort-reversed"),
    Q("speed"),
    Q("speed-Bps"),
    Q("speed-bytes"),
    Q("speed-limit-down"),
    Q("speed-limit-down-enabled"),
    Q("speed-limit-up"),
    Q("speed-limit-up-enabled"),
    Q("speed-units"),
    Q("start-added-torrents"),
    Q("start-minimized"),
    Q("startDate"),
    Q("status"),
    Q("statusbar-stats"),
    Q("tag"),
    Q("tier"),
    Q("time-checked"),
    Q("torrent-added"),
    Q("torrent-added-notification-command"),
    Q("torrent-added-notification-enabled"),
    Q("torrent-complete-notification-command"),
    Q("torrent-complete-notification-enabled"),
    Q("torrent-complete-sound-command"),
    Q("torrent-complete-sound-enabled"),
    Q("torrent-duplicate"),
    Q("torrent-get"),
    Q("torrent-set"),
    Q("torrent-set-location"),
    Q("torrentCount"),
    Q("torrentFile"),
    Q("torrents"),
    Q("totalSize"),
    Q("total_size"),
    Q("tracker id"),
    Q("trackerAdd"),
    Q("trackerRemove"),
    Q("trackerReplace"),
    Q("trackerStats"),
    Q("trackers"),
    Q("trash-can-enabled"),
    Q("trash-original-torrent-files"),
    Q("umask"),
    Q("units"),
    Q("upload-slots-per-torrent"),
    Q("uploadLimit"),
    Q("uploadLimited"),
    Q("uploadRatio"),
    Q("uploadSpeed"),
    Q("upload_only"),
    Q("uploaded"),
    Q("uploaded-bytes"),
    Q("uploadedBytes"),
    Q("uploadedEver"),
    Q("url-list"),
    Q("use-global-speed-limit"),
    Q("use-speed-limit"),
    Q("user-has-given-informed-consent"),
    Q("ut_comment"),
    Q("ut_holepunch"),
    Q("ut_metadata"),
    Q("ut_pex"),
    Q("ut_recommend"),
    Q("utp-enabled"),
    Q("v"),
    Q("version"),
    Q("wanted"),
    Q("warning message"),
    Q("watch-dir"),
    Q("watch-dir-enabled"),
    Q("webseeds"),
    Q("webseedsSendingToUs")
};

#undef Q

static int compareKeys(void const* va, void const* vb)
{
    int ret;
    struct tr_key_struct const* a = va;
    struct tr_key_struct const* b = vb;

    ret = memcmp(a->str, b->str, MIN(a->len, b->len));

    if (ret == 0 && a->len != b->len)
    {
        ret = a->len < b->len ? -1 : 1;
    }

    return ret;
}

static tr_ptrArray my_runtime = TR_PTR_ARRAY_INIT_STATIC;

bool tr_quark_lookup(void const* str, size_t len, tr_quark* setme)
{
    static size_t const n_static = TR_N_ELEMENTS(my_static);

    TR_ASSERT(n_static == TR_N_KEYS);

    struct tr_key_struct tmp;
    struct tr_key_struct* match;
    bool success = false;

    tmp.str = str;
    tmp.len = len;

    /* is it in our static array? */
    match = bsearch(&tmp, my_static, n_static, sizeof(struct tr_key_struct), compareKeys);

    if (match != NULL)
    {
        *setme = match - my_static;
        success = true;
    }

    /* was it added during runtime? */
    if (!success && !tr_ptrArrayEmpty(&my_runtime))
    {
        struct tr_key_struct** runtime = (struct tr_key_struct**)tr_ptrArrayBase(&my_runtime);
        size_t const n_runtime = tr_ptrArraySize(&my_runtime);

        for (size_t i = 0; i < n_runtime; ++i)
        {
            if (compareKeys(&tmp, runtime[i]) == 0)
            {
                *setme = TR_N_KEYS + i;
                success = true;
                break;
            }
        }
    }

    return success;
}

static tr_quark append_new_quark(void const* str, size_t len)
{
    tr_quark ret;
    struct tr_key_struct* tmp;
    tmp = tr_new(struct tr_key_struct, 1);
    tmp->str = tr_strndup(str, len);
    tmp->len = len;
    ret = TR_N_KEYS + tr_ptrArraySize(&my_runtime);
    tr_ptrArrayAppend(&my_runtime, tmp);
    return ret;
}

tr_quark tr_quark_new(void const* str, size_t len)
{
    tr_quark ret = TR_KEY_NONE;

    if (str == NULL)
    {
        goto finish;
    }

    if (len == TR_BAD_SIZE)
    {
        len = strlen(str);
    }

    if (!tr_quark_lookup(str, len, &ret))
    {
        ret = append_new_quark(str, len);
    }

finish:
    return ret;
}

char const* tr_quark_get_string(tr_quark q, size_t* len)
{
    struct tr_key_struct const* tmp;

    if (q < TR_N_KEYS)
    {
        tmp = &my_static[q];
    }
    else
    {
        tmp = tr_ptrArrayNth(&my_runtime, q - TR_N_KEYS);
    }

    if (len != NULL)
    {
        *len = tmp->len;
    }

    return tmp->str;
}
