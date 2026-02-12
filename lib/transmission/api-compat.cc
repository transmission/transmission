// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <cstddef>
#include <deque>
#include <initializer_list>
#include <ranges>
#include <string_view>
#include <vector>

#include "lib/base/env.h"
#include "lib/base/quark.h"
#include "lib/base/serializer.h"
#include "lib/base/string-utils.h"
#include "lib/base/variant.h"

#include "lib/transmission/api-compat.h"
#include "lib/transmission/rpcimpl.h"
#include "lib/transmission/types.h"

namespace tr::api_compat
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
    { .current = TR_KEY_active_torrent_count, .legacy = TR_KEY_active_torrent_count_camel_APICOMPAT },
    { .current = TR_KEY_activity_date, .legacy = TR_KEY_activity_date_camel_APICOMPAT },
    { .current = TR_KEY_added_date, .legacy = TR_KEY_added_date_camel_APICOMPAT },
    { .current = TR_KEY_alt_speed_down, .legacy = TR_KEY_alt_speed_down_kebab_APICOMPAT },
    { .current = TR_KEY_alt_speed_enabled, .legacy = TR_KEY_alt_speed_enabled_kebab_APICOMPAT },
    { .current = TR_KEY_alt_speed_time_begin, .legacy = TR_KEY_alt_speed_time_begin_kebab_APICOMPAT },
    { .current = TR_KEY_alt_speed_time_day, .legacy = TR_KEY_alt_speed_time_day_kebab_APICOMPAT },
    { .current = TR_KEY_alt_speed_time_enabled, .legacy = TR_KEY_alt_speed_time_enabled_kebab_APICOMPAT },
    { .current = TR_KEY_alt_speed_time_end, .legacy = TR_KEY_alt_speed_time_end_kebab_APICOMPAT },
    { .current = TR_KEY_alt_speed_up, .legacy = TR_KEY_alt_speed_up_kebab_APICOMPAT },
    { .current = TR_KEY_announce_state, .legacy = TR_KEY_announce_state_camel_APICOMPAT },
    { .current = TR_KEY_anti_brute_force_enabled, .legacy = TR_KEY_anti_brute_force_enabled_kebab_APICOMPAT },
    { .current = TR_KEY_anti_brute_force_threshold, .legacy = TR_KEY_anti_brute_force_threshold_kebab_APICOMPAT },
    { .current = TR_KEY_bandwidth_priority, .legacy = TR_KEY_bandwidth_priority_camel_APICOMPAT },
    { .current = TR_KEY_blocklist_enabled, .legacy = TR_KEY_blocklist_enabled_kebab_APICOMPAT },
    { .current = TR_KEY_blocklist_size, .legacy = TR_KEY_blocklist_size_kebab_APICOMPAT },
    { .current = TR_KEY_blocklist_url, .legacy = TR_KEY_blocklist_url_kebab_APICOMPAT },
    { .current = TR_KEY_bytes_completed, .legacy = TR_KEY_bytes_completed_camel_APICOMPAT },
    { .current = TR_KEY_cache_size_mib, .legacy = TR_KEY_cache_size_mb_kebab_APICOMPAT },
    { .current = TR_KEY_client_is_choked, .legacy = TR_KEY_client_is_choked_camel_APICOMPAT },
    { .current = TR_KEY_client_is_interested, .legacy = TR_KEY_client_is_interested_camel_APICOMPAT },
    { .current = TR_KEY_client_name, .legacy = TR_KEY_client_name_camel_APICOMPAT },
    { .current = TR_KEY_config_dir, .legacy = TR_KEY_config_dir_kebab_APICOMPAT },
    { .current = TR_KEY_corrupt_ever, .legacy = TR_KEY_corrupt_ever_camel_APICOMPAT },
    { .current = TR_KEY_cumulative_stats, .legacy = TR_KEY_cumulative_stats_kebab_APICOMPAT },
    { .current = TR_KEY_current_stats, .legacy = TR_KEY_current_stats_kebab_APICOMPAT },
    { .current = TR_KEY_date_created, .legacy = TR_KEY_date_created_camel_APICOMPAT },
    { .current = TR_KEY_default_trackers, .legacy = TR_KEY_default_trackers_kebab_APICOMPAT },
    { .current = TR_KEY_delete_local_data, .legacy = TR_KEY_delete_local_data_kebab_APICOMPAT },
    { .current = TR_KEY_desired_available, .legacy = TR_KEY_desired_available_camel_APICOMPAT },
    { .current = TR_KEY_dht_enabled, .legacy = TR_KEY_dht_enabled_kebab_APICOMPAT },
    { .current = TR_KEY_done_date, .legacy = TR_KEY_done_date_camel_APICOMPAT },
    { .current = TR_KEY_download_count, .legacy = TR_KEY_download_count_camel_APICOMPAT },
    { .current = TR_KEY_download_dir,
      .legacy = TR_KEY_download_dir_kebab_APICOMPAT }, // crazy case 1: camel in torrent-get/set, kebab everywhere else
    { .current = TR_KEY_download_dir_free_space, .legacy = TR_KEY_download_dir_free_space_kebab_APICOMPAT },
    { .current = TR_KEY_download_limit, .legacy = TR_KEY_download_limit_camel_APICOMPAT },
    { .current = TR_KEY_download_limited, .legacy = TR_KEY_download_limited_camel_APICOMPAT },
    { .current = TR_KEY_download_queue_enabled, .legacy = TR_KEY_download_queue_enabled_kebab_APICOMPAT },
    { .current = TR_KEY_download_queue_size, .legacy = TR_KEY_download_queue_size_kebab_APICOMPAT },
    { .current = TR_KEY_download_speed, .legacy = TR_KEY_download_speed_camel_APICOMPAT },
    { .current = TR_KEY_downloaded_bytes, .legacy = TR_KEY_downloaded_bytes_camel_APICOMPAT },
    { .current = TR_KEY_downloaded_ever, .legacy = TR_KEY_downloaded_ever_camel_APICOMPAT },
    { .current = TR_KEY_edit_date, .legacy = TR_KEY_edit_date_camel_APICOMPAT },
    { .current = TR_KEY_error_string, .legacy = TR_KEY_error_string_camel_APICOMPAT },
    { .current = TR_KEY_eta_idle, .legacy = TR_KEY_eta_idle_camel_APICOMPAT },
    { .current = TR_KEY_file_count, .legacy = TR_KEY_file_count_kebab_APICOMPAT },
    { .current = TR_KEY_file_stats, .legacy = TR_KEY_file_stats_camel_APICOMPAT },
    { .current = TR_KEY_files_added, .legacy = TR_KEY_files_added_camel_APICOMPAT },
    { .current = TR_KEY_files_unwanted, .legacy = TR_KEY_files_unwanted_kebab_APICOMPAT },
    { .current = TR_KEY_files_wanted, .legacy = TR_KEY_files_wanted_kebab_APICOMPAT },
    { .current = TR_KEY_flag_str, .legacy = TR_KEY_flag_str_camel_APICOMPAT },
    { .current = TR_KEY_from_cache, .legacy = TR_KEY_from_cache_camel_APICOMPAT },
    { .current = TR_KEY_from_dht, .legacy = TR_KEY_from_dht_camel_APICOMPAT },
    { .current = TR_KEY_from_incoming, .legacy = TR_KEY_from_incoming_camel_APICOMPAT },
    { .current = TR_KEY_from_lpd, .legacy = TR_KEY_from_lpd_camel_APICOMPAT },
    { .current = TR_KEY_from_ltep, .legacy = TR_KEY_from_ltep_camel_APICOMPAT },
    { .current = TR_KEY_from_pex, .legacy = TR_KEY_from_pex_camel_APICOMPAT },
    { .current = TR_KEY_from_tracker, .legacy = TR_KEY_from_tracker_camel_APICOMPAT },
    { .current = TR_KEY_has_announced, .legacy = TR_KEY_has_announced_camel_APICOMPAT },
    { .current = TR_KEY_has_scraped, .legacy = TR_KEY_has_scraped_camel_APICOMPAT },
    { .current = TR_KEY_hash_string, .legacy = TR_KEY_hash_string_camel_APICOMPAT },
    { .current = TR_KEY_have_unchecked, .legacy = TR_KEY_have_unchecked_camel_APICOMPAT },
    { .current = TR_KEY_have_valid, .legacy = TR_KEY_have_valid_camel_APICOMPAT },
    { .current = TR_KEY_honors_session_limits, .legacy = TR_KEY_honors_session_limits_camel_APICOMPAT },
    { .current = TR_KEY_idle_seeding_limit, .legacy = TR_KEY_idle_seeding_limit_kebab_APICOMPAT },
    { .current = TR_KEY_idle_seeding_limit_enabled, .legacy = TR_KEY_idle_seeding_limit_enabled_kebab_APICOMPAT },
    { .current = TR_KEY_incomplete_dir, .legacy = TR_KEY_incomplete_dir_kebab_APICOMPAT },
    { .current = TR_KEY_incomplete_dir_enabled, .legacy = TR_KEY_incomplete_dir_enabled_kebab_APICOMPAT },
    { .current = TR_KEY_is_backup, .legacy = TR_KEY_is_backup_camel_APICOMPAT },
    { .current = TR_KEY_is_downloading_from, .legacy = TR_KEY_is_downloading_from_camel_APICOMPAT },
    { .current = TR_KEY_is_encrypted, .legacy = TR_KEY_is_encrypted_camel_APICOMPAT },
    { .current = TR_KEY_is_finished, .legacy = TR_KEY_is_finished_camel_APICOMPAT },
    { .current = TR_KEY_is_incoming, .legacy = TR_KEY_is_incoming_camel_APICOMPAT },
    { .current = TR_KEY_is_private, .legacy = TR_KEY_is_private_camel_APICOMPAT },
    { .current = TR_KEY_is_stalled, .legacy = TR_KEY_is_stalled_camel_APICOMPAT },
    { .current = TR_KEY_is_uploading_to, .legacy = TR_KEY_is_uploading_to_camel_APICOMPAT },
    { .current = TR_KEY_is_utp, .legacy = TR_KEY_is_utp_camel_APICOMPAT },
    { .current = TR_KEY_last_announce_peer_count, .legacy = TR_KEY_last_announce_peer_count_camel_APICOMPAT },
    { .current = TR_KEY_last_announce_result, .legacy = TR_KEY_last_announce_result_camel_APICOMPAT },
    { .current = TR_KEY_last_announce_start_time, .legacy = TR_KEY_last_announce_start_time_camel_APICOMPAT },
    { .current = TR_KEY_last_announce_succeeded, .legacy = TR_KEY_last_announce_succeeded_camel_APICOMPAT },
    { .current = TR_KEY_last_announce_time, .legacy = TR_KEY_last_announce_time_camel_APICOMPAT },
    { .current = TR_KEY_last_announce_timed_out, .legacy = TR_KEY_last_announce_timed_out_camel_APICOMPAT },
    { .current = TR_KEY_last_scrape_result, .legacy = TR_KEY_last_scrape_result_camel_APICOMPAT },
    { .current = TR_KEY_last_scrape_start_time, .legacy = TR_KEY_last_scrape_start_time_camel_APICOMPAT },
    { .current = TR_KEY_last_scrape_succeeded, .legacy = TR_KEY_last_scrape_succeeded_camel_APICOMPAT },
    { .current = TR_KEY_last_scrape_time, .legacy = TR_KEY_last_scrape_time_camel_APICOMPAT },
    { .current = TR_KEY_last_scrape_timed_out, .legacy = TR_KEY_last_scrape_timed_out_camel_APICOMPAT },
    { .current = TR_KEY_leecher_count, .legacy = TR_KEY_leecher_count_camel_APICOMPAT },
    { .current = TR_KEY_left_until_done, .legacy = TR_KEY_left_until_done_camel_APICOMPAT },
    { .current = TR_KEY_lpd_enabled, .legacy = TR_KEY_lpd_enabled_kebab_APICOMPAT },
    { .current = TR_KEY_magnet_link, .legacy = TR_KEY_magnet_link_camel_APICOMPAT },
    { .current = TR_KEY_manual_announce_time, .legacy = TR_KEY_manual_announce_time_camel_APICOMPAT },
    { .current = TR_KEY_max_connected_peers, .legacy = TR_KEY_max_connected_peers_camel_APICOMPAT },
    { .current = TR_KEY_memory_bytes, .legacy = TR_KEY_memory_bytes_kebab_APICOMPAT },
    { .current = TR_KEY_memory_units, .legacy = TR_KEY_memory_units_kebab_APICOMPAT },
    { .current = TR_KEY_metadata_percent_complete, .legacy = TR_KEY_metadata_percent_complete_camel_APICOMPAT },
    { .current = TR_KEY_next_announce_time, .legacy = TR_KEY_next_announce_time_camel_APICOMPAT },
    { .current = TR_KEY_next_scrape_time, .legacy = TR_KEY_next_scrape_time_camel_APICOMPAT },
    { .current = TR_KEY_paused_torrent_count, .legacy = TR_KEY_paused_torrent_count_camel_APICOMPAT },
    { .current = TR_KEY_peer_is_choked, .legacy = TR_KEY_peer_is_choked_camel_APICOMPAT },
    { .current = TR_KEY_peer_is_interested, .legacy = TR_KEY_peer_is_interested_camel_APICOMPAT },
    { .current = TR_KEY_peer_limit, .legacy = TR_KEY_peer_limit_kebab_APICOMPAT },
    { .current = TR_KEY_peer_limit_global, .legacy = TR_KEY_peer_limit_global_kebab_APICOMPAT },
    { .current = TR_KEY_peer_limit_per_torrent, .legacy = TR_KEY_peer_limit_per_torrent_kebab_APICOMPAT },
    { .current = TR_KEY_peer_port, .legacy = TR_KEY_peer_port_kebab_APICOMPAT },
    { .current = TR_KEY_peer_port_random_on_start, .legacy = TR_KEY_peer_port_random_on_start_kebab_APICOMPAT },
    { .current = TR_KEY_peers_connected, .legacy = TR_KEY_peers_connected_camel_APICOMPAT },
    { .current = TR_KEY_peers_from, .legacy = TR_KEY_peers_from_camel_APICOMPAT },
    { .current = TR_KEY_peers_getting_from_us, .legacy = TR_KEY_peers_getting_from_us_camel_APICOMPAT },
    { .current = TR_KEY_peers_sending_to_us, .legacy = TR_KEY_peers_sending_to_us_camel_APICOMPAT },
    { .current = TR_KEY_percent_complete, .legacy = TR_KEY_percent_complete_camel_APICOMPAT },
    { .current = TR_KEY_percent_done, .legacy = TR_KEY_percent_done_camel_APICOMPAT },
    { .current = TR_KEY_pex_enabled, .legacy = TR_KEY_pex_enabled_kebab_APICOMPAT },
    { .current = TR_KEY_piece_count, .legacy = TR_KEY_piece_count_camel_APICOMPAT },
    { .current = TR_KEY_piece_size, .legacy = TR_KEY_piece_size_camel_APICOMPAT },
    { .current = TR_KEY_port_forwarding_enabled, .legacy = TR_KEY_port_forwarding_enabled_kebab_APICOMPAT },
    { .current = TR_KEY_port_is_open, .legacy = TR_KEY_port_is_open_kebab_APICOMPAT },
    { .current = TR_KEY_primary_mime_type, .legacy = TR_KEY_primary_mime_type_kebab_APICOMPAT },
    { .current = TR_KEY_priority_high, .legacy = TR_KEY_priority_high_kebab_APICOMPAT },
    { .current = TR_KEY_priority_low, .legacy = TR_KEY_priority_low_kebab_APICOMPAT },
    { .current = TR_KEY_priority_normal, .legacy = TR_KEY_priority_normal_kebab_APICOMPAT },
    { .current = TR_KEY_queue_position, .legacy = TR_KEY_queue_position_camel_APICOMPAT },
    { .current = TR_KEY_queue_stalled_enabled, .legacy = TR_KEY_queue_stalled_enabled_kebab_APICOMPAT },
    { .current = TR_KEY_queue_stalled_minutes, .legacy = TR_KEY_queue_stalled_minutes_kebab_APICOMPAT },
    { .current = TR_KEY_rate_download, .legacy = TR_KEY_rate_download_camel_APICOMPAT },
    { .current = TR_KEY_rate_to_client, .legacy = TR_KEY_rate_to_client_camel_APICOMPAT },
    { .current = TR_KEY_rate_to_peer, .legacy = TR_KEY_rate_to_peer_camel_APICOMPAT },
    { .current = TR_KEY_rate_upload, .legacy = TR_KEY_rate_upload_camel_APICOMPAT },
    { .current = TR_KEY_recently_active, .legacy = TR_KEY_recently_active_kebab_APICOMPAT },
    { .current = TR_KEY_recheck_progress, .legacy = TR_KEY_recheck_progress_camel_APICOMPAT },
    { .current = TR_KEY_rename_partial_files, .legacy = TR_KEY_rename_partial_files_kebab_APICOMPAT },
    { .current = TR_KEY_rpc_host_whitelist, .legacy = TR_KEY_rpc_host_whitelist_kebab_APICOMPAT },
    { .current = TR_KEY_rpc_host_whitelist_enabled, .legacy = TR_KEY_rpc_host_whitelist_enabled_kebab_APICOMPAT },
    { .current = TR_KEY_rpc_version, .legacy = TR_KEY_rpc_version_kebab_APICOMPAT },
    { .current = TR_KEY_rpc_version_minimum, .legacy = TR_KEY_rpc_version_minimum_kebab_APICOMPAT },
    { .current = TR_KEY_rpc_version_semver, .legacy = TR_KEY_rpc_version_semver_kebab_APICOMPAT },
    { .current = TR_KEY_scrape_state, .legacy = TR_KEY_scrape_state_camel_APICOMPAT },
    { .current = TR_KEY_script_torrent_added_enabled, .legacy = TR_KEY_script_torrent_added_enabled_kebab_APICOMPAT },
    { .current = TR_KEY_script_torrent_added_filename, .legacy = TR_KEY_script_torrent_added_filename_kebab_APICOMPAT },
    { .current = TR_KEY_script_torrent_done_enabled, .legacy = TR_KEY_script_torrent_done_enabled_kebab_APICOMPAT },
    { .current = TR_KEY_script_torrent_done_filename, .legacy = TR_KEY_script_torrent_done_filename_kebab_APICOMPAT },
    { .current = TR_KEY_script_torrent_done_seeding_enabled,
      .legacy = TR_KEY_script_torrent_done_seeding_enabled_kebab_APICOMPAT },
    { .current = TR_KEY_script_torrent_done_seeding_filename,
      .legacy = TR_KEY_script_torrent_done_seeding_filename_kebab_APICOMPAT },
    { .current = TR_KEY_seconds_active, .legacy = TR_KEY_seconds_active_camel_APICOMPAT },
    { .current = TR_KEY_seconds_downloading, .legacy = TR_KEY_seconds_downloading_camel_APICOMPAT },
    { .current = TR_KEY_seconds_seeding, .legacy = TR_KEY_seconds_seeding_camel_APICOMPAT },
    { .current = TR_KEY_seed_idle_limit, .legacy = TR_KEY_seed_idle_limit_camel_APICOMPAT },
    { .current = TR_KEY_seed_idle_mode, .legacy = TR_KEY_seed_idle_mode_camel_APICOMPAT },
    { .current = TR_KEY_seed_queue_enabled, .legacy = TR_KEY_seed_queue_enabled_kebab_APICOMPAT },
    { .current = TR_KEY_seed_queue_size, .legacy = TR_KEY_seed_queue_size_kebab_APICOMPAT },
    { .current = TR_KEY_seed_ratio_limit, .legacy = TR_KEY_seed_ratio_limit_camel_APICOMPAT },
    { .current = TR_KEY_seed_ratio_limited, .legacy = TR_KEY_seed_ratio_limited_camel_APICOMPAT },
    { .current = TR_KEY_seed_ratio_mode, .legacy = TR_KEY_seed_ratio_mode_camel_APICOMPAT },
    { .current = TR_KEY_seeder_count, .legacy = TR_KEY_seeder_count_camel_APICOMPAT },
    { .current = TR_KEY_session_count, .legacy = TR_KEY_session_count_camel_APICOMPAT },
    { .current = TR_KEY_session_id, .legacy = TR_KEY_session_id_kebab_APICOMPAT },
    { .current = TR_KEY_size_bytes, .legacy = TR_KEY_size_bytes_kebab_APICOMPAT },
    { .current = TR_KEY_size_units, .legacy = TR_KEY_size_units_kebab_APICOMPAT },
    { .current = TR_KEY_size_when_done, .legacy = TR_KEY_size_when_done_camel_APICOMPAT },
    { .current = TR_KEY_speed_bytes, .legacy = TR_KEY_speed_bytes_kebab_APICOMPAT },
    { .current = TR_KEY_speed_limit_down, .legacy = TR_KEY_speed_limit_down_kebab_APICOMPAT },
    { .current = TR_KEY_speed_limit_down_enabled, .legacy = TR_KEY_speed_limit_down_enabled_kebab_APICOMPAT },
    { .current = TR_KEY_speed_limit_up, .legacy = TR_KEY_speed_limit_up_kebab_APICOMPAT },
    { .current = TR_KEY_speed_limit_up_enabled, .legacy = TR_KEY_speed_limit_up_enabled_kebab_APICOMPAT },
    { .current = TR_KEY_speed_units, .legacy = TR_KEY_speed_units_kebab_APICOMPAT },
    { .current = TR_KEY_start_added_torrents, .legacy = TR_KEY_start_added_torrents_kebab_APICOMPAT },
    { .current = TR_KEY_start_date, .legacy = TR_KEY_start_date_camel_APICOMPAT },
    { .current = TR_KEY_tcp_enabled, .legacy = TR_KEY_tcp_enabled_kebab_APICOMPAT },
    { .current = TR_KEY_torrent_added, .legacy = TR_KEY_torrent_added_kebab_APICOMPAT },
    { .current = TR_KEY_torrent_count, .legacy = TR_KEY_torrent_count_camel_APICOMPAT },
    { .current = TR_KEY_torrent_duplicate, .legacy = TR_KEY_torrent_duplicate_kebab_APICOMPAT },
    { .current = TR_KEY_torrent_file, .legacy = TR_KEY_torrent_file_camel_APICOMPAT },
    { .current = TR_KEY_total_size, .legacy = TR_KEY_total_size_camel_APICOMPAT },
    { .current = TR_KEY_tracker_add, .legacy = TR_KEY_tracker_add_camel_APICOMPAT },
    { .current = TR_KEY_tracker_list, .legacy = TR_KEY_tracker_list_camel_APICOMPAT },
    { .current = TR_KEY_tracker_remove, .legacy = TR_KEY_tracker_remove_camel_APICOMPAT },
    { .current = TR_KEY_tracker_replace, .legacy = TR_KEY_tracker_replace_camel_APICOMPAT },
    { .current = TR_KEY_tracker_stats, .legacy = TR_KEY_tracker_stats_camel_APICOMPAT },
    { .current = TR_KEY_trash_original_torrent_files, .legacy = TR_KEY_trash_original_torrent_files_kebab_APICOMPAT },
    { .current = TR_KEY_upload_limit, .legacy = TR_KEY_upload_limit_camel_APICOMPAT },
    { .current = TR_KEY_upload_limited, .legacy = TR_KEY_upload_limited_camel_APICOMPAT },
    { .current = TR_KEY_upload_ratio, .legacy = TR_KEY_upload_ratio_camel_APICOMPAT },
    { .current = TR_KEY_upload_speed, .legacy = TR_KEY_upload_speed_camel_APICOMPAT },
    { .current = TR_KEY_uploaded_bytes, .legacy = TR_KEY_uploaded_bytes_camel_APICOMPAT },
    { .current = TR_KEY_uploaded_ever, .legacy = TR_KEY_uploaded_ever_camel_APICOMPAT },
    { .current = TR_KEY_utp_enabled, .legacy = TR_KEY_utp_enabled_kebab_APICOMPAT },
    { .current = TR_KEY_webseeds_sending_to_us, .legacy = TR_KEY_webseeds_sending_to_us_camel_APICOMPAT },
    { .current = TR_KEY_blocklist_update, .legacy = TR_KEY_blocklist_update_kebab_APICOMPAT },
    { .current = TR_KEY_free_space, .legacy = TR_KEY_free_space_kebab_APICOMPAT },
    { .current = TR_KEY_group_get, .legacy = TR_KEY_group_get_kebab_APICOMPAT },
    { .current = TR_KEY_group_set, .legacy = TR_KEY_group_set_kebab_APICOMPAT },
    { .current = TR_KEY_port_test, .legacy = TR_KEY_port_test_kebab_APICOMPAT },
    { .current = TR_KEY_queue_move_bottom, .legacy = TR_KEY_queue_move_bottom_kebab_APICOMPAT },
    { .current = TR_KEY_queue_move_down, .legacy = TR_KEY_queue_move_down_kebab_APICOMPAT },
    { .current = TR_KEY_queue_move_top, .legacy = TR_KEY_queue_move_top_kebab_APICOMPAT },
    { .current = TR_KEY_queue_move_up, .legacy = TR_KEY_queue_move_up_kebab_APICOMPAT },
    { .current = TR_KEY_session_close, .legacy = TR_KEY_session_close_kebab_APICOMPAT },
    { .current = TR_KEY_session_get, .legacy = TR_KEY_session_get_kebab_APICOMPAT },
    { .current = TR_KEY_session_set, .legacy = TR_KEY_session_set_kebab_APICOMPAT },
    { .current = TR_KEY_session_stats, .legacy = TR_KEY_session_stats_kebab_APICOMPAT },
    { .current = TR_KEY_torrent_add, .legacy = TR_KEY_torrent_add_kebab_APICOMPAT },
    { .current = TR_KEY_torrent_get, .legacy = TR_KEY_torrent_get_kebab },
    { .current = TR_KEY_torrent_reannounce, .legacy = TR_KEY_torrent_reannounce_kebab_APICOMPAT },
    { .current = TR_KEY_torrent_remove, .legacy = TR_KEY_torrent_remove_kebab_APICOMPAT },
    { .current = TR_KEY_torrent_rename_path, .legacy = TR_KEY_torrent_rename_path_kebab_APICOMPAT },
    { .current = TR_KEY_torrent_set, .legacy = TR_KEY_torrent_set_kebab_APICOMPAT },
    { .current = TR_KEY_torrent_set_location, .legacy = TR_KEY_torrent_set_location_kebab_APICOMPAT },
    { .current = TR_KEY_torrent_start, .legacy = TR_KEY_torrent_start_kebab_APICOMPAT },
    { .current = TR_KEY_torrent_start_now, .legacy = TR_KEY_torrent_start_now_kebab_APICOMPAT },
    { .current = TR_KEY_torrent_stop, .legacy = TR_KEY_torrent_stop_kebab_APICOMPAT },
    { .current = TR_KEY_torrent_verify, .legacy = TR_KEY_torrent_verify_kebab_APICOMPAT },
} };

