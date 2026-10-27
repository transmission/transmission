// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <cstddef>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#ifndef _WIN32
#include <sys/stat.h>
#endif

#include <fmt/core.h>

#include <libtransmission/file-utils.h>
#include <libtransmission/transmission.h>
#include <libtransmission/torrent.h>
#include <libtransmission/tr-strbuf.h>

#include "test-fixtures.h"

namespace tr::test
{

using TorrentTest = SessionTest;

namespace
{
auto constexpr TorFilenames = std::array{
    "Android-x86 8.1 r6 iso.torrent"sv,
    "debian-11.2.0-amd64-DVD-1.iso.torrent"sv,
    "ubuntu-18.04.6-desktop-amd64.iso.torrent"sv,
    "ubuntu-20.04.4-desktop-amd64.iso.torrent"sv,
};

size_t count_script_runs(std::string_view const filename)
{
    auto contents = std::vector<char>{};
    if (!tr_file_read(filename, contents) || contents.empty())
    {
        return 0U;
    }

    return static_cast<size_t>(std::count(contents.begin(), contents.end(), '\n'));
}

} // namespace

TEST_F(TorrentTest, queueMoveUp)
{
    static constexpr auto ExpectedQueuePosition = std::array{ 0, 1, 3, 2 };
    auto ctor = tr_ctor{ session_ };
    auto torrents = std::array<tr_torrent*, TorFilenames.size()>{};
    std::ranges::transform(
        TorFilenames,
        torrents.begin(),
        [this](auto const filename) { return torrentInitFromFile(filename); });
    auto const move_torrents = std::array{ torrents[0], torrents[1], torrents[3] };

    // Pre-test sanity checks
    for (size_t i = 0; i < torrents.size(); ++i)
    {
        ASSERT_EQ(i, torrents[i]->queue_position());
        ASSERT_EQ(i + 1U, torrents[i]->id());
    }

    tr_torrent::queue_move_up(move_torrents);

    for (size_t i = 0; i < ExpectedQueuePosition.size(); ++i)
    {
        EXPECT_EQ(ExpectedQueuePosition[i], torrents[i]->queue_position()) << i;
    }
}

TEST_F(TorrentTest, queueMoveDown)
{
    static constexpr auto ExpectedQueuePosition = std::array{ 1, 0, 2, 3 };
    auto ctor = tr_ctor{ session_ };
    auto torrents = std::array<tr_torrent*, TorFilenames.size()>{};
    std::ranges::transform(
        TorFilenames,
        torrents.begin(),
        [this](auto const filename) { return torrentInitFromFile(filename); });
    auto const move_torrents = std::array{ torrents[0], torrents[2], torrents[3] };

    // Pre-test sanity checks
    for (size_t i = 0; i < torrents.size(); ++i)
    {
        ASSERT_EQ(i, torrents[i]->queue_position());
        ASSERT_EQ(i + 1U, torrents[i]->id());
    }

    tr_torrent::queue_move_down(move_torrents);

    for (size_t i = 0; i < ExpectedQueuePosition.size(); ++i)
    {
        EXPECT_EQ(ExpectedQueuePosition[i], torrents[i]->queue_position()) << i;
    }
}

TEST_F(TorrentTest, queueMoveTop)
{
    static constexpr auto ExpectedQueuePosition = std::array{ 0, 3, 1, 2 };
    auto ctor = tr_ctor{ session_ };
    auto torrents = std::array<tr_torrent*, TorFilenames.size()>{};
    std::ranges::transform(
        TorFilenames,
        torrents.begin(),
        [this](auto const filename) { return torrentInitFromFile(filename); });
    auto const move_torrents = std::array{ torrents[0], torrents[2], torrents[3] };

    // Pre-test sanity checks
    for (size_t i = 0; i < torrents.size(); ++i)
    {
        ASSERT_EQ(i, torrents[i]->queue_position());
        ASSERT_EQ(i + 1U, torrents[i]->id());
    }

    tr_torrent::queue_move_top(move_torrents);

    for (size_t i = 0; i < ExpectedQueuePosition.size(); ++i)
    {
        EXPECT_EQ(ExpectedQueuePosition[i], torrents[i]->queue_position()) << i;
    }
}

TEST_F(TorrentTest, queueMoveBottom)
{
    static constexpr auto ExpectedQueuePosition = std::array{ 1, 2, 0, 3 };
    auto ctor = tr_ctor{ session_ };
    auto torrents = std::array<tr_torrent*, TorFilenames.size()>{};
    std::ranges::transform(
        TorFilenames,
        torrents.begin(),
        [this](auto const filename) { return torrentInitFromFile(filename); });
    auto const move_torrents = std::array{ torrents[0], torrents[1], torrents[3] };

    // Pre-test sanity checks
    for (size_t i = 0; i < torrents.size(); ++i)
    {
        ASSERT_EQ(i, torrents[i]->queue_position());
        ASSERT_EQ(i + 1U, torrents[i]->id());
    }

    tr_torrent::queue_move_bottom(move_torrents);

    for (size_t i = 0; i < ExpectedQueuePosition.size(); ++i)
    {
        EXPECT_EQ(ExpectedQueuePosition[i], torrents[i]->queue_position()) << i;
    }
}

TEST_F(TorrentTest, doneScriptNotTriggeredTwiceAfterRecheck)
{
#ifdef _WIN32
    GTEST_SKIP();
#else
    auto const script_path = tr_pathbuf{ sandboxDir(), "/done-script.sh" };
    auto const runs_path = tr_pathbuf{ sandboxDir(), "/done-script-runs.txt" };

    auto const script = fmt::format("#!/bin/sh\nprintf 'run\\n' >> \"{}\"\n", runs_path.sv());

    createFileWithContents(script_path.sv(), script);
    ASSERT_EQ(0, chmod(script_path.c_str(), 0700));

    tr_sessionSetScript(session_, TR_SCRIPT_ON_TORRENT_DONE, script_path.sv());
    tr_sessionSetScriptEnabled(session_, TR_SCRIPT_ON_TORRENT_DONE, true);

    auto* const tor = zeroTorrentInit(ZeroTorrentState::Partial);
    ASSERT_NE(nullptr, tor);
    EXPECT_FALSE(tor->is_done());
    EXPECT_EQ(time_t{}, tr_torrentStat(tor).done_date);

    for (tr_piece_index_t piece = 0, n = tor->piece_count(); piece < n; ++piece)
    {
        tor->set_has_piece(piece, true);
    }
    tor->recheck_completeness();

    EXPECT_TRUE(tor->is_done());
    EXPECT_NE(time_t{}, tr_torrentStat(tor).done_date);
    ASSERT_TRUE(waitFor([&runs_path]() { return count_script_runs(std::string{ runs_path.sv() }) == 1U; }, 5000));

    tor->set_has_piece(0, false);
    tor->recheck_completeness();

    EXPECT_FALSE(tor->is_done());
    EXPECT_NE(time_t{}, tr_torrentStat(tor).done_date);

    tor->set_has_piece(0, true);
    tor->recheck_completeness();

    EXPECT_TRUE(tor->is_done());
    EXPECT_NE(time_t{}, tr_torrentStat(tor).done_date);
    EXPECT_FALSE(waitFor([&runs_path]() { return count_script_runs(std::string{ runs_path.sv() }) > 1U; }, 1000));
    EXPECT_EQ(1U, count_script_runs(std::string{ runs_path.sv() }));
#endif
}

} // namespace tr::test
