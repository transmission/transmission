// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator> // for std::distance()
#include <optional>
#include <string_view>
#include <vector>

#include "libtransmission/quark.h"
#include "libtransmission/variant.h"

using namespace std::literals;

namespace
{

auto constexpr MyStatic = std::array<std::string_view, TR_N_KEYS>{
    ""sv,
    "activeTorrentCount"sv, // rpc
    "active_torrent_count"sv, // rpc
    "activity-date"sv, // .resume
    "activityDate"sv, // rpc
    "activity_date"sv, // .resume, rpc
    "added"sv, // BEP0011; BT protocol, rpc
    "added-date"sv, // .resume
    "added.f"sv, // BEP0011; BT protocol
    "added6"sv, // BEP0011; BT protocol
    "added6.f"sv, // BEP0011; BT protocol
    "addedDate"sv, // rpc
    "added_date"sv, // .resume, rpc
    "address"sv, // rpc
    "alt-speed-down"sv, // gtk app, rpc, speed settings
    "alt-speed-enabled"sv, // gtk app, rpc, speed settings
    "alt-speed-time-begin"sv, // rpc, speed settings
    "alt-speed-time-day"sv, // rpc, speed settings
    "alt-speed-time-enabled"sv, // rpc, speed settings
    "alt-speed-time-end"sv, // rpc, speed settings
    "alt-speed-up"sv, // gtk app, rpc, speed settings
    "alt_speed_down"sv, // gtk app, rpc, speed settings
    "alt_speed_enabled"sv, // gtk app, rpc, speed settings
    "alt_speed_time_begin"sv, // rpc, speed settings
    "alt_speed_time_day"sv, // rpc, speed settings
    "alt_speed_time_enabled"sv, // rpc, speed settings
    "alt_speed_time_end"sv, // rpc, speed settings
    "alt_speed_up"sv, // gtk app, rpc, speed settings
    "announce"sv, // BEP0003; BT protocol
    "announce-ip"sv, // tr_session::Settings
    "announce-ip-enabled"sv, // tr_session::Settings
    "announce-list"sv, // BEP0012; BT protocol
    "announceState"sv, // rpc
    "announce_ip"sv, // tr_session::Settings
    "announce_ip_enabled"sv, // tr_session::Settings
    "announce_state"sv, // rpc
    "anti-brute-force-enabled"sv, // rpc, rpc server settings
    "anti-brute-force-threshold"sv, // rpc server settings
    "anti_brute_force_enabled"sv, // rpc, rpc server settings
    "anti_brute_force_threshold"sv, // rpc server settings
    "arguments"sv, // json-rpc
    "availability"sv, // rpc
    "bandwidth-priority"sv, // .resume
    "bandwidthPriority"sv, // rpc
    "bandwidth_priority"sv, // .resume, rpc
    "begin_piece"sv, // rpc
    "bind-address-ipv4"sv, // daemon, tr_session::Settings
    "bind-address-ipv6"sv, // daemon, tr_session::Settings
    "bind_address_ipv4"sv, // daemon, tr_session::Settings
    "bind_address_ipv6"sv, // daemon, tr_session::Settings
    "bitfield"sv, // .resume
    "blocklist-date"sv, // gtk app, qt app
    "blocklist-enabled"sv, // daemon, gtk app, rpc, tr_session::Settings
    "blocklist-size"sv, // rpc
    "blocklist-update"sv, // rpc
    "blocklist-updates-enabled"sv, // gtk app, qt app
    "blocklist-url"sv, // rpc, tr_session::Settings
    "blocklist_date"sv, // gtk app, qt app
    "blocklist_enabled"sv, // daemon, gtk app, rpc, tr_session::Settings
    "blocklist_size"sv, // rpc
    "blocklist_update"sv, // rpc
    "blocklist_updates_enabled"sv, // gtk app, qt app
    "blocklist_url"sv, // rpc, tr_session::Settings
    "blocks"sv, // .resume
    "bytesCompleted"sv, // rpc
    "bytes_completed"sv, // rpc
    "bytes_to_client"sv, // rpc
    "bytes_to_peer"sv, // rpc
    "cache-size-mb"sv, // rpc, tr_session::Settings
    "cache_size_mb"sv, // rpc, tr_session::Settings
    "clientIsChoked"sv, // rpc
    "clientIsInterested"sv, // rpc
    "clientName"sv, // rpc
    "client_is_choked"sv, // rpc
    "client_is_interested"sv, // rpc
    "client_name"sv, // rpc
    "code"sv, // json-rpc
    "comment"sv, // .torrent, rpc
    "compact-view"sv, // gtk app, qt app
    "compact_view"sv, // gtk app, qt app
    "complete"sv, // BEP0048; BT protocol
    "config-dir"sv, // rpc
    "config_dir"sv, // rpc
    "cookies"sv, // rpc
    "corrupt"sv, // .resume
    "corruptEver"sv, // rpc
    "corrupt_ever"sv, // rpc
    "created by"sv, // .torrent
    "creation date"sv, // .torrent
    "creator"sv, // rpc
    "cumulative-stats"sv, // rpc
    "cumulative_stats"sv, // rpc
    "current-stats"sv, // rpc
    "current_stats"sv, // rpc
    "data"sv, // json-rpc, rpc
    "dateCreated"sv, // rpc
    "date_created"sv, // rpc
    "default-trackers"sv, // daemon, rpc, tr_session::Settings
    "default_trackers"sv, // daemon, rpc, tr_session::Settings
    "delete-local-data"sv, // rpc
    "delete_local_data"sv, // rpc
    "desiredAvailable"sv, // rpc
    "desired_available"sv, // rpc
    "destination"sv, // .resume
    "details-window-height"sv, // gtk app
    "details-window-width"sv, // gtk app
    "details_window_height"sv, // gtk app
    "details_window_width"sv, // gtk app
    "dht-enabled"sv, // daemon, rpc, tr_session::Settings
    "dht_enabled"sv, // daemon, rpc, tr_session::Settings
    "dnd"sv, // .resume
    "done-date"sv, // .resume
    "doneDate"sv, // rpc
    "done_date"sv, // .resume, rpc
    "download-dir"sv, // daemon, gtk app, tr_session::Settings
    "download-dir-free-space"sv, // rpc
    "download-queue-enabled"sv, // rpc, tr_session::Settings
    "download-queue-size"sv, // rpc, tr_session::Settings
    "downloadCount"sv, // rpc
    "downloadDir"sv, // rpc
    "downloadLimit"sv, // rpc
    "downloadLimited"sv, // rpc
    "downloadSpeed"sv, // rpc
    "download_count"sv, // rpc
    "download_dir"sv, // daemon, gtk app, rpc, tr_session::Settings
    "download_dir_free_space"sv, // rpc
    "download_limit"sv, // rpc
    "download_limited"sv, // rpc
    "download_queue_enabled"sv, // rpc, tr_session::Settings
    "download_queue_size"sv, // rpc, tr_session::Settings
    "download_speed"sv, // rpc
    "downloaded"sv, // BEP0048; .resume, BT protocol
    "downloaded-bytes"sv, // stats.json
    "downloadedBytes"sv, // rpc
    "downloadedEver"sv, // rpc
    "downloaded_bytes"sv, // rpc, stats.json
    "downloaded_ever"sv, // rpc
    "downloader_count"sv, // rpc
    "downloading-time-seconds"sv, // .resume
    "downloading_time_seconds"sv, // .resume
    "dropped"sv, // BEP0011; BT protocol
    "dropped6"sv, // BEP0011; BT protocol
    "e"sv, // BT protocol
    "editDate"sv, // rpc
    "edit_date"sv, // rpc
    "encoding"sv, // .torrent
    "encryption"sv, // daemon, rpc, tr_session::Settings
    "end_piece"sv, // rpc
    "error"sv, // rpc
    "errorString"sv, // rpc
    "error_string"sv, // rpc
    "eta"sv, // rpc
    "etaIdle"sv, // rpc
    "eta_idle"sv, // rpc
    "fields"sv, // rpc
    "file-count"sv, // rpc
    "fileStats"sv, // rpc
    "file_count"sv, // rpc
    "file_stats"sv, // rpc
    "filename"sv, // rpc
    "files"sv, // .resume, .torrent, rpc
    "files-added"sv, // stats.json
    "files-unwanted"sv, // rpc
    "files-wanted"sv, // rpc
    "filesAdded"sv, // rpc
    "files_added"sv, // rpc, stats.json
    "files_unwanted"sv, // rpc
    "files_wanted"sv, // rpc
    "filter-mode"sv, // qt app
    "filter-text"sv, // qt app
    "filter-trackers"sv, // qt app
    "filter_mode"sv, // qt app
    "filter_text"sv, // qt app
    "filter_trackers"sv, // qt app
    "flagStr"sv, // rpc
    "flag_str"sv, // rpc
    "flags"sv, // .resume
    "format"sv, // rpc
    "free-space"sv, // rpc
    "free_space"sv, // rpc
    "fromCache"sv, // rpc
    "fromDht"sv, // rpc
    "fromIncoming"sv, // rpc
    "fromLpd"sv, // rpc
    "fromLtep"sv, // rpc
    "fromPex"sv, // rpc
    "fromTracker"sv, // rpc
    "from_cache"sv, // rpc
    "from_dht"sv, // rpc
    "from_incoming"sv, // rpc
    "from_lpd"sv, // rpc
    "from_ltep"sv, // rpc
    "from_pex"sv, // rpc
    "from_tracker"sv, // rpc
    "group"sv, // .resume, rpc
    "group-get"sv, // rpc
    "group-set"sv, // rpc
    "group_get"sv, // rpc
    "group_set"sv, // rpc
    "hasAnnounced"sv, // rpc
    "hasScraped"sv, // rpc
    "has_announced"sv, // rpc
    "has_scraped"sv, // rpc
    "hashString"sv, // rpc
    "hash_string"sv, // rpc
    "haveUnchecked"sv, // rpc
    "haveValid"sv, // rpc
    "have_unchecked"sv, // rpc
    "have_valid"sv, // rpc
    "honorsSessionLimits"sv, // rpc
    "honors_session_limits"sv, // rpc
    "host"sv, // rpc
    "id"sv, // dht.dat, rpc
    "id_timestamp"sv, // dht.dat
    "idle-limit"sv, // .resume
    "idle-mode"sv, // .resume
    "idle-seeding-limit"sv, // rpc, tr_session::Settings
    "idle-seeding-limit-enabled"sv, // rpc, tr_session::Settings
    "idle_limit"sv, // .resume
    "idle_mode"sv, // .resume
    "idle_seeding_limit"sv, // rpc, tr_session::Settings
    "idle_seeding_limit_enabled"sv, // rpc, tr_session::Settings
    "ids"sv, // rpc
    "incomplete"sv, // BEP0048; BT protocol
    "incomplete-dir"sv, // .resume, daemon, gtk app, rpc, tr_session::Settings
    "incomplete-dir-enabled"sv, // daemon, rpc, tr_session::Settings
    "incomplete_dir"sv, // .resume, daemon, gtk app, rpc, tr_session::Settings
    "incomplete_dir_enabled"sv, // daemon, rpc, tr_session::Settings
    "info"sv, // .torrent
    "inhibit-desktop-hibernation"sv, // gtk app, qt app
    "inhibit_desktop_hibernation"sv, // gtk app, qt app
    "ip_protocol"sv, // rpc
    "ipv4"sv, // BEP0010; BT protocol, rpc
    "ipv6"sv, // BEP0010; BT protocol, rpc
    "isBackup"sv, // rpc
    "isDownloadingFrom"sv, // rpc
    "isEncrypted"sv, // rpc
    "isFinished"sv, // rpc
    "isIncoming"sv, // rpc
    "isPrivate"sv, // rpc
    "isStalled"sv, // rpc
    "isUTP"sv, // rpc
    "isUploadingTo"sv, // rpc
    "is_backup"sv, // rpc
    "is_downloading_from"sv, // rpc
    "is_encrypted"sv, // rpc
    "is_finished"sv, // rpc
    "is_incoming"sv, // rpc
    "is_private"sv, // rpc
    "is_stalled"sv, // rpc
    "is_uploading_to"sv, // rpc
    "is_utp"sv, // rpc
    "jsonrpc"sv, // json-rpc
    "labels"sv, // .resume, rpc
    "lastAnnouncePeerCount"sv, // rpc
    "lastAnnounceResult"sv, // rpc
    "lastAnnounceStartTime"sv, // rpc
    "lastAnnounceSucceeded"sv, // rpc
    "lastAnnounceTime"sv, // rpc
    "lastAnnounceTimedOut"sv, // rpc
    "lastScrapeResult"sv, // rpc
    "lastScrapeStartTime"sv, // rpc
    "lastScrapeSucceeded"sv, // rpc
    "lastScrapeTime"sv, // rpc
    "lastScrapeTimedOut"sv, // rpc
    "last_announce_peer_count"sv, // rpc
    "last_announce_result"sv, // rpc
    "last_announce_start_time"sv, // rpc
    "last_announce_succeeded"sv, // rpc
    "last_announce_time"sv, // rpc
    "last_announce_timed_out"sv, // rpc
    "last_scrape_result"sv, // rpc
    "last_scrape_start_time"sv, // rpc
    "last_scrape_succeeded"sv, // rpc
    "last_scrape_time"sv, // rpc
    "last_scrape_timed_out"sv, // rpc
    "leecherCount"sv, // rpc
    "leecher_count"sv, // rpc
    "leftUntilDone"sv, // rpc
    "left_until_done"sv, // rpc
    "length"sv, // .torrent, rpc
    "location"sv, // rpc
    "lpd-enabled"sv, // daemon, rpc, tr_session::Settings
    "lpd_enabled"sv, // daemon, rpc, tr_session::Settings
    "m"sv, // BEP0010, BEP0011; BT protocol
    "magnetLink"sv, // rpc
    "magnet_link"sv, // rpc
    "main-window-height"sv, // gtk app, qt app
    "main-window-is-maximized"sv, // gtk app
    "main-window-layout-order"sv, // qt app
    "main-window-width"sv, // gtk app, qt app
    "main-window-x"sv, // gtk app, qt app
    "main-window-y"sv, // gtk app, qt app
    "main_window_height"sv, // gtk app, qt app
    "main_window_is_maximized"sv, // gtk app
    "main_window_layout_order"sv, // qt app
    "main_window_width"sv, // gtk app, qt app
    "main_window_x"sv, // gtk app, qt app
    "main_window_y"sv, // gtk app, qt app
    "manualAnnounceTime"sv, // rpc
    "manual_announce_time"sv, // rpc
    "max-peers"sv, // .resume
    "maxConnectedPeers"sv, // rpc
    "max_connected_peers"sv, // rpc
    "max_peers"sv, // .resume
    "memory-bytes"sv, // rpc
    "memory-units"sv, // rpc
    "memory_bytes"sv, // rpc
    "memory_units"sv, // rpc
    "message"sv, // json-rpc, rpc
    "message-level"sv, // daemon, gtk app, tr_session::Settings
    "message_level"sv, // daemon, gtk app, tr_session::Settings
    "metadataPercentComplete"sv, // rpc
    "metadata_percent_complete"sv, // rpc
    "metadata_size"sv, // BEP0009; BT protocol
    "metainfo"sv, // rpc
    "method"sv, // json-rpc
    "move"sv, // rpc
    "msg_type"sv, // BT protocol
    "mtimes"sv, // .resume
    "name"sv, // .resume, .torrent, rpc
    "nextAnnounceTime"sv, // rpc
    "nextScrapeTime"sv, // rpc
    "next_announce_time"sv, // rpc
    "next_scrape_time"sv, // rpc
    "nodes"sv, // dht.dat
    "nodes6"sv, // dht.dat
    "open-dialog-dir"sv, // gtk app, qt app
    "open_dialog_dir"sv, // gtk app, qt app
    "p"sv, // BEP0010; BT protocol
    "params"sv, // json-rpc
    "path"sv, // .torrent, rpc
    "paused"sv, // .resume, rpc
    "pausedTorrentCount"sv, // rpc
    "paused_torrent_count"sv, // rpc
    "peer-congestion-algorithm"sv, // tr_session::Settings
    "peer-limit"sv, // rpc
    "peer-limit-global"sv, // daemon, rpc, tr_session::Settings
    "peer-limit-per-torrent"sv, // daemon, gtk app, rpc, tr_session::Settings
    "peer-port"sv, // daemon, gtk app, rpc, tr_session::Settings
    "peer-port-random-high"sv, // tr_session::Settings
    "peer-port-random-low"sv, // tr_session::Settings
    "peer-port-random-on-start"sv, // rpc, tr_session::Settings
    "peer-socket-tos"sv, // tr_session::Settings
    "peerIsChoked"sv, // rpc
    "peerIsInterested"sv, // rpc
    "peer_congestion_algorithm"sv, // tr_session::Settings
    "peer_id"sv, // rpc
    "peer_is_choked"sv, // rpc
    "peer_is_interested"sv, // rpc
    "peer_limit"sv, // rpc
    "peer_limit_global"sv, // daemon, rpc, tr_session::Settings
    "peer_limit_per_torrent"sv, // daemon, gtk app, rpc, tr_session::Settings
    "peer_port"sv, // daemon, gtk app, rpc, tr_session::Settings
    "peer_port_random_high"sv, // tr_session::Settings
    "peer_port_random_low"sv, // tr_session::Settings
    "peer_port_random_on_start"sv, // rpc, tr_session::Settings
    "peer_socket_tos"sv, // tr_session::Settings
    "peers"sv, // rpc
    "peers2"sv, // .resume
    "peers2-6"sv, // .resume
    "peers2_6"sv, // .resume
    "peersConnected"sv, // rpc
    "peersFrom"sv, // rpc
    "peersGettingFromUs"sv, // rpc
    "peersSendingToUs"sv, // rpc
    "peers_connected"sv, // rpc
    "peers_from"sv, // rpc
    "peers_getting_from_us"sv, // rpc
    "peers_sending_to_us"sv, // rpc
    "percentComplete"sv, // rpc
    "percentDone"sv, // rpc
    "percent_complete"sv, // rpc
    "percent_done"sv, // rpc
    "pex-enabled"sv, // rpc, tr_session::Settings
    "pex_enabled"sv, // rpc, tr_session::Settings
    "pidfile"sv, // daemon
    "piece"sv, // BT protocol
    "piece length"sv, // .torrent
    "pieceCount"sv, // rpc
    "pieceSize"sv, // rpc
    "piece_count"sv, // rpc
    "piece_size"sv, // rpc
    "pieces"sv, // .resume, .torrent, rpc
    "port"sv, // rpc
    "port-forwarding-enabled"sv, // daemon, rpc, tr_session::Settings
    "port-is-open"sv, // rpc
    "port-test"sv, // rpc
    "port_forwarding_enabled"sv, // daemon, rpc, tr_session::Settings
    "port_is_open"sv, // rpc
    "port_test"sv, // rpc
    "preallocation"sv, // tr_session::Settings
    "preferred_transports"sv, // rpc, tr_session::Settings
    "primary-mime-type"sv, // rpc
    "primary_mime_type"sv, // rpc
    "priorities"sv, // rpc
    "priority"sv, // .resume, rpc
    "priority-high"sv, // rpc
    "priority-low"sv, // rpc
    "priority-normal"sv, // rpc
    "priority_high"sv, // rpc
    "priority_low"sv, // rpc
    "priority_normal"sv, // rpc
    "private"sv, // .torrent
    "progress"sv, // .resume, rpc
    "prompt-before-exit"sv, // qt app
    "prompt_before_exit"sv, // qt app
    "proxy_url"sv, // tr_session::Settings
    "queue-move-bottom"sv, // rpc
    "queue-move-down"sv, // rpc
    "queue-move-top"sv, // rpc
    "queue-move-up"sv, // rpc
    "queue-stalled-enabled"sv, // rpc, tr_session::Settings
    "queue-stalled-minutes"sv, // rpc, tr_session::Settings
    "queuePosition"sv, // rpc
    "queue_move_bottom"sv, // rpc
    "queue_move_down"sv, // rpc
    "queue_move_top"sv, // rpc
    "queue_move_up"sv, // rpc
    "queue_position"sv, // rpc
    "queue_stalled_enabled"sv, // rpc, tr_session::Settings
    "queue_stalled_minutes"sv, // rpc, tr_session::Settings
    "rateDownload"sv, // rpc
    "rateToClient"sv, // rpc
    "rateToPeer"sv, // rpc
    "rateUpload"sv, // rpc
    "rate_download"sv, // rpc
    "rate_to_client"sv, // rpc
    "rate_to_peer"sv, // rpc
    "rate_upload"sv, // rpc
    "ratio-limit"sv, // .resume, daemon, gtk app, tr_session::Settings
    "ratio-limit-enabled"sv, // daemon, tr_session::Settings
    "ratio-mode"sv, // .resume
    "ratio_limit"sv, // .resume, daemon, gtk app, tr_session::Settings
    "ratio_limit_enabled"sv, // daemon, tr_session::Settings
    "ratio_mode"sv, // .resume
    "read-clipboard"sv, // qt app
    "read_clipboard"sv, // qt app
    "recently-active"sv, // rpc
    "recently_active"sv, // rpc
    "recheckProgress"sv, // rpc
    "recheck_progress"sv, // rpc
    "remote-session-enabled"sv, // qt app
    "remote-session-host"sv, // qt app
    "remote-session-https"sv, // qt app
    "remote-session-password"sv, // qt app
    "remote-session-port"sv, // qt app
    "remote-session-requres-authentication"sv, // SIC: misspelled prior to 4.1.0-beta.4; qt app
    "remote-session-username"sv, // qt app
    "remote_session_enabled"sv, // qt app
    "remote_session_host"sv, // qt app
    "remote_session_https"sv, // qt app
    "remote_session_password"sv, // qt app
    "remote_session_port"sv, // qt app
    "remote_session_requires_authentication"sv, // qt app
    "remote_session_rpc_url_path"sv, // qt app
    "remote_session_username"sv, // qt app
    "removed"sv, // rpc
    "rename-partial-files"sv, // rpc, tr_session::Settings
    "rename_partial_files"sv, // rpc, tr_session::Settings
    "reqq"sv, // BEP0010; BT protocol, rpc, tr_session::Settings
    "result"sv, // rpc
    "rpc-authentication-required"sv, // daemon, rpc server settings
    "rpc-bind-address"sv, // daemon, rpc server settings
    "rpc-enabled"sv, // daemon, rpc server settings
    "rpc-host-whitelist"sv, // rpc, rpc server settings
    "rpc-host-whitelist-enabled"sv, // rpc, rpc server settings
    "rpc-password"sv, // daemon, rpc server settings
    "rpc-port"sv, // daemon, gtk app, rpc server settings
    "rpc-socket-mode"sv, // rpc server settings
    "rpc-url"sv, // rpc server settings
    "rpc-username"sv, // daemon, rpc server settings
    "rpc-version"sv, // rpc
    "rpc-version-minimum"sv, // rpc
    "rpc-version-semver"sv, // rpc
    "rpc-whitelist"sv, // daemon, gtk app, rpc server settings
    "rpc-whitelist-enabled"sv, // daemon, rpc server settings
    "rpc_authentication_required"sv, // daemon, rpc server settings
    "rpc_bind_address"sv, // daemon, rpc server settings
    "rpc_enabled"sv, // daemon, rpc server settings
    "rpc_host_whitelist"sv, // rpc, rpc server settings
    "rpc_host_whitelist_enabled"sv, // rpc, rpc server settings
    "rpc_password"sv, // daemon, rpc server settings
    "rpc_port"sv, // daemon, gtk app, rpc server settings
    "rpc_socket_mode"sv, // rpc server settings
    "rpc_url"sv, // rpc server settings
    "rpc_username"sv, // daemon, rpc server settings
    "rpc_version"sv, // rpc
    "rpc_version_minimum"sv, // rpc
    "rpc_version_semver"sv, // rpc
    "rpc_whitelist"sv, // daemon, gtk app, rpc server settings
    "rpc_whitelist_enabled"sv, // daemon, rpc server settings
    "scrape"sv, // rpc
    "scrape-paused-torrents-enabled"sv, // tr_session::Settings
    "scrapeState"sv, // rpc
    "scrape_paused_torrents_enabled"sv, // tr_session::Settings
    "scrape_state"sv, // rpc
    "script-torrent-added-enabled"sv, // rpc, tr_session::Settings
    "script-torrent-added-filename"sv, // rpc, tr_session::Settings
    "script-torrent-done-enabled"sv, // rpc, tr_session::Settings
    "script-torrent-done-filename"sv, // rpc, tr_session::Settings
    "script-torrent-done-seeding-enabled"sv, // rpc, tr_session::Settings
    "script-torrent-done-seeding-filename"sv, // rpc, tr_session::Settings
    "script_torrent_added_enabled"sv, // rpc, tr_session::Settings
    "script_torrent_added_filename"sv, // rpc, tr_session::Settings
    "script_torrent_done_enabled"sv, // rpc, tr_session::Settings
    "script_torrent_done_filename"sv, // rpc, tr_session::Settings
    "script_torrent_done_seeding_enabled"sv, // rpc, tr_session::Settings
    "script_torrent_done_seeding_filename"sv, // rpc, tr_session::Settings
    "seconds-active"sv, // stats.json
    "secondsActive"sv, // rpc
    "secondsDownloading"sv, // rpc
    "secondsSeeding"sv, // rpc
    "seconds_active"sv, // rpc, stats.json
    "seconds_downloading"sv, // rpc
    "seconds_seeding"sv, // rpc
    "seed-queue-enabled"sv, // rpc, tr_session::Settings
    "seed-queue-size"sv, // rpc, tr_session::Settings
    "seedIdleLimit"sv, // rpc
    "seedIdleMode"sv, // rpc
    "seedRatioLimit"sv, // rpc
    "seedRatioLimited"sv, // rpc
    "seedRatioMode"sv, // rpc
    "seed_idle_limit"sv, // rpc
    "seed_idle_mode"sv, // rpc
    "seed_queue_enabled"sv, // rpc, tr_session::Settings
    "seed_queue_size"sv, // rpc, tr_session::Settings
    "seed_ratio_limit"sv, // rpc
    "seed_ratio_limited"sv, // rpc
    "seed_ratio_mode"sv, // rpc
    "seederCount"sv, // rpc
    "seeder_count"sv, // rpc
    "seeding-time-seconds"sv, // .resume
    "seeding_time_seconds"sv, // .resume
    "sequential_download"sv, // .resume, daemon, rpc, tr_session::Settings
    "sequential_download_from_piece"sv, // .resume, rpc
    "session-close"sv, // rpc
    "session-count"sv, // stats.json
    "session-get"sv, // rpc
    "session-id"sv, // rpc
    "session-set"sv, // rpc
    "session-stats"sv, // rpc
    "sessionCount"sv, // rpc
    "session_close"sv, // rpc
    "session_count"sv, // rpc, stats.json
    "session_get"sv, // rpc
    "session_id"sv, // rpc
    "session_set"sv, // rpc
    "session_stats"sv, // rpc
    "show-backup-trackers"sv, // gtk app, qt app
    "show-extra-peer-details"sv, // gtk app
    "show-filterbar"sv, // gtk app, qt app
    "show-notification-area-icon"sv, // gtk app, qt app
    "show-options-window"sv, // gtk app, qt app
    "show-statusbar"sv, // gtk app, qt app
    "show-toolbar"sv, // gtk app, qt app
    "show-tracker-scrapes"sv, // gtk app, qt app
    "show_backup_trackers"sv, // gtk app, qt app
    "show_extra_peer_details"sv, // gtk app
    "show_filterbar"sv, // gtk app, qt app
    "show_notification_area_icon"sv, // gtk app, qt app
    "show_options_window"sv, // gtk app, qt app
    "show_statusbar"sv, // gtk app, qt app
    "show_toolbar"sv, // gtk app, qt app
    "show_tracker_scrapes"sv, // gtk app, qt app
    "sitename"sv, // rpc
    "size-bytes"sv, // rpc
    "size-units"sv, // rpc
    "sizeWhenDone"sv, // rpc
    "size_bytes"sv, // rpc
    "size_units"sv, // rpc
    "size_when_done"sv, // rpc
    "sleep_per_seconds_during_verify"sv, // tr_session::Settings
    "socket_address"sv, // .resume
    "sort-mode"sv, // gtk app, qt app
    "sort-reversed"sv, // gtk app, qt app
    "sort_mode"sv, // gtk app, qt app
    "sort_reversed"sv, // gtk app, qt app
    "source"sv, // .torrent
    "speed"sv, // .resume
    "speed-Bps"sv, // .resume
    "speed-bytes"sv, // rpc
    "speed-limit-down"sv, // .resume, gtk app, rpc, tr_session::Settings
    "speed-limit-down-enabled"sv, // rpc, tr_session::Settings
    "speed-limit-up"sv, // .resume, gtk app, rpc, tr_session::Settings
    "speed-limit-up-enabled"sv, // rpc, tr_session::Settings
    "speed-units"sv, // rpc
    "speed_Bps"sv, // .resume
    "speed_bytes"sv, // rpc
    "speed_limit_down"sv, // .resume, gtk app, rpc, tr_session::Settings
    "speed_limit_down_enabled"sv, // rpc, tr_session::Settings
    "speed_limit_up"sv, // .resume, gtk app, rpc, tr_session::Settings
    "speed_limit_up_enabled"sv, // rpc, tr_session::Settings
    "speed_units"sv, // rpc
    "start-added-torrents"sv, // gtk app, rpc, tr_session::Settings
    "start-minimized"sv, // qt app
    "startDate"sv, // rpc
    "start_added_torrents"sv, // gtk app, rpc, tr_session::Settings
    "start_date"sv, // rpc
    "start_minimized"sv, // qt app
    "start_paused"sv, // daemon
    "status"sv, // rpc
    "statusbar-stats"sv, // gtk app, qt app
    "statusbar_stats"sv, // gtk app, qt app
    "tag"sv, // rpc
    "tcp-enabled"sv, // rpc, tr_session::Settings
    "tcp_enabled"sv, // rpc, tr_session::Settings
    "tier"sv, // rpc
    "time-checked"sv, // .resume
    "time_checked"sv, // .resume
    "torrent-add"sv, // rpc
    "torrent-added"sv, // rpc
    "torrent-added-notification-enabled"sv, // gtk app, qt app
    "torrent-added-verify-mode"sv, // tr_session::Settings
    "torrent-complete-notification-enabled"sv, // gtk app, qt app
    "torrent-complete-sound-command"sv, // gtk app, qt app
    "torrent-complete-sound-enabled"sv, // gtk app, qt app
    "torrent-duplicate"sv, // rpc
    "torrent-get"sv, // rpc
    "torrent-reannounce"sv, // rpc
    "torrent-remove"sv, // rpc
    "torrent-rename-path"sv, // rpc
    "torrent-set"sv, // rpc
    "torrent-set-location"sv, // rpc
    "torrent-start"sv, // rpc
    "torrent-start-now"sv, // rpc
    "torrent-stop"sv, // rpc
    "torrent-verify"sv, // rpc
    "torrentCount"sv, // rpc
    "torrentFile"sv, // rpc
    "torrent_add"sv, // rpc
    "torrent_added"sv, // rpc
    "torrent_added_notification_enabled"sv, // gtk app, qt app
    "torrent_added_verify_mode"sv, // tr_session::Settings
    "torrent_complete_notification_enabled"sv, // gtk app, qt app
    "torrent_complete_sound_command"sv, // gtk app, qt app
    "torrent_complete_sound_enabled"sv, // gtk app, qt app
    "torrent_complete_verify_enabled"sv, // tr_session::Settings
    "torrent_count"sv, // rpc
    "torrent_duplicate"sv, // rpc
    "torrent_file"sv, // rpc
    "torrent_get"sv, // rpc
    "torrent_reannounce"sv, // rpc
    "torrent_remove"sv, // rpc
    "torrent_rename_path"sv, // rpc
    "torrent_set"sv, // rpc
    "torrent_set_location"sv, // rpc
    "torrent_start"sv, // rpc
    "torrent_start_now"sv, // rpc
    "torrent_stop"sv, // rpc
    "torrent_verify"sv, // rpc
    "torrents"sv, // rpc
    "totalSize"sv, // rpc
    "total_size"sv, // BT protocol, rpc
    "trackerAdd"sv, // rpc
    "trackerList"sv, // rpc
    "trackerRemove"sv, // rpc
    "trackerReplace"sv, // rpc
    "trackerStats"sv, // rpc
    "tracker_add"sv, // rpc
    "tracker_list"sv, // rpc
    "tracker_remove"sv, // rpc
    "tracker_replace"sv, // rpc
    "tracker_stats"sv, // rpc
    "trackers"sv, // rpc
    "trash-can-enabled"sv, // gtk app
    "trash-original-torrent-files"sv, // gtk app, rpc, tr_session::Settings
    "trash_can_enabled"sv, // gtk app
    "trash_original_torrent_files"sv, // gtk app, rpc, tr_session::Settings
    "umask"sv, // tr_session::Settings
    "units"sv, // rpc
    "upload-slots-per-torrent"sv, // tr_session::Settings
    "uploadLimit"sv, // rpc
    "uploadLimited"sv, // rpc
    "uploadRatio"sv, // rpc
    "uploadSpeed"sv, // rpc
    "upload_limit"sv, // rpc
    "upload_limited"sv, // rpc
    "upload_only"sv, // BEP0021; BT protocol
    "upload_ratio"sv, // rpc
    "upload_slots_per_torrent"sv, // tr_session::Settings
    "upload_speed"sv, // rpc
    "uploaded"sv, // .resume
    "uploaded-bytes"sv, // stats.json
    "uploadedBytes"sv, // rpc
    "uploadedEver"sv, // rpc
    "uploaded_bytes"sv, // rpc, stats.json
    "uploaded_ever"sv, // rpc
    "url-list"sv, // .torrent
    "use-global-speed-limit"sv, // .resume
    "use-speed-limit"sv, // .resume
    "use_global_speed_limit"sv, // .resume
    "use_speed_limit"sv, // .resume
    "user-has-given-informed-consent"sv, // gtk app, qt app
    "user_has_given_informed_consent"sv, // gtk app, qt app
    "ut_holepunch"sv, // BT protocol
    "ut_metadata"sv, // BEP0011; BT protocol
    "ut_pex"sv, // BEP0010, BEP0011; BT protocol
    "utp-enabled"sv, // daemon, rpc, tr_session::Settings
    "utp_enabled"sv, // daemon, rpc, tr_session::Settings
    "v"sv, // BEP0010; BT protocol
    "version"sv, // rpc
    "wanted"sv, // rpc
    "watch-dir"sv, // daemon, gtk app, qt app
    "watch-dir-enabled"sv, // daemon, gtk app, qt app
    "watch-dir-force-generic"sv, // daemon
    "watch_dir"sv, // daemon, gtk app, qt app
    "watch_dir_enabled"sv, // daemon, gtk app, qt app
    "watch_dir_force_generic"sv, // daemon
    "webseeds"sv, // rpc
    "webseedsSendingToUs"sv, // rpc
    "webseeds_sending_to_us"sv, // rpc
    "yourip"sv, // BEP0010; BT protocol
};

bool constexpr quarks_are_sorted()
{
    for (size_t i = 1; i < std::size(MyStatic); ++i)
    {
        if (MyStatic[i - 1] >= MyStatic[i])
        {
            return false;
        }
    }

    return true;
}

static_assert(quarks_are_sorted(), "Predefined quarks must be sorted by their string value");
static_assert(std::size(MyStatic) == TR_N_KEYS);

auto& my_runtime{ *new std::vector<std::string_view>{} };

} // namespace

