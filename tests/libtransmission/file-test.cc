// This file Copyright (C) 2013-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <cassert>
#include <cstdint> // uint64_t
#include <cstdio> // stderr
#include <cstring>
#include <ctime> // time()
#include <ostream>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#else
#include <windows.h>
#endif

#include <fmt/format.h>

#include <gtest/gtest.h>

#include <libtransmission/error.h>
#include <libtransmission/file.h>
#include <libtransmission/string-utils.h>
#include <libtransmission/tr-macros.h>
#include <libtransmission/tr-strbuf.h>

#include "test-fixtures.h"

#if !defined(__OpenBSD__)
#define HAVE_UNIFIED_BUFFER_CACHE
#endif

using namespace std::literals;

namespace tr::test
{

class FileTest : public SandboxedTest
{
protected:
    auto createTestDir(std::string_view const test_name) const
    {
        auto test_dir = tr_u8path(sandboxDir()) / tr_u8path(test_name);
        tr_sys_dir_create(test_dir.string(), 0, 0777);
        return test_dir;
    }

    static bool createSymlink(
        std::filesystem::path const& dst_path,
        std::filesystem::path const& src_path,
        bool const dst_is_dir)
    {
        auto ec = std::error_code{};
        if (dst_is_dir)
        {
            std::filesystem::create_directory_symlink(src_path, dst_path, ec);
        }
        else
        {
            std::filesystem::create_symlink(src_path, dst_path, ec);
        }
        return !ec;
    }

    static bool createHardlink(std::filesystem::path const& dst_path, std::filesystem::path const& src_path)
    {
        auto ec = std::error_code{};
        std::filesystem::create_hard_link(src_path, dst_path, ec);
        return !ec;
    }

    static void clearPathInfo(tr_sys_path_info* info)
    {
        *info = {};
    }

    static bool pathContainsNoSymlinks(std::filesystem::path const& path)
    {
        auto subpath = std::filesystem::path{};
        for (auto const& component : path)
        {
            subpath /= component;
            auto const info = tr_sys_path_get_info(subpath.string(), TR_SYS_PATH_NO_FOLLOW);
            if (!info || (!info->isFile() && !info->isFolder()))
            {
                return false;
            }
        }

        return true;
    }

    static bool validatePermissions(
        [[maybe_unused]] std::filesystem::path const& path,
        [[maybe_unused]] unsigned int permissions)
    {
#ifndef _WIN32
        struct stat sb = {};
        return stat(path.c_str(), &sb) != -1 && (sb.st_mode & 0777) == permissions;
#else
        /* No UNIX permissions on Windows */
        return true;
#endif
    }

    struct XnameTestData
    {
        std::string_view input;
        std::string_view output;
    };

    static void testPathXname(
        XnameTestData const* data,
        size_t data_size,
        std::string_view (*func)(std::string_view, tr_error*))
    {
        for (size_t i = 0; i < data_size; ++i)
        {
            auto error = tr_error{};
            auto const& [input, output] = data[i];
            auto const name = func(input, &error);

            if (!std::empty(data[i].output))
            {
                EXPECT_NE(""sv, name);
                EXPECT_FALSE(error) << error;
                EXPECT_EQ(output, name) << " in [" << input << ']';
            }
            else
            {
                EXPECT_EQ(""sv, name) << " in [" << input << ']';
                EXPECT_TRUE(error);
            }
        }
    }

