// This file Copyright (C) 2013-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <string_view>

#define LIBTRANSMISSION_ANNOUNCER_MODULE

#include "transmission.h"

#include "announcer-common.h"

#include "test-fixtures.h"

using AnnouncerTest = ::testing::Test;

using namespace std::literals;

TEST_F(AnnouncerTest, parseHttpAnnounceResponseNoPeers)
{
    auto constexpr NoPeers = "d8:completei3e10:downloadedi2e10:incompletei0e8:intervali1803e12:min intervali1800e5:peers0:e"sv;
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

TEST_F(AnnouncerTest, parseHttpAnnounceResponseFailureReason)
{
    auto constexpr NoPeers =
        "d8:completei3e14:failure reason6:foobar10:downloadedi2e10:incompletei0e8:intervali1803e12:min intervali1800e5:peers0:e"sv;
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