std::optional<tr_quark> tr_quark_lookup(std::string_view key)
{
    // is it in our static array?
    auto constexpr Sbegin = std::begin(MyStatic);
    auto constexpr Send = std::end(MyStatic);

    if (auto const sit = std::lower_bound(Sbegin, Send, key); sit != Send && *sit == key)
    {
        return std::distance(Sbegin, sit);
    }

    /* was it added during runtime? */
    auto const rbegin = std::begin(my_runtime);
    auto const rend = std::end(my_runtime);
    if (auto const rit = std::find(rbegin, rend, key); rit != rend)
    {
        return TR_N_KEYS + std::distance(rbegin, rit);
    }

    return {};
}

tr_quark tr_quark_new(std::string_view str)
{
    if (auto const prior = tr_quark_lookup(str); prior)
    {
        return *prior;
    }

    auto const ret = TR_N_KEYS + std::size(my_runtime);
    auto const len = std::size(str);
    auto* perma = new char[len + 1];
    std::copy_n(std::begin(str), len, perma);
    perma[len] = '\0';
    my_runtime.emplace_back(perma);
    return ret;
}

std::string_view tr_quark_get_string_view(tr_quark q)
{
    return q < TR_N_KEYS ? MyStatic[q] : my_runtime[q - TR_N_KEYS];
}

