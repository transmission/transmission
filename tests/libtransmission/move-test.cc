/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <event2/buffer.h>

#include "transmission.h"
#include "cache.h" // tr_cacheWriteBlock()
#include "file.h" // tr_sys_path_*()
#include "variant.h"

#include "test-fixtures.h"

#include <string>
#include <utility>

namespace libtransmission
{

namespace test
{

class IncompleteDirTest :
    public SessionTest,
    public ::testing::WithParamInterface<std::pair<std::string, std::string>>
{
protected:
    void SetUp() override
    {
        auto const incomplete_dir = GetParam().first;
        auto const download_dir = GetParam().second;
        tr_variantDictAddStr(settings(), TR_KEY_download_dir, download_dir.data());
        tr_variantDictAddStr(settings(), TR_KEY_incomplete_dir, incomplete_dir.data());
        tr_variantDictAddBool(settings(), TR_KEY_incomplete_dir_enabled, true);

        SessionTest::SetUp();
    }
};

TEST_P(IncompleteDirTest, incompleteDir)
{
    auto const* download_dir = tr_sessionGetDownloadDir(session_);
    auto const* incomplete_dir = tr_sessionGetIncompleteDir(session_);

    // init an incomplete torrent.
    // the test zero_torrent will be missing its first piece.
    auto* tor = zeroTorrentInit();
    zeroTorrentPopulate(tor, false);
    EXPECT_EQ(makeString(tr_strdup_printf("%s/%s.part", incomplete_dir, tor->info.files[0].name)),
        makeString(tr_torrentFindFile(tor, 0)));
    EXPECT_EQ(makeString(tr_buildPath(incomplete_dir, tor->info.files[1].name, nullptr)),
        makeString(tr_torrentFindFile(tor, 1)));
    EXPECT_EQ(tor->info.pieceSize, tr_torrentStat(tor)->leftUntilDone);

    // auto constexpr completeness_unset = tr_completeness { -1 };
    // auto completeness = completeness_unset;
    int completeness = -1;
    auto const zeroes_completeness_func = [] (
        tr_torrent* /*torrent*/, tr_completeness c,
        bool /*was_running*/, void* vc) noexcept
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

    auto const test_incomplete_dir_threadfunc = [] (void* vdata)noexcept
    {
        auto* data = static_cast<TestIncompleteDirData*>(vdata);
        tr_cacheWriteBlock(data->session->cache, data->tor, 0, data->offset, data->tor->blockSize, data->buf);
        tr_torrentGotBlock(data->tor, data->block);
        data->done = true;
    };

    // now finish writing it
    {
        char* zero_block = tr_new0(char, tor->blockSize);

        struct TestIncompleteDirData data = {};
        data.session = session_;
        data.tor = tor;
        data.buf = evbuffer_new();

        tr_block_index_t first;
        tr_block_index_t last;
        tr_torGetPieceBlockRange(tor, data.pieceIndex, &first, &last);

        for (tr_block_index_t block_index = first; block_index <= last; ++block_index)
        {
            evbuffer_add(data.buf, zero_block, tor->blockSize);
            data.block = block_index;
            data.done = false;
            data.offset = data.block * tor->blockSize;
            tr_runInEventThread(session_, test_incomplete_dir_threadfunc, &data);

            auto const test = [&data]() { return data.done; };
            EXPECT_TRUE(waitFor(test, 1000));
        }

        evbuffer_free(data.buf);
        tr_free(zero_block);
    }

    blockingTorrentVerify(tor);
    EXPECT_EQ(0, tr_torrentStat(tor)->leftUntilDone);

    auto test = [&completeness]() { return completeness != -1; };
    EXPECT_TRUE(waitFor(test, 300));
    EXPECT_EQ(TR_SEED, completeness);

    for (tr_file_index_t file_index = 0; file_index < tor->info.fileCount; ++file_index)
    {
        EXPECT_EQ(makeString(tr_buildPath(download_dir, tor->info.files[file_index].name, nullptr)),
            makeString(tr_torrentFindFile(tor, file_index)));
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
    std::make_pair(std::string{ "Incomplete" }, std::string{ "Downloads" })
    )
    );

/***
****
***/

using MoveTest = SessionTest;

TEST_F(MoveTest, setLocation)
{
    auto const target_dir = makeString(tr_buildPath(tr_sessionGetConfigDir(session_), "target", nullptr));
    tr_sys_dir_create(target_dir.data(), TR_SYS_DIR_CREATE_PARENTS, 0777, nullptr);

    // init a torrent.
    auto* tor = zeroTorrentInit();
    zeroTorrentPopulate(tor, true);
    blockingTorrentVerify(tor);
    EXPECT_EQ(0, tr_torrentStat(tor)->leftUntilDone);

    // now move it
    auto state = int{ -1 };
    tr_torrentSetLocation(tor, target_dir.data(), true, nullptr, &state);
    auto test = [&state]() { return state == TR_LOC_DONE; };
    EXPECT_TRUE(waitFor(test, 300));
    EXPECT_EQ(TR_LOC_DONE, state);

    // confirm the torrent is still complete after being moved
    blockingTorrentVerify(tor);
    EXPECT_EQ(0, tr_torrentStat(tor)->leftUntilDone);

    // confirm the files really got moved
    sync();
    for (tr_file_index_t file_index = 0; file_index < tor->info.fileCount; ++file_index)
    {
        EXPECT_EQ(makeString(tr_buildPath(target_dir.data(), tor->info.files[file_index].name, nullptr)),
            makeString(tr_torrentFindFile(tor, file_index)));
    }

    // cleanup
    tr_torrentRemove(tor, true, tr_sys_path_remove);
}

} // namespace test

} // namespace libtransmission
