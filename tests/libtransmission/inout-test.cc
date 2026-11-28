// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <string>
#include <vector>

#include <libtransmission/transmission.h>

#include <libtransmission/block-info.h>
#include <libtransmission/file.h>
#include <libtransmission/inout.h>
#include <libtransmission/torrent.h>

#include <gtest/gtest.h>

#include "test-fixtures.h"

namespace tr::test
{

using InOutTest = SessionTest;

TEST_F(InOutTest, writeFailsWhenExistingFileCannotBeOpened)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Complete);
    auto constexpr MaxWaitMsec = 5000;

    // make the torrent's first file unopenable by replacing it with a directory
    auto const path = tr_torrentFindFile(tor, 0U);
    ASSERT_FALSE(std::empty(path));
    ASSERT_TRUE(tr_sys_path_remove(path.c_str()));
    ASSERT_TRUE(tr_sys_dir_create(path.c_str(), 0, 0700));

    // The write must fail: reporting success would make the caller
    // discard the data, leaving pieces that later fail verification.
    // (Run in the session thread; drop the file descriptors that verify
    // left open first, so the write has to reopen the unopenable path.)
    auto err = tr_error_code_t{};
    auto done = false;
    session_->run_in_session_thread(
        [this, tor, &err, &done]()
        {
            session_->openFiles().close_torrent(tor->id());
            auto const buf = std::vector<uint8_t>(tr_block_info::BlockSize);
            err = tr_ioWrite(*tor, session_->openFiles(), tor->block_loc(0U), buf);
            done = true;
        });
    ASSERT_TRUE(waitFor([&done]() { return done; }, MaxWaitMsec));

    EXPECT_NE(0, err);
    EXPECT_TRUE(waitFor([tor]() { return tr_torrentStat(tor).error == tr_stat::Error::LocalError; }, MaxWaitMsec));
}

} // namespace tr::test
