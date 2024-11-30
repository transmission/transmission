// This file Copyright (C) 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <cstddef> // size_t
#include <future>
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
    auto piece = 0;
    auto info_dict_size = size_t{ 0U };
    for (;;)
    {
        auto data = tor->get_metadata_piece(piece++);
        if (!data)
        {
            break;
        }

        benc.append(reinterpret_cast<char const*>(std::data(*data)), std::size(*data));
        info_dict_size += std::size(*data);
    }
    benc.append("e");
    EXPECT_EQ(tor->info_dict_size(), info_dict_size);

    auto torrent_metainfo = tr_torrent_metainfo{};
    auto error = tr_error{};
    EXPECT_TRUE(torrent_metainfo.parse_benc(benc, &error));
    EXPECT_FALSE(error) << error.message();

    EXPECT_EQ(tor->piece_hash(0), torrent_metainfo.piece_hash(0));
}

TEST_F(TorrentMagnetTest, setMetadataPiece)
{
    static auto constexpr InfoDictBase64 =
        "ZDU6ZmlsZXNsZDY6bGVuZ3RoaTEwNDg1NzZlNDpwYXRobDc6MTA0ODU3NmVlZDY6bGVuZ3RoaTQw"
        "OTZlNDpwYXRobDQ6NDA5NmVlZDY6bGVuZ3RoaTUxMmU0OnBhdGhsMzo1MTJlZWU0Om5hbWUyNDpm"
        "aWxlcy1maWxsZWQtd2l0aC16ZXJvZXMxMjpwaWVjZSBsZW5ndGhpMzI3NjhlNjpwaWVjZXM2NjA6"
        "UYhDGEm0YTFS/XvbpqP/Ck/WQktRiEMYSbRhMVL9e9umo/8KT9ZCS1GIQxhJtGExUv1726aj/wpP"
        "1kJLUYhDGEm0YTFS/XvbpqP/Ck/WQktRiEMYSbRhMVL9e9umo/8KT9ZCS1GIQxhJtGExUv1726aj"
        "/wpP1kJLUYhDGEm0YTFS/XvbpqP/Ck/WQktRiEMYSbRhMVL9e9umo/8KT9ZCS1GIQxhJtGExUv17"
        "26aj/wpP1kJLUYhDGEm0YTFS/XvbpqP/Ck/WQktRiEMYSbRhMVL9e9umo/8KT9ZCS1GIQxhJtGEx"
        "Uv1726aj/wpP1kJLUYhDGEm0YTFS/XvbpqP/Ck/WQktRiEMYSbRhMVL9e9umo/8KT9ZCS1GIQxhJ"
        "tGExUv1726aj/wpP1kJLUYhDGEm0YTFS/XvbpqP/Ck/WQktRiEMYSbRhMVL9e9umo/8KT9ZCS1GI"
        "QxhJtGExUv1726aj/wpP1kJLUYhDGEm0YTFS/XvbpqP/Ck/WQktRiEMYSbRhMVL9e9umo/8KT9ZC"
        "S1GIQxhJtGExUv1726aj/wpP1kJLUYhDGEm0YTFS/XvbpqP/Ck/WQktRiEMYSbRhMVL9e9umo/8K"
        "T9ZCS1GIQxhJtGExUv1726aj/wpP1kJLUYhDGEm0YTFS/XvbpqP/Ck/WQktRiEMYSbRhMVL9e9um"
        "o/8KT9ZCS1GIQxhJtGExUv1726aj/wpP1kJLUYhDGEm0YTFS/XvbpqP/Ck/WQktRiEMYSbRhMVL9"
        "e9umo/8KT9ZCS1GIQxhJtGExUv1726aj/wpP1kJLUYhDGEm0YTFS/XvbpqP/Ck/WQktRiEMYSbRh"
        "MVL9e9umo/8KT9ZCSzpX+QPk899JzAVbjTNoaVd8IP9dNzpwcml2YXRlaTBlZQ==";

    auto* const tor = zeroTorrentMagnetInit();
    EXPECT_NE(nullptr, tor);
    EXPECT_FALSE(tor->has_metainfo());

    auto const metainfo_benc = tr_base64_decode(InfoDictBase64);
    auto const metainfo_size = std::size(metainfo_benc);
    EXPECT_LE(metainfo_size, MetadataPieceSize);
    session_->run_in_session_thread(
        [&]()
        {
            tor->maybe_start_metadata_transfer(metainfo_size);
            tor->set_metadata_piece(0, std::data(metainfo_benc), metainfo_size);
        });

    EXPECT_TRUE(waitFor([tor] { return tor->has_metainfo(); }, 5s));
    EXPECT_EQ(tor->info_dict_size(), metainfo_size);
    EXPECT_EQ(tor->get_metadata_percent(), 1.0);
}

} // namespace libtransmission::test