auto constexpr SessionKeys = std::array<ApiKey, 139U>{ {
    { .current = TR_KEY_activity_date, .legacy = TR_KEY_activity_date_kebab_APICOMPAT },
    { .current = TR_KEY_added_date, .legacy = TR_KEY_added_date_kebab_APICOMPAT },
    { .current = TR_KEY_alt_speed_down, .legacy = TR_KEY_alt_speed_down_kebab_APICOMPAT },
    { .current = TR_KEY_alt_speed_enabled, .legacy = TR_KEY_alt_speed_enabled_kebab_APICOMPAT },
    { .current = TR_KEY_alt_speed_time_begin, .legacy = TR_KEY_alt_speed_time_begin_kebab_APICOMPAT },
    { .current = TR_KEY_alt_speed_time_day, .legacy = TR_KEY_alt_speed_time_day_kebab_APICOMPAT },
    { .current = TR_KEY_alt_speed_time_enabled, .legacy = TR_KEY_alt_speed_time_enabled_kebab_APICOMPAT },
    { .current = TR_KEY_alt_speed_time_end, .legacy = TR_KEY_alt_speed_time_end_kebab_APICOMPAT },
    { .current = TR_KEY_alt_speed_up, .legacy = TR_KEY_alt_speed_up_kebab_APICOMPAT },
    { .current = TR_KEY_announce_ip, .legacy = TR_KEY_announce_ip_kebab_APICOMPAT },
    { .current = TR_KEY_announce_ip_enabled, .legacy = TR_KEY_announce_ip_enabled_kebab_APICOMPAT },
    { .current = TR_KEY_anti_brute_force_enabled, .legacy = TR_KEY_anti_brute_force_enabled_kebab_APICOMPAT },
    { .current = TR_KEY_anti_brute_force_threshold, .legacy = TR_KEY_anti_brute_force_threshold_kebab_APICOMPAT },
    { .current = TR_KEY_bandwidth_priority, .legacy = TR_KEY_bandwidth_priority_kebab_APICOMPAT },
    { .current = TR_KEY_bind_address_ipv4, .legacy = TR_KEY_bind_address_ipv4_kebab_APICOMPAT },
    { .current = TR_KEY_bind_address_ipv6, .legacy = TR_KEY_bind_address_ipv6_kebab_APICOMPAT },
    { .current = TR_KEY_blocklist_date, .legacy = TR_KEY_blocklist_date_kebab_APICOMPAT },
    { .current = TR_KEY_blocklist_enabled, .legacy = TR_KEY_blocklist_enabled_kebab_APICOMPAT },
    { .current = TR_KEY_blocklist_updates_enabled, .legacy = TR_KEY_blocklist_updates_enabled_kebab_APICOMPAT },
    { .current = TR_KEY_blocklist_url, .legacy = TR_KEY_blocklist_url_kebab_APICOMPAT },
    { .current = TR_KEY_cache_size_mib, .legacy = TR_KEY_cache_size_mb_kebab_APICOMPAT },
    { .current = TR_KEY_compact_view, .legacy = TR_KEY_compact_view_kebab_APICOMPAT },
    { .current = TR_KEY_default_trackers, .legacy = TR_KEY_default_trackers_kebab_APICOMPAT },
    { .current = TR_KEY_details_window_height, .legacy = TR_KEY_details_window_height_kebab_APICOMPAT },
    { .current = TR_KEY_details_window_width, .legacy = TR_KEY_details_window_width_kebab_APICOMPAT },
    { .current = TR_KEY_dht_enabled, .legacy = TR_KEY_dht_enabled_kebab_APICOMPAT },
    { .current = TR_KEY_done_date, .legacy = TR_KEY_done_date_kebab_APICOMPAT },
    { .current = TR_KEY_download_dir, .legacy = TR_KEY_download_dir_kebab_APICOMPAT },
    { .current = TR_KEY_download_queue_enabled, .legacy = TR_KEY_download_queue_enabled_kebab_APICOMPAT },
    { .current = TR_KEY_download_queue_size, .legacy = TR_KEY_download_queue_size_kebab_APICOMPAT },
    { .current = TR_KEY_downloaded_bytes, .legacy = TR_KEY_downloaded_bytes_kebab_APICOMPAT },
    { .current = TR_KEY_downloading_time_seconds, .legacy = TR_KEY_downloading_time_seconds_kebab_APICOMPAT },
    { .current = TR_KEY_files_added, .legacy = TR_KEY_files_added_kebab_APICOMPAT },
    { .current = TR_KEY_filter_mode, .legacy = TR_KEY_filter_mode_kebab_APICOMPAT },
    { .current = TR_KEY_filter_text, .legacy = TR_KEY_filter_text_kebab_APICOMPAT },
    { .current = TR_KEY_filter_trackers, .legacy = TR_KEY_filter_trackers_kebab_APICOMPAT },
    { .current = TR_KEY_idle_limit, .legacy = TR_KEY_idle_limit_kebab_APICOMPAT },
    { .current = TR_KEY_idle_mode, .legacy = TR_KEY_idle_mode_kebab_APICOMPAT },
    { .current = TR_KEY_idle_seeding_limit, .legacy = TR_KEY_idle_seeding_limit_kebab_APICOMPAT },
    { .current = TR_KEY_idle_seeding_limit_enabled, .legacy = TR_KEY_idle_seeding_limit_enabled_kebab_APICOMPAT },
    { .current = TR_KEY_incomplete_dir, .legacy = TR_KEY_incomplete_dir_kebab_APICOMPAT },
    { .current = TR_KEY_incomplete_dir_enabled, .legacy = TR_KEY_incomplete_dir_enabled_kebab_APICOMPAT },
    { .current = TR_KEY_inhibit_desktop_hibernation, .legacy = TR_KEY_inhibit_desktop_hibernation_kebab_APICOMPAT },
    { .current = TR_KEY_lpd_enabled, .legacy = TR_KEY_lpd_enabled_kebab_APICOMPAT },
    { .current = TR_KEY_main_window_height, .legacy = TR_KEY_main_window_height_kebab_APICOMPAT },
    { .current = TR_KEY_main_window_is_maximized, .legacy = TR_KEY_main_window_is_maximized_kebab_APICOMPAT },
    { .current = TR_KEY_main_window_layout_order, .legacy = TR_KEY_main_window_layout_order_kebab_APICOMPAT },
    { .current = TR_KEY_main_window_width, .legacy = TR_KEY_main_window_width_kebab_APICOMPAT },
    { .current = TR_KEY_main_window_x, .legacy = TR_KEY_main_window_x_kebab_APICOMPAT },
    { .current = TR_KEY_main_window_y, .legacy = TR_KEY_main_window_y_kebab_APICOMPAT },
    { .current = TR_KEY_max_peers, .legacy = TR_KEY_max_peers_kebab_APICOMPAT },
    { .current = TR_KEY_message_level, .legacy = TR_KEY_message_level_kebab_APICOMPAT },
    { .current = TR_KEY_open_dialog_dir, .legacy = TR_KEY_open_dialog_dir_kebab_APICOMPAT },
    { .current = TR_KEY_peer_congestion_algorithm, .legacy = TR_KEY_peer_congestion_algorithm_kebab_APICOMPAT },
    { .current = TR_KEY_peer_limit_global, .legacy = TR_KEY_peer_limit_global_kebab_APICOMPAT },
    { .current = TR_KEY_peer_limit_per_torrent, .legacy = TR_KEY_peer_limit_per_torrent_kebab_APICOMPAT },
    { .current = TR_KEY_peer_port, .legacy = TR_KEY_peer_port_kebab_APICOMPAT },
    { .current = TR_KEY_peer_port_random_high, .legacy = TR_KEY_peer_port_random_high_kebab_APICOMPAT },
    { .current = TR_KEY_peer_port_random_low, .legacy = TR_KEY_peer_port_random_low_kebab_APICOMPAT },
    { .current = TR_KEY_peer_port_random_on_start, .legacy = TR_KEY_peer_port_random_on_start_kebab_APICOMPAT },
    { .current = TR_KEY_peer_socket_diffserv, .legacy = TR_KEY_peer_socket_tos_kebab_APICOMPAT },
    { .current = TR_KEY_peers2_6, .legacy = TR_KEY_peers2_6_kebab_APICOMPAT },
    { .current = TR_KEY_pex_enabled, .legacy = TR_KEY_pex_enabled_kebab_APICOMPAT },
    { .current = TR_KEY_port_forwarding_enabled, .legacy = TR_KEY_port_forwarding_enabled_kebab_APICOMPAT },
    { .current = TR_KEY_prompt_before_exit, .legacy = TR_KEY_prompt_before_exit_kebab_APICOMPAT },
    { .current = TR_KEY_queue_stalled_enabled, .legacy = TR_KEY_queue_stalled_enabled_kebab_APICOMPAT },
    { .current = TR_KEY_queue_stalled_minutes, .legacy = TR_KEY_queue_stalled_minutes_kebab_APICOMPAT },
    { .current = TR_KEY_ratio_limit, .legacy = TR_KEY_ratio_limit_kebab_APICOMPAT },
    { .current = TR_KEY_ratio_limit_enabled, .legacy = TR_KEY_ratio_limit_enabled_kebab_APICOMPAT },
    { .current = TR_KEY_ratio_mode, .legacy = TR_KEY_ratio_mode_kebab_APICOMPAT },
    { .current = TR_KEY_read_clipboard, .legacy = TR_KEY_read_clipboard_kebab_APICOMPAT },
    { .current = TR_KEY_remote_session_enabled, .legacy = TR_KEY_remote_session_enabled_kebab_APICOMPAT },
    { .current = TR_KEY_remote_session_host, .legacy = TR_KEY_remote_session_host_kebab_APICOMPAT },
    { .current = TR_KEY_remote_session_https, .legacy = TR_KEY_remote_session_https_kebab_APICOMPAT },
    { .current = TR_KEY_remote_session_password, .legacy = TR_KEY_remote_session_password_kebab_APICOMPAT },
    { .current = TR_KEY_remote_session_port, .legacy = TR_KEY_remote_session_port_kebab_APICOMPAT },
    { .current = TR_KEY_remote_session_requires_authentication,
      .legacy = TR_KEY_remote_session_requres_authentication_kebab_APICOMPAT },
    { .current = TR_KEY_remote_session_username, .legacy = TR_KEY_remote_session_username_kebab_APICOMPAT },
    { .current = TR_KEY_rename_partial_files, .legacy = TR_KEY_rename_partial_files_kebab_APICOMPAT },
    { .current = TR_KEY_rpc_authentication_required, .legacy = TR_KEY_rpc_authentication_required_kebab_APICOMPAT },
    { .current = TR_KEY_rpc_bind_address, .legacy = TR_KEY_rpc_bind_address_kebab_APICOMPAT },
    { .current = TR_KEY_rpc_enabled, .legacy = TR_KEY_rpc_enabled_kebab_APICOMPAT },
    { .current = TR_KEY_rpc_host_whitelist, .legacy = TR_KEY_rpc_host_whitelist_kebab_APICOMPAT },
    { .current = TR_KEY_rpc_host_whitelist_enabled, .legacy = TR_KEY_rpc_host_whitelist_enabled_kebab_APICOMPAT },
    { .current = TR_KEY_rpc_password, .legacy = TR_KEY_rpc_password_kebab_APICOMPAT },
    { .current = TR_KEY_rpc_port, .legacy = TR_KEY_rpc_port_kebab_APICOMPAT },
    { .current = TR_KEY_rpc_socket_mode, .legacy = TR_KEY_rpc_socket_mode_kebab_APICOMPAT },
    { .current = TR_KEY_rpc_url, .legacy = TR_KEY_rpc_url_kebab_APICOMPAT },
    { .current = TR_KEY_rpc_username, .legacy = TR_KEY_rpc_username_kebab_APICOMPAT },
    { .current = TR_KEY_rpc_whitelist, .legacy = TR_KEY_rpc_whitelist_kebab_APICOMPAT },
    { .current = TR_KEY_rpc_whitelist_enabled, .legacy = TR_KEY_rpc_whitelist_enabled_kebab_APICOMPAT },
    { .current = TR_KEY_scrape_paused_torrents_enabled, .legacy = TR_KEY_scrape_paused_torrents_enabled_kebab_APICOMPAT },
    { .current = TR_KEY_script_torrent_added_enabled, .legacy = TR_KEY_script_torrent_added_enabled_kebab_APICOMPAT },
    { .current = TR_KEY_script_torrent_added_filename, .legacy = TR_KEY_script_torrent_added_filename_kebab_APICOMPAT },
    { .current = TR_KEY_script_torrent_done_enabled, .legacy = TR_KEY_script_torrent_done_enabled_kebab_APICOMPAT },
    { .current = TR_KEY_script_torrent_done_filename, .legacy = TR_KEY_script_torrent_done_filename_kebab_APICOMPAT },
    { .current = TR_KEY_script_torrent_done_seeding_enabled,
      .legacy = TR_KEY_script_torrent_done_seeding_enabled_kebab_APICOMPAT },
    { .current = TR_KEY_script_torrent_done_seeding_filename,
      .legacy = TR_KEY_script_torrent_done_seeding_filename_kebab_APICOMPAT },
    { .current = TR_KEY_seconds_active, .legacy = TR_KEY_seconds_active_kebab_APICOMPAT },
    { .current = TR_KEY_seed_queue_enabled, .legacy = TR_KEY_seed_queue_enabled_kebab_APICOMPAT },
    { .current = TR_KEY_seed_queue_size, .legacy = TR_KEY_seed_queue_size_kebab_APICOMPAT },
    { .current = TR_KEY_seeding_time_seconds, .legacy = TR_KEY_seeding_time_seconds_kebab_APICOMPAT },
    { .current = TR_KEY_session_count, .legacy = TR_KEY_session_count_kebab_APICOMPAT },
    { .current = TR_KEY_show_backup_trackers, .legacy = TR_KEY_show_backup_trackers_kebab_APICOMPAT },
    { .current = TR_KEY_show_extra_peer_details, .legacy = TR_KEY_show_extra_peer_details_kebab_APICOMPAT },
    { .current = TR_KEY_show_filterbar, .legacy = TR_KEY_show_filterbar_kebab_APICOMPAT },
    { .current = TR_KEY_show_notification_area_icon, .legacy = TR_KEY_show_notification_area_icon_kebab_APICOMPAT },
    { .current = TR_KEY_show_options_window, .legacy = TR_KEY_show_options_window_kebab_APICOMPAT },
    { .current = TR_KEY_show_statusbar, .legacy = TR_KEY_show_statusbar_kebab_APICOMPAT },
    { .current = TR_KEY_show_toolbar, .legacy = TR_KEY_show_toolbar_kebab_APICOMPAT },
    { .current = TR_KEY_show_tracker_scrapes, .legacy = TR_KEY_show_tracker_scrapes_kebab_APICOMPAT },
    { .current = TR_KEY_sleep_per_seconds_during_verify, .legacy = TR_KEY_sleep_per_seconds_during_verify_kebab_APICOMPAT },
    { .current = TR_KEY_sort_mode, .legacy = TR_KEY_sort_mode_kebab_APICOMPAT },
    { .current = TR_KEY_sort_reversed, .legacy = TR_KEY_sort_reversed_kebab_APICOMPAT },
    { .current = TR_KEY_speed_Bps, .legacy = TR_KEY_speed_Bps_kebab_APICOMPAT },
    { .current = TR_KEY_speed_limit_down, .legacy = TR_KEY_speed_limit_down_kebab_APICOMPAT },
    { .current = TR_KEY_speed_limit_down_enabled, .legacy = TR_KEY_speed_limit_down_enabled_kebab_APICOMPAT },
    { .current = TR_KEY_speed_limit_up, .legacy = TR_KEY_speed_limit_up_kebab_APICOMPAT },
    { .current = TR_KEY_speed_limit_up_enabled, .legacy = TR_KEY_speed_limit_up_enabled_kebab_APICOMPAT },
    { .current = TR_KEY_start_added_torrents, .legacy = TR_KEY_start_added_torrents_kebab_APICOMPAT },
    { .current = TR_KEY_start_minimized, .legacy = TR_KEY_start_minimized_kebab_APICOMPAT },
    { .current = TR_KEY_statusbar_stats, .legacy = TR_KEY_statusbar_stats_kebab_APICOMPAT },
    { .current = TR_KEY_tcp_enabled, .legacy = TR_KEY_tcp_enabled_kebab_APICOMPAT },
    { .current = TR_KEY_time_checked, .legacy = TR_KEY_time_checked_kebab_APICOMPAT },
    { .current = TR_KEY_torrent_added_notification_enabled,
      .legacy = TR_KEY_torrent_added_notification_enabled_kebab_APICOMPAT },
    { .current = TR_KEY_torrent_added_verify_mode, .legacy = TR_KEY_torrent_added_verify_mode_kebab_APICOMPAT },
    { .current = TR_KEY_torrent_complete_notification_enabled,
      .legacy = TR_KEY_torrent_complete_notification_enabled_kebab_APICOMPAT },
    { .current = TR_KEY_torrent_complete_sound_command, .legacy = TR_KEY_torrent_complete_sound_command_kebab_APICOMPAT },
    { .current = TR_KEY_torrent_complete_sound_enabled, .legacy = TR_KEY_torrent_complete_sound_enabled_kebab_APICOMPAT },
    { .current = TR_KEY_trash_can_enabled, .legacy = TR_KEY_trash_can_enabled_kebab_APICOMPAT },
    { .current = TR_KEY_trash_original_torrent_files, .legacy = TR_KEY_trash_original_torrent_files_kebab_APICOMPAT },
    { .current = TR_KEY_upload_slots_per_torrent, .legacy = TR_KEY_upload_slots_per_torrent_kebab_APICOMPAT },
    { .current = TR_KEY_uploaded_bytes, .legacy = TR_KEY_uploaded_bytes_kebab_APICOMPAT },
    { .current = TR_KEY_use_global_speed_limit, .legacy = TR_KEY_use_global_speed_limit_kebab_APICOMPAT },
    { .current = TR_KEY_use_speed_limit, .legacy = TR_KEY_use_speed_limit_kebab_APICOMPAT },
    { .current = TR_KEY_utp_enabled, .legacy = TR_KEY_utp_enabled_kebab_APICOMPAT },
    { .current = TR_KEY_watch_dir, .legacy = TR_KEY_watch_dir_kebab_APICOMPAT },
    { .current = TR_KEY_watch_dir_enabled, .legacy = TR_KEY_watch_dir_enabled_kebab_APICOMPAT },
    { .current = TR_KEY_watch_dir_force_generic, .legacy = TR_KEY_watch_dir_force_generic_kebab_APICOMPAT },
} };

