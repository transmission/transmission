// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <string_view>

#include <libtransmission/api-compat.h>
#include <libtransmission/quark.h>
#include <libtransmission/variant.h>

#include "gtest/gtest.h"
#include "test-fixtures.h"

namespace
{

constexpr std::string_view LegacySessionGetJson = R"json({
    "method": "session-get",
    "tag": 0
})json";

constexpr std::string_view CurrentSessionGetJson = R"json({
    "id": 0,
    "jsonrpc": "2.0",
    "method": "session_get"
})json";

constexpr std::string_view LegacySessionGetResponseJson = R"json({
    "arguments": {
        "alt-speed-down": 50,
        "alt-speed-enabled": false,
        "alt-speed-time-begin": 540,
        "alt-speed-time-day": 127,
        "alt-speed-time-enabled": false,
        "alt-speed-time-end": 1020,
        "alt-speed-up": 50,
        "anti-brute-force-enabled": false,
        "anti-brute-force-threshold": 100,
        "blocklist-enabled": false,
        "blocklist-size": 0,
        "blocklist-url": "http://www.example.com/blocklist",
        "cache-size-mb": 4,
        "config-dir": "/home/user/.config/transmission",
        "default-trackers": "",
        "dht-enabled": true,
        "download-dir": "/home/user/Downloads",
        "download-dir-free-space": 2199023255552,
        "download-queue-enabled": true,
        "download-queue-size": 4,
        "encryption": "preferred",
        "idle-seeding-limit": 30,
        "idle-seeding-limit-enabled": false,
        "incomplete-dir": "/home/user/Downloads",
        "incomplete-dir-enabled": false,
        "lpd-enabled": false,
        "peer-limit-global": 200,
        "peer-limit-per-torrent": 50,
        "peer-port": 51413,
        "peer-port-random-on-start": false,
        "pex-enabled": true,
        "port-forwarding-enabled": true,
        "preferred_transports": [
            "utp",
            "tcp"
        ],
        "queue-stalled-enabled": true,
        "queue-stalled-minutes": 30,
        "rename-partial-files": true,
        "reqq": 2000,
        "rpc-version": 18,
        "rpc-version-minimum": 14,
        "rpc-version-semver": "6.0.0",
        "script-torrent-added-enabled": false,
        "script-torrent-added-filename": "",
        "script-torrent-done-enabled": false,
        "script-torrent-done-filename": "/home/user/scripts/script.sh",
        "script-torrent-done-seeding-enabled": false,
        "script-torrent-done-seeding-filename": "",
        "seed-queue-enabled": false,
        "seed-queue-size": 10,
        "seedRatioLimit": 2.0,
        "seedRatioLimited": false,
        "sequential_download": false,
        "session-id": "pdvuklydohaohwwluzpmpmllkaopzzlzzpvupkpuavhjhlzhyjfwoly",
        "speed-limit-down": 100.0,
        "speed-limit-down-enabled": false,
        "speed-limit-up": 100.0,
        "speed-limit-up-enabled": false,
        "start-added-torrents": true,
        "tcp-enabled": true,
        "trash-original-torrent-files": false,
        "units": {
            "memory-bytes": 1024,
            "memory-units": [
                "B",
                "KiB",
                "MiB",
                "GiB",
                "TiB"
            ],
            "size-bytes": 1000,
            "size-units": [
                "B",
                "kB",
                "MB",
                "GB",
                "TB"
            ],
            "speed-bytes": 1000,
            "speed-units": [
                "B/s",
                "kB/s",
                "MB/s",
                "GB/s",
                "TB/s"
            ]
        },
        "utp-enabled": true,
        "version": "4.1.0-beta.4 (b11bfd9712)"
    },
    "result": "success",
    "tag": 40
})json";

