/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <array>

#include "transmission.h"
#include "clients.h"

#include "gtest/gtest.h"

TEST(Client, clientForId)
{
    struct LocalTest
    {
        char const* peer_id;
        char const* expected_client;
    };

    auto const tests = std::array<LocalTest, 24>{
        LocalTest{ "-BT791B-", "BitTorrent 7.9.1 (Beta)" },
        { "-BT791\0-", "BitTorrent 7.9.1" },
        { "-FC1013-", "FileCroc 1.0.1.3" },
        { "-FC1013-", "FileCroc 1.0.1.3" },
        { "-MR1100-", "Miro 1.1.0.0" },
        { "-TR0006-", "Transmission 0.6" },
        { "-TR0072-", "Transmission 0.72" },
        { "-TR111Z-", "Transmission 1.11+" },
        { "-UT341\0-", "\xc2\xb5Torrent 3.4.1" },
        { "O1008132", "Osprey 1.0.0" },
        { "TIX0193-", "Tixati 1.93" },

        /* Xfplay 9.9.92 to 9.9.94 uses "-XF9992-" */
        { "-XF9992-", "Xfplay 9.9.92" },

        /* Older Xfplay versions have three digit version number */
        { "-XF9990-", "Xfplay 9.9.9" },

        /* PicoTorrent */
        { "-PI0091-", "PicoTorrent 0.09.1" },
        { "-PI0120-", "PicoTorrent 0.12.0" },

        /* Free Download Manager */
        { "-FD51R\xFF-", "Free Download Manager 5.1.27" },
        { "-FD51W\xFF-", "Free Download Manager 5.1.32" },
        { "-FD51@\xFF-", "Free Download Manager 5.1.x" }, /* Negative test case */

        /* Folx */
        { "-FL51FF-", "Folx 5.x" }, /* Folx v5.2.1.13690 */

        /* Baidu Netdisk */
        { "-BN0001-", "Baidu Netdisk" }, /* Baidu Netdisk Client v5.5.4 */

        /* gobbledygook */
        { "-IIO\x10\x2D\x04-", "-IIO%10-%04-" },
        { "-I\05O\x08\x03\x01-", "-I%05O%08%03%01-" },

        { "\x65\x78\x62\x63\x00\x38\x7A\x44\x63\x10\x2D\x6E\x9A\xD6\x72\x3B\x33\x9F\x35\xA9", "BitComet 0.56" },
        { "\x65\x78\x62\x63\x00\x38\x4C\x4F\x52\x44\x32\x00\x04\x8E\xCE\xD5\x7B\xD7\x10\x28", "BitLord 0.56" }
    };

    for (auto const& test : tests)
    {
        auto buf = std::array<char, 128>{};
        tr_clientForId(buf.data(), buf.size(), test.peer_id);
        EXPECT_STREQ(test.expected_client, buf.data());
    }
}
