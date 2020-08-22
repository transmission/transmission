/*
 * This file Copyright (C) 2013-2017 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include "transmission.h"
#include "error.h"
#include "file.h"
#include "tr-macros.h"

#include "test-fixtures.h"

#include <array>
#include <cstring>
#include <string>

#ifndef _WIN32
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#else
#include <windows.h>
#endif

#if !defined(__OpenBSD__)
#define HAVE_UNIFIED_BUFFER_CACHE
#endif

#ifndef _WIN32
#define NATIVE_PATH_SEP "/"
#else
#define NATIVE_PATH_SEP "\\"
#endif

namespace libtransmission
{

namespace test
{

class FileTest : public SessionTest
{
protected:
    auto createTestDir(std::string const& child_name)
    {
        auto test_dir = makeString(tr_buildPath(tr_sessionGetConfigDir(session_), child_name.c_str(), nullptr));
        tr_sys_dir_create(test_dir.data(), 0, 0777, nullptr);
        return test_dir;
    }

    bool createSymlink(char const* dst_path, char const* src_path, bool dst_is_dir)
    {
#ifndef _WIN32

        TR_UNUSED(dst_is_dir);

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

    bool createHardlink(char const* dst_path, char const* src_path)
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

    void clearPathInfo(tr_sys_path_info* info)
    {
        *info = {};
    }

    bool pathContainsNoSymlinks(char const* path)
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

            auto const path_part = makeString(tr_strndup(path, size_t(slash_pos - path + 1)));

            if (!tr_sys_path_get_info(path_part.c_str(), TR_SYS_PATH_NO_FOLLOW, &info, nullptr) ||
                (info.type != TR_SYS_PATH_IS_FILE && info.type != TR_SYS_PATH_IS_DIRECTORY))
            {
                return false;
            }

            p = slash_pos + 1;
        }

        return true;
    }

    bool validatePermissions(char const* path, unsigned int permissions)
    {
#ifndef _WIN32

        struct stat sb = {};
        return stat(path, &sb) != -1 && (sb.st_mode & 0777) == permissions;

#else

        TR_UNUSED(path);
        TR_UNUSED(permissions);

        /* No UNIX permissions on Windows */
        return true;

#endif
    }

    struct XnameTestData
    {
        char const* input;
        char const* output;
    };

    void testPathXname(XnameTestData const* data, size_t data_size, char* (*func)(char const*, tr_error**))
    {
        for (size_t i = 0; i < data_size; ++i)
        {
            tr_error* err = nullptr;
            char* name = func(data[i].input, &err);

            if (data[i].output != nullptr)
            {
                EXPECT_NE(nullptr, name);
                EXPECT_EQ(nullptr, err);
                EXPECT_STREQ(data[i].output, name);
                tr_free(name);
            }
            else
            {
                EXPECT_EQ(nullptr, name);
                EXPECT_NE(nullptr, err);
                tr_error_clear(&err);
            }
        }
    }

    static void testDirReadImpl(std::string const& path, bool* have1, bool* have2)
    {
        *have1 = *have2 = false;

        tr_error* err = nullptr;
        auto dd = tr_sys_dir_open(path.c_str(), &err);
        EXPECT_NE(TR_BAD_SYS_DIR, dd);
        EXPECT_EQ(nullptr, err);

        char const* name;
        while ((name = tr_sys_dir_read_name(dd, &err)) != nullptr)
        {
            EXPECT_EQ(nullptr, err);

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

        EXPECT_EQ(nullptr, err);

        EXPECT_TRUE(tr_sys_dir_close(dd, &err));
        EXPECT_EQ(nullptr, err);
    }
};

