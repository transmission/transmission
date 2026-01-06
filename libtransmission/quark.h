// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cstddef> // size_t
#include <optional>
#include <string_view>

#include "libtransmission/symbol.h"

/**
 * Quarks — a 2-way association between a compile-time interned string
 * and a unique integer identifier. Used to make well-known strings
 * (e.g. strings in settings files, the JSON-RPC API, and BitTorrent protocol)
 * cheap to store, cheap to compare, and usable in switch-case statements.
 */
using tr_quark = transmission::symbol::Symbol;

// Well-known names.

#define KNOWN_KEYS(X) \
    X(TR_KEY_NONE, "") \
    X(TR_KEY_active_torrent_count, "active_torrent_count") /* rpc */ \
    X(TR_KEY_activity_date, "activity_date") /* .resume, rpc */ \
    X(TR_KEY_added, "added") /* BEP0011; BT protocol, rpc */ \
    X(TR_KEY_added_f, "added.f") /* BEP0011; BT protocol */ \
    X(TR_KEY_added6, "added6") /* BEP0011; BT protocol */ \
    X(TR_KEY_added6_f, "added6.f") /* BEP0011; BT protocol */ \
    X(TR_KEY_added_date, "added_date") /* .resume, rpc */ \
    X(TR_KEY_address, "address") /* rpc */ \
    X(TR_KEY_alt_speed_down, "alt_speed_down") /* gtk app, rpc, speed settings */ \
    X(TR_KEY_alt_speed_enabled, "alt_speed_enabled") /* gtk app, rpc, speed settings */ \
    X(TR_KEY_alt_speed_time_begin, "alt_speed_time_begin") /* rpc, speed settings */ \
    X(TR_KEY_alt_speed_time_day, "alt_speed_time_day") /* rpc, speed settings */ \
    X(TR_KEY_alt_speed_time_enabled, "alt_speed_time_enabled") /* rpc, speed settings */ \
    X(TR_KEY_alt_speed_time_end, "alt_speed_time_end") /* rpc, speed settings */ \
    X(TR_KEY_alt_speed_up, "alt_speed_up") /* gtk app, rpc, speed settings */ \
    X(TR_KEY_announce, "announce") /* BEP0003; BT protocol */ \
    X(TR_KEY_announce_list, "announce-list") /* BEP0012; BT protocol */ \
    X(TR_KEY_announce_ip, "announce_ip") /* tr_session::Settings */ \
    X(TR_KEY_announce_ip_enabled, "announce_ip_enabled") /* tr_session::Settings */ \
    X(TR_KEY_announce_state, "announce_state") /* rpc */ \
    X(TR_KEY_anti_brute_force_enabled, "anti_brute_force_enabled") /* rpc, rpc server settings */ \
    X(TR_KEY_anti_brute_force_threshold, "anti_brute_force_threshold") /* rpc server settings */ \
    X(TR_KEY_arguments, "arguments") /* json-rpc */ \
    X(TR_KEY_availability, "availability") /* rpc */ \
    X(TR_KEY_bandwidth_priority, "bandwidth_priority") /* .resume, rpc */ \
    X(TR_KEY_begin_piece, "begin_piece") /* rpc */ \
    X(TR_KEY_bind_address_ipv4, "bind_address_ipv4") /* daemon, tr_session::Settings */ \
    X(TR_KEY_bind_address_ipv6, "bind_address_ipv6") /* daemon, tr_session::Settings */ \
    X(TR_KEY_bitfield, "bitfield") /* .resume */ \
    X(TR_KEY_blocklist_date, "blocklist_date") /* gtk app, qt app */ \
    X(TR_KEY_blocklist_enabled, "blocklist_enabled") /* daemon, gtk app, rpc, tr_session::Settings */ \
    X(TR_KEY_blocklist_size, "blocklist_size") /* rpc */ \
    X(TR_KEY_blocklist_update, "blocklist_update") /* rpc */ \
    X(TR_KEY_blocklist_updates_enabled, "blocklist_updates_enabled") /* gtk app, qt app */ \
    X(TR_KEY_blocklist_url, "blocklist_url") /* rpc, tr_session::Settings */ \
    X(TR_KEY_blocks, "blocks") /* .resume */ \
    X(TR_KEY_bytes_completed, "bytes_completed") /* rpc */ \
    X(TR_KEY_bytes_to_client, "bytes_to_client") /* rpc */ \
    X(TR_KEY_bytes_to_peer, "bytes_to_peer") /* rpc */ \
    X(TR_KEY_cache_size_mib, "cache_size_mib") /* rpc, tr_session::Settings */ \
    X(TR_KEY_client_is_choked, "client_is_choked") /* rpc */ \
    X(TR_KEY_client_is_interested, "client_is_interested") /* rpc */ \
    X(TR_KEY_client_name, "client_name") /* rpc */ \
    X(TR_KEY_code, "code") /* json-rpc */ \
    X(TR_KEY_comment, "comment") /* .torrent, rpc */ \
    X(TR_KEY_compact_view, "compact_view") /* gtk app, qt app */ \
    X(TR_KEY_complete, "complete") /* BEP0048; BT protocol */ \
    X(TR_KEY_config_dir, "config_dir") /* rpc */ \
    X(TR_KEY_cookies, "cookies") /* rpc */ \
    X(TR_KEY_corrupt, "corrupt") /* .resume */ \
    X(TR_KEY_corrupt_ever, "corrupt_ever") /* rpc */ \
    X(TR_KEY_created_by, "created by") /* .torrent */ \
    X(TR_KEY_creation_date, "creation date") /* .torrent */ \
    X(TR_KEY_creator, "creator") /* rpc */ \
    X(TR_KEY_cumulative_stats, "cumulative_stats") /* rpc */ \
    X(TR_KEY_current_stats, "current_stats") /* rpc */ \
    X(TR_KEY_data, "data") /* json-rpc, rpc */ \
    X(TR_KEY_date_created, "date_created") /* rpc */ \
    X(TR_KEY_default_trackers, "default_trackers") /* daemon, rpc, tr_session::Settings */ \
    X(TR_KEY_delete_local_data, "delete_local_data") /* rpc */ \
    X(TR_KEY_desired_available, "desired_available") /* rpc */ \
    X(TR_KEY_destination, "destination") /* .resume */ \
    X(TR_KEY_details_window_height, "details_window_height") /* gtk app */ \
    X(TR_KEY_details_window_width, "details_window_width") /* gtk app */ \
    X(TR_KEY_dht_enabled, "dht_enabled") /* daemon, rpc, tr_session::Settings */ \
    X(TR_KEY_dnd, "dnd") /* .resume */ \
    X(TR_KEY_done_date, "done_date") /* .resume, rpc */ \
    X(TR_KEY_download_count, "download_count") /* rpc */ \
    X(TR_KEY_download_dir, "download_dir") /* daemon, gtk app, rpc, tr_session::Settings */ \
    X(TR_KEY_download_dir_free_space, "download_dir_free_space") /* rpc */ \
    X(TR_KEY_download_limit, "download_limit") /* rpc */ \
    X(TR_KEY_download_limited, "download_limited") /* rpc */ \
    X(TR_KEY_download_queue_enabled, "download_queue_enabled") /* rpc, tr_session::Settings */ \
    X(TR_KEY_download_queue_size, "download_queue_size") /* rpc, tr_session::Settings */ \
    X(TR_KEY_download_speed, "download_speed") /* rpc */ \
    X(TR_KEY_downloaded, "downloaded") /* BEP0048; .resume, BT protocol */ \
    X(TR_KEY_downloaded_bytes, "downloaded_bytes") /* rpc, stats.json */ \
    X(TR_KEY_downloaded_ever, "downloaded_ever") /* rpc */ \
    X(TR_KEY_downloader_count, "downloader_count") /* rpc */ \
    X(TR_KEY_downloading_time_seconds, "downloading_time_seconds") /* .resume */ \
    X(TR_KEY_dropped, "dropped") /* BEP0011; BT protocol */ \
    X(TR_KEY_dropped6, "dropped6") /* BEP0011; BT protocol */ \
    X(TR_KEY_e, "e") /* BT protocol */ \
    X(TR_KEY_edit_date, "edit_date") /* rpc */ \
    X(TR_KEY_encoding, "encoding") /* .torrent */ \
    X(TR_KEY_encryption, "encryption") /* daemon, rpc, tr_session::Settings */ \
    X(TR_KEY_end_piece, "end_piece") /* rpc */ \
    X(TR_KEY_error, "error") /* rpc */ \
    X(TR_KEY_error_string, "error_string") /* rpc */ \
    X(TR_KEY_eta, "eta") /* rpc */ \
    X(TR_KEY_eta_idle, "eta_idle") /* rpc */ \
    X(TR_KEY_fields, "fields") /* rpc */ \
    X(TR_KEY_file_count, "file_count") /* rpc */ \
    X(TR_KEY_file_stats, "file_stats") /* rpc */ \
    X(TR_KEY_filename, "filename") /* rpc */ \
    X(TR_KEY_files, "files") /* .resume, .torrent, rpc */ \
    X(TR_KEY_files_added, "files_added") /* rpc, stats.json */ \
    X(TR_KEY_files_unwanted, "files_unwanted") /* rpc */ \
    X(TR_KEY_files_wanted, "files_wanted") /* rpc */ \
    X(TR_KEY_filter_mode, "filter_mode") /* qt app */ \
    X(TR_KEY_filter_text, "filter_text") /* qt app */ \
    X(TR_KEY_filter_trackers, "filter_trackers") /* qt app */ \
    X(TR_KEY_flag_str, "flag_str") /* rpc */ \
    X(TR_KEY_flags, "flags") /* .resume */ \
    X(TR_KEY_format, "format") /* rpc */ \
    X(TR_KEY_free_space, "free_space") /* rpc */ \
    X(TR_KEY_from_cache, "from_cache") /* rpc */ \
    X(TR_KEY_from_dht, "from_dht") /* rpc */ \
    X(TR_KEY_from_incoming, "from_incoming") /* rpc */ \
    X(TR_KEY_from_lpd, "from_lpd") /* rpc */ \
    X(TR_KEY_from_ltep, "from_ltep") /* rpc */ \
    X(TR_KEY_from_pex, "from_pex") /* rpc */ \
    X(TR_KEY_from_tracker, "from_tracker") /* rpc */ \
    X(TR_KEY_group, "group") /* .resume, rpc */ \
    X(TR_KEY_group_get, "group_get") /* rpc */ \
    X(TR_KEY_group_set, "group_set") /* rpc */ \
    X(TR_KEY_has_announced, "has_announced") /* rpc */ \
    X(TR_KEY_has_scraped, "has_scraped") /* rpc */ \
    X(TR_KEY_hash_string, "hash_string") /* rpc */ \
    X(TR_KEY_have_unchecked, "have_unchecked") /* rpc */ \
    X(TR_KEY_have_valid, "have_valid") /* rpc */ \
    X(TR_KEY_honors_session_limits, "honors_session_limits") /* rpc */ \
    X(TR_KEY_host, "host") /* rpc */ \
    X(TR_KEY_id, "id") /* dht.dat, rpc */ \
    X(TR_KEY_id_timestamp, "id_timestamp") /* dht.dat */ \
    X(TR_KEY_idle_limit, "idle_limit") /* .resume */ \
    X(TR_KEY_idle_mode, "idle_mode") /* .resume */ \
    X(TR_KEY_idle_seeding_limit, "idle_seeding_limit") /* rpc, tr_session::Settings */ \
    X(TR_KEY_idle_seeding_limit_enabled, "idle_seeding_limit_enabled") /* rpc, tr_session::Settings */ \
    X(TR_KEY_ids, "ids") /* rpc */ \
    X(TR_KEY_incomplete, "incomplete") /* BEP0048; BT protocol */ \
    X(TR_KEY_incomplete_dir, "incomplete_dir") /* .resume, daemon, gtk app, rpc, tr_session::Settings */ \
    X(TR_KEY_incomplete_dir_enabled, "incomplete_dir_enabled") /* daemon, rpc, tr_session::Settings */ \
    X(TR_KEY_info, "info") /* .torrent */ \
    X(TR_KEY_inhibit_desktop_hibernation, "inhibit_desktop_hibernation") /* gtk app, qt app */ \
    X(TR_KEY_ip_protocol, "ip_protocol") /* rpc */ \
    X(TR_KEY_ipv4, "ipv4") /* BEP0010; BT protocol, rpc */ \
    X(TR_KEY_ipv6, "ipv6") /* BEP0010; BT protocol, rpc */ \
    X(TR_KEY_is_backup, "is_backup") /* rpc */ \
    X(TR_KEY_is_downloading_from, "is_downloading_from") /* rpc */ \
    X(TR_KEY_is_encrypted, "is_encrypted") /* rpc */ \
    X(TR_KEY_is_finished, "is_finished") /* rpc */ \
    X(TR_KEY_is_incoming, "is_incoming") /* rpc */ \
    X(TR_KEY_is_private, "is_private") /* rpc */ \
    X(TR_KEY_is_stalled, "is_stalled") /* rpc */ \
    X(TR_KEY_is_uploading_to, "is_uploading_to") /* rpc */ \
    X(TR_KEY_is_utp, "is_utp") /* rpc */ \
    X(TR_KEY_jsonrpc, "jsonrpc") /* json-rpc */ \
    X(TR_KEY_labels, "labels") /* .resume, rpc */ \
    X(TR_KEY_last_announce_peer_count, "last_announce_peer_count") /* rpc */ \
    X(TR_KEY_last_announce_result, "last_announce_result") /* rpc */ \
    X(TR_KEY_last_announce_start_time, "last_announce_start_time") /* rpc */ \
    X(TR_KEY_last_announce_succeeded, "last_announce_succeeded") /* rpc */ \
    X(TR_KEY_last_announce_time, "last_announce_time") /* rpc */ \
    X(TR_KEY_last_announce_timed_out, "last_announce_timed_out") /* rpc */ \
    X(TR_KEY_last_scrape_result, "last_scrape_result") /* rpc */ \
    X(TR_KEY_last_scrape_start_time, "last_scrape_start_time") /* rpc */ \
    X(TR_KEY_last_scrape_succeeded, "last_scrape_succeeded") /* rpc */ \
    X(TR_KEY_last_scrape_time, "last_scrape_time") /* rpc */ \
    X(TR_KEY_last_scrape_timed_out, "last_scrape_timed_out") /* rpc */ \
    X(TR_KEY_leecher_count, "leecher_count") /* rpc */ \
    X(TR_KEY_left_until_done, "left_until_done") /* rpc */ \
    X(TR_KEY_length, "length") /* .torrent, rpc */ \
    X(TR_KEY_location, "location") /* rpc */ \
    X(TR_KEY_lpd_enabled, "lpd_enabled") /* daemon, rpc, tr_session::Settings */ \
    X(TR_KEY_m, "m") /* BEP0010, BEP0011; BT protocol */ \
    X(TR_KEY_magnet_link, "magnet_link") /* rpc */ \
    X(TR_KEY_main_window_height, "main_window_height") /* gtk app, qt app */ \
    X(TR_KEY_main_window_is_maximized, "main_window_is_maximized") /* gtk app */ \
    X(TR_KEY_main_window_layout_order, "main_window_layout_order") /* qt app */ \
    X(TR_KEY_main_window_width, "main_window_width") /* gtk app, qt app */ \
    X(TR_KEY_main_window_x, "main_window_x") /* gtk app, qt app */ \
    X(TR_KEY_main_window_y, "main_window_y") /* gtk app, qt app */ \
    X(TR_KEY_manual_announce_time, "manual_announce_time") /* rpc */ \
    X(TR_KEY_max_connected_peers, "max_connected_peers") /* rpc */ \
    X(TR_KEY_max_peers, "max_peers") /* .resume */ \
    X(TR_KEY_memory_bytes, "memory_bytes") /* rpc */ \
    X(TR_KEY_memory_units, "memory_units") /* rpc */ \
    X(TR_KEY_message, "message") /* json-rpc, rpc */ \
    X(TR_KEY_message_level, "message_level") /* daemon, gtk app, tr_session::Settings */ \
    X(TR_KEY_metadata_percent_complete, "metadata_percent_complete") /* rpc */ \
    X(TR_KEY_metadata_size, "metadata_size") /* BEP0009; BT protocol */ \
    X(TR_KEY_metainfo, "metainfo") /* rpc */ \
    X(TR_KEY_method, "method") /* json-rpc */ \
    X(TR_KEY_move, "move") /* rpc */ \
    X(TR_KEY_msg_type, "msg_type") /* BT protocol */ \
    X(TR_KEY_mtimes, "mtimes") /* .resume */ \
    X(TR_KEY_name, "name") /* .resume, .torrent, rpc */ \
    X(TR_KEY_next_announce_time, "next_announce_time") /* rpc */ \
    X(TR_KEY_next_scrape_time, "next_scrape_time") /* rpc */ \
    X(TR_KEY_nodes, "nodes") /* dht.dat */ \
    X(TR_KEY_nodes6, "nodes6") /* dht.dat */ \
    X(TR_KEY_open_dialog_dir, "open_dialog_dir") /* gtk app, qt app */ \
    X(TR_KEY_p, "p") /* BEP0010; BT protocol */ \
    X(TR_KEY_params, "params") /* json-rpc */ \
    X(TR_KEY_path, "path") /* .torrent, rpc */ \
    X(TR_KEY_paused, "paused") /* .resume, rpc */ \
    X(TR_KEY_paused_torrent_count, "paused_torrent_count") /* rpc */ \
    X(TR_KEY_peer_congestion_algorithm, "peer_congestion_algorithm") /* tr_session::Settings */ \
    X(TR_KEY_peer_id, "peer_id") /* rpc */ \
    X(TR_KEY_peer_is_choked, "peer_is_choked") /* rpc */ \
    X(TR_KEY_peer_is_interested, "peer_is_interested") /* rpc */ \
    X(TR_KEY_peer_limit, "peer_limit") /* rpc */ \
    X(TR_KEY_peer_limit_global, "peer_limit_global") /* daemon, rpc, tr_session::Settings */ \
    X(TR_KEY_peer_limit_per_torrent, "peer_limit_per_torrent") /* daemon, gtk app, rpc, tr_session::Settings */ \
    X(TR_KEY_peer_port, "peer_port") /* daemon, gtk app, rpc, tr_session::Settings */ \
    X(TR_KEY_peer_port_random_high, "peer_port_random_high") /* tr_session::Settings */ \
    X(TR_KEY_peer_port_random_low, "peer_port_random_low") /* tr_session::Settings */ \
    X(TR_KEY_peer_port_random_on_start, "peer_port_random_on_start") /* rpc, tr_session::Settings */ \
    X(TR_KEY_peer_socket_diffserv, "peer_socket_diffserv") /* tr_session::Settings */ \
    X(TR_KEY_peers, "peers") /* rpc */ \
    X(TR_KEY_peers2, "peers2") /* .resume */ \
    X(TR_KEY_peers2_6, "peers2_6") /* .resume */ \
    X(TR_KEY_peers_connected, "peers_connected") /* rpc */ \
    X(TR_KEY_peers_from, "peers_from") /* rpc */ \
    X(TR_KEY_peers_getting_from_us, "peers_getting_from_us") /* rpc */ \
    X(TR_KEY_peers_sending_to_us, "peers_sending_to_us") /* rpc */ \
    X(TR_KEY_percent_complete, "percent_complete") /* rpc */ \
    X(TR_KEY_percent_done, "percent_done") /* rpc */ \
    X(TR_KEY_pex_enabled, "pex_enabled") /* rpc, tr_session::Settings */ \
    X(TR_KEY_pidfile, "pidfile") /* daemon */ \
    X(TR_KEY_piece, "piece") /* BT protocol */ \
    X(TR_KEY_piece_length, "piece length") /* .torrent */ \
    X(TR_KEY_piece_count, "piece_count") /* rpc */ \
    X(TR_KEY_piece_size, "piece_size") /* rpc */ \
    X(TR_KEY_pieces, "pieces") /* .resume, .torrent, rpc */ \
    X(TR_KEY_port, "port") /* rpc */ \
    X(TR_KEY_port_forwarding_enabled, "port_forwarding_enabled") /* daemon, rpc, tr_session::Settings */ \
    X(TR_KEY_port_is_open, "port_is_open") /* rpc */ \
    X(TR_KEY_port_test, "port_test") /* rpc */ \
    X(TR_KEY_preallocation, "preallocation") /* tr_session::Settings */ \
    X(TR_KEY_preferred_transports, "preferred_transports") /* rpc, tr_session::Settings */ \
    X(TR_KEY_primary_mime_type, "primary_mime_type") /* rpc */ \
    X(TR_KEY_priorities, "priorities") /* rpc */ \
    X(TR_KEY_priority, "priority") /* .resume, rpc */ \
    X(TR_KEY_priority_high, "priority_high") /* rpc */ \
    X(TR_KEY_priority_low, "priority_low") /* rpc */ \
    X(TR_KEY_priority_normal, "priority_normal") /* rpc */ \
    X(TR_KEY_private, "private") /* .torrent */ \
    X(TR_KEY_progress, "progress") /* .resume, rpc */ \
    X(TR_KEY_prompt_before_exit, "prompt_before_exit") /* qt app */ \
    X(TR_KEY_proxy_url, "proxy_url") /* tr_session::Settings */ \
    X(TR_KEY_queue_move_bottom, "queue_move_bottom") /* rpc */ \
    X(TR_KEY_queue_move_down, "queue_move_down") /* rpc */ \
    X(TR_KEY_queue_move_top, "queue_move_top") /* rpc */ \
    X(TR_KEY_queue_move_up, "queue_move_up") /* rpc */ \
    X(TR_KEY_queue_position, "queue_position") /* rpc */ \
    X(TR_KEY_queue_stalled_enabled, "queue_stalled_enabled") /* rpc, tr_session::Settings */ \
    X(TR_KEY_queue_stalled_minutes, "queue_stalled_minutes") /* rpc, tr_session::Settings */ \
    X(TR_KEY_rate_download, "rate_download") /* rpc */ \
    X(TR_KEY_rate_to_client, "rate_to_client") /* rpc */ \
    X(TR_KEY_rate_to_peer, "rate_to_peer") /* rpc */ \
    X(TR_KEY_rate_upload, "rate_upload") /* rpc */ \
    X(TR_KEY_ratio_limit, "ratio_limit") /* .resume, daemon, gtk app, tr_session::Settings */ \
    X(TR_KEY_ratio_limit_enabled, "ratio_limit_enabled") /* daemon, tr_session::Settings */ \
    X(TR_KEY_ratio_mode, "ratio_mode") /* .resume */ \
    X(TR_KEY_read_clipboard, "read_clipboard") /* qt app */ \
    X(TR_KEY_recently_active, "recently_active") /* rpc */ \
    X(TR_KEY_recheck_progress, "recheck_progress") /* rpc */ \
    X(TR_KEY_remote_session_enabled, "remote_session_enabled") /* qt app */ \
    X(TR_KEY_remote_session_host, "remote_session_host") /* qt app */ \
    X(TR_KEY_remote_session_https, "remote_session_https") /* qt app */ \
    X(TR_KEY_remote_session_password, "remote_session_password") /* qt app */ \
    X(TR_KEY_remote_session_port, "remote_session_port") /* qt app */ \
    X(TR_KEY_remote_session_requires_authentication, "remote_session_requires_authentication") /* qt app */ \
    X(TR_KEY_remote_session_rpc_url_path, "remote_session_rpc_url_path") /* qt app */ \
    X(TR_KEY_remote_session_username, "remote_session_username") /* qt app */ \
    X(TR_KEY_removed, "removed") /* rpc */ \
    X(TR_KEY_rename_partial_files, "rename_partial_files") /* rpc, tr_session::Settings */ \
    X(TR_KEY_reqq, "reqq") /* BEP0010; BT protocol, rpc, tr_session::Settings */ \
    X(TR_KEY_result, "result") /* rpc */ \
    X(TR_KEY_rpc_authentication_required, "rpc_authentication_required") /* daemon, rpc server settings */ \
    X(TR_KEY_rpc_bind_address, "rpc_bind_address") /* daemon, rpc server settings */ \
    X(TR_KEY_rpc_enabled, "rpc_enabled") /* daemon, rpc server settings */ \
    X(TR_KEY_rpc_host_whitelist, "rpc_host_whitelist") /* rpc, rpc server settings */ \
    X(TR_KEY_rpc_host_whitelist_enabled, "rpc_host_whitelist_enabled") /* rpc, rpc server settings */ \
    X(TR_KEY_rpc_password, "rpc_password") /* daemon, rpc server settings */ \
    X(TR_KEY_rpc_port, "rpc_port") /* daemon, gtk app, rpc server settings */ \
    X(TR_KEY_rpc_socket_mode, "rpc_socket_mode") /* rpc server settings */ \
    X(TR_KEY_rpc_url, "rpc_url") /* rpc server settings */ \
    X(TR_KEY_rpc_username, "rpc_username") /* daemon, rpc server settings */ \
    X(TR_KEY_rpc_version, "rpc_version") /* rpc */ \
    X(TR_KEY_rpc_version_minimum, "rpc_version_minimum") /* rpc */ \
    X(TR_KEY_rpc_version_semver, "rpc_version_semver") /* rpc */ \
    X(TR_KEY_rpc_whitelist, "rpc_whitelist") /* daemon, gtk app, rpc server settings */ \
    X(TR_KEY_rpc_whitelist_enabled, "rpc_whitelist_enabled") /* daemon, rpc server settings */ \
    X(TR_KEY_scrape, "scrape") /* rpc */ \
    X(TR_KEY_scrape_paused_torrents_enabled, "scrape_paused_torrents_enabled") /* tr_session::Settings */ \
    X(TR_KEY_scrape_state, "scrape_state") /* rpc */ \
    X(TR_KEY_script_torrent_added_enabled, "script_torrent_added_enabled") /* rpc, tr_session::Settings */ \
    X(TR_KEY_script_torrent_added_filename, "script_torrent_added_filename") /* rpc, tr_session::Settings */ \
    X(TR_KEY_script_torrent_done_enabled, "script_torrent_done_enabled") /* rpc, tr_session::Settings */ \
    X(TR_KEY_script_torrent_done_filename, "script_torrent_done_filename") /* rpc, tr_session::Settings */ \
    X(TR_KEY_script_torrent_done_seeding_enabled, "script_torrent_done_seeding_enabled") /* rpc, tr_session::Settings */ \
    X(TR_KEY_script_torrent_done_seeding_filename, "script_torrent_done_seeding_filename") /* rpc, tr_session::Settings */ \
    X(TR_KEY_seconds_active, "seconds_active") /* rpc, stats.json */ \
    X(TR_KEY_seconds_downloading, "seconds_downloading") /* rpc */ \
    X(TR_KEY_seconds_seeding, "seconds_seeding") /* rpc */ \
    X(TR_KEY_seed_idle_limit, "seed_idle_limit") /* rpc */ \
    X(TR_KEY_seed_idle_mode, "seed_idle_mode") /* rpc */ \
    X(TR_KEY_seed_queue_enabled, "seed_queue_enabled") /* rpc, tr_session::Settings */ \
    X(TR_KEY_seed_queue_size, "seed_queue_size") /* rpc, tr_session::Settings */ \
    X(TR_KEY_seed_ratio_limit, "seed_ratio_limit") /* rpc */ \
    X(TR_KEY_seed_ratio_limited, "seed_ratio_limited") /* rpc */ \
    X(TR_KEY_seed_ratio_mode, "seed_ratio_mode") /* rpc */ \
    X(TR_KEY_seeder_count, "seeder_count") /* rpc */ \
    X(TR_KEY_seeding_time_seconds, "seeding_time_seconds") /* .resume */ \
    X(TR_KEY_sequential_download, "sequential_download") /* .resume, daemon, rpc, tr_session::Settings */ \
    X(TR_KEY_sequential_download_from_piece, "sequential_download_from_piece") /* .resume, rpc */ \
    X(TR_KEY_session_close, "session_close") /* rpc */ \
    X(TR_KEY_session_count, "session_count") /* rpc, stats.json */ \
    X(TR_KEY_session_get, "session_get") /* rpc */ \
    X(TR_KEY_session_id, "session_id") /* rpc */ \
    X(TR_KEY_session_set, "session_set") /* rpc */ \
    X(TR_KEY_session_stats, "session_stats") /* rpc */ \
    X(TR_KEY_show_backup_trackers, "show_backup_trackers") /* gtk app, qt app */ \
    X(TR_KEY_show_extra_peer_details, "show_extra_peer_details") /* gtk app */ \
    X(TR_KEY_show_filterbar, "show_filterbar") /* gtk app, qt app */ \
    X(TR_KEY_show_notification_area_icon, "show_notification_area_icon") /* gtk app, qt app */ \
    X(TR_KEY_show_options_window, "show_options_window") /* gtk app, qt app */ \
    X(TR_KEY_show_statusbar, "show_statusbar") /* gtk app, qt app */ \
    X(TR_KEY_show_toolbar, "show_toolbar") /* gtk app, qt app */ \
    X(TR_KEY_show_tracker_scrapes, "show_tracker_scrapes") /* gtk app, qt app */ \
    X(TR_KEY_sitename, "sitename") /* rpc */ \
    X(TR_KEY_size_bytes, "size_bytes") /* rpc */ \
    X(TR_KEY_size_units, "size_units") /* rpc */ \
    X(TR_KEY_size_when_done, "size_when_done") /* rpc */ \
    X(TR_KEY_sleep_per_seconds_during_verify, "sleep_per_seconds_during_verify") /* tr_session::Settings */ \
    X(TR_KEY_socket_address, "socket_address") /* .resume */ \
    X(TR_KEY_sort_mode, "sort_mode") /* gtk app, qt app */ \
    X(TR_KEY_sort_reversed, "sort_reversed") /* gtk app, qt app */ \
    X(TR_KEY_source, "source") /* .torrent */ \
    X(TR_KEY_speed, "speed") /* .resume */ \
    X(TR_KEY_speed_Bps, "speed_Bps") /* .resume */ \
    X(TR_KEY_speed_bytes, "speed_bytes") /* rpc */ \
    X(TR_KEY_speed_limit_down, "speed_limit_down") /* .resume, gtk app, rpc, tr_session::Settings */ \
    X(TR_KEY_speed_limit_down_enabled, "speed_limit_down_enabled") /* rpc, tr_session::Settings */ \
    X(TR_KEY_speed_limit_up, "speed_limit_up") /* .resume, gtk app, rpc, tr_session::Settings */ \
    X(TR_KEY_speed_limit_up_enabled, "speed_limit_up_enabled") /* rpc, tr_session::Settings */ \
    X(TR_KEY_speed_units, "speed_units") /* rpc */ \
    X(TR_KEY_start_added_torrents, "start_added_torrents") /* gtk app, rpc, tr_session::Settings */ \
    X(TR_KEY_start_date, "start_date") /* rpc */ \
    X(TR_KEY_start_minimized, "start_minimized") /* qt app */ \
    X(TR_KEY_start_paused, "start_paused") /* daemon */ \
    X(TR_KEY_status, "status") /* rpc */ \
    X(TR_KEY_statusbar_stats, "statusbar_stats") /* gtk app, qt app */ \
    X(TR_KEY_tag, "tag") /* rpc */ \
    X(TR_KEY_tcp_enabled, "tcp_enabled") /* rpc, tr_session::Settings */ \
    X(TR_KEY_tier, "tier") /* rpc */ \
    X(TR_KEY_time_checked, "time_checked") /* .resume */ \
    X(TR_KEY_torrent_get_kebab, "torrent-get") /* rpc */ \
    X(TR_KEY_torrent_add, "torrent_add") /* rpc */ \
    X(TR_KEY_torrent_added, "torrent_added") /* rpc */ \
    X(TR_KEY_torrent_added_notification_enabled, "torrent_added_notification_enabled") /* gtk app, qt app */ \
    X(TR_KEY_torrent_added_verify_mode, "torrent_added_verify_mode") /* tr_session::Settings */ \
    X(TR_KEY_torrent_complete_notification_enabled, "torrent_complete_notification_enabled") /* gtk app, qt app */ \
    X(TR_KEY_torrent_complete_sound_command, "torrent_complete_sound_command") /* gtk app, qt app */ \
    X(TR_KEY_torrent_complete_sound_enabled, "torrent_complete_sound_enabled") /* gtk app, qt app */ \
    X(TR_KEY_torrent_complete_verify_enabled, "torrent_complete_verify_enabled") /* tr_session::Settings */ \
    X(TR_KEY_torrent_count, "torrent_count") /* rpc */ \
    X(TR_KEY_torrent_duplicate, "torrent_duplicate") /* rpc */ \
    X(TR_KEY_torrent_file, "torrent_file") /* rpc */ \
    X(TR_KEY_torrent_get, "torrent_get") /* rpc */ \
    X(TR_KEY_torrent_reannounce, "torrent_reannounce") /* rpc */ \
    X(TR_KEY_torrent_remove, "torrent_remove") /* rpc */ \
    X(TR_KEY_torrent_rename_path, "torrent_rename_path") /* rpc */ \
    X(TR_KEY_torrent_set, "torrent_set") /* rpc */ \
    X(TR_KEY_torrent_set_location, "torrent_set_location") /* rpc */ \
    X(TR_KEY_torrent_start, "torrent_start") /* rpc */ \
    X(TR_KEY_torrent_start_now, "torrent_start_now") /* rpc */ \
    X(TR_KEY_torrent_stop, "torrent_stop") /* rpc */ \
    X(TR_KEY_torrent_verify, "torrent_verify") /* rpc */ \
    X(TR_KEY_torrents, "torrents") /* rpc */ \
    X(TR_KEY_total_size, "total_size") /* BT protocol, rpc */ \
    X(TR_KEY_tracker_add, "tracker_add") /* rpc */ \
    X(TR_KEY_tracker_list, "tracker_list") /* rpc */ \
    X(TR_KEY_tracker_remove, "tracker_remove") /* rpc */ \
    X(TR_KEY_tracker_replace, "tracker_replace") /* rpc */ \
    X(TR_KEY_tracker_stats, "tracker_stats") /* rpc */ \
    X(TR_KEY_trackers, "trackers") /* rpc */ \
    X(TR_KEY_trash_can_enabled, "trash_can_enabled") /* gtk app */ \
    X(TR_KEY_trash_original_torrent_files, "trash_original_torrent_files") /* gtk app, rpc, tr_session::Settings */ \
    X(TR_KEY_umask, "umask") /* tr_session::Settings */ \
    X(TR_KEY_units, "units") /* rpc */ \
    X(TR_KEY_upload_limit, "upload_limit") /* rpc */ \
    X(TR_KEY_upload_limited, "upload_limited") /* rpc */ \
    X(TR_KEY_upload_only, "upload_only") /* BEP0021; BT protocol */ \
    X(TR_KEY_upload_ratio, "upload_ratio") /* rpc */ \
    X(TR_KEY_upload_slots_per_torrent, "upload_slots_per_torrent") /* tr_session::Settings */ \
    X(TR_KEY_upload_speed, "upload_speed") /* rpc */ \
    X(TR_KEY_uploaded, "uploaded") /* .resume */ \
    X(TR_KEY_uploaded_bytes, "uploaded_bytes") /* rpc, stats.json */ \
    X(TR_KEY_uploaded_ever, "uploaded_ever") /* rpc */ \
    X(TR_KEY_url_list, "url-list") /* .torrent */ \
    X(TR_KEY_use_global_speed_limit, "use_global_speed_limit") /* .resume */ \
    X(TR_KEY_use_speed_limit, "use_speed_limit") /* .resume */ \
    X(TR_KEY_ut_holepunch, "ut_holepunch") /* BT protocol */ \
    X(TR_KEY_ut_metadata, "ut_metadata") /* BEP0011; BT protocol */ \
    X(TR_KEY_ut_pex, "ut_pex") /* BEP0010, BEP0011; BT protocol */ \
    X(TR_KEY_utp_enabled, "utp_enabled") /* daemon, rpc, tr_session::Settings */ \
    X(TR_KEY_v, "v") /* BEP0010; BT protocol */ \
    X(TR_KEY_version, "version") /* rpc */ \
    X(TR_KEY_wanted, "wanted") /* rpc */ \
    X(TR_KEY_watch_dir, "watch_dir") /* daemon, gtk app, qt app */ \
    X(TR_KEY_watch_dir_enabled, "watch_dir_enabled") /* daemon, gtk app, qt app */ \
    X(TR_KEY_watch_dir_force_generic, "watch_dir_force_generic") /* daemon */ \
    X(TR_KEY_webseeds, "webseeds") /* rpc */ \
    X(TR_KEY_webseeds_sending_to_us, "webseeds_sending_to_us") /* rpc */ \
    X(TR_KEY_yourip, "yourip") /* BEP0010; BT protocol */

#define MAKE_KNOWN_KEY(_key, _str) inline constexpr auto _key = transmission::symbol::known(_str);
KNOWN_KEYS(MAKE_KNOWN_KEY)
#undef MAKE_KNOWN_KEY

/**
 * Find the quark that matches the specified string
 *
 * @return true if the specified string exists as a quark
 */
[[nodiscard]] std::optional<tr_quark> tr_quark_lookup(std::string_view str);

/**
 * Get the string view that corresponds to the specified quark.
 *
 * Note: this view is guaranteed to be zero-terminated at view[std::size(view)]
 */
[[nodiscard]] std::string_view tr_quark_get_string_view(tr_quark quark);

/**
 * Create a new quark for the specified string. If a quark already
 * exists for that string, it is returned so that no duplicates are
 * created.
 */
[[nodiscard]] tr_quark tr_quark_new(std::string_view str);
