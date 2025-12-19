// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

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
    { TR_KEY_alt_speed_down, TR_KEY_alt_speed_down_kebab_APICOMPAT },
    { TR_KEY_alt_speed_enabled, TR_KEY_alt_speed_enabled_kebab_APICOMPAT },
    { TR_KEY_alt_speed_time_begin, TR_KEY_alt_speed_time_begin_kebab_APICOMPAT },
    { TR_KEY_alt_speed_time_day, TR_KEY_alt_speed_time_day_kebab_APICOMPAT },
    { TR_KEY_alt_speed_time_enabled, TR_KEY_alt_speed_time_enabled_kebab_APICOMPAT },
    { TR_KEY_alt_speed_time_end, TR_KEY_alt_speed_time_end_kebab_APICOMPAT },
    { TR_KEY_alt_speed_up, TR_KEY_alt_speed_up_kebab_APICOMPAT },
    { TR_KEY_announce_state, TR_KEY_announce_state_camel },
    { TR_KEY_anti_brute_force_enabled, TR_KEY_anti_brute_force_enabled_kebab_APICOMPAT },
    { TR_KEY_anti_brute_force_threshold, TR_KEY_anti_brute_force_threshold_kebab_APICOMPAT },
    { TR_KEY_bandwidth_priority, TR_KEY_bandwidth_priority_camel }, // TODO(ckerr) legacy duplicate
    { TR_KEY_blocklist_enabled, TR_KEY_blocklist_enabled_kebab_APICOMPAT },
    { TR_KEY_blocklist_size, TR_KEY_blocklist_size_kebab_APICOMPAT },
    { TR_KEY_blocklist_url, TR_KEY_blocklist_url_kebab_APICOMPAT },
    { TR_KEY_bytes_completed, TR_KEY_bytes_completed_camel },
    { TR_KEY_cache_size_mb, TR_KEY_cache_size_mb_kebab_APICOMPAT },
    { TR_KEY_client_is_choked, TR_KEY_client_is_choked_camel },
    { TR_KEY_client_is_interested, TR_KEY_client_is_interested_camel },
    { TR_KEY_client_name, TR_KEY_client_name_camel },
    { TR_KEY_config_dir, TR_KEY_config_dir_kebab_APICOMPAT },
    { TR_KEY_corrupt_ever, TR_KEY_corrupt_ever_camel },
    { TR_KEY_cumulative_stats, TR_KEY_cumulative_stats_kebab },
    { TR_KEY_current_stats, TR_KEY_current_stats_kebab },
    { TR_KEY_date_created, TR_KEY_date_created_camel },
    { TR_KEY_default_trackers, TR_KEY_default_trackers_kebab_APICOMPAT },
    { TR_KEY_delete_local_data, TR_KEY_delete_local_data_kebab },
    { TR_KEY_desired_available, TR_KEY_desired_available_camel },
    { TR_KEY_dht_enabled, TR_KEY_dht_enabled_kebab_APICOMPAT },
    { TR_KEY_done_date, TR_KEY_done_date_camel }, // TODO(ckerr) legacy duplicate
    { TR_KEY_download_count, TR_KEY_download_count_camel },
    { TR_KEY_download_dir,
      TR_KEY_download_dir_kebab_APICOMPAT }, // crazy case 1: camel in torrent-get/set, kebab everywhere else
    { TR_KEY_download_dir_free_space, TR_KEY_download_dir_free_space_kebab_APICOMPAT },
    { TR_KEY_download_limit, TR_KEY_download_limit_camel },
    { TR_KEY_download_limited, TR_KEY_download_limited_camel },
    { TR_KEY_download_queue_enabled, TR_KEY_download_queue_enabled_kebab_APICOMPAT },
    { TR_KEY_download_queue_size, TR_KEY_download_queue_size_kebab_APICOMPAT },
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
    { TR_KEY_idle_seeding_limit, TR_KEY_idle_seeding_limit_kebab_APICOMPAT },
    { TR_KEY_idle_seeding_limit_enabled, TR_KEY_idle_seeding_limit_enabled_kebab_APICOMPAT },
    { TR_KEY_incomplete_dir, TR_KEY_incomplete_dir_kebab_APICOMPAT },
    { TR_KEY_incomplete_dir_enabled, TR_KEY_incomplete_dir_enabled_kebab_APICOMPAT },
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
    { TR_KEY_lpd_enabled, TR_KEY_lpd_enabled_kebab_APICOMPAT },
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
    { TR_KEY_peer_limit_global, TR_KEY_peer_limit_global_kebab_APICOMPAT },
    { TR_KEY_peer_limit_per_torrent, TR_KEY_peer_limit_per_torrent_kebab_APICOMPAT },
    { TR_KEY_peer_port, TR_KEY_peer_port_kebab_APICOMPAT },
    { TR_KEY_peer_port_random_on_start, TR_KEY_peer_port_random_on_start_kebab_APICOMPAT },
    { TR_KEY_peers_connected, TR_KEY_peers_connected_camel },
    { TR_KEY_peers_from, TR_KEY_peers_from_camel },
    { TR_KEY_peers_getting_from_us, TR_KEY_peers_getting_from_us_camel },
    { TR_KEY_peers_sending_to_us, TR_KEY_peers_sending_to_us_camel },
    { TR_KEY_percent_complete, TR_KEY_percent_complete_camel },
    { TR_KEY_percent_done, TR_KEY_percent_done_camel },
    { TR_KEY_pex_enabled, TR_KEY_pex_enabled_kebab_APICOMPAT },
    { TR_KEY_piece_count, TR_KEY_piece_count_camel },
    { TR_KEY_piece_size, TR_KEY_piece_size_camel },
    { TR_KEY_port_forwarding_enabled, TR_KEY_port_forwarding_enabled_kebab_APICOMPAT },
    { TR_KEY_port_is_open, TR_KEY_port_is_open_kebab },
    { TR_KEY_primary_mime_type, TR_KEY_primary_mime_type_kebab },
    { TR_KEY_priority_high, TR_KEY_priority_high_kebab },
    { TR_KEY_priority_low, TR_KEY_priority_low_kebab },
    { TR_KEY_priority_normal, TR_KEY_priority_normal_kebab },
    { TR_KEY_queue_position, TR_KEY_queue_position_camel },
    { TR_KEY_queue_stalled_enabled, TR_KEY_queue_stalled_enabled_kebab_APICOMPAT },
    { TR_KEY_queue_stalled_minutes, TR_KEY_queue_stalled_minutes_kebab_APICOMPAT },
    { TR_KEY_rate_download, TR_KEY_rate_download_camel },
    { TR_KEY_rate_to_client, TR_KEY_rate_to_client_camel },
    { TR_KEY_rate_to_peer, TR_KEY_rate_to_peer_camel },
    { TR_KEY_rate_upload, TR_KEY_rate_upload_camel },
    { TR_KEY_recently_active, TR_KEY_recently_active_kebab },
    { TR_KEY_recheck_progress, TR_KEY_recheck_progress_camel },
    { TR_KEY_rename_partial_files, TR_KEY_rename_partial_files_kebab_APICOMPAT },
    { TR_KEY_rpc_host_whitelist, TR_KEY_rpc_host_whitelist_kebab },
    { TR_KEY_rpc_host_whitelist_enabled, TR_KEY_rpc_host_whitelist_enabled_kebab },
    { TR_KEY_rpc_version, TR_KEY_rpc_version_kebab_APICOMPAT },
    { TR_KEY_rpc_version_minimum, TR_KEY_rpc_version_minimum_kebab_APICOMPAT },
    { TR_KEY_rpc_version_semver, TR_KEY_rpc_version_semver_kebab_APICOMPAT },
    { TR_KEY_scrape_state, TR_KEY_scrape_state_camel },
    { TR_KEY_script_torrent_added_enabled, TR_KEY_script_torrent_added_enabled_kebab_APICOMPAT },
    { TR_KEY_script_torrent_added_filename, TR_KEY_script_torrent_added_filename_kebab_APICOMPAT },
    { TR_KEY_script_torrent_done_enabled, TR_KEY_script_torrent_done_enabled_kebab_APICOMPAT },
    { TR_KEY_script_torrent_done_filename, TR_KEY_script_torrent_done_filename_kebab_APICOMPAT },
    { TR_KEY_script_torrent_done_seeding_enabled, TR_KEY_script_torrent_done_seeding_enabled_kebab_APICOMPAT },
    { TR_KEY_script_torrent_done_seeding_filename, TR_KEY_script_torrent_done_seeding_filename_kebab_APICOMPAT },
    { TR_KEY_seconds_active, TR_KEY_seconds_active_camel }, // TODO(ckerr) legacy duplicate
    { TR_KEY_seconds_downloading, TR_KEY_seconds_downloading_camel },
    { TR_KEY_seconds_seeding, TR_KEY_seconds_seeding_camel },
    { TR_KEY_seed_idle_limit, TR_KEY_seed_idle_limit_camel },
    { TR_KEY_seed_idle_mode, TR_KEY_seed_idle_mode_camel },
    { TR_KEY_seed_queue_enabled, TR_KEY_seed_queue_enabled_kebab_APICOMPAT },
    { TR_KEY_seed_queue_size, TR_KEY_seed_queue_size_kebab_APICOMPAT },
    { TR_KEY_seed_ratio_limit, TR_KEY_seed_ratio_limit_camel_APICOMPAT },
    { TR_KEY_seed_ratio_limited, TR_KEY_seed_ratio_limited_camel_APICOMPAT },
    { TR_KEY_seed_ratio_mode, TR_KEY_seed_ratio_mode_camel },
    { TR_KEY_seeder_count, TR_KEY_seeder_count_camel },
    { TR_KEY_session_count, TR_KEY_session_count_camel }, // TODO(ckerr) legacy duplicate
    { TR_KEY_session_id, TR_KEY_session_id_kebab_APICOMPAT },
    { TR_KEY_size_bytes, TR_KEY_size_bytes_kebab },
    { TR_KEY_size_units, TR_KEY_size_units_kebab },
    { TR_KEY_size_when_done, TR_KEY_size_when_done_camel },
    { TR_KEY_speed_bytes, TR_KEY_speed_bytes_kebab },
    { TR_KEY_speed_limit_down, TR_KEY_speed_limit_down_kebab_APICOMPAT },
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

