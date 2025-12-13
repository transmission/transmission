// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <iostream>

#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>
#include <vector>

#include "libtransmission/api-compat.h"
#include "libtransmission/quark.h"
#include "libtransmission/rpcimpl.h"
#include "libtransmission/utils.h"
#include "libtransmission/variant.h"

namespace libtransmission::api_compat
{
namespace
{
struct ApiKey
{
    // snake-case quark
    tr_quark current;

    // legacy mixed-case RPC quark (pre-05aef3e7)
    tr_quark legacy;
};

auto constexpr RpcKeys = std::array<ApiKey, 212U>{ {
    { TR_KEY_active_torrent_count, TR_KEY_active_torrent_count_camel },
    { TR_KEY_activity_date, TR_KEY_activity_date_camel }, // TODO(ckerr) legacy duplicate
    { TR_KEY_added_date, TR_KEY_added_date_camel }, // TODO(ckerr) legacy duplicate
    { TR_KEY_alt_speed_down, TR_KEY_alt_speed_down_kebab },
    { TR_KEY_alt_speed_enabled, TR_KEY_alt_speed_enabled_kebab },
    { TR_KEY_alt_speed_time_begin, TR_KEY_alt_speed_time_begin_kebab },
    { TR_KEY_alt_speed_time_day, TR_KEY_alt_speed_time_day_kebab },
    { TR_KEY_alt_speed_time_enabled, TR_KEY_alt_speed_time_enabled_kebab },
    { TR_KEY_alt_speed_time_end, TR_KEY_alt_speed_time_end_kebab },
    { TR_KEY_alt_speed_up, TR_KEY_alt_speed_up_kebab },
    { TR_KEY_announce_state, TR_KEY_announce_state_camel },
    { TR_KEY_anti_brute_force_enabled, TR_KEY_anti_brute_force_enabled_kebab },
    { TR_KEY_anti_brute_force_threshold, TR_KEY_anti_brute_force_threshold_kebab },
    { TR_KEY_bandwidth_priority, TR_KEY_bandwidth_priority_camel }, // TODO(ckerr) legacy duplicate
    { TR_KEY_blocklist_enabled, TR_KEY_blocklist_enabled_kebab },
    { TR_KEY_blocklist_size, TR_KEY_blocklist_size_kebab },
    { TR_KEY_blocklist_url, TR_KEY_blocklist_url_kebab },
    { TR_KEY_bytes_completed, TR_KEY_bytes_completed_camel },
    { TR_KEY_cache_size_mb, TR_KEY_cache_size_mb_kebab },
    { TR_KEY_client_is_choked, TR_KEY_client_is_choked_camel },
    { TR_KEY_client_is_interested, TR_KEY_client_is_interested_camel },
    { TR_KEY_client_name, TR_KEY_client_name_camel },
    { TR_KEY_config_dir, TR_KEY_config_dir_kebab },
    { TR_KEY_corrupt_ever, TR_KEY_corrupt_ever_camel },
    { TR_KEY_cumulative_stats, TR_KEY_cumulative_stats_kebab },
    { TR_KEY_current_stats, TR_KEY_current_stats_kebab },
    { TR_KEY_date_created, TR_KEY_date_created_camel },
    { TR_KEY_default_trackers, TR_KEY_default_trackers_kebab },
    { TR_KEY_delete_local_data, TR_KEY_delete_local_data_kebab },
    { TR_KEY_desired_available, TR_KEY_desired_available_camel },
    { TR_KEY_dht_enabled, TR_KEY_dht_enabled_kebab },
    { TR_KEY_done_date, TR_KEY_done_date_camel }, // TODO(ckerr) legacy duplicate
    { TR_KEY_download_count, TR_KEY_download_count_camel },
    { TR_KEY_download_dir, TR_KEY_download_dir_kebab }, // Crazy case 1: camel in torrent-get/set, kebab everywhere else
    { TR_KEY_download_dir_free_space, TR_KEY_download_dir_free_space_kebab },
    { TR_KEY_download_limit, TR_KEY_download_limit_camel },
    { TR_KEY_download_limited, TR_KEY_download_limited_camel },
    { TR_KEY_download_queue_enabled, TR_KEY_download_queue_enabled_kebab },
    { TR_KEY_download_queue_size, TR_KEY_download_queue_size_kebab },
    { TR_KEY_download_speed, TR_KEY_download_speed_camel },
    { TR_KEY_downloaded_bytes, TR_KEY_downloaded_bytes_camel }, // TODO(ckerr) legacy duplicate
    { TR_KEY_downloaded_ever, TR_KEY_downloaded_ever_camel },
    { TR_KEY_edit_date, TR_KEY_edit_date_camel },
    { TR_KEY_error_string, TR_KEY_error_string_camel },
    { TR_KEY_eta_idle, TR_KEY_eta_idle_camel },
    { TR_KEY_file_count, TR_KEY_file_count_kebab },
    { TR_KEY_file_stats, TR_KEY_file_stats_camel },
    { TR_KEY_files_added, TR_KEY_files_added_camel }, // TODO(ckerr) legacy duplicate
    { TR_KEY_files_unwanted, TR_KEY_files_unwanted_kebab },
    { TR_KEY_files_wanted, TR_KEY_files_wanted_kebab },
    { TR_KEY_flag_str, TR_KEY_flag_str_camel },
    { TR_KEY_from_cache, TR_KEY_from_cache_camel },
    { TR_KEY_from_dht, TR_KEY_from_dht_camel },
    { TR_KEY_from_incoming, TR_KEY_from_incoming_camel },
    { TR_KEY_from_lpd, TR_KEY_from_lpd_camel },
    { TR_KEY_from_ltep, TR_KEY_from_ltep_camel },
    { TR_KEY_from_pex, TR_KEY_from_pex_camel },
    { TR_KEY_from_tracker, TR_KEY_from_tracker_camel },
    { TR_KEY_has_announced, TR_KEY_has_announced_camel },
    { TR_KEY_has_scraped, TR_KEY_has_scraped_camel },
    { TR_KEY_hash_string, TR_KEY_hash_string_camel },
    { TR_KEY_have_unchecked, TR_KEY_have_unchecked_camel },
    { TR_KEY_have_valid, TR_KEY_have_valid_camel },
    { TR_KEY_honors_session_limits, TR_KEY_honors_session_limits_camel },
    { TR_KEY_idle_seeding_limit, TR_KEY_idle_seeding_limit_kebab },
    { TR_KEY_idle_seeding_limit_enabled, TR_KEY_idle_seeding_limit_enabled_kebab },
    { TR_KEY_incomplete_dir, TR_KEY_incomplete_dir_kebab },
    { TR_KEY_incomplete_dir_enabled, TR_KEY_incomplete_dir_enabled_kebab },
    { TR_KEY_is_backup, TR_KEY_is_backup_camel },
    { TR_KEY_is_downloading_from, TR_KEY_is_downloading_from_camel },
    { TR_KEY_is_encrypted, TR_KEY_is_encrypted_camel },
    { TR_KEY_is_finished, TR_KEY_is_finished_camel },
    { TR_KEY_is_incoming, TR_KEY_is_incoming_camel },
    { TR_KEY_is_private, TR_KEY_is_private_camel },
    { TR_KEY_is_stalled, TR_KEY_is_stalled_camel },
    { TR_KEY_is_uploading_to, TR_KEY_is_uploading_to_camel },
    { TR_KEY_is_utp, TR_KEY_is_utp_camel },
    { TR_KEY_last_announce_peer_count, TR_KEY_last_announce_peer_count_camel },
    { TR_KEY_last_announce_result, TR_KEY_last_announce_result_camel },
    { TR_KEY_last_announce_start_time, TR_KEY_last_announce_start_time_camel },
    { TR_KEY_last_announce_succeeded, TR_KEY_last_announce_succeeded_camel },
    { TR_KEY_last_announce_time, TR_KEY_last_announce_time_camel },
    { TR_KEY_last_announce_timed_out, TR_KEY_last_announce_timed_out_camel },
    { TR_KEY_last_scrape_result, TR_KEY_last_scrape_result_camel },
    { TR_KEY_last_scrape_start_time, TR_KEY_last_scrape_start_time_camel },
    { TR_KEY_last_scrape_succeeded, TR_KEY_last_scrape_succeeded_camel },
    { TR_KEY_last_scrape_time, TR_KEY_last_scrape_time_camel },
    { TR_KEY_last_scrape_timed_out, TR_KEY_last_scrape_timed_out_camel },
    { TR_KEY_leecher_count, TR_KEY_leecher_count_camel },
    { TR_KEY_left_until_done, TR_KEY_left_until_done_camel },
    { TR_KEY_lpd_enabled, TR_KEY_lpd_enabled_kebab },
    { TR_KEY_magnet_link, TR_KEY_magnet_link_camel },
    { TR_KEY_manual_announce_time, TR_KEY_manual_announce_time_camel },
    { TR_KEY_max_connected_peers, TR_KEY_max_connected_peers_camel },
    { TR_KEY_memory_bytes, TR_KEY_memory_bytes_kebab },
    { TR_KEY_memory_units, TR_KEY_memory_units_kebab },
    { TR_KEY_metadata_percent_complete, TR_KEY_metadata_percent_complete_camel },
    { TR_KEY_next_announce_time, TR_KEY_next_announce_time_camel },
    { TR_KEY_next_scrape_time, TR_KEY_next_scrape_time_camel },
    { TR_KEY_paused_torrent_count, TR_KEY_paused_torrent_count_camel },
    { TR_KEY_peer_is_choked, TR_KEY_peer_is_choked_camel },
    { TR_KEY_peer_is_interested, TR_KEY_peer_is_interested_camel },
    { TR_KEY_peer_limit, TR_KEY_peer_limit_kebab },
    { TR_KEY_peer_limit_global, TR_KEY_peer_limit_global_kebab },
    { TR_KEY_peer_limit_per_torrent, TR_KEY_peer_limit_per_torrent_kebab },
    { TR_KEY_peer_port, TR_KEY_peer_port_kebab },
    { TR_KEY_peer_port_random_on_start, TR_KEY_peer_port_random_on_start_kebab },
    { TR_KEY_peers_connected, TR_KEY_peers_connected_camel },
    { TR_KEY_peers_from, TR_KEY_peers_from_camel },
    { TR_KEY_peers_getting_from_us, TR_KEY_peers_getting_from_us_camel },
    { TR_KEY_peers_sending_to_us, TR_KEY_peers_sending_to_us_camel },
    { TR_KEY_percent_complete, TR_KEY_percent_complete_camel },
    { TR_KEY_percent_done, TR_KEY_percent_done_camel },
    { TR_KEY_pex_enabled, TR_KEY_pex_enabled_kebab },
    { TR_KEY_piece_count, TR_KEY_piece_count_camel },
    { TR_KEY_piece_size, TR_KEY_piece_size_camel },
    { TR_KEY_port_forwarding_enabled, TR_KEY_port_forwarding_enabled_kebab },
    { TR_KEY_port_is_open, TR_KEY_port_is_open_kebab },
    { TR_KEY_primary_mime_type, TR_KEY_primary_mime_type_kebab },
    { TR_KEY_priority_high, TR_KEY_priority_high_kebab },
    { TR_KEY_priority_low, TR_KEY_priority_low_kebab },
    { TR_KEY_priority_normal, TR_KEY_priority_normal_kebab },
    { TR_KEY_queue_position, TR_KEY_queue_position_camel },
    { TR_KEY_queue_stalled_enabled, TR_KEY_queue_stalled_enabled_kebab },
    { TR_KEY_queue_stalled_minutes, TR_KEY_queue_stalled_minutes_kebab },
    { TR_KEY_rate_download, TR_KEY_rate_download_camel },
    { TR_KEY_rate_to_client, TR_KEY_rate_to_client_camel },
    { TR_KEY_rate_to_peer, TR_KEY_rate_to_peer_camel },
    { TR_KEY_rate_upload, TR_KEY_rate_upload_camel },
    { TR_KEY_recently_active, TR_KEY_recently_active_kebab },
    { TR_KEY_recheck_progress, TR_KEY_recheck_progress_camel },
    { TR_KEY_rename_partial_files, TR_KEY_rename_partial_files_kebab },
    { TR_KEY_rpc_host_whitelist, TR_KEY_rpc_host_whitelist_kebab },
    { TR_KEY_rpc_host_whitelist_enabled, TR_KEY_rpc_host_whitelist_enabled_kebab },
    { TR_KEY_rpc_version, TR_KEY_rpc_version_kebab },
    { TR_KEY_rpc_version_minimum, TR_KEY_rpc_version_minimum_kebab },
    { TR_KEY_rpc_version_semver, TR_KEY_rpc_version_semver_kebab },
    { TR_KEY_scrape_state, TR_KEY_scrape_state_camel },
    { TR_KEY_script_torrent_added_enabled, TR_KEY_script_torrent_added_enabled_kebab },
    { TR_KEY_script_torrent_added_filename, TR_KEY_script_torrent_added_filename_kebab },
    { TR_KEY_script_torrent_done_enabled, TR_KEY_script_torrent_done_enabled_kebab },
    { TR_KEY_script_torrent_done_filename, TR_KEY_script_torrent_done_filename_kebab },
    { TR_KEY_script_torrent_done_seeding_enabled, TR_KEY_script_torrent_done_seeding_enabled_kebab },
    { TR_KEY_script_torrent_done_seeding_filename, TR_KEY_script_torrent_done_seeding_filename_kebab },
    { TR_KEY_seconds_active, TR_KEY_seconds_active_camel }, // TODO(ckerr) legacy duplicate
    { TR_KEY_seconds_downloading, TR_KEY_seconds_downloading_camel },
    { TR_KEY_seconds_seeding, TR_KEY_seconds_seeding_camel },
    { TR_KEY_seed_idle_limit, TR_KEY_seed_idle_limit_camel },
    { TR_KEY_seed_idle_mode, TR_KEY_seed_idle_mode_camel },
    { TR_KEY_seed_queue_enabled, TR_KEY_seed_queue_enabled_kebab },
    { TR_KEY_seed_queue_size, TR_KEY_seed_queue_size_kebab },
    { TR_KEY_seed_ratio_limit, TR_KEY_seed_ratio_limit_camel },
    { TR_KEY_seed_ratio_limited, TR_KEY_seed_ratio_limited_camel },
    { TR_KEY_seed_ratio_mode, TR_KEY_seed_ratio_mode_camel },
    { TR_KEY_seeder_count, TR_KEY_seeder_count_camel },
    { TR_KEY_session_count, TR_KEY_session_count_camel }, // TODO(ckerr) legacy duplicate
    { TR_KEY_session_id, TR_KEY_session_id_kebab },
    { TR_KEY_size_bytes, TR_KEY_size_bytes_kebab },
    { TR_KEY_size_units, TR_KEY_size_units_kebab },
    { TR_KEY_size_when_done, TR_KEY_size_when_done_camel },
    { TR_KEY_speed_bytes, TR_KEY_speed_bytes_kebab },
    { TR_KEY_speed_limit_down, TR_KEY_speed_limit_down_kebab },
    { TR_KEY_speed_limit_down_enabled, TR_KEY_speed_limit_down_enabled_kebab },
    { TR_KEY_speed_limit_up, TR_KEY_speed_limit_up_kebab },
    { TR_KEY_speed_limit_up_enabled, TR_KEY_speed_limit_up_enabled_kebab },
    { TR_KEY_speed_units, TR_KEY_speed_units_kebab },
    { TR_KEY_start_added_torrents, TR_KEY_start_added_torrents_kebab },
    { TR_KEY_start_date, TR_KEY_start_date_camel },
    { TR_KEY_tcp_enabled, TR_KEY_tcp_enabled_kebab },
    { TR_KEY_torrent_added, TR_KEY_torrent_added_kebab },
    { TR_KEY_torrent_count, TR_KEY_torrent_count_camel },
    { TR_KEY_torrent_duplicate, TR_KEY_torrent_duplicate_kebab },
    { TR_KEY_torrent_file, TR_KEY_torrent_file_camel },
    { TR_KEY_total_size, TR_KEY_total_size_camel },
    { TR_KEY_tracker_add, TR_KEY_tracker_add_camel },
    { TR_KEY_tracker_list, TR_KEY_tracker_list_camel },
    { TR_KEY_tracker_remove, TR_KEY_tracker_remove_camel },
    { TR_KEY_tracker_replace, TR_KEY_tracker_replace_camel },
    { TR_KEY_tracker_stats, TR_KEY_tracker_stats_camel },
    { TR_KEY_trash_original_torrent_files, TR_KEY_trash_original_torrent_files_kebab },
    { TR_KEY_upload_limit, TR_KEY_upload_limit_camel },
    { TR_KEY_upload_limited, TR_KEY_upload_limited_camel },
    { TR_KEY_upload_ratio, TR_KEY_upload_ratio_camel },
    { TR_KEY_upload_speed, TR_KEY_upload_speed_camel },
    { TR_KEY_uploaded_bytes, TR_KEY_uploaded_bytes_camel }, // TODO(ckerr) legacy duplicate
    { TR_KEY_uploaded_ever, TR_KEY_uploaded_ever_camel },
    { TR_KEY_utp_enabled, TR_KEY_utp_enabled_kebab },
    { TR_KEY_webseeds_sending_to_us, TR_KEY_webseeds_sending_to_us_camel },
    { TR_KEY_blocklist_update, TR_KEY_blocklist_update_kebab },
    { TR_KEY_free_space, TR_KEY_free_space_kebab },
    { TR_KEY_group_get, TR_KEY_group_get_kebab },
    { TR_KEY_group_set, TR_KEY_group_set_kebab },
    { TR_KEY_port_test, TR_KEY_port_test_kebab },
    { TR_KEY_queue_move_bottom, TR_KEY_queue_move_bottom_kebab },
    { TR_KEY_queue_move_down, TR_KEY_queue_move_down_kebab },
    { TR_KEY_queue_move_top, TR_KEY_queue_move_top_kebab },
    { TR_KEY_queue_move_up, TR_KEY_queue_move_up_kebab },
    { TR_KEY_session_close, TR_KEY_session_close_kebab },
    { TR_KEY_session_get, TR_KEY_session_get_kebab },
    { TR_KEY_session_set, TR_KEY_session_set_kebab },
    { TR_KEY_session_stats, TR_KEY_session_stats_kebab },
    { TR_KEY_torrent_add, TR_KEY_torrent_add_kebab },
    { TR_KEY_torrent_get, TR_KEY_torrent_get_kebab },
    { TR_KEY_torrent_reannounce, TR_KEY_torrent_reannounce_kebab },
    { TR_KEY_torrent_remove, TR_KEY_torrent_remove_kebab },
    { TR_KEY_torrent_rename_path, TR_KEY_torrent_rename_path_kebab },
    { TR_KEY_torrent_set, TR_KEY_torrent_set_kebab },
    { TR_KEY_torrent_set_location, TR_KEY_torrent_set_location_kebab },
    { TR_KEY_torrent_start, TR_KEY_torrent_start_kebab },
    { TR_KEY_torrent_start_now, TR_KEY_torrent_start_now_kebab },
    { TR_KEY_torrent_stop, TR_KEY_torrent_stop_kebab },
    { TR_KEY_torrent_verify, TR_KEY_torrent_verify_kebab },
} };

auto constexpr SessionKeys = std::array<ApiKey, 219U>{ {
    // .resume
    { TR_KEY_peers2_6, TR_KEY_peers2_6_kebab },
    { TR_KEY_speed_Bps, TR_KEY_speed_Bps_kebab },
    { TR_KEY_use_global_speed_limit, TR_KEY_use_global_speed_limit_kebab },
    { TR_KEY_use_speed_limit, TR_KEY_use_speed_limit_kebab },
    { TR_KEY_speed_limit_down, TR_KEY_speed_limit_down_kebab },
    { TR_KEY_speed_limit_up, TR_KEY_speed_limit_up_kebab },
    { TR_KEY_ratio_limit, TR_KEY_ratio_limit_kebab },
    { TR_KEY_ratio_mode, TR_KEY_ratio_mode_kebab },
    { TR_KEY_idle_limit, TR_KEY_idle_limit_kebab },
    { TR_KEY_idle_mode, TR_KEY_idle_mode_kebab },
    { TR_KEY_speed_Bps, TR_KEY_speed_Bps_kebab },
    { TR_KEY_use_speed_limit, TR_KEY_use_speed_limit_kebab },
    { TR_KEY_use_global_speed_limit, TR_KEY_use_global_speed_limit_kebab },
    { TR_KEY_speed_limit_up, TR_KEY_speed_limit_up_kebab },
    { TR_KEY_speed_limit_down, TR_KEY_speed_limit_down_kebab },
    { TR_KEY_ratio_limit, TR_KEY_ratio_limit_kebab },
    { TR_KEY_ratio_mode, TR_KEY_ratio_mode_kebab },
    { TR_KEY_idle_limit, TR_KEY_idle_limit_kebab },
    { TR_KEY_idle_mode, TR_KEY_idle_mode_kebab },
    { TR_KEY_time_checked, TR_KEY_time_checked_kebab },
    { TR_KEY_incomplete_dir, TR_KEY_incomplete_dir_kebab },
    { TR_KEY_max_peers, TR_KEY_max_peers_kebab },
    { TR_KEY_added_date, TR_KEY_added_date_kebab }, // TODO(ckerr) legacy duplicate
    { TR_KEY_done_date, TR_KEY_done_date_kebab }, // TODO(ckerr) legacy duplicate
    { TR_KEY_activity_date, TR_KEY_activity_date_kebab }, // TODO(ckerr) legacy duplicate
    { TR_KEY_seeding_time_seconds, TR_KEY_seeding_time_seconds_kebab },
    { TR_KEY_downloading_time_seconds, TR_KEY_downloading_time_seconds_kebab },
    { TR_KEY_bandwidth_priority, TR_KEY_bandwidth_priority_kebab }, // TODO(ckerr) legacy duplicate

    // stats.json
    { TR_KEY_downloaded_bytes, TR_KEY_downloaded_bytes_kebab }, // TODO(ckerr) legacy duplicate
    { TR_KEY_files_added, TR_KEY_files_added_kebab }, // TODO(ckerr) legacy duplicate
    { TR_KEY_seconds_active, TR_KEY_seconds_active_kebab }, // TODO(ckerr) legacy duplicate
    { TR_KEY_session_count, TR_KEY_session_count_kebab }, // TODO(ckerr) legacy duplicate
    { TR_KEY_uploaded_bytes, TR_KEY_uploaded_bytes_kebab }, // TODO(ckerr) legacy duplicate

    // tr_session::Settings
    { TR_KEY_announce_ip, TR_KEY_announce_ip_kebab },
    { TR_KEY_announce_ip_enabled, TR_KEY_announce_ip_enabled_kebab },
    { TR_KEY_bind_address_ipv4, TR_KEY_bind_address_ipv4_kebab },
    { TR_KEY_bind_address_ipv6, TR_KEY_bind_address_ipv6_kebab },
    { TR_KEY_blocklist_enabled, TR_KEY_blocklist_enabled_kebab },
    { TR_KEY_blocklist_url, TR_KEY_blocklist_url_kebab },
    { TR_KEY_cache_size_mb, TR_KEY_cache_size_mb_kebab },
    { TR_KEY_default_trackers, TR_KEY_default_trackers_kebab },
    { TR_KEY_dht_enabled, TR_KEY_dht_enabled_kebab },
    { TR_KEY_download_dir, TR_KEY_download_dir_kebab }, // TODO(ckerr) legacy duplicate
    { TR_KEY_download_queue_enabled, TR_KEY_download_queue_enabled_kebab },
    { TR_KEY_download_queue_size, TR_KEY_download_queue_size_kebab },
    { TR_KEY_idle_seeding_limit, TR_KEY_idle_seeding_limit_kebab },
    { TR_KEY_idle_seeding_limit_enabled, TR_KEY_idle_seeding_limit_enabled_kebab },
    { TR_KEY_incomplete_dir, TR_KEY_incomplete_dir_kebab },
    { TR_KEY_incomplete_dir_enabled, TR_KEY_incomplete_dir_enabled_kebab },
    { TR_KEY_lpd_enabled, TR_KEY_lpd_enabled_kebab },
    { TR_KEY_message_level, TR_KEY_message_level_kebab },
    { TR_KEY_peer_congestion_algorithm, TR_KEY_peer_congestion_algorithm_kebab },
    { TR_KEY_peer_limit_global, TR_KEY_peer_limit_global_kebab },
    { TR_KEY_peer_limit_per_torrent, TR_KEY_peer_limit_per_torrent_kebab },
    { TR_KEY_peer_port, TR_KEY_peer_port_kebab },
    { TR_KEY_peer_port_random_high, TR_KEY_peer_port_random_high_kebab },
    { TR_KEY_peer_port_random_low, TR_KEY_peer_port_random_low_kebab },
    { TR_KEY_peer_port_random_on_start, TR_KEY_peer_port_random_on_start_kebab },
    { TR_KEY_peer_socket_tos, TR_KEY_peer_socket_tos_kebab },
    { TR_KEY_pex_enabled, TR_KEY_pex_enabled_kebab },
    { TR_KEY_port_forwarding_enabled, TR_KEY_port_forwarding_enabled_kebab },
    { TR_KEY_queue_stalled_enabled, TR_KEY_queue_stalled_enabled_kebab },
    { TR_KEY_queue_stalled_minutes, TR_KEY_queue_stalled_minutes_kebab },
    { TR_KEY_ratio_limit, TR_KEY_ratio_limit_kebab },
    { TR_KEY_ratio_limit_enabled, TR_KEY_ratio_limit_enabled_kebab },
    { TR_KEY_rename_partial_files, TR_KEY_rename_partial_files_kebab },
    { TR_KEY_scrape_paused_torrents_enabled, TR_KEY_scrape_paused_torrents_enabled_kebab },
    { TR_KEY_script_torrent_added_enabled, TR_KEY_script_torrent_added_enabled_kebab },
    { TR_KEY_script_torrent_added_filename, TR_KEY_script_torrent_added_filename_kebab },
    { TR_KEY_script_torrent_done_enabled, TR_KEY_script_torrent_done_enabled_kebab },
    { TR_KEY_script_torrent_done_filename, TR_KEY_script_torrent_done_filename_kebab },
    { TR_KEY_script_torrent_done_seeding_enabled, TR_KEY_script_torrent_done_seeding_enabled_kebab },
    { TR_KEY_script_torrent_done_seeding_filename, TR_KEY_script_torrent_done_seeding_filename_kebab },
    { TR_KEY_seed_queue_enabled, TR_KEY_seed_queue_enabled_kebab },
    { TR_KEY_seed_queue_size, TR_KEY_seed_queue_size_kebab },
    { TR_KEY_speed_limit_down, TR_KEY_speed_limit_down_kebab },
    { TR_KEY_speed_limit_down_enabled, TR_KEY_speed_limit_down_enabled_kebab },
    { TR_KEY_speed_limit_up, TR_KEY_speed_limit_up_kebab },
    { TR_KEY_speed_limit_up_enabled, TR_KEY_speed_limit_up_enabled_kebab },
    { TR_KEY_start_added_torrents, TR_KEY_start_added_torrents_kebab },
    { TR_KEY_tcp_enabled, TR_KEY_tcp_enabled_kebab },
    { TR_KEY_torrent_added_verify_mode, TR_KEY_torrent_added_verify_mode_kebab },
    { TR_KEY_trash_original_torrent_files, TR_KEY_trash_original_torrent_files_kebab },
    { TR_KEY_upload_slots_per_torrent, TR_KEY_upload_slots_per_torrent_kebab },
    { TR_KEY_utp_enabled, TR_KEY_utp_enabled_kebab },

    // rpc server settings
    { TR_KEY_anti_brute_force_enabled, TR_KEY_anti_brute_force_enabled_kebab },
    { TR_KEY_anti_brute_force_threshold, TR_KEY_anti_brute_force_threshold_kebab },
    { TR_KEY_rpc_authentication_required, TR_KEY_rpc_authentication_required_kebab },
    { TR_KEY_rpc_bind_address, TR_KEY_rpc_bind_address_kebab },
    { TR_KEY_rpc_enabled, TR_KEY_rpc_enabled_kebab },
    { TR_KEY_rpc_host_whitelist, TR_KEY_rpc_host_whitelist_kebab },
    { TR_KEY_rpc_host_whitelist_enabled, TR_KEY_rpc_host_whitelist_enabled_kebab },
    { TR_KEY_rpc_port, TR_KEY_rpc_port_kebab },
    { TR_KEY_rpc_password, TR_KEY_rpc_password_kebab },
    { TR_KEY_rpc_socket_mode, TR_KEY_rpc_socket_mode_kebab },
    { TR_KEY_rpc_url, TR_KEY_rpc_url_kebab },
    { TR_KEY_rpc_username, TR_KEY_rpc_username_kebab },
    { TR_KEY_rpc_whitelist, TR_KEY_rpc_whitelist_kebab },
    { TR_KEY_rpc_whitelist_enabled, TR_KEY_rpc_whitelist_enabled_kebab },

    // speed settings
    { TR_KEY_alt_speed_enabled, TR_KEY_alt_speed_enabled_kebab },
    { TR_KEY_alt_speed_up, TR_KEY_alt_speed_up_kebab },
    { TR_KEY_alt_speed_down, TR_KEY_alt_speed_down_kebab },
    { TR_KEY_alt_speed_time_enabled, TR_KEY_alt_speed_time_enabled_kebab },
    { TR_KEY_alt_speed_time_day, TR_KEY_alt_speed_time_day_kebab },
    { TR_KEY_alt_speed_time_begin, TR_KEY_alt_speed_time_begin_kebab },
    { TR_KEY_alt_speed_time_end, TR_KEY_alt_speed_time_end_kebab },

    // qt app
    { TR_KEY_show_options_window, TR_KEY_show_options_window_kebab },
    { TR_KEY_open_dialog_dir, TR_KEY_open_dialog_dir_kebab },
    { TR_KEY_inhibit_desktop_hibernation, TR_KEY_inhibit_desktop_hibernation_kebab },
    { TR_KEY_watch_dir, TR_KEY_watch_dir_kebab },
    { TR_KEY_watch_dir_enabled, TR_KEY_watch_dir_enabled_kebab },
    { TR_KEY_show_notification_area_icon, TR_KEY_show_notification_area_icon_kebab },
    { TR_KEY_start_minimized, TR_KEY_start_minimized_kebab },
    { TR_KEY_torrent_added_notification_enabled, TR_KEY_torrent_added_notification_enabled_kebab },
    { TR_KEY_torrent_complete_notification_enabled, TR_KEY_torrent_complete_notification_enabled_kebab },
    { TR_KEY_prompt_before_exit, TR_KEY_prompt_before_exit_kebab },
    { TR_KEY_sort_mode, TR_KEY_sort_mode_kebab },
    { TR_KEY_sort_reversed, TR_KEY_sort_reversed_kebab },
    { TR_KEY_compact_view, TR_KEY_compact_view_kebab },
    { TR_KEY_show_filterbar, TR_KEY_show_filterbar_kebab },
    { TR_KEY_show_statusbar, TR_KEY_show_statusbar_kebab },
    { TR_KEY_statusbar_stats, TR_KEY_statusbar_stats_kebab },
    { TR_KEY_show_tracker_scrapes, TR_KEY_show_tracker_scrapes_kebab },
    { TR_KEY_show_backup_trackers, TR_KEY_show_backup_trackers_kebab },
    { TR_KEY_show_toolbar, TR_KEY_show_toolbar_kebab },
    { TR_KEY_blocklist_date, TR_KEY_blocklist_date_kebab },
    { TR_KEY_blocklist_updates_enabled, TR_KEY_blocklist_updates_enabled_kebab },
    { TR_KEY_main_window_layout_order, TR_KEY_main_window_layout_order_kebab },
    { TR_KEY_main_window_height, TR_KEY_main_window_height_kebab },
    { TR_KEY_main_window_width, TR_KEY_main_window_width_kebab },
    { TR_KEY_main_window_x, TR_KEY_main_window_x_kebab },
    { TR_KEY_main_window_y, TR_KEY_main_window_y_kebab },
    { TR_KEY_filter_mode, TR_KEY_filter_mode_kebab },
    { TR_KEY_filter_trackers, TR_KEY_filter_trackers_kebab },
    { TR_KEY_filter_text, TR_KEY_filter_text_kebab },
    { TR_KEY_remote_session_enabled, TR_KEY_remote_session_enabled_kebab },
    { TR_KEY_remote_session_host, TR_KEY_remote_session_host_kebab },
    { TR_KEY_remote_session_https, TR_KEY_remote_session_https_kebab },
    { TR_KEY_remote_session_password, TR_KEY_remote_session_password_kebab },
    { TR_KEY_remote_session_port, TR_KEY_remote_session_port_kebab },
    { TR_KEY_remote_session_requires_authentication, TR_KEY_remote_session_requres_authentication_kebab },
    { TR_KEY_remote_session_username, TR_KEY_remote_session_username_kebab },
    { TR_KEY_torrent_complete_sound_command, TR_KEY_torrent_complete_sound_command_kebab },
    { TR_KEY_torrent_complete_sound_enabled, TR_KEY_torrent_complete_sound_enabled_kebab },
    { TR_KEY_read_clipboard, TR_KEY_read_clipboard_kebab },

    // daemon
    { TR_KEY_bind_address_ipv4, TR_KEY_bind_address_ipv4_kebab },
    { TR_KEY_bind_address_ipv6, TR_KEY_bind_address_ipv6_kebab },
    { TR_KEY_blocklist_enabled, TR_KEY_blocklist_enabled_kebab },
    { TR_KEY_default_trackers, TR_KEY_default_trackers_kebab },
    { TR_KEY_dht_enabled, TR_KEY_dht_enabled_kebab },
    { TR_KEY_download_dir, TR_KEY_download_dir_kebab }, // TODO(ckerr) legacy duplicate
    { TR_KEY_incomplete_dir, TR_KEY_incomplete_dir_kebab },
    { TR_KEY_incomplete_dir_enabled, TR_KEY_incomplete_dir_enabled_kebab },
    { TR_KEY_lpd_enabled, TR_KEY_lpd_enabled_kebab },
    { TR_KEY_message_level, TR_KEY_message_level_kebab },
    { TR_KEY_peer_limit_global, TR_KEY_peer_limit_global_kebab },
    { TR_KEY_peer_limit_per_torrent, TR_KEY_peer_limit_per_torrent_kebab },
    { TR_KEY_peer_port, TR_KEY_peer_port_kebab },
    { TR_KEY_port_forwarding_enabled, TR_KEY_port_forwarding_enabled_kebab },
    { TR_KEY_ratio_limit, TR_KEY_ratio_limit_kebab },
    { TR_KEY_ratio_limit_enabled, TR_KEY_ratio_limit_enabled_kebab },
    { TR_KEY_rpc_authentication_required, TR_KEY_rpc_authentication_required_kebab },
    { TR_KEY_rpc_bind_address, TR_KEY_rpc_bind_address_kebab },
    { TR_KEY_rpc_enabled, TR_KEY_rpc_enabled_kebab },
    { TR_KEY_rpc_password, TR_KEY_rpc_password_kebab },
    { TR_KEY_rpc_port, TR_KEY_rpc_port_kebab },
    { TR_KEY_rpc_username, TR_KEY_rpc_username_kebab },
    { TR_KEY_rpc_whitelist, TR_KEY_rpc_whitelist_kebab },
    { TR_KEY_rpc_whitelist_enabled, TR_KEY_rpc_whitelist_enabled_kebab },
    { TR_KEY_utp_enabled, TR_KEY_utp_enabled_kebab },
    { TR_KEY_watch_dir, TR_KEY_watch_dir_kebab },
    { TR_KEY_watch_dir_enabled, TR_KEY_watch_dir_enabled_kebab },
    { TR_KEY_watch_dir_force_generic, TR_KEY_watch_dir_force_generic_kebab },

    // gtk app
    { TR_KEY_blocklist_updates_enabled, TR_KEY_blocklist_updates_enabled_kebab },
    { TR_KEY_compact_view, TR_KEY_compact_view_kebab },
    { TR_KEY_details_window_height, TR_KEY_details_window_height_kebab },
    { TR_KEY_details_window_width, TR_KEY_details_window_width_kebab },
    { TR_KEY_download_dir, TR_KEY_download_dir_kebab }, // TODO(ckerr) legacy duplicate
    { TR_KEY_inhibit_desktop_hibernation, TR_KEY_inhibit_desktop_hibernation_kebab },
    { TR_KEY_main_window_height, TR_KEY_main_window_height_kebab },
    { TR_KEY_main_window_is_maximized, TR_KEY_main_window_is_maximized_kebab },
    { TR_KEY_main_window_width, TR_KEY_main_window_width_kebab },
    { TR_KEY_main_window_x, TR_KEY_main_window_x_kebab },
    { TR_KEY_main_window_y, TR_KEY_main_window_y_kebab },
    { TR_KEY_open_dialog_dir, TR_KEY_open_dialog_dir_kebab },
    { TR_KEY_show_backup_trackers, TR_KEY_show_backup_trackers_kebab },
    { TR_KEY_show_extra_peer_details, TR_KEY_show_extra_peer_details_kebab },
    { TR_KEY_show_filterbar, TR_KEY_show_filterbar_kebab },
    { TR_KEY_show_notification_area_icon, TR_KEY_show_notification_area_icon_kebab },
    { TR_KEY_show_options_window, TR_KEY_show_options_window_kebab },
    { TR_KEY_show_statusbar, TR_KEY_show_statusbar_kebab },
    { TR_KEY_show_toolbar, TR_KEY_show_toolbar_kebab },
    { TR_KEY_show_tracker_scrapes, TR_KEY_show_tracker_scrapes_kebab },
    { TR_KEY_sort_mode, TR_KEY_sort_mode_kebab },
    { TR_KEY_sort_reversed, TR_KEY_sort_reversed_kebab },
    { TR_KEY_statusbar_stats, TR_KEY_statusbar_stats_kebab },
    { TR_KEY_torrent_added_notification_enabled, TR_KEY_torrent_added_notification_enabled_kebab },
    { TR_KEY_torrent_complete_notification_enabled, TR_KEY_torrent_complete_notification_enabled_kebab },
    { TR_KEY_torrent_complete_sound_enabled, TR_KEY_torrent_complete_sound_enabled_kebab },
    { TR_KEY_trash_can_enabled, TR_KEY_trash_can_enabled_kebab },
    { TR_KEY_watch_dir, TR_KEY_watch_dir_kebab },
    { TR_KEY_watch_dir_enabled, TR_KEY_watch_dir_enabled_kebab },
    { TR_KEY_torrent_complete_sound_command, TR_KEY_torrent_complete_sound_command_kebab },
    { TR_KEY_incomplete_dir, TR_KEY_incomplete_dir_kebab },
    { TR_KEY_alt_speed_enabled, TR_KEY_alt_speed_enabled_kebab },
    { TR_KEY_peer_port, TR_KEY_peer_port_kebab },
    { TR_KEY_blocklist_enabled, TR_KEY_blocklist_enabled_kebab },
    { TR_KEY_blocklist_date, TR_KEY_blocklist_date_kebab },
    { TR_KEY_start_added_torrents, TR_KEY_start_added_torrents_kebab },
    { TR_KEY_alt_speed_down, TR_KEY_alt_speed_down_kebab },
    { TR_KEY_alt_speed_up, TR_KEY_alt_speed_up_kebab },
    { TR_KEY_speed_limit_down, TR_KEY_speed_limit_down_kebab },
    { TR_KEY_speed_limit_up, TR_KEY_speed_limit_up_kebab },
    { TR_KEY_ratio_limit, TR_KEY_ratio_limit_kebab },
    { TR_KEY_message_level, TR_KEY_message_level_kebab },
    { TR_KEY_rpc_port, TR_KEY_rpc_port_kebab },
    { TR_KEY_rpc_whitelist, TR_KEY_rpc_whitelist_kebab },
    { TR_KEY_trash_original_torrent_files, TR_KEY_trash_original_torrent_files_kebab },
    { TR_KEY_peer_limit_per_torrent, TR_KEY_peer_limit_per_torrent_kebab },
} };

[[nodiscard]] constexpr tr_quark convert_key(tr_quark const src, Style const style, bool const is_rpc)
{
    if (style == Style::Tr5)
    {
        for (auto const [current, legacy] : RpcKeys)
        {
            if (src == current || src == legacy)
            {
                return current;
            }
        }
        for (auto const [current, legacy] : SessionKeys)
        {
            if (src == current || src == legacy)
            {
                return current;
            }
        }
    }
    else if (is_rpc) // legacy RPC
    {
        for (auto const [current, legacy] : RpcKeys)
        {
            if (src == current || src == legacy)
            {
                return legacy;
            }
        }
    }
    else // legacy datafiles
    {
        for (auto const [current, legacy] : SessionKeys)
        {
            if (src == current || src == legacy)
            {
                return legacy;
            }
        }
    }

    return src;
}

/**
 * Guess the error code from a legacy RPC response message.
 *
 * Use case: a modern Transmission client that parses jsonrpc needs to
 * connect to a legacy Transmission RPC server.
 *
 * We're not going to always get this right: There are some edge cases
 * where legacy Transmission's error messages are unhelpful.
 */
[[nodiscard]] JsonRpc::Error::Code guess_error_code(std::string_view const result_in)
{
    using namespace JsonRpc;

    auto const result = tr_strlower(result_in);

    if (result == "success")
    {
        return Error::SUCCESS;
    }

    static auto constexpr Phrases = std::array<std::pair<std::string_view, Error::Code>, 13U>{ {
        { "absolute", Error::PATH_NOT_ABSOLUTE },
        { "couldn't fetch blocklist", Error::HTTP_ERROR },
        { "couldn't save", Error::SYSTEM_ERROR },
        { "couldn't test port", Error::HTTP_ERROR },
        { "file index out of range", Error::FILE_IDX_OOR },
        { "invalid ip protocol", Error::INVALID_PARAMS },
        { "invalid or corrupt torrent", Error::CORRUPT_TORRENT },
        { "invalid tracker list", Error::INVALID_TRACKER_LIST },
        { "labels cannot", Error::INVALID_PARAMS },
        { "no filename or metainfo specified", Error::INVALID_PARAMS },
        { "no location", Error::INVALID_PARAMS },
        { "torrent-rename-path requires 1 torrent", Error::INVALID_PARAMS },
        { "unrecognized info", Error::UNRECOGNIZED_INFO },
    } };

    for (auto const& [substr, code] : Phrases)
    {
        if (tr_strv_contains(result, substr))
        {
            return code;
        }
    }

    return {};
}

struct CloneState
{
    api_compat::Style style = {};
    bool is_rpc = false;
    bool convert_strings = false;
    bool is_torrent = false;
    bool is_free_space_response = false;
};

[[nodiscard]] tr_variant convert_impl(tr_variant const& self, CloneState& state)
{
    struct Visitor
    {
        tr_variant operator()(std::monostate const& /*unused*/) const
        {
            return tr_variant{};
        }

        tr_variant operator()(std::nullptr_t const& /*unused*/) const
        {
            return tr_variant{ nullptr };
        }

        tr_variant operator()(bool const& val) const
        {
            return tr_variant{ val };
        }

        tr_variant operator()(int64_t const& val) const
        {
            return tr_variant{ val };
        }

        tr_variant operator()(double const& val) const
        {
            return tr_variant{ val };
        }

        tr_variant operator()(std::string_view const& sv) const
        {
            if (state_.convert_strings)
            {
                if (auto lookup = tr_quark_lookup(sv))
                {
                    auto key = convert_key(*lookup, state_.style, state_.is_rpc);

                    // Crazy case: downloadDir in torrent-get, download-dir in session-get
                    if (state_.is_torrent && key == TR_KEY_download_dir_kebab)
                    {
                        key = TR_KEY_download_dir_camel;
                    }

                    return tr_variant::unmanaged_string(key);
                }
            }

            return tr_variant{ sv };
        }

        tr_variant operator()(tr_variant::Vector const& src)
        {
            auto tgt = tr_variant::Vector();
            tgt.reserve(std::size(src));
            for (auto const& val : src)
            {
                tgt.emplace_back(convert_impl(val, state_));
            }
            return tgt;
        }

        tr_variant operator()(tr_variant::Map const& src)
        {
            auto tgt = tr_variant::Map{ std::size(src) };
            tgt.reserve(std::size(src));
            for (auto const& [key, val] : src)
            {
                auto const pop = state_.convert_strings;
                auto new_key = convert_key(key, state_.style, state_.is_rpc);
                auto const special =
                    (state_.is_rpc &&
                     (new_key == TR_KEY_method || new_key == TR_KEY_fields || new_key == TR_KEY_ids ||
                      new_key == TR_KEY_torrents));
                // TODO(ckerr): replace `new_key == TR_KEY_TORRENTS` on previous line with logic to turn on convert
                // if it's an array inside an array val whose key was `torrents`.
                // This is for the edge case of table mode: `torrents : [ [ 'key1', 'key2' ], [ ... ] ]`
                state_.convert_strings |= special;

                // Crazy case: total_size in free-space, totalSize in torrent-get
                if (state_.is_free_space_response && new_key == TR_KEY_total_size_camel)
                {
                    new_key = TR_KEY_total_size;
                }

                tgt.insert_or_assign(new_key, convert_impl(val, state_));
                state_.convert_strings = pop;
            }
            return tgt;
        }

        CloneState& state_;
    };

    return self.visit(Visitor{ state });
}
} // namespace

tr_variant convert(tr_variant const& src, Style const tgt_style)
{
    // TODO: yes I know this method is ugly rn.
    // I've just been trying to get the tests passing.

    auto const* const src_top = src.get_if<tr_variant::Map>();

    // if it's not a Map, just clone it
    if (src_top == nullptr)
    {
        return src.clone();
    }

    auto const is_request = src_top->contains(TR_KEY_method);

    auto const was_jsonrpc = src_top->contains(TR_KEY_jsonrpc);
    auto const was_legacy = !was_jsonrpc;
    auto const was_jsonrpc_response = was_jsonrpc && (src_top->contains(TR_KEY_result) || src_top->contains(TR_KEY_error));
    auto const was_legacy_response = was_legacy && src_top->contains(TR_KEY_result);
    auto const is_response = was_jsonrpc_response || was_legacy_response;
    auto const is_rpc = is_request || is_response;

    auto state = CloneState{};
    state.style = tgt_style;
    state.is_rpc = is_rpc;

    auto const is_success = is_response &&
        (was_jsonrpc_response ? src_top->contains(TR_KEY_result) :
                                src_top->value_if<std::string_view>(TR_KEY_result).value_or("") == "success");

    if (auto const method = src_top->value_if<std::string_view>(TR_KEY_method))
    {
        auto const key = tr_quark_convert(tr_quark_new(*method));
        state.is_torrent = key == TR_KEY_torrent_get || key == TR_KEY_torrent_set;
    }

    if (is_response)
    {
        if (auto const* const args = src_top->find_if<tr_variant::Map>(was_jsonrpc ? TR_KEY_result : TR_KEY_arguments))
        {
            state.is_free_space_response = args->contains(TR_KEY_path) &&
                args->contains(was_jsonrpc ? TR_KEY_size_bytes : TR_KEY_size_bytes_kebab);
        }
    }

    auto ret = convert_impl(src, state);

    auto* const tgt_top = ret.get_if<tr_variant::Map>();

    // jsonrpc <-> legacy rpc conversion
    if (is_rpc)
    {
        auto const is_jsonrpc = tgt_style == Style::Tr5;
        auto const is_legacy = tgt_style != Style::Tr5;

        // - use `jsonrpc` in jsonrpc, but not in legacy
        // - use `id` in jsonrpc; use `tag` in legacy
        if (is_jsonrpc)
        {
            tgt_top->try_emplace(TR_KEY_jsonrpc, tr_variant::unmanaged_string(JsonRpc::Version));
            tgt_top->replace_key(TR_KEY_tag, TR_KEY_id);
        }
        else
        {
            tgt_top->erase(TR_KEY_jsonrpc);
            tgt_top->replace_key(TR_KEY_id, TR_KEY_tag);
        }

        if (is_response && is_legacy && is_success && was_jsonrpc)
        {
            // in legacy messages:
            // - move `result` to `arguments`
            // - add `result: "success"`
            tgt_top->replace_key(TR_KEY_result, TR_KEY_arguments);
            tgt_top->try_emplace(TR_KEY_result, tr_variant::unmanaged_string("success"));
        }

        if (is_response && is_legacy && !is_success)
        {
            // in legacy error responses:
            // - copy `error.data.error_string` to `result`
            // - remove `error` object
            // - add an empty `arguments` object
            if (auto const* error = tgt_top->find_if<tr_variant::Map>(TR_KEY_error))
            {
                if (auto const* data = error->find_if<tr_variant::Map>(TR_KEY_data))
                {
                    if (auto const* errmsg = data->find_if<std::string_view>(TR_KEY_error_string_camel))
                    {
                        tgt_top->try_emplace(TR_KEY_result, *errmsg);
                    }
                }
            }

            tgt_top->erase(TR_KEY_error);
            tgt_top->try_emplace(TR_KEY_arguments, tr_variant::make_map());
        }

        if (is_response && is_jsonrpc && is_success && was_legacy)
        {
            tgt_top->erase(TR_KEY_result);
            tgt_top->replace_key(TR_KEY_arguments, TR_KEY_result);
        }

        if (is_response && is_jsonrpc && !is_success)
        {
            // in jsonrpc error message:
            // - copy `result` to `error.data.error_string`
            // - ensure `error` object exists and is well-formatted
            // - remove `result`
            auto const* errmsg = tgt_top->find_if<std::string_view>(TR_KEY_result);
            std::string_view const errmsg_sv = errmsg != nullptr ? *errmsg : "unknown error";
            auto* error = tgt_top->try_emplace(TR_KEY_error, tr_variant::make_map()).first.get_if<tr_variant::Map>();
            auto* data = error->try_emplace(TR_KEY_data, tr_variant::make_map()).first.get_if<tr_variant::Map>();
            data->try_emplace(TR_KEY_error_string, errmsg_sv);
            auto const code = guess_error_code(errmsg_sv);
            error->try_emplace(TR_KEY_code, code);
            error->try_emplace(TR_KEY_message, JsonRpc::Error::to_string(code));
            tgt_top->erase(TR_KEY_result);
        }

        if (is_response && is_jsonrpc && !is_success && was_legacy)
        {
            tgt_top->erase(TR_KEY_arguments);
        }

        if (is_request && is_jsonrpc)
        {
            tgt_top->replace_key(TR_KEY_arguments, TR_KEY_params);
        }

        if (is_request && is_legacy)
        {
            tgt_top->replace_key(TR_KEY_params, TR_KEY_arguments);
        }
    }

    return ret;
}

[[nodiscard]] Style get_export_settings_style()
{
    // TODO: change default to Tr5 in transmission 5.0.0-beta.1
    static auto const style = tr_env_get_string("TR_SAVE_VERSION_FORMAT", "4") == "5" ? Style::Tr5 : Style::Tr4;
    return style;
}

[[nodiscard]] tr_variant convert_outgoing_data(tr_variant const& src)
{
    return convert(src, get_export_settings_style());
}

[[nodiscard]] tr_variant convert_incoming_data(tr_variant const& src)
{
    return convert(src, Style::Tr5);
}
} // namespace libtransmission::api_compat

tr_quark tr_quark_convert(tr_quark const quark)
{
    using namespace libtransmission::api_compat;
    return convert_key(quark, Style::Tr5, false /*ignored for Style::Tr5*/);
}
