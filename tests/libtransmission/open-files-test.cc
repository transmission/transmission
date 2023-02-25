// This file copyright Transmission authors and contributors.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <string_view>

#include <libtransmission/transmission.h>

#include <libtransmission/error.h>
#include <libtransmission/file.h>
#include <libtransmission/tr-strbuf.h>

#include "test-fixtures.h"

using namespace std::literals;

using OpenFilesTest = libtransmission::test::SessionTest;

TEST_F(OpenFilesTest, getCachedFailsIfNotCached)
{
    auto const fd = session_->openFiles().get(0, 0, false);
    EXPECT_FALSE(fd);
}

TEST_F(OpenFilesTest, getOpensIfNotCached)
{
    static auto constexpr Contents = "Hello, World!\n"sv;
    auto filename = tr_pathbuf{ sandboxDir(), "/test-file.txt" };
    createFileWithContents(filename, Contents);

    // confirm that it's not pre-cached
    EXPECT_FALSE(session_->openFiles().get(0, 0, false));

    // confirm that we can cache the file
    auto fd = session_->openFiles().get(0, 0, false, filename, TR_PREALLOCATE_FULL, std::size(Contents));
    EXPECT_TRUE(fd.has_value());
    assert(fd.has_value());
    EXPECT_NE(TR_BAD_SYS_FILE, *fd);

    // test the file contents to confirm that fd points to the right file
    auto buf = std::array<char, std::size(Contents) + 1>{};
    auto bytes_read = uint64_t{};
    EXPECT_TRUE(tr_sys_file_read_at(*fd, std::data(buf), std::size(Contents), 0, &bytes_read));
    auto const contents = std::string_view{ std::data(buf), static_cast<size_t>(bytes_read) };
    EXPECT_EQ(Contents, contents);
}

TEST_F(OpenFilesTest, getCacheSucceedsIfCached)
{
    static auto constexpr Contents = "Hello, World!\n"sv;
    auto filename = tr_pathbuf{ sandboxDir(), "/test-file.txt" };
    createFileWithContents(filename, Contents);

    EXPECT_FALSE(session_->openFiles().get(0, 0, false));
    EXPECT_TRUE(session_->openFiles().get(0, 0, false, filename, TR_PREALLOCATE_FULL, std::size(Contents)));
    EXPECT_TRUE(session_->openFiles().get(0, 0, false));
}

TEST_F(OpenFilesTest, getCachedReturnsTheSameFd)
{
    static auto constexpr Contents = "Hello, World!\n"sv;
    auto filename = tr_pathbuf{ sandboxDir(), "/test-file.txt" };
    createFileWithContents(filename, Contents);

    EXPECT_FALSE(session_->openFiles().get(0, 0, false));
    auto const fd1 = session_->openFiles().get(0, 0, false, filename, TR_PREALLOCATE_FULL, std::size(Contents));
    auto const fd2 = session_->openFiles().get(0, 0, false);
    EXPECT_TRUE(fd1.has_value());
    EXPECT_TRUE(fd2.has_value());
    assert(fd1.has_value());
    assert(fd2.has_value());
    EXPECT_EQ(*fd1, *fd2);
}

TEST_F(OpenFilesTest, getCachedFailsIfWrongPermissions)
{
    static auto constexpr Contents = "Hello, World!\n"sv;
    auto filename = tr_pathbuf{ sandboxDir(), "/test-file.txt" };
    createFileWithContents(filename, Contents);

    // cache it in ro mode
    EXPECT_FALSE(session_->openFiles().get(0, 0, false));
    EXPECT_TRUE(session_->openFiles().get(0, 0, false, filename, TR_PREALLOCATE_FULL, std::size(Contents)));

    // now try to get it in r/w mode
    EXPECT_TRUE(session_->openFiles().get(0, 0, false));
    EXPECT_FALSE(session_->openFiles().get(0, 0, true));
}

TEST_F(OpenFilesTest, opensInReadOnlyUnlessWritableIsRequested)
{
    static auto constexpr Contents = "Hello, World!\n"sv;
    auto filename = tr_pathbuf{ sandboxDir(), "/test-file.txt" };
    createFileWithContents(filename, Contents);

    // cache a file read-only mode
    tr_error* error = nullptr;
    auto fd = session_->openFiles().get(0, 0, false, filename, TR_PREALLOCATE_FULL, std::size(Contents));
    EXPECT_TRUE(fd.has_value());
    assert(fd.has_value());

    // confirm that writing to it fails
    EXPECT_FALSE(tr_sys_file_write(*fd, std::data(Contents), std::size(Contents), nullptr, &error));
    EXPECT_NE(0, error->code);
    tr_error_clear(&error);
}