auto constexpr MethodNotFoundLegacyErrmsg = std::string_view{ "no method name" };

namespace EncryptionModeString
{
auto constexpr PreferEncryption = std::string_view{ "preferred" };
auto constexpr RequireEncryption = std::string_view{ "required" };
auto constexpr PreferClear = std::string_view{ "allowed" };
auto constexpr PreferClearLegacy = std::string_view{ "tolerated" };
} // namespace EncryptionModeString

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
    bool is_free_space_response = false;
    bool is_request = false;
    bool is_response = false;
    bool is_rpc = false;
    bool is_settings = false;
    bool is_success = false;
    bool is_torrent = false;
    bool was_jsonrpc = false;
    bool was_legacy = false;
    std::deque<tr_quark> path;

    [[nodiscard]] bool current_key_is_any_of(std::initializer_list<tr_quark> const pool) const noexcept
    {
        return !std::empty(path) && std::count(std::cbegin(pool), std::cend(pool), path.back()) != 0U;
    }
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
    state.is_settings = !state.is_rpc;

    state.is_success = state.is_response &&
        (was_jsonrpc_response ? top.contains(TR_KEY_result) :
                                top.value_if<std::string_view>(TR_KEY_result).value_or("") == "success");

    if (auto const method = top.value_if<std::string_view>(TR_KEY_method))
    {
        auto const key = tr_quark_lookup(*method);
        state.is_torrent = key &&
            (*key == TR_KEY_torrent_get || *key == TR_KEY_torrent_get_kebab || *key == TR_KEY_torrent_set ||
             *key == TR_KEY_torrent_set_kebab_APICOMPAT);
    }

    if (state.is_response)
    {
        if (auto const* const args = top.find_if<tr_variant::Map>(state.was_jsonrpc ? TR_KEY_result : TR_KEY_arguments))
        {
            state.is_free_space_response = args->contains(TR_KEY_path) &&
                args->contains(state.was_jsonrpc ? TR_KEY_size_bytes : TR_KEY_size_bytes_kebab_APICOMPAT);
            state.is_torrent = args->contains(TR_KEY_torrents);
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
        (src == TR_KEY_download_dir_camel_APICOMPAT || src == TR_KEY_download_dir_kebab_APICOMPAT ||
         src == TR_KEY_download_dir))
    {
        if (state.style == Style::Tr5)
        {
            return TR_KEY_download_dir;
        }

        if (state.is_torrent)
        {
            return TR_KEY_download_dir_camel_APICOMPAT;
        }

        return TR_KEY_download_dir_kebab_APICOMPAT;
    }

    // Crazy case:
    // totalSize in Tr4 torrent-get
    // total_size in Tr4 free-space
    // total_size in Tr5
    if (state.is_rpc && state.is_free_space_response && (src == TR_KEY_total_size || src == TR_KEY_total_size_camel_APICOMPAT))
    {
        return state.style == Style::Tr5 || state.is_free_space_response ? TR_KEY_total_size :
                                                                           TR_KEY_total_size_camel_APICOMPAT;
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
    if (state.is_settings && state.current_key_is_any_of({ TR_KEY_sort_mode, TR_KEY_sort_mode_kebab_APICOMPAT }))
    {
        static auto constexpr Strings = std::array<std::pair<std::string_view /*Tr5*/, std::string_view /*Tr4*/>, 10U>{ {
            { "sort_by_activity", "sort-by-activity" },
            { "sort_by_age", "sort-by-age" },
            { "sort_by_eta", "sort-by-eta" },
            { "sort_by_id", "sort-by-id" },
            { "sort_by_name", "sort-by-name" },
            { "sort_by_progress", "sort-by-progress" },
            { "sort_by_queue", "sort-by-queue" },
            { "sort_by_ratio", "sort-by-ratio" },
            { "sort_by_size", "sort-by-size" },
            { "sort_by_state", "sort-by-state" },
        } };
        for (auto const& [current, legacy] : Strings)
        {
            if (src == current || src == legacy)
            {
                return state.style == Style::Tr5 ? current : legacy;
            }
        }
    }

    if (state.is_settings && state.current_key_is_any_of({ TR_KEY_filter_mode, TR_KEY_filter_mode_kebab_APICOMPAT }))
    {
        static auto constexpr Strings = std::array<std::pair<std::string_view, std::string_view>, 8U>{ {
            { "show_active", "show-active" },
            { "show_all", "show-all" },
            { "show_downloading", "show-downloading" },
            { "show_error", "show-error" },
            { "show_finished", "show-finished" },
            { "show_paused", "show-paused" },
            { "show_seeding", "show-seeding" },
            { "show_verifying", "show-verifying" },
        } };
        for (auto const& [current, legacy] : Strings)
        {
            if (src == current || src == legacy)
            {
                return state.style == Style::Tr5 ? current : legacy;
            }
        }
    }

    if (state.is_settings && state.current_key_is_any_of({ TR_KEY_statusbar_stats, TR_KEY_statusbar_stats_kebab_APICOMPAT }))
    {
        static auto constexpr Strings = std::array<std::pair<std::string_view, std::string_view>, 4U>{ {
            { "total_ratio", "total-ratio" },
            { "total_transfer", "total-transfer" },
            { "session_ratio", "session-ratio" },
            { "session_transfer", "session-transfer" },
        } };
        for (auto const& [current, legacy] : Strings)
        {
            if (src == current || src == legacy)
            {
                return state.style == Style::Tr5 ? current : legacy;
            }
        }
    }

    // TODO(ckerr): replace `new_key == TR_KEY_TORRENTS` here to turn on convert
    // if it's an array inside an array val whose key was `torrents`.
    // This is for the edge case of table mode: `torrents : [ [ 'key1', 'key2' ], [ ... ] ]`
    if (state.is_rpc && state.current_key_is_any_of({ TR_KEY_method, TR_KEY_fields, TR_KEY_ids, TR_KEY_torrents }))
    {
        if (auto const old_key = tr_quark_lookup(src))
        {
            if (auto const new_key = convert_key(state, *old_key); *old_key != new_key)
            {
                return tr_quark_get_string_view(new_key);
            }
        }
    }

    return {};
}

void convert_keys(tr_variant& var, State& state)
{
    var.visit(
        [&state](auto& val)
        {
            using ValueType = std::remove_cvref_t<decltype(val)>;

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

                    state.path.push_back(new_key);
                    convert_keys(child, state);
                    state.path.pop_back();
                }
            }
        });
}

