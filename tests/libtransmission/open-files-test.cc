// This file copyright Transmission authors and contributors.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <string_view>

#include "transmission.h"

#include "error.h"
#include "file.h"
#include "tr-strbuf.h"

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
    EXPECT_TRUE(fd);
    EXPECT_NE(TR_BAD_SYS_FILE, *fd);

    // test the file contents to confirm that fd points to the right file
    auto buf = std::array<char, std::size(Contents) + 1>{};
    auto bytes_read = uint64_t{};
    EXPECT_TRUE(tr_sys_file_read_at(*fd, std::data(buf), std::size(Contents), 0, &bytes_read));
    buf[bytes_read] = '\0';
    EXPECT_EQ(Contents, std::data(buf));
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
    EXPECT_TRUE(fd1);
    EXPECT_TRUE(fd2);
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
    EXPECT_TRUE(fd);
    EXPECT_NE(TR_BAD_SYS_FILE, *fd);
    EXPECT_TRUE(tr_sys_path_exists(filename));
}

TEST_F(OpenFilesTest, closesLeastRecentlyUsedFile)
{
}

TEST_F(OpenFilesTest, closeFile)
{
}

TEST_F(OpenFilesTest, closeTorrent)
{
}
