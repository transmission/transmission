// This file Copyright (C) 2013-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <cstddef> // size_t
#include <string_view>

#include <gtest/gtest.h>

#include "lib/base/tr-macros.h"

#include "libtransmission/crypto-utils.h" // tr_rand_obj()
#include "libtransmission/clients.h"

using namespace std::literals;

TEST(Client, clientForId)
{
    struct LocalTest
    {
        std::string_view peer_id;
        std::string_view expected_client;
    };

    auto constexpr Tests = std::array<LocalTest, 47>{ {
        { .peer_id = "-ADB560-"sv, .expected_client = "Advanced Download Manager 11.5.6"sv },
        { .peer_id = "-AZ8421-"sv, .expected_client = "Azureus / Vuze 8.4.2.1"sv },
        { .peer_id = "-BC0241-"sv, .expected_client = "BitComet 2.41"sv }, // two major, two minor
        { .peer_id = "-BI2300-"sv, .expected_client = "BiglyBT 2.3.0.0"sv },
        { .peer_id = "-BL246326"sv, .expected_client = "BitLord 2.4.6-326"sv }, // Style used after BitLord 0.59
        { .peer_id = "-BN0001-"sv, .expected_client = "Baidu Netdisk"sv }, // Baidu Netdisk Client v5.5.4
        { .peer_id = "-BT791B-"sv, .expected_client = "BitTorrent 7.9.1 (Beta)"sv },
        { .peer_id = "-BT791\0-"sv, .expected_client = "BitTorrent 7.9.1"sv },
        { .peer_id = "-BW1293-"sv, .expected_client = "BitTorrent Web 1.2.9"sv }, // BitTorrent Web 1.2.9.4938 (9924)
        { .peer_id = "-FC1013-"sv, .expected_client = "FileCroc 1.0.1.3"sv },
        { .peer_id = "-FC1013-"sv, .expected_client = "FileCroc 1.0.1.3"sv },
        { .peer_id = "-FD51@\xFF-"sv, .expected_client = "Free Download Manager 5.1.x"sv }, // Negative test case
        { .peer_id = "-FD51R\xFF-"sv, .expected_client = "Free Download Manager 5.1.27"sv },
        { .peer_id = "-FD51W\xFF-"sv, .expected_client = "Free Download Manager 5.1.32"sv },
        { .peer_id = "-FD51\x5D\xC7-"sv,
          .expected_client = "Free Download Manager 5.1.x"sv }, // Free Download Manager 5.1.38.7312 (79f26aa)
        { .peer_id = "-FL51FF-"sv, .expected_client = "Folx 5.x"sv }, // Folx v5.2.1.13690
        { .peer_id = "-FW6830-"sv, .expected_client = "FrostWire 6.8.3"sv },
        { .peer_id = "-IIO\x10\x2D\x04-"sv, .expected_client = "-IIO%10-%04-"sv },
        { .peer_id = "-I\05O\x08\x03\x01-"sv, .expected_client = "-I%05O%08%03%01-"sv },
        { .peer_id = "-KT33D1-"sv, .expected_client = "KTorrent 3.3 Dev 1"sv },
        { .peer_id = "-Lr10X0-"sv, .expected_client = "LibreTorrent 1.0.33"sv },
        { .peer_id = "-MR1100-"sv, .expected_client = "Miro 1.1.0.0"sv },
        { .peer_id = "-PI0091-"sv, .expected_client = "PicoTorrent 0.09.1"sv },
        { .peer_id = "-PI0120-"sv, .expected_client = "PicoTorrent 0.12.0"sv },
        { .peer_id = "-TB2137-"sv, .expected_client = "Torch Browser"sv }, // Torch Browser 55.0.0.12137
        { .peer_id = "-TR0006-"sv, .expected_client = "Transmission 0.6"sv },
        { .peer_id = "-TR0072-"sv, .expected_client = "Transmission 0.72"sv },
        { .peer_id = "-TR111Z-"sv, .expected_client = "Transmission 1.11 (Dev)"sv },
        { .peer_id = "-TR400B-"sv, .expected_client = "Transmission 4.0.0 (Beta)"sv },
        { .peer_id = "-TR4000-"sv, .expected_client = "Transmission 4.0.0"sv },
        { .peer_id = "-TR4Az0-"sv, .expected_client = "Transmission 4.10.61"sv },
        { .peer_id = "-UT341\0-"sv, .expected_client = "\xc2\xb5Torrent 3.4.1"sv },
        { .peer_id = "-UT7a5\0-"sv, .expected_client = "\xc2\xb5Torrent 7.10.5"sv },
        { .peer_id = "-UW110Q-"sv, .expected_client = "\xc2\xb5Torrent Web 1.1.0"sv },
        { .peer_id = "-UW1110Q"sv, .expected_client = "\xc2\xb5Torrent Web 1.1.10"sv }, // wider version
        { .peer_id = "-WS1000-"sv, .expected_client = "HTTP Seed"sv },
        { .peer_id = "-WW0007-"sv, .expected_client = "WebTorrent 0.0.0.7"sv },
        { .peer_id = "-XF9990-"sv,
          .expected_client = "Xfplay 9.9.9"sv }, // Older Xfplay versions have three digit version number
        { .peer_id = "-XF9992-"sv, .expected_client = "Xfplay 9.9.92"sv }, // Xfplay 9.9.92 to 9.9.94 uses "-XF9992-"
        { .peer_id = "A2-1-18-8-"sv, .expected_client = "aria2 1.18.8"sv },
        { .peer_id = "A2-1-2-0-"sv, .expected_client = "aria2 1.2.0"sv },
        { .peer_id = "FD6k4SYy9BOU4U4rk3-J"sv,
          .expected_client = "Free Download Manager 6"sv }, // Free Download Manager 6.17.0.4792 (9a17ce2)
        { .peer_id = "S58B-----"sv, .expected_client = "Shad0w 5.8.11"sv },
        { .peer_id = "Q1-23-4-"sv, .expected_client = "Queen Bee 1.23.4"sv },
        { .peer_id = "TIX0193-"sv, .expected_client = "Tixati 1.93"sv },
        { .peer_id = "\x65\x78\x62\x63\x00\x38\x4C\x4F\x52\x44\x32\x00\x04\x8E\xCE\xD5\x7B\xD7\x10\x28"sv,
          .expected_client = "BitLord 0.56"sv },
        { .peer_id = "\x65\x78\x62\x63\x00\x38\x7A\x44\x63\x10\x2D\x6E\x9A\xD6\x72\x3B\x33\x9F\x35\xA9"sv,
          .expected_client = "BitComet 0.56"sv },
    } };

    for (auto const& test : Tests)
    {
        auto peer_id = tr_rand_obj<tr_peer_id_t>();
        std::copy(std::begin(test.peer_id), std::end(test.peer_id), std::begin(peer_id));

        auto buf = std::array<char, 128>{};
        tr_clientForId(buf.data(), buf.size(), peer_id);
        EXPECT_EQ(test.expected_client, std::string_view{ buf.data() });
    }
}

TEST(Client, clientForIdFuzzRegressions)
{
    auto constexpr Tests = std::array<std::string_view, 5>{
        "LVJTp3u+Aptl01HjzTHXVC5b9g4="sv, "LWJrHb2OpoNsJdODHA7iyXjnHxc="sv, "LU1PjpTjmvUth+f15YTOOggXl3k="sv,
        "LUxU1gO7xhfBD4bmyZkB+neZIx0="sv, "LVJTp3u+Aptl01HjzTHXVC5b9g4="sv,
    };

    for (auto const& test : Tests)
    {
        auto const input = tr_base64_decode(test);
        auto peer_id = tr_peer_id_t{};
        std::copy(std::begin(input), std::end(input), std::begin(peer_id));
        auto buf = std::array<char, 128>{};
        tr_clientForId(buf.data(), buf.size(), peer_id);
    }
}

TEST(Client, clientForIdFuzz)
{
    for (size_t i = 0; i < 10000; ++i)
    {
        auto peer_id = tr_rand_obj<tr_peer_id_t>();
        auto buf = std::array<char, 128>{};
        tr_clientForId(buf.data(), buf.size(), peer_id);
    }
}