void convert_settings_encryption(tr_variant::Map& top, State const& state)
{
    if (state.is_rpc)
    {
        return;
    }

    if (state.style == Style::Tr4)
    {
        using namespace EncryptionModeString;
        if (auto const encryption = top.value_if<std::string_view>(TR_KEY_encryption); encryption == PreferEncryption)
        {
            top.insert_or_assign(TR_KEY_encryption, TR_ENCRYPTION_PREFERRED);
        }
        else if (encryption == RequireEncryption)
        {
            top.insert_or_assign(TR_KEY_encryption, TR_ENCRYPTION_REQUIRED);
        }
        else if (encryption == PreferClear)
        {
            top.insert_or_assign(TR_KEY_encryption, TR_CLEAR_PREFERRED);
        }
    }

    if (state.style == Style::Tr5)
    {
        if (auto const* const encryption = top.find_if<int64_t>(TR_KEY_encryption))
        {
            top.insert_or_assign(TR_KEY_encryption, serializer::to_variant(static_cast<tr_encryption_mode>(*encryption)));
        }
    }
}

namespace convert_jsonrpc_helpers
{
void convert_files_wanted(tr_variant::Vector& wanted, State const& state)
{
    auto ret = tr_variant::Vector{};
    ret.reserve(std::size(wanted));
    for (auto const& var : wanted)
    {
        if (state.style == Style::Tr5)
        {
            if (auto const val = var.value_if<bool>())
            {
                ret.emplace_back(*val);
            }
            else
            {
                return;
            }
        }
        else
        {
            if (auto const val = var.value_if<int64_t>(); val == 0 || val == 1)
            {
                ret.emplace_back(*val);
            }
            else
            {
                return;
            }
        }
    }

    wanted = std::move(ret);
}

void convert_files_wanted_response(tr_variant::Map& top, State const& state)
{
    if (auto* const args = top.find_if<tr_variant::Map>(state.style == Style::Tr5 ? TR_KEY_result : TR_KEY_arguments))
    {
        if (auto* const torrents = args->find_if<tr_variant::Vector>(TR_KEY_torrents);
            torrents != nullptr && !std::empty(*torrents))
        {
            // TrFormat::Table
            if (auto* const first_vec = torrents->front().get_if<tr_variant::Vector>();
                first_vec != nullptr && !std::empty(*first_vec))
            {
                if (auto const wanted_iter = std::ranges::find_if(
                        *first_vec,
                        [](tr_variant const& v)
                        { return v.value_if<std::string_view>() == tr_quark_get_string_view(TR_KEY_wanted); });
                    wanted_iter != std::ranges::end(*first_vec))
                {
                    auto const wanted_idx = static_cast<size_t>(wanted_iter - std::begin(*first_vec));
                    for (auto it = std::next(std::begin(*torrents)); it != std::end(*torrents); ++it)
                    {
                        if (auto* const row = it->get_if<tr_variant::Vector>(); row != nullptr && wanted_idx < std::size(*row))
                        {
                            if (auto* const wanted = (*row)[wanted_idx].get_if<tr_variant::Vector>())
                            {
                                convert_files_wanted(*wanted, state);
                            }
                        }
                    }
                }
            }
            // TrFormat::Object
            else if (torrents->front().index() == tr_variant::MapIndex)
            {
                for (auto& var : *torrents)
                {
                    if (auto* const map = var.get_if<tr_variant::Map>())
                    {
                        if (auto* const wanted = map->find_if<tr_variant::Vector>(TR_KEY_wanted))
                        {
                            convert_files_wanted(*wanted, state);
                        }
                    }
                }
            }
        }
    }
}

// ---

void convert_encryption(tr_variant& var, State const& state)
{
    using namespace EncryptionModeString;
    if (auto const val = var.value_if<std::string_view>())
    {
        switch (state.style)
        {
        case Style::Tr5:
            if (val == PreferClearLegacy)
            {
                var = tr_variant::unmanaged_string(PreferClear);
            }
            break;
        case Style::Tr4:
            if (val == PreferClear)
            {
                var = tr_variant::unmanaged_string(PreferClearLegacy);
            }
            break;
        }
    }
}
} // namespace convert_jsonrpc_helpers