auto constexpr SessionKeys = std::array<ApiKey, 157U>{ {
    { TR_KEY_activity_date, TR_KEY_activity_date_kebab_APICOMPAT },
    { TR_KEY_added_date, TR_KEY_added_date_kebab_APICOMPAT },
    { TR_KEY_alt_speed_down, TR_KEY_alt_speed_down_kebab_APICOMPAT },
    { TR_KEY_alt_speed_enabled, TR_KEY_alt_speed_enabled_kebab_APICOMPAT },
    { TR_KEY_alt_speed_time_begin, TR_KEY_alt_speed_time_begin_kebab_APICOMPAT },
    { TR_KEY_alt_speed_time_day, TR_KEY_alt_speed_time_day_kebab_APICOMPAT },
    { TR_KEY_alt_speed_time_enabled, TR_KEY_alt_speed_time_enabled_kebab_APICOMPAT },
    { TR_KEY_alt_speed_time_end, TR_KEY_alt_speed_time_end_kebab_APICOMPAT },
    { TR_KEY_alt_speed_up, TR_KEY_alt_speed_up_kebab_APICOMPAT },
    { TR_KEY_announce_ip, TR_KEY_announce_ip_kebab },
    { TR_KEY_announce_ip_enabled, TR_KEY_announce_ip_enabled_kebab },
    { TR_KEY_anti_brute_force_enabled, TR_KEY_anti_brute_force_enabled_kebab_APICOMPAT },
    { TR_KEY_anti_brute_force_threshold, TR_KEY_anti_brute_force_threshold_kebab_APICOMPAT },
    { TR_KEY_bandwidth_priority, TR_KEY_bandwidth_priority_kebab_APICOMPAT },
    { TR_KEY_bind_address_ipv4, TR_KEY_bind_address_ipv4_kebab },
    { TR_KEY_bind_address_ipv6, TR_KEY_bind_address_ipv6_kebab },
    { TR_KEY_blocklist_date, TR_KEY_blocklist_date_kebab },
    { TR_KEY_blocklist_enabled, TR_KEY_blocklist_enabled_kebab_APICOMPAT },
    { TR_KEY_blocklist_updates_enabled, TR_KEY_blocklist_updates_enabled_kebab },
    { TR_KEY_blocklist_url, TR_KEY_blocklist_url_kebab_APICOMPAT },
    { TR_KEY_cache_size_mb, TR_KEY_cache_size_mb_kebab_APICOMPAT },
    { TR_KEY_compact_view, TR_KEY_compact_view_kebab },
    { TR_KEY_default_trackers, TR_KEY_default_trackers_kebab_APICOMPAT },
    { TR_KEY_details_window_height, TR_KEY_details_window_height_kebab },
    { TR_KEY_details_window_width, TR_KEY_details_window_width_kebab },
    { TR_KEY_dht_enabled, TR_KEY_dht_enabled_kebab_APICOMPAT },
    { TR_KEY_done_date, TR_KEY_done_date_kebab_APICOMPAT },
    { TR_KEY_download_dir, TR_KEY_download_dir_kebab_APICOMPAT },
    { TR_KEY_download_queue_enabled, TR_KEY_download_queue_enabled_kebab_APICOMPAT },
    { TR_KEY_download_queue_size, TR_KEY_download_queue_size_kebab_APICOMPAT },
    { TR_KEY_downloaded_bytes, TR_KEY_downloaded_bytes_kebab_APICOMPAT },
    { TR_KEY_downloading_time_seconds, TR_KEY_downloading_time_seconds_kebab_APICOMPAT },
    { TR_KEY_files_added, TR_KEY_files_added_kebab_APICOMPAT },
    { TR_KEY_filter_mode, TR_KEY_filter_mode_kebab },
    { TR_KEY_filter_text, TR_KEY_filter_text_kebab },
    { TR_KEY_filter_trackers, TR_KEY_filter_trackers_kebab },
    { TR_KEY_idle_limit, TR_KEY_idle_limit_kebab_APICOMPAT },
    { TR_KEY_idle_mode, TR_KEY_idle_mode_kebab_APICOMPAT },
    { TR_KEY_idle_seeding_limit, TR_KEY_idle_seeding_limit_kebab_APICOMPAT },
    { TR_KEY_idle_seeding_limit_enabled, TR_KEY_idle_seeding_limit_enabled_kebab_APICOMPAT },
    { TR_KEY_incomplete_dir, TR_KEY_incomplete_dir_kebab_APICOMPAT },
    { TR_KEY_incomplete_dir_enabled, TR_KEY_incomplete_dir_enabled_kebab_APICOMPAT },
    { TR_KEY_inhibit_desktop_hibernation, TR_KEY_inhibit_desktop_hibernation_kebab },
    { TR_KEY_lpd_enabled, TR_KEY_lpd_enabled_kebab_APICOMPAT },
    { TR_KEY_main_window_height, TR_KEY_main_window_height_kebab },
    { TR_KEY_main_window_is_maximized, TR_KEY_main_window_is_maximized_kebab },
    { TR_KEY_main_window_layout_order, TR_KEY_main_window_layout_order_kebab },
    { TR_KEY_main_window_width, TR_KEY_main_window_width_kebab },
    { TR_KEY_main_window_x, TR_KEY_main_window_x_kebab },
    { TR_KEY_main_window_y, TR_KEY_main_window_y_kebab },
    { TR_KEY_max_peers, TR_KEY_max_peers_kebab_APICOMPAT },
    { TR_KEY_message_level, TR_KEY_message_level_kebab },
    { TR_KEY_open_dialog_dir, TR_KEY_open_dialog_dir_kebab },
    { TR_KEY_peer_congestion_algorithm, TR_KEY_peer_congestion_algorithm_kebab },
    { TR_KEY_peer_limit_global, TR_KEY_peer_limit_global_kebab_APICOMPAT },
    { TR_KEY_peer_limit_per_torrent, TR_KEY_peer_limit_per_torrent_kebab_APICOMPAT },
    { TR_KEY_peer_port, TR_KEY_peer_port_kebab_APICOMPAT },
    { TR_KEY_peer_port_random_high, TR_KEY_peer_port_random_high_kebab },
    { TR_KEY_peer_port_random_low, TR_KEY_peer_port_random_low_kebab },
    { TR_KEY_peer_port_random_on_start, TR_KEY_peer_port_random_on_start_kebab_APICOMPAT },
    { TR_KEY_peer_socket_tos, TR_KEY_peer_socket_tos_kebab },
    { TR_KEY_peers2_6, TR_KEY_peers2_6_kebab_APICOMPAT },
    { TR_KEY_pex_enabled, TR_KEY_pex_enabled_kebab_APICOMPAT },
    { TR_KEY_port_forwarding_enabled, TR_KEY_port_forwarding_enabled_kebab_APICOMPAT },
    { TR_KEY_prompt_before_exit, TR_KEY_prompt_before_exit_kebab },
    { TR_KEY_queue_stalled_enabled, TR_KEY_queue_stalled_enabled_kebab_APICOMPAT },
    { TR_KEY_queue_stalled_minutes, TR_KEY_queue_stalled_minutes_kebab_APICOMPAT },
    { TR_KEY_ratio_limit, TR_KEY_ratio_limit_kebab },
    { TR_KEY_ratio_limit_enabled, TR_KEY_ratio_limit_enabled_kebab },
    { TR_KEY_ratio_mode, TR_KEY_ratio_mode_kebab_APICOMPAT },
    { TR_KEY_read_clipboard, TR_KEY_read_clipboard_kebab },
    { TR_KEY_remote_session_enabled, TR_KEY_remote_session_enabled_kebab },
    { TR_KEY_remote_session_host, TR_KEY_remote_session_host_kebab },
    { TR_KEY_remote_session_https, TR_KEY_remote_session_https_kebab },
    { TR_KEY_remote_session_password, TR_KEY_remote_session_password_kebab },
    { TR_KEY_remote_session_port, TR_KEY_remote_session_port_kebab },
    { TR_KEY_remote_session_requires_authentication, TR_KEY_remote_session_requres_authentication_kebab },
    { TR_KEY_remote_session_username, TR_KEY_remote_session_username_kebab },
    { TR_KEY_rename_partial_files, TR_KEY_rename_partial_files_kebab_APICOMPAT },
    { TR_KEY_rpc_authentication_required, TR_KEY_rpc_authentication_required_kebab },
    { TR_KEY_rpc_bind_address, TR_KEY_rpc_bind_address_kebab },
    { TR_KEY_rpc_enabled, TR_KEY_rpc_enabled_kebab },
    { TR_KEY_rpc_host_whitelist, TR_KEY_rpc_host_whitelist_kebab },
    { TR_KEY_rpc_host_whitelist_enabled, TR_KEY_rpc_host_whitelist_enabled_kebab },
    { TR_KEY_rpc_password, TR_KEY_rpc_password_kebab },
    { TR_KEY_rpc_port, TR_KEY_rpc_port_kebab },
    { TR_KEY_rpc_socket_mode, TR_KEY_rpc_socket_mode_kebab },
    { TR_KEY_rpc_url, TR_KEY_rpc_url_kebab },
    { TR_KEY_rpc_username, TR_KEY_rpc_username_kebab },
    { TR_KEY_rpc_whitelist, TR_KEY_rpc_whitelist_kebab },
    { TR_KEY_rpc_whitelist_enabled, TR_KEY_rpc_whitelist_enabled_kebab },
    { TR_KEY_scrape_paused_torrents_enabled, TR_KEY_scrape_paused_torrents_enabled_kebab },
    { TR_KEY_script_torrent_added_enabled, TR_KEY_script_torrent_added_enabled_kebab_APICOMPAT },
    { TR_KEY_script_torrent_added_filename, TR_KEY_script_torrent_added_filename_kebab_APICOMPAT },
    { TR_KEY_script_torrent_done_enabled, TR_KEY_script_torrent_done_enabled_kebab_APICOMPAT },
    { TR_KEY_script_torrent_done_filename, TR_KEY_script_torrent_done_filename_kebab_APICOMPAT },
    { TR_KEY_script_torrent_done_seeding_enabled, TR_KEY_script_torrent_done_seeding_enabled_kebab_APICOMPAT },
    { TR_KEY_script_torrent_done_seeding_filename, TR_KEY_script_torrent_done_seeding_filename_kebab_APICOMPAT },
    { TR_KEY_seconds_active, TR_KEY_seconds_active_kebab_APICOMPAT },
    { TR_KEY_seed_queue_enabled, TR_KEY_seed_queue_enabled_kebab_APICOMPAT },
    { TR_KEY_seed_queue_size, TR_KEY_seed_queue_size_kebab_APICOMPAT },
    { TR_KEY_seeding_time_seconds, TR_KEY_seeding_time_seconds_kebab_APICOMPAT },
    { TR_KEY_session_count, TR_KEY_session_count_kebab_APICOMPAT },
    { TR_KEY_show_active, TR_KEY_show_active_kebab_APICOMPAT },
    { TR_KEY_show_all, TR_KEY_show_all_kebab_APICOMPAT },
    { TR_KEY_show_backup_trackers, TR_KEY_show_backup_trackers_kebab },
    { TR_KEY_show_downloading, TR_KEY_show_downloading_kebab_APICOMPAT },
    { TR_KEY_show_error, TR_KEY_show_error_kebab_APICOMPAT },
    { TR_KEY_show_extra_peer_details, TR_KEY_show_extra_peer_details_kebab },
    { TR_KEY_show_filterbar, TR_KEY_show_filterbar_kebab },
    { TR_KEY_show_finished, TR_KEY_show_finished_kebab_APICOMPAT },
    { TR_KEY_show_notification_area_icon, TR_KEY_show_notification_area_icon_kebab },
    { TR_KEY_show_options_window, TR_KEY_show_options_window_kebab },
    { TR_KEY_show_paused, TR_KEY_show_paused_kebab_APICOMPAT },
    { TR_KEY_show_seeding, TR_KEY_show_seeding_kebab_APICOMPAT },
    { TR_KEY_show_statusbar, TR_KEY_show_statusbar_kebab },
    { TR_KEY_show_toolbar, TR_KEY_show_toolbar_kebab },
    { TR_KEY_show_tracker_scrapes, TR_KEY_show_tracker_scrapes_kebab },
    { TR_KEY_show_verifying, TR_KEY_show_verifying_kebab_APICOMPAT },
    { TR_KEY_sleep_per_seconds_during_verify, TR_KEY_sleep_per_seconds_during_verify_kebab },
    { TR_KEY_sort_by_activity, TR_KEY_sort_by_activity_kebab_APICOMPAT },
    { TR_KEY_sort_by_age, TR_KEY_sort_by_age_kebab_APICOMPAT },
    { TR_KEY_sort_by_eta, TR_KEY_sort_by_eta_kebab_APICOMPAT },
    { TR_KEY_sort_by_id, TR_KEY_sort_by_id_kebab_APICOMPAT },
    { TR_KEY_sort_by_name, TR_KEY_sort_by_name_kebab_APICOMPAT },
    { TR_KEY_sort_by_progress, TR_KEY_sort_by_progress_kebab_APICOMPAT },
    { TR_KEY_sort_by_queue, TR_KEY_sort_by_queue_kebab_APICOMPAT },
    { TR_KEY_sort_by_ratio, TR_KEY_sort_by_ratio_kebab_APICOMPAT },
    { TR_KEY_sort_by_size, TR_KEY_sort_by_size_kebab_APICOMPAT },
    { TR_KEY_sort_by_state, TR_KEY_sort_by_state_kebab_APICOMPAT },
    { TR_KEY_sort_mode, TR_KEY_sort_mode_kebab },
    { TR_KEY_sort_reversed, TR_KEY_sort_reversed_kebab },
    { TR_KEY_speed_Bps, TR_KEY_speed_Bps_kebab_APICOMPAT },
    { TR_KEY_speed_limit_down, TR_KEY_speed_limit_down_kebab_APICOMPAT },
    { TR_KEY_speed_limit_down_enabled, TR_KEY_speed_limit_down_enabled_kebab },
    { TR_KEY_speed_limit_up, TR_KEY_speed_limit_up_kebab },
    { TR_KEY_speed_limit_up_enabled, TR_KEY_speed_limit_up_enabled_kebab },
    { TR_KEY_start_added_torrents, TR_KEY_start_added_torrents_kebab },
    { TR_KEY_start_minimized, TR_KEY_start_minimized_kebab },
    { TR_KEY_statusbar_stats, TR_KEY_statusbar_stats_kebab },
    { TR_KEY_tcp_enabled, TR_KEY_tcp_enabled_kebab },
    { TR_KEY_time_checked, TR_KEY_time_checked_kebab },
    { TR_KEY_torrent_added_notification_enabled, TR_KEY_torrent_added_notification_enabled_kebab },
    { TR_KEY_torrent_added_verify_mode, TR_KEY_torrent_added_verify_mode_kebab },
    { TR_KEY_torrent_complete_notification_enabled, TR_KEY_torrent_complete_notification_enabled_kebab },
    { TR_KEY_torrent_complete_sound_command, TR_KEY_torrent_complete_sound_command_kebab },
    { TR_KEY_torrent_complete_sound_enabled, TR_KEY_torrent_complete_sound_enabled_kebab },
    { TR_KEY_trash_can_enabled, TR_KEY_trash_can_enabled_kebab },
    { TR_KEY_trash_original_torrent_files, TR_KEY_trash_original_torrent_files_kebab },
    { TR_KEY_upload_slots_per_torrent, TR_KEY_upload_slots_per_torrent_kebab },
    { TR_KEY_uploaded_bytes, TR_KEY_uploaded_bytes_kebab_APICOMPAT },
    { TR_KEY_use_global_speed_limit, TR_KEY_use_global_speed_limit_kebab_APICOMPAT },
    { TR_KEY_use_speed_limit, TR_KEY_use_speed_limit_kebab_APICOMPAT },
    { TR_KEY_utp_enabled, TR_KEY_utp_enabled_kebab },
    { TR_KEY_watch_dir, TR_KEY_watch_dir_kebab },
    { TR_KEY_watch_dir_enabled, TR_KEY_watch_dir_enabled_kebab },
    { TR_KEY_watch_dir_force_generic, TR_KEY_watch_dir_force_generic_kebab },
} };

