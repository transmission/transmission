// This file Copyright (C) 2013-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <iostream>
#include <string>
#include <utility>

#include <event2/buffer.h>

#include "transmission.h"

#include "cache.h" // tr_cacheWriteBlock()
#include "file.h" // tr_sys_path_*()
#include "utils.h"
#include "variant.h"

#include "test-fixtures.h"

namespace libtransmission
{

namespace test
{

auto constexpr MaxWaitMsec = 5000;

class IncompleteDirTest
    : public SessionTest
    , public ::testing::WithParamInterface<std::pair<std::string, std::string>>
{
protected:
    void SetUp() override
    {
        std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
        auto const download_dir = GetParam().second;
        tr_variantDictAddStr(settings(), TR_KEY_download_dir, download_dir.c_str());
        auto const incomplete_dir = GetParam().first;
        tr_variantDictAddStr(settings(), TR_KEY_incomplete_dir, incomplete_dir.c_str());
        tr_variantDictAddBool(settings(), TR_KEY_incomplete_dir_enabled, true);

        std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
        SessionTest::SetUp();
        std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
    }

    static auto constexpr MaxWaitMsec = 3000;
};

TEST_P(IncompleteDirTest, incompleteDir)
{
    auto const* download_dir = tr_sessionGetDownloadDir(session_);
    auto const* incomplete_dir = tr_sessionGetIncompleteDir(session_);

    // init an incomplete torrent.
    // the test zero_torrent will be missing its first piece.
    auto* tor = zeroTorrentInit();
    zeroTorrentPopulate(tor, false);
    EXPECT_EQ(tr_strvJoin(incomplete_dir, "/", tr_torrentFile(tor, 0).name, ".part"), makeString(tr_torrentFindFile(tor, 0)));
    EXPECT_EQ(tr_strvPath(incomplete_dir, tr_torrentFile(tor, 1).name), makeString(tr_torrentFindFile(tor, 1)));
    EXPECT_EQ(tor->pieceSize(), tr_torrentStat(tor)->leftUntilDone);

    // auto constexpr completeness_unset = tr_completeness { -1 };
    // auto completeness = completeness_unset;
    int completeness = -1;
    auto const zeroes_completeness_func =
        [](tr_torrent* /*torrent*/, tr_completeness c, bool /*was_running*/, void* vc) noexcept
    {
        *static_cast<tr_completeness*>(vc) = c;
    };
    tr_torrentSetCompletenessCallback(tor, zeroes_completeness_func, &completeness);

    struct TestIncompleteDirData
    {
        tr_session* session = {};
        tr_torrent* tor = {};
        tr_block_index_t block = {};
        tr_piece_index_t pieceIndex = {};
        uint32_t offset = {};
        struct evbuffer* buf = {};
        bool done = {};
    };

    auto const test_incomplete_dir_threadfunc = [](void* vdata) noexcept
    {
        auto* data = static_cast<TestIncompleteDirData*>(vdata);
        tr_cacheWriteBlock(
            data->session->cache,
            data->tor,
            data->tor->pieceLoc(0, data->offset),
            tr_block_info::BlockSize,
            data->buf);
        tr_torrentGotBlock(data->tor, data->block);
        data->done = true;
    };

    // now finish writing it
    {
        char* zero_block = tr_new0(char, tr_block_info::BlockSize);

        struct TestIncompleteDirData data = {};
        data.session = session_;
        data.tor = tor;
        data.buf = evbuffer_new();

        auto const [begin, end] = tor->blockSpanForPiece(data.pieceIndex);

        for (tr_block_index_t block_index = begin; block_index < end; ++block_index)
        {
            evbuffer_add(data.buf, zero_block, tr_block_info::BlockSize);
            data.block = block_index;
            data.done = false;
            data.offset = data.block * tr_block_info::BlockSize;
            tr_runInEventThread(session_, test_incomplete_dir_threadfunc, &data);

            auto const test = [&data]()
            {
                return data.done;
            };
            EXPECT_TRUE(waitFor(test, MaxWaitMsec));
        }

        evbuffer_free(data.buf);
        tr_free(zero_block);
    }

    blockingTorrentVerify(tor);
    EXPECT_EQ(0, tr_torrentStat(tor)->leftUntilDone);

    auto test = [&completeness]()
    {
        return completeness != -1;
    };
    EXPECT_TRUE(waitFor(test, MaxWaitMsec));
    EXPECT_EQ(TR_SEED, completeness);

    auto const n = tr_torrentFileCount(tor);
    for (tr_file_index_t i = 0; i < n; ++i)
    {
        EXPECT_EQ(tr_strvPath(download_dir, tr_torrentFile(tor, i).name), makeString(tr_torrentFindFile(tor, i)));
    }

    // cleanup
    tr_torrentRemove(tor, true, tr_sys_path_remove);
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
    stds::cerr << __FILE__ << ':' << __LINE__ << " setLocation() begin" << std::endl;
    auto const target_dir = tr_strvPath(tr_sessionGetConfigDir(session_), "target");
    tr_sys_dir_create(target_dir.data(), TR_SYS_DIR_CREATE_PARENTS, 0777, nullptr);

    // init a torrent.
    auto* tor = zeroTorrentInit();
    zeroTorrentPopulate(tor, true);
    blockingTorrentVerify(tor);
    EXPECT_EQ(0, tr_torrentStat(tor)->leftUntilDone);

    // now move it
    auto state = int{ -1 };
    tr_torrentSetLocation(tor, target_dir.data(), true, nullptr, &state);
    auto test = [&state]()
    {
        return state == TR_LOC_DONE;
    };
    EXPECT_TRUE(waitFor(test, MaxWaitMsec));
    EXPECT_EQ(TR_LOC_DONE, state);

    // confirm the torrent is still complete after being moved
    blockingTorrentVerify(tor);
    EXPECT_EQ(0, tr_torrentStat(tor)->leftUntilDone);

    // confirm the files really got moved
    sync();
    auto const n = tr_torrentFileCount(tor);
    for (tr_file_index_t i = 0; i < n; ++i)
    {
        EXPECT_EQ(tr_strvPath(target_dir.data(), tr_torrentFile(tor, i).name), makeString(tr_torrentFindFile(tor, i)));
    }

    // cleanup
    tr_torrentRemove(tor, true, tr_sys_path_remove);
    stds::cerr << __FILE__ << ':' << __LINE__ << " setLocation() end" << std::endl;
}

} // namespace test

} // namespace libtransmission
