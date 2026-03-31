// This file Copyright (C) 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <condition_variable>
#include <cstddef> // size_t
#include <future>
#include <mutex>
#include <string>

#include <gtest/gtest.h>

#include <libtransmission/crypto-utils.h> // tr_base64_decode()
#include <libtransmission/error.h>
#include <libtransmission/torrent-ctor.h>
#include <libtransmission/torrent-magnet.h>
#include <libtransmission/torrent-metainfo.h>
#include <libtransmission/transmission.h>

#include "test-fixtures.h"

namespace tr::test
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

TEST_F(TorrentMagnetTest, addedPausedFetchesMetadataButRemainsStoppedAfterwards)
{
    // Same info dict as `setMetadataPiece` (the zero-torrent's BEP-9 metadata)
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

    // Create a ctor for a paused magnet torrent and register a verify-done hook
    // so we can wait for the verify that fires automatically after metadata arrives
    static auto constexpr V1Hash = "fa5794674a18241bec985ddc3390e3cb171345e4";
    auto* ctor = tr_ctorNew(session_);
    ctor->set_metainfo_from_magnet_link(V1Hash);
    tr_ctorSetPaused(ctor, TR_FORCE, true);

    auto verified = std::promise<tr_torrent*>{};
    auto verified_future = verified.get_future();
    ctor->set_verify_done_callback([&verified](tr_torrent* const tor) { verified.set_value(tor); });

    auto* const tor = tr_torrentNew(ctor, nullptr);
    ASSERT_NE(nullptr, tor);
    tr_ctorFree(ctor);

    // Even though the torrent was added paused it must start immediately so
    // it can connect to peers and fetch the missing metadata via BEP-9.
    EXPECT_TRUE(tor->is_running());
    EXPECT_FALSE(tor->has_metainfo());

    // Simulate receiving the metadata from a peer
    auto const metainfo_benc = tr_base64_decode(InfoDictBase64);
    auto const metainfo_size = std::size(metainfo_benc);
    EXPECT_LE(metainfo_size, MetadataPieceSize);
    session_->run_in_session_thread(
        [&]()
        {
            tor->maybe_start_metadata_transfer(metainfo_size);
            tor->set_metadata_piece(0, std::data(metainfo_benc), metainfo_size);
        });

    // Wait for the metainfo to be stored on the torrent
    EXPECT_TRUE(waitFor([tor] { return tor->has_metainfo(); }, 5s));

    // Wait for the verify pass triggered by on_metainfo_completed() to finish
    ASSERT_EQ(std::future_status::ready, verified_future.wait_for(20s));
    EXPECT_EQ(tor, verified_future.get());

    // After metadata + verify the torrent must still be stopped (paused),
    // keeping the user's original "add paused" intent
    EXPECT_FALSE(tor->is_running());
    EXPECT_EQ(TR_STATUS_STOPPED, tor->activity());
}

} // namespace tr::test
