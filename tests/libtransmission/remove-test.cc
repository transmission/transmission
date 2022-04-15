// This file Copyright (C) 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <set>
#include <string_view>
#include <utility>

#include "transmission.h"

#include "file.h"
#include "torrent-files.h"
#include "tr-strbuf.h"

#include "test-fixtures.h"

using namespace std::literals;
using RemoveTest = libtransmission::test::SandboxedTest;
using SubpathAndSize = std::pair<std::string_view, uint64_t>;

static auto getSubtreeContents(std::string_view parent_dir)
{
    auto filenames = std::set<std::string>{};

    auto file_func = [&filenames](const char* filename)
    {
        filenames.emplace(filename);
    };

    libtransmission::test::depthFirstWalk(tr_pathbuf{ parent_dir }, file_func);

    return filenames;
}

TEST_F(RemoveTest, RemovesSingleFile)
{
    // test setup: define a single-file torrent, no folders
    static auto constexpr Content = "Hello, World!"sv;
    static auto constexpr Files = std::array<SubpathAndSize, 1>{{
        { "ubuntu-20.04.4-desktop-amd64.iso"sv, 3379068928ULL }
    }};

    // test setup: create the `tr_torrent_files`
    auto files = tr_torrent_files{};
    for (auto const& file : Files)
    {
        auto const& [filename, size] = file;
        files.add(filename, size);
    }

    // test setup: populate the filesystem
    auto const parent = sandboxDir();
    auto expected_subtree_contents = std::set<std::string>{
        std::string{ parent }
    };
    EXPECT_EQ(expected_subtree_contents, getSubtreeContents(parent));

    auto const filename = tr_pathbuf{ parent, '/', files.path(0) };
    createFileWithContents(filename, std::data(Content), std::size(Content));

    // before remove, the subtree should be:
    expected_subtree_contents = std::set<std::string>{
        std::string{ parent },
        std::string{ filename }
    };
    EXPECT_EQ(expected_subtree_contents, getSubtreeContents(parent));

    // now remove the files
    files.remove(parent, "tmpdir_prefix"sv, tr_sys_path_remove);

    // after remove, the subtree should be:
    expected_subtree_contents = std::set<std::string>{
        std::string{ parent }
    };
    EXPECT_EQ(expected_subtree_contents, getSubtreeContents(parent));
}

TEST_F(RemoveTest, RemovesSubtree)
{
}

TEST_F(RemoveTest, RemovesSubtreeIfPossible)
{
}

TEST_F(RemoveTest, RemovesFilesIfUnableToRemoveSubtree)
{
}

TEST_F(RemoveTest, RemovesLeftoverJunk)
{
}

TEST_F(RemoveTest, CleansUpTmpdirWhenDone)
{
}

TEST_F(RemoveTest, DoesNotRemoveOtherFilesInSubtree)
{
}

TEST_F(RemoveTest, DoesNotRemoveSiblingFiles)
{
}
