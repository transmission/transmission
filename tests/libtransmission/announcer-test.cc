// This file Copyright (C) 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <string_view>

#define LIBTRANSMISSION_ANNOUNCER_MODULE

#include <libtransmission/transmission.h>

#include <libtransmission/announcer-common.h>
#include <libtransmission/net.h>

#include "test-fixtures.h"

using AnnouncerTest = ::testing::Test;

using namespace std::literals;

static char const* const LogName = "LogName";

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
            "11:external ip" "4:\x01\x02\x03\x04"
        "e"sv;
    // clang-format on

    auto response = tr_announce_response{};
    tr_announcerParseHttpAnnounceResponse(response, NoPeers, LogName);
    EXPECT_EQ(1803, response.interval);
    EXPECT_EQ(1800, response.min_interval);
    EXPECT_EQ(3, response.seeders);
    EXPECT_EQ(0, response.leechers);
    EXPECT_EQ(2, response.downloads);
    auto addr = tr_address::from_string("1.2.3.4");
    EXPECT_TRUE(addr.has_value());
    assert(addr.has_value());
    EXPECT_EQ(*addr, response.external_ip);
    EXPECT_EQ(0U, std::size(response.pex));
    EXPECT_EQ(0U, std::size(response.pex6));
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
    tr_announcerParseHttpAnnounceResponse(response, IPv4Peers, LogName);
    EXPECT_EQ(1803, response.interval);
    EXPECT_EQ(1800, response.min_interval);
    EXPECT_EQ(3, response.seeders);
    EXPECT_EQ(0, response.leechers);
    EXPECT_EQ(2, response.downloads);
    EXPECT_EQ(""sv, response.errmsg);
    EXPECT_EQ(""sv, response.warning);
    EXPECT_EQ(1U, std::size(response.pex));
    EXPECT_EQ(0U, std::size(response.pex6));

    if (std::size(response.pex) == 1)
    {
        EXPECT_EQ("[127.0.0.1]:64551"sv, response.pex[0].display_name());
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
    tr_announcerParseHttpAnnounceResponse(response, IPv4Peers, LogName);
    EXPECT_EQ(1803, response.interval);
    EXPECT_EQ(1800, response.min_interval);
    EXPECT_EQ(3, response.seeders);
    EXPECT_EQ(0, response.leechers);
    EXPECT_EQ(2, response.downloads);
    EXPECT_EQ(""sv, response.errmsg);
    EXPECT_EQ(""sv, response.warning);
    EXPECT_EQ(1U, std::size(response.pex));
    EXPECT_EQ(0U, std::size(response.pex6));

    if (std::size(response.pex) == 1)
    {
        EXPECT_EQ("[8.8.4.4]:53"sv, response.pex[0].display_name());
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
    tr_announcerParseHttpAnnounceResponse(response, NoPeers, LogName);
    EXPECT_EQ(1803, response.interval);
    EXPECT_EQ(1800, response.min_interval);
    EXPECT_EQ(3, response.seeders);
    EXPECT_EQ(0, response.leechers);
    EXPECT_EQ(2, response.downloads);
    EXPECT_EQ(0U, std::size(response.pex));
    EXPECT_EQ(0U, std::size(response.pex6));
    EXPECT_EQ("foobar"sv, response.errmsg);
    EXPECT_EQ(""sv, response.warning);
}

TEST_F(AnnouncerTest, parseHttpScrapeResponseMulti)
{
    // clang-format off
    auto constexpr ResponseBenc =
        "d"
            "5:files"
            "d"
                "20:aaaaaaaaaaaaaaaaaaaa"
                "d"
                    "8:complete" "i1e"
                    "10:incomplete" "i2e"
                    "10:downloaded" "i3e"
                "e"

                "20:bbbbbbbbbbbbbbbbbbbb"
                "d"
                    "8:complete" "i4e"
                    "10:incomplete" "i5e"
                    "10:downloaded" "i6e"
                "e"

                "20:cccccccccccccccccccc"
                "d"
                    "8:complete" "i7e"
                    "10:incomplete" "i8e"
                    "10:downloaded" "i9e"
                "e"
            "e"
        "e"sv;
    // clang-format on

    auto response = tr_scrape_response{};
    std::fill_n(std::data(response.rows[0].info_hash), std::size(response.rows[0].info_hash), std::byte{ 'a' });
    std::fill_n(std::data(response.rows[1].info_hash), std::size(response.rows[1].info_hash), std::byte{ 'b' });
    std::fill_n(std::data(response.rows[2].info_hash), std::size(response.rows[2].info_hash), std::byte{ 'c' });
    response.row_count = 3;
    tr_announcerParseHttpScrapeResponse(response, ResponseBenc, LogName);

    EXPECT_EQ(1, response.rows[0].seeders);
    EXPECT_EQ(2, response.rows[0].leechers);
    EXPECT_EQ(3, response.rows[0].downloads);

    EXPECT_EQ(4, response.rows[1].seeders);
    EXPECT_EQ(5, response.rows[1].leechers);
    EXPECT_EQ(6, response.rows[1].downloads);

    EXPECT_EQ(7, response.rows[2].seeders);
    EXPECT_EQ(8, response.rows[2].leechers);
    EXPECT_EQ(9, response.rows[2].downloads);
}

TEST_F(AnnouncerTest, parseHttpScrapeResponseMultiWithUnexpected)
{
    // clang-format off
    auto constexpr ResponseBenc =
        "d"
            "5:files"
            "d"
                "20:aaaaaaaaaaaaaaaaaaaa"
                "d"
                    "8:complete" "i1e"
                    "10:incomplete" "i2e"
                    "10:downloaded" "i3e"
                "e"

                "20:bbbbbbbbbbbbbbbbbbbb"
                "d"
                    "8:complete" "i4e"
                    "10:incomplete" "i5e"
                    "10:downloaded" "i6e"
                "e"

                "20:cccccccccccccccccccc"
                "d"
                    "8:complete" "i7e"
                    "10:incomplete" "i8e"
                    "10:downloaded" "i9e"
                "e"

                "20:dddddddddddddddddddd"
                "d"
                    "8:complete" "i7e"
                    "10:incomplete" "i8e"
                    "10:downloaded" "i9e"
                "e"
            "e"
        "e"sv;
    // clang-format on

    auto response = tr_scrape_response{};
    std::fill_n(std::data(response.rows[0].info_hash), std::size(response.rows[0].info_hash), std::byte{ 'a' });
    std::fill_n(std::data(response.rows[1].info_hash), std::size(response.rows[1].info_hash), std::byte{ 'b' });
    std::fill_n(std::data(response.rows[2].info_hash), std::size(response.rows[2].info_hash), std::byte{ 'c' });
    response.row_count = 3;
    tr_announcerParseHttpScrapeResponse(response, ResponseBenc, LogName);

    EXPECT_EQ(1, response.rows[0].seeders);
    EXPECT_EQ(2, response.rows[0].leechers);
    EXPECT_EQ(3, response.rows[0].downloads);

    EXPECT_EQ(4, response.rows[1].seeders);
    EXPECT_EQ(5, response.rows[1].leechers);
    EXPECT_EQ(6, response.rows[1].downloads);

    EXPECT_EQ(7, response.rows[2].seeders);
    EXPECT_EQ(8, response.rows[2].leechers);
    EXPECT_EQ(9, response.rows[2].downloads);
}

TEST_F(AnnouncerTest, parseHttpScrapeResponseMultiWithMissing)
{
    // clang-format off
    auto constexpr ResponseBenc =
        "d"
            "5:files"
            "d"
                "20:aaaaaaaaaaaaaaaaaaaa"
                "d"
                    "8:complete" "i1e"
                    "10:incomplete" "i2e"
                    "10:downloaded" "i3e"
                "e"

                "20:cccccccccccccccccccc"
                "d"
                    "8:complete" "i7e"
                    "10:incomplete" "i8e"
                    "10:downloaded" "i9e"
                "e"
            "e"
        "e"sv;
    // clang-format on

    auto response = tr_scrape_response{};
    std::fill_n(std::data(response.rows[0].info_hash), std::size(response.rows[0].info_hash), std::byte{ 'a' });
    std::fill_n(std::data(response.rows[1].info_hash), std::size(response.rows[1].info_hash), std::byte{ 'b' });
    std::fill_n(std::data(response.rows[2].info_hash), std::size(response.rows[2].info_hash), std::byte{ 'c' });
    response.row_count = 3;
    tr_announcerParseHttpScrapeResponse(response, ResponseBenc, LogName);

    EXPECT_EQ(1, response.rows[0].seeders);
    EXPECT_EQ(2, response.rows[0].leechers);
    EXPECT_EQ(3, response.rows[0].downloads);

    EXPECT_EQ(0, response.rows[1].seeders);
    EXPECT_EQ(0, response.rows[1].leechers);
    EXPECT_EQ(0, response.rows[1].downloads);

    EXPECT_EQ(7, response.rows[2].seeders);
    EXPECT_EQ(8, response.rows[2].leechers);
    EXPECT_EQ(9, response.rows[2].downloads);
}
