// This file Copyright (C) 2013-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <cstring>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>

#ifndef _WIN32
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#else
#include <windows.h>
#endif

#include "transmission.h"

#include "error.h"
#include "file.h"
#include "tr-macros.h"
#include "tr-strbuf.h"

#include "test-fixtures.h"

#if !defined(__OpenBSD__)
#define HAVE_UNIFIED_BUFFER_CACHE
#endif

#ifndef _WIN32
#define NATIVE_PATH_SEP "/"
#else
#define NATIVE_PATH_SEP "\\"
#endif

using namespace std::literals;

namespace libtransmission
{

namespace test
{

class FileTest : public SessionTest
{
protected:
    auto createTestDir(std::string const& child_name)
    {
        auto test_dir = tr_pathbuf{ tr_sessionGetConfigDir(session_), '/', child_name };
        tr_sys_dir_create(test_dir, 0, 0777);
        return test_dir;
    }

    static bool createSymlink(char const* dst_path, char const* src_path, [[maybe_unused]] bool dst_is_dir)
    {
#ifndef _WIN32

        return symlink(src_path, dst_path) != -1;

#else
        wchar_t* wide_src_path = tr_win32_utf8_to_native(src_path, -1);
        wchar_t* wide_dst_path = tr_win32_utf8_to_native(dst_path, -1);

        auto const ret = CreateSymbolicLinkW(wide_dst_path, wide_src_path, dst_is_dir ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0);

        tr_free(wide_dst_path);
        tr_free(wide_src_path);

        return ret;

#endif
    }

    static bool createHardlink(char const* dst_path, char const* src_path)
    {
#ifndef _WIN32

        return link(src_path, dst_path) != -1;

#else

        wchar_t* wide_src_path = tr_win32_utf8_to_native(src_path, -1);
        wchar_t* wide_dst_path = tr_win32_utf8_to_native(dst_path, -1);

        auto const ret = CreateHardLinkW(wide_dst_path, wide_src_path, nullptr);

        tr_free(wide_dst_path);
        tr_free(wide_src_path);

        return ret;

#endif
    }

    static void clearPathInfo(tr_sys_path_info* info)
    {
        *info = {};
    }

    static bool pathContainsNoSymlinks(char const* path)
    {
        char const* p = path;

        while (*p != '\0')
        {
            tr_sys_path_info info;
            char const* slash_pos = strchr(p, '/');

#ifdef _WIN32

            char const* backslash_pos = strchr(p, '\\');

            if (slash_pos == nullptr || (backslash_pos != nullptr && backslash_pos < slash_pos))
            {
                slash_pos = backslash_pos;
            }

#endif

            if (slash_pos == nullptr)
            {
                slash_pos = p + strlen(p) - 1;
            }

            auto const path_part = std::string{ path, size_t(slash_pos - path + 1) };
            if (!tr_sys_path_get_info(path_part, TR_SYS_PATH_NO_FOLLOW, &info) ||
                (info.type != TR_SYS_PATH_IS_FILE && info.type != TR_SYS_PATH_IS_DIRECTORY))
            {
                return false;
            }

            p = slash_pos + 1;
        }

        return true;
    }

    static bool validatePermissions([[maybe_unused]] char const* path, [[maybe_unused]] unsigned int permissions)
    {
#ifndef _WIN32

        struct stat sb = {};
        return stat(path, &sb) != -1 && (sb.st_mode & 0777) == permissions;

#else

        /* No UNIX permissions on Windows */
        return true;

#endif
    }

    struct XnameTestData
    {
        char const* input;
        char const* output;
    };

    static void testPathXname(XnameTestData const* data, size_t data_size, std::string (*func)(std::string_view, tr_error**))
    {
        for (size_t i = 0; i < data_size; ++i)
        {
            tr_error* err = nullptr;
            auto const name = func(data[i].input, &err);

            if (data[i].output != nullptr)
            {
                EXPECT_NE(""sv, name);
                EXPECT_EQ(nullptr, err) << *err;
                EXPECT_EQ(data[i].output, name);
            }
            else
            {
                EXPECT_EQ(""sv, name);
                EXPECT_NE(nullptr, err);
                tr_error_clear(&err);
            }
        }
    }

    static void testPathXname(
        XnameTestData const* data,
        size_t data_size,
        std::string_view (*func)(std::string_view, tr_error**))
    {
        for (size_t i = 0; i < data_size; ++i)
        {
            tr_error* err = nullptr;
            auto const name = func(data[i].input, &err);
            std::cerr << __FILE__ << ':' << __LINE__ << " in [" << data[i].input << "] out [" << name << ']' << std::endl;

            if (data[i].output != nullptr)
            {
                EXPECT_NE(""sv, name);
                EXPECT_EQ(nullptr, err) << *err;
                EXPECT_EQ(std::string{ data[i].output }, name);
            }
            else
            {
                EXPECT_EQ(""sv, name);
                EXPECT_NE(nullptr, err);
                tr_error_clear(&err);
            }
        }
    }

