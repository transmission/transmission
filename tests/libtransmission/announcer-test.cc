// This file Copyright (C) 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <string_view>

#define LIBTRANSMISSION_ANNOUNCER_MODULE

#include "transmission.h"

#include "announcer-common.h"
#include "crypto-utils.h"
#include "net.h"

#include "test-fixtures.h"

using AnnouncerTest = ::testing::Test;

using namespace std::literals;

TEST_F(AnnouncerTest, parseHttpAnnounceResponseNoPeers)
{
    // clang-format off
    auto constexpr NoPeers =
        "d"
            "8:complete" "i3e"
            "10:downloaded" "i2e"
            "10:incomplete" "i0e"
            "8:interval" "i1803e"
            "12:min interval" "i1800e"
            "5:peers" "0:"
        "e"sv;
    // clang-format on

    auto response = tr_announce_response{};
    tr_announcerParseHttpAnnounceResponse(response, NoPeers);
    EXPECT_EQ(1803, response.interval);
    EXPECT_EQ(1800, response.min_interval);
    EXPECT_EQ(3, response.seeders);
    EXPECT_EQ(0, response.leechers);
    EXPECT_EQ(2, response.downloads);
    EXPECT_EQ(0, std::size(response.pex));
    EXPECT_EQ(0, std::size(response.pex6));
    EXPECT_EQ(""sv, response.errmsg);
    EXPECT_EQ(""sv, response.warning);
}

TEST_F(AnnouncerTest, parseHttpAnnounceResponsePexCompact)
{
    // clang-format off
    auto constexpr IPv4Peers =
        "d"
            "8:complete" "i3e"
            "10:downloaded" "i2e"
            "10:incomplete" "i0e"
            "8:interval" "i1803e"
            "12:min interval" "i1800e"
            "5:peers"
            "6:\x7F\x00\x00\x01\xfc\x27"
        "e"sv;
    // clang-format on

    auto response = tr_announce_response{};
    tr_announcerParseHttpAnnounceResponse(response, IPv4Peers);
    EXPECT_EQ(1803, response.interval);
    EXPECT_EQ(1800, response.min_interval);
    EXPECT_EQ(3, response.seeders);
    EXPECT_EQ(0, response.leechers);
    EXPECT_EQ(2, response.downloads);
    EXPECT_EQ(""sv, response.errmsg);
    EXPECT_EQ(""sv, response.warning);
    EXPECT_EQ(1, std::size(response.pex));
    EXPECT_EQ(0, std::size(response.pex6));

    if (std::size(response.pex) == 1)
    {
        EXPECT_EQ("[127.0.0.1]:64551"sv, response.pex[0].to_string());
    }
}

TEST_F(AnnouncerTest, parseHttpAnnounceResponsePexList)
{
    // clang-format off
    auto constexpr IPv4Peers =
        "d"
            "8:complete" "i3e"
            "10:downloaded" "i2e"
            "10:incomplete" "i0e"
            "8:interval" "i1803e"
            "12:min interval" "i1800e"
            "5:peers"
            "l"
                "d"
                    "7:peer id" "20:-TR300Z-0123456789AB"
                    "2:ip" "7:8.8.4.4"
                    "4:port" "i53e"
                "e"
            "e"
        "e"sv;
    // clang-format on

    auto response = tr_announce_response{};
    tr_announcerParseHttpAnnounceResponse(response, IPv4Peers);
    EXPECT_EQ(1803, response.interval);
    EXPECT_EQ(1800, response.min_interval);
    EXPECT_EQ(3, response.seeders);
    EXPECT_EQ(0, response.leechers);
    EXPECT_EQ(2, response.downloads);
    EXPECT_EQ(""sv, response.errmsg);
    EXPECT_EQ(""sv, response.warning);
    EXPECT_EQ(1, std::size(response.pex));
    EXPECT_EQ(0, std::size(response.pex6));

    if (std::size(response.pex) == 1)
    {
        EXPECT_EQ("[8.8.4.4]:53"sv, response.pex[0].to_string());
    }
}

TEST_F(AnnouncerTest, parseHttpAnnounceResponseFailureReason)
{
    // clang-format off
    auto constexpr NoPeers =
        "d"
            "8:complete" "i3e"
            "14:failure reason" "6:foobar"
            "10:downloaded" "i2e"
            "10:incomplete" "i0e"
            "8:interval" "i1803e"
            "12:min interval" "i1800e"
            "5:peers" "0:"
        "e"sv;
    // clang-format on

    auto response = tr_announce_response{};
    tr_announcerParseHttpAnnounceResponse(response, NoPeers);
    EXPECT_EQ(1803, response.interval);
    EXPECT_EQ(1800, response.min_interval);
    EXPECT_EQ(3, response.seeders);
    EXPECT_EQ(0, response.leechers);
    EXPECT_EQ(2, response.downloads);
    EXPECT_EQ(0, std::size(response.pex));
    EXPECT_EQ(0, std::size(response.pex6));
    EXPECT_EQ("foobar"sv, response.errmsg);
    EXPECT_EQ(""sv, response.warning);
}
