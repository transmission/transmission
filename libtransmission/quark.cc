/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <algorithm>
#include <array>
#include <cstring> // strlen()
#include <iterator>
#include <string_view>
#include <vector>

#include "transmission.h"
#include "quark.h"
#include "tr-assert.h"
#include "utils.h" // tr_strndup()

namespace
{

auto constexpr my_static = std::array<std::string_view, 389>{ "",
                                                              "activeTorrentCount",
                                                              "activity-date",
                                                              "activityDate",
                                                              "added",
                                                              "added-date",
                                                              "added.f",
                                                              "added6",
                                                              "added6.f",
                                                              "addedDate",
                                                              "address",
                                                              "alt-speed-down",
                                                              "alt-speed-enabled",
                                                              "alt-speed-time-begin",
                                                              "alt-speed-time-day",
                                                              "alt-speed-time-enabled",
                                                              "alt-speed-time-end",
                                                              "alt-speed-up",
                                                              "announce",
                                                              "announce-list",
                                                              "announceState",
                                                              "anti-brute-force-enabled",
                                                              "anti-brute-force-threshold",
                                                              "arguments",
                                                              "bandwidth-priority",
                                                              "bandwidthPriority",
                                                              "bind-address-ipv4",
                                                              "bind-address-ipv6",
                                                              "bitfield",
                                                              "blocklist-date",
                                                              "blocklist-enabled",
                                                              "blocklist-size",
                                                              "blocklist-updates-enabled",
                                                              "blocklist-url",
                                                              "blocks",
                                                              "bytesCompleted",
                                                              "cache-size-mb",
                                                              "clientIsChoked",
                                                              "clientIsInterested",
                                                              "clientName",
                                                              "comment",
                                                              "comment_utf_8",
                                                              "compact-view",
                                                              "complete",
                                                              "config-dir",
                                                              "cookies",
                                                              "corrupt",
                                                              "corruptEver",
                                                              "created by",
                                                              "created by.utf-8",
                                                              "creation date",
                                                              "creator",
                                                              "cumulative-stats",
                                                              "current-stats",
                                                              "date",
                                                              "dateCreated",
                                                              "delete-local-data",
                                                              "desiredAvailable",
                                                              "destination",
                                                              "details-window-height",
                                                              "details-window-width",
                                                              "dht-enabled",
                                                              "display-name",
                                                              "dnd",
                                                              "done-date",
                                                              "doneDate",
                                                              "download-dir",
                                                              "download-dir-free-space",
                                                              "download-queue-enabled",
                                                              "download-queue-size",
                                                              "downloadCount",
                                                              "downloadDir",
                                                              "downloadLimit",
                                                              "downloadLimited",
                                                              "downloadSpeed",
                                                              "downloaded",
                                                              "downloaded-bytes",
                                                              "downloadedBytes",
                                                              "downloadedEver",
                                                              "downloaders",
                                                              "downloading-time-seconds",
                                                              "dropped",
                                                              "dropped6",
                                                              "e",
                                                              "editDate",
                                                              "encoding",
                                                              "encryption",
                                                              "error",
                                                              "errorString",
                                                              "eta",
                                                              "etaIdle",
                                                              "failure reason",
                                                              "fields",
                                                              "file-count",
                                                              "fileStats",
                                                              "filename",
                                                              "files",
                                                              "files-added",
                                                              "files-unwanted",
                                                              "files-wanted",
                                                              "filesAdded",
                                                              "filter-mode",
                                                              "filter-text",
                                                              "filter-trackers",
                                                              "flagStr",
                                                              "flags",
                                                              "format",
                                                              "fromCache",
                                                              "fromDht",
                                                              "fromIncoming",
                                                              "fromLpd",
                                                              "fromLtep",
                                                              "fromPex",
                                                              "fromTracker",
                                                              "hasAnnounced",
                                                              "hasScraped",
                                                              "hashString",
                                                              "have",
                                                              "haveUnchecked",
                                                              "haveValid",
                                                              "honorsSessionLimits",
                                                              "host",
                                                              "id",
                                                              "idle-limit",
                                                              "idle-mode",
                                                              "idle-seeding-limit",
                                                              "idle-seeding-limit-enabled",
                                                              "ids",
                                                              "incomplete",
                                                              "incomplete-dir",
                                                              "incomplete-dir-enabled",
                                                              "info",
                                                              "info_hash",
                                                              "inhibit-desktop-hibernation",
                                                              "interval",
                                                              "ip",
                                                              "ipv4",
                                                              "ipv6",
                                                              "isBackup",
                                                              "isDownloadingFrom",
                                                              "isEncrypted",
                                                              "isFinished",
                                                              "isIncoming",
                                                              "isPrivate",
                                                              "isStalled",
                                                              "isUTP",
                                                              "isUploadingTo",
                                                              "labels",
                                                              "lastAnnouncePeerCount",
                                                              "lastAnnounceResult",
                                                              "lastAnnounceStartTime",
                                                              "lastAnnounceSucceeded",
                                                              "lastAnnounceTime",
                                                              "lastAnnounceTimedOut",
                                                              "lastScrapeResult",
                                                              "lastScrapeStartTime",
                                                              "lastScrapeSucceeded",
                                                              "lastScrapeTime",
                                                              "lastScrapeTimedOut",
                                                              "leecherCount",
                                                              "leftUntilDone",
                                                              "length",
                                                              "location",
                                                              "lpd-enabled",
                                                              "m",
                                                              "magnet-info",
                                                              "magnetLink",
                                                              "main-window-height",
                                                              "main-window-is-maximized",
                                                              "main-window-layout-order",
                                                              "main-window-width",
                                                              "main-window-x",
                                                              "main-window-y",
                                                              "manualAnnounceTime",
                                                              "max-peers",
                                                              "maxConnectedPeers",
                                                              "memory-bytes",
                                                              "memory-units",
                                                              "message-level",
                                                              "metadataPercentComplete",
                                                              "metadata_size",
                                                              "metainfo",
                                                              "method",
                                                              "min interval",
                                                              "min_request_interval",
                                                              "move",
                                                              "msg_type",
                                                              "mtimes",
                                                              "name",
                                                              "name.utf-8",
                                                              "nextAnnounceTime",
                                                              "nextScrapeTime",
                                                              "nodes",
                                                              "nodes6",
                                                              "open-dialog-dir",
                                                              "p",
                                                              "path",
                                                              "path.utf-8",
                                                              "paused",
                                                              "pausedTorrentCount",
                                                              "peer-congestion-algorithm",
                                                              "peer-id-ttl-hours",
                                                              "peer-limit",
                                                              "peer-limit-global",
                                                              "peer-limit-per-torrent",
                                                              "peer-port",
                                                              "peer-port-random-high",
                                                              "peer-port-random-low",
                                                              "peer-port-random-on-start",
                                                              "peer-socket-tos",
                                                              "peerIsChoked",
                                                              "peerIsInterested",
                                                              "peers",
                                                              "peers2",
                                                              "peers2-6",
                                                              "peers6",
                                                              "peersConnected",
                                                              "peersFrom",
                                                              "peersGettingFromUs",
                                                              "peersSendingToUs",
                                                              "percentDone",
                                                              "pex-enabled",
                                                              "piece",
                                                              "piece length",
                                                              "pieceCount",
                                                              "pieceSize",
                                                              "pieces",
                                                              "play-download-complete-sound",
                                                              "port",
                                                              "port-forwarding-enabled",
                                                              "port-is-open",
                                                              "preallocation",
                                                              "prefetch-enabled",
                                                              "primary-mime-type",
                                                              "priorities",
                                                              "priority",
                                                              "priority-high",
                                                              "priority-low",
                                                              "priority-normal",
                                                              "private",
                                                              "progress",
                                                              "prompt-before-exit",
                                                              "queue-move-bottom",
                                                              "queue-move-down",
                                                              "queue-move-top",
                                                              "queue-move-up",
                                                              "queue-stalled-enabled",
                                                              "queue-stalled-minutes",
                                                              "queuePosition",
                                                              "rateDownload",
                                                              "rateToClient",
                                                              "rateToPeer",
                                                              "rateUpload",
                                                              "ratio-limit",
                                                              "ratio-limit-enabled",
                                                              "ratio-mode",
                                                              "recent-download-dir-1",
                                                              "recent-download-dir-2",
                                                              "recent-download-dir-3",
                                                              "recent-download-dir-4",
                                                              "recheckProgress",
                                                              "remote-session-enabled",
                                                              "remote-session-host",
                                                              "remote-session-password",
                                                              "remote-session-port",
                                                              "remote-session-requres-authentication",
                                                              "remote-session-username",
                                                              "removed",
                                                              "rename-partial-files",
                                                              "reqq",
                                                              "result",
                                                              "rpc-authentication-required",
                                                              "rpc-bind-address",
                                                              "rpc-enabled",
                                                              "rpc-host-whitelist",
                                                              "rpc-host-whitelist-enabled",
                                                              "rpc-password",
                                                              "rpc-port",
                                                              "rpc-url",
                                                              "rpc-username",
                                                              "rpc-version",
                                                              "rpc-version-minimum",
                                                              "rpc-whitelist",
                                                              "rpc-whitelist-enabled",
                                                              "scrape",
                                                              "scrape-paused-torrents-enabled",
                                                              "scrapeState",
                                                              "script-torrent-done-enabled",
                                                              "script-torrent-done-filename",
                                                              "seconds-active",
                                                              "secondsActive",
                                                              "secondsDownloading",
                                                              "secondsSeeding",
                                                              "seed-queue-enabled",
                                                              "seed-queue-size",
                                                              "seedIdleLimit",
                                                              "seedIdleMode",
                                                              "seedRatioLimit",
                                                              "seedRatioLimited",
                                                              "seedRatioMode",
                                                              "seederCount",
                                                              "seeding-time-seconds",
                                                              "session-count",
                                                              "session-id",
                                                              "sessionCount",
                                                              "show-backup-trackers",
                                                              "show-extra-peer-details",
                                                              "show-filterbar",
                                                              "show-notification-area-icon",
                                                              "show-options-window",
                                                              "show-statusbar",
                                                              "show-toolbar",
                                                              "show-tracker-scrapes",
                                                              "size-bytes",
                                                              "size-units",
                                                              "sizeWhenDone",
                                                              "sort-mode",
                                                              "sort-reversed",
                                                              "speed",
                                                              "speed-Bps",
                                                              "speed-bytes",
                                                              "speed-limit-down",
                                                              "speed-limit-down-enabled",
                                                              "speed-limit-up",
                                                              "speed-limit-up-enabled",
                                                              "speed-units",
                                                              "start-added-torrents",
                                                              "start-minimized",
                                                              "startDate",
                                                              "status",
                                                              "statusbar-stats",
                                                              "tag",
                                                              "tier",
                                                              "time-checked",
                                                              "torrent-added",
                                                              "torrent-added-notification-command",
                                                              "torrent-added-notification-enabled",
                                                              "torrent-complete-notification-command",
                                                              "torrent-complete-notification-enabled",
                                                              "torrent-complete-sound-command",
                                                              "torrent-complete-sound-enabled",
                                                              "torrent-duplicate",
                                                              "torrent-get",
                                                              "torrent-set",
                                                              "torrent-set-location",
                                                              "torrentCount",
                                                              "torrentFile",
                                                              "torrents",
                                                              "totalSize",
                                                              "total_size",
                                                              "tracker id",
                                                              "trackerAdd",
                                                              "trackerRemove",
                                                              "trackerReplace",
                                                              "trackerStats",
                                                              "trackers",
                                                              "trash-can-enabled",
                                                              "trash-original-torrent-files",
                                                              "umask",
                                                              "units",
                                                              "upload-slots-per-torrent",
                                                              "uploadLimit",
                                                              "uploadLimited",
                                                              "uploadRatio",
                                                              "uploadSpeed",
                                                              "upload_only",
                                                              "uploaded",
                                                              "uploaded-bytes",
                                                              "uploadedBytes",
                                                              "uploadedEver",
                                                              "url-list",
                                                              "use-global-speed-limit",
                                                              "use-speed-limit",
                                                              "user-has-given-informed-consent",
                                                              "ut_comment",
                                                              "ut_holepunch",
                                                              "ut_metadata",
                                                              "ut_pex",
                                                              "ut_recommend",
                                                              "utp-enabled",
                                                              "v",
                                                              "verify-threads",
                                                              "version",
                                                              "wanted",
                                                              "warning message",
                                                              "watch-dir",
                                                              "watch-dir-enabled",
                                                              "webseeds",
                                                              "webseedsSendingToUs" };

size_t constexpr quarks_are_sorted = ( //
    []() constexpr
    {
        for (size_t i = 1; i < std::size(my_static); ++i)
        {
            if (my_static[i - 1] >= my_static[i])
            {
                return false;
            }
        }

        return true;
    })();

static_assert(quarks_are_sorted, "Predefined quarks must be sorted by their string value");

auto& my_runtime{ *new std::vector<std::string_view>{} };

} // namespace

