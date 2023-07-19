// This file Copyright (C) 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <cstddef> // size_t
#include <string>

#include <libtransmission/error.h>
#include <libtransmission/torrent-magnet.h>
#include <libtransmission/torrent-metainfo.h>

#include "gtest/gtest.h"
#include "test-fixtures.h"

namespace libtransmission::test
{

using TorrentMagnetTest = SessionTest;

TEST_F(TorrentMagnetTest, getMetadataPiece)
{
    auto* tor = zeroTorrentInit(ZeroTorrentState::Complete);
    EXPECT_NE(nullptr, tor);

    auto benc = std::string{
        "d8:announce31:http://www.example.com/announce10:created by25:Transmission/2.61 (13407)13:creation datei1358704075e8:encoding5:UTF-84:info"
    };
    auto piece = int{ 0 };
    auto info_dict_size = size_t{ 0U };
    auto data = tr_metadata_piece{};
    for (;;)
    {
        if (!tr_torrentGetMetadataPiece(tor, piece++, data))
        {
            break;
        }

        benc.append(reinterpret_cast<char const*>(std::data(data)), std::size(data));
        info_dict_size += std::size(data);
    }
    benc.append("e");
    EXPECT_EQ(tor->info_dict_size(), info_dict_size);

    auto torrent_metainfo = tr_torrent_metainfo{};
    tr_error* error = nullptr;
    EXPECT_TRUE(torrent_metainfo.parse_benc(benc, &error));
    EXPECT_EQ(nullptr, error) << error->message;
    tr_error_clear(&error);

    EXPECT_EQ(tor->piece_hash(0), torrent_metainfo.piece_hash(0));
}

} // namespace libtransmission::test