auto constexpr MethodNotFoundLegacyErrmsg = std::string_view{ "no method name" };

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

    static auto constexpr Phrases = std::array<std::pair<std::string_view, Error::Code>, 14U>{ {
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
        { MethodNotFoundLegacyErrmsg, Error::METHOD_NOT_FOUND },
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

struct State
{
    api_compat::Style style = {};
    bool convert_strings = false;
    bool is_free_space_response = false;
    bool is_request = false;
    bool is_response = false;
    bool is_rpc = false;
    bool is_success = false;
    bool is_torrent = false;
    bool was_jsonrpc = false;
    bool was_legacy = false;
};

[[nodiscard]] State makeState(tr_variant::Map const& top)
{
    auto state = State{};

    state.is_request = top.contains(TR_KEY_method);
    state.was_jsonrpc = top.contains(TR_KEY_jsonrpc);
    state.was_legacy = !state.was_jsonrpc;
    auto const was_jsonrpc_response = state.was_jsonrpc && (top.contains(TR_KEY_result) || top.contains(TR_KEY_error));
    auto const was_legacy_response = state.was_legacy && top.contains(TR_KEY_result);
    state.is_response = was_jsonrpc_response || was_legacy_response;
    state.is_rpc = state.is_request || state.is_response;

    state.is_success = state.is_response &&
        (was_jsonrpc_response ? top.contains(TR_KEY_result) :
                                top.value_if<std::string_view>(TR_KEY_result).value_or("") == "success");

    if (auto const method = top.value_if<std::string_view>(TR_KEY_method))
    {
        auto const key = tr_quark_convert(tr_quark_new(*method));
        state.is_torrent = key == TR_KEY_torrent_get || key == TR_KEY_torrent_set;
    }

    if (state.is_response)
    {
        if (auto const* const args = top.find_if<tr_variant::Map>(state.was_jsonrpc ? TR_KEY_result : TR_KEY_arguments))
        {
            state.is_free_space_response = args->contains(TR_KEY_path) &&
                args->contains(state.was_jsonrpc ? TR_KEY_size_bytes : TR_KEY_size_bytes_kebab);
        }
    }

    return state;
}

[[nodiscard]] tr_quark constexpr convert_key(State const& state, tr_quark const src)
{
    // special cases here

    // Crazy case:
    // download-dir in Tr4 session-get
    // downloadDir in Tr4 torrent-get
    // download_dir in Tr5
    if (state.is_rpc &&
        (src == TR_KEY_download_dir_camel || src == TR_KEY_download_dir_kebab_APICOMPAT || src == TR_KEY_download_dir))
    {
        if (state.style == Style::Tr5)
        {
            return TR_KEY_download_dir;
        }

        if (state.is_torrent)
        {
            return TR_KEY_download_dir_camel;
        }

        return TR_KEY_download_dir_kebab_APICOMPAT;
    }

    // Crazy case:
    // totalSize in Tr4 torrent-get
    // total_size in Tr4 free-space
    // total_size in Tr5
    if (state.is_rpc && state.is_free_space_response && (src == TR_KEY_total_size || src == TR_KEY_total_size_camel))
    {
        return state.style == Style::Tr5 || state.is_free_space_response ? TR_KEY_total_size : TR_KEY_total_size_camel;
    }

    // Crazy cases done.
    // Now for the lookup tables

    if (state.style == Style::Tr5)
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
    else if (state.is_rpc) // legacy RPC
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

[[nodiscard]] std::optional<std::string_view> convert_string(State const& state, std::string_view const src)
{
    if (!state.convert_strings)
    {
        return {};
    }

    auto const old_key = tr_quark_lookup(src);
    if (!old_key)
    {
        return {};
    }

    auto const new_key = convert_key(state, *old_key);
    if (*old_key == new_key)
    {
        return {};
    }

    auto ret = tr_quark_get_string_view(new_key);
    return ret;
}

[[nodiscard]] bool should_convert_child_strings(State const& state, tr_quark const old_key, tr_quark const new_key)
{
    // TODO(ckerr): replace `new_key == TR_KEY_TORRENTS` here to turn on convert
    // if it's an array inside an array val whose key was `torrents`.
    // This is for the edge case of table mode: `torrents : [ [ 'key1', 'key2' ], [ ... ] ]`
    if (state.is_rpc &&
        (new_key == TR_KEY_method || new_key == TR_KEY_fields || new_key == TR_KEY_ids || new_key == TR_KEY_torrents))
    {
        return true;
    }

    if (!state.is_rpc &&
        (TR_KEY_filter_mode == new_key || TR_KEY_filter_mode == old_key || TR_KEY_filter_mode_kebab == new_key ||
         TR_KEY_filter_mode_kebab == old_key || TR_KEY_sort_mode == new_key || TR_KEY_sort_mode == old_key ||
         TR_KEY_sort_mode_kebab == new_key || TR_KEY_sort_mode_kebab == old_key))
    {
        return true;
    }

    return false;
}

void convert_keys(tr_variant& var, State& state)
{
    var.visit(
        [&state](auto& val)
        {
            using ValueType = std::decay_t<decltype(val)>;

            if constexpr (std::is_same_v<ValueType, std::string> || std::is_same_v<ValueType, std::string_view>)
            {
                if (auto const new_val = convert_string(state, val))
                {
                    val = *new_val;
                }
            }
            else if constexpr (std::is_same_v<ValueType, tr_variant::Vector>)
            {
                for (auto& child : val)
                {
                    convert_keys(child, state);
                }
            }
            else if constexpr (std::is_same_v<ValueType, tr_variant::Map>)
            {
                for (auto& [old_key, child] : val)
                {
                    auto const new_key = convert_key(state, old_key);

                    // maybe change the key.
                    // IMPORTANT: this is safe even inside a range loop of `val`:
                    // tr_variant.replace_key() does not invalidate iterators
                    if (old_key != new_key)
                    {
                        val.replace_key(old_key, new_key);
                    }

                    auto const pop = state.convert_strings;
                    state.convert_strings |= should_convert_child_strings(state, old_key, new_key);
                    convert_keys(child, state);
                    state.convert_strings = pop;
                }
            }
        });
}

// jsonrpc <-> legacy rpc conversion
void convert_jsonrpc(tr_variant::Map& top, State const& state)
{
    if (!state.is_rpc)
    {
        return;
    }

    auto const is_jsonrpc = state.style == Style::Tr5;
    auto const is_legacy = state.style != Style::Tr5;

    // - use `jsonrpc` in jsonrpc, but not in legacy
    // - use `id` in jsonrpc; use `tag` in legacy
    if (is_jsonrpc)
    {
        top.try_emplace(TR_KEY_jsonrpc, tr_variant::unmanaged_string(JsonRpc::Version));
        if (auto const tag = top.value_if<int64_t>(TR_KEY_tag); state.was_legacy && !tag)
        {
            top.erase(TR_KEY_tag);
        }
        top.replace_key(TR_KEY_tag, TR_KEY_id);
    }
    else
    {
        top.erase(TR_KEY_jsonrpc);
        top.replace_key(TR_KEY_id, TR_KEY_tag);
    }

    if (state.is_response && is_legacy && state.is_success && state.was_jsonrpc)
    {
        // in legacy messages:
        // - move `result` to `arguments`
        // - add `result: "success"`
        top.replace_key(TR_KEY_result, TR_KEY_arguments);
        top.try_emplace(TR_KEY_result, tr_variant::unmanaged_string("success"));
    }

    if (state.is_response && is_legacy && !state.is_success)
    {
        // in legacy error responses:
        // - copy `error.data.error_string` to `result`
        // - remove `error` object
        // - add an empty `arguments` object
        if (auto* error_ptr = top.find_if<tr_variant::Map>(TR_KEY_error))
        {
            // move the `error` object before memory reallocations invalidate the pointer
            auto error = std::move(*error_ptr);
            top.erase(TR_KEY_error);

            // crazy case: current and legacy METHOD_NOT_FOUND has different error messages
            if (auto const code = error.value_if<int64_t>(TR_KEY_code); code && *code == JsonRpc::Error::METHOD_NOT_FOUND)
            {
                top.try_emplace(TR_KEY_result, tr_variant::unmanaged_string(MethodNotFoundLegacyErrmsg));
            }

            if (auto* data = error.find_if<tr_variant::Map>(TR_KEY_data))
            {
                if (auto const errmsg = data->value_if<std::string_view>(TR_KEY_error_string_camel))
                {
                    top.try_emplace(TR_KEY_result, *errmsg);
                }

                if (auto const result = data->find(TR_KEY_result); result != std::end(*data))
                {
                    top.try_emplace(TR_KEY_arguments, std::move(result->second));
                }
            }

            if (auto const errmsg = error.value_if<std::string_view>(TR_KEY_message))
            {
                top.try_emplace(TR_KEY_result, *errmsg);
            }
        }

        top.try_emplace(TR_KEY_arguments, tr_variant::make_map());
    }

    if (state.is_response && is_jsonrpc && state.is_success && state.was_legacy)
    {
        top.erase(TR_KEY_result);
        top.replace_key(TR_KEY_arguments, TR_KEY_result);
    }

    if (state.is_response && is_jsonrpc && !state.is_success && state.was_legacy)
    {
        // in jsonrpc error message:
        // - copy `result` to `error.data.error_string`
        // - ensure `error` object exists and is well-formatted
        // - remove `result`
        auto const errstr = top.value_if<std::string_view>(TR_KEY_result).value_or("unknown error");
        auto error = tr_variant::Map{ 3U };
        auto data = tr_variant::Map{ 2U };
        auto const code = guess_error_code(errstr);
        auto const errmsg = JsonRpc::Error::to_string(code);
        error.try_emplace(TR_KEY_code, code);
        error.try_emplace(TR_KEY_message, errmsg);
        // crazy case: current and legacy METHOD_NOT_FOUND has different error messages
        if (errstr != errmsg && errstr != MethodNotFoundLegacyErrmsg)
        {
            data.try_emplace(TR_KEY_error_string, errstr);
        }
        top.erase(TR_KEY_result);

        if (auto const args_it = top.find(TR_KEY_arguments); args_it != std::end(top))
        {
            auto args = std::move(args_it->second);
            top.erase(TR_KEY_arguments);

            if (auto const* args_map = args.get_if<tr_variant::Map>(); args_map != nullptr && !std::empty(*args_map))
            {
                data.try_emplace(TR_KEY_result, std::move(args));
            }
        }

        if (!std::empty(data))
        {
            error.try_emplace(TR_KEY_data, std::move(data));
        }
        top.try_emplace(TR_KEY_error, std::move(error));
    }

    if (state.is_request && is_jsonrpc)
    {
        top.replace_key(TR_KEY_arguments, TR_KEY_params);
    }

    if (state.is_request && is_legacy)
    {
        top.replace_key(TR_KEY_params, TR_KEY_arguments);
    }
}
} // namespace

void convert(tr_variant& var, Style const tgt_style)
{
    if (auto* const top = var.get_if<tr_variant::Map>())
    {
        auto state = makeState(*top);
        state.style = tgt_style;
        convert_keys(var, state);
        convert_jsonrpc(*top, state);
    }
}

[[nodiscard]] Style get_export_settings_style()
{
    // TODO: change default to Tr5 in transmission 5.0.0-beta.1
    static auto const style = tr_env_get_string("TR_SAVE_VERSION_FORMAT", "4") == "5" ? Style::Tr5 : Style::Tr4;
    return style;
}

void convert_outgoing_data(tr_variant& var)
{
    convert(var, get_export_settings_style());
}

void convert_incoming_data(tr_variant& var)
{
    convert(var, Style::Tr5);
}
} // namespace libtransmission::api_compat

tr_quark tr_quark_convert(tr_quark const quark)
{
    using namespace libtransmission::api_compat;

    auto state = State{};
    state.style = Style::Tr5;
    return convert_key(state, quark);
}
