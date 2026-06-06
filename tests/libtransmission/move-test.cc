// This file Copyright (C) 2013-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <gtest/gtest.h>

#include <libtransmission/transmission.h>

#include <libtransmission/block-info.h>
#include <libtransmission/file.h> // tr_sys_path_*()
#include <libtransmission/inout.h>
#include <libtransmission/quark.h>
#include <libtransmission/torrent-files.h>
#include <libtransmission/torrent.h>
#include <libtransmission/tr-strbuf.h>
#include <libtransmission/variant.h>

#include "test-fixtures.h"

using namespace std::literals;

namespace tr::test
{

auto constexpr MaxWaitMsec = 5000;

class IncompleteDirTest
    : public SessionTest
    , public ::testing::WithParamInterface<std::pair<std::string, std::string>>
{
protected:
    void SetUp() override
    {
        if (auto* map = settings()->get_if<tr_variant::Map>(); map != nullptr)
        {
            auto const download_dir = GetParam().second;
            map->insert_or_assign(TR_KEY_download_dir, download_dir);
            auto const incomplete_dir = GetParam().first;
            map->insert_or_assign(TR_KEY_incomplete_dir, incomplete_dir);
            map->insert_or_assign(TR_KEY_incomplete_dir_enabled, true);
        }

        SessionTest::SetUp();
    }

    static auto constexpr MaxWaitMsec = 3000;
};

TEST_P(IncompleteDirTest, incompleteDir)
{
    std::string const download_dir = tr_sessionGetDownloadDir(session_);
    std::string const incomplete_dir = tr_sessionGetIncompleteDir(session_);

    // init an incomplete torrent.
    // the test zero_torrent will be missing its first piece.
    tr_sessionSetIncompleteFileNamingEnabled(session_, true);
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Partial);
    auto path = tr_pathbuf{};

    path.assign(incomplete_dir, '/', tr_torrentFile(tor, 0).name, tr_torrent_files::PartialFileSuffix);
    EXPECT_EQ(path, tr_torrentFindFile(tor, 0));
    path.assign(incomplete_dir, '/', tr_torrentFile(tor, 1).name);
    EXPECT_EQ(path, tr_torrentFindFile(tor, 1));
    EXPECT_EQ(tor->piece_size(), tr_torrentStat(tor).left_until_done);

    auto completeness = TR_LEECH;
    tr_sessionSetCompletenessCallback(
        session_,
        [&completeness](tr_torrent_id_t const /*tor_id*/, tr_completeness const c, bool const /*was_running*/) noexcept
        { completeness = c; });

    struct TestIncompleteDirData
    {
        tr_session* session = {};
        tr_torrent* tor = {};
        tr_block_index_t block = {};
        tr_piece_index_t pieceIndex = {};
        std::vector<uint8_t> buf;
        bool done = {};
    };

    auto const test_incomplete_dir_threadfunc = [](TestIncompleteDirData* data) noexcept
    {
        auto& tor = *data->tor;
        if (tr_ioWrite(tor, data->session->openFiles(), tor.block_loc(data->block), data->buf) == 0)
        {
            data->tor->on_block_received(data->block);
        }

        data->done = true;
    };

    // now finish writing it
    {
        auto data = TestIncompleteDirData{};
        data.session = session_;
        data.tor = tor;

        auto const [begin, end] = tor->block_span_for_piece(data.pieceIndex);

        for (tr_block_index_t block_index = begin; block_index < end; ++block_index)
        {
            data.buf.resize(tr_block_info::BlockSize);
            std::ranges::fill(data.buf, '\0');
            data.block = block_index;
            data.done = false;
            session_->run_in_session_thread(test_incomplete_dir_threadfunc, &data);

            auto const test = [&data]()
            {
                return data.done;
            };
            EXPECT_TRUE(waitFor(test, MaxWaitMsec));
        }
    }

    blockingTorrentVerify(tor);
    EXPECT_EQ(0, tr_torrentStat(tor).left_until_done);

    auto test = [&completeness]()
    {
        return completeness != TR_LEECH;
    };
    EXPECT_TRUE(waitFor(test, MaxWaitMsec));
    EXPECT_EQ(TR_SEED, completeness);

    // the files are relocated to the download dir on a background thread,
    // so wait for the move to finish before checking their final location
    auto const n = tr_torrentFileCount(tor);
    auto const files_moved = [tor, &download_dir, n]()
    {
        for (tr_file_index_t i = 0; i < n; ++i)
        {
            auto const expected = tr_pathbuf{ download_dir, '/', tr_torrentFile(tor, i).name };
            if (expected != tr_torrentFindFile(tor, i))
            {
                return false;
            }
        }
        return true;
    };
    EXPECT_TRUE(waitFor(files_moved, MaxWaitMsec));

    // cleanup
    tr_torrentRemove(tor, true);
}

INSTANTIATE_TEST_SUITE_P(
    IncompleteDir,
    IncompleteDirTest,
    ::testing::Values(
        // what happens when incompleteDir is a subdir of downloadDir
        std::make_pair(std::string{ "Downloads/Incomplete" }, std::string{ "Downloads" }),
        // test what happens when downloadDir is a subdir of incompleteDir
        std::make_pair(std::string{ "Downloads" }, std::string{ "Downloads/Complete" }),
        // test what happens when downloadDir and incompleteDir are siblings
        std::make_pair(std::string{ "Incomplete" }, std::string{ "Downloads" })));

/***
****
***/

using MoveTest = SessionTest;