TEST_F(FileTest, getInfo)
{
    auto const test_dir = createTestDir(currentTestName());
    tr_sys_path_info info;

    char* path1 = tr_buildPath(test_dir.data(), "a", nullptr);
    char* path2 = tr_buildPath(test_dir.data(), "b", nullptr);

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
    EXPECT_EQ(nullptr, err);
    EXPECT_EQ(TR_SYS_PATH_IS_FILE, info.type);
    EXPECT_EQ(4, info.size);
    EXPECT_GT(info.last_modified_at, t - 1);
    EXPECT_LE(info.last_modified_at, time(nullptr) + 1);

    // Good file info (by handle)
    auto fd = tr_sys_file_open(path1, TR_SYS_FILE_READ, 0, nullptr);
    clearPathInfo(&info);
    EXPECT_TRUE(tr_sys_file_get_info(fd, &info, &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_EQ(TR_SYS_PATH_IS_FILE, info.type);
    EXPECT_EQ(4, info.size);
    EXPECT_GT(info.last_modified_at, t - 1);
    EXPECT_LE(info.last_modified_at, time(nullptr) + 1);
    tr_sys_file_close(fd, nullptr);

    tr_sys_path_remove(path1, nullptr);

    // Good directory info
    t = time(nullptr);
    tr_sys_dir_create(path1, 0, 0777, nullptr);
    clearPathInfo(&info);
    EXPECT_TRUE(tr_sys_path_get_info(path1, 0, &info, &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_EQ(TR_SYS_PATH_IS_DIRECTORY, info.type);
    EXPECT_NE(uint64_t(-1), info.size);
    EXPECT_GE(info.last_modified_at, t - 1);
    EXPECT_LE(info.last_modified_at, time(nullptr) + 1);
    tr_sys_path_remove(path1, nullptr);

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
        EXPECT_EQ(nullptr, err);
        EXPECT_EQ(TR_SYS_PATH_IS_FILE, info.type);
        EXPECT_EQ(4, info.size);
        EXPECT_GE(info.last_modified_at, t - 1);
        EXPECT_LE(info.last_modified_at, time(nullptr) + 1);

        // Good file info (by handle)
        fd = tr_sys_file_open(path1, TR_SYS_FILE_READ, 0, nullptr);
        clearPathInfo(&info);
        EXPECT_TRUE(tr_sys_file_get_info(fd, &info, &err));
        EXPECT_EQ(nullptr, err);
        EXPECT_EQ(TR_SYS_PATH_IS_FILE, info.type);
        EXPECT_EQ(4, info.size);
        EXPECT_GE(info.last_modified_at, t - 1);
        EXPECT_LE(info.last_modified_at, time(nullptr) + 1);
        tr_sys_file_close(fd, nullptr);

        tr_sys_path_remove(path2, nullptr);
        tr_sys_path_remove(path1, nullptr);

        // Good directory info
        t = time(nullptr);
        tr_sys_dir_create(path2, 0, 0777, nullptr);
        EXPECT_TRUE(createSymlink(path1, path2, true)); /* Win32: directory and file symlinks differ :( */
        clearPathInfo(&info);
        EXPECT_TRUE(tr_sys_path_get_info(path1, 0, &info, &err));
        EXPECT_EQ(nullptr, err);
        EXPECT_EQ(TR_SYS_PATH_IS_DIRECTORY, info.type);
        EXPECT_NE(uint64_t(-1), info.size);
        EXPECT_GE(info.last_modified_at, t - 1);
        EXPECT_LE(info.last_modified_at, time(nullptr) + 1);

        tr_sys_path_remove(path2, nullptr);
        tr_sys_path_remove(path1, nullptr);
    }
    else
    {
        fprintf(stderr, "WARNING: [%s] unable to run symlink tests\n", __FUNCTION__);
    }

    tr_free(path2);
    tr_free(path1);
}

TEST_F(FileTest, pathExists)
{
    auto const test_dir = createTestDir(currentTestName());

    auto* path1 = tr_buildPath(test_dir.data(), "a", nullptr);
    auto* path2 = tr_buildPath(test_dir.data(), "b", nullptr);

    // Non-existent file does not exist
    tr_error* err = nullptr;
    EXPECT_FALSE(tr_sys_path_exists(path1, &err));
    EXPECT_EQ(nullptr, err);

    // Create file and see that it exists
    createFileWithContents(path1, "test");
    EXPECT_TRUE(tr_sys_path_exists(path1, &err));
    EXPECT_EQ(nullptr, err);

    tr_sys_path_remove(path1, nullptr);

    // Create directory and see that it exists
    tr_sys_dir_create(path1, 0, 0777, nullptr);
    EXPECT_TRUE(tr_sys_path_exists(path1, &err));
    EXPECT_EQ(nullptr, err);

    tr_sys_path_remove(path1, nullptr);

    if (createSymlink(path1, path2, false))
    {
        // Non-existent file does not exist (via symlink)
        EXPECT_FALSE(tr_sys_path_exists(path1, &err));
        EXPECT_EQ(nullptr, err);

        // Create file and see that it exists (via symlink)
        createFileWithContents(path2, "test");
        EXPECT_TRUE(tr_sys_path_exists(path1, &err));
        EXPECT_EQ(nullptr, err);

        tr_sys_path_remove(path2, nullptr);
        tr_sys_path_remove(path1, nullptr);

        /* Create directory and see that it exists (via symlink) */
        tr_sys_dir_create(path2, 0, 0777, nullptr);
        EXPECT_TRUE(createSymlink(path1, path2, true)); /* Win32: directory and file symlinks differ :( */
        EXPECT_TRUE(tr_sys_path_exists(path1, &err));
        EXPECT_EQ(nullptr, err);

        tr_sys_path_remove(path2, nullptr);
        tr_sys_path_remove(path1, nullptr);
    }
    else
    {
        fprintf(stderr, "WARNING: [%s] unable to run symlink tests\n", __FUNCTION__);
    }

    tr_free(path2);
    tr_free(path1);
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
    auto const test_dir = createTestDir(currentTestName());

    auto* path1 = tr_buildPath(test_dir.data(), "a", nullptr);
    auto* path2 = tr_buildPath(test_dir.data(), "b", nullptr);
    auto* path3 = tr_buildPath(path2, "c", nullptr);

    /* Two non-existent files are not the same */
    tr_error* err = nullptr;
    EXPECT_FALSE(tr_sys_path_is_same(path1, path1, &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_FALSE(tr_sys_path_is_same(path1, path2, &err));
    EXPECT_EQ(nullptr, err);

    /* Two same files are the same */
    createFileWithContents(path1, "test");
    EXPECT_TRUE(tr_sys_path_is_same(path1, path1, &err));
    EXPECT_EQ(nullptr, err);

    /* Existent and non-existent files are not the same */
    EXPECT_FALSE(tr_sys_path_is_same(path1, path2, &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_FALSE(tr_sys_path_is_same(path2, path1, &err));
    EXPECT_EQ(nullptr, err);

    /* Two separate files (even with same content) are not the same */
    createFileWithContents(path2, "test");
    EXPECT_FALSE(tr_sys_path_is_same(path1, path2, &err));
    EXPECT_EQ(nullptr, err);

    tr_sys_path_remove(path1, nullptr);

    /* Two same directories are the same */
    tr_sys_dir_create(path1, 0, 0777, nullptr);
    EXPECT_TRUE(tr_sys_path_is_same(path1, path1, &err));
    EXPECT_EQ(nullptr, err);

    /* File and directory are not the same */
    EXPECT_FALSE(tr_sys_path_is_same(path1, path2, &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_FALSE(tr_sys_path_is_same(path2, path1, &err));
    EXPECT_EQ(nullptr, err);

    tr_sys_path_remove(path2, nullptr);

    /* Two separate directories are not the same */
    tr_sys_dir_create(path2, 0, 0777, nullptr);
    EXPECT_FALSE(tr_sys_path_is_same(path1, path2, &err));
    EXPECT_EQ(nullptr, err);

    tr_sys_path_remove(path1, nullptr);
    tr_sys_path_remove(path2, nullptr);

    if (createSymlink(path1, ".", true))
    {
        /* Directory and symlink pointing to it are the same */
        EXPECT_TRUE(tr_sys_path_is_same(path1, test_dir.data(), &err));
        EXPECT_EQ(nullptr, err);
        EXPECT_TRUE(tr_sys_path_is_same(test_dir.data(), path1, &err));
        EXPECT_EQ(nullptr, err);

        /* Non-existent file and symlink are not the same */
        EXPECT_FALSE(tr_sys_path_is_same(path1, path2, &err));
        EXPECT_EQ(nullptr, err);
        EXPECT_FALSE(tr_sys_path_is_same(path2, path1, &err));
        EXPECT_EQ(nullptr, err);

        /* Symlinks pointing to different directories are not the same */
        createSymlink(path2, "..", true);
        EXPECT_FALSE(tr_sys_path_is_same(path1, path2, &err));
        EXPECT_EQ(nullptr, err);
        EXPECT_FALSE(tr_sys_path_is_same(path2, path1, &err));
        EXPECT_EQ(nullptr, err);

        tr_sys_path_remove(path2, nullptr);

        /* Symlinks pointing to same directory are the same */
        createSymlink(path2, ".", true);
        EXPECT_TRUE(tr_sys_path_is_same(path1, path2, &err));
        EXPECT_EQ(nullptr, err);

        tr_sys_path_remove(path2, nullptr);

        /* Directory and symlink pointing to another directory are not the same */
        tr_sys_dir_create(path2, 0, 0777, nullptr);
        EXPECT_FALSE(tr_sys_path_is_same(path1, path2, &err));
        EXPECT_EQ(nullptr, err);
        EXPECT_FALSE(tr_sys_path_is_same(path2, path1, &err));
        EXPECT_EQ(nullptr, err);

        /* Symlinks pointing to same directory are the same */
        createSymlink(path3, "..", true);
        EXPECT_TRUE(tr_sys_path_is_same(path1, path3, &err));
        EXPECT_EQ(nullptr, err);

        tr_sys_path_remove(path1, nullptr);

        /* File and symlink pointing to directory are not the same */
        createFileWithContents(path1, "test");
        EXPECT_FALSE(tr_sys_path_is_same(path1, path3, &err));
        EXPECT_EQ(nullptr, err);
        EXPECT_FALSE(tr_sys_path_is_same(path3, path1, &err));
        EXPECT_EQ(nullptr, err);

        tr_sys_path_remove(path3, nullptr);

        /* File and symlink pointing to same file are the same */
        createSymlink(path3, path1, false);
        EXPECT_TRUE(tr_sys_path_is_same(path1, path3, &err));
        EXPECT_EQ(nullptr, err);
        EXPECT_TRUE(tr_sys_path_is_same(path3, path1, &err));
        EXPECT_EQ(nullptr, err);

        /* Symlinks pointing to non-existent files are not the same */
        tr_sys_path_remove(path1, nullptr);
        createSymlink(path1, "missing", false);
        tr_sys_path_remove(path3, nullptr);
        createSymlink(path3, "missing", false);
        EXPECT_FALSE(tr_sys_path_is_same(path1, path3, &err));
        EXPECT_EQ(nullptr, err);
        EXPECT_FALSE(tr_sys_path_is_same(path3, path1, &err));
        EXPECT_EQ(nullptr, err);

        tr_sys_path_remove(path3, nullptr);

        /* Symlinks pointing to same non-existent file are not the same */
        createSymlink(path3, ".." NATIVE_PATH_SEP "missing", false);
        EXPECT_FALSE(tr_sys_path_is_same(path1, path3, &err));
        EXPECT_EQ(nullptr, err);
        EXPECT_FALSE(tr_sys_path_is_same(path3, path1, &err));
        EXPECT_EQ(nullptr, err);

        /* Non-existent file and symlink pointing to non-existent file are not the same */
        tr_sys_path_remove(path3, nullptr);
        EXPECT_FALSE(tr_sys_path_is_same(path1, path3, &err));
        EXPECT_EQ(nullptr, err);
        EXPECT_FALSE(tr_sys_path_is_same(path3, path1, &err));
        EXPECT_EQ(nullptr, err);

        tr_sys_path_remove(path2, nullptr);
        tr_sys_path_remove(path1, nullptr);
    }
    else
    {
        fprintf(stderr, "WARNING: [%s] unable to run symlink tests\n", __FUNCTION__);
    }

    tr_free(path3);
    path3 = tr_buildPath(test_dir.data(), "c", nullptr);

    createFileWithContents(path1, "test");

    if (createHardlink(path2, path1))
    {
        /* File and hardlink to it are the same */
        EXPECT_TRUE(tr_sys_path_is_same(path1, path2, &err));
        EXPECT_EQ(nullptr, err);

        /* Two hardlinks to the same file are the same */
        createHardlink(path3, path2);
        EXPECT_TRUE(tr_sys_path_is_same(path2, path3, &err));
        EXPECT_EQ(nullptr, err);
        EXPECT_TRUE(tr_sys_path_is_same(path1, path3, &err));
        EXPECT_EQ(nullptr, err);

        tr_sys_path_remove(path2, nullptr);

        EXPECT_TRUE(tr_sys_path_is_same(path1, path3, &err));
        EXPECT_EQ(nullptr, err);

        tr_sys_path_remove(path3, nullptr);

        /* File and hardlink to another file are not the same */
        createFileWithContents(path3, "test");
        createHardlink(path2, path3);
        EXPECT_FALSE(tr_sys_path_is_same(path1, path2, &err));
        EXPECT_EQ(nullptr, err);
        EXPECT_FALSE(tr_sys_path_is_same(path2, path1, &err));
        EXPECT_EQ(nullptr, err);

        tr_sys_path_remove(path3, nullptr);
        tr_sys_path_remove(path2, nullptr);
    }
    else
    {
        fprintf(stderr, "WARNING: [%s] unable to run hardlink tests\n", __FUNCTION__);
    }

    if (createSymlink(path2, path1, false) && createHardlink(path3, path1))
    {
        EXPECT_TRUE(tr_sys_path_is_same(path2, path3, &err));
        EXPECT_EQ(nullptr, err);
    }
    else
    {
        fprintf(stderr, "WARNING: [%s] unable to run combined symlink and hardlink tests\n", __FUNCTION__);
    }

    tr_sys_path_remove(path3, nullptr);
    tr_sys_path_remove(path2, nullptr);
    tr_sys_path_remove(path1, nullptr);

    tr_free(path3);
    tr_free(path2);
    tr_free(path1);
}

TEST_F(FileTest, pathResolve)
{
    auto const test_dir = createTestDir(currentTestName());

    tr_error* err = nullptr;
    auto* path1 = tr_buildPath(test_dir.data(), "a", nullptr);
    auto* path2 = tr_buildPath(test_dir.data(), "b", nullptr);

    createFileWithContents(path1, "test");

    if (createSymlink(path2, path1, false))
    {
        auto tmp = makeString(tr_sys_path_resolve(path2, &err));
        EXPECT_EQ(nullptr, err);
        EXPECT_TRUE(pathContainsNoSymlinks(tmp.c_str()));

        tr_sys_path_remove(path2, nullptr);
        tr_sys_path_remove(path1, nullptr);

        tr_sys_dir_create(path1, 0, 0755, nullptr);
        EXPECT_TRUE(createSymlink(path2, path1, true)); /* Win32: directory and file symlinks differ :( */
        tmp = makeString(tr_sys_path_resolve(path2, &err));
        EXPECT_EQ(nullptr, err);
        EXPECT_TRUE(pathContainsNoSymlinks(tmp.c_str()));
    }
    else
    {
        fprintf(stderr, "WARNING: [%s] unable to run symlink tests\n", __FUNCTION__);
    }

    tr_sys_path_remove(path2, nullptr);
    tr_sys_path_remove(path1, nullptr);

    tr_free(path2);
    tr_free(path1);

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
        EXPECT_EQ(nullptr, err);
        tr_free(tmp);
    }

#endif
}

TEST_F(FileTest, pathBasenameDirname)
{
    auto const common_xname_tests = std::vector<XnameTestData>{
        XnameTestData{ "/", "/" },
        { "", "." },
#ifdef _WIN32
        {
            "\\", "/"
        },
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
        { "c:\\a\\b*c\\d", nullptr }
#else
        {
            "////", "/"
        }
#endif
    };

    testPathXname(common_xname_tests.data(), common_xname_tests.size(), tr_sys_path_basename);
    testPathXname(common_xname_tests.data(), common_xname_tests.size(), tr_sys_path_dirname);

    auto const basename_tests = std::vector<XnameTestData>{
        XnameTestData{ "a", "a" },
        { "aa", "aa" },
        { "/aa", "aa" },
        { "/a/b/c", "c" },
        { "/a/b/c/", "c" },
#ifdef _WIN32
        {
            "c:\\a\\b\\c", "c"
        },
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
        { "\\a", "a" }
#endif
    };

    testPathXname(basename_tests.data(), basename_tests.size(), tr_sys_path_basename);

    auto const dirname_tests = std::vector<XnameTestData>{
        XnameTestData{ "/a/b/c", "/a/b" },
        { "a/b/c", "a/b" },
        { "a/b/c/", "a/b" },
        { "a", "." },
        { "a/", "." },
#ifdef _WIN32
        {
            "C:\\a/b\\c", "C:\\a/b"
        },
        { "C:\\a/b\\c\\", "C:\\a/b" },
        { "C:\\a/b", "C:\\a" },
        { "C:/a", "C:" },
        { "C:", "C:" },
        { "C:/", "C:" },
        { "C:\\", "C:" },
        { "c:a/b", "c:a" },
        { "c:a", "c:." },
        { "c:.", "c:." },
        { "\\\\a\\b\\c", "\\\\a\\b" },
        { "\\\\a\\b\\c/", "\\\\a\\b" },
        { "//a/b", "//a" },
        { "//1.2.3.4/b", "//1.2.3.4" },
        { "\\\\a", "\\\\" },
        { "\\\\1.2.3.4", "\\\\" },
        { "\\\\", "\\\\" },
        { "a/b\\c", "a/b" }
#endif
    };

    testPathXname(dirname_tests.data(), dirname_tests.size(), tr_sys_path_dirname);

    /* TODO: is_same(dirname(x) + '/' + basename(x), x) */
}

TEST_F(FileTest, pathRename)
{
    auto const test_dir = createTestDir(currentTestName());

    auto* path1 = tr_buildPath(test_dir.data(), "a", nullptr);
    auto* path2 = tr_buildPath(test_dir.data(), "b", nullptr);
    auto* path3 = tr_buildPath(path2, "c", nullptr);

    createFileWithContents(path1, "test");

    /* Preconditions */
    EXPECT_TRUE(tr_sys_path_exists(path1, nullptr));
    EXPECT_FALSE(tr_sys_path_exists(path2, nullptr));

    /* Forward rename works */
    tr_error* err = nullptr;
    EXPECT_TRUE(tr_sys_path_rename(path1, path2, &err));
    EXPECT_FALSE(tr_sys_path_exists(path1, nullptr));
    EXPECT_TRUE(tr_sys_path_exists(path2, nullptr));
    EXPECT_EQ(nullptr, err);

    /* Backward rename works */
    EXPECT_TRUE(tr_sys_path_rename(path2, path1, &err));
    EXPECT_TRUE(tr_sys_path_exists(path1, nullptr));
    EXPECT_FALSE(tr_sys_path_exists(path2, nullptr));
    EXPECT_EQ(nullptr, err);

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
    EXPECT_EQ(nullptr, err);

    tr_sys_dir_create(path2, 0, 0777, nullptr);

    /* Renaming file does not overwrite existing directory, and vice versa */
    EXPECT_FALSE(tr_sys_path_rename(path1, path2, &err));
    EXPECT_NE(nullptr, err);
    tr_error_clear(&err);
    EXPECT_FALSE(tr_sys_path_rename(path2, path1, &err));
    EXPECT_NE(nullptr, err);
    tr_error_clear(&err);

    tr_sys_path_remove(path2, nullptr);

    tr_free(path3);
    path3 = tr_buildPath(test_dir.data(), "c", nullptr);

    if (createSymlink(path2, path1, false))
    {
        /* Preconditions */
        EXPECT_TRUE(tr_sys_path_exists(path2, nullptr));
        EXPECT_FALSE(tr_sys_path_exists(path3, nullptr));
        EXPECT_TRUE(tr_sys_path_is_same(path1, path2, nullptr));

        /* Rename of symlink works, files stay the same */
        EXPECT_TRUE(tr_sys_path_rename(path2, path3, &err));
        EXPECT_EQ(nullptr, err);
        EXPECT_FALSE(tr_sys_path_exists(path2, nullptr));
        EXPECT_TRUE(tr_sys_path_exists(path3, nullptr));
        EXPECT_TRUE(tr_sys_path_is_same(path1, path3, nullptr));

        tr_sys_path_remove(path3, nullptr);
    }
    else
    {
        fprintf(stderr, "WARNING: [%s] unable to run symlink tests\n", __FUNCTION__);
    }

    if (createHardlink(path2, path1))
    {
        /* Preconditions */
        EXPECT_TRUE(tr_sys_path_exists(path2, nullptr));
        EXPECT_FALSE(tr_sys_path_exists(path3, nullptr));
        EXPECT_TRUE(tr_sys_path_is_same(path1, path2, nullptr));

        /* Rename of hardlink works, files stay the same */
        EXPECT_TRUE(tr_sys_path_rename(path2, path3, &err));
        EXPECT_EQ(nullptr, err);
        EXPECT_FALSE(tr_sys_path_exists(path2, nullptr));
        EXPECT_TRUE(tr_sys_path_exists(path3, nullptr));
        EXPECT_TRUE(tr_sys_path_is_same(path1, path3, nullptr));

        tr_sys_path_remove(path3, nullptr);
    }
    else
    {
        fprintf(stderr, "WARNING: [%s] unable to run hardlink tests\n", __FUNCTION__);
    }

    tr_sys_path_remove(path1, nullptr);

    tr_free(path3);
    tr_free(path2);
    tr_free(path1);
}

TEST_F(FileTest, pathRemove)
{
    auto const test_dir = createTestDir(currentTestName());

    auto* path1 = tr_buildPath(test_dir.data(), "a", nullptr);
    auto* path2 = tr_buildPath(test_dir.data(), "b", nullptr);
    auto* path3 = tr_buildPath(path2, "c", nullptr);

    /* Can't remove non-existent file/directory */
    EXPECT_FALSE(tr_sys_path_exists(path1, nullptr));
    tr_error* err = nullptr;
    EXPECT_FALSE(tr_sys_path_remove(path1, &err));
    EXPECT_NE(nullptr, err);
    EXPECT_FALSE(tr_sys_path_exists(path1, nullptr));
    tr_error_clear(&err);

    /* Removing file works */
    createFileWithContents(path1, "test");
    EXPECT_TRUE(tr_sys_path_exists(path1, nullptr));
    EXPECT_TRUE(tr_sys_path_remove(path1, &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_FALSE(tr_sys_path_exists(path1, nullptr));

    /* Removing empty directory works */
    tr_sys_dir_create(path1, 0, 0777, nullptr);
    EXPECT_TRUE(tr_sys_path_exists(path1, nullptr));
    EXPECT_TRUE(tr_sys_path_remove(path1, &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_FALSE(tr_sys_path_exists(path1, nullptr));

    /* Removing non-empty directory fails */
    tr_sys_dir_create(path2, 0, 0777, nullptr);
    createFileWithContents(path3, "test");
    EXPECT_TRUE(tr_sys_path_exists(path2, nullptr));
    EXPECT_TRUE(tr_sys_path_exists(path3, nullptr));
    EXPECT_FALSE(tr_sys_path_remove(path2, &err));
    EXPECT_NE(nullptr, err);
    EXPECT_TRUE(tr_sys_path_exists(path2, nullptr));
    EXPECT_TRUE(tr_sys_path_exists(path3, nullptr));
    tr_error_clear(&err);

    tr_sys_path_remove(path3, nullptr);
    tr_sys_path_remove(path2, nullptr);

    tr_free(path3);
    tr_free(path2);
    tr_free(path1);
}

TEST_F(FileTest, pathNativeSeparators)
{
    EXPECT_EQ(nullptr, tr_sys_path_native_separators(nullptr));

    struct Test
    {
        std::string input;
        std::string expected_output;
    };

    auto const tests = std::array<Test, 5>
    {
        Test{ "", "" },
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
    auto* path1 = tr_buildPath(test_dir.data(), "a", nullptr);
    EXPECT_FALSE(tr_sys_path_exists(path1, nullptr));
    tr_error* err = nullptr;
    EXPECT_TRUE(tr_sys_file_open(path1, TR_SYS_FILE_READ, 0600, &err) == TR_BAD_SYS_FILE);
    EXPECT_NE(nullptr, err);
    EXPECT_FALSE(tr_sys_path_exists(path1, nullptr));
    tr_error_clear(&err);
    EXPECT_TRUE(tr_sys_file_open(path1, TR_SYS_FILE_WRITE, 0600, &err) == TR_BAD_SYS_FILE);
    EXPECT_NE(nullptr, err);
    EXPECT_FALSE(tr_sys_path_exists(path1, nullptr));
    tr_error_clear(&err);

    // can't open directory
    tr_sys_dir_create(path1, 0, 0777, nullptr);
#ifdef _WIN32
    // this works on *NIX
    EXPECT_TRUE(tr_sys_file_open(path1, TR_SYS_FILE_READ, 0600, &err) == TR_BAD_SYS_FILE);
    EXPECT_NE(nullptr, err);
    tr_error_clear(&err);
#endif
    EXPECT_TRUE(tr_sys_file_open(path1, TR_SYS_FILE_WRITE, 0600, &err) == TR_BAD_SYS_FILE);
    EXPECT_NE(nullptr, err);
    tr_error_clear(&err);

    tr_sys_path_remove(path1, nullptr);

    // can create non-existent file
    auto fd = tr_sys_file_open(path1, TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE, 0640, &err);
    EXPECT_NE(TR_BAD_SYS_FILE, fd);
    EXPECT_EQ(nullptr, err);
    tr_sys_file_close(fd, nullptr);
    EXPECT_TRUE(tr_sys_path_exists(path1, nullptr));
    EXPECT_TRUE(validatePermissions(path1, 0640));

    // can open existing file
    EXPECT_TRUE(tr_sys_path_exists(path1, nullptr));
    fd = tr_sys_file_open(path1, TR_SYS_FILE_READ, 0600, &err);
    EXPECT_NE(TR_BAD_SYS_FILE, fd);
    EXPECT_EQ(nullptr, err);
    tr_sys_file_close(fd, nullptr);
    fd = tr_sys_file_open(path1, TR_SYS_FILE_WRITE, 0600, &err);
    EXPECT_NE(TR_BAD_SYS_FILE, fd);
    EXPECT_EQ(nullptr, err);
    tr_sys_file_close(fd, nullptr);

    tr_sys_path_remove(path1, nullptr);
    createFileWithContents(path1, "test");

    /* Can't create new file if it already exists */
    fd = tr_sys_file_open(path1, TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE_NEW, 0640, &err);
    EXPECT_EQ(TR_BAD_SYS_FILE, fd);
    EXPECT_NE(nullptr, err);
    tr_error_clear(&err);
    tr_sys_path_info info;
    tr_sys_path_get_info(path1, TR_SYS_PATH_NO_FOLLOW, &info, nullptr);
    EXPECT_EQ(4, info.size);

    /* Pointer is at the end of file */
    tr_sys_path_get_info(path1, TR_SYS_PATH_NO_FOLLOW, &info, nullptr);
    EXPECT_EQ(4, info.size);
    fd = tr_sys_file_open(path1, TR_SYS_FILE_WRITE | TR_SYS_FILE_APPEND, 0600, &err);
    EXPECT_NE(TR_BAD_SYS_FILE, fd);
    EXPECT_EQ(nullptr, err);
    tr_sys_file_write(fd, "s", 1, nullptr, nullptr); /* On *NIX, pointer is positioned on each write but not initially */
    auto n = uint64_t {};
    tr_sys_file_seek(fd, 0, TR_SEEK_CUR, &n, nullptr);
    EXPECT_EQ(5, n);
    tr_sys_file_close(fd, nullptr);

    /* File gets truncated */
    tr_sys_path_get_info(path1, TR_SYS_PATH_NO_FOLLOW, &info, nullptr);
    EXPECT_EQ(5, info.size);
    fd = tr_sys_file_open(path1, TR_SYS_FILE_WRITE | TR_SYS_FILE_TRUNCATE, 0600, &err);
    EXPECT_NE(TR_BAD_SYS_FILE, fd);
    EXPECT_EQ(nullptr, err);
    tr_sys_file_get_info(fd, &info, nullptr);
    EXPECT_EQ(0, info.size);
    tr_sys_file_close(fd, nullptr);
    tr_sys_path_get_info(path1, TR_SYS_PATH_NO_FOLLOW, &info, nullptr);
    EXPECT_EQ(0, info.size);

    /* TODO: symlink and hardlink tests */

    tr_sys_path_remove(path1, nullptr);

    tr_free(path1);
}

TEST_F(FileTest, fileReadWriteSeek)
{
    auto const test_dir = createTestDir(currentTestName());

    auto* path1 = tr_buildPath(test_dir.data(), "a", nullptr);
    auto const fd = tr_sys_file_open(path1, TR_SYS_FILE_READ | TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE, 0600, nullptr);

    uint64_t n;
    tr_error* err = nullptr;
    EXPECT_TRUE(tr_sys_file_seek(fd, 0, TR_SEEK_CUR, &n, &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_EQ(0, n);

    EXPECT_TRUE(tr_sys_file_write(fd, "test", 4, &n, &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_EQ(4, n);

    EXPECT_TRUE(tr_sys_file_seek(fd, 0, TR_SEEK_CUR, &n, &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_EQ(4, n);

    EXPECT_TRUE(tr_sys_file_seek(fd, 0, TR_SEEK_SET, &n, &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_EQ(0, n);

    auto buf = std::array<char, 100>{};
    EXPECT_TRUE(tr_sys_file_read(fd, buf.data(), buf.size(), &n, &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_EQ(4, n);

    EXPECT_EQ(0, memcmp("test", buf.data(), 4));

    EXPECT_TRUE(tr_sys_file_seek(fd, -3, TR_SEEK_CUR, &n, &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_EQ(1, n);

    EXPECT_TRUE(tr_sys_file_write(fd, "E", 1, &n, &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_EQ(1, n);

    EXPECT_TRUE(tr_sys_file_seek(fd, -2, TR_SEEK_CUR, &n, &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_EQ(0, n);

    EXPECT_TRUE(tr_sys_file_read(fd, buf.data(), buf.size(), &n, &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_EQ(4, n);

    EXPECT_EQ(0, memcmp("tEst", buf.data(), 4));

    EXPECT_TRUE(tr_sys_file_seek(fd, 0, TR_SEEK_END, &n, &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_EQ(4, n);

    EXPECT_TRUE(tr_sys_file_write(fd, " ok", 3, &n, &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_EQ(3, n);

    EXPECT_TRUE(tr_sys_file_seek(fd, 0, TR_SEEK_SET, &n, &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_EQ(0, n);

    EXPECT_TRUE(tr_sys_file_read(fd, buf.data(), buf.size(), &n, &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_EQ(7, n);

    EXPECT_EQ(0, memcmp("tEst ok", buf.data(), 7));

    EXPECT_TRUE(tr_sys_file_write_at(fd, "-", 1, 4, &n, &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_EQ(1, n);

    EXPECT_TRUE(tr_sys_file_read_at(fd, buf.data(), 5, 2, &n, &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_EQ(5, n);

    EXPECT_EQ(0, memcmp("st-ok", buf.data(), 5));

    tr_sys_file_close(fd, nullptr);

    tr_sys_path_remove(path1, nullptr);

    tr_free(path1);
}

TEST_F(FileTest, fileTruncate)
{
    auto const test_dir = createTestDir(currentTestName());

    auto* path1 = tr_buildPath(test_dir.c_str(), "a", nullptr);
    auto fd = tr_sys_file_open(path1, TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE, 0600, nullptr);

    tr_error* err = nullptr;
    EXPECT_TRUE(tr_sys_file_truncate(fd, 10, &err));
    EXPECT_EQ(nullptr, err);
    tr_sys_path_info info;
    tr_sys_file_get_info(fd, &info, nullptr);
    EXPECT_EQ(10, info.size);

    EXPECT_TRUE(tr_sys_file_truncate(fd, 20, &err));
    EXPECT_EQ(nullptr, err);
    tr_sys_file_get_info(fd, &info, nullptr);
    EXPECT_EQ(20, info.size);

    EXPECT_TRUE(tr_sys_file_truncate(fd, 0, &err));
    EXPECT_EQ(nullptr, err);
    tr_sys_file_get_info(fd, &info, nullptr);
    EXPECT_EQ(0, info.size);

    EXPECT_TRUE(tr_sys_file_truncate(fd, 50, &err));
    EXPECT_EQ(nullptr, err);

    tr_sys_file_close(fd, nullptr);

    tr_sys_path_get_info(path1, 0, &info, nullptr);
    EXPECT_EQ(50, info.size);

    fd = tr_sys_file_open(path1, TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE, 0600, nullptr);

    EXPECT_TRUE(tr_sys_file_truncate(fd, 25, &err));
    EXPECT_EQ(nullptr, err);

    tr_sys_file_close(fd, nullptr);

    tr_sys_path_get_info(path1, 0, &info, nullptr);
    EXPECT_EQ(25, info.size);

    tr_sys_path_remove(path1, nullptr);

    tr_free(path1);
}

TEST_F(FileTest, filePreallocate)
{
    auto const test_dir = createTestDir(currentTestName());

    auto* path1 = tr_buildPath(test_dir.data(), "a", nullptr);
    auto fd = tr_sys_file_open(path1, TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE, 0600, nullptr);

    tr_error* err = nullptr;
    auto prealloc_size = size_t{ 50 };
    if (tr_sys_file_preallocate(fd, prealloc_size, 0, &err))
    {
        EXPECT_EQ(nullptr, err);
        tr_sys_path_info info;
        tr_sys_file_get_info(fd, &info, nullptr);
        EXPECT_EQ(prealloc_size, info.size);
    }
    else
    {
        EXPECT_NE(nullptr, err);
        fprintf(stderr, "WARNING: [%s] unable to preallocate file (full): %s (%d)\n", __FUNCTION__, err->message, err->code);
        tr_error_clear(&err);
    }

    tr_sys_file_close(fd, nullptr);

    tr_sys_path_remove(path1, nullptr);

    fd = tr_sys_file_open(path1, TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE, 0600, nullptr);

    prealloc_size = 500 * 1024 * 1024;
    if (tr_sys_file_preallocate(fd, prealloc_size, TR_SYS_FILE_PREALLOC_SPARSE, &err))
    {
        EXPECT_EQ(nullptr, err);
        tr_sys_path_info info;
        tr_sys_file_get_info(fd, &info, nullptr);
        EXPECT_EQ(prealloc_size, info.size);
    }
    else
    {
        EXPECT_NE(nullptr, err);
        fprintf(stderr, "WARNING: [%s] unable to preallocate file (sparse): %s (%d)\n", __FUNCTION__, err->message, err->code);
        tr_error_clear(&err);
    }

    tr_sys_file_close(fd, nullptr);

    tr_sys_path_remove(path1, nullptr);

    tr_free(path1);
}

TEST_F(FileTest, map)
{
    auto const test_dir = createTestDir(currentTestName());

    auto* path1 = tr_buildPath(test_dir.data(), "a", nullptr);
    auto const contents = std::string { "test" };
    createFileWithContents(path1, contents.data());

    auto fd = tr_sys_file_open(path1, TR_SYS_FILE_READ | TR_SYS_FILE_WRITE, 0600, nullptr);

    tr_error* err = nullptr;
    auto map_len = contents.size();
    auto* view = static_cast<char*>(tr_sys_file_map_for_reading(fd, 0, map_len, &err));
    EXPECT_NE(nullptr, view);
    EXPECT_EQ(nullptr, err);
    EXPECT_EQ(contents, std::string(view, map_len));

#ifdef HAVE_UNIFIED_BUFFER_CACHE

    auto const contents_2 = std::string { "more" };
    auto n_written = uint64_t {};
    tr_sys_file_write_at(fd, contents_2.data(), contents_2.size(), 0, &n_written, &err);
    EXPECT_EQ(map_len, contents_2.size());
    EXPECT_EQ(map_len, n_written);
    EXPECT_EQ(nullptr, err);
    EXPECT_EQ(contents_2, std::string(view, map_len));

#endif

    EXPECT_TRUE(tr_sys_file_unmap(view, map_len, &err));
    EXPECT_EQ(nullptr, err);

    tr_sys_file_close(fd, nullptr);

    tr_sys_path_remove(path1, nullptr);

    tr_free(path1);
}

TEST_F(FileTest, fileUtilities)
{
    auto const test_dir = createTestDir(currentTestName());

    auto* path1 = tr_buildPath(test_dir.data(), "a", nullptr);
    auto const contents = std::string { "a\nbc\r\ndef\nghij\r\n\n\nklmno\r" };
    createFileWithContents(path1, contents.data());

    auto fd = tr_sys_file_open(path1, TR_SYS_FILE_READ, 0, nullptr);

    tr_error* err = nullptr;
    auto buffer = std::array<char, 16>{};
    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer.data(), buffer.size(), &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_STREQ("a", buffer.data());
    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer.data(), buffer.size(), &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_STREQ("bc", buffer.data());
    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer.data(), buffer.size(), &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_STREQ("def", buffer.data());
    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer.data(), buffer.size(), &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_STREQ("ghij", buffer.data());
    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer.data(), buffer.size(), &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_STREQ("", buffer.data());
    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer.data(), buffer.size(), &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_STREQ("", buffer.data());
    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer.data(), 4, &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_STREQ("klmn", buffer.data());
    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer.data(), buffer.size(), &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_STREQ("o", buffer.data());
    EXPECT_FALSE(tr_sys_file_read_line(fd, buffer.data(), buffer.size(), &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_STREQ("o", buffer.data()); // on EOF, buffer stays unchanged

    tr_sys_file_close(fd, nullptr);

    fd = tr_sys_file_open(path1, TR_SYS_FILE_READ | TR_SYS_FILE_WRITE | TR_SYS_FILE_TRUNCATE, 0, nullptr);

    EXPECT_TRUE(tr_sys_file_write_line(fd, "p", &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_TRUE(tr_sys_file_write_line(fd, "", &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_TRUE(tr_sys_file_write_line(fd, "qr", &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_TRUE(tr_sys_file_write_fmt(fd, "s%cu\r\n", &err, 't'));
    EXPECT_EQ(nullptr, err);
    EXPECT_TRUE(tr_sys_file_write_line(fd, "", &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_TRUE(tr_sys_file_write_line(fd, "", &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_TRUE(tr_sys_file_write_fmt(fd, "v%sy%d", &err, "wx", 2));
    EXPECT_EQ(nullptr, err);

    tr_sys_file_seek(fd, 0, TR_SEEK_SET, nullptr, nullptr);

    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer.data(), buffer.size(), &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_STREQ("p", buffer.data());
    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer.data(), buffer.size(), &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_STREQ("", buffer.data());
    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer.data(), buffer.size(), &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_STREQ("qr", buffer.data());
    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer.data(), buffer.size(), &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_STREQ("stu", buffer.data());
    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer.data(), buffer.size(), &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_STREQ("", buffer.data());
    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer.data(), buffer.size(), &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_STREQ("", buffer.data());
    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer.data(), buffer.size(), &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_STREQ("vwxy2", buffer.data());
    EXPECT_FALSE(tr_sys_file_read_line(fd, buffer.data(), buffer.size(), &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_STREQ("vwxy2", buffer.data()); // on EOF, buffer stays unchanged

    tr_sys_file_close(fd, nullptr);

    tr_sys_path_remove(path1, nullptr);

    tr_free(path1);
}

TEST_F(FileTest, dirCreate)
{
    auto const test_dir = createTestDir(currentTestName());

    auto* path1 = tr_buildPath(test_dir.data(), "a", nullptr);
    auto* path2 = tr_buildPath(path1, "b", nullptr);

    // Can create directory which has parent
    tr_error* err = nullptr;
    EXPECT_TRUE(tr_sys_dir_create(path1, 0, 0700, &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_TRUE(tr_sys_path_exists(path1, nullptr));
    EXPECT_TRUE(validatePermissions(path1, 0700));

    tr_sys_path_remove(path1, nullptr);
    createFileWithContents(path1, "test");

    // Can't create directory where file already exists
    EXPECT_FALSE(tr_sys_dir_create(path1, 0, 0700, &err));
    EXPECT_NE(nullptr, err);
    tr_error_clear(&err);
    EXPECT_FALSE(tr_sys_dir_create(path1, TR_SYS_DIR_CREATE_PARENTS, 0700, &err));
    EXPECT_NE(nullptr, err);
    tr_error_clear(&err);

    tr_sys_path_remove(path1, nullptr);

    // Can't create directory which has no parent
    EXPECT_FALSE(tr_sys_dir_create(path2, 0, 0700, &err));
    EXPECT_NE(nullptr, err);
    EXPECT_FALSE(tr_sys_path_exists(path2, nullptr));
    tr_error_clear(&err);

    // Can create directory with parent directories
    EXPECT_TRUE(tr_sys_dir_create(path2, TR_SYS_DIR_CREATE_PARENTS, 0751, &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_TRUE(tr_sys_path_exists(path1, nullptr));
    EXPECT_TRUE(tr_sys_path_exists(path2, nullptr));
    EXPECT_TRUE(validatePermissions(path1, 0751));
    EXPECT_TRUE(validatePermissions(path2, 0751));

    // Can create existing directory (no-op)
    EXPECT_TRUE(tr_sys_dir_create(path1, 0, 0700, &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_TRUE(tr_sys_dir_create(path1, TR_SYS_DIR_CREATE_PARENTS, 0700, &err));
    EXPECT_EQ(nullptr, err);

    tr_sys_path_remove(path2, nullptr);
    tr_sys_path_remove(path1, nullptr);

    tr_free(path2);
    tr_free(path1);
}

TEST_F(FileTest, dirRead)
{
    auto const test_dir = createTestDir(currentTestName());

    auto* path1 = tr_buildPath(test_dir.data(), "a", nullptr);
    auto* path2 = tr_buildPath(test_dir.data(), "b", nullptr);

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

    tr_sys_path_remove(path1, nullptr);
    testDirReadImpl(test_dir, &have1, &have2);
    EXPECT_FALSE(have1);
    EXPECT_TRUE(have2);

    tr_free(path2);
    tr_free(path1);
}

} // namespace test

} // namespace libtransmission