TEST_F(OpenFilesTest, createsMissingFileIfWriteRequested)
{
    static auto constexpr Contents = "Hello, World!\n"sv;
    auto filename = tr_pathbuf{ sandboxDir(), "/test-file.txt" };
    EXPECT_FALSE(tr_sys_path_exists(filename));

    auto fd = session_->openFiles().get(0, 0, false);
    EXPECT_FALSE(fd);
    EXPECT_FALSE(tr_sys_path_exists(filename));

    fd = session_->openFiles().get(0, 0, true, filename, TR_PREALLOCATE_FULL, std::size(Contents));
    EXPECT_TRUE(fd.has_value());
    assert(fd.has_value());
    EXPECT_NE(TR_BAD_SYS_FILE, *fd);
    EXPECT_TRUE(tr_sys_path_exists(filename));
}

TEST_F(OpenFilesTest, closeFileClosesTheFile)
{
    static auto constexpr Contents = "Hello, World!\n"sv;
    auto filename = tr_pathbuf{ sandboxDir(), "/test-file.txt" };
    createFileWithContents(filename, Contents);

    // cache a file read-only mode
    EXPECT_TRUE(session_->openFiles().get(0, 0, false, filename, TR_PREALLOCATE_FULL, std::size(Contents)));
    EXPECT_TRUE(session_->openFiles().get(0, 0, false));

    // close the file
    session_->openFiles().closeFile(0, 0);

    // confirm that its fd is no longer cached
    EXPECT_FALSE(session_->openFiles().get(0, 0, false));
}

TEST_F(OpenFilesTest, closeTorrentClosesTheTorrentFiles)
{
    static auto constexpr Contents = "Hello, World!\n"sv;
    static auto constexpr TorId = tr_torrent_id_t{ 0 };

    auto filename = tr_pathbuf{ sandboxDir(), "/a.txt" };
    createFileWithContents(filename, Contents);
    EXPECT_TRUE(session_->openFiles().get(TorId, 1, false, filename, TR_PREALLOCATE_FULL, std::size(Contents)));

    filename.assign(sandboxDir(), "/b.txt");
    createFileWithContents(filename, Contents);
    EXPECT_TRUE(session_->openFiles().get(TorId, 3, false, filename, TR_PREALLOCATE_FULL, std::size(Contents)));

    // confirm that closing a different torrent does not affect these files
    session_->openFiles().closeTorrent(TorId + 1);
    EXPECT_TRUE(session_->openFiles().get(TorId, 1, false));
    EXPECT_TRUE(session_->openFiles().get(TorId, 3, false));

    // confirm that closing this torrent closes and uncaches the files
    session_->openFiles().closeTorrent(TorId);
    EXPECT_FALSE(session_->openFiles().get(TorId, 1, false));
    EXPECT_FALSE(session_->openFiles().get(TorId, 3, false));
}

TEST_F(OpenFilesTest, closesLeastRecentlyUsedFile)
{
    static auto constexpr Contents = "Hello, World!\n"sv;
    static auto constexpr TorId = tr_torrent_id_t{ 0 };
    static auto constexpr LargerThanCacheLimit = 100;

    // Walk through a number of files. Confirm that they all succeed
    // even when the number exhausts the cache size, and newer files
    // supplant older ones.
    for (int i = 0; i < LargerThanCacheLimit; ++i)
    {
        auto filename = tr_pathbuf{ sandboxDir(), fmt::format("/file-{:d}.txt"sv, i) };
        EXPECT_TRUE(session_->openFiles().get(TorId, i, true, filename, TR_PREALLOCATE_FULL, std::size(Contents)));
    }

    // Do a lookup-only for the files again *in the same order*. By following the
    // order, the first files we check will be the oldest from the last pass and
    // should have aged out. So we should have a nonzero number of failures; but
    // once we get a success, all the remaining should also succeed.
    auto results = std::array<bool, LargerThanCacheLimit>{};
    auto sorted = std::array<bool, LargerThanCacheLimit>{};
    for (int i = 0; i < LargerThanCacheLimit; ++i)
    {
        auto filename = tr_pathbuf{ sandboxDir(), fmt::format("/file-{:d}.txt"sv, i) };
        results[i] = !!session_->openFiles().get(TorId, i, false);
    }
    sorted = results;
    std::sort(std::begin(sorted), std::end(sorted));
    EXPECT_EQ(sorted, results);
    EXPECT_GT(std::count(std::begin(results), std::end(results), true), 0);
}
