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

tr_quark tr_quark_convert(tr_quark const key)
{
    using namespace libtransmission::api_compat;
    return convert(key, Style::Current);
}