    static void testDirReadImpl(tr_pathbuf const& path, bool* have1, bool* have2)
    {
        *have1 = *have2 = false;

        tr_error* err = nullptr;
        auto dd = tr_sys_dir_open(path, &err);
        EXPECT_NE(TR_BAD_SYS_DIR, dd);
        EXPECT_EQ(nullptr, err) << *err;

        char const* name;
        while ((name = tr_sys_dir_read_name(dd, &err)) != nullptr)
        {
            EXPECT_EQ(nullptr, err) << *err;

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

        EXPECT_EQ(nullptr, err) << *err;

        EXPECT_TRUE(tr_sys_dir_close(dd, &err));
        EXPECT_EQ(nullptr, err) << *err;
    }
};

TEST_F(FileTest, getInfo)
{
    auto const test_dir = createTestDir(currentTestName());
    tr_sys_path_info info;

    auto const path1 = tr_pathbuf{ test_dir, "/a"sv };
    auto const path2 = tr_pathbuf{ test_dir, "/b"sv };

    // Can't get info of non-existent file/directory
    tr_error* err = nullptr;
    EXPECT_FALSE(tr_sys_path_get_info(path1, 0, &info, &err));
    EXPECT_NE(nullptr, err);
    tr_error_clear(&err);

    auto t = time(nullptr);
    createFileWithContents(path1, "test");

    // Good file info
    clearPathInfo(&info);
    EXPECT_TRUE(tr_sys_path_get_info(path1, 0, &info, &err));
    EXPECT_EQ(nullptr, err) << *err;
    EXPECT_EQ(TR_SYS_PATH_IS_FILE, info.type);
    EXPECT_EQ(4, info.size);
    EXPECT_GE(info.last_modified_at, t - 1);
    EXPECT_LE(info.last_modified_at, time(nullptr) + 1);

    // Good file info (by handle)
    auto fd = tr_sys_file_open(path1, TR_SYS_FILE_READ, 0);
    clearPathInfo(&info);
    EXPECT_TRUE(tr_sys_file_get_info(fd, &info, &err));
    EXPECT_EQ(nullptr, err) << *err;
    EXPECT_EQ(TR_SYS_PATH_IS_FILE, info.type);
    EXPECT_EQ(4, info.size);
    EXPECT_GE(info.last_modified_at, t - 1);
    EXPECT_LE(info.last_modified_at, time(nullptr) + 1);
    tr_sys_file_close(fd);

    tr_sys_path_remove(path1);

    // Good directory info
    t = time(nullptr);
    tr_sys_dir_create(path1, 0, 0777);
    clearPathInfo(&info);
    EXPECT_TRUE(tr_sys_path_get_info(path1, 0, &info, &err));
    EXPECT_EQ(nullptr, err) << *err;
    EXPECT_EQ(TR_SYS_PATH_IS_DIRECTORY, info.type);
    EXPECT_NE(uint64_t(-1), info.size);
    EXPECT_GE(info.last_modified_at, t - 1);
    EXPECT_LE(info.last_modified_at, time(nullptr) + 1);
    tr_sys_path_remove(path1);

    if (createSymlink(path1, path2, false))
    {
        // Can't get info of non-existent file/directory
        EXPECT_FALSE(tr_sys_path_get_info(path1, 0, &info, &err));
        EXPECT_NE(nullptr, err);
        tr_error_clear(&err);

        t = time(nullptr);
        createFileWithContents(path2, "test");

        // Good file info
        clearPathInfo(&info);
        EXPECT_TRUE(tr_sys_path_get_info(path1, 0, &info, &err));
        EXPECT_EQ(nullptr, err) << *err;
        EXPECT_EQ(TR_SYS_PATH_IS_FILE, info.type);
        EXPECT_EQ(4, info.size);
        EXPECT_GE(info.last_modified_at, t - 1);
        EXPECT_LE(info.last_modified_at, time(nullptr) + 1);

        // Good file info (by handle)
        fd = tr_sys_file_open(path1, TR_SYS_FILE_READ, 0);
        clearPathInfo(&info);
        EXPECT_TRUE(tr_sys_file_get_info(fd, &info, &err));
        EXPECT_EQ(nullptr, err) << *err;
        EXPECT_EQ(TR_SYS_PATH_IS_FILE, info.type);
        EXPECT_EQ(4, info.size);
        EXPECT_GE(info.last_modified_at, t - 1);
        EXPECT_LE(info.last_modified_at, time(nullptr) + 1);
        tr_sys_file_close(fd);

        tr_sys_path_remove(path2);
        tr_sys_path_remove(path1);

        // Good directory info
        t = time(nullptr);
        tr_sys_dir_create(path2, 0, 0777);
        EXPECT_TRUE(createSymlink(path1, path2, true)); /* Win32: directory and file symlinks differ :( */
        clearPathInfo(&info);
        EXPECT_TRUE(tr_sys_path_get_info(path1, 0, &info, &err));
        EXPECT_EQ(nullptr, err) << *err;
        EXPECT_EQ(TR_SYS_PATH_IS_DIRECTORY, info.type);
        EXPECT_NE(uint64_t(-1), info.size);
        EXPECT_GE(info.last_modified_at, t - 1);
        EXPECT_LE(info.last_modified_at, time(nullptr) + 1);

        tr_sys_path_remove(path2);
        tr_sys_path_remove(path1);
    }
    else
    {
        fprintf(stderr, "WARNING: [%s] unable to run symlink tests\n", __FUNCTION__);
    }
}

TEST_F(FileTest, pathExists)
{
    auto const test_dir = createTestDir(currentTestName());

    auto const path1 = tr_pathbuf{ test_dir, "/a"sv };
    auto const path2 = tr_pathbuf{ test_dir, "/b"sv };

    // Non-existent file does not exist
    tr_error* err = nullptr;
    EXPECT_FALSE(tr_sys_path_exists(path1, &err));
    EXPECT_EQ(nullptr, err) << *err;

    // Create file and see that it exists
    createFileWithContents(path1, "test");
    EXPECT_TRUE(tr_sys_path_exists(path1, &err));
    EXPECT_EQ(nullptr, err) << *err;

    tr_sys_path_remove(path1);

    // Create directory and see that it exists
    tr_sys_dir_create(path1, 0, 0777);
    EXPECT_TRUE(tr_sys_path_exists(path1, &err));
    EXPECT_EQ(nullptr, err) << *err;

    tr_sys_path_remove(path1);

    if (createSymlink(path1, path2, false))
    {
        // Non-existent file does not exist (via symlink)
        EXPECT_FALSE(tr_sys_path_exists(path1, &err));
        EXPECT_EQ(nullptr, err) << *err;

        // Create file and see that it exists (via symlink)
        createFileWithContents(path2, "test");
        EXPECT_TRUE(tr_sys_path_exists(path1, &err));
        EXPECT_EQ(nullptr, err) << *err;

        tr_sys_path_remove(path2);
        tr_sys_path_remove(path1);

        /* Create directory and see that it exists (via symlink) */
        tr_sys_dir_create(path2, 0, 0777);
        EXPECT_TRUE(createSymlink(path1, path2, true)); /* Win32: directory and file symlinks differ :( */
        EXPECT_TRUE(tr_sys_path_exists(path1, &err));
        EXPECT_EQ(nullptr, err) << *err;

        tr_sys_path_remove(path2);
        tr_sys_path_remove(path1);
    }
    else
    {
        fprintf(stderr, "WARNING: [%s] unable to run symlink tests\n", __FUNCTION__);
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

    auto const path1 = tr_pathbuf{ test_dir, "/a"sv };
    auto const path2 = tr_pathbuf{ test_dir, "/b"sv };
    auto path3 = tr_pathbuf{ path2, "/c"sv };

    /* Two non-existent files are not the same */
    tr_error* err = nullptr;
    EXPECT_FALSE(tr_sys_path_is_same(path1, path1, &err));
    EXPECT_EQ(nullptr, err) << *err;
    EXPECT_FALSE(tr_sys_path_is_same(path1, path2, &err));
    EXPECT_EQ(nullptr, err) << *err;

    /* Two same files are the same */
    createFileWithContents(path1, "test");
    EXPECT_TRUE(tr_sys_path_is_same(path1, path1, &err));
    EXPECT_EQ(nullptr, err) << *err;

    /* Existent and non-existent files are not the same */
    EXPECT_FALSE(tr_sys_path_is_same(path1, path2, &err));
    EXPECT_EQ(nullptr, err) << *err;
    EXPECT_FALSE(tr_sys_path_is_same(path2, path1, &err));
    EXPECT_EQ(nullptr, err) << *err;

    /* Two separate files (even with same content) are not the same */
    createFileWithContents(path2, "test");
    EXPECT_FALSE(tr_sys_path_is_same(path1, path2, &err));
    EXPECT_EQ(nullptr, err) << *err;

    tr_sys_path_remove(path1);

    /* Two same directories are the same */
    tr_sys_dir_create(path1, 0, 0777);
    EXPECT_TRUE(tr_sys_path_is_same(path1, path1, &err));
    EXPECT_EQ(nullptr, err) << *err;

    /* File and directory are not the same */
    EXPECT_FALSE(tr_sys_path_is_same(path1, path2, &err));
    EXPECT_EQ(nullptr, err) << *err;
    EXPECT_FALSE(tr_sys_path_is_same(path2, path1, &err));
    EXPECT_EQ(nullptr, err) << *err;

    tr_sys_path_remove(path2);

    /* Two separate directories are not the same */
    tr_sys_dir_create(path2, 0, 0777);
    EXPECT_FALSE(tr_sys_path_is_same(path1, path2, &err));
    EXPECT_EQ(nullptr, err) << *err;

    tr_sys_path_remove(path1);
    tr_sys_path_remove(path2);

    if (createSymlink(path1, ".", true))
    {
        /* Directory and symlink pointing to it are the same */
        EXPECT_TRUE(tr_sys_path_is_same(path1, test_dir.data(), &err));
        EXPECT_EQ(nullptr, err) << *err;
        EXPECT_TRUE(tr_sys_path_is_same(test_dir.data(), path1, &err));
        EXPECT_EQ(nullptr, err) << *err;

        /* Non-existent file and symlink are not the same */
        EXPECT_FALSE(tr_sys_path_is_same(path1, path2, &err));
        EXPECT_EQ(nullptr, err) << *err;
        EXPECT_FALSE(tr_sys_path_is_same(path2, path1, &err));
        EXPECT_EQ(nullptr, err) << *err;

        /* Symlinks pointing to different directories are not the same */
        createSymlink(path2, "..", true);
        EXPECT_FALSE(tr_sys_path_is_same(path1, path2, &err));
        EXPECT_EQ(nullptr, err) << *err;
        EXPECT_FALSE(tr_sys_path_is_same(path2, path1, &err));
        EXPECT_EQ(nullptr, err) << *err;

        tr_sys_path_remove(path2);

        /* Symlinks pointing to same directory are the same */
        createSymlink(path2, ".", true);
        EXPECT_TRUE(tr_sys_path_is_same(path1, path2, &err));
        EXPECT_EQ(nullptr, err) << *err;

        tr_sys_path_remove(path2);

        /* Directory and symlink pointing to another directory are not the same */
        tr_sys_dir_create(path2, 0, 0777);
        EXPECT_FALSE(tr_sys_path_is_same(path1, path2, &err));
        EXPECT_EQ(nullptr, err) << *err;
        EXPECT_FALSE(tr_sys_path_is_same(path2, path1, &err));
        EXPECT_EQ(nullptr, err) << *err;

        /* Symlinks pointing to same directory are the same */
        createSymlink(path3, "..", true);
        EXPECT_TRUE(tr_sys_path_is_same(path1, path3, &err));
        EXPECT_EQ(nullptr, err) << *err;

        tr_sys_path_remove(path1);

        /* File and symlink pointing to directory are not the same */
        createFileWithContents(path1, "test");
        EXPECT_FALSE(tr_sys_path_is_same(path1, path3, &err));
        EXPECT_EQ(nullptr, err) << *err;
        EXPECT_FALSE(tr_sys_path_is_same(path3, path1, &err));
        EXPECT_EQ(nullptr, err) << *err;

        tr_sys_path_remove(path3);

        /* File and symlink pointing to same file are the same */
        createSymlink(path3, path1, false);
        EXPECT_TRUE(tr_sys_path_is_same(path1, path3, &err));
        EXPECT_EQ(nullptr, err) << *err;
        EXPECT_TRUE(tr_sys_path_is_same(path3, path1, &err));
        EXPECT_EQ(nullptr, err) << *err;

        /* Symlinks pointing to non-existent files are not the same */
        tr_sys_path_remove(path1);
        createSymlink(path1, "missing", false);
        tr_sys_path_remove(path3);
        createSymlink(path3, "missing", false);
        EXPECT_FALSE(tr_sys_path_is_same(path1, path3, &err));
        EXPECT_EQ(nullptr, err) << *err;
        EXPECT_FALSE(tr_sys_path_is_same(path3, path1, &err));
        EXPECT_EQ(nullptr, err) << *err;

        tr_sys_path_remove(path3);

        /* Symlinks pointing to same non-existent file are not the same */
        createSymlink(path3.c_str(), ".." NATIVE_PATH_SEP "missing", false);
        EXPECT_FALSE(tr_sys_path_is_same(path1, path3, &err));
        EXPECT_EQ(nullptr, err) << *err;
        EXPECT_FALSE(tr_sys_path_is_same(path3, path1, &err));
        EXPECT_EQ(nullptr, err) << *err;

        /* Non-existent file and symlink pointing to non-existent file are not the same */
        tr_sys_path_remove(path3);
        EXPECT_FALSE(tr_sys_path_is_same(path1, path3, &err));
        EXPECT_EQ(nullptr, err) << *err;
        EXPECT_FALSE(tr_sys_path_is_same(path3, path1, &err));
        EXPECT_EQ(nullptr, err) << *err;

        tr_sys_path_remove(path2);
        tr_sys_path_remove(path1);
    }
    else
    {
        fprintf(stderr, "WARNING: [%s] unable to run symlink tests\n", __FUNCTION__);
    }

    path3.assign(test_dir, "/c"sv);

    createFileWithContents(path1, "test");

    if (createHardlink(path2, path1))
    {
        /* File and hardlink to it are the same */
        EXPECT_TRUE(tr_sys_path_is_same(path1, path2, &err));
        EXPECT_EQ(nullptr, err) << *err;

        /* Two hardlinks to the same file are the same */
        createHardlink(path3, path2);
        EXPECT_TRUE(tr_sys_path_is_same(path2, path3, &err));
        EXPECT_EQ(nullptr, err) << *err;
        EXPECT_TRUE(tr_sys_path_is_same(path1, path3, &err));
        EXPECT_EQ(nullptr, err) << *err;

        tr_sys_path_remove(path2);

        EXPECT_TRUE(tr_sys_path_is_same(path1, path3, &err));
        EXPECT_EQ(nullptr, err) << *err;

        tr_sys_path_remove(path3);

        /* File and hardlink to another file are not the same */
        createFileWithContents(path3, "test");
        createHardlink(path2, path3);
        EXPECT_FALSE(tr_sys_path_is_same(path1, path2, &err));
        EXPECT_EQ(nullptr, err) << *err;
        EXPECT_FALSE(tr_sys_path_is_same(path2, path1, &err));
        EXPECT_EQ(nullptr, err) << *err;

        tr_sys_path_remove(path3);
        tr_sys_path_remove(path2);
    }
    else
    {
        fprintf(stderr, "WARNING: [%s] unable to run hardlink tests\n", __FUNCTION__);
    }

    if (createSymlink(path2, path1, false) && createHardlink(path3, path1))
    {
        EXPECT_TRUE(tr_sys_path_is_same(path2, path3, &err));
        EXPECT_EQ(nullptr, err) << *err;
    }
    else
    {
        fprintf(stderr, "WARNING: [%s] unable to run combined symlink and hardlink tests\n", __FUNCTION__);
    }

    tr_sys_path_remove(path3);
    tr_sys_path_remove(path2);
    tr_sys_path_remove(path1);

    // NOLINTEND(readability-suspicious-call-argument)
}

TEST_F(FileTest, pathResolve)
{
    auto const test_dir = createTestDir(currentTestName());

    tr_error* err = nullptr;
    auto const path1 = tr_pathbuf{ test_dir, "/a"sv };
    auto const path2 = tr_pathbuf{ test_dir, "/b"sv };

    createFileWithContents(path1, "test");

    if (createSymlink(path2, path1, false))
    {
        auto tmp = makeString(tr_sys_path_resolve(path2, &err));
        EXPECT_EQ(nullptr, err) << *err;
        EXPECT_TRUE(pathContainsNoSymlinks(tmp.c_str()));

        tr_sys_path_remove(path2);
        tr_sys_path_remove(path1);

        tr_sys_dir_create(path1, 0, 0755);
        EXPECT_TRUE(createSymlink(path2, path1, true)); /* Win32: directory and file symlinks differ :( */
        tmp = makeString(tr_sys_path_resolve(path2, &err));
        EXPECT_EQ(nullptr, err) << *err;
        EXPECT_TRUE(pathContainsNoSymlinks(tmp.c_str()));
    }
    else
    {
        fprintf(stderr, "WARNING: [%s] unable to run symlink tests\n", __FUNCTION__);
    }

    tr_sys_path_remove(path2);
    tr_sys_path_remove(path1);

#ifdef _WIN32

    {
        char* tmp;

        tmp = tr_sys_path_resolve("\\\\127.0.0.1\\NonExistent", &err);
        EXPECT_EQ(nullptr, tmp);
        EXPECT_NE(nullptr, err);
        tr_error_clear(&err);

        tmp = tr_sys_path_resolve("\\\\127.0.0.1\\ADMIN$\\NonExistent", &err);
        EXPECT_EQ(nullptr, tmp);
        EXPECT_NE(nullptr, err);
        tr_error_clear(&err);

        tmp = tr_sys_path_resolve("\\\\127.0.0.1\\ADMIN$\\System32", &err);
        EXPECT_STREQ("\\\\127.0.0.1\\ADMIN$\\System32", tmp);
        EXPECT_EQ(nullptr, err) << *err;
        tr_free(tmp);
    }

#endif
}

TEST_F(FileTest, pathBasename)
{
    auto const common_xname_tests = std::vector<XnameTestData>{
        XnameTestData{ "/", "/" },
        { "", "." },
#ifdef _WIN32
        { "\\", "/" },
        /* Invalid paths */
        { "\\\\\\", nullptr },
        { "123:", nullptr },
        /* Reserved characters */
        { "<", nullptr },
        { ">", nullptr },
        { ":", nullptr },
        { "\"", nullptr },
        { "|", nullptr },
        { "?", nullptr },
        { "*", nullptr },
        { "a\\<", nullptr },
        { "a\\>", nullptr },
        { "a\\:", nullptr },
        { "a\\\"", nullptr },
        { "a\\|", nullptr },
        { "a\\?", nullptr },
        { "a\\*", nullptr },
        { "c:\\a\\b<c\\d", nullptr },
        { "c:\\a\\b>c\\d", nullptr },
        { "c:\\a\\b:c\\d", nullptr },
        { "c:\\a\\b\"c\\d", nullptr },
        { "c:\\a\\b|c\\d", nullptr },
        { "c:\\a\\b?c\\d", nullptr },
        { "c:\\a\\b*c\\d", nullptr },
#else
        { "////", "/" },
#endif
    };

    testPathXname(common_xname_tests.data(), common_xname_tests.size(), tr_sys_path_basename);
    // testPathXname(common_xname_tests.data(), common_xname_tests.size(), tr_sys_path_dirname);

    auto const basename_tests = std::vector<XnameTestData>{
        XnameTestData{ "a", "a" },
        { "aa", "aa" },
        { "/aa", "aa" },
        { "/a/b/c", "c" },
        { "/a/b/c/", "c" },
#ifdef _WIN32
        { "c:\\a\\b\\c", "c" },
        { "c:", "/" },
        { "c:/", "/" },
        { "c:\\", "/" },
        { "c:a/b", "b" },
        { "c:a", "a" },
        { "\\\\a\\b\\c", "c" },
        { "//a/b", "b" },
        { "//1.2.3.4/b", "b" },
        { "\\\\a", "a" },
        { "\\\\1.2.3.4", "1.2.3.4" },
        { "\\", "/" },
        { "\\a", "a" },
#endif
    };

    testPathXname(basename_tests.data(), basename_tests.size(), tr_sys_path_basename);
}

TEST_F(FileTest, pathDirname)
{
#ifdef _WIN32
    static auto constexpr DirnameTests = std::array<std::pair<std::string_view, std::string_view>, 48>{ {
        { "C:\\a/b\\c"sv, "C:\\a/b"sv },
        { "C:\\a/b\\c\\"sv, "C:\\a/b"sv },
        { "C:\\a/b"sv, "C:\\a"sv },
        { "C:/a"sv, "C:/"sv },
        { "C:"sv, "C:"sv },
        { "C:/"sv, "C:/"sv },
        { "c:a/b"sv, "c:a"sv },
        { "c:a"sv, "c:"sv },
        { "\\\\a"sv, "\\"sv },
        { "\\\\1.2.3.4"sv, "\\"sv },
        { "\\\\"sv, "\\"sv },
        { "a/b\\c"sv, "a/b"sv },
        // taken from Node.js unit tests
        // https://github.com/nodejs/node/blob/e46c680bf2b211bbd52cf959ca17ee98c7f657f5/test/parallel/test-path-dirname.js
        { "c:\\"sv, "c:\\"sv },
        { "c:\\foo"sv, "c:\\"sv },
        { "c:\\foo\\"sv, "c:\\"sv },
        { "c:\\foo\\bar"sv, "c:\\foo"sv },
        { "c:\\foo\\bar\\"sv, "c:\\foo"sv },
        { "c:\\foo\\bar\\baz"sv, "c:\\foo\\bar"sv },
        { "c:\\foo bar\\baz"sv, "c:\\foo bar"sv },
        { "\\"sv, "\\"sv },
        { "\\foo"sv, "\\"sv },
        { "\\foo\\"sv, "\\"sv },
        { "\\foo\\bar"sv, "\\foo"sv },
        { "\\foo\\bar\\"sv, "\\foo"sv },
        { "\\foo\\bar\\baz"sv, "\\foo\\bar"sv },
        { "\\foo bar\\baz"sv, "\\foo bar"sv },
        { "c:"sv, "c:"sv },
        { "c:foo"sv, "c:"sv },
        { "c:foo\\"sv, "c:"sv },
        { "c:foo\\bar"sv, "c:foo"sv },
        { "c:foo\\bar\\"sv, "c:foo"sv },
        { "c:foo\\bar\\baz"sv, "c:foo\\bar"sv },
        { "c:foo bar\\baz"sv, "c:foo bar"sv },
        { "file:stream"sv, "."sv },
        { "dir\\file:stream"sv, "dir"sv },
        { "\\\\unc\\share"sv, "\\\\unc\\share"sv },
        { "\\\\unc\\share\\foo"sv, "\\\\unc\\share\\"sv },
        { "\\\\unc\\share\\foo\\"sv, "\\\\unc\\share\\"sv },
        { "\\\\unc\\share\\foo\\bar"sv, "\\\\unc\\share\\foo"sv },
        { "\\\\unc\\share\\foo\\bar\\"sv, "\\\\unc\\share\\foo"sv },
        { "\\\\unc\\share\\foo\\bar\\baz"sv, "\\\\unc\\share\\foo\\bar"sv },
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
        EXPECT_EQ(expected, tr_sys_path_dirname(input)) << "input[" << input << "] expected [" << expected << "] actual ["
                                                        << tr_sys_path_dirname(input) << ']' << std::endl;

        auto path = tr_pathbuf{ input };
        path.popdir();
        EXPECT_EQ(expected, path) << "input[" << input << "] expected [" << expected << "] actual [" << path << ']'
                                  << std::endl;
    }

    /* TODO: is_same(dirname(x) + '/' + basename(x), x) */
}

TEST_F(FileTest, pathRename)
{
    auto const test_dir = createTestDir(currentTestName());

    auto const path1 = tr_pathbuf{ test_dir, "/a"sv };
    auto const path2 = tr_pathbuf{ test_dir, "/b"sv };
    auto path3 = tr_pathbuf{ path2, "/c"sv };

    createFileWithContents(path1, "test");

    /* Preconditions */
    EXPECT_TRUE(tr_sys_path_exists(path1));
    EXPECT_FALSE(tr_sys_path_exists(path2));

    /* Forward rename works */
    tr_error* err = nullptr;
    EXPECT_TRUE(tr_sys_path_rename(path1, path2, &err));
    EXPECT_FALSE(tr_sys_path_exists(path1));
    EXPECT_TRUE(tr_sys_path_exists(path2));
    EXPECT_EQ(nullptr, err) << *err;

    /* Backward rename works */
    EXPECT_TRUE(tr_sys_path_rename(path2, path1, &err));
    EXPECT_TRUE(tr_sys_path_exists(path1));
    EXPECT_FALSE(tr_sys_path_exists(path2));
    EXPECT_EQ(nullptr, err) << *err;

    /* Another backward rename [of non-existent file] does not work */
    EXPECT_FALSE(tr_sys_path_rename(path2, path1, &err));
    EXPECT_NE(nullptr, err);
    tr_error_clear(&err);

    /* Rename to file which couldn't be created does not work */
    EXPECT_FALSE(tr_sys_path_rename(path1, path3, &err));
    EXPECT_NE(nullptr, err);
    tr_error_clear(&err);

    /* Rename of non-existent file does not work */
    EXPECT_FALSE(tr_sys_path_rename(path3, path2, &err));
    EXPECT_NE(nullptr, err);
    tr_error_clear(&err);

    createFileWithContents(path2, "test");

    /* Renaming file does overwrite existing file */
    EXPECT_TRUE(tr_sys_path_rename(path2, path1, &err));
    EXPECT_EQ(nullptr, err) << *err;

    tr_sys_dir_create(path2, 0, 0777);

    /* Renaming file does not overwrite existing directory, and vice versa */
    EXPECT_FALSE(tr_sys_path_rename(path1, path2, &err));
    EXPECT_NE(nullptr, err);
    tr_error_clear(&err);
    EXPECT_FALSE(tr_sys_path_rename(path2, path1, &err));
    EXPECT_NE(nullptr, err);
    tr_error_clear(&err);

    tr_sys_path_remove(path2);

    path3.assign(test_dir, "/c"sv);

    if (createSymlink(path2, path1, false))
    {
        /* Preconditions */
        EXPECT_TRUE(tr_sys_path_exists(path2));
        EXPECT_FALSE(tr_sys_path_exists(path3));
        EXPECT_TRUE(tr_sys_path_is_same(path1, path2));

        /* Rename of symlink works, files stay the same */
        EXPECT_TRUE(tr_sys_path_rename(path2, path3, &err));
        EXPECT_EQ(nullptr, err) << *err;
        EXPECT_FALSE(tr_sys_path_exists(path2));
        EXPECT_TRUE(tr_sys_path_exists(path3));
        EXPECT_TRUE(tr_sys_path_is_same(path1, path3));

        tr_sys_path_remove(path3);
    }
    else
    {
        fprintf(stderr, "WARNING: [%s] unable to run symlink tests\n", __FUNCTION__);
    }

    if (createHardlink(path2, path1))
    {
        /* Preconditions */
        EXPECT_TRUE(tr_sys_path_exists(path2));
        EXPECT_FALSE(tr_sys_path_exists(path3));
        EXPECT_TRUE(tr_sys_path_is_same(path1, path2));

        /* Rename of hardlink works, files stay the same */
        EXPECT_TRUE(tr_sys_path_rename(path2, path3, &err));
        EXPECT_EQ(nullptr, err) << *err;
        EXPECT_FALSE(tr_sys_path_exists(path2));
        EXPECT_TRUE(tr_sys_path_exists(path3));
        EXPECT_TRUE(tr_sys_path_is_same(path1, path3));

        tr_sys_path_remove(path3);
    }
    else
    {
        fprintf(stderr, "WARNING: [%s] unable to run hardlink tests\n", __FUNCTION__);
    }

    tr_sys_path_remove(path1);
}

TEST_F(FileTest, pathRemove)
{
    auto const test_dir = createTestDir(currentTestName());

    auto const path1 = tr_pathbuf{ test_dir, "/a"sv };
    auto const path2 = tr_pathbuf{ test_dir, "/b"sv };
    auto const path3 = tr_pathbuf{ path2, "/c"sv };

    /* Can't remove non-existent file/directory */
    EXPECT_FALSE(tr_sys_path_exists(path1));
    tr_error* err = nullptr;
    EXPECT_FALSE(tr_sys_path_remove(path1, &err));
    EXPECT_NE(nullptr, err);
    EXPECT_FALSE(tr_sys_path_exists(path1));
    tr_error_clear(&err);

    /* Removing file works */
    createFileWithContents(path1, "test");
    EXPECT_TRUE(tr_sys_path_exists(path1));
    EXPECT_TRUE(tr_sys_path_remove(path1, &err));
    EXPECT_EQ(nullptr, err) << *err;
    EXPECT_FALSE(tr_sys_path_exists(path1));

    /* Removing empty directory works */
    tr_sys_dir_create(path1, 0, 0777);
    EXPECT_TRUE(tr_sys_path_exists(path1));
    EXPECT_TRUE(tr_sys_path_remove(path1, &err));
    EXPECT_EQ(nullptr, err) << *err;
    EXPECT_FALSE(tr_sys_path_exists(path1));

    /* Removing non-empty directory fails */
    tr_sys_dir_create(path2, 0, 0777);
    createFileWithContents(path3, "test");
    EXPECT_TRUE(tr_sys_path_exists(path2));
    EXPECT_TRUE(tr_sys_path_exists(path3));
    EXPECT_FALSE(tr_sys_path_remove(path2, &err));
    EXPECT_NE(nullptr, err);
    EXPECT_TRUE(tr_sys_path_exists(path2));
    EXPECT_TRUE(tr_sys_path_exists(path3));
    tr_error_clear(&err);

    tr_sys_path_remove(path3);
    tr_sys_path_remove(path2);
}

TEST_F(FileTest, pathNativeSeparators)
{
    EXPECT_EQ(nullptr, tr_sys_path_native_separators(nullptr));

    struct LocalTest
    {
        std::string input;
        std::string expected_output;
    };

    auto const tests = std::array<LocalTest, 5>{
        LocalTest{ "", "" },
        { "a", TR_IF_WIN32("a", "a") },
        { "/", TR_IF_WIN32("\\", "/") },
        { "/a/b/c", TR_IF_WIN32("\\a\\b\\c", "/a/b/c") },
        { "C:\\a/b\\c", TR_IF_WIN32("C:\\a\\b\\c", "C:\\a/b\\c") },
    };

    for (auto const& test : tests)
    {
        auto buf = std::string(test.input);
        char* const output = tr_sys_path_native_separators(&buf.front());
        EXPECT_EQ(test.expected_output, output);
        EXPECT_EQ(buf.data(), output);
    }
}

TEST_F(FileTest, fileOpen)
{
    auto const test_dir = createTestDir(currentTestName());

    // can't open non-existent file
    auto const path1 = tr_pathbuf{ test_dir, "/a"sv };
    EXPECT_FALSE(tr_sys_path_exists(path1));
    tr_error* err = nullptr;
    EXPECT_EQ(TR_BAD_SYS_FILE, tr_sys_file_open(path1, TR_SYS_FILE_READ, 0600, &err));
    EXPECT_NE(nullptr, err);
    EXPECT_FALSE(tr_sys_path_exists(path1));
    tr_error_clear(&err);
    EXPECT_EQ(TR_BAD_SYS_FILE, tr_sys_file_open(path1, TR_SYS_FILE_WRITE, 0600, &err));
    EXPECT_NE(nullptr, err);
    EXPECT_FALSE(tr_sys_path_exists(path1));
    tr_error_clear(&err);

    // can't open directory
    tr_sys_dir_create(path1, 0, 0777);
#ifdef _WIN32
    // this works on *NIX
    EXPECT_EQ(TR_BAD_SYS_FILE, tr_sys_file_open(path1, TR_SYS_FILE_READ, 0600, &err));
    EXPECT_NE(nullptr, err);
    tr_error_clear(&err);
#endif
    EXPECT_EQ(TR_BAD_SYS_FILE, tr_sys_file_open(path1, TR_SYS_FILE_WRITE, 0600, &err));
    EXPECT_NE(nullptr, err);
    tr_error_clear(&err);

    tr_sys_path_remove(path1);

    // can create non-existent file
    auto fd = tr_sys_file_open(path1, TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE, 0640, &err);
    EXPECT_NE(TR_BAD_SYS_FILE, fd);
    EXPECT_EQ(nullptr, err) << *err;
    tr_sys_file_close(fd);
    EXPECT_TRUE(tr_sys_path_exists(path1));
    EXPECT_TRUE(validatePermissions(path1, 0640));

    // can open existing file
    EXPECT_TRUE(tr_sys_path_exists(path1));
    fd = tr_sys_file_open(path1, TR_SYS_FILE_READ, 0600, &err);
    EXPECT_NE(TR_BAD_SYS_FILE, fd);
    EXPECT_EQ(nullptr, err) << *err;
    tr_sys_file_close(fd);
    fd = tr_sys_file_open(path1, TR_SYS_FILE_WRITE, 0600, &err);
    EXPECT_NE(TR_BAD_SYS_FILE, fd);
    EXPECT_EQ(nullptr, err) << *err;
    tr_sys_file_close(fd);

    tr_sys_path_remove(path1);
    createFileWithContents(path1, "test");

    /* Can't create new file if it already exists */
    fd = tr_sys_file_open(path1, TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE_NEW, 0640, &err);
    EXPECT_EQ(TR_BAD_SYS_FILE, fd);
    EXPECT_NE(nullptr, err);
    tr_error_clear(&err);
    tr_sys_path_info info;
    tr_sys_path_get_info(path1, TR_SYS_PATH_NO_FOLLOW, &info);
    EXPECT_EQ(4, info.size);

    /* Pointer is at the end of file */
    tr_sys_path_get_info(path1, TR_SYS_PATH_NO_FOLLOW, &info);
    EXPECT_EQ(4, info.size);
    fd = tr_sys_file_open(path1, TR_SYS_FILE_WRITE | TR_SYS_FILE_APPEND, 0600, &err);
    EXPECT_NE(TR_BAD_SYS_FILE, fd);
    EXPECT_EQ(nullptr, err) << *err;
    tr_sys_file_write(fd, "s", 1, nullptr); /* On *NIX, pointer is positioned on each write but not initially */
    auto n = uint64_t{};
    tr_sys_file_seek(fd, 0, TR_SEEK_CUR, &n);
    EXPECT_EQ(5, n);
    tr_sys_file_close(fd);

    /* File gets truncated */
    tr_sys_path_get_info(path1, TR_SYS_PATH_NO_FOLLOW, &info);
    EXPECT_EQ(5, info.size);
    fd = tr_sys_file_open(path1, TR_SYS_FILE_WRITE | TR_SYS_FILE_TRUNCATE, 0600, &err);
    EXPECT_NE(TR_BAD_SYS_FILE, fd);
    EXPECT_EQ(nullptr, err) << *err;
    tr_sys_file_get_info(fd, &info);
    EXPECT_EQ(0, info.size);
    tr_sys_file_close(fd);
    tr_sys_path_get_info(path1, TR_SYS_PATH_NO_FOLLOW, &info);
    EXPECT_EQ(0, info.size);

    /* TODO: symlink and hardlink tests */

    tr_sys_path_remove(path1);
}

TEST_F(FileTest, fileReadWriteSeek)
{
    auto const test_dir = createTestDir(currentTestName());

    auto const path1 = tr_pathbuf{ test_dir, "/a"sv };
    auto const fd = tr_sys_file_open(path1, TR_SYS_FILE_READ | TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE, 0600);

    uint64_t n;
    tr_error* err = nullptr;
    EXPECT_TRUE(tr_sys_file_seek(fd, 0, TR_SEEK_CUR, &n, &err));
    EXPECT_EQ(nullptr, err) << *err;
    EXPECT_EQ(0, n);

    EXPECT_TRUE(tr_sys_file_write(fd, "test", 4, &n, &err));
    EXPECT_EQ(nullptr, err) << *err;
    EXPECT_EQ(4, n);

    EXPECT_TRUE(tr_sys_file_seek(fd, 0, TR_SEEK_CUR, &n, &err));
    EXPECT_EQ(nullptr, err) << *err;
    EXPECT_EQ(4, n);

    EXPECT_TRUE(tr_sys_file_seek(fd, 0, TR_SEEK_SET, &n, &err));
    EXPECT_EQ(nullptr, err) << *err;
    EXPECT_EQ(0, n);

    auto buf = std::array<char, 100>{};
    EXPECT_TRUE(tr_sys_file_read(fd, buf.data(), buf.size(), &n, &err));
    EXPECT_EQ(nullptr, err) << *err;
    EXPECT_EQ(4, n);

    EXPECT_EQ(0, memcmp("test", buf.data(), 4));

    EXPECT_TRUE(tr_sys_file_seek(fd, -3, TR_SEEK_CUR, &n, &err));
    EXPECT_EQ(nullptr, err) << *err;
    EXPECT_EQ(1, n);

    EXPECT_TRUE(tr_sys_file_write(fd, "E", 1, &n, &err));
    EXPECT_EQ(nullptr, err) << *err;
    EXPECT_EQ(1, n);

    EXPECT_TRUE(tr_sys_file_seek(fd, -2, TR_SEEK_CUR, &n, &err));
    EXPECT_EQ(nullptr, err) << *err;
    EXPECT_EQ(0, n);

    EXPECT_TRUE(tr_sys_file_read(fd, buf.data(), buf.size(), &n, &err));
    EXPECT_EQ(nullptr, err) << *err;
    EXPECT_EQ(4, n);

    EXPECT_EQ(0, memcmp("tEst", buf.data(), 4));

    EXPECT_TRUE(tr_sys_file_seek(fd, 0, TR_SEEK_END, &n, &err));
    EXPECT_EQ(nullptr, err) << *err;
    EXPECT_EQ(4, n);

    EXPECT_TRUE(tr_sys_file_write(fd, " ok", 3, &n, &err));
    EXPECT_EQ(nullptr, err) << *err;
    EXPECT_EQ(3, n);

    EXPECT_TRUE(tr_sys_file_seek(fd, 0, TR_SEEK_SET, &n, &err));
    EXPECT_EQ(nullptr, err) << *err;
    EXPECT_EQ(0, n);

    EXPECT_TRUE(tr_sys_file_read(fd, buf.data(), buf.size(), &n, &err));
    EXPECT_EQ(nullptr, err) << *err;
    EXPECT_EQ(7, n);

    EXPECT_EQ(0, memcmp("tEst ok", buf.data(), 7));

    EXPECT_TRUE(tr_sys_file_write_at(fd, "-", 1, 4, &n, &err));
    EXPECT_EQ(nullptr, err) << *err;
    EXPECT_EQ(1, n);

    EXPECT_TRUE(tr_sys_file_read_at(fd, buf.data(), 5, 2, &n, &err));
    EXPECT_EQ(nullptr, err) << *err;
    EXPECT_EQ(5, n);

    EXPECT_EQ(0, memcmp("st-ok", buf.data(), 5));

    tr_sys_file_close(fd);

    tr_sys_path_remove(path1);
}

TEST_F(FileTest, fileTruncate)
{
    auto const test_dir = createTestDir(currentTestName());

    auto const path1 = tr_pathbuf{ test_dir, "/a"sv };
    auto fd = tr_sys_file_open(path1, TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE, 0600);

    tr_error* err = nullptr;
    EXPECT_TRUE(tr_sys_file_truncate(fd, 10, &err));
    EXPECT_EQ(nullptr, err) << *err;
    tr_sys_path_info info;
    tr_sys_file_get_info(fd, &info);
    EXPECT_EQ(10, info.size);

    EXPECT_TRUE(tr_sys_file_truncate(fd, 20, &err));
    EXPECT_EQ(nullptr, err) << *err;
    tr_sys_file_get_info(fd, &info);
    EXPECT_EQ(20, info.size);

    EXPECT_TRUE(tr_sys_file_truncate(fd, 0, &err));
    EXPECT_EQ(nullptr, err) << *err;
    tr_sys_file_get_info(fd, &info);
    EXPECT_EQ(0, info.size);

    EXPECT_TRUE(tr_sys_file_truncate(fd, 50, &err));
    EXPECT_EQ(nullptr, err) << *err;

    tr_sys_file_close(fd);

    tr_sys_path_get_info(path1, 0, &info);
    EXPECT_EQ(50, info.size);

    fd = tr_sys_file_open(path1, TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE, 0600);

    EXPECT_TRUE(tr_sys_file_truncate(fd, 25, &err));
    EXPECT_EQ(nullptr, err) << *err;

    tr_sys_file_close(fd);

    tr_sys_path_get_info(path1, 0, &info);
    EXPECT_EQ(25, info.size);

    tr_sys_path_remove(path1);
}

TEST_F(FileTest, filePreallocate)
{
    auto const test_dir = createTestDir(currentTestName());

    auto const path1 = tr_pathbuf{ test_dir, "/a"sv };
    auto fd = tr_sys_file_open(path1, TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE, 0600);

    tr_error* err = nullptr;
    auto prealloc_size = size_t{ 50 };
    if (tr_sys_file_preallocate(fd, prealloc_size, 0, &err))
    {
        EXPECT_EQ(nullptr, err) << *err;
        tr_sys_path_info info;
        tr_sys_file_get_info(fd, &info);
        EXPECT_EQ(prealloc_size, info.size);
    }
    else
    {
        EXPECT_NE(nullptr, err);
        fprintf(stderr, "WARNING: [%s] unable to preallocate file (full): %s (%d)\n", __FUNCTION__, err->message, err->code);
        tr_error_clear(&err);
    }

    tr_sys_file_close(fd);

    tr_sys_path_remove(path1);

    fd = tr_sys_file_open(path1, TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE, 0600);

    prealloc_size = 500 * 1024 * 1024;
    if (tr_sys_file_preallocate(fd, prealloc_size, TR_SYS_FILE_PREALLOC_SPARSE, &err))
    {
        EXPECT_EQ(nullptr, err) << *err;
        tr_sys_path_info info;
        tr_sys_file_get_info(fd, &info);
        EXPECT_EQ(prealloc_size, info.size);
    }
    else
    {
        EXPECT_NE(nullptr, err) << *err;
        fprintf(stderr, "WARNING: [%s] unable to preallocate file (sparse): %s (%d)\n", __FUNCTION__, err->message, err->code);
        tr_error_clear(&err);
    }

    tr_sys_file_close(fd);

    tr_sys_path_remove(path1);
}

TEST_F(FileTest, map)
{
    auto const test_dir = createTestDir(currentTestName());

    auto const path1 = tr_pathbuf{ test_dir, "/a"sv };
    auto const contents = std::string{ "test" };
    createFileWithContents(path1, contents.data());

    auto fd = tr_sys_file_open(path1, TR_SYS_FILE_READ | TR_SYS_FILE_WRITE, 0600);

    tr_error* err = nullptr;
    auto map_len = contents.size();
    auto* view = static_cast<char*>(tr_sys_file_map_for_reading(fd, 0, map_len, &err));
    EXPECT_NE(nullptr, view);
    EXPECT_EQ(nullptr, err) << *err;
    EXPECT_EQ(contents, std::string(view, map_len));

#ifdef HAVE_UNIFIED_BUFFER_CACHE

    auto const contents_2 = std::string{ "more" };
    auto n_written = uint64_t{};
    tr_sys_file_write_at(fd, contents_2.data(), contents_2.size(), 0, &n_written, &err);
    EXPECT_EQ(map_len, contents_2.size());
    EXPECT_EQ(map_len, n_written);
    EXPECT_EQ(nullptr, err) << *err;
    EXPECT_EQ(contents_2, std::string(view, map_len));

#endif

    EXPECT_TRUE(tr_sys_file_unmap(view, map_len, &err));
    EXPECT_EQ(nullptr, err) << *err;

    tr_sys_file_close(fd);

    tr_sys_path_remove(path1);
}

TEST_F(FileTest, fileUtilities)
{
    auto const test_dir = createTestDir(currentTestName());

    auto const path1 = tr_pathbuf{ test_dir, "/a"sv };
    auto const contents = std::string{ "a\nbc\r\ndef\nghij\r\n\n\nklmno\r" };
    createFileWithContents(path1, contents.data());

    auto fd = tr_sys_file_open(path1, TR_SYS_FILE_READ, 0);

    tr_error* err = nullptr;
    auto buffer = std::array<char, 16>{};
    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer.data(), buffer.size(), &err));
    EXPECT_EQ(nullptr, err) << *err;
    EXPECT_STREQ("a", buffer.data());
    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer.data(), buffer.size(), &err));
    EXPECT_EQ(nullptr, err) << *err;
    EXPECT_STREQ("bc", buffer.data());
    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer.data(), buffer.size(), &err));
    EXPECT_EQ(nullptr, err) << *err;
    EXPECT_STREQ("def", buffer.data());
    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer.data(), buffer.size(), &err));
    EXPECT_EQ(nullptr, err) << *err;
    EXPECT_STREQ("ghij", buffer.data());
    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer.data(), buffer.size(), &err));
    EXPECT_EQ(nullptr, err) << *err;
    EXPECT_STREQ("", buffer.data());
    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer.data(), buffer.size(), &err));
    EXPECT_EQ(nullptr, err) << *err;
    EXPECT_STREQ("", buffer.data());
    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer.data(), 4, &err));
    EXPECT_EQ(nullptr, err) << *err;
    EXPECT_STREQ("klmn", buffer.data());
    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer.data(), buffer.size(), &err));
    EXPECT_EQ(nullptr, err) << *err;
    EXPECT_STREQ("o", buffer.data());
    EXPECT_FALSE(tr_sys_file_read_line(fd, buffer.data(), buffer.size(), &err));
    EXPECT_EQ(nullptr, err) << *err;
    EXPECT_STREQ("o", buffer.data()); // on EOF, buffer stays unchanged

    tr_sys_file_close(fd);

    fd = tr_sys_file_open(path1, TR_SYS_FILE_READ | TR_SYS_FILE_WRITE | TR_SYS_FILE_TRUNCATE, 0);

    EXPECT_TRUE(tr_sys_file_write_line(fd, "p", &err));
    EXPECT_EQ(nullptr, err) << *err;
    EXPECT_TRUE(tr_sys_file_write_line(fd, "", &err));
    EXPECT_EQ(nullptr, err) << *err;
    EXPECT_TRUE(tr_sys_file_write_line(fd, "qr", &err));
    EXPECT_EQ(nullptr, err) << *err;
    EXPECT_TRUE(tr_sys_file_write_line(fd, "stu", &err));
    EXPECT_EQ(nullptr, err) << *err;
    EXPECT_TRUE(tr_sys_file_write_line(fd, "", &err));
    EXPECT_EQ(nullptr, err) << *err;
    EXPECT_TRUE(tr_sys_file_write_line(fd, "", &err));
    EXPECT_EQ(nullptr, err) << *err;
    EXPECT_TRUE(tr_sys_file_write_line(fd, "vwxy2", &err));
    EXPECT_EQ(nullptr, err) << *err;

    tr_sys_file_seek(fd, 0, TR_SEEK_SET, nullptr);

    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer.data(), buffer.size(), &err));
    EXPECT_EQ(nullptr, err) << *err;
    EXPECT_STREQ("p", buffer.data());
    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer.data(), buffer.size(), &err));
    EXPECT_EQ(nullptr, err) << *err;
    EXPECT_STREQ("", buffer.data());
    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer.data(), buffer.size(), &err));
    EXPECT_EQ(nullptr, err) << *err;
    EXPECT_STREQ("qr", buffer.data());
    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer.data(), buffer.size(), &err));
    EXPECT_EQ(nullptr, err) << *err;
    EXPECT_STREQ("stu", buffer.data());
    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer.data(), buffer.size(), &err));
    EXPECT_EQ(nullptr, err) << *err;
    EXPECT_STREQ("", buffer.data());
    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer.data(), buffer.size(), &err));
    EXPECT_EQ(nullptr, err) << *err;
    EXPECT_STREQ("", buffer.data());
    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer.data(), buffer.size(), &err));
    EXPECT_EQ(nullptr, err) << *err;
    EXPECT_STREQ("vwxy2", buffer.data());
    EXPECT_FALSE(tr_sys_file_read_line(fd, buffer.data(), buffer.size(), &err));
    EXPECT_EQ(nullptr, err) << *err;
    EXPECT_STREQ("vwxy2", buffer.data()); // on EOF, buffer stays unchanged

    tr_sys_file_close(fd);

    tr_sys_path_remove(path1);
}