constexpr std::string_view CurrentSessionGetResponseJson = R"json({
    "id": 40,
    "jsonrpc": "2.0",
    "result": {
        "alt_speed_down": 50,
        "alt_speed_enabled": false,
        "alt_speed_time_begin": 540,
        "alt_speed_time_day": 127,
        "alt_speed_time_enabled": false,
        "alt_speed_time_end": 1020,
        "alt_speed_up": 50,
        "anti_brute_force_enabled": false,
        "anti_brute_force_threshold": 100,
        "blocklist_enabled": false,
        "blocklist_size": 0,
        "blocklist_url": "http://www.example.com/blocklist",
        "cache_size_mb": 4,
        "config_dir": "/home/user/.config/transmission",
        "default_trackers": "",
        "dht_enabled": true,
        "download_dir": "/home/user/Downloads",
        "download_dir_free_space": 2199023255552,
        "download_queue_enabled": true,
        "download_queue_size": 4,
        "encryption": "preferred",
        "idle_seeding_limit": 30,
        "idle_seeding_limit_enabled": false,
        "incomplete_dir": "/home/user/Downloads",
        "incomplete_dir_enabled": false,
        "lpd_enabled": false,
        "peer_limit_global": 200,
        "peer_limit_per_torrent": 50,
        "peer_port": 51413,
        "peer_port_random_on_start": false,
        "pex_enabled": true,
        "port_forwarding_enabled": true,
        "preferred_transports": [
            "utp",
            "tcp"
        ],
        "queue_stalled_enabled": true,
        "queue_stalled_minutes": 30,
        "rename_partial_files": true,
        "reqq": 2000,
        "rpc_version": 18,
        "rpc_version_minimum": 14,
        "rpc_version_semver": "6.0.0",
        "script_torrent_added_enabled": false,
        "script_torrent_added_filename": "",
        "script_torrent_done_enabled": false,
        "script_torrent_done_filename": "/home/user/scripts/script.sh",
        "script_torrent_done_seeding_enabled": false,
        "script_torrent_done_seeding_filename": "",
        "seed_queue_enabled": false,
        "seed_queue_size": 10,
        "seed_ratio_limit": 2.0,
        "seed_ratio_limited": false,
        "sequential_download": false,
        "session_id": "pdvuklydohaohwwluzpmpmllkaopzzlzzpvupkpuavhjhlzhyjfwoly",
        "speed_limit_down": 100.0,
        "speed_limit_down_enabled": false,
        "speed_limit_up": 100.0,
        "speed_limit_up_enabled": false,
        "start_added_torrents": true,
        "tcp_enabled": true,
        "trash_original_torrent_files": false,
        "units": {
            "memory_bytes": 1024,
            "memory_units": [
                "B",
                "KiB",
                "MiB",
                "GiB",
                "TiB"
            ],
            "size_bytes": 1000,
            "size_units": [
                "B",
                "kB",
                "MB",
                "GB",
                "TB"
            ],
            "speed_bytes": 1000,
            "speed_units": [
                "B/s",
                "kB/s",
                "MB/s",
                "GB/s",
                "TB/s"
            ]
        },
        "utp_enabled": true,
        "version": "4.1.0-beta.4 (b11bfd9712)"
    }
})json";

constexpr std::string_view LegacyTorrentGetJson = R"json({
    "arguments": {
        "fields": [
            "downloadDir",
            "downloadedEver",
            "editDate",
            "error",
            "errorString",
            "eta",
            "haveUnchecked",
            "haveValid",
            "id",
            "isFinished",
            "leftUntilDone",
            "manualAnnounceTime",
            "metadataPercentComplete",
            "name",
            "peersConnected",
            "peersGettingFromUs",
            "peersSendingToUs",
            "percentDone",
            "queuePosition",
            "rateDownload",
            "rateUpload",
            "recheckProgress",
            "seedRatioLimit",
            "seedRatioMode",
            "sizeWhenDone",
            "status",
            "uploadRatio",
            "uploadedEver",
            "webseedsSendingToUs"
        ],
        "ids": "recently-active"
    },
    "method": "torrent-get",
    "tag": 6
})json";

constexpr std::string_view CurrentTorrentGetJson = R"json({
    "id": 6,
    "jsonrpc": "2.0",
    "method": "torrent_get",
    "params": {
        "fields": [
            "download_dir",
            "downloaded_ever",
            "edit_date",
            "error",
            "error_string",
            "eta",
            "have_unchecked",
            "have_valid",
            "id",
            "is_finished",
            "left_until_done",
            "manual_announce_time",
            "metadata_percent_complete",
            "name",
            "peers_connected",
            "peers_getting_from_us",
            "peers_sending_to_us",
            "percent_done",
            "queue_position",
            "rate_download",
            "rate_upload",
            "recheck_progress",
            "seed_ratio_limit",
            "seed_ratio_mode",
            "size_when_done",
            "status",
            "upload_ratio",
            "uploaded_ever",
            "webseeds_sending_to_us"
        ],
        "ids": "recently_active"
    }
})json";

constexpr std::string_view CurrentPortTestErrorResponse = R"json({
    "error": {
        "code": 8,
        "data": {
            "error_string": "Couldn't test port: No Response (0)",
            "result": {
                "ip_protocol": "ipv6"
            }
        },
        "message": "HTTP error from backend service"
    },
    "id": 9,
    "jsonrpc": "2.0"
})json";