TEST_F(MoveTest, setLocation)
{
    auto const target_dir = tr_pathbuf{ session_->configDir(), "/target"sv };
    tr_sys_dir_create(target_dir, TR_SYS_DIR_CREATE_PARENTS, 0777, nullptr);

    // init a torrent.
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Complete);
    blockingTorrentVerify(tor);
    EXPECT_EQ(0, tr_torrentStat(tor).left_until_done);

    // now move it
    auto state = -1;
    tr_torrentSetLocation(tor, target_dir, true, &state);
    auto test = [&state]()
    {
        return state == TR_LOC_DONE;
    };
    EXPECT_TRUE(waitFor(test, MaxWaitMsec));
    EXPECT_EQ(TR_LOC_DONE, state);

    // confirm the torrent is still complete after being moved
    blockingTorrentVerify(tor);
    EXPECT_EQ(0, tr_torrentStat(tor).left_until_done);

    // confirm the files really got moved
    sync();
    auto const n = tr_torrentFileCount(tor);
    for (tr_file_index_t i = 0; i < n; ++i)
    {
        auto const expected = tr_pathbuf{ target_dir, '/', tr_torrentFile(tor, i).name };
        EXPECT_EQ(expected, tr_torrentFindFile(tor, i));
    }

    // cleanup
    tr_torrentRemove(tor, true);
}

// The relocation runs on a background thread and is expected to create the
// destination directory tree if it does not already exist.
TEST_F(MoveTest, setLocationCreatesDestination)
{
    // a deliberately not-yet-existing, nested destination
    auto const target_dir = tr_pathbuf{ session_->configDir(), "/new/nested/target"sv };
    EXPECT_FALSE(tr_sys_path_exists(target_dir));

    auto* const tor = zeroTorrentInit(ZeroTorrentState::Complete);
    blockingTorrentVerify(tor);
    EXPECT_EQ(0, tr_torrentStat(tor).left_until_done);

    // move it; the background move thread should create the destination
    auto state = -1;
    tr_torrentSetLocation(tor, target_dir, true, &state);
    auto const moved = [&state]()
    {
        return state == TR_LOC_DONE;
    };
    EXPECT_TRUE(waitFor(moved, MaxWaitMsec));
    EXPECT_EQ(TR_LOC_DONE, state);

    // confirm the files ended up at the new location...
    sync();
    auto const n = tr_torrentFileCount(tor);
    for (tr_file_index_t i = 0; i < n; ++i)
    {
        auto const expected = tr_pathbuf{ target_dir, '/', tr_torrentFile(tor, i).name };
        EXPECT_EQ(expected, tr_torrentFindFile(tor, i));
    }

    // ...and that the torrent is still complete
    blockingTorrentVerify(tor);
    EXPECT_EQ(0, tr_torrentStat(tor).left_until_done);

    tr_torrentRemove(tor, true);
}

// Two relocations of the same torrent are serialized through the move worker;
// the second one must observe the bookkeeping left by the first.
TEST_F(MoveTest, setLocationMoveBackAndForth)
{
    auto const original_dir = tr_pathbuf{ tr_sessionGetDownloadDir(session_) };
    auto const target_dir = tr_pathbuf{ session_->configDir(), "/elsewhere"sv };
    tr_sys_dir_create(target_dir, TR_SYS_DIR_CREATE_PARENTS, 0777, nullptr);

    auto* const tor = zeroTorrentInit(ZeroTorrentState::Complete);
    blockingTorrentVerify(tor);
    EXPECT_EQ(0, tr_torrentStat(tor).left_until_done);

    auto const n = tr_torrentFileCount(tor);
    auto const files_in = [tor, n](tr_pathbuf const& dir)
    {
        for (tr_file_index_t i = 0; i < n; ++i)
        {
            if (tr_pathbuf{ dir, '/', tr_torrentFile(tor, i).name } != tr_torrentFindFile(tor, i))
            {
                return false;
            }
        }
        return true;
    };

    // move out...
    auto state = -1;
    tr_torrentSetLocation(tor, target_dir, true, &state);
    EXPECT_TRUE(waitFor([&state]() { return state == TR_LOC_DONE; }, MaxWaitMsec));
    sync();
    EXPECT_TRUE(files_in(target_dir));

    // ...and back again
    state = -1;
    tr_torrentSetLocation(tor, original_dir, true, &state);
    EXPECT_TRUE(waitFor([&state]() { return state == TR_LOC_DONE; }, MaxWaitMsec));
    sync();
    EXPECT_TRUE(files_in(original_dir));

    // the round trip preserved the data
    blockingTorrentVerify(tor);
    EXPECT_EQ(0, tr_torrentStat(tor).left_until_done);

    tr_torrentRemove(tor, true);
}

// When move_from_old_path is false the files are left in place and only the
// torrent's download dir is repointed.
TEST_F(MoveTest, setLocationWithoutMovingUpdatesDownloadDir)
{
    auto const target_dir = tr_pathbuf{ session_->configDir(), "/already-there"sv };
    tr_sys_dir_create(target_dir, TR_SYS_DIR_CREATE_PARENTS, 0777, nullptr);

    auto* const tor = zeroTorrentInit(ZeroTorrentState::Complete);
    blockingTorrentVerify(tor);

    auto state = -1;
    tr_torrentSetLocation(tor, target_dir, false, &state);
    EXPECT_TRUE(waitFor([&state]() { return state == TR_LOC_DONE; }, MaxWaitMsec));
    EXPECT_EQ(TR_LOC_DONE, state);

    // the torrent now points at the new dir even though nothing was relocated
    EXPECT_EQ(target_dir.sv(), tr_torrentGetDownloadDir(tor));

    tr_torrentRemove(tor, true);
}

} // namespace tr::test
