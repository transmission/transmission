// This file Copyright (C) 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "transmission.h"

#include "files.h"

#include "test-fixtures.h"

using namespace std::literals;

class FilesTest : public ::libtransmission::test::SandboxedTest
{
};

TEST_F(FilesTest, add)
{
    auto constexpr Path = "/hello/world"sv;
    auto constexpr Size = size_t{ 1024 };

    auto files = tr_files{};
    EXPECT_EQ(size_t{ 0U }, std::size(files));
    EXPECT_TRUE(std::empty(files));

    auto const file_index = files.add(Path, Size);
    EXPECT_EQ(tr_file_index_t{ 0U }, file_index);
    EXPECT_EQ(size_t{ 1U }, std::size(files));
    EXPECT_EQ(Size, files.size(file_index));
    EXPECT_EQ(Path, files.path(file_index));
    EXPECT_FALSE(std::empty(files));
}

TEST_F(FilesTest, setPath)
{
    auto constexpr Path1 = "/hello/world"sv;
    auto constexpr Path2 = "/hello/there"sv;
    auto constexpr Size = size_t{ 2048 };

    auto files = tr_files{};
    auto const file_index = files.add(Path1, Size);
    EXPECT_EQ(Path1, files.path(file_index));
    EXPECT_EQ(Size, files.size(file_index));

    files.setPath(file_index, Path2);
    EXPECT_EQ(Path2, files.path(file_index));
    EXPECT_EQ(Size, files.size(file_index));
}

TEST_F(FilesTest, clear)
{
    auto constexpr Path1 = "/hello/world"sv;
    auto constexpr Path2 = "/hello/there"sv;
    auto constexpr Size = size_t{ 2048 };

    auto files = tr_files{};
    files.add(Path1, Size);
    EXPECT_EQ(size_t{ 1U }, std::size(files));
    files.add(Path2, Size);
    EXPECT_EQ(size_t{ 2U }, std::size(files));

    files.clear();
    EXPECT_TRUE(std::empty(files));
    EXPECT_EQ(size_t{ 0U }, std::size(files));
}

TEST_F(FilesTest, find)
{
    static auto constexpr Contents = "hello"sv;
    auto const filename = tr_pathbuf{ sandboxDir(), "/first_dir/hello.txt"sv };
    createFileWithContents(std::string{ filename }, std::data(Contents), std::size(Contents));

    auto files = tr_files{};
    auto const file_index = files.add("first_dir/hello.txt", 1024);

    auto const search_path_1 = tr_pathbuf{ sandboxDir() };
    auto const search_path_2 = tr_pathbuf{ "/tmp"sv };

    auto search_path = std::vector<std::string_view>{ search_path_1.sv(), search_path_2.sv() };
    auto found = files.find(std::data(search_path), std::size(search_path), file_index);
    EXPECT_TRUE(found);
    EXPECT_EQ(filename, found->filename());

    // same search, but with the search paths reversed
    search_path = std::vector<std::string_view>{ search_path_2.sv(), search_path_1.sv() };
    found = files.find(std::data(search_path), std::size(search_path), file_index);
    EXPECT_TRUE(found);
    EXPECT_EQ(filename, found->filename());

    // now make it an incomplete file
    auto const partial_filename = tr_pathbuf{ filename, tr_files::PartialFileSuffix };
    EXPECT_TRUE(tr_sys_path_rename(filename, partial_filename));
    search_path = std::vector<std::string_view>{ search_path_1.sv(), search_path_2.sv() };
    found = files.find(std::data(search_path), std::size(search_path), file_index);
    EXPECT_TRUE(found);
    EXPECT_EQ(partial_filename, found->filename());

    // same search, but with the search paths reversed
    search_path = std::vector<std::string_view>{ search_path_2.sv(), search_path_1.sv() };
    found = files.find(std::data(search_path), std::size(search_path), file_index);
    EXPECT_TRUE(found);
    EXPECT_EQ(partial_filename, found->filename());

    // what about if we look for a file that does not exist
    EXPECT_TRUE(tr_sys_path_remove(partial_filename));
    EXPECT_FALSE(files.find(std::data(search_path), std::size(search_path), file_index));
}
