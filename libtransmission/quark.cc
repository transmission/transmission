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
#include "libtransmission/string-utils.h"

using namespace std::literals;

namespace
{

auto constexpr MyStatic = std::array<std::u8string_view, TR_N_KEYS>{
    u8""sv,
    u8"activeTorrentCount"sv, // rpc
    u8"active_torrent_count"sv, // rpc
    u8"activity-date"sv, // .resume
    u8"activityDate"sv, // rpc
    u8"activity_date"sv, // .resume, rpc
    u8"added"sv, // BEP0011; BT protocol, rpc
    u8"added-date"sv, // .resume
    u8"added.f"sv, // BEP0011; BT protocol
    u8"added6"sv, // BEP0011; BT protocol
    u8"added6.f"sv, // BEP0011; BT protocol
    u8"addedDate"sv, // rpc
    u8"added_date"sv, // .resume, rpc
    u8"address"sv, // rpc
    u8"alt-speed-down"sv, // gtk app, rpc, speed settings
    u8"alt-speed-enabled"sv, // gtk app, rpc, speed settings
    u8"alt-speed-time-begin"sv, // rpc, speed settings
    u8"alt-speed-time-day"sv, // rpc, speed settings
    u8"alt-speed-time-enabled"sv, // rpc, speed settings
    u8"alt-speed-time-end"sv, // rpc, speed settings
    u8"alt-speed-up"sv, // gtk app, rpc, speed settings
    u8"alt_speed_down"sv, // gtk app, rpc, speed settings
    u8"alt_speed_enabled"sv, // gtk app, rpc, speed settings
    u8"alt_speed_time_begin"sv, // rpc, speed settings
    u8"alt_speed_time_day"sv, // rpc, speed settings
    u8"alt_speed_time_enabled"sv, // rpc, speed settings
    u8"alt_speed_time_end"sv, // rpc, speed settings
    u8"alt_speed_up"sv, // gtk app, rpc, speed settings
    u8"announce"sv, // BEP0003; BT protocol
    u8"announce-ip"sv, // tr_session::Settings
    u8"announce-ip-enabled"sv, // tr_session::Settings
    u8"announce-list"sv, // BEP0012; BT protocol
    u8"announceState"sv, // rpc
    u8"announce_ip"sv, // tr_session::Settings
    u8"announce_ip_enabled"sv, // tr_session::Settings
    u8"announce_state"sv, // rpc
    u8"anti-brute-force-enabled"sv, // rpc, rpc server settings
    u8"anti-brute-force-threshold"sv, // rpc server settings
    u8"anti_brute_force_enabled"sv, // rpc, rpc server settings
    u8"anti_brute_force_threshold"sv, // rpc server settings
    u8"arguments"sv, // json-rpc
    u8"availability"sv, // rpc
    u8"bandwidth-priority"sv, // .resume
    u8"bandwidthPriority"sv, // rpc
    u8"bandwidth_priority"sv, // .resume, rpc
    u8"begin_piece"sv, // rpc
    u8"bind-address-ipv4"sv, // daemon, tr_session::Settings
    u8"bind-address-ipv6"sv, // daemon, tr_session::Settings
    u8"bind_address_ipv4"sv, // daemon, tr_session::Settings
    u8"bind_address_ipv6"sv, // daemon, tr_session::Settings
    u8"bitfield"sv, // .resume
    u8"blocklist-date"sv, // gtk app, qt app
    u8"blocklist-enabled"sv, // daemon, gtk app, rpc, tr_session::Settings
    u8"blocklist-size"sv, // rpc
    u8"blocklist-update"sv, // rpc
    u8"blocklist-updates-enabled"sv, // gtk app, qt app
    u8"blocklist-url"sv, // rpc, tr_session::Settings
    u8"blocklist_date"sv, // gtk app, qt app
    u8"blocklist_enabled"sv, // daemon, gtk app, rpc, tr_session::Settings
    u8"blocklist_size"sv, // rpc
    u8"blocklist_update"sv, // rpc
    u8"blocklist_updates_enabled"sv, // gtk app, qt app
    u8"blocklist_url"sv, // rpc, tr_session::Settings
    u8"blocks"sv, // .resume
    u8"bytesCompleted"sv, // rpc
    u8"bytes_completed"sv, // rpc
    u8"bytes_to_client"sv, // rpc
    u8"bytes_to_peer"sv, // rpc
    u8"cache-size-mb"sv, // rpc, tr_session::Settings
    u8"cache_size_mib"sv, // rpc, tr_session::Settings
    u8"clientIsChoked"sv, // rpc
    u8"clientIsInterested"sv, // rpc
    u8"clientName"sv, // rpc
    u8"client_is_choked"sv, // rpc
    u8"client_is_interested"sv, // rpc
    u8"client_name"sv, // rpc
    u8"code"sv, // json-rpc
    u8"comment"sv, // .torrent, rpc
    u8"compact-view"sv, // gtk app, qt app
    u8"compact_view"sv, // gtk app, qt app
    u8"complete"sv, // BEP0048; BT protocol
    u8"config-dir"sv, // rpc
    u8"config_dir"sv, // rpc
    u8"cookies"sv, // rpc
    u8"corrupt"sv, // .resume
    u8"corruptEver"sv, // rpc
    u8"corrupt_ever"sv, // rpc
    u8"created by"sv, // .torrent
    u8"creation date"sv, // .torrent
    u8"creator"sv, // rpc
    u8"cumulative-stats"sv, // rpc
    u8"cumulative_stats"sv, // rpc
    u8"current-stats"sv, // rpc
    u8"current_stats"sv, // rpc
    u8"data"sv, // json-rpc, rpc
    u8"dateCreated"sv, // rpc
    u8"date_created"sv, // rpc
    u8"default-trackers"sv, // daemon, rpc, tr_session::Settings
    u8"default_trackers"sv, // daemon, rpc, tr_session::Settings
    u8"delete-local-data"sv, // rpc
    u8"delete_local_data"sv, // rpc
    u8"desiredAvailable"sv, // rpc
    u8"desired_available"sv, // rpc
    u8"destination"sv, // .resume
    u8"details-window-height"sv, // gtk app
    u8"details-window-width"sv, // gtk app
    u8"details_window_height"sv, // gtk app
    u8"details_window_width"sv, // gtk app
    u8"dht-enabled"sv, // daemon, rpc, tr_session::Settings
    u8"dht_enabled"sv, // daemon, rpc, tr_session::Settings
    u8"dnd"sv, // .resume
    u8"done-date"sv, // .resume
    u8"doneDate"sv, // rpc
    u8"done_date"sv, // .resume, rpc
    u8"download-dir"sv, // daemon, gtk app, tr_session::Settings
    u8"download-dir-free-space"sv, // rpc
    u8"download-queue-enabled"sv, // rpc, tr_session::Settings
    u8"download-queue-size"sv, // rpc, tr_session::Settings
    u8"downloadCount"sv, // rpc
    u8"downloadDir"sv, // rpc
    u8"downloadLimit"sv, // rpc
    u8"downloadLimited"sv, // rpc
    u8"downloadSpeed"sv, // rpc
    u8"download_bytes_per_second"sv, // rpc
    u8"download_count"sv, // rpc
    u8"download_dir"sv, // daemon, gtk app, rpc, tr_session::Settings
    u8"download_dir_free_space"sv, // rpc
    u8"download_limit"sv, // rpc
    u8"download_limited"sv, // rpc
    u8"download_queue_enabled"sv, // rpc, tr_session::Settings
    u8"download_queue_size"sv, // rpc, tr_session::Settings
    u8"download_speed"sv, // rpc
    u8"downloaded"sv, // BEP0048; .resume, BT protocol
    u8"downloaded-bytes"sv, // stats.json
    u8"downloadedBytes"sv, // rpc
    u8"downloadedEver"sv, // rpc
    u8"downloaded_bytes"sv, // rpc, stats.json
    u8"downloaded_ever"sv, // rpc
    u8"downloader_count"sv, // rpc
    u8"downloading-time-seconds"sv, // .resume
    u8"downloading_time_seconds"sv, // .resume
    u8"dropped"sv, // BEP0011; BT protocol
    u8"dropped6"sv, // BEP0011; BT protocol
    u8"e"sv, // BT protocol
    u8"editDate"sv, // rpc
    u8"edit_date"sv, // rpc
    u8"encoding"sv, // .torrent
    u8"encryption"sv, // daemon, rpc, tr_session::Settings
    u8"end_piece"sv, // rpc
    u8"error"sv, // rpc
    u8"errorString"sv, // rpc
    u8"error_string"sv, // rpc
    u8"eta"sv, // rpc
    u8"etaIdle"sv, // rpc
    u8"eta_idle"sv, // rpc
    u8"fields"sv, // rpc
    u8"file-count"sv, // rpc
    u8"fileStats"sv, // rpc
    u8"file_count"sv, // rpc
    u8"file_stats"sv, // rpc
    u8"filename"sv, // rpc
    u8"files"sv, // .resume, .torrent, rpc
    u8"files-added"sv, // stats.json
    u8"files-unwanted"sv, // rpc
    u8"files-wanted"sv, // rpc
    u8"filesAdded"sv, // rpc
    u8"files_added"sv, // rpc, stats.json
    u8"files_unwanted"sv, // rpc
    u8"files_wanted"sv, // rpc
    u8"filter-mode"sv, // qt app
    u8"filter-text"sv, // qt app
    u8"filter-trackers"sv, // qt app
    u8"filter_mode"sv, // qt app
    u8"filter_text"sv, // qt app
    u8"filter_trackers"sv, // qt app
    u8"flagStr"sv, // rpc
    u8"flag_str"sv, // rpc
    u8"flags"sv, // .resume
    u8"format"sv, // rpc
    u8"free-space"sv, // rpc
    u8"free_space"sv, // rpc
    u8"fromCache"sv, // rpc
    u8"fromDht"sv, // rpc
    u8"fromIncoming"sv, // rpc
    u8"fromLpd"sv, // rpc
    u8"fromLtep"sv, // rpc
    u8"fromPex"sv, // rpc
    u8"fromTracker"sv, // rpc
    u8"from_cache"sv, // rpc
    u8"from_dht"sv, // rpc
    u8"from_incoming"sv, // rpc
    u8"from_lpd"sv, // rpc
    u8"from_ltep"sv, // rpc
    u8"from_pex"sv, // rpc
    u8"from_tracker"sv, // rpc
    u8"group"sv, // .resume, rpc
    u8"group-get"sv, // rpc
    u8"group-set"sv, // rpc
    u8"group_get"sv, // rpc
    u8"group_set"sv, // rpc
    u8"hasAnnounced"sv, // rpc
    u8"hasScraped"sv, // rpc
    u8"has_announced"sv, // rpc
    u8"has_scraped"sv, // rpc
    u8"hashString"sv, // rpc
    u8"hash_string"sv, // rpc
    u8"haveUnchecked"sv, // rpc
    u8"haveValid"sv, // rpc
    u8"have_unchecked"sv, // rpc
    u8"have_valid"sv, // rpc
    u8"honorsSessionLimits"sv, // rpc
    u8"honors_session_limits"sv, // rpc
    u8"host"sv, // rpc
    u8"id"sv, // dht.dat, rpc
    u8"id_timestamp"sv, // dht.dat
    u8"idle-limit"sv, // .resume
    u8"idle-mode"sv, // .resume
    u8"idle-seeding-limit"sv, // rpc, tr_session::Settings
    u8"idle-seeding-limit-enabled"sv, // rpc, tr_session::Settings
    u8"idle_limit"sv, // .resume
    u8"idle_mode"sv, // .resume
    u8"idle_seeding_limit"sv, // rpc, tr_session::Settings
    u8"idle_seeding_limit_enabled"sv, // rpc, tr_session::Settings
    u8"ids"sv, // rpc
    u8"incomplete"sv, // BEP0048; BT protocol
    u8"incomplete-dir"sv, // .resume, daemon, gtk app, rpc, tr_session::Settings
    u8"incomplete-dir-enabled"sv, // daemon, rpc, tr_session::Settings
    u8"incomplete_dir"sv, // .resume, daemon, gtk app, rpc, tr_session::Settings
    u8"incomplete_dir_enabled"sv, // daemon, rpc, tr_session::Settings
    u8"info"sv, // .torrent
    u8"inhibit-desktop-hibernation"sv, // gtk app, qt app
    u8"inhibit_desktop_hibernation"sv, // gtk app, qt app
    u8"ip_protocol"sv, // rpc
    u8"ipv4"sv, // BEP0010; BT protocol, rpc
    u8"ipv6"sv, // BEP0010; BT protocol, rpc
    u8"isBackup"sv, // rpc
    u8"isDownloadingFrom"sv, // rpc
    u8"isEncrypted"sv, // rpc
    u8"isFinished"sv, // rpc
    u8"isIncoming"sv, // rpc
    u8"isPrivate"sv, // rpc
    u8"isStalled"sv, // rpc
    u8"isUTP"sv, // rpc
    u8"isUploadingTo"sv, // rpc
    u8"is_backup"sv, // rpc
    u8"is_downloading"sv, // rpc
    u8"is_downloading_from"sv, // rpc
    u8"is_encrypted"sv, // rpc
    u8"is_finished"sv, // rpc
    u8"is_incoming"sv, // rpc
    u8"is_private"sv, // rpc
    u8"is_stalled"sv, // rpc
    u8"is_uploading_to"sv, // rpc
    u8"is_utp"sv, // rpc
    u8"jsonrpc"sv, // json-rpc
    u8"labels"sv, // .resume, rpc
    u8"lastAnnouncePeerCount"sv, // rpc
    u8"lastAnnounceResult"sv, // rpc
    u8"lastAnnounceStartTime"sv, // rpc
    u8"lastAnnounceSucceeded"sv, // rpc
    u8"lastAnnounceTime"sv, // rpc
    u8"lastAnnounceTimedOut"sv, // rpc
    u8"lastScrapeResult"sv, // rpc
    u8"lastScrapeStartTime"sv, // rpc
    u8"lastScrapeSucceeded"sv, // rpc
    u8"lastScrapeTime"sv, // rpc
    u8"lastScrapeTimedOut"sv, // rpc
    u8"last_announce_peer_count"sv, // rpc
    u8"last_announce_result"sv, // rpc
    u8"last_announce_start_time"sv, // rpc
    u8"last_announce_succeeded"sv, // rpc
    u8"last_announce_time"sv, // rpc
    u8"last_announce_timed_out"sv, // rpc
    u8"last_scrape_result"sv, // rpc
    u8"last_scrape_start_time"sv, // rpc
    u8"last_scrape_succeeded"sv, // rpc
    u8"last_scrape_time"sv, // rpc
    u8"last_scrape_timed_out"sv, // rpc
    u8"leecherCount"sv, // rpc
    u8"leecher_count"sv, // rpc
    u8"leftUntilDone"sv, // rpc
    u8"left_until_done"sv, // rpc
    u8"length"sv, // .torrent, rpc
    u8"location"sv, // rpc
    u8"lpd-enabled"sv, // daemon, rpc, tr_session::Settings
    u8"lpd_enabled"sv, // daemon, rpc, tr_session::Settings
    u8"m"sv, // BEP0010, BEP0011; BT protocol
    u8"magnetLink"sv, // rpc
    u8"magnet_link"sv, // rpc
    u8"main-window-height"sv, // gtk app, qt app
    u8"main-window-is-maximized"sv, // gtk app
    u8"main-window-layout-order"sv, // qt app
    u8"main-window-width"sv, // gtk app, qt app
    u8"main-window-x"sv, // gtk app, qt app
    u8"main-window-y"sv, // gtk app, qt app
    u8"main_window_height"sv, // gtk app, qt app
    u8"main_window_is_maximized"sv, // gtk app
    u8"main_window_layout_order"sv, // qt app
    u8"main_window_width"sv, // gtk app, qt app
    u8"main_window_x"sv, // gtk app, qt app
    u8"main_window_y"sv, // gtk app, qt app
    u8"manualAnnounceTime"sv, // rpc
    u8"manual_announce_time"sv, // rpc
    u8"max-peers"sv, // .resume
    u8"maxConnectedPeers"sv, // rpc
    u8"max_connected_peers"sv, // rpc
    u8"max_peers"sv, // .resume
    u8"memory-bytes"sv, // rpc
    u8"memory-units"sv, // rpc
    u8"memory_bytes"sv, // rpc
    u8"memory_units"sv, // rpc
    u8"message"sv, // json-rpc, rpc
    u8"message-level"sv, // daemon, gtk app, tr_session::Settings
    u8"message_level"sv, // daemon, gtk app, tr_session::Settings
    u8"metadataPercentComplete"sv, // rpc
    u8"metadata_percent_complete"sv, // rpc
    u8"metadata_size"sv, // BEP0009; BT protocol
    u8"metainfo"sv, // rpc
    u8"method"sv, // json-rpc
    u8"move"sv, // rpc
    u8"msg_type"sv, // BT protocol
    u8"mtimes"sv, // .resume
    u8"name"sv, // .resume, .torrent, rpc
    u8"nextAnnounceTime"sv, // rpc
    u8"nextScrapeTime"sv, // rpc
    u8"next_announce_time"sv, // rpc
    u8"next_scrape_time"sv, // rpc
    u8"nodes"sv, // dht.dat
    u8"nodes6"sv, // dht.dat
    u8"open-dialog-dir"sv, // gtk app, qt app
    u8"open_dialog_dir"sv, // gtk app, qt app
    u8"p"sv, // BEP0010; BT protocol
    u8"params"sv, // json-rpc
    u8"path"sv, // .torrent, rpc
    u8"paused"sv, // .resume, rpc
    u8"pausedTorrentCount"sv, // rpc
    u8"paused_torrent_count"sv, // rpc
    u8"peer-congestion-algorithm"sv, // tr_session::Settings
    u8"peer-limit"sv, // rpc
    u8"peer-limit-global"sv, // daemon, rpc, tr_session::Settings
    u8"peer-limit-per-torrent"sv, // daemon, gtk app, rpc, tr_session::Settings
    u8"peer-port"sv, // daemon, gtk app, rpc, tr_session::Settings
    u8"peer-port-random-high"sv, // tr_session::Settings
    u8"peer-port-random-low"sv, // tr_session::Settings
    u8"peer-port-random-on-start"sv, // rpc, tr_session::Settings
    u8"peer-socket-tos"sv, // tr_session::Settings
    u8"peerIsChoked"sv, // rpc
    u8"peerIsInterested"sv, // rpc
    u8"peer_congestion_algorithm"sv, // tr_session::Settings
    u8"peer_id"sv, // rpc
    u8"peer_is_choked"sv, // rpc
    u8"peer_is_interested"sv, // rpc
    u8"peer_limit"sv, // rpc
    u8"peer_limit_global"sv, // daemon, rpc, tr_session::Settings
    u8"peer_limit_per_torrent"sv, // daemon, gtk app, rpc, tr_session::Settings
    u8"peer_port"sv, // daemon, gtk app, rpc, tr_session::Settings
    u8"peer_port_random_high"sv, // tr_session::Settings
    u8"peer_port_random_low"sv, // tr_session::Settings
    u8"peer_port_random_on_start"sv, // rpc, tr_session::Settings
    u8"peer_socket_diffserv"sv, // tr_session::Settings
    u8"peers"sv, // rpc
    u8"peers2"sv, // .resume
    u8"peers2-6"sv, // .resume
    u8"peers2_6"sv, // .resume
    u8"peersConnected"sv, // rpc
    u8"peersFrom"sv, // rpc
    u8"peersGettingFromUs"sv, // rpc
    u8"peersSendingToUs"sv, // rpc
    u8"peers_connected"sv, // rpc
    u8"peers_from"sv, // rpc
    u8"peers_getting_from_us"sv, // rpc
    u8"peers_sending_to_us"sv, // rpc
    u8"percentComplete"sv, // rpc
    u8"percentDone"sv, // rpc
    u8"percent_complete"sv, // rpc
    u8"percent_done"sv, // rpc
    u8"pex-enabled"sv, // rpc, tr_session::Settings
    u8"pex_enabled"sv, // rpc, tr_session::Settings
    u8"pidfile"sv, // daemon
    u8"piece"sv, // BT protocol
    u8"piece length"sv, // .torrent
    u8"pieceCount"sv, // rpc
    u8"pieceSize"sv, // rpc
    u8"piece_count"sv, // rpc
    u8"piece_size"sv, // rpc
    u8"pieces"sv, // .resume, .torrent, rpc
    u8"port"sv, // rpc
    u8"port-forwarding-enabled"sv, // daemon, rpc, tr_session::Settings
    u8"port-is-open"sv, // rpc
    u8"port-test"sv, // rpc
    u8"port_forwarding_enabled"sv, // daemon, rpc, tr_session::Settings
    u8"port_is_open"sv, // rpc
    u8"port_test"sv, // rpc
    u8"preallocation"sv, // tr_session::Settings
    u8"preferred_transports"sv, // rpc, tr_session::Settings
    u8"primary-mime-type"sv, // rpc
    u8"primary_mime_type"sv, // rpc
    u8"priorities"sv, // rpc
    u8"priority"sv, // .resume, rpc
    u8"priority-high"sv, // rpc
    u8"priority-low"sv, // rpc
    u8"priority-normal"sv, // rpc
    u8"priority_high"sv, // rpc
    u8"priority_low"sv, // rpc
    u8"priority_normal"sv, // rpc
    u8"private"sv, // .torrent
    u8"progress"sv, // .resume, rpc
    u8"prompt-before-exit"sv, // qt app
    u8"prompt_before_exit"sv, // qt app
    u8"proxy_url"sv, // tr_session::Settings
    u8"queue-move-bottom"sv, // rpc
    u8"queue-move-down"sv, // rpc
    u8"queue-move-top"sv, // rpc
    u8"queue-move-up"sv, // rpc
    u8"queue-stalled-enabled"sv, // rpc, tr_session::Settings
    u8"queue-stalled-minutes"sv, // rpc, tr_session::Settings
    u8"queuePosition"sv, // rpc
    u8"queue_move_bottom"sv, // rpc
    u8"queue_move_down"sv, // rpc
    u8"queue_move_top"sv, // rpc
    u8"queue_move_up"sv, // rpc
    u8"queue_position"sv, // rpc
    u8"queue_stalled_enabled"sv, // rpc, tr_session::Settings
    u8"queue_stalled_minutes"sv, // rpc, tr_session::Settings
    u8"rateDownload"sv, // rpc
    u8"rateToClient"sv, // rpc
    u8"rateToPeer"sv, // rpc
    u8"rateUpload"sv, // rpc
    u8"rate_download"sv, // rpc
    u8"rate_to_client"sv, // rpc
    u8"rate_to_peer"sv, // rpc
    u8"rate_upload"sv, // rpc
    u8"ratio-limit"sv, // .resume, daemon, gtk app, tr_session::Settings
    u8"ratio-limit-enabled"sv, // daemon, tr_session::Settings
    u8"ratio-mode"sv, // .resume
    u8"ratio_limit"sv, // .resume, daemon, gtk app, tr_session::Settings
    u8"ratio_limit_enabled"sv, // daemon, tr_session::Settings
    u8"ratio_mode"sv, // .resume
    u8"read-clipboard"sv, // qt app
    u8"read_clipboard"sv, // qt app
    u8"recently-active"sv, // rpc
    u8"recently_active"sv, // rpc
    u8"recheckProgress"sv, // rpc
    u8"recheck_progress"sv, // rpc
    u8"remote-session-enabled"sv, // qt app
    u8"remote-session-host"sv, // qt app
    u8"remote-session-https"sv, // qt app
    u8"remote-session-password"sv, // qt app
    u8"remote-session-port"sv, // qt app
    u8"remote-session-requres-authentication"sv, // SIC: misspelled prior to 4.1.0-beta.4; qt app
    u8"remote-session-username"sv, // qt app
    u8"remote_session_enabled"sv, // qt app
    u8"remote_session_host"sv, // qt app
    u8"remote_session_https"sv, // qt app
    u8"remote_session_password"sv, // qt app
    u8"remote_session_port"sv, // qt app
    u8"remote_session_requires_authentication"sv, // qt app
    u8"remote_session_url_base_path"sv, // qt app
    u8"remote_session_username"sv, // qt app
    u8"removed"sv, // rpc
    u8"rename-partial-files"sv, // rpc, tr_session::Settings
    u8"rename_partial_files"sv, // rpc, tr_session::Settings
    u8"reqq"sv, // BEP0010; BT protocol, rpc, tr_session::Settings
    u8"result"sv, // rpc
    u8"rpc-authentication-required"sv, // daemon, rpc server settings
    u8"rpc-bind-address"sv, // daemon, rpc server settings
    u8"rpc-enabled"sv, // daemon, rpc server settings
    u8"rpc-host-whitelist"sv, // rpc, rpc server settings
    u8"rpc-host-whitelist-enabled"sv, // rpc, rpc server settings
    u8"rpc-password"sv, // daemon, rpc server settings
    u8"rpc-port"sv, // daemon, gtk app, rpc server settings
    u8"rpc-socket-mode"sv, // rpc server settings
    u8"rpc-url"sv, // rpc server settings
    u8"rpc-username"sv, // daemon, rpc server settings
    u8"rpc-version"sv, // rpc
    u8"rpc-version-minimum"sv, // rpc
    u8"rpc-version-semver"sv, // rpc
    u8"rpc-whitelist"sv, // daemon, gtk app, rpc server settings
    u8"rpc-whitelist-enabled"sv, // daemon, rpc server settings
    u8"rpc_authentication_required"sv, // daemon, rpc server settings
    u8"rpc_bind_address"sv, // daemon, rpc server settings
    u8"rpc_enabled"sv, // daemon, rpc server settings
    u8"rpc_host_whitelist"sv, // rpc, rpc server settings
    u8"rpc_host_whitelist_enabled"sv, // rpc, rpc server settings
    u8"rpc_password"sv, // daemon, rpc server settings
    u8"rpc_port"sv, // daemon, gtk app, rpc server settings
    u8"rpc_socket_mode"sv, // rpc server settings
    u8"rpc_url"sv, // rpc server settings
    u8"rpc_username"sv, // daemon, rpc server settings
    u8"rpc_version"sv, // rpc
    u8"rpc_version_minimum"sv, // rpc
    u8"rpc_version_semver"sv, // rpc
    u8"rpc_whitelist"sv, // daemon, gtk app, rpc server settings
    u8"rpc_whitelist_enabled"sv, // daemon, rpc server settings
    u8"scrape"sv, // rpc
    u8"scrape-paused-torrents-enabled"sv, // tr_session::Settings
    u8"scrapeState"sv, // rpc
    u8"scrape_paused_torrents_enabled"sv, // tr_session::Settings
    u8"scrape_state"sv, // rpc
    u8"script-torrent-added-enabled"sv, // rpc, tr_session::Settings
    u8"script-torrent-added-filename"sv, // rpc, tr_session::Settings
    u8"script-torrent-done-enabled"sv, // rpc, tr_session::Settings
    u8"script-torrent-done-filename"sv, // rpc, tr_session::Settings
    u8"script-torrent-done-seeding-enabled"sv, // rpc, tr_session::Settings
    u8"script-torrent-done-seeding-filename"sv, // rpc, tr_session::Settings
    u8"script_torrent_added_enabled"sv, // rpc, tr_session::Settings
    u8"script_torrent_added_filename"sv, // rpc, tr_session::Settings
    u8"script_torrent_done_enabled"sv, // rpc, tr_session::Settings
    u8"script_torrent_done_filename"sv, // rpc, tr_session::Settings
    u8"script_torrent_done_seeding_enabled"sv, // rpc, tr_session::Settings
    u8"script_torrent_done_seeding_filename"sv, // rpc, tr_session::Settings
    u8"seconds-active"sv, // stats.json
    u8"secondsActive"sv, // rpc
    u8"secondsDownloading"sv, // rpc
    u8"secondsSeeding"sv, // rpc
    u8"seconds_active"sv, // rpc, stats.json
    u8"seconds_downloading"sv, // rpc
    u8"seconds_seeding"sv, // rpc
    u8"seed-queue-enabled"sv, // rpc, tr_session::Settings
    u8"seed-queue-size"sv, // rpc, tr_session::Settings
    u8"seedIdleLimit"sv, // rpc
    u8"seedIdleMode"sv, // rpc
    u8"seedRatioLimit"sv, // rpc
    u8"seedRatioLimited"sv, // rpc
    u8"seedRatioMode"sv, // rpc
    u8"seed_idle_limit"sv, // rpc
    u8"seed_idle_mode"sv, // rpc
    u8"seed_queue_enabled"sv, // rpc, tr_session::Settings
    u8"seed_queue_size"sv, // rpc, tr_session::Settings
    u8"seed_ratio_limit"sv, // rpc
    u8"seed_ratio_limited"sv, // rpc
    u8"seed_ratio_mode"sv, // rpc
    u8"seederCount"sv, // rpc
    u8"seeder_count"sv, // rpc
    u8"seeding-time-seconds"sv, // .resume
    u8"seeding_time_seconds"sv, // .resume
    u8"sequential_download"sv, // .resume, daemon, rpc, tr_session::Settings
    u8"sequential_download_from_piece"sv, // .resume, rpc
    u8"session-close"sv, // rpc
    u8"session-count"sv, // stats.json
    u8"session-get"sv, // rpc
    u8"session-id"sv, // rpc
    u8"session-set"sv, // rpc
    u8"session-stats"sv, // rpc
    u8"sessionCount"sv, // rpc
    u8"session_close"sv, // rpc
    u8"session_count"sv, // rpc, stats.json
    u8"session_get"sv, // rpc
    u8"session_id"sv, // rpc
    u8"session_set"sv, // rpc
    u8"session_stats"sv, // rpc
    u8"show-backup-trackers"sv, // gtk app, qt app
    u8"show-extra-peer-details"sv, // gtk app
    u8"show-filterbar"sv, // gtk app, qt app
    u8"show-notification-area-icon"sv, // gtk app, qt app
    u8"show-options-window"sv, // gtk app, qt app
    u8"show-statusbar"sv, // gtk app, qt app
    u8"show-toolbar"sv, // gtk app, qt app
    u8"show-tracker-scrapes"sv, // gtk app, qt app
    u8"show_backup_trackers"sv, // gtk app, qt app
    u8"show_extra_peer_details"sv, // gtk app
    u8"show_filterbar"sv, // gtk app, qt app
    u8"show_notification_area_icon"sv, // gtk app, qt app
    u8"show_options_window"sv, // gtk app, qt app
    u8"show_statusbar"sv, // gtk app, qt app
    u8"show_toolbar"sv, // gtk app, qt app
    u8"show_tracker_scrapes"sv, // gtk app, qt app
    u8"sitename"sv, // rpc
    u8"size-bytes"sv, // rpc
    u8"size-units"sv, // rpc
    u8"sizeWhenDone"sv, // rpc
    u8"size_bytes"sv, // rpc
    u8"size_units"sv, // rpc
    u8"size_when_done"sv, // rpc
    u8"sleep-per-seconds-during-verify"sv, // tr_session::Settings
    u8"sleep_per_seconds_during_verify"sv, // tr_session::Settings
    u8"socket_address"sv, // .resume
    u8"sort-mode"sv, // gtk app, qt app
    u8"sort-reversed"sv, // gtk app, qt app
    u8"sort_mode"sv, // gtk app, qt app
    u8"sort_reversed"sv, // gtk app, qt app
    u8"source"sv, // .torrent
    u8"speed"sv, // .resume
    u8"speed-Bps"sv, // .resume
    u8"speed-bytes"sv, // rpc
    u8"speed-limit-down"sv, // .resume, gtk app, rpc, tr_session::Settings
    u8"speed-limit-down-enabled"sv, // rpc, tr_session::Settings
    u8"speed-limit-up"sv, // .resume, gtk app, rpc, tr_session::Settings
    u8"speed-limit-up-enabled"sv, // rpc, tr_session::Settings
    u8"speed-units"sv, // rpc
    u8"speed_Bps"sv, // .resume
    u8"speed_bytes"sv, // rpc
    u8"speed_limit_down"sv, // .resume, gtk app, rpc, tr_session::Settings
    u8"speed_limit_down_enabled"sv, // rpc, tr_session::Settings
    u8"speed_limit_up"sv, // .resume, gtk app, rpc, tr_session::Settings
    u8"speed_limit_up_enabled"sv, // rpc, tr_session::Settings
    u8"speed_units"sv, // rpc
    u8"start-added-torrents"sv, // gtk app, rpc, tr_session::Settings
    u8"start-minimized"sv, // qt app
    u8"startDate"sv, // rpc
    u8"start_added_torrents"sv, // gtk app, rpc, tr_session::Settings
    u8"start_date"sv, // rpc
    u8"start_minimized"sv, // qt app
    u8"start_paused"sv, // daemon
    u8"status"sv, // rpc
    u8"statusbar-stats"sv, // gtk app, qt app
    u8"statusbar_stats"sv, // gtk app, qt app
    u8"tag"sv, // rpc
    u8"tcp-enabled"sv, // rpc, tr_session::Settings
    u8"tcp_enabled"sv, // rpc, tr_session::Settings
    u8"tier"sv, // rpc
    u8"time-checked"sv, // .resume
    u8"time_checked"sv, // .resume
    u8"torrent-add"sv, // rpc
    u8"torrent-added"sv, // rpc
    u8"torrent-added-notification-enabled"sv, // gtk app, qt app
    u8"torrent-added-verify-mode"sv, // tr_session::Settings
    u8"torrent-complete-notification-enabled"sv, // gtk app, qt app
    u8"torrent-complete-sound-command"sv, // gtk app, qt app
    u8"torrent-complete-sound-enabled"sv, // gtk app, qt app
    u8"torrent-duplicate"sv, // rpc
    u8"torrent-get"sv, // rpc
    u8"torrent-reannounce"sv, // rpc
    u8"torrent-remove"sv, // rpc
    u8"torrent-rename-path"sv, // rpc
    u8"torrent-set"sv, // rpc
    u8"torrent-set-location"sv, // rpc
    u8"torrent-start"sv, // rpc
    u8"torrent-start-now"sv, // rpc
    u8"torrent-stop"sv, // rpc
    u8"torrent-verify"sv, // rpc
    u8"torrentCount"sv, // rpc
    u8"torrentFile"sv, // rpc
    u8"torrent_add"sv, // rpc
    u8"torrent_added"sv, // rpc
    u8"torrent_added_notification_enabled"sv, // gtk app, qt app
    u8"torrent_added_verify_mode"sv, // tr_session::Settings
    u8"torrent_complete_notification_enabled"sv, // gtk app, qt app
    u8"torrent_complete_sound_command"sv, // gtk app, qt app
    u8"torrent_complete_sound_enabled"sv, // gtk app, qt app
    u8"torrent_complete_verify_enabled"sv, // tr_session::Settings
    u8"torrent_count"sv, // rpc
    u8"torrent_duplicate"sv, // rpc
    u8"torrent_file"sv, // rpc
    u8"torrent_get"sv, // rpc
    u8"torrent_reannounce"sv, // rpc
    u8"torrent_remove"sv, // rpc
    u8"torrent_rename_path"sv, // rpc
    u8"torrent_set"sv, // rpc
    u8"torrent_set_location"sv, // rpc
    u8"torrent_start"sv, // rpc
    u8"torrent_start_now"sv, // rpc
    u8"torrent_stop"sv, // rpc
    u8"torrent_verify"sv, // rpc
    u8"torrents"sv, // rpc
    u8"totalSize"sv, // rpc
    u8"total_size"sv, // BT protocol, rpc
    u8"trackerAdd"sv, // rpc
    u8"trackerList"sv, // rpc
    u8"trackerRemove"sv, // rpc
    u8"trackerReplace"sv, // rpc
    u8"trackerStats"sv, // rpc
    u8"tracker_add"sv, // rpc
    u8"tracker_list"sv, // rpc
    u8"tracker_remove"sv, // rpc
    u8"tracker_replace"sv, // rpc
    u8"tracker_stats"sv, // rpc
    u8"trackers"sv, // rpc
    u8"trash-can-enabled"sv, // gtk app
    u8"trash-original-torrent-files"sv, // gtk app, rpc, tr_session::Settings
    u8"trash_can_enabled"sv, // gtk app
    u8"trash_original_torrent_files"sv, // gtk app, rpc, tr_session::Settings
    u8"umask"sv, // tr_session::Settings
    u8"units"sv, // rpc
    u8"upload-slots-per-torrent"sv, // tr_session::Settings
    u8"uploadLimit"sv, // rpc
    u8"uploadLimited"sv, // rpc
    u8"uploadRatio"sv, // rpc
    u8"uploadSpeed"sv, // rpc
    u8"upload_limit"sv, // rpc
    u8"upload_limited"sv, // rpc
    u8"upload_only"sv, // BEP0021; BT protocol
    u8"upload_ratio"sv, // rpc
    u8"upload_slots_per_torrent"sv, // tr_session::Settings
    u8"upload_speed"sv, // rpc
    u8"uploaded"sv, // .resume
    u8"uploaded-bytes"sv, // stats.json
    u8"uploadedBytes"sv, // rpc
    u8"uploadedEver"sv, // rpc
    u8"uploaded_bytes"sv, // rpc, stats.json
    u8"uploaded_ever"sv, // rpc
    u8"url"sv, // rpc
    u8"url-list"sv, // .torrent
    u8"use-global-speed-limit"sv, // .resume
    u8"use-speed-limit"sv, // .resume
    u8"use_global_speed_limit"sv, // .resume
    u8"use_speed_limit"sv, // .resume
    u8"ut_holepunch"sv, // BT protocol
    u8"ut_metadata"sv, // BEP0011; BT protocol
    u8"ut_pex"sv, // BEP0010, BEP0011; BT protocol
    u8"utp-enabled"sv, // daemon, rpc, tr_session::Settings
    u8"utp_enabled"sv, // daemon, rpc, tr_session::Settings
    u8"v"sv, // BEP0010; BT protocol
    u8"version"sv, // rpc
    u8"wanted"sv, // rpc
    u8"watch-dir"sv, // daemon, gtk app, qt app
    u8"watch-dir-enabled"sv, // daemon, gtk app, qt app
    u8"watch-dir-force-generic"sv, // daemon
    u8"watch_dir"sv, // daemon, gtk app, qt app
    u8"watch_dir_enabled"sv, // daemon, gtk app, qt app
    u8"watch_dir_force_generic"sv, // daemon
    u8"webseeds"sv, // rpc
    u8"webseedsSendingToUs"sv, // rpc
    u8"webseeds_ex"sv, // rpc
    u8"webseeds_sending_to_us"sv, // rpc
    u8"yourip"sv, // BEP0010; BT protocol
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

    auto const u8key = tr_strv_to_u8string(key);
    if (auto const sit = std::lower_bound(Sbegin, Send, u8key); sit != Send && *sit == u8key)
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
    return q < TR_N_KEYS ? std::string_view{ reinterpret_cast<char const*>(std::data(MyStatic[q])), std::size(MyStatic[q]) } :
                           my_runtime[q - TR_N_KEYS];
}

std::u8string_view tr_quark_get_u8string_view(tr_quark q)
{
    return MyStatic[q];
}