tr_quark tr_quark_convert(tr_quark q)
{
    // clang-format off
    switch (q)
    {
    case TR_KEY_active_torrent_count_camel: return TR_KEY_active_torrent_count;
    case TR_KEY_activity_date_camel:
    case TR_KEY_activity_date_kebab:
        return TR_KEY_activity_date;
    case TR_KEY_added_date_camel:
    case TR_KEY_added_date_kebab:
        return TR_KEY_added_date;
    case TR_KEY_alt_speed_down_kebab: return TR_KEY_alt_speed_down;
    case TR_KEY_alt_speed_enabled_kebab: return TR_KEY_alt_speed_enabled;
    case TR_KEY_alt_speed_time_begin_kebab: return TR_KEY_alt_speed_time_begin;
    case TR_KEY_alt_speed_time_day_kebab: return TR_KEY_alt_speed_time_day;
    case TR_KEY_alt_speed_time_enabled_kebab: return TR_KEY_alt_speed_time_enabled;
    case TR_KEY_alt_speed_time_end_kebab: return TR_KEY_alt_speed_time_end;
    case TR_KEY_alt_speed_up_kebab: return TR_KEY_alt_speed_up;
    case TR_KEY_announce_ip_kebab: return TR_KEY_announce_ip;
    case TR_KEY_announce_ip_enabled_kebab: return TR_KEY_announce_ip_enabled;
    case TR_KEY_announce_state_camel: return TR_KEY_announce_state;
    case TR_KEY_anti_brute_force_enabled_kebab: return TR_KEY_anti_brute_force_enabled;
    case TR_KEY_anti_brute_force_threshold_kebab: return TR_KEY_anti_brute_force_threshold;
    case TR_KEY_bandwidth_priority_camel:
    case TR_KEY_bandwidth_priority_kebab:
        return TR_KEY_bandwidth_priority;
    case TR_KEY_bind_address_ipv4_kebab: return TR_KEY_bind_address_ipv4;
    case TR_KEY_bind_address_ipv6_kebab: return TR_KEY_bind_address_ipv6;
    case TR_KEY_blocklist_date_kebab: return TR_KEY_blocklist_date;
    case TR_KEY_blocklist_enabled_kebab: return TR_KEY_blocklist_enabled;
    case TR_KEY_blocklist_size_kebab: return TR_KEY_blocklist_size;
    case TR_KEY_blocklist_update_kebab: return TR_KEY_blocklist_update;
    case TR_KEY_blocklist_updates_enabled_kebab: return TR_KEY_blocklist_updates_enabled;
    case TR_KEY_blocklist_url_kebab: return TR_KEY_blocklist_url;
    case TR_KEY_bytes_completed_camel: return TR_KEY_bytes_completed;
    case TR_KEY_cache_size_mb_kebab: return TR_KEY_cache_size_mb;
    case TR_KEY_client_is_choked_camel: return TR_KEY_client_is_choked;
    case TR_KEY_client_is_interested_camel: return TR_KEY_client_is_interested;
    case TR_KEY_client_name_camel: return TR_KEY_client_name;
    case TR_KEY_compact_view_kebab: return TR_KEY_compact_view;
    case TR_KEY_config_dir_kebab: return TR_KEY_config_dir;
    case TR_KEY_corrupt_ever_camel: return TR_KEY_corrupt_ever;
    case TR_KEY_cumulative_stats_kebab: return TR_KEY_cumulative_stats;
    case TR_KEY_current_stats_kebab: return TR_KEY_current_stats;
    case TR_KEY_date_created_camel: return TR_KEY_date_created;
    case TR_KEY_default_trackers_kebab: return TR_KEY_default_trackers;
    case TR_KEY_delete_local_data_kebab: return TR_KEY_delete_local_data;
    case TR_KEY_desired_available_camel: return TR_KEY_desired_available;
    case TR_KEY_details_window_height_kebab: return TR_KEY_details_window_height;
    case TR_KEY_details_window_width_kebab: return TR_KEY_details_window_width;
    case TR_KEY_dht_enabled_kebab: return TR_KEY_dht_enabled;
    case TR_KEY_done_date_camel:
    case TR_KEY_done_date_kebab:
        return TR_KEY_done_date;
    case TR_KEY_download_count_camel: return TR_KEY_download_count;
    case TR_KEY_download_dir_camel:
    case TR_KEY_download_dir_kebab:
        return TR_KEY_download_dir;
    case TR_KEY_download_dir_free_space_kebab: return TR_KEY_download_dir_free_space;
    case TR_KEY_download_limit_camel: return TR_KEY_download_limit;
    case TR_KEY_download_limited_camel: return TR_KEY_download_limited;
    case TR_KEY_download_queue_enabled_kebab: return TR_KEY_download_queue_enabled;
    case TR_KEY_download_queue_size_kebab: return TR_KEY_download_queue_size;
    case TR_KEY_download_speed_camel: return TR_KEY_download_speed;
    case TR_KEY_downloaded_bytes_camel:
    case TR_KEY_downloaded_bytes_kebab:
        return TR_KEY_downloaded_bytes;
    case TR_KEY_downloaded_ever_camel: return TR_KEY_downloaded_ever;
    case TR_KEY_downloading_time_seconds_kebab: return TR_KEY_downloading_time_seconds;
    case TR_KEY_edit_date_camel: return TR_KEY_edit_date;
    case TR_KEY_error_string_camel: return TR_KEY_error_string;
    case TR_KEY_eta_idle_camel: return TR_KEY_eta_idle;
    case TR_KEY_file_count_kebab: return TR_KEY_file_count;
    case TR_KEY_file_stats_camel: return TR_KEY_file_stats;
    case TR_KEY_files_added_camel:
    case TR_KEY_files_added_kebab:
        return TR_KEY_files_added;
    case TR_KEY_files_unwanted_kebab: return TR_KEY_files_unwanted;
    case TR_KEY_files_wanted_kebab: return TR_KEY_files_wanted;
    case TR_KEY_filter_mode_kebab: return TR_KEY_filter_mode;
    case TR_KEY_filter_text_kebab: return TR_KEY_filter_text;
    case TR_KEY_filter_trackers_kebab: return TR_KEY_filter_trackers;
    case TR_KEY_flag_str_camel: return TR_KEY_flag_str;
    case TR_KEY_free_space_kebab: return TR_KEY_free_space;
    case TR_KEY_from_cache_camel: return TR_KEY_from_cache;
    case TR_KEY_from_dht_camel: return TR_KEY_from_dht;
    case TR_KEY_from_incoming_camel: return TR_KEY_from_incoming;
    case TR_KEY_from_lpd_camel: return TR_KEY_from_lpd;
    case TR_KEY_from_ltep_camel: return TR_KEY_from_ltep;
    case TR_KEY_from_pex_camel: return TR_KEY_from_pex;
    case TR_KEY_from_tracker_camel: return TR_KEY_from_tracker;
    case TR_KEY_group_get_kebab: return TR_KEY_group_get;
    case TR_KEY_group_set_kebab: return TR_KEY_group_set;
    case TR_KEY_hash_string_camel: return TR_KEY_hash_string;
    case TR_KEY_has_announced_camel: return TR_KEY_has_announced;
    case TR_KEY_has_scraped_camel: return TR_KEY_has_scraped;
    case TR_KEY_have_unchecked_camel: return TR_KEY_have_unchecked;
    case TR_KEY_have_valid_camel: return TR_KEY_have_valid;
    case TR_KEY_honors_session_limits_camel: return TR_KEY_honors_session_limits;
    case TR_KEY_idle_limit_kebab: return TR_KEY_idle_limit;
    case TR_KEY_idle_mode_kebab: return TR_KEY_idle_mode;
    case TR_KEY_idle_seeding_limit_kebab: return TR_KEY_idle_seeding_limit;
    case TR_KEY_idle_seeding_limit_enabled_kebab: return TR_KEY_idle_seeding_limit_enabled;
    case TR_KEY_incomplete_dir_kebab: return TR_KEY_incomplete_dir;
    case TR_KEY_incomplete_dir_enabled_kebab: return TR_KEY_incomplete_dir_enabled;
    case TR_KEY_inhibit_desktop_hibernation_kebab: return TR_KEY_inhibit_desktop_hibernation;
    case TR_KEY_is_backup_camel: return TR_KEY_is_backup;
    case TR_KEY_is_downloading_from_camel: return TR_KEY_is_downloading_from;
    case TR_KEY_is_encrypted_camel: return TR_KEY_is_encrypted;
    case TR_KEY_is_finished_camel: return TR_KEY_is_finished;
    case TR_KEY_is_incoming_camel: return TR_KEY_is_incoming;
    case TR_KEY_is_private_camel: return TR_KEY_is_private;
    case TR_KEY_is_stalled_camel: return TR_KEY_is_stalled;
    case TR_KEY_is_uploading_to_camel: return TR_KEY_is_uploading_to;
    case TR_KEY_is_utp_camel: return TR_KEY_is_utp;
    case TR_KEY_last_announce_peer_count_camel: return TR_KEY_last_announce_peer_count;
    case TR_KEY_last_announce_result_camel: return TR_KEY_last_announce_result;
    case TR_KEY_last_announce_start_time_camel: return TR_KEY_last_announce_start_time;
    case TR_KEY_last_announce_succeeded_camel: return TR_KEY_last_announce_succeeded;
    case TR_KEY_last_announce_time_camel: return TR_KEY_last_announce_time;
    case TR_KEY_last_announce_timed_out_camel: return TR_KEY_last_announce_timed_out;
    case TR_KEY_last_scrape_result_camel: return TR_KEY_last_scrape_result;
    case TR_KEY_last_scrape_start_time_camel: return TR_KEY_last_scrape_start_time;
    case TR_KEY_last_scrape_succeeded_camel: return TR_KEY_last_scrape_succeeded;
    case TR_KEY_last_scrape_time_camel: return TR_KEY_last_scrape_time;
    case TR_KEY_last_scrape_timed_out_camel: return TR_KEY_last_scrape_timed_out;
    case TR_KEY_leecher_count_camel: return TR_KEY_leecher_count;
    case TR_KEY_left_until_done_camel: return TR_KEY_left_until_done;
    case TR_KEY_lpd_enabled_kebab: return TR_KEY_lpd_enabled;
    case TR_KEY_magnet_link_camel: return TR_KEY_magnet_link;
    case TR_KEY_main_window_height_kebab: return TR_KEY_main_window_height;
    case TR_KEY_main_window_is_maximized_kebab: return TR_KEY_main_window_is_maximized;
    case TR_KEY_main_window_layout_order_kebab: return TR_KEY_main_window_layout_order;
    case TR_KEY_main_window_width_kebab: return TR_KEY_main_window_width;
    case TR_KEY_main_window_x_kebab: return TR_KEY_main_window_x;
    case TR_KEY_main_window_y_kebab: return TR_KEY_main_window_y;
    case TR_KEY_manual_announce_time_camel: return TR_KEY_manual_announce_time;
    case TR_KEY_max_connected_peers_camel: return TR_KEY_max_connected_peers;
    case TR_KEY_max_peers_kebab: return TR_KEY_max_peers;
    case TR_KEY_memory_bytes_kebab: return TR_KEY_memory_bytes;
    case TR_KEY_memory_units_kebab: return TR_KEY_memory_units;
    case TR_KEY_message_level_kebab: return TR_KEY_message_level;
    case TR_KEY_metadata_percent_complete_camel: return TR_KEY_metadata_percent_complete;
    case TR_KEY_next_announce_time_camel: return TR_KEY_next_announce_time;
    case TR_KEY_next_scrape_time_camel: return TR_KEY_next_scrape_time;
    case TR_KEY_open_dialog_dir_kebab: return TR_KEY_open_dialog_dir;
    case TR_KEY_paused_torrent_count_camel: return TR_KEY_paused_torrent_count;
    case TR_KEY_peer_congestion_algorithm_kebab: return TR_KEY_peer_congestion_algorithm;
    case TR_KEY_peer_is_choked_camel: return TR_KEY_peer_is_choked;
    case TR_KEY_peer_is_interested_camel: return TR_KEY_peer_is_interested;
    case TR_KEY_peer_limit_kebab: return TR_KEY_peer_limit;
    case TR_KEY_peer_limit_global_kebab: return TR_KEY_peer_limit_global;
    case TR_KEY_peer_limit_per_torrent_kebab: return TR_KEY_peer_limit_per_torrent;
    case TR_KEY_peer_port_kebab: return TR_KEY_peer_port;
    case TR_KEY_peer_port_random_high_kebab: return TR_KEY_peer_port_random_high;
    case TR_KEY_peer_port_random_low_kebab: return TR_KEY_peer_port_random_low;
    case TR_KEY_peer_port_random_on_start_kebab: return TR_KEY_peer_port_random_on_start;
    case TR_KEY_peer_socket_tos_kebab: return TR_KEY_peer_socket_tos;
    case TR_KEY_peers2_6_kebab: return TR_KEY_peers2_6;
    case TR_KEY_peers_connected_camel: return TR_KEY_peers_connected;
    case TR_KEY_peers_from_camel: return TR_KEY_peers_from;
    case TR_KEY_peers_getting_from_us_camel: return TR_KEY_peers_getting_from_us;
    case TR_KEY_peers_sending_to_us_camel: return TR_KEY_peers_sending_to_us;
    case TR_KEY_percent_complete_camel: return TR_KEY_percent_complete;
    case TR_KEY_percent_done_camel: return TR_KEY_percent_done;
    case TR_KEY_pex_enabled_kebab: return TR_KEY_pex_enabled;
    case TR_KEY_piece_count_camel: return TR_KEY_piece_count;
    case TR_KEY_piece_size_camel: return TR_KEY_piece_size;
    case TR_KEY_port_forwarding_enabled_kebab: return TR_KEY_port_forwarding_enabled;
    case TR_KEY_port_is_open_kebab: return TR_KEY_port_is_open;
    case TR_KEY_port_test_kebab: return TR_KEY_port_test;
    case TR_KEY_primary_mime_type_kebab: return TR_KEY_primary_mime_type;
    case TR_KEY_priority_high_kebab: return TR_KEY_priority_high;
    case TR_KEY_priority_low_kebab: return TR_KEY_priority_low;
    case TR_KEY_priority_normal_kebab: return TR_KEY_priority_normal;
    case TR_KEY_prompt_before_exit_kebab: return TR_KEY_prompt_before_exit;
    case TR_KEY_queue_position_camel: return TR_KEY_queue_position;
    case TR_KEY_queue_move_bottom_kebab: return TR_KEY_queue_move_bottom;
    case TR_KEY_queue_move_down_kebab: return TR_KEY_queue_move_down;
    case TR_KEY_queue_move_top_kebab: return TR_KEY_queue_move_top;
    case TR_KEY_queue_move_up_kebab: return TR_KEY_queue_move_up;
    case TR_KEY_queue_stalled_enabled_kebab: return TR_KEY_queue_stalled_enabled;
    case TR_KEY_queue_stalled_minutes_kebab: return TR_KEY_queue_stalled_minutes;
    case TR_KEY_rate_download_camel: return TR_KEY_rate_download;
    case TR_KEY_rate_to_client_camel: return TR_KEY_rate_to_client;
    case TR_KEY_rate_to_peer_camel: return TR_KEY_rate_to_peer;
    case TR_KEY_rate_upload_camel: return TR_KEY_rate_upload;
    case TR_KEY_ratio_limit_kebab: return TR_KEY_ratio_limit;
    case TR_KEY_ratio_limit_enabled_kebab: return TR_KEY_ratio_limit_enabled;
    case TR_KEY_ratio_mode_kebab: return TR_KEY_ratio_mode;
    case TR_KEY_read_clipboard_kebab: return TR_KEY_read_clipboard;
    case TR_KEY_recheck_progress_camel: return TR_KEY_recheck_progress;
    case TR_KEY_remote_session_enabled_kebab: return TR_KEY_remote_session_enabled;
    case TR_KEY_remote_session_host_kebab: return TR_KEY_remote_session_host;
    case TR_KEY_remote_session_https_kebab: return TR_KEY_remote_session_https;
    case TR_KEY_remote_session_password_kebab: return TR_KEY_remote_session_password;
    case TR_KEY_remote_session_port_kebab: return TR_KEY_remote_session_port;
    case TR_KEY_remote_session_requres_authentication_kebab: return TR_KEY_remote_session_requires_authentication;
    case TR_KEY_remote_session_username_kebab: return TR_KEY_remote_session_username;
    case TR_KEY_rename_partial_files_kebab: return TR_KEY_rename_partial_files;
    case TR_KEY_rpc_authentication_required_kebab: return TR_KEY_rpc_authentication_required;
    case TR_KEY_rpc_bind_address_kebab: return TR_KEY_rpc_bind_address;
    case TR_KEY_rpc_enabled_kebab: return TR_KEY_rpc_enabled;
    case TR_KEY_rpc_host_whitelist_kebab: return TR_KEY_rpc_host_whitelist;
    case TR_KEY_rpc_host_whitelist_enabled_kebab: return TR_KEY_rpc_host_whitelist_enabled;
    case TR_KEY_rpc_password_kebab: return TR_KEY_rpc_password;
    case TR_KEY_rpc_port_kebab: return TR_KEY_rpc_port;
    case TR_KEY_rpc_socket_mode_kebab: return TR_KEY_rpc_socket_mode;
    case TR_KEY_rpc_url_kebab: return TR_KEY_rpc_url;
    case TR_KEY_rpc_username_kebab: return TR_KEY_rpc_username;
    case TR_KEY_rpc_version_kebab: return TR_KEY_rpc_version;
    case TR_KEY_rpc_version_minimum_kebab: return TR_KEY_rpc_version_minimum;
    case TR_KEY_rpc_version_semver_kebab: return TR_KEY_rpc_version_semver;
    case TR_KEY_rpc_whitelist_kebab: return TR_KEY_rpc_whitelist;
    case TR_KEY_rpc_whitelist_enabled_kebab: return TR_KEY_rpc_whitelist_enabled;
    case TR_KEY_seconds_downloading_camel: return TR_KEY_seconds_downloading;
    case TR_KEY_scrape_paused_torrents_enabled_kebab: return TR_KEY_scrape_paused_torrents_enabled;
    case TR_KEY_scrape_state_camel: return TR_KEY_scrape_state;
    case TR_KEY_script_torrent_added_enabled_kebab: return TR_KEY_script_torrent_added_enabled;
    case TR_KEY_script_torrent_added_filename_kebab: return TR_KEY_script_torrent_added_filename;
    case TR_KEY_script_torrent_done_enabled_kebab: return TR_KEY_script_torrent_done_enabled;
    case TR_KEY_script_torrent_done_filename_kebab: return TR_KEY_script_torrent_done_filename;
    case TR_KEY_script_torrent_done_seeding_enabled_kebab: return TR_KEY_script_torrent_done_seeding_enabled;
    case TR_KEY_script_torrent_done_seeding_filename_kebab: return TR_KEY_script_torrent_done_seeding_filename;
    case TR_KEY_seconds_active_camel:
    case TR_KEY_seconds_active_kebab:
        return TR_KEY_seconds_active;
    case TR_KEY_seconds_seeding_camel: return TR_KEY_seconds_seeding;
    case TR_KEY_seed_idle_limit_camel: return TR_KEY_seed_idle_limit;
    case TR_KEY_seed_idle_mode_camel: return TR_KEY_seed_idle_mode;
    case TR_KEY_seed_queue_enabled_kebab: return TR_KEY_seed_queue_enabled;
    case TR_KEY_seed_queue_size_kebab: return TR_KEY_seed_queue_size;
    case TR_KEY_seed_ratio_limit_camel: return TR_KEY_seed_ratio_limit;
    case TR_KEY_seed_ratio_limited_camel: return TR_KEY_seed_ratio_limited;
    case TR_KEY_seed_ratio_mode_camel: return TR_KEY_seed_ratio_mode;
    case TR_KEY_seeding_time_seconds_kebab: return TR_KEY_seeding_time_seconds;
    case TR_KEY_seeder_count_camel: return TR_KEY_seeder_count;
    case TR_KEY_session_close_kebab: return TR_KEY_session_close;
    case TR_KEY_session_count_camel:
    case TR_KEY_session_count_kebab:
        return TR_KEY_session_count;
    case TR_KEY_session_get_kebab: return TR_KEY_session_get;
    case TR_KEY_session_id_kebab: return TR_KEY_session_id;
    case TR_KEY_session_set_kebab: return TR_KEY_session_set;
    case TR_KEY_session_stats_kebab: return TR_KEY_session_stats;
    case TR_KEY_show_backup_trackers_kebab: return TR_KEY_show_backup_trackers;
    case TR_KEY_show_extra_peer_details_kebab: return TR_KEY_show_extra_peer_details;
    case TR_KEY_show_filterbar_kebab: return TR_KEY_show_filterbar;
    case TR_KEY_show_notification_area_icon_kebab: return TR_KEY_show_notification_area_icon;
    case TR_KEY_show_options_window_kebab: return TR_KEY_show_options_window;
    case TR_KEY_show_statusbar_kebab: return TR_KEY_show_statusbar;
    case TR_KEY_show_toolbar_kebab: return TR_KEY_show_toolbar;
    case TR_KEY_show_tracker_scrapes_kebab: return TR_KEY_show_tracker_scrapes;
    case TR_KEY_size_bytes_kebab: return TR_KEY_size_bytes;
    case TR_KEY_size_units_kebab: return TR_KEY_size_units;
    case TR_KEY_size_when_done_camel: return TR_KEY_size_when_done;
    case TR_KEY_sort_mode_kebab: return TR_KEY_sort_mode;
    case TR_KEY_sort_reversed_kebab: return TR_KEY_sort_reversed;
    case TR_KEY_speed_Bps_kebab: return TR_KEY_speed_Bps;
    case TR_KEY_speed_bytes_kebab: return TR_KEY_speed_bytes;
    case TR_KEY_speed_limit_down_kebab: return TR_KEY_speed_limit_down;
    case TR_KEY_speed_limit_down_enabled_kebab: return TR_KEY_speed_limit_down_enabled;
    case TR_KEY_speed_limit_up_kebab: return TR_KEY_speed_limit_up;
    case TR_KEY_speed_limit_up_enabled_kebab: return TR_KEY_speed_limit_up_enabled;
    case TR_KEY_speed_units_kebab: return TR_KEY_speed_units;
    case TR_KEY_start_added_torrents_kebab: return TR_KEY_start_added_torrents;
    case TR_KEY_start_date_camel: return TR_KEY_start_date;
    case TR_KEY_start_minimized_kebab: return TR_KEY_start_minimized;
    case TR_KEY_statusbar_stats_kebab: return TR_KEY_statusbar_stats;
    case TR_KEY_tcp_enabled_kebab: return TR_KEY_tcp_enabled;
    case TR_KEY_torrent_add_kebab: return TR_KEY_torrent_add;
    case TR_KEY_torrent_added_kebab: return TR_KEY_torrent_added;
    case TR_KEY_torrent_added_notification_enabled_kebab: return TR_KEY_torrent_added_notification_enabled;
    case TR_KEY_torrent_added_verify_mode_kebab: return TR_KEY_torrent_added_verify_mode;
    case TR_KEY_torrent_complete_notification_enabled_kebab: return TR_KEY_torrent_complete_notification_enabled;
    case TR_KEY_torrent_complete_sound_command_kebab: return TR_KEY_torrent_complete_sound_command;
    case TR_KEY_torrent_complete_sound_enabled_kebab: return TR_KEY_torrent_complete_sound_enabled;
    case TR_KEY_torrent_count_camel: return TR_KEY_torrent_count;
    case TR_KEY_torrent_duplicate_kebab: return TR_KEY_torrent_duplicate;
    case TR_KEY_torrent_file_camel: return TR_KEY_torrent_file;
    case TR_KEY_torrent_get_kebab: return TR_KEY_torrent_get;
    case TR_KEY_torrent_reannounce_kebab: return TR_KEY_torrent_reannounce;
    case TR_KEY_torrent_remove_kebab: return TR_KEY_torrent_remove;
    case TR_KEY_torrent_rename_path_kebab: return TR_KEY_torrent_rename_path;
    case TR_KEY_torrent_set_kebab: return TR_KEY_torrent_set;
    case TR_KEY_torrent_set_location_kebab: return TR_KEY_torrent_set_location;
    case TR_KEY_torrent_start_kebab: return TR_KEY_torrent_start;
    case TR_KEY_torrent_start_now_kebab: return TR_KEY_torrent_start_now;
    case TR_KEY_torrent_stop_kebab: return TR_KEY_torrent_stop;
    case TR_KEY_torrent_verify_kebab: return TR_KEY_torrent_verify;
    case TR_KEY_total_size_camel: return TR_KEY_total_size;
    case TR_KEY_tracker_add_camel: return TR_KEY_tracker_add;
    case TR_KEY_tracker_list_camel: return TR_KEY_tracker_list;
    case TR_KEY_tracker_remove_camel: return TR_KEY_tracker_remove;
    case TR_KEY_tracker_replace_camel: return TR_KEY_tracker_replace;
    case TR_KEY_tracker_stats_camel: return TR_KEY_tracker_stats;
    case TR_KEY_trash_can_enabled_kebab: return TR_KEY_trash_can_enabled;
    case TR_KEY_trash_original_torrent_files_kebab: return TR_KEY_trash_original_torrent_files;
    case TR_KEY_upload_limit_camel: return TR_KEY_upload_limit;
    case TR_KEY_upload_limited_camel: return TR_KEY_upload_limited;
    case TR_KEY_upload_slots_per_torrent_kebab: return TR_KEY_upload_slots_per_torrent;
    case TR_KEY_upload_ratio_camel: return TR_KEY_upload_ratio;
    case TR_KEY_upload_speed_camel: return TR_KEY_upload_speed;
    case TR_KEY_uploaded_bytes_camel:
    case TR_KEY_uploaded_bytes_kebab:
        return TR_KEY_uploaded_bytes;
    case TR_KEY_uploaded_ever_camel: return TR_KEY_uploaded_ever;
    case TR_KEY_use_global_speed_limit_kebab: return TR_KEY_use_global_speed_limit;
    case TR_KEY_use_speed_limit_kebab: return TR_KEY_use_speed_limit;
    case TR_KEY_user_has_given_informed_consent_kebab: return TR_KEY_user_has_given_informed_consent;
    case TR_KEY_utp_enabled_kebab: return TR_KEY_utp_enabled;
    case TR_KEY_watch_dir_kebab: return TR_KEY_watch_dir;
    case TR_KEY_watch_dir_enabled_kebab: return TR_KEY_watch_dir_enabled;
    case TR_KEY_watch_dir_force_generic_kebab: return TR_KEY_watch_dir_force_generic;
    case TR_KEY_webseeds_sending_to_us_camel: return TR_KEY_webseeds_sending_to_us;
    default: return q;
    }
    // clang-format on
}