// jsonrpc <-> legacy rpc conversion
void convert_jsonrpc(tr_variant::Map& top, State const& state)
{
    using namespace convert_jsonrpc_helpers;

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
        if (auto const tag = top.find_if<int64_t>(TR_KEY_tag); state.was_jsonrpc && tag == nullptr)
        {
            top.erase(TR_KEY_tag);
        }
    }

    if (state.is_response && is_legacy && state.is_success && state.was_jsonrpc)
    {
        // in legacy messages:
        // - move `result` to `arguments`
        // - add `result: "success"`
        top.replace_key(TR_KEY_result, TR_KEY_arguments);
        top.try_emplace(TR_KEY_result, tr_variant::unmanaged_string("success"));

        convert_files_wanted_response(top, state);

        if (auto* const args = top.find_if<tr_variant::Map>(TR_KEY_arguments))
        {
            if (auto const iter = args->find(TR_KEY_encryption); iter != std::end(*args))
            {
                convert_encryption(iter->second, state);
            }
        }
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
                if (auto const errmsg = data->value_if<std::string_view>(TR_KEY_error_string_camel_APICOMPAT))
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

        convert_files_wanted_response(top, state);

        if (auto* const result = top.find_if<tr_variant::Map>(TR_KEY_result))
        {
            if (auto const iter = result->find(TR_KEY_encryption); iter != std::end(*result))
            {
                convert_encryption(iter->second, state);
            }
        }
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

        if (auto* const params = top.find_if<tr_variant::Map>(TR_KEY_params))
        {
            if (auto const iter = params->find(TR_KEY_encryption); iter != std::end(*params))
            {
                convert_encryption(iter->second, state);
            }
        }
    }

    if (state.is_request && is_legacy)
    {
        top.replace_key(TR_KEY_params, TR_KEY_arguments);

        if (auto* const args = top.find_if<tr_variant::Map>(TR_KEY_arguments))
        {
            if (auto const iter = args->find(TR_KEY_encryption); iter != std::end(*args))
            {
                convert_encryption(iter->second, state);
            }
        }
    }
}

// TODO(TR5) change default to Tr5.
Style default_style_g = tr_env_get_string("TR_SAVE_VERSION_FORMAT", "4") == "5" ? Style::Tr5 : Style::Tr4;

} // namespace

Style default_style()
{
    return default_style_g;
}

void set_default_style(Style const style)
{
    default_style_g = style;
}

void convert(tr_variant& var, Style const tgt_style)
{
    if (auto* const top = var.get_if<tr_variant::Map>())
    {
        auto state = makeState(*top);
        state.style = tgt_style;
        convert_keys(var, state);
        convert_settings_encryption(*top, state);
        convert_jsonrpc(*top, state);
    }
}

void convert_outgoing_data(tr_variant& var)
{
    convert(var, default_style());
}

void convert_incoming_data(tr_variant& var)
{
    convert(var, Style::Tr5);
}
} // namespace tr::api_compat