TEST_F(FileTest, dirCreate)
{
    auto const test_dir = createTestDir(currentTestName());

    auto const path1 = tr_pathbuf{ test_dir, "/a"sv };
    auto const path2 = tr_pathbuf{ path1, "/b"sv };

    // Can create directory which has parent
    tr_error* err = nullptr;
    EXPECT_TRUE(tr_sys_dir_create(path1, 0, 0700, &err));
    EXPECT_EQ(nullptr, err) << *err;
    EXPECT_TRUE(tr_sys_path_exists(path1));
    EXPECT_TRUE(validatePermissions(path1, 0700));

    tr_sys_path_remove(path1);
    createFileWithContents(path1, "test");

    // Can't create directory where file already exists
    EXPECT_FALSE(tr_sys_dir_create(path1, 0, 0700, &err));
    EXPECT_NE(nullptr, err);
    tr_error_clear(&err);
    EXPECT_FALSE(tr_sys_dir_create(path1, TR_SYS_DIR_CREATE_PARENTS, 0700, &err));
    EXPECT_NE(nullptr, err);
    tr_error_clear(&err);

    tr_sys_path_remove(path1);

    // Can't create directory which has no parent
    EXPECT_FALSE(tr_sys_dir_create(path2, 0, 0700, &err));
    EXPECT_NE(nullptr, err);
    EXPECT_FALSE(tr_sys_path_exists(path2));
    tr_error_clear(&err);

    // Can create directory with parent directories
    EXPECT_TRUE(tr_sys_dir_create(path2, TR_SYS_DIR_CREATE_PARENTS, 0751, &err));
    EXPECT_EQ(nullptr, err) << *err;
    EXPECT_TRUE(tr_sys_path_exists(path1));
    EXPECT_TRUE(tr_sys_path_exists(path2));
    EXPECT_TRUE(validatePermissions(path1, 0751));
    EXPECT_TRUE(validatePermissions(path2, 0751));

    // Can create existing directory (no-op)
    EXPECT_TRUE(tr_sys_dir_create(path1, 0, 0700, &err));
    EXPECT_EQ(nullptr, err) << *err;
    EXPECT_TRUE(tr_sys_dir_create(path1, TR_SYS_DIR_CREATE_PARENTS, 0700, &err));
    EXPECT_EQ(nullptr, err) << *err;

    tr_sys_path_remove(path2);
    tr_sys_path_remove(path1);
}

TEST_F(FileTest, dirRead)
{
    auto const test_dir = createTestDir(currentTestName());

    auto const path1 = tr_pathbuf{ test_dir, "/a"sv };
    auto const path2 = tr_pathbuf{ test_dir, "/b"sv };

    bool have1;
    bool have2;
    testDirReadImpl(test_dir, &have1, &have2);
    EXPECT_FALSE(have1);
    EXPECT_FALSE(have2);

    createFileWithContents(path1, "test");
    testDirReadImpl(test_dir, &have1, &have2);
    EXPECT_TRUE(have1);
    EXPECT_FALSE(have2);

    createFileWithContents(path2, "test");
    testDirReadImpl(test_dir, &have1, &have2);
    EXPECT_TRUE(have1);
    EXPECT_TRUE(have2);

    tr_sys_path_remove(path1);
    testDirReadImpl(test_dir, &have1, &have2);
    EXPECT_FALSE(have1);
    EXPECT_TRUE(have2);
}

} // namespace test

} // namespace libtransmission