constexpr std::string_view LegacyPortTestErrorResponse = R"json({
    "arguments": {
        "ip_protocol": "ipv6"
    },
    "result": "Couldn't test port: No Response (0)",
    "tag": 9
})json";

constexpr std::string_view LegacyStatsJson = R"json({
    "downloaded-bytes": 12,
    "files-added": 34,
    "seconds-active": 56,
    "session-count": 78,
    "uploaded-bytes": 90
})json";

constexpr std::string_view CurrentStatsJson = R"json({
    "downloaded_bytes": 12,
    "files_added": 34,
    "seconds_active": 56,
    "session_count": 78,
    "uploaded_bytes": 90
})json";

constexpr std::string_view LegacySettingsJson = R"json({
    "alt-speed-down": 50,
    "alt-speed-enabled": false,
    "alt-speed-time-begin": 540,
    "alt-speed-time-day": 127,
    "alt-speed-time-enabled": false,
    "alt-speed-time-end": 1020,
    "alt-speed-up": 50,
    "blocklist-date": 0,
    "blocklist-enabled": false,
    "blocklist-updates-enabled": true,
    "blocklist-url": "http://www.example.com/blocklist",
    "compact-view": false,
    "default-trackers": "",
    "dht-enabled": true,
    "download-dir": "/home/user/Downloads",
    "download-queue-enabled": true,
    "download-queue-size": 5,
    "encryption": 1,
    "filter-mode": "show-all",
    "filter-trackers": "",
    "idle-seeding-limit": 30,
    "idle-seeding-limit-enabled": false,
    "incomplete-dir": "/home/user/Downloads",
    "incomplete-dir-enabled": false,
    "inhibit-desktop-hibernation": false,
    "lpd-enabled": true,
    "main-window-height": 500,
    "main-window-layout-order": "menu,toolbar,filter,list,statusbar",
    "main-window-width": 650,
    "main-window-x": 3840,
    "main-window-y": 0,
    "message-level": 4,
    "open-dialog-dir": "/home/user",
    "peer-limit-global": 200,
    "peer-limit-per-torrent": 50,
    "peer-port": 51413,
    "peer-port-random-high": 65535,
    "peer-port-random-low": 49152,
    "peer-port-random-on-start": false,
    "peer-socket-tos": "le",
    "pex-enabled": true,
    "port-forwarding-enabled": true,
    "preallocation": 1,
    "prompt-before-exit": true,
    "queue-stalled-minutes": 30,
    "ratio-limit": 2.0,
    "ratio-limit-enabled": false,
    "read-clipboard": false,
    "remote-session-enabled": false,
    "remote-session-host": "localhost",
    "remote-session-https": false,
    "remote-session-password": "",
    "remote-session-port": 9091,
    "remote-session-requres-authentication": false,
    "remote-session-username": "",
    "rename-partial-files": true,
    "rpc-authentication-required": false,
    "rpc-enabled": false,
    "rpc-password": "",
    "rpc-port": 9091,
    "rpc-username": "",
    "rpc-whitelist": "127.0.0.1,::1",
    "rpc-whitelist-enabled": true,
    "script-torrent-done-enabled": false,
    "script-torrent-done-filename": "",
    "script-torrent-done-seeding-enabled": false,
    "script-torrent-done-seeding-filename": "",
    "show-backup-trackers": false,
    "show-filterbar": true,
    "show-notification-area-icon": false,
    "show-options-window": true,
    "show-statusbar": true,
    "show-toolbar": true,
    "show-tracker-scrapes": false,
    "sleep-per-seconds-during-verify": 100,
    "sort-mode": "sort-by-name",
    "sort-reversed": false,
    "speed-limit-down": 100,
    "speed-limit-down-enabled": false,
    "speed-limit-up": 100,
    "speed-limit-up-enabled": false,
    "start-added-torrents": true,
    "start-minimized": false,
    "statusbar-stats": "total-ratio",
    "torrent-added-notification-enabled": true,
    "torrent-complete-notification-enabled": true,
    "torrent-complete-sound-command": [
        "canberra-gtk-play",
        "-i",
        "complete-download",
        "-d",
        "transmission torrent downloaded"
    ],
    "torrent-complete-sound-enabled": true,
    "trash-original-torrent-files": false,
    "upload-slots-per-torrent": 8,
    "utp-enabled": true,
    "watch-dir": "/home/user/Downloads",
    "watch-dir-enabled": false
})json";

