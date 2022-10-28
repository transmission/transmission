// This file Copyright © 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cstddef> // for size_t
#include <string>

#include "transmission.h"

#include "log.h" // for tr_log_level
#include "net.h" // for tr_port
#include "quark.h"
#include "rpc-server.h" // for tr_rpc_server::DefaultRpcSocketMode

struct tr_variant;

#define SESSION_SETTINGS_FIELDS(V) \
    V(TR_KEY_dht_enabled, dht_enabled, bool, true) \
    V(TR_KEY_encryption, encryption_mode, tr_encryption_mode, TR_ENCRYPTION_PREFERRED) \
    V(TR_KEY_lpd_enabled, lpd_enabled, bool, true) \
    V(TR_KEY_pex_enabled, pex_enabled, bool, true) \
    V(TR_KEY_umask, umask, mode_t, 022) \
    V(TR_KEY_utp_enabled, utp_enabled, bool, true) \
    /* below here: unprocessed */ \
    V(TR_KEY_alt_speed_down, alt_speed_down_kilobytes_per_second, size_t, 50U) \
    V(TR_KEY_alt_speed_enabled, alt_speed_enabled, bool, false) \
    V(TR_KEY_alt_speed_time_begin, alt_speed_time_begin, size_t, 540U) /* minutes past midnight; 9AM */ \
    V(TR_KEY_alt_speed_time_day, alt_speed_time_day, size_t, TR_SCHED_ALL) \
    V(TR_KEY_alt_speed_time_enabled, alt_speed_time_enabled, bool, false) \
    V(TR_KEY_alt_speed_time_end, alt_speed_time_end, size_t, 1020U) /* minutes past midnight; 5PM */ \
    V(TR_KEY_alt_speed_up, alt_speed_up_kilobytes_per_second, size_t, 50U) \
    V(TR_KEY_announce_ip, announce_ip, std::string, "") \
    V(TR_KEY_announce_ip_enabled, announce_ip_enabled, bool, false) \
    V(TR_KEY_anti_brute_force_enabled, anti_brute_force_enabled, bool, false) \
    V(TR_KEY_anti_brute_force_threshold, anti_brute_force_threshold, size_t, 100U) \
    V(TR_KEY_bind_address_ipv4, bind_address_ipv4, std::string, "0.0.0.0") \
    V(TR_KEY_bind_address_ipv6, bind_address_ipv6, std::string, "::") \
    V(TR_KEY_blocklist_enabled, blocklist_enabled, bool, false) \
    V(TR_KEY_blocklist_url, blocklist_url, std::string, "http://www.example.com/blocklist") \
    V(TR_KEY_cache_size_mb, cache_sizes_mb, size_t, 4U) \
    V(TR_KEY_default_trackers, default_trackers_str, std::string, "") \
    V(TR_KEY_download_dir, download_dir, std::string, tr_getDefaultDownloadDir()) \
    V(TR_KEY_download_queue_enabled, download_queue_enabled, bool, true) \
    V(TR_KEY_download_queue_size, download_queue_size, size_t, 5U) \
    V(TR_KEY_idle_seeding_limit, idle_seeding_limit, size_t, 30U) \
    V(TR_KEY_idle_seeding_limit_enabled, idle_seeding_limit_enabled, bool, false) \
    V(TR_KEY_incomplete_dir, incomplete_dir, std::string, tr_getDefaultDownloadDir()) \
    V(TR_KEY_incomplete_dir_enabled, incomplete_dir_enabled, bool, false) \
    V(TR_KEY_message_level, log_level, tr_log_level, TR_LOG_INFO) \
    V(TR_KEY_peer_congestion_algorithm, peer_congestion_algorithm, std::string, "") \
    V(TR_KEY_peer_id_ttl_hours, peer_id_ttl_hours, size_t, 6U) \
    V(TR_KEY_peer_limit_global, peer_limit_global, size_t, TR_DEFAULT_PEER_LIMIT_GLOBAL) \
    V(TR_KEY_peer_limit_per_torrent, peer_limit_per_torrent, size_t, TR_DEFAULT_PEER_LIMIT_TORRENT) \
    V(TR_KEY_peer_port, peer_port, tr_port, tr_port::fromHost(TR_DEFAULT_PEER_PORT)) \
    V(TR_KEY_peer_port_random_high, peer_port_random_high, tr_port, tr_port::fromHost(65535)) \
    V(TR_KEY_peer_port_random_low, peer_port_random_low, tr_port, tr_port::fromHost(49152)) \
    V(TR_KEY_peer_port_random_on_start, peer_port_random_on_start, bool, false) \
    V(TR_KEY_peer_socket_tos, peer_socket_tos, int, 0x04) \
    V(TR_KEY_port_forwarding_enabled, port_forwarding_enabled, bool, true) \
    V(TR_KEY_preallocation, preallocation_mode, tr_preallocation_mode, TR_PREALLOCATE_SPARSE) \
    V(TR_KEY_prefetch_enabled, prefetch_enabled, bool, true) \
    V(TR_KEY_queue_stalled_enabled, queue_stalled_enabled, bool, true) \
    V(TR_KEY_queue_stalled_minutes, queue_stalled_minutes, size_t, 30U) \
    V(TR_KEY_ratio_limit, ratio_limit, double, 2.0) \
    V(TR_KEY_ratio_limit_enabled, ratio_limit_enabled, bool, false) \
    V(TR_KEY_rename_partial_files, rename_partial_files, bool, false) \
    V(TR_KEY_rpc_authentication_required, rpc_authentication_required, bool, false) \
    V(TR_KEY_rpc_bind_address, rpc_bind_address, std::string, "0.0.0.0") \
    V(TR_KEY_rpc_enabled, rpc_enabled, bool, false) \
    V(TR_KEY_rpc_host_whitelist, rpc_host_whitelist, std::string, "") \
    V(TR_KEY_rpc_host_whitelist_enabled, rpc_host_whitelist_enabled, bool, true) \
    V(TR_KEY_rpc_password, rpc_password, std::string, "") \
    V(TR_KEY_rpc_port, rpc_port, tr_port, tr_port::fromHost(TR_DEFAULT_RPC_PORT)) \
    V(TR_KEY_rpc_socket_mode, rpc_socket_mode, mode_t, tr_rpc_server::DefaultRpcSocketMode) \
    V(TR_KEY_rpc_url, rpc_url, std::string, TR_DEFAULT_RPC_URL_STR) \
    V(TR_KEY_rpc_username, rpc_username, std::string, "") \
    V(TR_KEY_rpc_whitelist, rpc_whitelist, std::string, TR_DEFAULT_RPC_WHITELIST) \
    V(TR_KEY_rpc_whitelist_enabled, rpc_whitelist_enabled, bool, true) \
    V(TR_KEY_scrape_paused_torrents_enabled, scrape_paused_torrents_enabled, bool, true) \
    V(TR_KEY_script_torrent_added_enabled, script_torrent_added_enabled, bool, false) \
    V(TR_KEY_script_torrent_added_filename, script_torrent_added_filename, std::string, "") \
    V(TR_KEY_script_torrent_done_enabled, script_torrent_done_enabled, bool, false) \
    V(TR_KEY_script_torrent_done_filename, script_torrent_done_filename, std::string, "") \
    V(TR_KEY_script_torrent_done_seeding_enabled, script_torrent_done_seeding_enabled, bool, false) \
    V(TR_KEY_script_torrent_done_seeding_filename, script_torrent_done_seeding_filename, std::string, "") \
    V(TR_KEY_seed_queue_enabled, seed_queue_enabled, bool, false) \
    V(TR_KEY_seed_queue_size, seed_queue_size, size_t, 10U) \
    V(TR_KEY_speed_limit_down, speed_limit_down, size_t, 100U) \
    V(TR_KEY_speed_limit_down_enabled, speed_limit_down_enabled, bool, false) \
    V(TR_KEY_speed_limit_up, speed_limit_up, size_t, 100U) \
    V(TR_KEY_speed_limit_up_enabled, speed_limit_up_enabled, bool, false) \
    V(TR_KEY_start_added_torrents, start_added_torrents, bool, true) \
    V(TR_KEY_tcp_enabled, tcp_enabled, bool, true) \
    V(TR_KEY_trash_original_torrent_files, trash_original_torrent_files, bool, false) \
    V(TR_KEY_upload_slots_per_torrent, upload_slots_per_torrent, size_t, 8U)

struct tr_session_settings
{
    void load(tr_variant* src);
    void save(tr_variant* tgt) const;

#define V(key, name, type, default_value) type name = type{ default_value };
    SESSION_SETTINGS_FIELDS(V)
#undef V
};
