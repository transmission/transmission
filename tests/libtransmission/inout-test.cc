// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <utility>
#include <vector>

#include <libtransmission/transmission.h>

#include <libtransmission/block-info.h>
#include <libtransmission/file.h>
#include <libtransmission/inout.h>
#include <libtransmission/torrent.h>

#include "gtest/gtest.h"
#include "test-fixtures.h"

namespace libtransmission::test
{

using InOutTest = SessionTest;

TEST_F(InOutTest, writeFailsWhenExistingFileCannotBeOpened)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Complete);
    auto constexpr MaxWaitMsec = 5000;

    auto const path = tr_torrentFindFile(tor, 0U);
    ASSERT_FALSE(std::empty(path));

    // tr_ioWrite() must run in the session thread; block until it's done
    auto const run_in_session_thread = [this, MaxWaitMsec](std::function<void()> fn)
    {
        auto const promise = std::make_shared<std::promise<void>>();
        auto future = promise->get_future();
        session_->run_in_session_thread(
            [fn = std::move(fn), promise]()
            {
                fn();
                promise->set_value();
            });
        ASSERT_EQ(std::future_status::ready, future.wait_for(std::chrono::milliseconds{ MaxWaitMsec }));
    };

    auto err = int{};
    auto error_type = TR_STAT_OK;
    auto const write_block = [tor, &err, &error_type]()
    {
        auto const buf = std::vector<uint8_t>(tr_block_info::BlockSize);
        err = tr_ioWrite(*tor, tor->block_loc(0U), std::size(buf), std::data(buf));
        error_type = tor->error().error_type();
    };

    // Neither the fixture nor verify go through the fd pool, so warm it
    // with a write that succeeds: this is the steady state of a torrent
    // that's been receiving blocks from peers.
    run_in_session_thread(write_block);
    ASSERT_EQ(0, err);
    ASSERT_EQ(TR_STAT_OK, error_type);

    // Evict the pooled fd, then make the path unopenable by replacing
    // the file with a directory.
    run_in_session_thread([session = session_, tor]() { session->openFiles().close_torrent(tor->id()); });
    ASSERT_TRUE(tr_sys_path_remove(path.c_str()));
    ASSERT_TRUE(tr_sys_dir_create(path.c_str(), 0, 0700));

    // The next write has to reopen the path. It must fail and set a
    // local error: reporting success would make the caller discard the
    // data, leaving pieces that later fail verification.
    run_in_session_thread(write_block);
    EXPECT_NE(0, err);
    EXPECT_EQ(TR_STAT_LOCAL_ERROR, error_type);
}

} // namespace libtransmission::test