constexpr std::string_view CurrentSettingsJson = R"json({
    "alt_speed_down": 50,
    "alt_speed_enabled": false,
    "alt_speed_time_begin": 540,
    "alt_speed_time_day": 127,
    "alt_speed_time_enabled": false,
    "alt_speed_time_end": 1020,
    "alt_speed_up": 50,
    "blocklist_date": 0,
    "blocklist_enabled": false,
    "blocklist_updates_enabled": true,
    "blocklist_url": "http://www.example.com/blocklist",
    "compact_view": false,
    "default_trackers": "",
    "dht_enabled": true,
    "download_dir": "/home/user/Downloads",
    "download_queue_enabled": true,
    "download_queue_size": 5,
    "encryption": 1,
    "filter_mode": "show-all",
    "filter_trackers": "",
    "idle_seeding_limit": 30,
    "idle_seeding_limit_enabled": false,
    "incomplete_dir": "/home/user/Downloads",
    "incomplete_dir_enabled": false,
    "inhibit_desktop_hibernation": false,
    "lpd_enabled": true,
    "main_window_height": 500,
    "main_window_layout_order": "menu,toolbar,filter,list,statusbar",
    "main_window_width": 650,
    "main_window_x": 3840,
    "main_window_y": 0,
    "message_level": 4,
    "open_dialog_dir": "/home/user",
    "peer_limit_global": 200,
    "peer_limit_per_torrent": 50,
    "peer_port": 51413,
    "peer_port_random_high": 65535,
    "peer_port_random_low": 49152,
    "peer_port_random_on_start": false,
    "peer_socket_tos": "le",
    "pex_enabled": true,
    "port_forwarding_enabled": true,
    "preallocation": 1,
    "prompt_before_exit": true,
    "queue_stalled_minutes": 30,
    "ratio_limit": 2.0,
    "ratio_limit_enabled": false,
    "read_clipboard": false,
    "remote_session_enabled": false,
    "remote_session_host": "localhost",
    "remote_session_https": false,
    "remote_session_password": "",
    "remote_session_port": 9091,
    "remote_session_requires_authentication": false,
    "remote_session_username": "",
    "rename_partial_files": true,
    "rpc_authentication_required": false,
    "rpc_enabled": false,
    "rpc_password": "",
    "rpc_port": 9091,
    "rpc_username": "",
    "rpc_whitelist": "127.0.0.1,::1",
    "rpc_whitelist_enabled": true,
    "script_torrent_done_enabled": false,
    "script_torrent_done_filename": "",
    "script_torrent_done_seeding_enabled": false,
    "script_torrent_done_seeding_filename": "",
    "show_backup_trackers": false,
    "show_filterbar": true,
    "show_notification_area_icon": false,
    "show_options_window": true,
    "show_statusbar": true,
    "show_toolbar": true,
    "show_tracker_scrapes": false,
    "sleep_per_seconds_during_verify": 100,
    "sort_mode": "sort-by-name",
    "sort_reversed": false,
    "speed_limit_down": 100,
    "speed_limit_down_enabled": false,
    "speed_limit_up": 100,
    "speed_limit_up_enabled": false,
    "start_added_torrents": true,
    "start_minimized": false,
    "statusbar_stats": "total-ratio",
    "torrent_added_notification_enabled": true,
    "torrent_complete_notification_enabled": true,
    "torrent_complete_sound_command": [
        "canberra-gtk-play",
        "-i",
        "complete-download",
        "-d",
        "transmission torrent downloaded"
    ],
    "torrent_complete_sound_enabled": true,
    "trash_original_torrent_files": false,
    "upload_slots_per_torrent": 8,
    "utp_enabled": true,
    "watch_dir": "/home/user/Downloads",
    "watch_dir_enabled": false
})json";

constexpr std::string_view BadFreeSpaceRequest = R"json({
    "id": 39693,
    "jsonrpc": "2.0",
    "method": "free_space",
    "params": {
        "path": "this/path/is/not/absolute"
    }
})json";

constexpr std::string_view BadFreeSpaceRequestLegacy = R"json({
    "arguments": {
        "path": "this/path/is/not/absolute"
    },
    "method": "free-space",
    "tag": 39693
})json";

constexpr std::string_view BadFreeSpaceResponse = R"json({
    "error": {
        "code": 3,
        "data": {
            "error_string": "directory path is not absolute"
        },
        "message": "path is not absolute"
    },
    "id": 39693,
    "jsonrpc": "2.0"
})json";

