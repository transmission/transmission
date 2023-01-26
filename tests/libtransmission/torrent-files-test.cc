// This file Copyright (C) 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <string_view>
#include <utility>

#include <libtransmission/transmission.h>

#include <libtransmission/torrent-files.h>

#include "test-fixtures.h"

using namespace std::literals;

class TorrentFilesTest : public ::libtransmission::test::SandboxedTest
{
};

TEST_F(TorrentFilesTest, add)
{
    auto constexpr Path = "/hello/world"sv;
    auto constexpr Size = size_t{ 1024 };

    auto files = tr_torrent_files{};
    EXPECT_EQ(size_t{ 0U }, files.fileCount());
    EXPECT_TRUE(std::empty(files));

    auto const file_index = files.add(Path, Size);
    EXPECT_EQ(tr_file_index_t{ 0U }, file_index);
    EXPECT_EQ(size_t{ 1U }, files.fileCount());
    EXPECT_EQ(Size, files.fileSize(file_index));
    EXPECT_EQ(Path, files.path(file_index));
    EXPECT_FALSE(std::empty(files));
}

TEST_F(TorrentFilesTest, setPath)
{
    auto constexpr Path1 = "/hello/world"sv;
    auto constexpr Path2 = "/hello/there"sv;
    auto constexpr Size = size_t{ 2048 };

    auto files = tr_torrent_files{};
    auto const file_index = files.add(Path1, Size);
    EXPECT_EQ(Path1, files.path(file_index));
    EXPECT_EQ(Size, files.fileSize(file_index));

    files.setPath(file_index, Path2);
    EXPECT_EQ(Path2, files.path(file_index));
    EXPECT_EQ(Size, files.fileSize(file_index));
}

TEST_F(TorrentFilesTest, clear)
{
    auto constexpr Path1 = "/hello/world"sv;
    auto constexpr Path2 = "/hello/there"sv;
    auto constexpr Size = size_t{ 2048 };

    auto files = tr_torrent_files{};
    files.add(Path1, Size);
    EXPECT_EQ(size_t{ 1U }, files.fileCount());
    files.add(Path2, Size);
    EXPECT_EQ(size_t{ 2U }, files.fileCount());

    files.clear();
    EXPECT_TRUE(std::empty(files));
    EXPECT_EQ(size_t{ 0U }, files.fileCount());
}

TEST_F(TorrentFilesTest, find)
{
    static auto constexpr Contents = "hello"sv;
    auto const filename = tr_pathbuf{ sandboxDir(), "/first_dir/hello.txt"sv };
    createFileWithContents(std::string{ filename }, std::data(Contents), std::size(Contents));

    auto files = tr_torrent_files{};
    auto const file_index = files.add("first_dir/hello.txt", 1024);

    auto const search_path_1 = tr_pathbuf{ sandboxDir() };
    auto const search_path_2 = tr_pathbuf{ "/tmp"sv };

    auto search_path = std::vector<std::string_view>{ search_path_1.sv(), search_path_2.sv() };
    auto found = files.find(file_index, std::data(search_path), std::size(search_path));
    EXPECT_TRUE(found.has_value());
    assert(found.has_value());
    EXPECT_EQ(filename, found->filename());

    // same search, but with the search paths reversed
    search_path = std::vector<std::string_view>{ search_path_2.sv(), search_path_1.sv() };
    found = files.find(file_index, std::data(search_path), std::size(search_path));
    EXPECT_TRUE(found.has_value());
    assert(found.has_value());
    EXPECT_EQ(filename, found->filename());

    // now make it an incomplete file
    auto const partial_filename = tr_pathbuf{ filename, tr_torrent_files::PartialFileSuffix };
    EXPECT_TRUE(tr_sys_path_rename(filename, partial_filename));
    search_path = std::vector<std::string_view>{ search_path_1.sv(), search_path_2.sv() };
    found = files.find(file_index, std::data(search_path), std::size(search_path));
    EXPECT_TRUE(found.has_value());
    assert(found.has_value());
    EXPECT_EQ(partial_filename, found->filename());

    // same search, but with the search paths reversed
    search_path = std::vector<std::string_view>{ search_path_2.sv(), search_path_1.sv() };
    found = files.find(file_index, std::data(search_path), std::size(search_path));
    EXPECT_TRUE(found.has_value());
    assert(found.has_value());
    EXPECT_EQ(partial_filename, found->filename());

    // what about if we look for a file that does not exist
    EXPECT_TRUE(tr_sys_path_remove(partial_filename));
    EXPECT_FALSE(files.find(file_index, std::data(search_path), std::size(search_path)));
}

TEST_F(TorrentFilesTest, hasAnyLocalData)
{
    static auto constexpr Contents = "hello"sv;
    auto const filename = tr_pathbuf{ sandboxDir(), "/first_dir/hello.txt"sv };
    createFileWithContents(std::string{ filename }, std::data(Contents), std::size(Contents));

    auto files = tr_torrent_files{};
    files.add("first_dir/hello.txt", 1024);

    auto const search_path_1 = tr_pathbuf{ sandboxDir() };
    auto const search_path_2 = tr_pathbuf{ "/tmp"sv };

    auto search_path = std::vector<std::string_view>{ search_path_1.sv(), search_path_2.sv() };
    EXPECT_TRUE(files.hasAnyLocalData(std::data(search_path), 2U));
    EXPECT_TRUE(files.hasAnyLocalData(std::data(search_path), 1U));
    EXPECT_FALSE(files.hasAnyLocalData(std::data(search_path) + 1, 1U));
    EXPECT_FALSE(files.hasAnyLocalData(std::data(search_path), 0U));
}

TEST_F(TorrentFilesTest, isSubpathPortable)
{
    static auto constexpr Tests = std::array<std::pair<std::string_view, bool>, 15>{ {
        // don't end with periods
        { "foo.", false },
        { "foo..", false },

        // don't begin or end with whitespace
        { " foo ", false },
        { " foo", false },
        { "foo ", false },

        // reserved names
        { "COM1", false },
        { "COM1.txt", false },
        { "Com1", false },
        { "com1", false },

        // reserved characters
        { "hell:o.txt", false },

        // everything else
        { ".foo", true },
        { "com99.txt", true },
        { "foo", true },
        { "hello.txt", true },
        { "hello#.txt", true },
    } };

    for (auto const& [subpath, expected] : Tests)
    {
        EXPECT_EQ(expected, tr_torrent_files::isSubpathPortable(subpath)) << " subpath " << subpath;
    }
}
