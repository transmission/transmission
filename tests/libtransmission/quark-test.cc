// This file Copyright (C) 2013-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <cassert>
#include <cstddef> // size_t
#include <string>
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

constexpr std::string_view LegacyRpcJson = R"json({
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

} // namespace

class QuarkTest : public ::testing::Test
{
protected:
    template<typename T>
    std::string quarkGetString(T i)
    {
        return std::string{ tr_quark_get_string_view(tr_quark{ i }) };
    }
};

TEST_F(QuarkTest, allPredefinedKeysCanBeLookedUp)
{
    for (size_t i = 0; i < TR_N_KEYS; ++i)
    {
        auto const str = quarkGetString(i);
        auto const q = tr_quark_lookup(str);
        ASSERT_TRUE(q.has_value());
        assert(q.has_value());
        EXPECT_EQ(i, *q);
    }
}

TEST_F(QuarkTest, newQuarkByStringView)
{
    auto constexpr UniqueString = std::string_view{ "this string is not a predefined quark" };
    auto const q = tr_quark_new(UniqueString);
    EXPECT_EQ(UniqueString, tr_quark_get_string_view(q));
}

TEST_F(QuarkTest, canDetectLegacySettings)
{
    auto serde = tr_variant_serde::json();
    auto variant = serde.parse(LegacySettingsJson);
    ASSERT_TRUE(variant);

    auto const style = libtransmission::api_compat::detect_style(*variant);
    ASSERT_TRUE(style);
    EXPECT_EQ(libtransmission::api_compat::Style::LegacySettings, *style);
}

TEST_F(QuarkTest, canDetectLegacyRpc)
{
    auto serde = tr_variant_serde::json();
    auto variant = serde.parse(LegacyRpcJson);
    ASSERT_TRUE(variant);

    auto const style = libtransmission::api_compat::detect_style(*variant);
    ASSERT_TRUE(style);
    EXPECT_EQ(libtransmission::api_compat::Style::LegacyRpc, *style);
}

TEST_F(QuarkTest, canDetectLegacyStats)
{
    auto serde = tr_variant_serde::json();
    auto variant = serde.parse(LegacyStatsJson);
    ASSERT_TRUE(variant);

    auto const style = libtransmission::api_compat::detect_style(*variant);
    ASSERT_TRUE(style);
    EXPECT_EQ(libtransmission::api_compat::Style::LegacySettings, *style);
}