constexpr std::string_view BadFreeSpaceResponseLegacy = R"json({
    "arguments": {},
    "result": "directory path is not absolute",
    "tag": 39693
})json";

constexpr std::string_view WellFormedFreeSpaceRequest = R"json({
    "id": 41414,
    "jsonrpc": "2.0",
    "method": "free_space",
    "params": {
        "path": "/this/path/does/not/exist"
    }
})json";

constexpr std::string_view WellFormedFreeSpaceResponse = R"json({
    "id": 41414,
    "jsonrpc": "2.0",
    "result": {
        "path": "/this/path/does/not/exist",
        "size_bytes": -1,
        "total_size": -1
    }
})json";

constexpr std::string_view WellFormedFreeSpaceLegacyRequest = R"json({
    "arguments": {
        "path": "/this/path/does/not/exist"
    },
    "method": "free-space",
    "tag": 41414
})json";

constexpr std::string_view WellFormedFreeSpaceLegacyResponse = R"json({
    "arguments": {
        "path": "/this/path/does/not/exist",
        "size-bytes": -1,
        "total_size": -1
    },
    "result": "success",
    "tag": 41414
})json";

constexpr std::string_view BadMethodNameResponse = R"json({
    "error": {
        "code": -32601,
        "message": "Method not found"
    },
    "id": 39693,
    "jsonrpc": "2.0"
})json";

constexpr std::string_view BadMethodNameLegacyResponse = R"json({
    "arguments": {},
    "result": "no method name",
    "tag": 39693
})json";

} // namespace

TEST(ApiCompatTest, canConvertRpc)
{
    using Style = libtransmission::api_compat::Style;
    using TestCase = std::tuple<std::string_view, std::string_view, Style, std::string_view>;

    // clang-format off
    static auto constexpr TestCases = std::array<TestCase, 36U>{ {
        { "free_space tr5 -> tr5", BadFreeSpaceRequest, Style::Tr5, BadFreeSpaceRequest },
        { "free_space tr5 -> tr4", BadFreeSpaceRequest, Style::Tr4, BadFreeSpaceRequestLegacy },
        { "free_space tr4 -> tr5", BadFreeSpaceRequestLegacy, Style::Tr5, BadFreeSpaceRequest },
        { "free_space tr4 -> tr4", BadFreeSpaceRequestLegacy, Style::Tr4, BadFreeSpaceRequestLegacy },
        { "free_space error response tr5 -> tr5", BadFreeSpaceResponse, Style::Tr5, BadFreeSpaceResponse },
        { "free_space error response tr5 -> tr4", BadFreeSpaceResponse, Style::Tr4, BadFreeSpaceResponseLegacy },
        { "free_space error response tr4 -> tr5", BadFreeSpaceResponseLegacy, Style::Tr5, BadFreeSpaceResponse },
        { "free_space error response tr4 -> tr4", BadFreeSpaceResponseLegacy, Style::Tr4, BadFreeSpaceResponseLegacy },
        { "free_space req tr5 -> tr5", WellFormedFreeSpaceRequest, Style::Tr5, WellFormedFreeSpaceRequest },
        { "free_space req tr5 -> tr4", WellFormedFreeSpaceRequest, Style::Tr4, WellFormedFreeSpaceLegacyRequest },
        { "free_space req tr4 -> tr5", WellFormedFreeSpaceLegacyRequest, Style::Tr5, WellFormedFreeSpaceRequest },
        { "free_space req tr4 -> tr4", WellFormedFreeSpaceLegacyRequest, Style::Tr4, WellFormedFreeSpaceLegacyRequest },
        { "free_space response tr5 -> tr5", WellFormedFreeSpaceResponse, Style::Tr5, WellFormedFreeSpaceResponse },
        { "free_space response tr5 -> tr4", WellFormedFreeSpaceResponse, Style::Tr4, WellFormedFreeSpaceLegacyResponse },
        { "free_space response tr4 -> tr5", WellFormedFreeSpaceLegacyResponse, Style::Tr5, WellFormedFreeSpaceResponse },
        { "free_space response tr4 -> tr4", WellFormedFreeSpaceLegacyResponse, Style::Tr4, WellFormedFreeSpaceLegacyResponse },
        { "session_get tr5 -> tr5", CurrentSessionGetJson, Style::Tr5, CurrentSessionGetJson },
        { "session_get tr5 -> tr4", CurrentSessionGetJson, Style::Tr4, LegacySessionGetJson },
        { "session_get tr4 -> tr5", LegacySessionGetJson, Style::Tr5, CurrentSessionGetJson },
        { "session_get tr4 -> tr4", LegacySessionGetJson, Style::Tr4, LegacySessionGetJson },
        { "session_get response tr5 -> tr5", CurrentSessionGetResponseJson, Style::Tr5, CurrentSessionGetResponseJson },
        { "session_get response tr5 -> tr4", CurrentSessionGetResponseJson, Style::Tr4, LegacySessionGetResponseJson },
        { "session_get response tr4 -> tr5", LegacySessionGetResponseJson, Style::Tr5, CurrentSessionGetResponseJson },
        { "session_get response tr4 -> tr4", LegacySessionGetResponseJson, Style::Tr4, LegacySessionGetResponseJson },
        { "torrent_get tr5 -> tr5", CurrentTorrentGetJson, Style::Tr5, CurrentTorrentGetJson },
        { "torrent_get tr5 -> tr4", CurrentTorrentGetJson, Style::Tr4, LegacyTorrentGetJson },
        { "torrent_get tr4 -> tr5", LegacyTorrentGetJson, Style::Tr5, CurrentTorrentGetJson },
        { "torrent_get tr4 -> tr4", LegacyTorrentGetJson, Style::Tr4, LegacyTorrentGetJson },
        { "port_test error response tr5 -> tr5", CurrentPortTestErrorResponse, Style::Tr5, CurrentPortTestErrorResponse },
        { "port_test error response tr5 -> tr4", CurrentPortTestErrorResponse, Style::Tr4, LegacyPortTestErrorResponse },
        { "port_test error response tr4 -> tr5", LegacyPortTestErrorResponse, Style::Tr5, CurrentPortTestErrorResponse },
        { "port_test error response tr4 -> tr4", LegacyPortTestErrorResponse, Style::Tr4, LegacyPortTestErrorResponse },
        { "bad method name tr5 -> tr5", BadMethodNameResponse, Style::Tr5, BadMethodNameResponse },
        { "bad method name tr5 -> tr4", BadMethodNameResponse, Style::Tr4, BadMethodNameLegacyResponse },
        { "bad method name tr4 -> tr5", BadMethodNameLegacyResponse, Style::Tr5, BadMethodNameResponse },
        { "bad method name tr4 -> tr4", BadMethodNameLegacyResponse, Style::Tr4, BadMethodNameLegacyResponse },

        // TODO(ckerr): torrent-get with 'table'
    } };
    // clang-format on

    for (auto const& [name, src, tgt_style, expected] : TestCases)
    {
        auto serde = tr_variant_serde::json();
        auto parsed = serde.parse(src);
        ASSERT_TRUE(parsed.has_value()) << name << ": " << serde.error_;
        auto converted = libtransmission::api_compat::convert(*parsed, tgt_style);
        EXPECT_EQ(expected, serde.to_string(converted)) << name;
    }
}