// ---

namespace libtransmission::api_compat
{

namespace
{

struct ApiKey
{
    // snake-case string
    std::string_view current;

    // legacy mixed-case RPC string (pre-05aef3e7)
    std::string_view legacy;

    // ignore for now
    //std::string_view legacy_setting;
};

auto constexpr RpcKeys = std::array<ApiKey, 277U>{
    { // rpc
      { "active_torrent_count"sv, "activeTorrentCount"sv },
      { "activity_date"sv, "activityDate"sv }, // TODO(ckerr) legacy duplicate
      { "added"sv, "added"sv },
      { "added_date"sv, "addedDate"sv }, // TODO(ckerr) legacy duplicate
      { "address"sv, "address"sv },
      { "alt_speed_down"sv, "alt-speed-down"sv },
      { "alt_speed_enabled"sv, "alt-speed-enabled"sv },
      { "alt_speed_time_begin"sv, "alt-speed-time-begin"sv },
      { "alt_speed_time_day"sv, "alt-speed-time-day"sv },
      { "alt_speed_time_enabled"sv, "alt-speed-time-enabled"sv },
      { "alt_speed_time_end"sv, "alt-speed-time-end"sv },
      { "alt_speed_up"sv, "alt-speed-up"sv },
      { "announce_state"sv, "announceState"sv },
      { "anti_brute_force_enabled"sv, "anti-brute-force-enabled"sv },
      { "availability"sv, "availability"sv },
      { "bandwidth_priority"sv, "bandwidthPriority"sv }, // TODO(ckerr) legacy duplicate
      { "begin_piece"sv, "begin_piece"sv }, // camelCase in nightly builds pre-7b83c7d6
      { "blocklist_enabled"sv, "blocklist-enabled"sv },
      { "blocklist_size"sv, "blocklist-size"sv },
      { "blocklist_url"sv, "blocklist-url"sv },
      { "bytes_completed"sv, "bytesCompleted"sv },
      { "bytes_to_client"sv, "bytes_to_client"sv },
      { "bytes_to_peer"sv, "bytes_to_peer"sv },
      { "cache_size_mb"sv, "cache-size-mb"sv },
      { "client_is_choked"sv, "clientIsChoked"sv },
      { "client_is_interested"sv, "clientIsInterested"sv },
      { "client_name"sv, "clientName"sv },
      { "comment"sv, "comment"sv },
      { "config_dir"sv, "config-dir"sv },
      { "cookies"sv, "cookies"sv },
      { "corrupt_ever"sv, "corruptEver"sv },
      { "creator"sv, "creator"sv },
      { "cumulative_stats"sv, "cumulative-stats"sv },
      { "current_stats"sv, "current-stats"sv },
      { "data"sv, "data"sv },
      { "date_created"sv, "dateCreated"sv },
      { "default_trackers"sv, "default-trackers"sv },
      { "delete_local_data"sv, "delete-local-data"sv },
      { "desired_available"sv, "desiredAvailable"sv },
      { "dht_enabled"sv, "dht-enabled"sv },
      { "done_date"sv, "doneDate"sv }, // TODO(ckerr) legacy duplicate
      { "download_count"sv, "downloadCount"sv },
      { "download_dir"sv, "downloadDir"sv }, // TODO(ckerr) legacy duplicate
      { "download_dir_free_space"sv, "download-dir-free-space"sv },
      { "download_limit"sv, "downloadLimit"sv },
      { "download_limited"sv, "downloadLimited"sv },
      { "download_queue_enabled"sv, "download-queue-enabled"sv },
      { "download_queue_size"sv, "download-queue-size"sv },
      { "download_speed"sv, "downloadSpeed"sv },
      { "downloaded_bytes"sv, "downloadedBytes"sv }, // TODO(ckerr) legacy duplicate
      { "downloaded_ever"sv, "downloadedEver"sv },
      { "downloader_count"sv, "downloader_count"sv },
      { "edit_date"sv, "editDate"sv },
      { "encryption"sv, "encryption"sv },
      { "end_piece"sv, "end_piece"sv }, // camelCase in nightly builds pre-7b83c7d6
      { "error"sv, "error"sv },
      { "error_string"sv, "errorString"sv },
      { "eta"sv, "eta"sv },
      { "eta_idle"sv, "etaIdle"sv },
      { "fields"sv, "fields"sv },
      { "file_count"sv, "file-count"sv },
      { "file_stats"sv, "fileStats"sv },
      { "filename"sv, "filename"sv },
      { "files"sv, "files"sv },
      { "files_added"sv, "filesAdded"sv }, // TODO(ckerr) legacy duplicate
      { "files_unwanted"sv, "files-unwanted"sv },
      { "files_wanted"sv, "files-wanted"sv },
      { "flag_str"sv, "flagStr"sv },
      { "format"sv, "format"sv },
      { "from_cache"sv, "fromCache"sv },
      { "from_dht"sv, "fromDht"sv },
      { "from_incoming"sv, "fromIncoming"sv },
      { "from_lpd"sv, "fromLpd"sv },
      { "from_ltep"sv, "fromLtep"sv },
      { "from_pex"sv, "fromPex"sv },
      { "from_tracker"sv, "fromTracker"sv },
      { "group"sv, "group"sv },
      { "has_announced"sv, "hasAnnounced"sv },
      { "has_scraped"sv, "hasScraped"sv },
      { "hash_string"sv, "hashString"sv },
      { "have_unchecked"sv, "haveUnchecked"sv },
      { "have_valid"sv, "haveValid"sv },
      { "honors_session_limits"sv, "honorsSessionLimits"sv },
      { "host"sv, "host"sv },
      { "id"sv, "id"sv },
      { "id"sv, "tag"sv }, // FIXME(ckerr): edge case: id<->id elsewhere
      { "idle_seeding_limit"sv, "idle-seeding-limit"sv },
      { "idle_seeding_limit_enabled"sv, "idle-seeding-limit-enabled"sv },
      { "ids"sv, "ids"sv },
      { "incomplete_dir"sv, "incomplete-dir"sv },
      { "incomplete_dir_enabled"sv, "incomplete-dir-enabled"sv },
      { "ip_protocol"sv, "ip_protocol"sv }, // camelCase in nightly builds pre-7b83c7d6
      { "ipv4"sv, "ipv4"sv },
      { "ipv6"sv, "ipv6"sv },
      { "is_backup"sv, "isBackup"sv },
      { "is_downloading_from"sv, "isDownloadingFrom"sv },
      { "is_encrypted"sv, "isEncrypted"sv },
      { "is_finished"sv, "isFinished"sv },
      { "is_incoming"sv, "isIncoming"sv },
      { "is_private"sv, "isPrivate"sv },
      { "is_stalled"sv, "isStalled"sv },
      { "is_uploading_to"sv, "isUploadingTo"sv },
      { "is_utp"sv, "isUTP"sv },
      { "labels"sv, "labels"sv },
      { "last_announce_peer_count"sv, "lastAnnouncePeerCount"sv },
      { "last_announce_result"sv, "lastAnnounceResult"sv },
      { "last_announce_start_time"sv, "lastAnnounceStartTime"sv },
      { "last_announce_succeeded"sv, "lastAnnounceSucceeded"sv },
      { "last_announce_time"sv, "lastAnnounceTime"sv },
      { "last_announce_timed_out"sv, "lastAnnounceTimedOut"sv },
      { "last_scrape_result"sv, "lastScrapeResult"sv },
      { "last_scrape_start_time"sv, "lastScrapeStartTime"sv },
      { "last_scrape_succeeded"sv, "lastScrapeSucceeded"sv },
      { "last_scrape_time"sv, "lastScrapeTime"sv },
      { "last_scrape_timed_out"sv, "lastScrapeTimedOut"sv },
      { "leecher_count"sv, "leecherCount"sv },
      { "left_until_done"sv, "leftUntilDone"sv },
      { "length"sv, "length"sv },
      { "location"sv, "location"sv },
      { "lpd_enabled"sv, "lpd-enabled"sv },
      { "magnet_link"sv, "magnetLink"sv },
      { "manual_announce_time"sv, "manualAnnounceTime"sv },
      { "max_connected_peers"sv, "maxConnectedPeers"sv },
      { "memory_bytes"sv, "memory-bytes"sv },
      { "memory_units"sv, "memory-units"sv },
      { "message"sv, "message"sv },
      { "metadata_percent_complete"sv, "metadataPercentComplete"sv },
      { "metainfo"sv, "metainfo"sv },
      { "move"sv, "move"sv },
      { "name"sv, "name"sv },
      { "next_announce_time"sv, "nextAnnounceTime"sv },
      { "next_scrape_time"sv, "nextScrapeTime"sv },
      { "path"sv, "path"sv },
      { "paused"sv, "paused"sv },
      { "paused_torrent_count"sv, "pausedTorrentCount"sv },
      { "peer_id"sv, "peer_id"sv },
      { "peer_is_choked"sv, "peerIsChoked"sv },
      { "peer_is_interested"sv, "peerIsInterested"sv },
      { "peer_limit"sv, "peer-limit"sv },
      { "peer_limit_global"sv, "peer-limit-global"sv },
      { "peer_limit_per_torrent"sv, "peer-limit-per-torrent"sv },
      { "peer_port"sv, "peer-port"sv },
      { "peer_port_random_on_start"sv, "peer-port-random-on-start"sv },
      { "peers"sv, "peers"sv },
      { "peers_connected"sv, "peersConnected"sv },
      { "peers_from"sv, "peersFrom"sv },
      { "peers_getting_from_us"sv, "peersGettingFromUs"sv },
      { "peers_sending_to_us"sv, "peersSendingToUs"sv },
      { "percent_complete"sv, "percentComplete"sv },
      { "percent_done"sv, "percentDone"sv },
      { "pex_enabled"sv, "pex-enabled"sv },
      { "piece_count"sv, "pieceCount"sv },
      { "piece_size"sv, "pieceSize"sv },
      { "pieces"sv, "pieces"sv },
      { "port"sv, "port"sv },
      { "port_forwarding_enabled"sv, "port-forwarding-enabled"sv },
      { "port_is_open"sv, "port-is-open"sv },
      { "preferred_transports"sv, "preferred_transports"sv }, // camelCase in nightly builds pre-7b83c7d6
      { "primary_mime_type"sv, "primary-mime-type"sv },
      { "priorities"sv, "priorities"sv },
      { "priority"sv, "priority"sv },
      { "priority_high"sv, "priority-high"sv },
      { "priority_low"sv, "priority-low"sv },
      { "priority_normal"sv, "priority-normal"sv },
      { "progress"sv, "progress"sv },
      { "queue_position"sv, "queuePosition"sv },
      { "queue_stalled_enabled"sv, "queue-stalled-enabled"sv },
      { "queue_stalled_minutes"sv, "queue-stalled-minutes"sv },
      { "rate_download"sv, "rateDownload"sv },
      { "rate_to_client"sv, "rateToClient"sv },
      { "rate_to_peer"sv, "rateToPeer"sv },
      { "rate_upload"sv, "rateUpload"sv },
      { "recently_active"sv, "recently-active"sv },
      { "recheck_progress"sv, "recheckProgress"sv },
      { "removed"sv, "removed"sv },
      { "rename_partial_files"sv, "rename-partial-files"sv },
      { "reqq"sv, "reqq"sv },
      { "result"sv, "result"sv },
      { "rpc_host_whitelist"sv, "rpc-host-whitelist"sv },
      { "rpc_host_whitelist_enabled"sv, "rpc-host-whitelist-enabled"sv },
      { "rpc_version"sv, "rpc-version"sv },
      { "rpc_version_minimum"sv, "rpc-version-minimum"sv },
      { "rpc_version_semver"sv, "rpc-version-semver"sv },
      { "scrape"sv, "scrape"sv },
      { "scrape_state"sv, "scrapeState"sv },
      { "script_torrent_added_enabled"sv, "script-torrent-added-enabled"sv },
      { "script_torrent_added_filename"sv, "script-torrent-added-filename"sv },
      { "script_torrent_done_enabled"sv, "script-torrent-done-enabled"sv },
      { "script_torrent_done_filename"sv, "script-torrent-done-filename"sv },
      { "script_torrent_done_seeding_enabled"sv, "script-torrent-done-seeding-enabled"sv },
      { "script_torrent_done_seeding_filename"sv, "script-torrent-done-seeding-filename"sv },
      { "seconds_active"sv, "secondsActive"sv }, // TODO(ckerr) legacy duplicate
      { "seconds_downloading"sv, "secondsDownloading"sv },
      { "seconds_seeding"sv, "secondsSeeding"sv },
      { "seed_idle_limit"sv, "seedIdleLimit"sv },
      { "seed_idle_mode"sv, "seedIdleMode"sv },
      { "seed_queue_enabled"sv, "seed-queue-enabled"sv },
      { "seed_queue_size"sv, "seed-queue-size"sv },
      { "seed_ratio_limit"sv, "seedRatioLimit"sv },
      { "seed_ratio_limited"sv, "seedRatioLimited"sv },
      { "seed_ratio_mode"sv, "seedRatioMode"sv },
      { "seeder_count"sv, "seederCount"sv },
      { "sequential_download"sv, "sequential_download"sv }, // camelCase in nightly builds pre-7b83c7d6
      { "sequential_download_from_piece"sv, "sequential_download_from_piece"sv },
      { "session_count"sv, "sessionCount"sv }, // TODO(ckerr) legacy duplicate
      { "session_id"sv, "session-id"sv },
      { "sitename"sv, "sitename"sv },
      { "size_bytes"sv, "size-bytes"sv },
      { "size_units"sv, "size-units"sv },
      { "size_when_done"sv, "sizeWhenDone"sv },
      { "speed_bytes"sv, "speed-bytes"sv },
      { "speed_limit_down"sv, "speed-limit-down"sv },
      { "speed_limit_down_enabled"sv, "speed-limit-down-enabled"sv },
      { "speed_limit_up"sv, "speed-limit-up"sv },
      { "speed_limit_up_enabled"sv, "speed-limit-up-enabled"sv },
      { "speed_units"sv, "speed-units"sv },
      { "start_added_torrents"sv, "start-added-torrents"sv },
      { "start_date"sv, "startDate"sv },
      { "status"sv, "status"sv },
      { "tcp_enabled"sv, "tcp-enabled"sv },
      { "tier"sv, "tier"sv },
      { "torrent_added"sv, "torrent-added"sv },
      { "torrent_count"sv, "torrentCount"sv },
      { "torrent_duplicate"sv, "torrent-duplicate"sv },
      { "torrent_file"sv, "torrentFile"sv },
      { "torrents"sv, "torrents"sv },
      { "total_size"sv, "totalSize"sv },
      { "tracker_add"sv, "trackerAdd"sv },
      { "tracker_list"sv, "trackerList"sv },
      { "tracker_remove"sv, "trackerRemove"sv },
      { "tracker_replace"sv, "trackerReplace"sv },
      { "tracker_stats"sv, "trackerStats"sv },
      { "trackers"sv, "trackers"sv },
      { "trash_original_torrent_files"sv, "trash-original-torrent-files"sv },
      { "units"sv, "units"sv },
      { "upload_limit"sv, "uploadLimit"sv },
      { "upload_limited"sv, "uploadLimited"sv },
      { "upload_ratio"sv, "uploadRatio"sv },
      { "upload_speed"sv, "uploadSpeed"sv },
      { "uploaded_bytes"sv, "uploadedBytes"sv }, // TODO(ckerr) legacy duplicate
      { "uploaded_ever"sv, "uploadedEver"sv },
      { "utp_enabled"sv, "utp-enabled"sv },
      { "version"sv, "version"sv },
      { "wanted"sv, "wanted"sv },
      { "webseeds"sv, "webseeds"sv },
      { "webseeds_sending_to_us"sv, "webseedsSendingToUs"sv },
      { "blocklist_update"sv, "blocklist-update"sv },
      { "free_space"sv, "free-space"sv },
      { "group_get"sv, "group-get"sv },
      { "group_set"sv, "group-set"sv },
      { "port_test"sv, "port-test"sv },
      { "queue_move_bottom"sv, "queue-move-bottom"sv },
      { "queue_move_down"sv, "queue-move-down"sv },
      { "queue_move_top"sv, "queue-move-top"sv },
      { "queue_move_up"sv, "queue-move-up"sv },
      { "session_close"sv, "session-close"sv },
      { "session_get"sv, "session-get"sv },
      { "session_set"sv, "session-set"sv },
      { "session_stats"sv, "session-stats"sv },
      { "torrent_add"sv, "torrent-add"sv },
      { "torrent_get"sv, "torrent-get"sv },
      { "torrent_reannounce"sv, "torrent-reannounce"sv },
      { "torrent_remove"sv, "torrent-remove"sv },
      { "torrent_rename_path"sv, "torrent-rename-path"sv },
      { "torrent_set"sv, "torrent-set"sv },
      { "torrent_set_location"sv, "torrent-set-location"sv },
      { "torrent_start"sv, "torrent-start"sv },
      { "torrent_start_now"sv, "torrent-start-now"sv },
      { "torrent_stop"sv, "torrent-stop"sv },
      { "torrent_verify"sv, "torrent-verify"sv },

      // json-rpc
      { "code"sv, "code"sv }, // string unused in legacy
      { "data"sv, "data"sv }, // string unused in legacy
      { "jsonrpc"sv, "jsonrpc"sv }, // string unused in legacy
      { "message"sv, "message"sv }, // string unused in legacy
      { "params"sv, "arguments"sv }, // legacy JSON-RPC alias
      { "method"sv, "method"sv } }
};

auto constexpr SessionKeys = std::array<ApiKey, 311U>{ {
    // BT protocol
    { "announce-list"sv, "announce-list"sv }, // BEP0012
    { "added"sv, "added"sv }, // BEP0011
    { "added.f"sv, "added.f"sv }, // BEP0011
    { "added6"sv, "added6"sv }, // BEP0011
    { "added6.f"sv, "added6.f"sv }, // BEP0011
    { "announce"sv, "announce"sv }, // BEP0003
    { "complete"sv, "complete"sv }, // BEP0048
    { "downloaded"sv, "downloaded"sv }, // BEP0048
    { "dropped"sv, "dropped"sv }, // BEP0011
    { "dropped6"sv, "dropped6"sv }, // BEP0011
    { "e"sv, "e"sv },
    { "incomplete"sv, "incomplete"sv }, // BEP0048
    { "ipv4"sv, "ipv4"sv }, // BEP0010
    { "ipv6"sv, "ipv6"sv }, // BEP0010
    { "m"sv, "m"sv }, // BEP0010, BEP0011
    { "metadata_size"sv, "metadata_size"sv }, // BEP0009
    { "msg_type"sv, "msg_type"sv },
    { "p"sv, "p"sv }, // BEP0010
    { "piece"sv, "piece"sv },
    { "reqq"sv, "reqq"sv }, // BEP0010
    { "total_size"sv, "total_size"sv },
    { "upload_only"sv, "upload_only"sv }, // BEP0021
    { "ut_holepunch"sv, "ut_holepunch"sv },
    { "ut_metadata"sv, "ut_metadata"sv }, // BEP0011
    { "ut_pex"sv, "ut_pex"sv }, // BEP0010, BEP0011
    { "v"sv, "v"sv }, // BEP0010
    { "yourip"sv, "yourip"sv }, // BEP0010

    // .torrent
    { "length"sv, "length"sv },
    { "url-list"sv, "url-list"sv },
    { "comment"sv, "comment"sv },
    { "created by"sv, "created by"sv },
    { "creation date"sv, "creation date"sv },
    { "encoding"sv, "encoding"sv },
    { "path"sv, "path"sv },
    { "files"sv, "files"sv },
    { "name"sv, "name"sv },
    { "piece length"sv, "piece length"sv },
    { "pieces"sv, "pieces"sv },
    { "private"sv, "private"sv },
    { "source"sv, "source"sv },
    { "info"sv, "info"sv },

    // .resume
    { "peers2"sv, "peers2"sv },
    { "peers2_6"sv, "peers2-6"sv },
    { "peers2_6"sv, "peers2-6"sv },
    { "socket_address"sv, "socket_address"sv }, // for pex
    { "flags"sv, "flags"sv }, // for pex
    { "labels"sv, "labels"sv },
    { "group"sv, "group"sv },
    { "dnd"sv, "dnd"sv },
    { "priority"sv, "priority"sv },
    { "speed_Bps"sv, "speed-Bps"sv },
    { "use_global_speed_limit"sv, "use-global-speed-limit"sv },
    { "use_speed_limit"sv, "use-speed-limit"sv },
    { "speed_limit_down"sv, "speed-limit-down"sv },
    { "speed_limit_up"sv, "speed-limit-up"sv },
    { "ratio_limit"sv, "ratio-limit"sv },
    { "ratio_mode"sv, "ratio-mode"sv },
    { "idle_limit"sv, "idle-limit"sv },
    { "idle_mode"sv, "idle-mode"sv },
    { "speed_Bps"sv, "speed-Bps"sv },
    { "speed"sv, "speed"sv },
    { "use_speed_limit"sv, "use-speed-limit"sv },
    { "use_global_speed_limit"sv, "use-global-speed-limit"sv },
    { "speed_limit_up"sv, "speed-limit-up"sv },
    { "speed_limit_down"sv, "speed-limit-down"sv },
    { "ratio_limit"sv, "ratio-limit"sv },
    { "ratio_mode"sv, "ratio-mode"sv },
    { "idle_limit"sv, "idle-limit"sv },
    { "idle_mode"sv, "idle-mode"sv },
    { "name"sv, "name"sv },
    { "files"sv, "files"sv },
    { "mtimes"sv, "mtimes"sv },
    { "pieces"sv, "pieces"sv },
    { "blocks"sv, "blocks"sv },
    { "progress"sv, "progress"sv },
    { "time_checked"sv, "time-checked"sv },
    { "bitfield"sv, "bitfield"sv },
    { "corrupt"sv, "corrupt"sv },
    { "destination"sv, "destination"sv },
    { "incomplete_dir"sv, "incomplete-dir"sv },
    { "incomplete_dir"sv, "incomplete-dir"sv },
    { "downloaded"sv, "downloaded"sv },
    { "uploaded"sv, "uploaded"sv },
    { "max_peers"sv, "max-peers"sv },
    { "max_peers"sv, "max-peers"sv },
    { "paused"sv, "paused"sv },
    { "added_date"sv, "added-date"sv }, // TODO(ckerr) legacy duplicate
    { "added_date"sv, "added-date"sv }, // TODO(ckerr) legacy duplicate
    { "done_date"sv, "done-date"sv }, // TODO(ckerr) legacy duplicate
    { "done_date"sv, "done-date"sv }, // TODO(ckerr) legacy duplicate
    { "activity_date"sv, "activity-date"sv }, // TODO(ckerr) legacy duplicate
    { "activity_date"sv, "activity-date"sv }, // TODO(ckerr) legacy duplicate
    { "seeding_time_seconds"sv, "seeding-time-seconds"sv },
    { "seeding_time_seconds"sv, "seeding-time-seconds"sv },
    { "downloading_time_seconds"sv, "downloading-time-seconds"sv },
    { "downloading_time_seconds"sv, "downloading-time-seconds"sv },
    { "bandwidth_priority"sv, "bandwidth-priority"sv }, // TODO(ckerr) legacy duplicate
    { "bandwidth_priority"sv, "bandwidth-priority"sv }, // TODO(ckerr) legacy duplicate
    { "sequential_download"sv, "sequential_download"sv },
    { "sequential_download_from_piece"sv, "sequential_download_from_piece"sv },

    // stats.json
    { "downloaded_bytes"sv, "downloaded-bytes"sv }, // TODO(ckerr) legacy duplicate
    { "files_added"sv, "files-added"sv }, // TODO(ckerr) legacy duplicate
    { "seconds_active"sv, "seconds-active"sv }, // TODO(ckerr) legacy duplicate
    { "session_count"sv, "session-count"sv }, // TODO(ckerr) legacy duplicate
    { "uploaded_bytes"sv, "uploaded-bytes"sv }, // TODO(ckerr) legacy duplicate

    // dht.dat
    { "id"sv, "id"sv },
    { "id_timestamp"sv, "id_timestamp"sv },
    { "nodes"sv, "nodes"sv },
    { "nodes6"sv, "nodes6"sv },

    // tr_session::Settings
    { "announce_ip"sv, "announce-ip"sv },
    { "announce_ip_enabled"sv, "announce-ip-enabled"sv },
    { "bind_address_ipv4"sv, "bind-address-ipv4"sv },
    { "bind_address_ipv6"sv, "bind-address-ipv6"sv },
    { "blocklist_enabled"sv, "blocklist-enabled"sv },
    { "blocklist_url"sv, "blocklist-url"sv },
    { "cache_size_mb"sv, "cache-size-mb"sv },
    { "default_trackers"sv, "default-trackers"sv },
    { "dht_enabled"sv, "dht-enabled"sv },
    { "download_dir"sv, "download-dir"sv }, // TODO(ckerr) legacy duplicate
    { "download_queue_enabled"sv, "download-queue-enabled"sv },
    { "download_queue_size"sv, "download-queue-size"sv },
    { "encryption"sv, "encryption"sv },
    { "idle_seeding_limit"sv, "idle-seeding-limit"sv },
    { "idle_seeding_limit_enabled"sv, "idle-seeding-limit-enabled"sv },
    { "incomplete_dir"sv, "incomplete-dir"sv },
    { "incomplete_dir_enabled"sv, "incomplete-dir-enabled"sv },
    { "lpd_enabled"sv, "lpd-enabled"sv },
    { "message_level"sv, "message-level"sv },
    { "peer_congestion_algorithm"sv, "peer-congestion-algorithm"sv },
    { "peer_limit_global"sv, "peer-limit-global"sv },
    { "peer_limit_per_torrent"sv, "peer-limit-per-torrent"sv },
    { "peer_port"sv, "peer-port"sv },
    { "peer_port_random_high"sv, "peer-port-random-high"sv },
    { "peer_port_random_low"sv, "peer-port-random-low"sv },
    { "peer_port_random_on_start"sv, "peer-port-random-on-start"sv },
    { "peer_socket_tos"sv, "peer-socket-tos"sv },
    { "pex_enabled"sv, "pex-enabled"sv },
    { "port_forwarding_enabled"sv, "port-forwarding-enabled"sv },
    { "preallocation"sv, "preallocation"sv },
    { "preferred_transports"sv, "preferred_transports"sv },
    { "proxy_url"sv, "proxy_url"sv },
    { "queue_stalled_enabled"sv, "queue-stalled-enabled"sv },
    { "queue_stalled_minutes"sv, "queue-stalled-minutes"sv },
    { "ratio_limit"sv, "ratio-limit"sv },
    { "ratio_limit_enabled"sv, "ratio-limit-enabled"sv },
    { "rename_partial_files"sv, "rename-partial-files"sv },
    { "reqq"sv, "reqq"sv },
    { "scrape_paused_torrents_enabled"sv, "scrape-paused-torrents-enabled"sv },
    { "script_torrent_added_enabled"sv, "script-torrent-added-enabled"sv },
    { "script_torrent_added_filename"sv, "script-torrent-added-filename"sv },
    { "script_torrent_done_enabled"sv, "script-torrent-done-enabled"sv },
    { "script_torrent_done_filename"sv, "script-torrent-done-filename"sv },
    { "script_torrent_done_seeding_enabled"sv, "script-torrent-done-seeding-enabled"sv },
    { "script_torrent_done_seeding_filename"sv, "script-torrent-done-seeding-filename"sv },
    { "seed_queue_enabled"sv, "seed-queue-enabled"sv },
    { "seed_queue_size"sv, "seed-queue-size"sv },
    { "sequential_download"sv, "sequential_download"sv },
    { "sleep_per_seconds_during_verify"sv, "sleep_per_seconds_during_verify"sv }, // kebab-case in 4.1.0-beta.1
    { "speed_limit_down"sv, "speed-limit-down"sv },
    { "speed_limit_down_enabled"sv, "speed-limit-down-enabled"sv },
    { "speed_limit_up"sv, "speed-limit-up"sv },
    { "speed_limit_up_enabled"sv, "speed-limit-up-enabled"sv },
    { "start_added_torrents"sv, "start-added-torrents"sv },
    { "tcp_enabled"sv, "tcp-enabled"sv },
    { "torrent_added_verify_mode"sv, "torrent-added-verify-mode"sv },
    { "torrent_complete_verify_enabled"sv, "torrent_complete_verify_enabled"sv },
    { "trash_original_torrent_files"sv, "trash-original-torrent-files"sv },
    { "umask"sv, "umask"sv },
    { "upload_slots_per_torrent"sv, "upload-slots-per-torrent"sv },
    { "utp_enabled"sv, "utp-enabled"sv },

    // rpc server settings
    { "anti_brute_force_enabled"sv, "anti-brute-force-enabled"sv },
    { "anti_brute_force_threshold"sv, "anti-brute-force-threshold"sv },
    { "rpc_authentication_required"sv, "rpc-authentication-required"sv },
    { "rpc_bind_address"sv, "rpc-bind-address"sv },
    { "rpc_enabled"sv, "rpc-enabled"sv },
    { "rpc_host_whitelist"sv, "rpc-host-whitelist"sv },
    { "rpc_host_whitelist_enabled"sv, "rpc-host-whitelist-enabled"sv },
    { "rpc_port"sv, "rpc-port"sv },
    { "rpc_password"sv, "rpc-password"sv },
    { "rpc_socket_mode"sv, "rpc-socket-mode"sv },
    { "rpc_url"sv, "rpc-url"sv },
    { "rpc_username"sv, "rpc-username"sv },
    { "rpc_whitelist"sv, "rpc-whitelist"sv },
    { "rpc_whitelist_enabled"sv, "rpc-whitelist-enabled"sv },

    // speed settings
    { "alt_speed_enabled"sv, "alt-speed-enabled"sv },
    { "alt_speed_up"sv, "alt-speed-up"sv },
    { "alt_speed_down"sv, "alt-speed-down"sv },
    { "alt_speed_time_enabled"sv, "alt-speed-time-enabled"sv },
    { "alt_speed_time_day"sv, "alt-speed-time-day"sv },
    { "alt_speed_time_begin"sv, "alt-speed-time-begin"sv },
    { "alt_speed_time_end"sv, "alt-speed-time-end"sv },

    // qt app
    { "show_options_window"sv, "show-options-window"sv },
    { "open_dialog_dir"sv, "open-dialog-dir"sv },
    { "inhibit_desktop_hibernation"sv, "inhibit-desktop-hibernation"sv },
    { "watch_dir"sv, "watch-dir"sv },
    { "watch_dir_enabled"sv, "watch-dir-enabled"sv },
    { "show_notification_area_icon"sv, "show-notification-area-icon"sv },
    { "start_minimized"sv, "start-minimized"sv },
    { "torrent_added_notification_enabled"sv, "torrent-added-notification-enabled"sv },
    { "torrent_complete_notification_enabled"sv, "torrent-complete-notification-enabled"sv },
    { "prompt_before_exit"sv, "prompt-before-exit"sv },
    { "sort_mode"sv, "sort-mode"sv },
    { "sort_reversed"sv, "sort-reversed"sv },
    { "compact_view"sv, "compact-view"sv },
    { "show_filterbar"sv, "show-filterbar"sv },
    { "show_statusbar"sv, "show-statusbar"sv },
    { "statusbar_stats"sv, "statusbar-stats"sv },
    { "show_tracker_scrapes"sv, "show-tracker-scrapes"sv },
    { "show_backup_trackers"sv, "show-backup-trackers"sv },
    { "show_toolbar"sv, "show-toolbar"sv },
    { "blocklist_date"sv, "blocklist-date"sv },
    { "blocklist_updates_enabled"sv, "blocklist-updates-enabled"sv },
    { "main_window_layout_order"sv, "main-window-layout-order"sv },
    { "main_window_height"sv, "main-window-height"sv },
    { "main_window_width"sv, "main-window-width"sv },
    { "main_window_x"sv, "main-window-x"sv },
    { "main_window_y"sv, "main-window-y"sv },
    { "filter_mode"sv, "filter-mode"sv },
    { "filter_trackers"sv, "filter-trackers"sv },
    { "filter_text"sv, "filter-text"sv },
    { "remote_session_enabled"sv, "remote-session-enabled"sv },
    { "remote_session_host"sv, "remote-session-host"sv },
    { "remote_session_https"sv, "remote-session-https"sv },
    { "remote_session_password"sv, "remote-session-password"sv },
    { "remote_session_port"sv, "remote-session-port"sv },
    { "remote_session_requires_authentication"sv, "remote-session-requres-authentication"sv },
    { "remote_session_username"sv, "remote-session-username"sv },
    { "remote_session_rpc_url_path"sv, "remote_session_rpc_url_path"sv },
    { "torrent_complete_sound_command"sv, "torrent-complete-sound-command"sv },
    { "torrent_complete_sound_enabled"sv, "torrent-complete-sound-enabled"sv },
    { "user_has_given_informed_consent"sv, "user-has-given-informed-consent"sv },
    { "read_clipboard"sv, "read-clipboard"sv },

    // daemon
    { "bind_address_ipv4"sv, "bind-address-ipv4"sv },
    { "bind_address_ipv6"sv, "bind-address-ipv6"sv },
    { "blocklist_enabled"sv, "blocklist-enabled"sv },
    { "default_trackers"sv, "default-trackers"sv },
    { "dht_enabled"sv, "dht-enabled"sv },
    { "download_dir"sv, "download-dir"sv }, // TODO(ckerr) legacy duplicate
    { "encryption"sv, "encryption"sv },
    { "incomplete_dir"sv, "incomplete-dir"sv },
    { "incomplete_dir_enabled"sv, "incomplete-dir-enabled"sv },
    { "lpd_enabled"sv, "lpd-enabled"sv },
    { "message_level"sv, "message-level"sv },
    { "peer_limit_global"sv, "peer-limit-global"sv },
    { "peer_limit_per_torrent"sv, "peer-limit-per-torrent"sv },
    { "peer_port"sv, "peer-port"sv },
    { "pidfile"sv, "pidfile"sv },
    { "port_forwarding_enabled"sv, "port-forwarding-enabled"sv },
    { "ratio_limit"sv, "ratio-limit"sv },
    { "ratio_limit_enabled"sv, "ratio-limit-enabled"sv },
    { "rpc_authentication_required"sv, "rpc-authentication-required"sv },
    { "rpc_bind_address"sv, "rpc-bind-address"sv },
    { "rpc_enabled"sv, "rpc-enabled"sv },
    { "rpc_password"sv, "rpc-password"sv },
    { "rpc_port"sv, "rpc-port"sv },
    { "rpc_username"sv, "rpc-username"sv },
    { "rpc_whitelist"sv, "rpc-whitelist"sv },
    { "rpc_whitelist_enabled"sv, "rpc-whitelist-enabled"sv },
    { "sequential_download"sv, "sequential_download"sv },
    { "start_paused"sv, "start_paused"sv },
    { "utp_enabled"sv, "utp-enabled"sv },
    { "watch_dir"sv, "watch-dir"sv },
    { "watch_dir_enabled"sv, "watch-dir-enabled"sv },
    { "watch_dir_force_generic"sv, "watch-dir-force-generic"sv },

    // gtk app
    { "blocklist_updates_enabled"sv, "blocklist-updates-enabled"sv },
    { "compact_view"sv, "compact-view"sv },
    { "details_window_height"sv, "details-window-height"sv },
    { "details_window_width"sv, "details-window-width"sv },
    { "download_dir"sv, "download-dir"sv }, // TODO(ckerr) legacy duplicate
    { "inhibit_desktop_hibernation"sv, "inhibit-desktop-hibernation"sv },
    { "main_window_height"sv, "main-window-height"sv },
    { "main_window_is_maximized"sv, "main-window-is-maximized"sv },
    { "main_window_width"sv, "main-window-width"sv },
    { "main_window_x"sv, "main-window-x"sv },
    { "main_window_y"sv, "main-window-y"sv },
    { "open_dialog_dir"sv, "open-dialog-dir"sv },
    { "show_backup_trackers"sv, "show-backup-trackers"sv },
    { "show_extra_peer_details"sv, "show-extra-peer-details"sv },
    { "show_filterbar"sv, "show-filterbar"sv },
    { "show_notification_area_icon"sv, "show-notification-area-icon"sv },
    { "show_options_window"sv, "show-options-window"sv },
    { "show_statusbar"sv, "show-statusbar"sv },
    { "show_toolbar"sv, "show-toolbar"sv },
    { "show_tracker_scrapes"sv, "show-tracker-scrapes"sv },
    { "sort_mode"sv, "sort-mode"sv },
    { "sort_reversed"sv, "sort-reversed"sv },
    { "statusbar_stats"sv, "statusbar-stats"sv },
    { "torrent_added_notification_enabled"sv, "torrent-added-notification-enabled"sv },
    { "torrent_complete_notification_enabled"sv, "torrent-complete-notification-enabled"sv },
    { "torrent_complete_sound_enabled"sv, "torrent-complete-sound-enabled"sv },
    { "trash_can_enabled"sv, "trash-can-enabled"sv },
    { "user_has_given_informed_consent"sv, "user-has-given-informed-consent"sv },
    { "watch_dir"sv, "watch-dir"sv },
    { "watch_dir_enabled"sv, "watch-dir-enabled"sv },
    { "torrent_complete_sound_command"sv, "torrent-complete-sound-command"sv },
    { "incomplete_dir"sv, "incomplete-dir"sv },
    { "alt_speed_enabled"sv, "alt-speed-enabled"sv },
    { "peer_port"sv, "peer-port"sv },
    { "blocklist_enabled"sv, "blocklist-enabled"sv },
    { "blocklist_date"sv, "blocklist-date"sv },
    { "start_added_torrents"sv, "start-added-torrents"sv },
    { "alt_speed_down"sv, "alt-speed-down"sv },
    { "alt_speed_up"sv, "alt-speed-up"sv },
    { "speed_limit_down"sv, "speed-limit-down"sv },
    { "speed_limit_up"sv, "speed-limit-up"sv },
    { "ratio_limit"sv, "ratio-limit"sv },
    { "message_level"sv, "message-level"sv },
    { "rpc_port"sv, "rpc-port"sv },
    { "rpc_whitelist"sv, "rpc-whitelist"sv },
    { "trash_original_torrent_files"sv, "trash-original-torrent-files"sv },
    { "peer_limit_per_torrent"sv, "peer-limit-per-torrent"sv },
} };

// ---

template<typename Predicate>
[[nodiscard]] bool walk_variant(tr_variant const& value, Predicate const& predicate);

template<typename Predicate>
[[nodiscard]] bool walk_map(tr_variant::Map const& map, Predicate const& predicate)
{
    for (auto const& [key, child] : map)
    {
        if (!predicate(tr_quark_get_string_view(key)))
        {
            return false;
        }

        if (!walk_variant(child, predicate))
        {
            return false;
        }
    }

    return true;
}

template<typename Predicate>
[[nodiscard]] bool walk_variant(tr_variant const& value, Predicate const& predicate)
{
    if (auto const* map = value.get_if<tr_variant::Map>())
    {
        return walk_map(*map, predicate);
    }

    if (auto const* vec = value.get_if<tr_variant::Vector>())
    {
        for (auto const& item : *vec)
        {
            if (!walk_variant(item, predicate))
            {
                return false;
            }
        }
    }

    return true;
}

template<size_t N>
[[nodiscard]] bool matches(std::array<ApiKey, N> const& keys, std::string_view ApiKey::* member, std::string_view name)
{
    return std::any_of(std::begin(keys), std::end(keys), [member, name](ApiKey const& key) { return key.*member == name; });
}

// ---

namespace detect_style_helpers
{
[[nodiscard]] bool is_current_key(std::string_view const name)
{
    return matches(RpcKeys, &ApiKey::current, name) || matches(SessionKeys, &ApiKey::current, name);
}

[[nodiscard]] bool is_legacy_rpc_key(std::string_view const name)
{
    return matches(RpcKeys, &ApiKey::legacy, name);
}

[[nodiscard]] bool is_legacy_settings_key(std::string_view const name)
{
    return matches(SessionKeys, &ApiKey::legacy, name);
}
} // namespace detect_style_helpers
} // namespace

/**
 * Detect whether this payload is using Legacy or Current style.
 * Used for ensuring we return RPC replies in style of the
 * corresponding request.
 */
[[nodiscard]] std::optional<Style> detect_style(tr_variant const& src)
{
    using namespace detect_style_helpers;

    if (walk_variant(src, is_current_key))
        return Style::Current;

    if (walk_variant(src, is_legacy_rpc_key))
        return Style::LegacyRpc;

    if (walk_variant(src, is_legacy_settings_key))
        return Style::LegacySettings;

    return {};
}

[[nodiscard]] tr_variant apply_style(tr_variant const& src, Style const tgt_style)
{
    return src.cloneToStyle(tgt_style);
}

} // namespace libtransmission::api_compat