bool tr_quark_lookup(void const* str, size_t len, tr_quark* setme)
{
    auto constexpr n_static = std::size(my_static);
    static_assert(n_static == TR_N_KEYS);

    /* is it in our static array? */
    auto const key = std::string_view{ static_cast<char const*>(str), len };
    auto constexpr sbegin = std::begin(my_static), send = std::end(my_static);
    auto const sit = std::lower_bound(sbegin, send, key);
    if (sit != send && *sit == key)
    {
        *setme = std::distance(sbegin, sit);
        return true;
    }

    /* was it added during runtime? */
    auto const rbegin = std::begin(my_runtime), rend = std::end(my_runtime);
    auto const rit = std::find(rbegin, rend, key);
    if (rit != rend)
    {
        *setme = TR_N_KEYS + std::distance(rbegin, rit);
        return true;
    }

    return false;
}

tr_quark tr_quark_new(void const* str, size_t len)
{
    tr_quark ret = TR_KEY_NONE;

    if (str != nullptr)
    {
        if (len == TR_BAD_SIZE)
        {
            len = strlen(static_cast<char const*>(str));
        }

        if (!tr_quark_lookup(str, len, &ret))
        {
            ret = TR_N_KEYS + std::size(my_runtime);
            my_runtime.emplace_back(tr_strndup(str, len), len);
        }
    }

    return ret;
}

char const* tr_quark_get_string(tr_quark q, size_t* len)
{
    auto const& tmp = q < TR_N_KEYS ? my_static[q] : my_runtime[q - TR_N_KEYS];

    if (len != nullptr)
    {
        *len = std::size(tmp);
    }

    return std::data(tmp);
}
