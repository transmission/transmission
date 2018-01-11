/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <assert.h>
#include <stdlib.h> /* bsearch() */
#include <string.h> /* memcmp() */

#include "transmission.h"
#include "ptrarray.h"
#include "quark.h"
#include "utils.h" /* tr_memdup(), tr_strndup() */

struct tr_key_struct
{
  const char * str;
  size_t len;
};

static const struct tr_key_struct my_static[] =
{
  { "", 0 },
  { "activeTorrentCount", 18 },
  { "activity-date", 13 },
  { "activityDate", 12 },
  { "added", 5 },
  { "added-date", 10 },
  { "added.f", 7 },
  { "added6", 6 },
  { "added6.f", 8 },
  { "addedDate", 9 },
  { "address", 7 },
  { "alt-speed-down", 14 },
  { "alt-speed-enabled", 17 },
  { "alt-speed-time-begin", 20 },
  { "alt-speed-time-day", 18 },
  { "alt-speed-time-enabled", 22 },
  { "alt-speed-time-end", 18 },
  { "alt-speed-up", 12 },
  { "announce", 8 },
  { "announce-list", 13 },
  { "announceState", 13 },
  { "arguments", 9 },
  { "bandwidth-priority", 18 },
  { "bandwidthPriority", 17 },
  { "bind-address-ipv4", 17 },
  { "bind-address-ipv6", 17 },
  { "bitfield",  8 },
  { "blocklist-date", 14 },
  { "blocklist-enabled", 17 },
  { "blocklist-size", 14 },
  { "blocklist-updates-enabled", 25 },
  { "blocklist-url", 13 },
  { "blocks", 6 },
  { "bytesCompleted", 14 },
  { "cache-size-mb", 13 },
  { "clientIsChoked", 14 },
  { "clientIsInterested", 18 },
  { "clientName", 10 },
  { "comment", 7 },
  { "comment_utf_8", 13 },
  { "compact-view", 12 },
  { "complete", 8 },
  { "config-dir", 10 },
  { "cookies", 7 },
  { "corrupt", 7 },
  { "corruptEver", 11 },
  { "created by", 10 },
  { "created by.utf-8", 16 },
  { "creation date", 13 },
  { "creator", 7 },
  { "cumulative-stats", 16 },
  { "current-stats", 13 },
  { "date", 4 },
  { "dateCreated", 11 },
  { "delete-local-data", 17 },
  { "desiredAvailable", 16 },
  { "destination", 11 },
  { "dht-enabled", 11 },
  { "display-name", 12 },
  { "dnd", 3 },
  { "done-date", 9 },
  { "doneDate", 8 },
  { "download-dir", 12 },
  { "download-dir-free-space", 23 },
  { "download-queue-enabled", 22 },
  { "download-queue-size", 19 },
  { "downloadCount", 13 },
  { "downloadDir", 11 },
  { "downloadLimit", 13 },
  { "downloadLimited", 15 },
  { "downloadSpeed", 13 },
  { "downloaded", 10 },
  { "downloaded-bytes", 16 },
  { "downloadedBytes", 15 },
  { "downloadedEver", 14 },
  { "downloaders", 11 },
  { "downloading-time-seconds", 24 },
  { "dropped", 7 },
  { "dropped6", 8 },
  { "e", 1 },
  { "encoding", 8 },
  { "encryption", 10 },
  { "error", 5 },
  { "errorString", 11 },
  { "eta", 3 },
  { "etaIdle", 7 },
  { "failure reason", 14 },
  { "fields", 6 },
  { "fileStats", 9 },
  { "filename", 8 },
  { "files", 5 },
  { "files-added", 11 },
  { "files-unwanted", 14 },
  { "files-wanted", 12 },
  { "filesAdded", 10 },
  { "filter-mode", 11 },
  { "filter-text", 11 },
  { "filter-trackers", 15 },
  { "flagStr", 7 },
  { "flags", 5 },
  { "fromCache", 9 },
  { "fromDht", 7 },
  { "fromIncoming", 12 },
  { "fromLpd", 7 },
  { "fromLtep", 8 },
  { "fromPex", 7 },
  { "fromTracker", 11 },
  { "hasAnnounced", 12 },
  { "hasScraped", 10 },
  { "hashString", 10 },
  { "have", 4 },
  { "haveUnchecked", 13 },
  { "haveValid", 9 },
  { "honorsSessionLimits", 19 },
  { "host", 4 },
  { "id", 2 },
  { "idle-limit", 10 },
  { "idle-mode", 9 },
  { "idle-seeding-limit", 18 },
  { "idle-seeding-limit-enabled", 26 },
  { "ids", 3 },
  { "incomplete", 10 },
  { "incomplete-dir", 14 },
  { "incomplete-dir-enabled", 22 },
  { "info", 4 },
  { "info_hash", 9 },
  { "inhibit-desktop-hibernation", 27 },
  { "interval", 8 },
  { "ip", 2 },
  { "ipv4", 4 },
  { "ipv6", 4 },
  { "isBackup", 8 },
  { "isDownloadingFrom", 17 },
  { "isEncrypted", 11 },
  { "isFinished", 10 },
  { "isIncoming", 10 },
  { "isPrivate", 9 },
  { "isStalled", 9 },
  { "isUTP", 5 },
  { "isUploadingTo", 13 },
  { "lastAnnouncePeerCount", 21 },
  { "lastAnnounceResult", 18 },
  { "lastAnnounceStartTime", 21 },
  { "lastAnnounceSucceeded", 21 },
  { "lastAnnounceTime", 16 },
  { "lastAnnounceTimedOut", 20 },
  { "lastScrapeResult", 16 },
  { "lastScrapeStartTime", 19 },
  { "lastScrapeSucceeded", 19 },
  { "lastScrapeTime", 14 },
  { "lastScrapeTimedOut", 18 },
  { "leecherCount", 12 },
  { "leftUntilDone", 13 },
  { "length", 6 },
  { "location", 8 },
  { "lpd-enabled", 11 },
  { "m", 1 },
  { "magnet-info", 11 },
  { "magnetLink", 10 },
  { "main-window-height", 18 },
  { "main-window-is-maximized", 24 },
  { "main-window-layout-order", 24 },
  { "main-window-width", 17 },
  { "main-window-x", 13 },
  { "main-window-y", 13 },
  { "manualAnnounceTime", 18 },
  { "max-peers", 9 },
  { "maxConnectedPeers", 17 },
  { "memory-bytes", 12 },
  { "memory-units", 12 },
  { "message-level", 13 },
  { "metadataPercentComplete", 23 },
  { "metadata_size", 13 },
  { "metainfo", 8 },
  { "method", 6 },
  { "min interval", 12 },
  { "min_request_interval", 20 },
  { "move", 4 },
  { "msg_type", 8 },
  { "mtimes", 6 },
  { "name", 4 },
  { "name.utf-8", 10 },
  { "nextAnnounceTime", 16 },
  { "nextScrapeTime", 14 },
  { "nodes", 5 },
  { "nodes6", 6 },
  { "open-dialog-dir", 15 },
  { "p", 1 },
  { "path", 4 },
  { "path.utf-8", 10 },
  { "paused", 6 },
  { "pausedTorrentCount", 18 },
  { "peer-congestion-algorithm", 25 },
  { "peer-id-ttl-hours", 17 },
  { "peer-limit", 10 },
  { "peer-limit-global", 17 },
  { "peer-limit-per-torrent", 22 },
  { "peer-port", 9 },
  { "peer-port-random-high", 21 },
  { "peer-port-random-low", 20 },
  { "peer-port-random-on-start", 25 },
  { "peer-socket-tos", 15 },
  { "peerIsChoked", 12 },
  { "peerIsInterested", 16 },
  { "peers", 5 },
  { "peers2", 6 },
  { "peers2-6", 8 },
  { "peers6", 6 },
  { "peersConnected", 14 },
  { "peersFrom", 9 },
  { "peersGettingFromUs", 18 },
  { "peersSendingToUs", 16 },
  { "percentDone", 11 },
  { "pex-enabled", 11 },
  { "piece", 5 },
  { "piece length", 12 },
  { "pieceCount", 10 },
  { "pieceSize", 9 },
  { "pieces", 6 },
  { "play-download-complete-sound", 28 },
  { "port", 4 },
  { "port-forwarding-enabled", 23 },
  { "port-is-open", 12 },
  { "preallocation", 13 },
  { "prefetch-enabled", 16 },
  { "priorities", 10 },
  { "priority", 8 },
  { "priority-high", 13 },
  { "priority-low", 12 },
  { "priority-normal", 15 },
  { "private", 7 },
  { "progress", 8 },
  { "prompt-before-exit", 18 },
  { "queue-move-bottom", 17 },
  { "queue-move-down", 15 },
  { "queue-move-top", 14 },
  { "queue-move-up", 13 },
  { "queue-stalled-enabled", 21 },
  { "queue-stalled-minutes", 21 },
  { "queuePosition", 13 },
  { "rateDownload", 12 },
  { "rateToClient", 12 },
  { "rateToPeer", 10 },
  { "rateUpload", 10 },
  { "ratio-limit", 11 },
  { "ratio-limit-enabled", 19 },
  { "ratio-mode", 10 },
  { "recent-download-dir-1", 21 },
  { "recent-download-dir-2", 21 },
  { "recent-download-dir-3", 21 },
  { "recent-download-dir-4", 21 },
  { "recheckProgress", 15 },
  { "remote-session-enabled", 22 },
  { "remote-session-host", 19 },
  { "remote-session-password", 23 },
  { "remote-session-port", 19 },
  { "remote-session-requres-authentication", 37 },
  { "remote-session-username", 23 },
  { "removed", 7 },
  { "rename-partial-files", 20 },
  { "reqq", 4 },
  { "result", 6 },
  { "rpc-authentication-required", 27 },
  { "rpc-bind-address", 16 },
  { "rpc-enabled", 11 },
  { "rpc-host-whitelist", 18 },
  { "rpc-host-whitelist-enabled", 26 },
  { "rpc-password", 12 },
  { "rpc-port", 8 },
  { "rpc-url", 7 },
  { "rpc-username", 12 },
  { "rpc-version", 11 },
  { "rpc-version-minimum", 19 },
  { "rpc-whitelist", 13 },
  { "rpc-whitelist-enabled", 21 },
  { "scrape", 6 },
  { "scrape-paused-torrents-enabled", 30 },
  { "scrapeState", 11 },
  { "script-torrent-done-enabled", 27 },
  { "script-torrent-done-filename", 28 },
  { "seconds-active", 14 },
  { "secondsActive", 13 },
  { "secondsDownloading", 18 },
  { "secondsSeeding", 14 },
  { "seed-queue-enabled", 18 },
  { "seed-queue-size", 15 },
  { "seedIdleLimit", 13 },
  { "seedIdleMode", 12 },
  { "seedRatioLimit", 14 },
  { "seedRatioLimited", 16 },
  { "seedRatioMode", 13 },
  { "seederCount", 11 },
  { "seeding-time-seconds", 20 },
  { "session-count", 13 },
  { "sessionCount", 12 },
  { "show-backup-trackers", 20 },
  { "show-extra-peer-details", 23 },
  { "show-filterbar", 14 },
  { "show-notification-area-icon", 27 },
  { "show-options-window", 19 },
  { "show-statusbar", 14 },
  { "show-toolbar", 12 },
  { "show-tracker-scrapes", 20 },
  { "size-bytes", 10 },
  { "size-units", 10 },
  { "sizeWhenDone", 12 },
  { "sort-mode", 9 },
  { "sort-reversed", 13 },
  { "speed", 5 },
  { "speed-Bps", 9 },
  { "speed-bytes", 11 },
  { "speed-limit-down", 16 },
  { "speed-limit-down-enabled", 24 },
  { "speed-limit-up", 14 },
  { "speed-limit-up-enabled", 22 },
  { "speed-units", 11 },
  { "start-added-torrents", 20 },
  { "start-minimized", 15 },
  { "startDate", 9 },
  { "status", 6 },
  { "statusbar-stats", 15 },
  { "tag", 3 },
  { "tier", 4 },
  { "time-checked", 12 },
  { "torrent-added", 13 },
  { "torrent-added-notification-command", 34 },
  { "torrent-added-notification-enabled", 34 },
  { "torrent-complete-notification-command", 37 },
  { "torrent-complete-notification-enabled", 37 },
  { "torrent-complete-sound-command", 30 },
  { "torrent-complete-sound-enabled", 30 },
  { "torrent-duplicate", 17 },
  { "torrent-get", 11 },
  { "torrent-set", 11 },
  { "torrent-set-location", 20 },
  { "torrentCount", 12 },
  { "torrentFile", 11 },
  { "torrents", 8 },
  { "totalSize", 9 },
  { "total_size", 10 },
  { "tracker id", 10 },
  { "trackerAdd", 10 },
  { "trackerRemove", 13 },
  { "trackerReplace", 14 },
  { "trackerStats", 12 },
  { "trackers", 8 },
  { "trash-can-enabled", 17 },
  { "trash-original-torrent-files", 28 },
  { "umask", 5 },
  { "units", 5 },
  { "upload-slots-per-torrent", 24 },
  { "uploadLimit", 11 },
  { "uploadLimited", 13 },
  { "uploadRatio", 11 },
  { "uploadSpeed", 11 },
  { "upload_only", 11 },
  { "uploaded", 8 },
  { "uploaded-bytes", 14 },
  { "uploadedBytes", 13 },
  { "uploadedEver", 12 },
  { "url-list", 8 },
  { "use-global-speed-limit", 22 },
  { "use-speed-limit", 15 },
  { "user-has-given-informed-consent", 31 },
  { "ut_comment", 10 },
  { "ut_holepunch", 12 },
  { "ut_metadata", 11 },
  { "ut_pex", 6 },
  { "ut_recommend", 12 },
  { "utp-enabled", 11 },
  { "v", 1 },
  { "version", 7 },
  { "wanted", 6 },
  { "warning message", 15 },
  { "watch-dir", 9 },
  { "watch-dir-enabled", 17 },
  { "webseeds", 8 },
  { "webseedsSendingToUs", 19 }
};

