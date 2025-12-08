// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <string_view>

#include <libtransmission/quark.h>
#include <libtransmission/variant.h>

#include "gtest/gtest.h"

namespace
{

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
    "main-window-width": 766,
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
    "remote_session_rpc_url_path": "/transmission/rpc",
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
    "user-has-given-informed-consent": true,
    "utp-enabled": true,
    "watch-dir": "/home/user/Downloads",
    "watch-dir-enabled": false
})json";

constexpr std::string_view LegacyStatsJson = R"json({
    "downloaded-bytes": 314159265358,
    "files-added": 271828,
    "seconds-active": 1618033988,
    "session-count": 141421,
    "uploaded-bytes": 299792458
})json";

constexpr std::string_view LegacySessionGetRpcJson = R"json({
    "method": "session-get",
    "tag": 0
})json";

constexpr std::string_view CurrentSessionGetRpcJson = R"json({
    "id": 0,
    "jsonrpc": "2.0",
    "method": "session_get"
})json";

constexpr std::string_view LegacySessionGetResponseJson = R"json({
    "arguments": {
        "alt-speed-down": 1000,
        "alt-speed-enabled": false,
        "alt-speed-time-begin": 540,
        "alt-speed-time-day": 127,
        "alt-speed-time-enabled": false,
        "alt-speed-time-end": 1020,
        "alt-speed-up": 1000,
        "anti-brute-force-enabled": true,
        "anti-brute-force-threshold": 100,
        "blocklist-enabled": false,
        "blocklist-size": 0,
        "blocklist-url": "http://www.example.com/blocklist",
        "cache-size-mb": 4,
        "config-dir": "/home/user/.config/transmission",
        "default-trackers": "",
        "dht-enabled": true,
        "download-dir": "/home/user/Downloads",
        "download-dir-free-space": 1395465670656,
        "download-queue-enabled": true,
        "download-queue-size": 4,
        "encryption": "preferred",
        "idle-seeding-limit": 30,
        "idle-seeding-limit-enabled": false,
        "incomplete-dir": "/home/user/Downloads",
        "incomplete-dir-enabled": false,
        "lpd-enabled": false,
        "peer-limit-global": 200,
        "peer-limit-per-torrent": 20,
        "peer-port": 55606,
        "peer-port-random-on-start": false,
        "pex-enabled": true,
        "port-forwarding-enabled": true,
        "queue-stalled-enabled": true,
        "queue-stalled-minutes": 5,
        "rename-partial-files": true,
        "reqq": 2000,
        "rpc-version": 18,
        "rpc-version-minimum": 14,
        "rpc-version-semver": "5.4.0",
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
        "speed-limit-down": 2000.0,
        "speed-limit-down-enabled": false,
        "speed-limit-up": 5000.0,
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
        "version": "2.93 (3c5870d4f5)"
    },
    "result": "success",
    "tag": 0
})json";

constexpr std::string_view CurrentSessionGetResponseJson = R"json({
})json";

constexpr std::string_view LegacyTorrentGetRpcJson = R"json({
    "arguments": {
        "fields": [
            "error",
            "errorString",
            "eta",
            "id",
            "isFinished",
            "leftUntilDone",
            "name",
            "peersGettingFromUs",
            "peersSendingToUs",
            "rateDownload",
            "rateUpload",
            "sizeWhenDone",
            "status",
            "uploadRatio"
        ]
    },
    "method": "torrent-get",
    "tag": 6
})json";

constexpr std::string_view CurrentTorrentGetRpcJson = R"json({
    "id": 6,
    "jsonrpc": "2.0",
    "method": "torrent_get",
    "params": {
        "fields": [
            "error",
            "error_string",
            "eta",
            "id",
            "is_finished",
            "left_until_done",
            "name",
            "peers_getting_from_us",
            "peers_sending_to_us",
            "rate_download",
            "rate_upload",
            "size_when_done",
            "status",
            "upload_ratio"
        ]
    }
})json";

constexpr std::string_view LegacySettingsShort = R"json({
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
    "compact-view": false
})json";

constexpr std::string_view CurrentSettingsShort = R"json({
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
    "compact_view": false
})json";
} // namespace

TEST(ApiCompatTest, convert)
{
    using namespace libtransmission::api_compat;

    EXPECT_EQ(TR_KEY_activity_date, convert(TR_KEY_activity_date, Style::Current));
    EXPECT_EQ(TR_KEY_activity_date_camel, convert(TR_KEY_activity_date, Style::LegacyRpc));
    EXPECT_EQ(TR_KEY_activity_date_kebab, convert(TR_KEY_activity_date, Style::LegacySettings));
}

TEST(ApiCompatTest, canConvertRpc)
{
    using Style = libtransmission::api_compat::Style;
    using TestCase = std::tuple<std::string_view, std::string_view, Style, std::string_view>;
    static auto constexpr TestCases = std::array<TestCase, 8U>{ {
        { "torrent-get current -> current", CurrentTorrentGetRpcJson, Style::Current, CurrentTorrentGetRpcJson },
        { "torrent-get current -> legacy", CurrentTorrentGetRpcJson, Style::LegacyRpc, LegacyTorrentGetRpcJson },
        { "torrent-get legacy -> current", LegacyTorrentGetRpcJson, Style::Current, CurrentTorrentGetRpcJson },
        { "torrent-get legacy -> legacy", LegacyTorrentGetRpcJson, Style::LegacyRpc, LegacyTorrentGetRpcJson },

        { "session-get legacy -> current", LegacySessionGetRpcJson, Style::Current, CurrentSessionGetRpcJson },
        { "session-get current -> legacy", CurrentSessionGetRpcJson, Style::LegacyRpc, LegacySessionGetRpcJson },
        { "session-get legacy -> legacy", LegacySessionGetRpcJson, Style::LegacyRpc, LegacySessionGetRpcJson },
        { "session-get current -> current", CurrentSessionGetRpcJson, Style::Current, CurrentSessionGetRpcJson },

        //constexpr std::string_view LegacySessionGetResponseJson = R"json({
    } };

    for (auto [name, src, tgt_style, expected] : TestCases)
    {
        auto serde = tr_variant_serde::json();
        auto parsed = serde.parse(src);
        ASSERT_TRUE(parsed.has_value());
        auto converted = libtransmission::api_compat::apply_style(*parsed, tgt_style);
        EXPECT_EQ(expected, serde.to_string(converted)) << name;
    }
}

TEST(ApiCompatTest, canApplyCurrentStyleFromLegacySettings)
{
    auto serde = tr_variant_serde::json();
    auto const parsed = serde.parse(LegacySettingsShort);
    ASSERT_TRUE(parsed.has_value());

    auto converted = libtransmission::api_compat::apply_style(*parsed, libtransmission::api_compat::Style::Current);
    auto converted_json = tr_variant_serde::json().to_string(converted);
    EXPECT_EQ(CurrentSettingsShort, converted_json);

#if 0
    converted = libtransmission::api_compat::apply_style(*parsed, libtransmission::api_compat::Style::LegacySettings);
    converted_json = tr_variant_serde::json().to_string(converted);
    EXPECT_EQ(LegacySettingsShort, converted_json);
#endif
}