TEST(ApiCompatTest, canConvertDataFiles)
{
    using Style = libtransmission::api_compat::Style;
    using TestCase = std::tuple<std::string_view, std::string_view, Style, std::string_view>;

    // clang-format off
    static auto constexpr TestCases = std::array<TestCase, 8U>{ {
        { "settings tr5 -> tr5", CurrentSettingsJson, Style::Tr5, CurrentSettingsJson },
        { "settings tr5 -> tr4", CurrentSettingsJson, Style::Tr4, LegacySettingsJson },
        { "settings tr4 -> tr5", LegacySettingsJson, Style::Tr5, CurrentSettingsJson },
        { "settings tr4 -> tr4", LegacySettingsJson, Style::Tr4, LegacySettingsJson },

        { "stats tr5 -> tr5", CurrentStatsJson, Style::Tr5, CurrentStatsJson },
        { "stats tr5 -> tr4", CurrentStatsJson, Style::Tr4, LegacyStatsJson },
        { "stats tr4 -> tr5", LegacyStatsJson, Style::Tr5, CurrentStatsJson },
        { "stats tr4 -> tr4", LegacyStatsJson, Style::Tr4, LegacyStatsJson },
    } };
    // clang-format on

    for (auto const& [name, src, tgt_style, expected] : TestCases)
    {
        auto serde = tr_variant_serde::json();
        auto parsed = serde.parse(src);
        ASSERT_TRUE(parsed.has_value());
        auto converted = libtransmission::api_compat::convert(*parsed, tgt_style);
        EXPECT_EQ(expected, serde.to_string(converted)) << name;
    }
}