static int
compareKeys (const void * va, const void * vb)
{
  int ret;
  const struct tr_key_struct * a = va;
  const struct tr_key_struct * b = vb;

  ret = memcmp (a->str, b->str, MIN (a->len, b->len));

  if (!ret && (a->len != b->len))
    ret = a->len < b->len ? -1 : 1;

  return ret;
}

static tr_ptrArray my_runtime = TR_PTR_ARRAY_INIT_STATIC;

bool
tr_quark_lookup (const void * str, size_t len, tr_quark * setme)
{
  struct tr_key_struct tmp;
  struct tr_key_struct * match;
  static const size_t n_static = sizeof(my_static) / sizeof(struct tr_key_struct);
  bool success = false;

  assert (n_static == TR_N_KEYS);

  tmp.str = str;
  tmp.len = len;

  /* is it in our static array? */
  match = bsearch (&tmp, my_static, n_static, sizeof(struct tr_key_struct), compareKeys);
  if (match != NULL)
    {
      *setme = match - my_static;
      success = true;
    }

  /* was it added during runtime? */
  if (!success && !tr_ptrArrayEmpty(&my_runtime))
    {
      size_t i;
      struct tr_key_struct ** runtime = (struct tr_key_struct **) tr_ptrArrayBase (&my_runtime);
      const size_t n_runtime = tr_ptrArraySize (&my_runtime);
      for (i=0; i<n_runtime; ++i)
        {
          if (compareKeys (&tmp, runtime[i]) == 0)
            {
              *setme = TR_N_KEYS + i;
              success = true;
              break;
            }
        }
    }

  return success;
}

static tr_quark
append_new_quark (const void * str, size_t len)
{
  tr_quark ret;
  struct tr_key_struct * tmp;
  tmp = tr_new (struct tr_key_struct, 1);
  tmp->str = tr_strndup (str, len);
  tmp->len = len;
  ret = TR_N_KEYS + tr_ptrArraySize (&my_runtime);
  tr_ptrArrayAppend (&my_runtime, tmp);
  return ret;
}

tr_quark
tr_quark_new (const void * str, size_t len)
{
  tr_quark ret = TR_KEY_NONE;

  if (str == NULL)
    len = 0;
  else if (len == TR_BAD_SIZE)
    len = strlen (str);

  if (!tr_quark_lookup (str, len, &ret))
    ret = append_new_quark (str, len);

  return ret;
}

const char *
tr_quark_get_string (tr_quark q, size_t * len)
{
  const struct tr_key_struct * tmp;

  if (q < TR_N_KEYS)
    tmp = &my_static[q];
  else
    tmp = tr_ptrArrayNth (&my_runtime, q-TR_N_KEYS);

  if (len != NULL)
    *len = tmp->len;

  return tmp->str;
}