    static void testDirReadImpl(std::filesystem::path const& path, bool* have1, bool* have2)
    {
        *have1 = *have2 = false;

        auto err = tr_error{};
        auto dd = tr_sys_dir_open(path.string(), &err);
        EXPECT_NE(TR_BAD_SYS_DIR, dd);
        EXPECT_FALSE(err) << err;

        for (;;)
        {
            char const* name = tr_sys_dir_read_name(dd, &err);
            if (name == nullptr)
            {
                break;
            }

            EXPECT_FALSE(err) << err;

            if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
            {
                continue;
            }

            if (strcmp(name, "a") == 0)
            {
                *have1 = true;
            }
            else if (strcmp(name, "b") == 0)
            {
                *have2 = true;
            }
            else
            {
                FAIL();
            }
        }

        EXPECT_FALSE(err) << err;

        EXPECT_TRUE(tr_sys_dir_close(dd, &err));
        EXPECT_FALSE(err) << err;
    }
};

TEST_F(FileTest, getInfo)
{
    auto const test_dir = createTestDir(currentTestName());

    auto const path1 = test_dir / u8"a"sv;
    auto const path2 = test_dir / u8"b"sv;

    // Can't get info of non-existent file/directory
    auto err = tr_error{};
    auto info = tr_sys_path_get_info(path1.string(), 0, &err);
    EXPECT_FALSE(info.has_value());
    EXPECT_TRUE(err);
    err = {};

    auto t = time(nullptr);
    createFileWithContents(path1.string(), "test");

    // Good file info
    info = tr_sys_path_get_info(path1.string(), 0, &err);
    EXPECT_TRUE(info.has_value());
    assert(info.has_value());
    EXPECT_FALSE(err) << err;
    EXPECT_EQ(TR_SYS_PATH_IS_FILE, info->type);
    EXPECT_EQ(4U, info->size);
    EXPECT_GE(info->last_modified_at, t - 1);
    EXPECT_LE(info->last_modified_at, time(nullptr) + 1);

    tr_sys_path_remove(path1);

    // Good directory info
    t = time(nullptr);
    tr_sys_dir_create(path1.string(), 0, 0777);
    info = tr_sys_path_get_info(path1.string(), 0, &err);
    EXPECT_TRUE(info.has_value());
    assert(info.has_value());
    EXPECT_FALSE(err) << err;
    EXPECT_EQ(TR_SYS_PATH_IS_DIRECTORY, info->type);
    EXPECT_NE(uint64_t(-1), info->size);
    EXPECT_GE(info->last_modified_at, t - 1);
    EXPECT_LE(info->last_modified_at, time(nullptr) + 1);
    tr_sys_path_remove(path1);

    if (createSymlink(path1, path2, false))
    {
        // Can't get info of non-existent file/directory
        info = tr_sys_path_get_info(path1.string(), 0, &err);
        EXPECT_FALSE(info.has_value());
        EXPECT_TRUE(err);
        err = {};

        t = time(nullptr);
        createFileWithContents(path2.string(), "test");

        // Good file info
        info = tr_sys_path_get_info(path1.string(), 0, &err);
        EXPECT_TRUE(info.has_value());
        assert(info.has_value());
        EXPECT_FALSE(err) << err;
        EXPECT_EQ(TR_SYS_PATH_IS_FILE, info->type);
        EXPECT_EQ(4, info->size);
        EXPECT_GE(info->last_modified_at, t - 1);
        EXPECT_LE(info->last_modified_at, time(nullptr) + 1);

        // Symlink
        info = tr_sys_path_get_info(path1.string(), TR_SYS_PATH_NO_FOLLOW, &err);
        EXPECT_TRUE(info.has_value());
        assert(info.has_value());
        EXPECT_FALSE(err) << err;
        EXPECT_EQ(TR_SYS_PATH_IS_OTHER, info->type);

        tr_sys_path_remove(path2);
        tr_sys_path_remove(path1);

        // Good directory info
        t = time(nullptr);
        tr_sys_dir_create(path2.string(), 0, 0777);
        EXPECT_TRUE(createSymlink(path1, path2, true)); /* Win32: directory and file symlinks differ :( */
        info = tr_sys_path_get_info(path1.string(), 0, &err);
        EXPECT_TRUE(info.has_value());
        assert(info.has_value());
        EXPECT_FALSE(err) << err;
        EXPECT_EQ(TR_SYS_PATH_IS_DIRECTORY, info->type);
        EXPECT_NE(uint64_t(-1), info->size);
        EXPECT_GE(info->last_modified_at, t - 1);
        EXPECT_LE(info->last_modified_at, time(nullptr) + 1);

        tr_sys_path_remove(path2);
        tr_sys_path_remove(path1);
    }
    else
    {
        fmt::print(stderr, "WARNING: [{:s}] unable to run symlink tests\n", __FUNCTION__);
    }
}

TEST_F(FileTest, readFile)
{
    auto const test_dir = createTestDir(currentTestName());

    auto const path = test_dir / u8"a.txt"sv;
    auto constexpr Contents = "hello, world!"sv;
    createFileWithContents(path.string(), Contents);

    auto n_read = uint64_t{};
    auto buf = std::array<char, 64>{};
    auto fd = tr_sys_file_open(path.string(), TR_SYS_FILE_READ, 0);
    EXPECT_NE(TR_BAD_SYS_FILE, fd);

    // successful read
    auto error = tr_error{};
    EXPECT_TRUE(tr_sys_file_read(fd, std::data(buf), std::size(buf), &n_read, &error));
    EXPECT_EQ(Contents, std::string_view(std::data(buf), n_read));
    EXPECT_EQ(std::size(Contents), n_read);
    EXPECT_FALSE(error) << error;

    // successful read_at
    auto const offset = 1U;
    EXPECT_TRUE(tr_sys_file_read_at(fd, std::data(buf), std::size(buf), offset, &n_read, &error));
    auto constexpr Expected = Contents.substr(offset);
    EXPECT_EQ(Expected, std::string_view(std::data(buf), n_read));
    EXPECT_EQ(std::size(Expected), n_read);
    EXPECT_FALSE(error) << error;

    tr_sys_file_close(fd);
}

TEST_F(FileTest, pathExists)
{
    auto const test_dir = createTestDir(currentTestName());

    auto const path1 = test_dir / u8"a"sv;
    auto const path2 = test_dir / u8"b"sv;

    // Non-existent file does not exist
    auto error = tr_error{};
    EXPECT_FALSE(tr_sys_path_exists(path1.string(), &error));
    EXPECT_FALSE(error) << error;

    // Create file and see that it exists
    createFileWithContents(path1.string(), "test");
    EXPECT_TRUE(tr_sys_path_exists(path1.string(), &error));
    EXPECT_FALSE(error) << error;

    tr_sys_path_remove(path1);

    // Create directory and see that it exists
    tr_sys_dir_create(path1.string(), 0, 0777);
    EXPECT_TRUE(tr_sys_path_exists(path1.string(), &error));
    EXPECT_FALSE(error) << error;

    tr_sys_path_remove(path1);

    if (createSymlink(path1, path2, false))
    {
        // Non-existent file does not exist (via symlink)
        EXPECT_FALSE(tr_sys_path_exists(path1.string(), &error));
        EXPECT_FALSE(error) << error;

        // Create file and see that it exists (via symlink)
        createFileWithContents(path2.string(), "test");
        EXPECT_TRUE(tr_sys_path_exists(path1.string(), &error));
        EXPECT_FALSE(error) << error;

        tr_sys_path_remove(path2);
        tr_sys_path_remove(path1);

        /* Create directory and see that it exists (via symlink) */
        tr_sys_dir_create(path2.string(), 0, 0777);
        EXPECT_TRUE(createSymlink(path1, path2, true)); /* Win32: directory and file symlinks differ :( */
        EXPECT_TRUE(tr_sys_path_exists(path1.string(), &error));
        EXPECT_FALSE(error) << error;

        tr_sys_path_remove(path2);
        tr_sys_path_remove(path1);
    }
    else
    {
        fmt::print(stderr, "WARNING: [{:s}] unable to run symlink tests\n", __FUNCTION__);
    }
}

TEST_F(FileTest, pathIsRelative)
{
    EXPECT_TRUE(tr_sys_path_is_relative(""));
    EXPECT_TRUE(tr_sys_path_is_relative("."));
    EXPECT_TRUE(tr_sys_path_is_relative(".."));
    EXPECT_TRUE(tr_sys_path_is_relative("x"));
    EXPECT_TRUE(tr_sys_path_is_relative("\\"));
    EXPECT_TRUE(tr_sys_path_is_relative(":"));

#ifdef _WIN32
    EXPECT_TRUE(tr_sys_path_is_relative("/"));
    EXPECT_TRUE(tr_sys_path_is_relative("\\x"));
    EXPECT_TRUE(tr_sys_path_is_relative("/x"));
    EXPECT_TRUE(tr_sys_path_is_relative("\\x\\y"));
    EXPECT_TRUE(tr_sys_path_is_relative("/x/y"));
    EXPECT_TRUE(tr_sys_path_is_relative("C:x"));
    EXPECT_TRUE(tr_sys_path_is_relative("C:x\\y"));
    EXPECT_TRUE(tr_sys_path_is_relative("C:x/y"));

    EXPECT_FALSE(tr_sys_path_is_relative("\\\\"));
    EXPECT_FALSE(tr_sys_path_is_relative("//"));
    EXPECT_FALSE(tr_sys_path_is_relative("\\\\x"));
    EXPECT_FALSE(tr_sys_path_is_relative("//x"));
    EXPECT_FALSE(tr_sys_path_is_relative("\\\\x\\y"));
    EXPECT_FALSE(tr_sys_path_is_relative("//x/y"));
    EXPECT_FALSE(tr_sys_path_is_relative("\\\\.\\x"));
    EXPECT_FALSE(tr_sys_path_is_relative("//./x"));

    EXPECT_FALSE(tr_sys_path_is_relative("a:"));
    EXPECT_FALSE(tr_sys_path_is_relative("a:\\"));
    EXPECT_FALSE(tr_sys_path_is_relative("a:/"));
    EXPECT_FALSE(tr_sys_path_is_relative("Z:"));
    EXPECT_FALSE(tr_sys_path_is_relative("Z:\\"));
    EXPECT_FALSE(tr_sys_path_is_relative("Z:/"));
#else /* _WIN32 */
    EXPECT_FALSE(tr_sys_path_is_relative("/"));
    EXPECT_FALSE(tr_sys_path_is_relative("/x"));
    EXPECT_FALSE(tr_sys_path_is_relative("/x/y"));
    EXPECT_FALSE(tr_sys_path_is_relative("//x"));
#endif /* _WIN32 */
}

TEST_F(FileTest, pathIsSame)
{
    // NOLINTBEGIN(readability-suspicious-call-argument)

    auto const test_dir = createTestDir(currentTestName());

    auto const path1 = test_dir / u8"a"sv;
    auto const path2 = test_dir / u8"b"sv;
    auto path3 = path2 / u8"c"sv;

    /* Two non-existent files are not the same */
    auto error = tr_error{};
    EXPECT_FALSE(tr_sys_path_is_same(path1.string(), path1.string(), &error));
    EXPECT_FALSE(error) << error;
    EXPECT_FALSE(tr_sys_path_is_same(path1.string(), path2.string(), &error));
    EXPECT_FALSE(error) << error;

    /* Two same files are the same */
    createFileWithContents(path1.string(), "test");
    EXPECT_TRUE(tr_sys_path_is_same(path1.string(), path1.string(), &error));
    EXPECT_FALSE(error) << error;

    /* Existent and non-existent files are not the same */
    EXPECT_FALSE(tr_sys_path_is_same(path1.string(), path2.string(), &error));
    EXPECT_FALSE(error) << error;
    EXPECT_FALSE(tr_sys_path_is_same(path2.string(), path1.string(), &error));
    EXPECT_FALSE(error) << error;

    /* Two separate files (even with same content) are not the same */
    createFileWithContents(path2.string(), "test");
    EXPECT_FALSE(tr_sys_path_is_same(path1.string(), path2.string(), &error));
    EXPECT_FALSE(error) << error;

    tr_sys_path_remove(path1);

    /* Two same directories are the same */
    tr_sys_dir_create(path1.string(), 0, 0777);
    EXPECT_TRUE(tr_sys_path_is_same(path1.string(), path1.string(), &error));
    EXPECT_FALSE(error) << error;

    /* File and directory are not the same */
    EXPECT_FALSE(tr_sys_path_is_same(path1.string(), path2.string(), &error));
    EXPECT_FALSE(error) << error;
    EXPECT_FALSE(tr_sys_path_is_same(path2.string(), path1.string(), &error));
    EXPECT_FALSE(error) << error;

    tr_sys_path_remove(path2);

    /* Two separate directories are not the same */
    tr_sys_dir_create(path2.string(), 0, 0777);
    EXPECT_FALSE(tr_sys_path_is_same(path1.string(), path2.string(), &error));
    EXPECT_FALSE(error) << error;

    tr_sys_path_remove(path1);
    tr_sys_path_remove(path2);

    if (createSymlink(path1, u8"."sv, true))
    {
        /* Directory and symlink pointing to it are the same */
        EXPECT_TRUE(tr_sys_path_is_same(path1.string(), test_dir.string(), &error));
        EXPECT_FALSE(error) << error;
        EXPECT_TRUE(tr_sys_path_is_same(test_dir.string(), path1.string(), &error));
        EXPECT_FALSE(error) << error;

        /* Non-existent file and symlink are not the same */
        EXPECT_FALSE(tr_sys_path_is_same(path1.string(), path2.string(), &error));
        EXPECT_FALSE(error) << error;
        EXPECT_FALSE(tr_sys_path_is_same(path2.string(), path1.string(), &error));
        EXPECT_FALSE(error) << error;

        /* Symlinks pointing to different directories are not the same */
        createSymlink(path2, u8".."sv, true);
        EXPECT_FALSE(tr_sys_path_is_same(path1.string(), path2.string(), &error));
        EXPECT_FALSE(error) << error;
        EXPECT_FALSE(tr_sys_path_is_same(path2.string(), path1.string(), &error));
        EXPECT_FALSE(error) << error;

        tr_sys_path_remove(path2);

        /* Symlinks pointing to same directory are the same */
        createSymlink(path2, u8"."sv, true);
        EXPECT_TRUE(tr_sys_path_is_same(path1.string(), path2.string(), &error));
        EXPECT_FALSE(error) << error;

        tr_sys_path_remove(path2);

        /* Directory and symlink pointing to another directory are not the same */
        tr_sys_dir_create(path2.string(), 0, 0777);
        EXPECT_FALSE(tr_sys_path_is_same(path1.string(), path2.string(), &error));
        EXPECT_FALSE(error) << error;
        EXPECT_FALSE(tr_sys_path_is_same(path2.string(), path1.string(), &error));
        EXPECT_FALSE(error) << error;

        /* Symlinks pointing to same directory are the same */
        createSymlink(path3, u8".."sv, true);
        EXPECT_TRUE(tr_sys_path_is_same(path1.string(), path3.string(), &error));
        EXPECT_FALSE(error) << error;

        tr_sys_path_remove(path1);

        /* File and symlink pointing to directory are not the same */
        createFileWithContents(path1.string(), "test");
        EXPECT_FALSE(tr_sys_path_is_same(path1.string(), path3.string(), &error));
        EXPECT_FALSE(error) << error;
        EXPECT_FALSE(tr_sys_path_is_same(path3.string(), path1.string(), &error));
        EXPECT_FALSE(error) << error;

        tr_sys_path_remove(path3);

        /* File and symlink pointing to same file are the same */
        createSymlink(path3, path1, false);
        EXPECT_TRUE(tr_sys_path_is_same(path1.string(), path3.string(), &error));
        EXPECT_FALSE(error) << error;
        EXPECT_TRUE(tr_sys_path_is_same(path3.string(), path1.string(), &error));
        EXPECT_FALSE(error) << error;

        /* Symlinks pointing to non-existent files are not the same */
        tr_sys_path_remove(path1);
        createSymlink(path1, u8"missing"sv, false);
        tr_sys_path_remove(path3);
        createSymlink(path3, u8"missing"sv, false);
        EXPECT_FALSE(tr_sys_path_is_same(path1.string(), path3.string(), &error));
        EXPECT_FALSE(error) << error;
        EXPECT_FALSE(tr_sys_path_is_same(path3.string(), path1.string(), &error));
        EXPECT_FALSE(error) << error;

        tr_sys_path_remove(path3);

        /* Symlinks pointing to same non-existent file are not the same */
        createSymlink(path3, u8"../missing"sv, false);
        EXPECT_FALSE(tr_sys_path_is_same(path1.string(), path3.string(), &error));
        EXPECT_FALSE(error) << error;
        EXPECT_FALSE(tr_sys_path_is_same(path3.string(), path1.string(), &error));
        EXPECT_FALSE(error) << error;

        /* Non-existent file and symlink pointing to non-existent file are not the same */
        tr_sys_path_remove(path3);
        EXPECT_FALSE(tr_sys_path_is_same(path1.string(), path3.string(), &error));
        EXPECT_FALSE(error) << error;
        EXPECT_FALSE(tr_sys_path_is_same(path3.string(), path1.string(), &error));
        EXPECT_FALSE(error) << error;

        tr_sys_path_remove(path2);
        tr_sys_path_remove(path1);
    }
    else
    {
        fmt::print(stderr, "WARNING: [{:s}] unable to run symlink tests\n", __FUNCTION__);
    }

    path3 = test_dir / u8"c"sv;
    ;

    createFileWithContents(path1.string(), "test");

    if (createHardlink(path2, path1))
    {
        /* File and hardlink to it are the same */
        EXPECT_TRUE(tr_sys_path_is_same(path1.string(), path2.string(), &error));
        EXPECT_FALSE(error) << error;

        /* Two hardlinks to the same file are the same */
        createHardlink(path3, path2);
        EXPECT_TRUE(tr_sys_path_is_same(path2.string(), path3.string(), &error));
        EXPECT_FALSE(error) << error;
        EXPECT_TRUE(tr_sys_path_is_same(path1.string(), path3.string(), &error));
        EXPECT_FALSE(error) << error;

        tr_sys_path_remove(path2);

        EXPECT_TRUE(tr_sys_path_is_same(path1.string(), path3.string(), &error));
        EXPECT_FALSE(error) << error;

        tr_sys_path_remove(path3);

        /* File and hardlink to another file are not the same */
        createFileWithContents(path3.string(), "test");
        createHardlink(path2, path3);
        EXPECT_FALSE(tr_sys_path_is_same(path1.string(), path2.string(), &error));
        EXPECT_FALSE(error) << error;
        EXPECT_FALSE(tr_sys_path_is_same(path2.string(), path1.string(), &error));
        EXPECT_FALSE(error) << error;

        tr_sys_path_remove(path3);
        tr_sys_path_remove(path2);
    }
    else
    {
        fmt::print(stderr, "WARNING: [{:s}] unable to run symlink tests\n", __FUNCTION__);
    }

    if (createSymlink(path2, path1, false) && createHardlink(path3, path1))
    {
        EXPECT_TRUE(tr_sys_path_is_same(path2.string(), path3.string(), &error));
        EXPECT_FALSE(error) << error;
    }
    else
    {
        fmt::print(stderr, "WARNING: [{:s}] unable to run combined symlink and hardlink tests\n", __FUNCTION__);
    }

    tr_sys_path_remove(path3);
    tr_sys_path_remove(path2);
    tr_sys_path_remove(path1);

    // NOLINTEND(readability-suspicious-call-argument)
}

TEST_F(FileTest, pathResolve)
{
    auto const test_dir = createTestDir(currentTestName());

    auto error = tr_error{};
    auto const path1 = test_dir / u8"a"sv;
    auto const path2 = test_dir / u8"b"sv;

    createFileWithContents(path1.string(), "test");

    if (createSymlink(path2, path1, false))
    {
        auto resolved = tr_u8path(tr_sys_path_resolve(path2.string(), &error));
        EXPECT_FALSE(error) << error;
        EXPECT_TRUE(pathContainsNoSymlinks(resolved));

        tr_sys_path_remove(path2);
        tr_sys_path_remove(path1);

        tr_sys_dir_create(path1.string(), 0, 0755);
        EXPECT_TRUE(createSymlink(path2, path1, true)); /* Win32: directory and file symlinks differ :( */
        resolved = tr_u8path(tr_sys_path_resolve(path2.string(), &error));
        EXPECT_FALSE(error) << error;
        EXPECT_TRUE(pathContainsNoSymlinks(resolved));
    }
    else
    {
        fmt::print(stderr, "WARNING: [{:s}] unable to run symlink tests\n", __FUNCTION__);
    }

    tr_sys_path_remove(path2);
    tr_sys_path_remove(path1);

#ifdef _WIN32
    auto resolved = tr_sys_path_resolve(R"(\\127.0.0.1\NonExistent)"sv, &error);
    EXPECT_EQ(""sv, resolved);
    EXPECT_TRUE(error);
    error = {};

    resolved = tr_sys_path_resolve(R"(\\127.0.0.1\ADMIN$\NonExistent)"sv, &error);
    EXPECT_EQ(""sv, resolved);
    EXPECT_TRUE(error);
    error = {};

    for (auto const& input : { R"(\\127.0.0.1\ADMIN$\System32)"sv,
                               R"(\\127.0.0.1\ADMIN$\\System32)"sv,
                               R"(\\127.0.0.1\\ADMIN$\System32)"sv,
                               R"(\\127.0.0.1\\ADMIN$\\System32)"sv,
                               R"(\\127.0.0.1\ADMIN$/System32)"sv })
    {
        resolved = tr_sys_path_resolve(input, &error);
        EXPECT_EQ(R"(\\127.0.0.1\ADMIN$\System32)"sv, resolved);
        EXPECT_FALSE(error) << error;
    }
#endif
}

TEST_F(FileTest, pathBasename)
{
    auto const common_xname_tests = std::vector<XnameTestData>{
        XnameTestData{ .input = "/", .output = "/" },
        { .input = "", .output = "." },
#ifdef _WIN32
        { .input = "\\", .output = "/" },
        /* Invalid paths */
        { .input = R"(\\\)", .output = "" },
        { .input = "123:", .output = "" },
        /* Reserved characters */
        { .input = "<", .output = "" },
        { .input = ">", .output = "" },
        { .input = ":", .output = "" },
        { .input = "\"", .output = "" },
        { .input = "|", .output = "" },
        { .input = "?", .output = "" },
        { .input = "*", .output = "" },
        { .input = "a\\<", .output = "" },
        { .input = "a\\>", .output = "" },
        { .input = "a\\:", .output = "" },
        { .input = "a\\\"", .output = "" },
        { .input = "a\\|", .output = "" },
        { .input = "a\\?", .output = "" },
        { .input = "a\\*", .output = "" },
        { .input = R"(c:\a\b<c\d)", .output = "" },
        { .input = R"(c:\a\b>c\d)", .output = "" },
        { .input = R"(c:\a\b:c\d)", .output = "" },
        { .input = R"(c:\a\b"c\d)", .output = "" },
        { .input = R"(c:\a\b|c\d)", .output = "" },
        { .input = R"(c:\a\b?c\d)", .output = "" },
        { .input = R"(c:\a\b*c\d)", .output = "" },
#else
        { .input = "////", .output = "/" },
#endif
    };

    testPathXname(common_xname_tests.data(), common_xname_tests.size(), tr_sys_path_basename);
    // testPathXname(common_xname_tests.data(), common_xname_tests.size(), tr_sys_path_dirname);

    auto const basename_tests = std::vector<XnameTestData>{
        XnameTestData{ .input = "a", .output = "a" },
        { .input = "aa", .output = "aa" },
        { .input = "/aa", .output = "aa" },
        { .input = "/a/b/c", .output = "c" },
        { .input = "/a/b/c/", .output = "c" },
#ifdef _WIN32
        { .input = R"(c:\a\b\c)", .output = "c" },
        { .input = "c:", .output = "/" },
        { .input = "c:/", .output = "/" },
        { .input = "c:\\", .output = "/" },
        { .input = "c:a/b", .output = "b" },
        { .input = "c:a", .output = "a" },
        { .input = R"(\\a\b\c)", .output = "c" },
        { .input = "//a/b", .output = "b" },
        { .input = "//1.2.3.4/b", .output = "b" },
        { .input = "\\\\a", .output = "a" },
        { .input = "\\\\1.2.3.4", .output = "1.2.3.4" },
        { .input = "\\", .output = "/" },
        { .input = "\\a", .output = "a" },
#endif
    };

    testPathXname(basename_tests.data(), basename_tests.size(), tr_sys_path_basename);
}

TEST_F(FileTest, pathDirname)
{
#ifdef _WIN32
    static auto constexpr DirnameTests = std::array<std::pair<std::string_view, std::string_view>, 48>{ {
        { R"(C:\a/b\c)"sv, R"(C:\a/b)"sv },
        { R"(C:\a/b\c\)"sv, R"(C:\a/b)"sv },
        { R"(C:\a/b)"sv, R"(C:\a)"sv },
        { "C:/a"sv, "C:/"sv },
        { "C:"sv, "C:"sv },
        { "C:/"sv, "C:/"sv },
        { "c:a/b"sv, "c:a"sv },
        { "c:a"sv, "c:"sv },
        { R"(\\a)"sv, R"(\)"sv },
        { R"(\\1.2.3.4)"sv, R"(\)"sv },
        { R"(\\)"sv, R"(\)"sv },
        { R"(a/b\c)"sv, "a/b"sv },
        // taken from Node.js unit tests
        // https://github.com/nodejs/node/blob/e46c680bf2b211bbd52cf959ca17ee98c7f657f5/test/parallel/test-path-dirname.js
        { R"(c:\)"sv, R"(c:\)"sv },
        { R"(c:\foo)"sv, R"(c:\)"sv },
        { R"(c:\foo\)"sv, R"(c:\)"sv },
        { R"(c:\foo\bar)"sv, R"(c:\foo)"sv },
        { R"(c:\foo\bar\)"sv, R"(c:\foo)"sv },
        { R"(c:\foo\bar\baz)"sv, R"(c:\foo\bar)"sv },
        { R"(c:\foo bar\baz)"sv, R"(c:\foo bar)"sv },
        { R"(\)"sv, R"(\)"sv },
        { R"(\foo)"sv, R"(\)"sv },
        { R"(\foo\)"sv, R"(\)"sv },
        { R"(\foo\bar)"sv, R"(\foo)"sv },
        { R"(\foo\bar\)"sv, R"(\foo)"sv },
        { R"(\foo\bar\baz)"sv, R"(\foo\bar)"sv },
        { R"(\foo bar\baz)"sv, R"(\foo bar)"sv },
        { "c:"sv, "c:"sv },
        { "c:foo"sv, "c:"sv },
        { R"(c:foo\)"sv, "c:"sv },
        { R"(c:foo\bar)"sv, "c:foo"sv },
        { R"(c:foo\bar\)"sv, "c:foo"sv },
        { R"(c:foo\bar\baz)"sv, R"(c:foo\bar)"sv },
        { R"(c:foo bar\baz)"sv, "c:foo bar"sv },
        { "file:stream"sv, "."sv },
        { R"(dir\file:stream)"sv, "dir"sv },
        { R"(\\unc\share)"sv, R"(\\unc\share)"sv },
        { R"(\\unc\share\foo)"sv, R"(\\unc\share\)"sv },
        { R"(\\unc\share\foo\)"sv, R"(\\unc\share\)"sv },
        { R"(\\unc\share\foo\bar)"sv, R"(\\unc\share\foo)"sv },
        { R"(\\unc\share\foo\bar\)"sv, R"(\\unc\share\foo)"sv },
        { R"(\\unc\share\foo\bar\baz)"sv, R"(\\unc\share\foo\bar)"sv },
        { "/a/b/"sv, "/a"sv },
        { "/a/b"sv, "/a"sv },
        { "/a"sv, "/"sv },
        { ""sv, "."sv },
        { "/"sv, "/"sv },
        { "////"sv, "/"sv },
        { "foo"sv, "."sv },
    } };
#else
    static auto constexpr DirnameTests = std::array<std::pair<std::string_view, std::string_view>, 15>{ {
        // taken from Node.js unit tests
        // https://github.com/nodejs/node/blob/e46c680bf2b211bbd52cf959ca17ee98c7f657f5/test/parallel/test-path-dirname.js
        { "/a/b/"sv, "/a"sv },
        { "/a/b"sv, "/a"sv },
        { "/a"sv, "/"sv },
        { ""sv, "."sv },
        { "/"sv, "/"sv },
        { "////"sv, "/"sv },
        { "//a"sv, "//"sv },
        { "foo"sv, "."sv },
        // taken from dirname(3) manpage
        { "usr"sv, "."sv },
        { "/usr/lib", "/usr"sv },
        { "/usr/"sv, "/"sv },
        { "/usr/"sv, "/"sv },
        { "/"sv, "/"sv },
        { "."sv, "."sv },
        { ".."sv, "."sv },
    } };
#endif

    for (auto const& [input, expected] : DirnameTests)
    {
        EXPECT_EQ(expected, tr_sys_path_dirname(input))
            << "input[" << input << "] expected [" << expected << "] actual [" << tr_sys_path_dirname(input) << "]\n";
    }

    /* TODO: is_same(dirname(x) + '/' + basename(x), x) */
}

TEST_F(FileTest, pathRename)
{
    auto const test_dir = createTestDir(currentTestName());

    auto const path1 = test_dir / u8"a"sv;
    auto const path2 = test_dir / u8"b"sv;
    auto path3 = path2 / u8"c"sv;

    createFileWithContents(path1.string(), "test");

    /* Preconditions */
    EXPECT_TRUE(tr_sys_path_exists(path1.string()));
    EXPECT_FALSE(tr_sys_path_exists(path2.string()));

    /* Forward rename works */
    auto error = tr_error{};
    EXPECT_TRUE(tr_sys_path_rename(path1.string(), path2.string(), &error));
    EXPECT_FALSE(tr_sys_path_exists(path1.string()));
    EXPECT_TRUE(tr_sys_path_exists(path2.string()));
    EXPECT_FALSE(error) << error;

    /* Backward rename works */
    EXPECT_TRUE(tr_sys_path_rename(path2.string(), path1.string(), &error));
    EXPECT_TRUE(tr_sys_path_exists(path1.string()));
    EXPECT_FALSE(tr_sys_path_exists(path2.string()));
    EXPECT_FALSE(error) << error;

    /* Another backward rename [of non-existent file] does not work */
    EXPECT_FALSE(tr_sys_path_rename(path2.string(), path1.string(), &error));
    EXPECT_TRUE(error);
    error = {};

    /* Rename to file which couldn't be created does not work */
    EXPECT_FALSE(tr_sys_path_rename(path1.string(), path3.string(), &error));
    EXPECT_TRUE(error);
    error = {};

    /* Rename of non-existent file does not work */
    EXPECT_FALSE(tr_sys_path_rename(path3.string(), path2.string(), &error));
    EXPECT_TRUE(error);
    error = {};

    createFileWithContents(path2.string(), "test");

    /* Renaming file does overwrite existing file */
    EXPECT_TRUE(tr_sys_path_rename(path2.string(), path1.string(), &error));
    EXPECT_FALSE(error) << error;

    tr_sys_dir_create(path2.string(), 0, 0777);

    /* Renaming file does not overwrite existing directory, and vice versa */
    EXPECT_FALSE(tr_sys_path_rename(path1.string(), path2.string(), &error));
    EXPECT_TRUE(error);
    error = {};
    EXPECT_FALSE(tr_sys_path_rename(path2.string(), path1.string(), &error));
    EXPECT_TRUE(error);
    error = {};

    tr_sys_path_remove(path2);

    path3 = test_dir / u8"c"sv;

    if (createSymlink(path2, path1, false))
    {
        /* Preconditions */
        EXPECT_TRUE(tr_sys_path_exists(path2.string()));
        EXPECT_FALSE(tr_sys_path_exists(path3.string()));
        EXPECT_TRUE(tr_sys_path_is_same(path1.string(), path2.string()));

        /* Rename of symlink works, files stay the same */
        EXPECT_TRUE(tr_sys_path_rename(path2.string(), path3.string(), &error));
        EXPECT_FALSE(error) << error;
        EXPECT_FALSE(tr_sys_path_exists(path2.string()));
        EXPECT_TRUE(tr_sys_path_exists(path3.string()));
        EXPECT_TRUE(tr_sys_path_is_same(path1.string(), path3.string()));

        tr_sys_path_remove(path3);
    }
    else
    {
        fmt::print(stderr, "WARNING: [{:s}] unable to run symlink tests\n", __FUNCTION__);
    }

    if (createHardlink(path2, path1))
    {
        /* Preconditions */
        EXPECT_TRUE(tr_sys_path_exists(path2.string()));
        EXPECT_FALSE(tr_sys_path_exists(path3.string()));
        EXPECT_TRUE(tr_sys_path_is_same(path1.string(), path2.string()));

        /* Rename of hardlink works, files stay the same */
        EXPECT_TRUE(tr_sys_path_rename(path2.string(), path3.string(), &error));
        EXPECT_FALSE(error) << error;
        EXPECT_FALSE(tr_sys_path_exists(path2.string()));
        EXPECT_TRUE(tr_sys_path_exists(path3.string()));
        EXPECT_TRUE(tr_sys_path_is_same(path1.string(), path3.string()));

        tr_sys_path_remove(path3);
    }
    else
    {
        fmt::print(stderr, "WARNING: [{:s}] unable to run hardlink tests\n", __FUNCTION__);
    }

    tr_sys_path_remove(path1);
}

TEST_F(FileTest, pathRemove)
{
    auto const test_dir = createTestDir(currentTestName());

    auto const path1 = test_dir / u8"a"sv;
    auto const path2 = test_dir / u8"b"sv;
    auto const path3 = path2 / u8"c"sv;

    /* Can't remove non-existent file/directory */
    EXPECT_FALSE(tr_sys_path_exists(path1.string()));
    auto error = tr_error{};
    EXPECT_FALSE(tr_sys_path_remove(path1, &error));
    EXPECT_TRUE(error);
    EXPECT_FALSE(tr_sys_path_exists(path1.string()));
    error = {};

    /* Removing file works */
    createFileWithContents(path1.string(), "test");
    EXPECT_TRUE(tr_sys_path_exists(path1.string()));
    EXPECT_TRUE(tr_sys_path_remove(path1, &error));
    EXPECT_FALSE(error) << error;
    EXPECT_FALSE(tr_sys_path_exists(path1.string()));

    /* Removing empty directory works */
    tr_sys_dir_create(path1.string(), 0, 0777);
    EXPECT_TRUE(tr_sys_path_exists(path1.string()));
    EXPECT_TRUE(tr_sys_path_remove(path1, &error));
    EXPECT_FALSE(error) << error;
    EXPECT_FALSE(tr_sys_path_exists(path1.string()));

    /* Removing non-empty directory fails */
    tr_sys_dir_create(path2.string(), 0, 0777);
    createFileWithContents(path3.string(), "test");
    EXPECT_TRUE(tr_sys_path_exists(path2.string()));
    EXPECT_TRUE(tr_sys_path_exists(path3.string()));
    EXPECT_FALSE(tr_sys_path_remove(path2, &error));
    EXPECT_TRUE(error);
    EXPECT_TRUE(tr_sys_path_exists(path2.string()));
    EXPECT_TRUE(tr_sys_path_exists(path3.string()));
    error = {};

    tr_sys_path_remove(path3);
    tr_sys_path_remove(path2);
}

TEST_F(FileTest, pathNativeSeparators)
{
    EXPECT_EQ(nullptr, tr_sys_path_native_separators(nullptr));

    static auto constexpr Tests = std::array<std::pair<std::string_view, std::string_view>, 5>{ {
        { "", "" },
        { "a", TR_IF_WIN32("a", "a") },
        { "/", TR_IF_WIN32("\\", "/") },
        { "/a/b/c", TR_IF_WIN32("\\a\\b\\c", "/a/b/c") },
        { "C:\\a/b\\c", TR_IF_WIN32("C:\\a\\b\\c", "C:\\a/b\\c") },
    } };

    for (auto const& [input, expected] : Tests)
    {
        auto buf = tr_pathbuf{ input };
        EXPECT_EQ(expected, tr_sys_path_native_separators(std::data(buf)));
    }
}

TEST_F(FileTest, fileCopy)
{
    auto const test_dir = createTestDir(currentTestName());

    auto const path1 = test_dir / u8"a.txt"sv;
    auto const path2 = test_dir / u8"b.txt"sv;
    auto constexpr Contents = "hello, world!"sv;

    // no source file
    auto error = tr_error{};
    EXPECT_FALSE(tr_sys_path_copy(path1, path2, &error));
    EXPECT_TRUE(error);
    error = {};

    createFileWithContents(path1.string(), Contents);

    // source file exists but is inaccessible
    (void)chmod(path1.string().c_str(), 0);
    EXPECT_FALSE(tr_sys_path_copy(path1, test_dir, &error));
    EXPECT_TRUE(error);
    error = {};
    (void)chmod(path1.string().c_str(), 0600);

    // source file exists but target is invalid
    EXPECT_FALSE(tr_sys_path_copy(path1, test_dir, &error));
    EXPECT_TRUE(error);
    error = {};

    // source and target are valid
    createFileWithContents(path1.string(), Contents);
    EXPECT_TRUE(tr_sys_path_copy(path1, path2, &error));
    EXPECT_FALSE(error) << error;
}

TEST_F(FileTest, fileOpen)
{
    auto const test_dir = createTestDir(currentTestName());

    // can't open non-existent file
    auto const path1 = test_dir / u8"a"sv;
    EXPECT_FALSE(tr_sys_path_exists(path1.string()));
    auto error = tr_error{};
    EXPECT_EQ(TR_BAD_SYS_FILE, tr_sys_file_open(path1.string(), TR_SYS_FILE_READ, 0600, &error));
    EXPECT_TRUE(error);
    EXPECT_FALSE(tr_sys_path_exists(path1.string()));
    error = {};
    EXPECT_EQ(TR_BAD_SYS_FILE, tr_sys_file_open(path1.string(), TR_SYS_FILE_WRITE, 0600, &error));
    EXPECT_TRUE(error);
    EXPECT_FALSE(tr_sys_path_exists(path1.string()));
    error = {};

    // can't open directory
    tr_sys_dir_create(path1.string(), 0, 0777);
#ifdef _WIN32
    // this works on *NIX
    EXPECT_EQ(TR_BAD_SYS_FILE, tr_sys_file_open(path1.string(), TR_SYS_FILE_READ, 0600, &error));
    EXPECT_TRUE(error);
    error = {};
#endif
    EXPECT_EQ(TR_BAD_SYS_FILE, tr_sys_file_open(path1.string(), TR_SYS_FILE_WRITE, 0600, &error));
    EXPECT_TRUE(error);
    error = {};

    tr_sys_path_remove(path1);

    // can create non-existent file
    auto fd = tr_sys_file_open(path1.string(), TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE, 0640, &error);
    EXPECT_NE(TR_BAD_SYS_FILE, fd);
    EXPECT_FALSE(error) << error;
    tr_sys_file_close(fd);
    EXPECT_TRUE(tr_sys_path_exists(path1.string()));
    EXPECT_TRUE(validatePermissions(path1, 0640));

    // can open existing file
    EXPECT_TRUE(tr_sys_path_exists(path1.string()));
    fd = tr_sys_file_open(path1.string(), TR_SYS_FILE_READ, 0600, &error);
    EXPECT_NE(TR_BAD_SYS_FILE, fd);
    EXPECT_FALSE(error) << error;
    tr_sys_file_close(fd);
    fd = tr_sys_file_open(path1.string(), TR_SYS_FILE_WRITE, 0600, &error);
    EXPECT_NE(TR_BAD_SYS_FILE, fd);
    EXPECT_FALSE(error) << error;
    tr_sys_file_close(fd);

    tr_sys_path_remove(path1);
    createFileWithContents(path1.string(), "test");

    /* File gets truncated */
    auto info = tr_sys_path_get_info(path1.string(), TR_SYS_PATH_NO_FOLLOW);
    EXPECT_TRUE(info.has_value());
    assert(info.has_value());
    EXPECT_EQ(4U, info->size);
    fd = tr_sys_file_open(path1.string(), TR_SYS_FILE_WRITE | TR_SYS_FILE_TRUNCATE, 0600, &error);
    EXPECT_NE(TR_BAD_SYS_FILE, fd);
    EXPECT_FALSE(error) << error;
    info = tr_sys_path_get_info(path1.string());
    EXPECT_TRUE(info.has_value());
    assert(info.has_value());
    EXPECT_EQ(0U, info->size);
    tr_sys_file_close(fd);
    info = tr_sys_path_get_info(path1.string(), TR_SYS_PATH_NO_FOLLOW);
    EXPECT_TRUE(info.has_value());
    assert(info.has_value());
    EXPECT_EQ(0U, info->size);

    /* TODO: symlink and hardlink tests */

    tr_sys_path_remove(path1);
}

TEST_F(FileTest, fileTruncate)
{
    auto const test_dir = createTestDir(currentTestName());

    auto const path = test_dir / u8"a"sv;
    auto fd = tr_sys_file_open(path.string(), TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE, 0600);

    auto error = tr_error{};
    EXPECT_TRUE(tr_sys_file_truncate(fd, 10, &error));
    EXPECT_FALSE(error) << error;
    auto info = tr_sys_path_get_info(path.string());
    EXPECT_TRUE(info.has_value());
    assert(info.has_value());
    EXPECT_EQ(10U, info->size);

    EXPECT_TRUE(tr_sys_file_truncate(fd, 20, &error));
    EXPECT_FALSE(error) << error;
    info = tr_sys_path_get_info(path.string());
    EXPECT_TRUE(info.has_value());
    assert(info.has_value());
    EXPECT_EQ(20U, info->size);

    EXPECT_TRUE(tr_sys_file_truncate(fd, 0, &error));
    EXPECT_FALSE(error) << error;
    info = tr_sys_path_get_info(path.string());
    EXPECT_TRUE(info.has_value());
    assert(info.has_value());
    EXPECT_EQ(0U, info->size);

    EXPECT_TRUE(tr_sys_file_truncate(fd, 50, &error));
    EXPECT_FALSE(error) << error;

    tr_sys_file_close(fd);

    info = tr_sys_path_get_info(path.string());
    EXPECT_TRUE(info.has_value());
    assert(info.has_value());
    EXPECT_EQ(50U, info->size);

    fd = tr_sys_file_open(path.string(), TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE, 0600);

    EXPECT_TRUE(tr_sys_file_truncate(fd, 25, &error));
    EXPECT_FALSE(error) << error;

    tr_sys_file_close(fd);

    info = tr_sys_path_get_info(path.string());
    EXPECT_TRUE(info.has_value());
    assert(info.has_value());
    EXPECT_EQ(25U, info->size);

    // try to truncate a closed file
    EXPECT_FALSE(tr_sys_file_truncate(fd, 10, &error)); // coverity[USE_AFTER_FREE]
    EXPECT_TRUE(error);
    error = {};

    tr_sys_path_remove(path);
}

TEST_F(FileTest, filePreallocate)
{
    auto const test_dir = createTestDir(currentTestName());

    auto const path1 = test_dir / u8"a"sv;
    auto fd = tr_sys_file_open(path1.string(), TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE, 0600);

    auto error = tr_error{};
    auto prealloc_size = size_t{ 50 };
    if (tr_sys_file_preallocate(fd, prealloc_size, 0, &error))
    {
        EXPECT_FALSE(error) << error;
        auto info = tr_sys_path_get_info(path1.string());
        EXPECT_TRUE(info.has_value());
        assert(info.has_value());
        EXPECT_EQ(prealloc_size, info->size);
    }
    else
    {
        EXPECT_TRUE(error);
        fmt::print(
            stderr,
            "WARNING: [{:s}] unable to preallocate file (full): {:s} ({:d})\n",
            __FUNCTION__,
            error.message(),
            error.code());
        error = {};
    }

    tr_sys_file_close(fd);

    tr_sys_path_remove(path1);

    fd = tr_sys_file_open(path1.string(), TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE, 0600);

    prealloc_size = size_t{ 500U } * 1024U * 1024U;
    if (tr_sys_file_preallocate(fd, prealloc_size, TR_SYS_FILE_PREALLOC_SPARSE, &error))
    {
        EXPECT_FALSE(error) << error;
        auto info = tr_sys_path_get_info(path1.string());
        EXPECT_TRUE(info.has_value());
        assert(info.has_value());
        EXPECT_EQ(prealloc_size, info->size);
    }
    else
    {
        EXPECT_TRUE(error);
        fmt::print(
            stderr,
            "WARNING: [{:s}] unable to preallocate file (sparse): {:s} ({:d})\n",
            __FUNCTION__,
            error.message(),
            error.code());
        error = {};
    }

    tr_sys_file_close(fd);

    tr_sys_path_remove(path1);
}

TEST_F(FileTest, dirCreate)
{
    auto const test_dir = createTestDir(currentTestName());

    auto const path1 = test_dir / u8"a"sv;
    auto const path2 = path1 / u8"b"sv;

    // Can create directory which has parent
    auto error = tr_error{};
    EXPECT_TRUE(tr_sys_dir_create(path1.string(), 0, 0700, &error));
    EXPECT_FALSE(error) << error;
    EXPECT_TRUE(tr_sys_path_exists(path1.string()));
    EXPECT_TRUE(validatePermissions(path1, 0700));

    tr_sys_path_remove(path1);
    createFileWithContents(path1.string(), "test");

    // Can't create directory where file already exists
    EXPECT_FALSE(tr_sys_dir_create(path1.string(), 0, 0700, &error));
    EXPECT_TRUE(error);
    error = {};
    EXPECT_FALSE(tr_sys_dir_create(path1.string(), TR_SYS_DIR_CREATE_PARENTS, 0700, &error));
    EXPECT_TRUE(error);
    error = {};

    tr_sys_path_remove(path1);

    // Can't create directory which has no parent
    EXPECT_FALSE(tr_sys_dir_create(path2.string(), 0, 0700, &error));
    EXPECT_TRUE(error);
    EXPECT_FALSE(tr_sys_path_exists(path2.string()));
    error = {};

    // Can create directory with parent directories
    EXPECT_TRUE(tr_sys_dir_create(path2.string(), TR_SYS_DIR_CREATE_PARENTS, 0751, &error));
    EXPECT_FALSE(error) << error;
    EXPECT_TRUE(tr_sys_path_exists(path1.string()));
    EXPECT_TRUE(tr_sys_path_exists(path2.string()));
    EXPECT_TRUE(validatePermissions(path1, 0751));
    EXPECT_TRUE(validatePermissions(path2, 0751));

    // Can create existing directory (no-op)
    EXPECT_TRUE(tr_sys_dir_create(path1.string(), 0, 0700, &error));
    EXPECT_FALSE(error) << error;
    EXPECT_TRUE(tr_sys_dir_create(path1.string(), TR_SYS_DIR_CREATE_PARENTS, 0700, &error));
    EXPECT_FALSE(error) << error;

    tr_sys_path_remove(path2);
    tr_sys_path_remove(path1);
}

TEST_F(FileTest, dirCreateTemp)
{
    auto const test_dir = createTestDir(currentTestName());

    auto error = tr_error{};
    auto path = tr_pathbuf{ test_dir.string(), "/test-XXXXXX" };
    EXPECT_TRUE(tr_sys_dir_create_temp(std::data(path), &error));
    EXPECT_FALSE(error) << error;
    tr_sys_path_remove(tr_u8path(path));

    path.assign(test_dir.string(), "/path-does-not-exist/test-XXXXXX");
    EXPECT_FALSE(tr_sys_dir_create_temp(std::data(path), &error));
    EXPECT_TRUE(error);
}

TEST_F(FileTest, dirRead)
{
    auto const test_dir = createTestDir(currentTestName());

    auto const path1 = test_dir / u8"a"sv;
    auto const path2 = test_dir / u8"b"sv;

    auto have1 = bool{};
    auto have2 = bool{};
    testDirReadImpl(test_dir, &have1, &have2);
    EXPECT_FALSE(have1);
    EXPECT_FALSE(have2);

    createFileWithContents(path1.string(), "test");
    testDirReadImpl(test_dir, &have1, &have2);
    EXPECT_TRUE(have1);
    EXPECT_FALSE(have2);

    createFileWithContents(path2.string(), "test");
    testDirReadImpl(test_dir, &have1, &have2);
    EXPECT_TRUE(have1);
    EXPECT_TRUE(have2);

    tr_sys_path_remove(path1);
    testDirReadImpl(test_dir, &have1, &have2);
    EXPECT_FALSE(have1);
    EXPECT_TRUE(have2);
}

TEST_F(FileTest, dirOpen)
{
    auto const test_dir = createTestDir(currentTestName());

    auto const file = test_dir / u8"foo.fxt"sv;
    auto constexpr Contents = "hello, world!"sv;
    createFileWithContents(file.string(), std::data(Contents), std::size(Contents));

    // path does not exist
    auto err = tr_error{};
    auto odir = tr_sys_dir_open("/no/such/path", &err);
    EXPECT_EQ(TR_BAD_SYS_DIR, odir);
    EXPECT_TRUE(err);
    err = {};

    // path is not a directory
    odir = tr_sys_dir_open(file.string(), &err);
    EXPECT_EQ(TR_BAD_SYS_DIR, odir);
    EXPECT_TRUE(err);
    err = {};

    // path exists and is readable
    odir = tr_sys_dir_open(test_dir.string(), &err);
    EXPECT_NE(TR_BAD_SYS_DIR, odir);
    EXPECT_FALSE(err);
    auto files = std::set<std::string>{};
    for (;;)
    {
        char const* const filename = tr_sys_dir_read_name(odir, &err);
        if (filename == nullptr)
        {
            break;
        }
        files.insert(filename);
    }
    EXPECT_EQ(3U, std::size(files));
    EXPECT_FALSE(err) << err;
    EXPECT_TRUE(tr_sys_dir_close(odir, &err));
    EXPECT_FALSE(err) << err;
}

} // namespace tr::test
